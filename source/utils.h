/*
 * utils.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __UTILS_H__
#define __UTILS_H__

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <threads.h>
#include <stdatomic.h>
#include <switch.h>

#define APP_BASE_PATH                   "sdmc:/switch/nxdumptool/"

#define MEMBER_SIZE(type, member)       sizeof(((type*)NULL)->member)

#define MAX_ELEMENTS(x)                 ((sizeof((x))) / (sizeof((x)[0])))

#define LOGFILE(fmt, ...)               utilsWriteMessageToLogFile(__func__, fmt, ##__VA_ARGS__)
#define LOGBUF(dst, dst_size, fmt, ...) utilsWriteMessageToLogBuffer(dst, dst_size, __func__, fmt, ##__VA_ARGS__)

#define ALIGN_DOWN(x, y)                ((x) & ~((y) - 1))
#define ALIGN_UP(x, y)                  ((((y) - 1) + (x)) & ~((y) - 1))
#define IS_ALIGNED(x, y)                (((x) & ((y) - 1)) == 0)

#define BIS_SYSTEM_PARTITION_MOUNT_NAME "sys:"

#define KEY_NONE                        0



/// Need to move this to npdm.c/h eventually.
#define NPDM_META_MAGIC                 0x4D455441  /* "META". */







typedef enum {
    UtilsInputType_Down = 0,
    UtilsInputType_Held = 1
} UtilsInputType;

typedef enum {
    UtilsCustomFirmwareType_Unknown    = 0,
    UtilsCustomFirmwareType_Atmosphere = 1,
    UtilsCustomFirmwareType_SXOS       = 2,
    UtilsCustomFirmwareType_ReiNX      = 3
} UtilsCustomFirmwareType;

bool utilsInitializeResources(void);
void utilsCloseResources(void);

u64 utilsReadInput(u8 input_type);
void utilsWaitForButtonPress(u64 flag);

void utilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...);
void utilsWriteMessageToLogBuffer(char *dst, size_t dst_size, const char *func_name, const char *fmt, ...);
void utilsWriteLogBufferToLogFile(const char *src);
void utilsLogFileMutexControl(bool lock);

void utilsReplaceIllegalCharacters(char *str, bool ascii_only);

void utilsTrimString(char *str);

void utilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size);

bool utilsGetFreeSdCardSpace(u64 *out);
bool utilsGetFreeFileSystemSpace(FsFileSystem *fs, u64 *out);

bool utilsCheckIfFileExists(const char *path);

bool utilsCreateConcatenationFile(const char *path);

bool utilsAppletModeCheck(void);

void utilsChangeHomeButtonBlockStatus(bool block);

u8 utilsGetCustomFirmwareType(void);    ///< UtilsCustomFirmwareType.

FsStorage *utilsGetEmmcBisSystemPartitionStorage(void);

void utilsOverclockSystem(bool overclock);

NX_INLINE void utilsSleep(u64 seconds)
{
    if (seconds) svcSleepThread(seconds * (u64)1000000000);
}

#endif /* __UTILS_H__ */
