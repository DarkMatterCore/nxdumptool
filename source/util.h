#pragma once

#ifndef __UTIL_H__
#define __UTIL_H__

#include <switch.h>

#define APP_VERSION "1.0.5"
#define NAME_BUF_LEN 4096
#define SOCK_BUFFERSIZE 65536

bool isGameCardInserted(FsDeviceOperator* o);

void syncDisplay();

void delay(u8 seconds);

bool getGameCardTitleIDAndVersion(u64 *titleID, u32 *version);

void convertTitleVersionToDecimal(u32 version, char *versionBuf, int versionBufSize);

bool getGameCardControlNacp(u64 titleID, char *nameBuf, int nameBufSize, char *authorBuf, int authorBufSize);

int getSdCardFreeSpace(u64 *out);

void convertSize(u64 size, char *out, int bufsize);

void waitForButtonPress();

bool isDirectory(char *path);

void addString(char **filenames, int *filenamesCount, char **nextFilename, const char *string);

void getDirectoryContents(char *filenameBuffer, char **filenames, int *filenamesCount, const char *directory, bool skipParent);

void gameCardDumpNSWDBCheck(u32 crc);

void updateNSWDBXml();

void updateApplication();

void removeIllegalCharacters(char *name);

void strtrim(char *str);

#endif
