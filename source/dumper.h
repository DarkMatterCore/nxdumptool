#pragma once

#ifndef __DUMPER_H__
#define __DUMPER_H__

#include <switch.h>

#define DUMP_BUFFER_SIZE				(u64)0x100000		// 1 MiB
#define ISTORAGE_PARTITION_CNT			2
#define SPLIT_FILE_MIN					(u64)0xEE6B2800		// 4 GB (4000000000 bytes)
#define SPLIT_FILE_2GiB					(u64)0x80000000

#define CERT_OFFSET						0x7000
#define CERT_SIZE						0x200

#define GAMECARD_HEADER_SIZE			0x200
#define GAMECARD_SIZE_ADDR				0x10D

#define HFS0_OFFSET_ADDR				0x130
#define HFS0_SIZE_ADDR					0x138
#define HFS0_MAGIC						0x48465330			// "HFS0"
#define HFS0_FILE_COUNT_ADDR			0x04
#define HFS0_ENTRY_TABLE_ADDR			0x10

#define GAMECARD_TYPE1_PARTITION_CNT	3					// "update" (0), "normal" (1), "update" (2)
#define GAMECARD_TYPE2_PARTITION_CNT	4					// "update" (0), "logo" (1), "normal" (2), "update" (3)
#define GAMECARD_TYPE(x)				((x) == GAMECARD_TYPE1_PARTITION_CNT ? "Type 0x01" : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? "Type 0x02" : "Unknown"))

#define GAMECARD_SIZE_1GiB				(u64)0x40000000
#define GAMECARD_SIZE_2GiB				(u64)0x80000000
#define GAMECARD_SIZE_4GiB				(u64)0x100000000
#define GAMECARD_SIZE_8GiB				(u64)0x200000000
#define GAMECARD_SIZE_16GiB				(u64)0x400000000
#define GAMECARD_SIZE_32GiB				(u64)0x800000000

#define bswap_32(a)						((((a) << 24) & 0xff000000) | (((a) << 8) & 0xff0000) | (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))

typedef struct
{
	u64 file_offset;
	u64 file_size;
	u32 filename_offset;
	u32 hashed_region_size;
	u64 reserved;
	u8 hashed_region_sha256[0x20];
} PACKED hfs0_entry_table;

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator);
bool getRootHfs0Header(FsDeviceOperator* fsOperator);
bool getHsf0PartitionDetails(u32 partition, u64 *out_offset, u64 *out_size);
bool dumpGameCartridge(FsDeviceOperator* fsOperator, bool isFat32, bool dumpCert, bool addPadding);
bool dumpRawPartition(FsDeviceOperator* fsOperator, u32 partition, bool doSplitting);
bool openPartitionFs(FsFileSystem* ret, FsDeviceOperator* fsOperator, u32 partition);
bool copyFile(const char* source, const char* dest, bool doSplitting);
bool copyDirectory(const char* source, const char* dest, bool doSplitting);
void removeDirectory(const char *path);
bool getDirectorySize(const char *path, u64 *out_size);
bool dumpPartitionData(FsDeviceOperator* fsOperator, u32 partition);
bool mountViewPartition(FsDeviceOperator *fsOperator, u32 partition);
bool dumpGameCertificate(FsDeviceOperator *fsOperator);

#endif
