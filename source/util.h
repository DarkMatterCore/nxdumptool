#pragma once

#ifndef __UTIL_H__
#define __UTIL_H__

#include <switch.h>
#include "nca.h"

#define KiB                             (1024.0)
#define MiB                             (1024.0 * KiB)
#define GiB                             (1024.0 * MiB)

#define NAME_BUF_LEN                    4096

#define SOCK_BUFFERSIZE                 65536

#define META_DB_REGULAR_APPLICATION     0x80
#define META_DB_PATCH                   0x81
#define META_DB_ADDON                   0x82

#define APPLICATION_PATCH_BITMASK       (u64)0x800
#define APPLICATION_ADDON_BITMASK       (u64)0xFFFFFFFFFFFF0000

#define FILENAME_MAX_CNT                20000
#define FILENAME_BUFFER_SIZE            (512 * FILENAME_MAX_CNT)   // 10000 KiB

#define NACP_APPNAME_LEN                0x200
#define NACP_AUTHOR_LEN                 0x100
#define VERSION_STR_LEN                 0x40

#define GAMECARD_WAIT_TIME              3                           // 3 seconds

#define GAMECARD_HEADER_SIZE            0x200
#define GAMECARD_SIZE_ADDR              0x10D
#define GAMECARD_DATAEND_ADDR           0x118

#define HFS0_OFFSET_ADDR                0x130
#define HFS0_SIZE_ADDR                  0x138
#define HFS0_MAGIC                      (u32)0x48465330             // "HFS0"
#define HFS0_FILE_COUNT_ADDR            0x04
#define HFS0_STR_TABLE_SIZE_ADDR        0x08
#define HFS0_ENTRY_TABLE_ADDR           0x10

#define MEDIA_UNIT_SIZE                 0x200

#define GAMECARD_TYPE1_PARTITION_CNT    3                           // "update" (0), "normal" (1), "update" (2)
#define GAMECARD_TYPE2_PARTITION_CNT    4                           // "update" (0), "logo" (1), "normal" (2), "update" (3)
#define GAMECARD_TYPE(x)                ((x) == GAMECARD_TYPE1_PARTITION_CNT ? "Type 0x01" : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? "Type 0x02" : "Unknown"))
#define GAMECARD_TYPE1_PART_NAMES(x)    ((x) == 0 ? "Update" : ((x) == 1 ? "Normal" : ((x) == 2 ? "Secure" : "Unknown")))
#define GAMECARD_TYPE2_PART_NAMES(x)    ((x) == 0 ? "Update" : ((x) == 1 ? "Logo" : ((x) == 2 ? "Normal" : ((x) == 3 ? "Secure" : "Unknown"))))
#define GAMECARD_PARTITION_NAME(x, y)   ((x) == GAMECARD_TYPE1_PARTITION_CNT ? GAMECARD_TYPE1_PART_NAMES(y) : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? GAMECARD_TYPE2_PART_NAMES(y) : "Unknown"))

#define HFS0_TO_ISTORAGE_IDX(x, y)      ((x) == GAMECARD_TYPE1_PARTITION_CNT ? ((y) < 2 ? 0 : 1) : ((y) < 3 ? 0 : 1))

#define GAMECARD_SIZE_1GiB              (u64)0x40000000
#define GAMECARD_SIZE_2GiB              (u64)0x80000000
#define GAMECARD_SIZE_4GiB              (u64)0x100000000
#define GAMECARD_SIZE_8GiB              (u64)0x200000000
#define GAMECARD_SIZE_16GiB             (u64)0x400000000
#define GAMECARD_SIZE_32GiB             (u64)0x800000000

/* Reference: https://switchbrew.org/wiki/Title_list */
#define GAMECARD_UPDATE_TITLEID         (u64)0x0100000000000816

#define SYSUPDATE_100                   (u32)450
#define SYSUPDATE_200                   (u32)65796
#define SYSUPDATE_210                   (u32)131162
#define SYSUPDATE_220                   (u32)196628
#define SYSUPDATE_230                   (u32)262164
#define SYSUPDATE_300                   (u32)201327002
#define SYSUPDATE_301                   (u32)201392178
#define SYSUPDATE_302                   (u32)201457684
#define SYSUPDATE_400                   (u32)268435656
#define SYSUPDATE_401                   (u32)268501002
#define SYSUPDATE_410                   (u32)269484082
#define SYSUPDATE_500                   (u32)335544750
#define SYSUPDATE_501                   (u32)335609886
#define SYSUPDATE_502                   (u32)335675432
#define SYSUPDATE_510                   (u32)336592976
#define SYSUPDATE_600                   (u32)402653544
#define SYSUPDATE_601                   (u32)402718730
#define SYSUPDATE_610                   (u32)403701850
#define SYSUPDATE_620                   (u32)404750376
#define SYSUPDATE_700                   (u32)469762248
#define SYSUPDATE_701                   (u32)469827614
#define SYSUPDATE_800                   (u32)536871442

#define NACP_ICON_SQUARE_DIMENSION      256
#define NACP_ICON_DOWNSCALED            96

#define bswap_32(a)                     ((((a) << 24) & 0xff000000) | (((a) << 8) & 0xff0000) | (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))
#define round_up(x, y)                  ((x) + (((y) - ((x) % (y))) % (y)))			// Aligns 'x' bytes to a 'y' bytes boundary

typedef struct {
    u64 file_offset;
    u64 file_size;
    u32 filename_offset;
    u32 hashed_region_size;
    u64 reserved;
    u8 hashed_region_sha256[0x20];
} PACKED hfs0_entry_table;

typedef struct {
    int line_offset;
    u64 totalSize;
    char totalSizeStr[32];
    u64 curOffset;
    char curOffsetStr[32];
    u8 progress;
    u64 start;
    u64 now;
    u64 remainingTime;
    char etaInfo[32];
    double lastSpeed;
    double averageSpeed;
} PACKED progress_ctx_t;

bool isGameCardInserted();

void fsGameCardDetectionThreadFunc(void *arg);

void delay(u8 seconds);

void formatETAString(u64 curTime, char *output, u32 outSize);

void convertTitleVersionToDecimal(u32 version, char *versionBuf, size_t versionBufSize);

void removeIllegalCharacters(char *name);

void strtrim(char *str);

void freeStringsPtr(char **var);

void initRomFsContext();

void freeRomFsContext();

void freeGameCardInfo();

void loadGameCardInfo();

bool getHfs0EntryDetails(u8 *hfs0Header, u64 hfs0HeaderOffset, u64 hfs0HeaderSize, u32 num_entries, u32 entry_idx, bool isRoot, u32 partitionIndex, u64 *out_offset, u64 *out_size);

bool getPartitionHfs0Header(u32 partition);

bool getHfs0FileList(u32 partition);

u8 *getPartitionHfs0FileByName(FsStorage *gameCardStorage, const char *filename, u64 *outSize);

bool calculateRomFsExtractedDataSize(u64 *out);

bool readProgramNcaRomFs(u32 appIndex);

bool getRomFsFileList(u32 dir_offset);

int getSdCardFreeSpace(u64 *out);

void convertSize(u64 size, char *out, int bufsize);

char *generateDumpFullName();

char *generateNSPDumpName(nspDumpType selectedNspDumpType, u32 titleIndex);

void retrieveDescriptionForPatchOrAddOn(u64 titleID, u32 version, bool addOn, const char *prefix);

void waitForButtonPress();

void printProgressBar(progress_ctx_t *progressCtx, bool calcData, u64 chunkSize);

void setProgressBarError(progress_ctx_t *progressCtx);

void convertDataToHexString(const u8 *data, const u32 dataSize, char *outBuf, const u32 outBufSize);

void addStringToFilenameBuffer(const char *string, char **nextFilename);

void removeDirectory(const char *path);

void gameCardDumpNSWDBCheck(u32 crc);

void updateNSWDBXml();

void updateApplication();

#endif
