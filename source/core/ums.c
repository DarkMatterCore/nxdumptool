/*
 * ums.c
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

#include "nxdt_utils.h"

/* Global variables. */

static Mutex g_umsMutex = 0;
static bool g_umsInterfaceInit = false;

static Thread g_umsDetectionThread = {0};
static UEvent *g_umsStatusChangeEvent = NULL, g_umsDetectionThreadExitEvent = {0};
static bool g_umsDetectionThreadCreated = false, g_umsDeviceInfoUpdated = false;

static u32 g_umsDeviceCount = 0;
static UsbHsFsDevice *g_umsDevices = NULL;

/* Function prototypes. */

static bool umsCreateDetectionThread(void);
static void umsDestroyDetectionThread(void);
static void umsDetectionThreadFunc(void *arg);

static void umsFreeDeviceData(void);

bool umsInitialize(void)
{
    bool ret = false;
    
    SCOPED_LOCK(&g_umsMutex)
    {
        ret = g_umsInterfaceInit;
        if (ret) break;
        
        /* Initialize USB Mass Storage Host interface. */
        Result rc = usbHsFsInitialize(0);
        if (R_FAILED(rc))
        {
            LOG_MSG("usbHsFsInitialize failed! (0x%08X).", rc);
            break;
        }
        
        /* Get USB Mass Storage status change event. */
        g_umsStatusChangeEvent = usbHsFsGetStatusChangeUserEvent();
        
        /* Create user-mode exit event. */
        ueventCreate(&g_umsDetectionThreadExitEvent, true);
        
        /* Create USB Mass Storage detection thread. */
        if (!(g_umsDetectionThreadCreated = umsCreateDetectionThread())) break;
        
        /* Update flags. */
        ret = g_umsInterfaceInit = true;
    }
    
    return ret;
}

void umsExit(void)
{
    SCOPED_LOCK(&g_umsMutex)
    {
        /* Destroy USB Mass Storage detection thread. */
        if (g_umsDetectionThreadCreated)
        {
            umsDestroyDetectionThread();
            g_umsDetectionThreadCreated = false;
        }
        
        /* Close USB Mass Storage Host interface. */
        usbHsFsExit();
        
        /* Update flag. */
        g_umsInterfaceInit = false;
    }
}

bool umsIsDeviceInfoUpdated(void)
{
    bool ret = false;
    
    SCOPED_TRY_LOCK(&g_umsMutex)
    {
        if (!g_umsInterfaceInit || !g_umsDeviceInfoUpdated) break;
        ret = true;
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
            LOG_MSG("Invalid parameters!");
            break;
        }
        
        if (!g_umsDeviceCount || !g_umsDevices)
        {
            /* Update output device count. */
            *out_count = 0;
            break;
        }
        
        /* Allocate memory for the output devices. */
        devices = calloc(g_umsDeviceCount, sizeof(UsbHsFsDevice));
        if (!devices)
        {
            LOG_MSG("Failed to allocate memory for %u devices!", g_umsDeviceCount);
            break;
        }
        
        /* Copy device data. */
        memcpy(devices, g_umsDevices, g_umsDeviceCount * sizeof(UsbHsFsDevice));
        
        /* Update output device count. */
        *out_count = g_umsDeviceCount;
    }
    
    return devices;
}

static bool umsCreateDetectionThread(void)
{
    if (!utilsCreateThread(&g_umsDetectionThread, umsDetectionThreadFunc, NULL, 1))
    {
        LOG_MSG("Failed to create USB Mass Storage detection thread!");
        return false;
    }
    
    return true;
}

static void umsDestroyDetectionThread(void)
{
    /* Signal the exit event to terminate the USB Mass Storage detection thread. */
    ueventSignal(&g_umsDetectionThreadExitEvent);
    
    /* Wait for the USB Mass Storage detection thread to exit. */
    utilsJoinThread(&g_umsDetectionThread);
}

static void umsDetectionThreadFunc(void *arg)
{
    (void)arg;
    
    Result rc = 0;
    int idx = 0;
    u32 listed_device_count = 0;
    
    Waiter status_change_event_waiter = waiterForUEvent(g_umsStatusChangeEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_umsDetectionThreadExitEvent);
    
    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, status_change_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;
        
        /* Exit event triggered. */
        if (idx == 1) break;
        
        SCOPED_LOCK(&g_umsMutex)
        {
            /* Free USB Mass Storage device data. */
            umsFreeDeviceData();
            
            /* Get mounted device count. */
            g_umsDeviceCount = usbHsFsGetMountedDeviceCount();
            LOG_MSG("USB Mass Storage status change event triggered! Mounted USB Mass Storage device count: %u.", g_umsDeviceCount);
            
            if (g_umsDeviceCount)
            {
                bool fail = false;
                
                /* Allocate mounted devices buffer. */
                g_umsDevices = calloc(g_umsDeviceCount, sizeof(UsbHsFsDevice));
                if (g_umsDevices)
                {
                    /* List mounted devices. */
                    listed_device_count = usbHsFsListMountedDevices(g_umsDevices, g_umsDeviceCount);
                    if (listed_device_count)
                    {
                        /* Check if we got as many devices as we expected. */
                        if (listed_device_count == g_umsDeviceCount)
                        {
                            /* Update USB Mass Storage device info updated flag. */
                            g_umsDeviceInfoUpdated = true;
                        } else {
                            LOG_MSG("USB Mass Storage device count mismatch! (%u != %u).", listed_device_count, g_umsDeviceCount);
                            fail = true;
                        }
                    } else {
                        LOG_MSG("Failed to list mounted USB Mass Storage devices!");
                        fail = true;
                    }
                } else {
                    LOG_MSG("Failed to allocate memory for mounted USB Mass Storage devices buffer!");
                    fail = true;
                }
                
                /* Free USB Mass Storage device data if something went wrong. */
                if (fail) umsFreeDeviceData();
            } else {
                /* Update USB Mass Storage device info updated flag. */
                g_umsDeviceInfoUpdated = true;
            }
        }
    }
    
    /* Free USB Mass Storage device data. */
    umsFreeDeviceData();
    
    threadExit();
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
