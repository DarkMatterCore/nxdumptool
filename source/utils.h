/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
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

#include <switch.h>

#define APP_BASE_PATH                   "sdmc:/switch/nxdumptool/"

#define LOGFILE(fmt, ...)               utilsWriteLogMessage(__func__, fmt, ##__VA_ARGS__)

#define MEMBER_SIZE(type, member)       sizeof(((type*)NULL)->member)

#define MAX_ELEMENTS(x)                 ((sizeof((x))) / (sizeof((x)[0])))

#define ALIGN_DOWN(x, y)                ((x) & ~((y) - 1))
#define ALIGN_UP(x, y)                  ((((y) - 1) + (x)) & ~((y) - 1))

#define BIS_SYSTEM_PARTITION_MOUNT_NAME "sys:"

#define NPDM_META_MAGIC                 0x4D455441  /* "META" */

typedef enum {
    UtilsCustomFirmwareType_Unknown    = 0,
    UtilsCustomFirmwareType_Atmosphere = 1,
    UtilsCustomFirmwareType_SXOS       = 2,
    UtilsCustomFirmwareType_ReiNX      = 3
} UtilsCustomFirmwareType;

typedef struct {
    u16 major : 6;
    u16 minor : 6;
    u16 micro : 4;
    u16 bugfix;
} TitleVersion;




u64 utilsHidKeysAllDown(void);
u64 utilsHidKeysAllHeld(void);

void utilsWaitForButtonPress(void);

void utilsWriteLogMessage(const char *func_name, const char *fmt, ...);

void utilsOverclockSystem(bool restore);

bool utilsInitializeResources(void);
void utilsCloseResources(void);

u8 utilsGetCustomFirmwareType(void);    ///< UtilsCustomFirmwareType.

void utilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size);

FsStorage *utilsGetEmmcBisSystemPartitionStorage(void);

static inline void utilsSleep(u64 seconds)
{
    if (seconds) svcSleepThread(seconds * (u64)1000000000);
}



#endif /* __UTILS_H__ */
