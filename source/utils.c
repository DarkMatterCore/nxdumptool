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

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <switch.h>

//#include "freetype_helper.h"
//#include "lvgl_helper.h"
#include "keys.h"
#include "gamecard.h"
#include "services.h"
#include "utils.h"

/* Global variables. */

static u8 g_customFirmwareType = UtilsCustomFirmwareType_Unknown;

static AppletHookCookie g_systemOverclockCookie = {0};

static Mutex g_logfileMutex = 0;

/* Function prototypes. */

static void _utilsGetCustomFirmwareType(void);

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param);

u64 utilsHidKeysAllDown(void)
{
    u8 controller;
    u64 keys_down = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keys_down |= hidKeysDown((HidControllerID)controller);
    
    return keys_down;
}

u64 utilsHidKeysAllHeld(void)
{
    u8 controller;
    u64 keys_held = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keys_held |= hidKeysHeld((HidControllerID)controller);
    
    return keys_held;
}

void utilsWaitForButtonPress(void)
{
    u64 flag, keys_down;
    
    /* Don't consider touch screen presses nor stick movement as button inputs */
    flag = ~(KEY_TOUCH | KEY_LSTICK_LEFT | KEY_LSTICK_RIGHT | KEY_LSTICK_UP | KEY_LSTICK_DOWN | KEY_RSTICK_LEFT | KEY_RSTICK_RIGHT | KEY_RSTICK_UP | KEY_RSTICK_DOWN);
    
    while(appletMainLoop())
    {
        hidScanInput();
        keys_down = utilsHidKeysAllDown();
        if (keys_down & flag) break;
    }
}

void utilsWriteLogMessage(const char *func_name, const char *fmt, ...)
{
    mutexLock(&g_logfileMutex);
    
    va_list args;
    FILE *logfile = NULL;
    
    logfile = fopen(APP_BASE_PATH "nxdumptool.log", "a+");
    if (!logfile) goto out;
    
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    fprintf(logfile, "%d/%d/%d %d:%02d:%02d -> %s: ", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    
    va_start(args, fmt);
    vfprintf(logfile, fmt, args);
    va_end(args);
    
    fprintf(logfile, "\r\n");
    fclose(logfile);
    
out:
    mutexUnlock(&g_logfileMutex);
}

void utilsOverclockSystem(bool restore)
{
    u32 cpuClkRate = ((restore ? CPU_CLKRT_NORMAL : CPU_CLKRT_OVERCLOCKED) * 1000000);
    u32 memClkRate = ((restore ? MEM_CLKRT_NORMAL : MEM_CLKRT_OVERCLOCKED) * 1000000);
    servicesChangeHardwareClockRates(cpuClkRate, memClkRate);
}

bool utilsInitializeResources(void)
{
    /* Initialize needed services */
    if (!servicesInitialize())
    {
        LOGFILE("Failed to initialize needed services!");
        return false;
    }
    
    /* Load NCA keyset */
    if (!keysLoadNcaKeyset())
    {
        LOGFILE("Failed to load NCA keyset!");
        return false;
    }
    
    /* Initialize FreeType */
    //if (!freeTypeHelperInitialize()) return false;
    
    /* Initialize LVGL */
    //if (!lvglHelperInitialize()) return false;
    
    /* Retrieve custom firmware type */
    _utilsGetCustomFirmwareType();
    
    /* Overclock system */
    utilsOverclockSystem(false);
    
    /* Setup an applet hook to change the hardware clocks after a system mode change (docked <-> undocked) */
    appletHook(&g_systemOverclockCookie, utilsOverclockSystemAppletHook, NULL);
    
    /* Initialize gamecard interface */
    Result rc = gamecardInitialize();
    if (R_FAILED(rc))
    {
        LOGFILE("Failed to initialize gamecard interface!");
        return false;
    }
    
    return true;
}

void utilsCloseResources(void)
{
    /* Deinitialize gamecard interface */
    gamecardExit();
    
    /* Unset our overclock applet hook */
    appletUnhook(&g_systemOverclockCookie);
    
    /* Restore hardware clocks */
    utilsOverclockSystem(true);
    
    /* Free LVGL resources */
    //lvglHelperExit();
    
    /* Free FreeType resouces */
    //freeTypeHelperExit();
    
    /* Close initialized services */
    servicesClose();
}

u8 utilsGetCustomFirmwareType(void)
{
    return g_customFirmwareType;
}

static void _utilsGetCustomFirmwareType(void)
{
    bool tx_srv = servicesCheckRunningServiceByName("tx");
    bool rnx_srv = servicesCheckRunningServiceByName("rnx");
    
    if (!tx_srv && !rnx_srv)
    {
        /* Atmosphere */
        g_customFirmwareType = UtilsCustomFirmwareType_Atmosphere;
    } else
    if (tx_srv && !rnx_srv)
    {
        /* SX OS */
        g_customFirmwareType = UtilsCustomFirmwareType_SXOS;
    } else {
        /* ReiNX */
        g_customFirmwareType = UtilsCustomFirmwareType_ReiNX;
    }
}

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param)
{
    (void)param;
    
    if (hook != AppletHookType_OnOperationMode && hook != AppletHookType_OnPerformanceMode) return;
    
    /* To do: read config here to actually know the value to use with utilsOverclockSystem */
    utilsOverclockSystem(true);
}
