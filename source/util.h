#pragma once

#ifndef __UTIL_H__
#define __UTIL_H__

#include <switch.h>

#define KiB                     (1024.0)
#define MiB                     (1024.0 * KiB)
#define GiB                     (1024.0 * MiB)

#define APP_VERSION             "1.0.7"

#define NAME_BUF_LEN            4096

#define SOCK_BUFFERSIZE         65536

#define META_DATABASE_FILTER    0x80	// Regular Application

#define FILENAME_BUFFER_SIZE    (1024 * 512)  // 512 KiB
#define FILENAME_MAX_CNT        2048

bool isGameCardInserted();

void fsGameCardDetectionThreadFunc(void *arg);

void delay(u8 seconds);

void convertTitleVersionToDecimal(u32 version, char *versionBuf, int versionBufSize);

void removeIllegalCharacters(char *name);

void strtrim(char *str);

void loadGameCardInfo();

int getSdCardFreeSpace(u64 *out);

void convertSize(u64 size, char *out, int bufsize);

void waitForButtonPress();

void addStringToFilenameBuffer(const char *string, char **nextFilename);

void removeDirectory(const char *path);

void gameCardDumpNSWDBCheck(u32 crc);

void updateNSWDBXml();

void updateApplication();

#endif
