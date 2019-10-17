#pragma once

#ifndef __DUMPER_H__
#define __DUMPER_H__

#include <switch.h>
#include "util.h"

#define ISTORAGE_PARTITION_CNT          2

#define FAT32_FILESIZE_LIMIT            (u64)0xFFFFFFFF             // 4 GiB - 1 (4294967295 bytes)

#define SPLIT_FILE_XCI_PART_SIZE        (u64)0xFFFF8000             // 4 GiB - 0x8000 (4294934528 bytes) (based on XCI-Cutter)
#define SPLIT_FILE_NSP_PART_SIZE        (u64)0xFFFF0000             // 4 GiB - 0x10000 (4294901760 bytes) (based on splitNSP.py)
#define SPLIT_FILE_GENERIC_PART_SIZE    SPLIT_FILE_NSP_PART_SIZE
#define SPLIT_FILE_SEQUENTIAL_SIZE      (u64)0x40000000             // 1 GiB (used for sequential dumps when there's not enough storage space available)

#define CERT_OFFSET                     0x7000
#define CERT_SIZE                       0x200

#define SMOOTHING_FACTOR                (double)0.1

#define CANCEL_BTN_SEC_HOLD             2                           // The cancel button must be held for at least CANCEL_BTN_SEC_HOLD seconds to cancel an ongoing operation

#define DUMP_NSP_CRC_WAIT               5                           // The user must wait for at least DUMP_NSP_CRC_WAIT seconds before the CRC32 checksum calculation process starts after the NSP dump process is finished

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
    FsStorageId storageId;                          // Source storage from which the data is dumped
    bool removeConsoleData;                         // Original value for the "Remove console specific data" option. Overrides the selected setting in the current session
    bool tiklessDump;                               // Original value for the "Generate ticket-less dump" option. Overrides the selected setting in the current session. Ignored if removeConsoleData == false
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
    char nspFilename[NAME_BUF_LEN];
    char truncatedNspFilename[NAME_BUF_LEN];
} batchEntry;

void workaroundPartitionZeroAccess();
bool dumpCartridgeImage(xciOptions *xciDumpCfg);
bool dumpNintendoSubmissionPackage(nspDumpType selectedNspDumpType, u32 titleIndex, nspOptions *nspDumpCfg, bool batch);
bool dumpNintendoSubmissionPackageBatch(batchOptions *batchDumpCfg);
bool dumpRawHfs0Partition(u32 partition, bool doSplitting);
bool dumpHfs0PartitionData(u32 partition, bool doSplitting);
bool dumpFileFromHfs0Partition(u32 partition, u32 file, char *filename, bool doSplitting);
bool dumpExeFsSectionData(u32 titleIndex, bool usePatch, bool doSplitting);
bool dumpFileFromExeFsSection(u32 titleIndex, u32 fileIndex, bool usePatch, bool doSplitting);
bool dumpRomFsSectionData(u32 titleIndex, selectedRomFsType curRomFsType, bool doSplitting);
bool dumpFileFromRomFsSection(u32 titleIndex, u32 file_offset, selectedRomFsType curRomFsType, bool doSplitting);
bool dumpCurrentDirFromRomFsSection(u32 titleIndex, selectedRomFsType curRomFsType, bool doSplitting);
bool dumpGameCardCertificate();

#endif
