#pragma once

#ifndef __DUMPER_H__
#define __DUMPER_H__

#include <switch.h>

#define DUMP_BUFFER_SIZE                (u64)0x100000		// 1 MiB (1048576 bytes)
#define ISTORAGE_PARTITION_CNT          2

#define FAT32_FILESIZE_LIMIT            (u64)0xFFFFFFFF     // 4 GiB - 1 (4294967295 bytes)

#define SPLIT_FILE_XCI_PART_SIZE        (u64)0xFFFF8000     // 4 GiB - 0x8000 (4294934528 bytes) (based on XCI-Cutter)
#define SPLIT_FILE_NSP_PART_SIZE        (u64)0xFFFF0000     // 4 GiB - 0x10000 (4294901760 bytes) (based on splitNSP.py)
#define SPLIT_FILE_GENERIC_PART_SIZE    SPLIT_FILE_XCI_PART_SIZE

#define CERT_OFFSET                     0x7000
#define CERT_SIZE                       0x200

#define SMOOTHING_FACTOR                (double)0.01

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator);
bool dumpCartridgeImage(FsDeviceOperator* fsOperator, bool isFat32, bool dumpCert, bool trimDump, bool calcCrc);
bool dumpApplicationNSP(FsDeviceOperator* fsOperator, bool isFat32, bool calcCrc, u32 appIndex);
bool dumpRawPartition(FsDeviceOperator* fsOperator, u32 partition, bool doSplitting);
bool dumpPartitionData(FsDeviceOperator* fsOperator, u32 partition);
bool dumpFileFromPartition(FsDeviceOperator* fsOperator, u32 partition, u32 file, char *filename);
bool dumpGameCertificate(FsDeviceOperator *fsOperator);

#endif
