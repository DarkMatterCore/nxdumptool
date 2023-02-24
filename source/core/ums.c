/*
 * ums.c
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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

/* Global variables. */

static Mutex g_umsMutex = 0;
static bool g_umsInterfaceInit = false, g_umsDeviceInfoUpdated = false;

static u32 g_umsDeviceCount = 0;
static UsbHsFsDevice *g_umsDevices = NULL;

/* Function prototypes. */

static void umsFreeDeviceData(void);
static void umsPopulateCallback(const UsbHsFsDevice *devices, u32 device_count, void *user_data);
static bool umsDuplicateDeviceArray(const UsbHsFsDevice *in_devices, u32 in_device_count, UsbHsFsDevice **out_devices, u32 *out_device_count);

bool umsInitialize(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_umsMutex)
    {
        ret = g_umsInterfaceInit;
        if (ret) break;

        /* Set populate callback function. */
        usbHsFsSetPopulateCallback(&umsPopulateCallback, NULL);

        /* Initialize USB Mass Storage Host interface. */
        Result rc = usbHsFsInitialize(0);
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("usbHsFsInitialize failed! (0x%X).", rc);
            break;
        }

        /* Update flags. */
        ret = g_umsInterfaceInit = true;
    }

    return ret;
}

void umsExit(void)
{
    SCOPED_LOCK(&g_umsMutex)
    {
        /* Close USB Mass Storage Host interface. */
        usbHsFsExit();

        /* Free USB Mass Storage device data. */
        umsFreeDeviceData();

        /* Update flags. */
        g_umsInterfaceInit = g_umsDeviceInfoUpdated = false;
    }
}

bool umsIsDeviceInfoUpdated(void)
{
    bool ret = false;

    SCOPED_TRY_LOCK(&g_umsMutex)
    {
        ret = (g_umsInterfaceInit && g_umsDeviceInfoUpdated);
        g_umsDeviceInfoUpdated = false;
    }

    return ret;
}

UsbHsFsDevice *umsGetDevices(u32 *out_count)
{
    UsbHsFsDevice *devices = NULL;

    SCOPED_LOCK(&g_umsMutex)
    {
        if (!g_umsInterfaceInit || !out_count)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        /* Duplicate device data. */
        if (!umsDuplicateDeviceArray(g_umsDevices, g_umsDeviceCount, &devices, out_count)) LOG_MSG_ERROR("Failed to duplicate USB Mass Storage device data!");
    }

    return devices;
}

static void umsFreeDeviceData(void)
{
    /* Free devices buffer. */
    if (g_umsDevices)
    {
        free(g_umsDevices);
        g_umsDevices = NULL;
    }

    /* Reset device count. */
    g_umsDeviceCount = 0;
}

static void umsPopulateCallback(const UsbHsFsDevice *devices, u32 device_count, void *user_data)
{
    (void)user_data;

    SCOPED_LOCK(&g_umsMutex)
    {
        /* Free USB Mass Storage device data. */
        umsFreeDeviceData();

        LOG_MSG_INFO("Mounted USB Mass Storage device count: %u.", device_count);

        if (devices && device_count)
        {
            /* Duplicate device data. */
            if (!umsDuplicateDeviceArray(devices, device_count, &g_umsDevices, &g_umsDeviceCount)) LOG_MSG_ERROR("Failed to duplicate USB Mass Storage device data!");
        }

        /* Update USB Mass Storage device info updated flag. */
        g_umsDeviceInfoUpdated = true;
    }
}

static bool umsDuplicateDeviceArray(const UsbHsFsDevice *in_devices, u32 in_device_count, UsbHsFsDevice **out_devices, u32 *out_device_count)
{
    if (!out_devices || !out_device_count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    UsbHsFsDevice *tmp_devices = NULL;
    bool ret = false;

    /* Clear output. */
    *out_devices = NULL;
    *out_device_count = 0;

    /* Short-circuit. */
    if (!in_devices || !in_device_count)
    {
        ret = true;
        goto end;
    }

    /* Duplicate input array. */
    tmp_devices = calloc(in_device_count, sizeof(UsbHsFsDevice));
    if (!tmp_devices)
    {
        LOG_MSG_ERROR("Failed to allocate memory for %u devices!", in_device_count);
        goto end;
    }

    /* Copy device data. */
    memcpy(tmp_devices, in_devices, in_device_count * sizeof(UsbHsFsDevice));

    /* Update output. */
    *out_devices = tmp_devices;
    *out_device_count = in_device_count;

    /* Update return value. */
    ret = true;

end:
    return ret;
}
