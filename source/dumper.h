#pragma once

#ifndef __DUMPER_H__
#define __DUMPER_H__

#include <switch.h>
#include "util.h"

#define FAT32_FILESIZE_LIMIT            (u64)0xFFFFFFFF             // 4 GiB - 1 (4294967295 bytes)

#define SPLIT_FILE_XCI_PART_SIZE        (u64)0xFFFF8000             // 4 GiB - 0x8000 (4294934528 bytes) (based on XCI-Cutter)
#define SPLIT_FILE_NSP_PART_SIZE        (u64)0xFFFF0000             // 4 GiB - 0x10000 (4294901760 bytes) (based on splitNSP.py)
#define SPLIT_FILE_GENERIC_PART_SIZE    SPLIT_FILE_NSP_PART_SIZE
#define SPLIT_FILE_SEQUENTIAL_SIZE      (u64)0x40000000             // 1 GiB (used for sequential dumps when there's not enough storage space available)

#define CERT_OFFSET                     0x7000
#define CERT_SIZE                       0x200

typedef struct {
    bool keepCert;                                  // Original value for the "Keep certificate" option. Overrides the selected setting in the current session
    bool trimDump;                                  // Original value for the "Trim output dump" option. Overrides the selected setting in the current session
    bool calcCrc;                                   // "CRC32 checksum calculation + dump verification" option
    u8 partNumber;                                  // Next part number
    u32 partitionIndex;                             // IStorage partition index
    u64 partitionOffset;                            // IStorage partition offset
    u32 certCrc;                                    // CRC32 checksum accumulator (XCI with cert). Only used if calcCrc == true and keepCert == true
    u32 certlessCrc;                                // CRC32 checksum accumulator (certless XCI). Only used if calcCrc == true
} PACKED sequentialXciCtx;

typedef struct {
    NcmStorageId storageId;                          // Source storage from which the data is dumped
    bool removeConsoleData;                         // Original value for the "Remove console specific data" option. Overrides the selected setting in the current session
    bool tiklessDump;                               // Original value for the "Generate ticket-less dump" option. Overrides the selected setting in the current session. Ignored if removeConsoleData == false
    bool npdmAcidRsaPatch;                          // Original value for the "Change NPDM RSA key/sig in Program NCA" option. Overrides the selected setting in the current session
    bool preInstall;                                // Indicates if we're dealing with a preinstalled title - e.g. if the user already accepted the missing ticket prompt
    u8 partNumber;                                  // Next part number
    u32 nspFileCount;                               // PFS0 file count
    u32 ncaCount;                                   // NCA count
    u32 fileIndex;                                  // Current PFS0 file entry index
    u64 fileOffset;                                 // Current PFS0 file entry offset
    Sha256Context hashCtx;                          // Current NCA SHA-256 checksum context. Only used when dealing with the same NCA between different parts
    u8 programNcaHeaderMod[NCA_FULL_HEADER_LENGTH]; // Modified + reencrypted Program NCA header. Only needed if the NPDM signature in the Program NCA header is replaced (it uses cryptographically secure random numbers). The RSA public key used in the ACID section from the main.npdm file is constant, so we don't need to keep track of that
} PACKED sequentialNspCtx;

typedef struct {
    bool enabled;
    nspDumpType titleType;
    u32 titleIndex;
    u64 contentSize;
    char *contentSizeStr;
    char nspFilename[NAME_BUF_LEN];
    char truncatedNspFilename[NAME_BUF_LEN];
} batchEntry;

bool dumpNXCardImage(xciOptions *xciDumpCfg);
int dumpNintendoSubmissionPackage(nspDumpType selectedNspDumpType, u32 titleIndex, nspOptions *nspDumpCfg, bool batch);
int dumpNintendoSubmissionPackageBatch(batchOptions *batchDumpCfg);
bool dumpRawHfs0Partition(u32 partition, bool doSplitting);
bool dumpHfs0PartitionData(u32 partition, bool doSplitting);
bool dumpFileFromHfs0Partition(u32 partition, u32 fileIndex, char *filename, bool doSplitting);
bool dumpExeFsSectionData(u32 titleIndex, bool usePatch, ncaFsOptions *exeFsDumpCfg);
bool dumpFileFromExeFsSection(u32 titleIndex, u32 fileIndex, bool usePatch, ncaFsOptions *exeFsDumpCfg);
bool dumpRomFsSectionData(u32 titleIndex, selectedRomFsType curRomFsType, ncaFsOptions *romFsDumpCfg);
bool dumpFileFromRomFsSection(u32 titleIndex, u32 file_offset, selectedRomFsType curRomFsType, ncaFsOptions *romFsDumpCfg);
bool dumpCurrentDirFromRomFsSection(u32 titleIndex, selectedRomFsType curRomFsType, ncaFsOptions *romFsDumpCfg);
bool dumpGameCardCertificate();
bool dumpTicketFromTitle(u32 titleIndex, selectedTicketType curTikType, ticketOptions *tikDumpCfg);

#endif
