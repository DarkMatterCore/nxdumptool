/*
 * bis_storage.c
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include <core/nxdt_utils.h>
#include <core/bis_storage.h>
#include <core/devoptab/nxdt_devoptab.h>

#define BIS_STORAGE_INDEX(type)         ((type) - FsBisPartitionId_CalibrationFile)

#define BIS_STORAGE_GPT_NAME(type)      g_bisStorageGptPartitionNames[BIS_STORAGE_INDEX(type)]
#define BIS_STORAGE_SYSINIT_NAME(type)  g_bisStorageSystemInitializerPartitionNames[BIS_STORAGE_INDEX(type)]
#define BIS_STORAGE_MOUNT_NAME(type)    g_bisStorageDevoptabMountNames[BIS_STORAGE_INDEX(type)]

#define BIS_STORAGE_FATFS_CTX(type)     g_bisStorageContexts[BIS_STORAGE_INDEX(type)]

#define BIS_GET_NAME_FUNC(property, field) \
const char *bisStorageGet##property##ByBisPartitionId(u8 bis_partition_id) { \
    const char *ret = NULL; \
    SCOPED_LOCK(&g_bisStorageMutex) { \
        BisStorageFatFsContext *bis_fatfs_ctx = NULL; \
        if (!g_bisStorageInterfaceInit || bis_partition_id < FsBisPartitionId_CalibrationFile || bis_partition_id > FsBisPartitionId_System || \
            !(bis_fatfs_ctx = BIS_STORAGE_FATFS_CTX(bis_partition_id))) break; \
        ret = bis_fatfs_ctx->field; \
    } \
    return ret; \
}

/* Type definitions. */

typedef struct {
    u8 bis_partition_id;                ///< FsBisPartitionId.
    const char *gpt_name;
    const char *sysinit_name;
    const char *devoptab_mount_name;
    char fatfs_mount_name[4];
    FsStorage bis_storage;
    FATFS fatfs;
} BisStorageFatFsContext;

/* Global variables. */

static Mutex g_bisStorageMutex = 0;
static BisStorageFatFsContext *g_bisStorageContexts[BIS_FAT_PARTITION_COUNT] = {NULL};
static bool g_bisStorageInterfaceInit = false;

static const char *g_bisStorageGptPartitionNames[BIS_FAT_PARTITION_COUNT] = {
    [BIS_STORAGE_INDEX(FsBisPartitionId_CalibrationFile)] = "PRODINFOF",
    [BIS_STORAGE_INDEX(FsBisPartitionId_SafeMode)]        = "SAFE",
    [BIS_STORAGE_INDEX(FsBisPartitionId_User)]            = "USER",
    [BIS_STORAGE_INDEX(FsBisPartitionId_System)]          = "SYSTEM"
};

static const char *g_bisStorageSystemInitializerPartitionNames[BIS_FAT_PARTITION_COUNT] = {
    [BIS_STORAGE_INDEX(FsBisPartitionId_CalibrationFile)] = "CalibrationFile",
    [BIS_STORAGE_INDEX(FsBisPartitionId_SafeMode)]        = "SafeMode",
    [BIS_STORAGE_INDEX(FsBisPartitionId_User)]            = "User",
    [BIS_STORAGE_INDEX(FsBisPartitionId_System)]          = "System"
};

static const char *g_bisStorageDevoptabMountNames[BIS_FAT_PARTITION_COUNT] = {
    [BIS_STORAGE_INDEX(FsBisPartitionId_CalibrationFile)] = "bisprodinfof",
    [BIS_STORAGE_INDEX(FsBisPartitionId_SafeMode)]        = "bissafe",
    [BIS_STORAGE_INDEX(FsBisPartitionId_User)]            = "bisuser",
    [BIS_STORAGE_INDEX(FsBisPartitionId_System)]          = "bissystem"
};

/* Function prototypes. */

NX_INLINE bool bisStorageMountAllPartitions(void);
NX_INLINE void bisStorageUnmountAllPartitions(void);

static bool bisStorageMountPartition(u8 bis_partition_id);
static void bisStorageUnmountPartition(u8 bis_partition_id);

static void bisStorageFreeFatFsContext(BisStorageFatFsContext **bis_fatfs_ctx);

bool bisStorageInitialize(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_bisStorageMutex)
    {
        ret = g_bisStorageInterfaceInit;
        if (ret) break;

        /* Mount all eMMC BIS FAT partitions. */
        ret = g_bisStorageInterfaceInit = bisStorageMountAllPartitions();
    }

    return ret;
}

void bisStorageExit(void)
{
    SCOPED_LOCK(&g_bisStorageMutex)
    {
        /* Unmount all eMMC BIS FAT partitions. */
        bisStorageUnmountAllPartitions();

        g_bisStorageInterfaceInit = false;
    }
}

BIS_GET_NAME_FUNC(GptPartitionName, gpt_name);

BIS_GET_NAME_FUNC(SystemInitializerPartitionName, sysinit_name);

BIS_GET_NAME_FUNC(MountName, devoptab_mount_name);

FsStorage *bisStorageGetFsStorageByFatFsDriveNumber(u8 drive_number)
{
    FsStorage *bis_storage = NULL;

    SCOPED_LOCK(&g_bisStorageMutex)
    {
        if (drive_number >= BIS_FAT_PARTITION_COUNT || !g_bisStorageContexts[drive_number]) break;
        bis_storage = &(g_bisStorageContexts[drive_number]->bis_storage);
    }

    return bis_storage;
}

void bisStorageControlMutex(bool lock)
{
    bool locked = mutexIsLockedByCurrentThread(&g_bisStorageMutex);

    if (!locked && lock)
    {
        mutexLock(&g_bisStorageMutex);
    } else
    if (locked && !lock)
    {
        mutexUnlock(&g_bisStorageMutex);
    }
}

NX_INLINE bool bisStorageMountAllPartitions(void)
{
    bool ret = false;

    for(u8 i = FsBisPartitionId_CalibrationFile; i <= FsBisPartitionId_System; i++)
    {
        ret = bisStorageMountPartition(i);
        if (!ret) break;
    }

    return ret;
}

NX_INLINE void bisStorageUnmountAllPartitions(void)
{
    for(u8 i = FsBisPartitionId_CalibrationFile; i <= FsBisPartitionId_System; i++) bisStorageUnmountPartition(i);
}

static bool bisStorageMountPartition(u8 bis_partition_id)
{
    if (bis_partition_id < FsBisPartitionId_CalibrationFile || bis_partition_id > FsBisPartitionId_System)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BisStorageFatFsContext *bis_fatfs_ctx = NULL;
    Result rc = 0;
    FRESULT fr = FR_OK;
    bool success = false, empty_partition = false;

    /* Check if we have already mounted this eMMC partition. */
    if (BIS_STORAGE_FATFS_CTX(bis_partition_id))
    {
        success = true;
        goto end;
    }

    /* Allocate memory for our BIS FatFs context. */
    bis_fatfs_ctx = calloc(1, sizeof(BisStorageFatFsContext));
    if (!bis_fatfs_ctx)
    {
        LOG_MSG_ERROR("Failed to allocate memory for BIS FatFs context! (partition ID %u).", bis_partition_id);
        goto end;
    }

    /* Update context. */
    bis_fatfs_ctx->bis_partition_id = bis_partition_id;
    bis_fatfs_ctx->gpt_name = BIS_STORAGE_GPT_NAME(bis_partition_id);
    bis_fatfs_ctx->sysinit_name = BIS_STORAGE_SYSINIT_NAME(bis_partition_id);
    bis_fatfs_ctx->devoptab_mount_name = BIS_STORAGE_MOUNT_NAME(bis_partition_id);
    snprintf(bis_fatfs_ctx->fatfs_mount_name, sizeof(bis_fatfs_ctx->fatfs_mount_name), "%u:", BIS_STORAGE_INDEX(bis_partition_id));

    /* Open BIS storage. */
    rc = fsOpenBisStorage(&(bis_fatfs_ctx->bis_storage), bis_fatfs_ctx->bis_partition_id);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("Failed to open BIS storage for %s partition! (0x%X).", bis_fatfs_ctx->gpt_name, rc);
        goto end;
    }

    /* Update context array. */
    /* FatFs diskio demands we do this here. */
    BIS_STORAGE_FATFS_CTX(bis_partition_id) = bis_fatfs_ctx;

    /* Mount BIS partition using FatFs. */
    fr = f_mount(&(bis_fatfs_ctx->fatfs), bis_fatfs_ctx->fatfs_mount_name, 1);
    if (fr != FR_OK)
    {
        LOG_MSG_ERROR("Failed to mount %s partition via FatFs! (%u).", bis_fatfs_ctx->gpt_name, fr);

        /* Add an exception for partitions that don't hold a valid FAT volume. */
        /* TODO: find out why some of these partitions are empty in some consoles. */
        if (fr == FR_NO_FILESYSTEM) empty_partition = true;

        goto end;
    }

    /* Mount devoptab device. */
    if (!devoptabMountFatFsDevice(&(bis_fatfs_ctx->fatfs), bis_fatfs_ctx->devoptab_mount_name))
    {
        LOG_MSG_ERROR("Failed to mount devoptab device for %s partition!", bis_fatfs_ctx->gpt_name);
        goto end;
    }

    /* Update flag. */
    success = true;

end:
    if (!success && bis_fatfs_ctx)
    {
        bisStorageFreeFatFsContext(&bis_fatfs_ctx);
        BIS_STORAGE_FATFS_CTX(bis_partition_id) = NULL;
        if (empty_partition) success = true;
    }

    return success;
}

static void bisStorageUnmountPartition(u8 bis_partition_id)
{
    if (bis_partition_id < FsBisPartitionId_CalibrationFile || bis_partition_id > FsBisPartitionId_System)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    /* Check if we have already mounted this eMMC partition. */
    BisStorageFatFsContext *bis_fatfs_ctx = BIS_STORAGE_FATFS_CTX(bis_partition_id);
    if (!bis_fatfs_ctx) return;

    /* Free BIS FatFs context. This will take care of unmounting the partition. */
    bisStorageFreeFatFsContext(&bis_fatfs_ctx);

    /* Update context array. */
    BIS_STORAGE_FATFS_CTX(bis_partition_id) = NULL;
}

static void bisStorageFreeFatFsContext(BisStorageFatFsContext **bis_fatfs_ctx)
{
    if (!bis_fatfs_ctx || !*bis_fatfs_ctx) return;

    BisStorageFatFsContext *ctx = *bis_fatfs_ctx;

    if (ctx->fatfs.fs_type)
    {
        devoptabUnmountDevice(ctx->devoptab_mount_name);
        f_unmount(ctx->fatfs_mount_name);
    }

    if (serviceIsActive(&(ctx->bis_storage.s))) fsStorageClose(&(ctx->bis_storage));

    free(ctx);
    ctx = *bis_fatfs_ctx = NULL;
}
