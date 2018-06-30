#pragma once

#ifndef __DUMPER_H__
#define __DUMPER_H__

#include <switch.h>

#define DUMP_BUFFER_SIZE				(u64)0x100000		// 1 MiB
#define ISTORAGE_PARTITION_CNT			2
#define SPLIT_FILE_MIN					(u64)0xEE6B2800		// 4 GB (4000000000 bytes)
#define SPLIT_FILE_2GiB					(u64)0x80000000

#define MEDIA_UNIT_SIZE					0x200

#define CERT_OFFSET						0x7000
#define CERT_SIZE						0x200

#define GAMECARD_HEADER_SIZE			0x200
#define GAMECARD_SIZE_ADDR				0x10D
#define GAMECARD_DATAEND_ADDR			0x118

#define HFS0_OFFSET_ADDR				0x130
#define HFS0_SIZE_ADDR					0x138
#define HFS0_MAGIC						0x48465330			// "HFS0"
#define HFS0_FILE_COUNT_ADDR			0x04
#define HFS0_ENTRY_TABLE_ADDR			0x10

#define GAMECARD_TYPE1_PARTITION_CNT	3					// "update" (0), "normal" (1), "update" (2)
#define GAMECARD_TYPE2_PARTITION_CNT	4					// "update" (0), "logo" (1), "normal" (2), "update" (3)
#define GAMECARD_TYPE(x)				((x) == GAMECARD_TYPE1_PARTITION_CNT ? "Type 0x01" : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? "Type 0x02" : "Unknown"))
#define GAMECARD_TYPE1_PART_NAMES(x)	((x) == 0 ? "Update" : ((x) == 1 ? "Normal" : ((x) == 2 ? "Secure" : "Unknown")))
#define GAMECARD_TYPE2_PART_NAMES(x)	((x) == 0 ? "Update" : ((x) == 1 ? "Logo" : ((x) == 2 ? "Normal" : ((x) == 3 ? "Secure" : "Unknown"))))
#define GAMECARD_PARTITION_NAME(x, y)	((x) == GAMECARD_TYPE1_PARTITION_CNT ? GAMECARD_TYPE1_PART_NAMES(y) : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? GAMECARD_TYPE2_PART_NAMES(y) : "Unknown"))

#define GAMECARD_SIZE_1GiB				(u64)0x40000000
#define GAMECARD_SIZE_2GiB				(u64)0x80000000
#define GAMECARD_SIZE_4GiB				(u64)0x100000000
#define GAMECARD_SIZE_8GiB				(u64)0x200000000
#define GAMECARD_SIZE_16GiB				(u64)0x400000000
#define GAMECARD_SIZE_32GiB				(u64)0x800000000

#define GAMECARD_UPDATE_TITLEID			(u64)0x0100000000000816

#define SYSUPDATE_100					(u32)450
#define SYSUPDATE_200					(u32)65796
#define SYSUPDATE_210					(u32)131162
#define SYSUPDATE_220					(u32)196628
#define SYSUPDATE_230					(u32)262164
#define SYSUPDATE_300					(u32)201327002
#define SYSUPDATE_301					(u32)201392178
#define SYSUPDATE_302					(u32)201457684
#define SYSUPDATE_400					(u32)268435656
#define SYSUPDATE_401					(u32)268501002
#define SYSUPDATE_410					(u32)269484082
#define SYSUPDATE_500					(u32)335544750
#define SYSUPDATE_501					(u32)335609886
#define SYSUPDATE_502					(u32)335675432
#define SYSUPDATE_510					(u32)336592976

#define bswap_32(a)						((((a) << 24) & 0xff000000) | (((a) << 8) & 0xff0000) | (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))
#define round_up(x, y)					((x) + (((y) - ((x) % (y))) % (y)))			// Aligns 'x' bytes to a 'y' bytes boundary

#define SMOOTHING_FACTOR				(double)0.05

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
bool dumpGameCartridge(FsDeviceOperator* fsOperator, bool isFat32, bool dumpCert, bool trimDump, bool calcCrc);
bool dumpRawPartition(FsDeviceOperator* fsOperator, u32 partition, bool doSplitting);
bool openPartitionFs(FsFileSystem* ret, FsDeviceOperator* fsOperator, u32 partition);
bool copyFile(const char* source, const char* dest, bool doSplitting, bool calcEta);
bool copyDirectory(const char* source, const char* dest, bool doSplitting);
void removeDirectory(const char *path);
bool getDirectorySize(const char *path, u64 *out_size);
bool dumpPartitionData(FsDeviceOperator* fsOperator, u32 partition);
bool mountViewPartition(FsDeviceOperator *fsOperator, u32 partition);
bool dumpGameCertificate(FsDeviceOperator *fsOperator);

#endif
