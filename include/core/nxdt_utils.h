/*
 * nxdt_utils.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __NXDT_UTILS_H__
#define __NXDT_UTILS_H__

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_BASE_PATH                   "sdmc:/switch/" APP_TITLE "/"

#define MEMBER_SIZE(type, member)       sizeof(((type*)NULL)->member)

#define MAX_ELEMENTS(x)                 ((sizeof((x))) / (sizeof((x)[0])))

#define BIT_LONG(n)                     (1UL << (n))

#define ALIGN_UP(x, y)                  (((x) + ((y) - 1)) & ~((y) - 1))
#define ALIGN_DOWN(x, y)                ((x) & ~((y) - 1))
#define IS_ALIGNED(x, y)                (((x) & ((y) - 1)) == 0)

#define IS_POWER_OF_TWO(x)              (((x) & ((x) - 1)) == 0)

#define BIS_SYSTEM_PARTITION_MOUNT_NAME "sys:"

/// Used to determine which CFW is the application running under.
typedef enum {
    UtilsCustomFirmwareType_Unknown    = 0,
    UtilsCustomFirmwareType_Atmosphere = 1,
    UtilsCustomFirmwareType_SXOS       = 2,
    UtilsCustomFirmwareType_ReiNX      = 3
} UtilsCustomFirmwareType;

/// Resource (de)initialization.
/// Called at program startup and exit.
bool utilsInitializeResources(void);
void utilsCloseResources(void);

/// Thread management functions.
bool utilsCreateThread(Thread *out_thread, ThreadFunc func, void *arg, int cpu_id);
void utilsJoinThread(Thread *thread);

/// Returns true if the application is running under a development unit.
bool utilsIsDevelopmentUnit(void);

/// Formats a string and appends it to the provided buffer.
/// If the buffer isn't big enough to hold both its current contents and the new formatted string, it will be resized.
__attribute__((format(printf, 3, 4))) bool utilsAppendFormattedStringToBuffer(char **dst, size_t *dst_size, const char *fmt, ...);

/// Replaces illegal FAT characters in the provided string with underscores.
/// If 'ascii_only' is set to true, all characters outside the (0x20,0x7E] range will also be replaced with underscores.
void utilsReplaceIllegalCharacters(char *str, bool ascii_only);

/// Trims whitespace characters from the provided string.
void utilsTrimString(char *str);

/// Generates a lowercase hex string representation of the binary data stored in 'src' and stores it in 'dst'.
void utilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size);

/// Formats the provided 'size' value to a human-readable size string and stores it in 'dst'.
void utilsGenerateFormattedSizeString(u64 size, char *dst, size_t dst_size);

/// Saves the total size and free space available from the filesystem pointed to by the input path (e.g. "sdmc:/") to 'out_total' and 'out_free', respectively.
/// Either 'out_total' or 'out_free' can be NULL, but at least one of them must be a valid pointer.
/// Returns false if there's an error.
bool utilsGetFileSystemStatsByPath(const char *path, u64 *out_total, u64 *out_free);

/// Returns a pointer to the FsFileSystem object for the SD card.
FsFileSystem *utilsGetSdCardFileSystemObject(void);

/// Commits SD card filesystem changes.
/// Must be used after closing a file handle from the SD card.
bool utilsCommitSdCardFileSystemChanges(void);

/// Returns true if a file exists.
bool utilsCheckIfFileExists(const char *path);

/// Deletes a ConcatenationFile located at the input path.
void utilsRemoveConcatenationFile(const char *path);

/// Creates a ConcatenationFile at the input path.
bool utilsCreateConcatenationFile(const char *path);

/// Creates a full directory tree using the provided path.
/// If 'create_last_element' is true, the last element from the provided path will be created as well.
void utilsCreateDirectoryTree(const char *path, bool create_last_element);

/// Returns a pointer to a dynamically allocated string that holds the full path formed by the provided arguments.
char *utilsGeneratePath(const char *prefix, const char *filename, const char *extension);

/// Returns true if the application is running under Applet Mode.
bool utilsAppletModeCheck(void);

/// (Un)blocks HOME button presses.
void utilsChangeHomeButtonBlockStatus(bool block);

/// Returns a UtilsCustomFirmwareType value.
u8 utilsGetCustomFirmwareType(void);

/// Returns a pointer to the FsStorage object for the eMMC BIS System partition.
FsStorage *utilsGetEmmcBisSystemPartitionStorage(void);

/// Enables/disables CPU/MEM overclocking.
void utilsOverclockSystem(bool overclock);

/// Simple wrapper to sleep the current thread for a specific number of full seconds.
NX_INLINE void utilsSleep(u64 seconds)
{
    if (seconds) svcSleepThread(seconds * (u64)1000000000);
}

#ifdef __cplusplus
}
#endif

#endif /* __NXDT_UTILS_H__ */
