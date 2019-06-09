#pragma once

#ifndef __DUMPER_H__
#define __DUMPER_H__

#include <switch.h>
#include "util.h"

#define DUMP_BUFFER_SIZE                (u64)0x100000		// 1 MiB (1048576 bytes)
#define ISTORAGE_PARTITION_CNT          2

#define FAT32_FILESIZE_LIMIT            (u64)0xFFFFFFFF     // 4 GiB - 1 (4294967295 bytes)

#define SPLIT_FILE_XCI_PART_SIZE        (u64)0xFFFF8000     // 4 GiB - 0x8000 (4294934528 bytes) (based on XCI-Cutter)
#define SPLIT_FILE_NSP_PART_SIZE        (u64)0xFFFF0000     // 4 GiB - 0x10000 (4294901760 bytes) (based on splitNSP.py)
#define SPLIT_FILE_GENERIC_PART_SIZE    SPLIT_FILE_XCI_PART_SIZE

#define CERT_OFFSET                     0x7000
#define CERT_SIZE                       0x200

#define SMOOTHING_FACTOR                (double)0.05

void workaroundPartitionZeroAccess();
bool dumpCartridgeImage(bool isFat32, bool setXciArchiveBit, bool dumpCert, bool trimDump, bool calcCrc);
bool dumpNintendoSubmissionPackage(nspDumpType selectedNspDumpType, u32 titleIndex, bool isFat32, bool calcCrc, bool removeConsoleData, bool tiklessDump);
bool dumpRawHfs0Partition(u32 partition, bool doSplitting);
bool dumpHfs0PartitionData(u32 partition);
bool dumpFileFromHfs0Partition(u32 partition, u32 file, char *filename);
bool dumpExeFsSectionData(u32 appIndex, bool doSplitting);
bool dumpFileFromExeFsSection(u32 appIndex, u32 fileIndex, bool doSplitting);
bool dumpRomFsSectionData(u32 appIndex);
bool dumpFileFromRomFsSection(u32 appIndex, u32 file_offset, bool doSplitting);
bool dumpGameCardCertificate();

#endif
