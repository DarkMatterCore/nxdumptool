/*
 * nxdt_devoptab.c
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

#include "nxdt_utils.h"
#include "nxdt_devoptab.h"

#define DEVOPTAB_DEVICE_COUNT   4

/* Type definitions. */

typedef enum {
    DevoptabDeviceType_PartitionFileSystem = 0,
    DevoptabDeviceType_HashFileSystem      = 1,
    DevoptabDeviceType_RomFileSystem       = 2,
    DevoptabDeviceType_Count               = 3  ///< Total values supported by this enum.
} DevoptabDeviceType;

/* Global variables. */

static Mutex g_devoptabMutex = 0;
static DevoptabDeviceContext g_devoptabDevices[DEVOPTAB_DEVICE_COUNT] = {0};
static const u32 g_devoptabDeviceCount = MAX_ELEMENTS(g_devoptabDevices);

/* Function prototypes. */

const devoptab_t *pfsdev_get_devoptab();
const devoptab_t *hfsdev_get_devoptab();

static bool devoptabMountDevice(void *fs_ctx, const char *name, u8 type);
static DevoptabDeviceContext *devoptabFindDevice(const char *name);
static void devoptabResetDevice(DevoptabDeviceContext *dev_ctx);

bool devoptabMountPartitionFileSystemDevice(PartitionFileSystemContext *pfs_ctx, const char *name)
{
    if (!pfsIsValidContext(pfs_ctx) || !name || !*name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_devoptabMutex)
    {
        ret = devoptabMountDevice(pfs_ctx, name, DevoptabDeviceType_PartitionFileSystem);
    }

    return ret;
}

bool devoptabMountHashFileSystemDevice(HashFileSystemContext *hfs_ctx, const char *name)
{
    if (!hfsIsValidContext(hfs_ctx) || !name || !*name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_devoptabMutex)
    {
        ret = devoptabMountDevice(hfs_ctx, name, DevoptabDeviceType_HashFileSystem);
    }

    return ret;
}

bool devoptabMountRomFileSystemDevice(RomFileSystemContext *romfs_ctx, const char *name)
{
    if (!romfsIsValidContext(romfs_ctx) || !name || !*name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_devoptabMutex)
    {
        ret = devoptabMountDevice(romfs_ctx, name, DevoptabDeviceType_RomFileSystem);
    }

    return ret;
}

void devoptabUnmountDevice(const char *name)
{
    if (!name || !*name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    SCOPED_LOCK(&g_devoptabMutex)
    {
        /* Get device entry. */
        DevoptabDeviceContext *dev_ctx = devoptabFindDevice(name);
        if (dev_ctx)
        {
            /* Reset device. */
            devoptabResetDevice(dev_ctx);
        } else {
            LOG_MSG_ERROR("Error: unable to find devoptab device \"%s\".", name);
        }
    }
}

void devoptabUnmountAllDevices(void)
{
    SCOPED_LOCK(&g_devoptabMutex)
    {
        /* Loop through all of our device entries and reset them all. */
        for(u32 i = 0; i < g_devoptabDeviceCount; i++) devoptabResetDevice(&(g_devoptabDevices[i]));
    }
}

void devoptabControlMutex(bool lock)
{
    bool locked = mutexIsLockedByCurrentThread(&g_devoptabMutex);

    if (!locked && lock)
    {
        mutexLock(&g_devoptabMutex);
    } else
    if (locked && !lock)
    {
        mutexUnlock(&g_devoptabMutex);
    }
}

static bool devoptabMountDevice(void *fs_ctx, const char *name, u8 type)
{
    if (!fs_ctx || !name || !*name || type >= DevoptabDeviceType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    DevoptabDeviceContext *dev_ctx = NULL;
    const devoptab_t *device = NULL;
    bool ret = false;

    /* Retrieve a pointer to the first unused device entry. */
    if (!(dev_ctx = devoptabFindDevice(NULL)))
    {
        LOG_MSG_ERROR("Error: unable to find an empty device slot for \"%s\" (type 0x%02X).", name, type);
        return false;
    }

    /* Retrieve a pointer to the appropriate devoptab interface for this filesystem type. */
    switch(type)
    {
        case DevoptabDeviceType_PartitionFileSystem:
            device = pfsdev_get_devoptab();
            break;
        case DevoptabDeviceType_HashFileSystem:
            device = hfsdev_get_devoptab();
            break;
        case DevoptabDeviceType_RomFileSystem:
            break;
        default:
            break;
    }

    if (!device)
    {
        LOG_MSG_ERROR("Error: unable to retrieve a devoptab interface for \"%s\" (type 0x%02X).", name, type);
        return false;
    }

    /* Populate device entry. */
    snprintf(dev_ctx->name, MAX_ELEMENTS(dev_ctx->name), "%s", name);

    dev_ctx->mount_time = time(NULL);

    memcpy(&(dev_ctx->device), device, sizeof(devoptab_t));
    dev_ctx->device.name = dev_ctx->name;
    dev_ctx->device.deviceData = dev_ctx;

    dev_ctx->fs_ctx = fs_ctx;

    /* Add devoptab device. */
    int res = AddDevice(&(dev_ctx->device));
    if (res < 0)
    {
        LOG_MSG_ERROR("Error: AddDevice failed! (%d).", res);
        goto end;
    }

    /* Update flags. */
    ret = dev_ctx->initialized = true;

end:
    if (!ret) memset(dev_ctx, 0, sizeof(DevoptabDeviceContext));

    return ret;
}

static DevoptabDeviceContext *devoptabFindDevice(const char *name)
{
    DevoptabDeviceContext *dev_ctx = NULL;

    for(u32 i = 0; i < g_devoptabDeviceCount; i++)
    {
        dev_ctx = &(g_devoptabDevices[i]);

        if (!name)
        {
            /* Find an unused entry. */
            if (!dev_ctx->initialized) break;
        } else
        if (dev_ctx->initialized)
        {
            /* Find an entry with a matching mount name. */
            if (!strncmp(dev_ctx->name, name, sizeof(dev_ctx->name))) break;
        }

        dev_ctx = NULL;
    }

    return dev_ctx;
}

static void devoptabResetDevice(DevoptabDeviceContext *dev_ctx)
{
    if (!dev_ctx || !dev_ctx->initialized) return;

    char tmp_name[DEVOPTAB_MOUNT_NAME_LENGTH + 2] = {0};

    snprintf(tmp_name, MAX_ELEMENTS(tmp_name), "%s:", dev_ctx->name);
    RemoveDevice(tmp_name);

    memset(dev_ctx, 0, sizeof(DevoptabDeviceContext));

    LOG_MSG_DEBUG("Successfully unmounted device \"%s\".", tmp_name);
}
