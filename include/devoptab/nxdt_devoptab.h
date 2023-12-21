/*
 * nxdt_devoptab.h
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __NXDT_DEVOPTAB_H__
#define __NXDT_DEVOPTAB_H__

#include "pfs.h"
#include "hfs.h"
#include "romfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DEVOPTAB_MOUNT_NAME_LENGTH                  32  // Including NULL terminator.

#define DEVOPTAB_DECL_ERROR_STATE                   int _errno = 0
#define DEVOPTAB_DECL_DEV_CTX                       DevoptabDeviceContext *dev_ctx = (DevoptabDeviceContext*)r->deviceData
#define DEVOPTAB_DECL_FS_CTX(type)                  type *fs_ctx = (type*)dev_ctx->fs_ctx
#define DEVOPTAB_DECL_FILE_STATE(type)              type *file = (type*)fd
#define DEVOPTAB_DECL_DIR_STATE(type)               type *dir = (type*)dirState->dirStruct

#define DEVOPTAB_SET_ERROR(x)                       r->_errno = _errno = (x)
#define DEVOPTAB_IS_ERROR_SET                       (_errno != 0)

#define DEVOPTAB_EXIT                               goto end
#define DEVOPTAB_SET_ERROR_AND_EXIT(x)              \
do { \
    DEVOPTAB_SET_ERROR(x); \
    DEVOPTAB_EXIT; \
} while(0)

#define DEVOPTAB_RETURN_INT(x)                      return (DEVOPTAB_IS_ERROR_SET ? -1 : (x))
#define DEVOPTAB_RETURN_PTR(x)                      return (DEVOPTAB_IS_ERROR_SET ? NULL : (x))
#define DEVOPTAB_RETURN_BOOL                        return (DEVOPTAB_IS_ERROR_SET ? false : true)
#define DEVOPTAB_RETURN_UNSUPPORTED_OP              r->_errno = ENOSYS; \
                                                    return -1;

#define DEVOPTAB_INIT_VARS(type)                    devoptabControlMutex(true); \
                                                    DEVOPTAB_DECL_ERROR_STATE; \
                                                    DEVOPTAB_DECL_DEV_CTX; \
                                                    if (!dev_ctx->initialized) DEVOPTAB_SET_ERROR_AND_EXIT(ENODEV);

#define DEVOPTAB_INIT_FILE_VARS(fs_type, file_type) DEVOPTAB_INIT_VARS(fs_type); \
                                                    DEVOPTAB_DECL_FILE_STATE(file_type)

#define DEVOPTAB_INIT_DIR_VARS(fs_type, dir_type)   DEVOPTAB_INIT_VARS(fs_type); \
                                                    DEVOPTAB_DECL_DIR_STATE(dir_type)

#define DEVOPTAB_DEINIT_VARS                        devoptabControlMutex(false)

typedef struct {
    bool initialized;                       ///< Device initialization flag.
    char name[DEVOPTAB_MOUNT_NAME_LENGTH];  ///< Mount name string, without a trailing colon (:).
    time_t mount_time;                      ///< Mount time.
    devoptab_t device;                      ///< Devoptab virtual device interface. Provides a way to use libcstd I/O calls on the mounted filesystem.
    void *fs_ctx;                           ///< Pointer to actual type-specific filesystem context (PartitionFileSystemContext, HashFileSystemContext, RomFileSystemContext).
} DevoptabDeviceContext;

/// Mounts a virtual Partition FS device using the provided Partition FS context and a mount name.
bool devoptabMountPartitionFileSystemDevice(PartitionFileSystemContext *pfs_ctx, const char *name);

/// Mounts a virtual Hash FS device using the provided Hash FS context and a mount name.
bool devoptabMountHashFileSystemDevice(HashFileSystemContext *hfs_ctx, const char *name);

/// Mounts a virtual RomFS device using the provided RomFS context and a mount name.
bool devoptabMountRomFileSystemDevice(RomFileSystemContext *romfs_ctx, const char *name);

/// Unmounts a previously mounted virtual device.
void devoptabUnmountDevice(const char *name);

/// Unmounts all previously mounted virtual devices.
void devoptabUnmountAllDevices(void);

/// (Un)locks the devoptab mutex. Used by filesystem-specific devoptab interfaces.
void devoptabControlMutex(bool lock);

#ifdef __cplusplus
}
#endif

#endif /* __NXDT_DEVOPTAB_H__ */
