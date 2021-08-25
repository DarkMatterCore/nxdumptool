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

/* Included here for convenience. */
#include "nxdt_includes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Scoped lock macro. */
#define SCOPED_LOCK(mtx)        for(UtilsScopedLock ANONYMOUS_VARIABLE(scoped_lock) CLEANUP(utilsUnlockScope) = utilsLockScope(mtx); ANONYMOUS_VARIABLE(scoped_lock).cond; ANONYMOUS_VARIABLE(scoped_lock).cond = 0)

/* Scoped try lock macro. */
#define SCOPED_TRY_LOCK(mtx)    for(UtilsScopedLock ANONYMOUS_VARIABLE(scoped_lock) CLEANUP(utilsUnlockScope) = utilsTryLockScope(mtx); ANONYMOUS_VARIABLE(scoped_lock).cond; ANONYMOUS_VARIABLE(scoped_lock).cond = 0)

/// Used by scoped locks.
typedef struct {
    Mutex *mtx;
    bool lock;
    int cond;
} UtilsScopedLock;

/// Used to determine which CFW is the application running under.
typedef enum {
    UtilsCustomFirmwareType_Unknown    = 0,
    UtilsCustomFirmwareType_Atmosphere = 1,
    UtilsCustomFirmwareType_SXOS       = 2,
    UtilsCustomFirmwareType_ReiNX      = 3
} UtilsCustomFirmwareType;

/// Used to handle parsed data from a GitHub release JSON.
/// All strings are dynamically allocated.
typedef struct {
    struct json_object *obj;    ///< JSON object. Must be freed using json_object_put().
    const char *version;        ///< Pointer to the version string, referenced by obj.
    const char *commit_hash;    ///< Pointer to the commit hash string, referenced by obj.
    struct tm date;             ///< Release date.
    const char *changelog;      ///< Pointer to the changelog string, referenced by obj.
    const char *download_url;   ///< Pointer to the download URL string, referenced by obj.
} UtilsGitHubReleaseJsonData;

/// Resource initialization.
/// Called at program startup.
bool utilsInitializeResources(const int program_argc, const char **program_argv);

/// Resource deinitialization.
/// Called at program exit.
void utilsCloseResources(void);

/// Returns a pointer to the application launch path.
const char *utilsGetLaunchPath(void);

/// Returns a pointer to the FsFileSystem object for the SD card.
FsFileSystem *utilsGetSdCardFileSystemObject(void);

/// Commits SD card filesystem changes.
/// Must be used after closing a file handle from the SD card.
bool utilsCommitSdCardFileSystemChanges(void);

/// Returns a UtilsCustomFirmwareType value.
u8 utilsGetCustomFirmwareType(void);

/// Returns true if the application is running under a development unit.
bool utilsIsDevelopmentUnit(void);

/// Returns true if the application is running under applet mode.
bool utilsAppletModeCheck(void);

/// Returns a pointer to the FsStorage object for the eMMC BIS System partition.
FsStorage *utilsGetEmmcBisSystemPartitionStorage(void);

/// Enables/disables CPU/MEM overclocking.
void utilsOverclockSystem(bool overclock);

/// (Un)blocks HOME button presses and (un)sets screen dimming and auto sleep.
/// Must be called before starting long-running processes.
void utilsSetLongRunningProcessState(bool state);

/// Thread management functions.
bool utilsCreateThread(Thread *out_thread, ThreadFunc func, void *arg, int cpu_id);
void utilsJoinThread(Thread *thread);

/// Formats a string and appends it to the provided buffer.
/// If the buffer isn't big enough to hold both its current contents and the new formatted string, it will be resized.
__attribute__((format(printf, 3, 4))) bool utilsAppendFormattedStringToBuffer(char **dst, size_t *dst_size, const char *fmt, ...);

/// Replaces illegal FAT characters in the provided UTF-8 string with underscores.
/// If 'ascii_only' is set to true, all codepoints outside the (0x20,0x7E] range will also be replaced with underscores.
/// Replacements are performed on a per-codepoint basis, which means the string length can be reduced by this function.
void utilsReplaceIllegalCharacters(char *str, bool ascii_only);

/// Trims whitespace characters from the provided string.
void utilsTrimString(char *str);

/// Generates a hex string representation of the binary data stored in 'src' and stores it in 'dst'.
/// If 'uppercase' is true, uppercase characters will be used to generate the hex string. Otherwise, lowercase characters will be used.
void utilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size, bool uppercase);

/// Formats the provided 'size' value to a human-readable size string and stores it in 'dst'.
void utilsGenerateFormattedSizeString(double size, char *dst, size_t dst_size);

/// Saves the total size and free space available from the filesystem pointed to by the input path (e.g. "sdmc:/") to 'out_total' and 'out_free', respectively.
/// Either 'out_total' or 'out_free' can be NULL, but at least one of them must be a valid pointer.
/// Returns false if there's an error.
bool utilsGetFileSystemStatsByPath(const char *path, u64 *out_total, u64 *out_free);

/// Creates output directories in the specified device.
/// If 'device' is NULL, output directories will be created on the SD card.
void utilsCreateOutputDirectories(const char *device);

/// Returns true if a file exists.
bool utilsCheckIfFileExists(const char *path);

/// Deletes a ConcatenationFile located at the input path.
void utilsRemoveConcatenationFile(const char *path);

/// Creates a ConcatenationFile at the input path.
bool utilsCreateConcatenationFile(const char *path);

/// Creates a full directory tree using the provided path.
/// If 'create_last_element' is true, the last element from the provided path will be created as well.
void utilsCreateDirectoryTree(const char *path, bool create_last_element);

/// Returns a pointer to a dynamically allocated string that holds the full path formed by the provided arguments. Both path prefix and file extension are optional.
/// If any elements from the generated path exceed safe filesystem limits, each exceeding element will be truncated. Truncations, if needed, are performed on a per-codepoint basis (UTF-8).
/// If an extension is provided, it will always be preserved, regardless of any possible truncations being carried out.
/// Furthermore, if the full length for the generated path is >= FS_MAX_PATH, NULL will be returned.
char *utilsGeneratePath(const char *prefix, const char *filename, const char *extension);

/// Prints an error message using the standard console output and waits for the user to press a button.
void utilsPrintConsoleError(const char *msg);

/// Returns the current application updated state.
bool utilsGetApplicationUpdatedState(void);

/// Sets the application updated state to true, which makes utilsCloseResources() replace the application NRO.
void utilsSetApplicationUpdatedState(void);

/// Parses the provided GitHub release JSON data buffer.
/// The data from the output buffer must be freed using utilsFreeGitHubReleaseJsonData().
bool utilsParseGitHubReleaseJsonData(const char *json_buf, size_t json_buf_size, UtilsGitHubReleaseJsonData *out);

/// Parses the provided version string and compares it to the application version. Returns true if the application can be updated.
/// If both versions are equal, the provided commit hash is compared to our commit hash - if they're different, true will be returned.
bool utilsIsApplicationUpdatable(const char *version, const char *commit_hash);

/// Frees previously allocated data from a UtilsGitHubReleaseJsonData element.
NX_INLINE void utilsFreeGitHubReleaseJsonData(UtilsGitHubReleaseJsonData *data)
{
    if (!data) return;
    if (data->obj) json_object_put(data->obj);
    memset(data, 0, sizeof(UtilsGitHubReleaseJsonData));
}

/// Simple wrapper to sleep the current thread for a specific number of full seconds.
NX_INLINE void utilsSleep(u64 seconds)
{
    if (seconds) svcSleepThread(seconds * (u64)1000000000);
}

/// Wrappers used in scoped locks.
NX_INLINE UtilsScopedLock utilsLockScope(Mutex *mtx)
{
    UtilsScopedLock scoped_lock = { mtx, !mutexIsLockedByCurrentThread(mtx), 1 };
    if (scoped_lock.lock) mutexLock(scoped_lock.mtx);
    return scoped_lock;
}

NX_INLINE UtilsScopedLock utilsTryLockScope(Mutex *mtx)
{
    UtilsScopedLock scoped_lock = { mtx, !mutexIsLockedByCurrentThread(mtx), 1 };
    if (scoped_lock.lock) scoped_lock.cond = (int)mutexTryLock(scoped_lock.mtx);
    return scoped_lock;
}

NX_INLINE void utilsUnlockScope(UtilsScopedLock *scoped_lock)
{
    if (scoped_lock->lock) mutexUnlock(scoped_lock->mtx);
}

#ifdef __cplusplus
}
#endif

#endif /* __NXDT_UTILS_H__ */
