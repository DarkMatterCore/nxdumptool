/*
 * utils.c
 *
 * Copyright (c) 2018-2020, WerWolv.
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

#include "utils.h"
//#include "freetype_helper.h"
//#include "lvgl_helper.h"
#include "keys.h"
#include "gamecard.h"
#include "services.h"
#include "nca.h"
#include "usb.h"
#include "fatfs/ff.h"

/* Global variables. */

static bool g_resourcesInitialized = false;
static Mutex g_resourcesMutex = 0;

static FsFileSystem *g_sdCardFileSystem = NULL;

static FsStorage g_emmcBisSystemPartitionStorage = {0};
static FATFS *g_emmcBisSystemPartitionFatFsObj = NULL;

static AppletType g_programAppletType = 0;
static bool g_homeButtonBlocked = false;
static Mutex g_homeButtonMutex = 0;

static u8 g_customFirmwareType = UtilsCustomFirmwareType_Unknown;

static AppletHookCookie g_systemOverclockCookie = {0};

static Mutex g_logfileMutex = 0;

/* Function prototypes. */

static u64 utilsHidKeysAllDown(void);
static u64 utilsHidKeysAllHeld(void);

static bool utilsMountEmmcBisSystemPartitionStorage(void);
static void utilsUnmountEmmcBisSystemPartitionStorage(void);

static void _utilsGetCustomFirmwareType(void);

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param);

bool utilsInitializeResources(void)
{
    mutexLock(&g_resourcesMutex);
    
    bool ret = g_resourcesInitialized;
    if (ret) goto end;
    
    /* Initialize needed services. */
    if (!servicesInitialize())
    {
        LOGFILE("Failed to initialize needed services!");
        goto end;
    }
    
    /* Initialize USB interface. */
    if (!usbInitialize())
    {
        LOGFILE("Failed to initialize USB interface!");
        goto end;
    }
    
    /* Load NCA keyset. */
    if (!keysLoadNcaKeyset())
    {
        LOGFILE("Failed to load NCA keyset!");
        goto end;
    }
    
    /* Allocate NCA crypto buffer. */
    if (!ncaAllocateCryptoBuffer())
    {
        LOGFILE("Unable to allocate memory for NCA crypto buffer!");
        goto end;
    }
    
    /* Initialize gamecard interface. */
    if (!gamecardInitialize())
    {
        LOGFILE("Failed to initialize gamecard interface!");
        goto end;
    }
    
    /* Retrieve SD card FsFileSystem element. */
    if (!(g_sdCardFileSystem = fsdevGetDeviceFileSystem("sdmc:")))
    {
        LOGFILE("fsdevGetDeviceFileSystem failed!");
        goto end;
    }
    
    /* Mount eMMC BIS System partition. */
    if (!utilsMountEmmcBisSystemPartitionStorage()) goto end;
    
    /* Get applet type. */
    g_programAppletType = appletGetAppletType();
    
    /* Disable screen dimming and auto sleep. */
    appletSetMediaPlaybackState(true);
    
    /* Retrieve custom firmware type. */
    _utilsGetCustomFirmwareType();
    
    /* Overclock system. */
    utilsOverclockSystem(true);
    
    /* Setup an applet hook to change the hardware clocks after a system mode change (docked <-> undocked). */
    appletHook(&g_systemOverclockCookie, utilsOverclockSystemAppletHook, NULL);
    
    /* Initialize FreeType. */
    //if (!freeTypeHelperInitialize()) return false;
    
    /* Initialize LVGL. */
    //if (!lvglHelperInitialize()) return false;
    
    ret = g_resourcesInitialized = true;
    
end:
    mutexUnlock(&g_resourcesMutex);
    
    return ret;
}

void utilsCloseResources(void)
{
    mutexLock(&g_resourcesMutex);
    
    /* Free LVGL resources. */
    //lvglHelperExit();
    
    /* Free FreeType resources. */
    //freeTypeHelperExit();
    
    /* Unset our overclock applet hook. */
    appletUnhook(&g_systemOverclockCookie);
    
    /* Restore hardware clocks. */
    utilsOverclockSystem(false);
    
    /* Enable screen dimming and auto sleep. */
    appletSetMediaPlaybackState(false);
    
    /* Unblock HOME button presses. */
    utilsChangeHomeButtonBlockStatus(false);
    
    /* Unmount eMMC BIS System partition. */
    utilsUnmountEmmcBisSystemPartitionStorage();
    
    /* Deinitialize gamecard interface. */
    gamecardExit();
    
    /* Free NCA crypto buffer. */
    ncaFreeCryptoBuffer();
    
    /* Close USB interface. */
    usbExit();
    
    /* Close initialized services. */
    servicesClose();
    
    g_resourcesInitialized = false;
    
    mutexUnlock(&g_resourcesMutex);
}

u64 utilsReadInput(u8 input_type)
{
    if (input_type != UtilsInputType_Down && input_type != UtilsInputType_Held) return 0;
    
    hidScanInput();
    
    return (input_type == UtilsInputType_Down ? utilsHidKeysAllDown() : utilsHidKeysAllHeld());
}

void utilsWaitForButtonPress(u64 flag)
{
    u64 keys_down = 0;
    
    if (!flag)
    {
        /* Don't consider touch screen presses nor stick movement as button inputs. */
        flag = ~(KEY_TOUCH | KEY_LSTICK_LEFT | KEY_LSTICK_RIGHT | KEY_LSTICK_UP | KEY_LSTICK_DOWN | KEY_RSTICK_LEFT | KEY_RSTICK_RIGHT | KEY_RSTICK_UP | KEY_RSTICK_DOWN);
    }
    
    while(appletMainLoop())
    {
        keys_down = utilsReadInput(UtilsInputType_Down);
        if (keys_down & flag) break;
    }
}

void utilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...)
{
    if (!func_name || !strlen(func_name) || !fmt || !strlen(fmt)) return;
    
    mutexLock(&g_logfileMutex);
    
    va_list args;
    FILE *logfile = NULL;
    
    logfile = fopen(LOGFILE_PATH, "a+");
    if (!logfile) goto end;
    
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    fprintf(logfile, "%d-%02d-%02d %02d:%02d:%02d -> %s: ", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    
    va_start(args, fmt);
    vfprintf(logfile, fmt, args);
    va_end(args);
    
    fprintf(logfile, "\r\n");
    fclose(logfile);
    
end:
    mutexUnlock(&g_logfileMutex);
}

void utilsWriteMessageToLogBuffer(char *dst, size_t dst_size, const char *func_name, const char *fmt, ...)
{
    if (!dst || !dst_size || !func_name || !strlen(func_name) || !fmt || !strlen(fmt)) return;
    
    va_list args;
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    char msg[512] = {0};
    size_t msg_len = 0, dst_len = strlen(dst);
    
    snprintf(msg, sizeof(msg), "%d-%02d-%02d %02d:%02d:%02d -> %s: ", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    msg_len = strlen(msg);
    
    va_start(args, fmt);
    vsnprintf(msg + msg_len, sizeof(msg) - msg_len, fmt, args);
    va_end(args);
    msg_len = strlen(msg);
    
    if ((dst_size - dst_len) > (msg_len + 2)) snprintf(dst + dst_len, dst_size - dst_len, "%s\r\n", msg);
}

void utilsWriteLogBufferToLogFile(const char *src)
{
    if (!src || !strlen(src)) return;
    
    mutexLock(&g_logfileMutex);
    
    FILE *logfile = fopen(LOGFILE_PATH, "a+");
    if (!logfile) goto end;
    
    fprintf(logfile, "%s", src);
    fclose(logfile);
    
end:
    mutexUnlock(&g_logfileMutex);
}

void utilsLogFileMutexControl(bool lock)
{
    if (lock)
    {
        mutexLock(&g_logfileMutex);
    } else {
        mutexUnlock(&g_logfileMutex);
    }
}

void utilsReplaceIllegalCharacters(char *str, bool ascii_only)
{
    size_t strsize = 0;
    
    if (!str || !(strsize = strlen(str))) return;
    
    for(size_t i = 0; i < strsize; i++)
    {
        if (memchr("?[]/\\=+<>:;\",*|^", str[i], sizeof("?[]/\\=+<>:;\",*|^") - 1) || str[i] < 0x20 || (!ascii_only && str[i] == 0x7F) || (ascii_only && str[i] >= 0x7F)) str[i] = '_';
    }
}

void utilsTrimString(char *str)
{
    size_t strsize = 0;
    char *start = NULL, *end = NULL;
    
    if (!str || !(strsize = strlen(str))) return;
    
    start = str;
    end = (start + strsize);
    
    while(--end >= start)
    {
        if (!isspace((unsigned char)*end)) break;
    }
    
    *(++end) = '\0';
    
    while(isspace((unsigned char)*start)) start++;
    
    if (start != str) memmove(str, start, end - start + 1);
}

void utilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size)
{
    if (!src || !src_size || !dst || dst_size < ((src_size * 2) + 1)) return;
    
    size_t i, j;
    const u8 *src_u8 = (const u8*)src;
    
    for(i = 0, j = 0; i < src_size; i++)
    {
        char h_nib = ((src_u8[i] >> 4) & 0xF);
        char l_nib = (src_u8[i] & 0xF);
        
        dst[j++] = (h_nib + (h_nib < 0xA ? 0x30 : 0x57));
        dst[j++] = (l_nib + (l_nib < 0xA ? 0x30 : 0x57));
    }
    
    dst[j] = '\0';
}

bool utilsGetFreeSdCardSpace(u64 *out)
{
    return utilsGetFreeFileSystemSpace(g_sdCardFileSystem, out);
}

bool utilsGetFreeFileSystemSpace(FsFileSystem *fs, u64 *out)
{
    if (!fs || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = fsFsGetFreeSpace(fs, "/", (s64*)out);
    if (R_FAILED(rc))
    {
        LOGFILE("fsFsGetFreeSpace failed! (0x%08X).", rc);
        return false;
    }
    
    return true;
}

bool utilsCheckIfFileExists(const char *path)
{
    if (!path || !strlen(path)) return false;
    
    FILE *chkfile = fopen(path, "rb");
    if (chkfile)
    {
        fclose(chkfile);
        return true;
    }
    
    return false;
}

bool utilsCreateConcatenationFile(const char *path)
{
    Result rc = 0;
    
    if (!path || !strlen(path))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Safety check: remove any existant file/directory at the destination path. */
    remove(path);
    fsdevDeleteDirectoryRecursively(path);
    
    /* Create ConcatenationFile */
    /* If the call succeeds, the caller function will be able to operate on this file using stdio calls. */
    rc = fsdevCreateFile(path, 0, FsCreateOption_BigFile);
    if (R_FAILED(rc))
    {
        LOGFILE("fsdevCreateFile failed for \"%s\"! (0x%08X).", path, rc);
        return false;
    }
    
    return true;
}

bool utilsAppletModeCheck(void)
{
    return (g_programAppletType != AppletType_Application && g_programAppletType != AppletType_SystemApplication);
}

void utilsChangeHomeButtonBlockStatus(bool block)
{
    mutexLock(&g_homeButtonMutex);
    
    /* Only change HOME button blocking status if we're running as a regular application or a system application, and if it's current blocking status is different than the requested one. */
    if (!utilsAppletModeCheck() && block != g_homeButtonBlocked)
    {
        if (block)
        {
            appletBeginBlockingHomeButtonShortAndLongPressed(0);
        } else {
            appletEndBlockingHomeButtonShortAndLongPressed();
        }
        
        g_homeButtonBlocked = block;
    }
    
    mutexUnlock(&g_homeButtonMutex);
}

u8 utilsGetCustomFirmwareType(void)
{
    return g_customFirmwareType;
}

FsStorage *utilsGetEmmcBisSystemPartitionStorage(void)
{
    return &g_emmcBisSystemPartitionStorage;
}

void utilsOverclockSystem(bool overclock)
{
    u32 cpuClkRate = ((overclock ? CPU_CLKRT_OVERCLOCKED : CPU_CLKRT_NORMAL) * 1000000);
    u32 memClkRate = ((overclock ? MEM_CLKRT_OVERCLOCKED : MEM_CLKRT_NORMAL) * 1000000);
    servicesChangeHardwareClockRates(cpuClkRate, memClkRate);
}

static u64 utilsHidKeysAllDown(void)
{
    u8 controller;
    u64 keys_down = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keys_down |= hidKeysDown((HidControllerID)controller);
    
    return keys_down;
}

static u64 utilsHidKeysAllHeld(void)
{
    u8 controller;
    u64 keys_held = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keys_held |= hidKeysHeld((HidControllerID)controller);
    
    return keys_held;
}

static bool utilsMountEmmcBisSystemPartitionStorage(void)
{
    Result rc = 0;
    FRESULT fr = FR_OK;
    
    rc = fsOpenBisStorage(&g_emmcBisSystemPartitionStorage, FsBisPartitionId_System);
    if (R_FAILED(rc))
    {
        LOGFILE("Failed to open eMMC BIS System partition storage! (0x%08X).", rc);
        return false;
    }
    
    g_emmcBisSystemPartitionFatFsObj = calloc(1, sizeof(FATFS));
    if (!g_emmcBisSystemPartitionFatFsObj)
    {
        LOGFILE("Unable to allocate memory for FatFs element!");
        return false;
    }
    
    fr = f_mount(g_emmcBisSystemPartitionFatFsObj, BIS_SYSTEM_PARTITION_MOUNT_NAME, 1);
    if (fr != FR_OK)
    {
        LOGFILE("Failed to mount eMMC BIS System partition! (%u).", fr);
        return false;
    }
    
    return true;
}

static void utilsUnmountEmmcBisSystemPartitionStorage(void)
{
    if (g_emmcBisSystemPartitionFatFsObj)
    {
        f_unmount(BIS_SYSTEM_PARTITION_MOUNT_NAME);
        free(g_emmcBisSystemPartitionFatFsObj);
        g_emmcBisSystemPartitionFatFsObj = NULL;
    }
    
    if (serviceIsActive(&(g_emmcBisSystemPartitionStorage.s)))
    {
        fsStorageClose(&g_emmcBisSystemPartitionStorage);
        memset(&g_emmcBisSystemPartitionStorage, 0, sizeof(FsStorage));
    }
}

static void _utilsGetCustomFirmwareType(void)
{
    bool tx_srv = servicesCheckRunningServiceByName("tx");
    bool rnx_srv = servicesCheckRunningServiceByName("rnx");
    g_customFirmwareType = (rnx_srv ? UtilsCustomFirmwareType_ReiNX : (tx_srv ? UtilsCustomFirmwareType_SXOS : UtilsCustomFirmwareType_Atmosphere));
}

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param)
{
    (void)param;
    
    if (hook != AppletHookType_OnOperationMode && hook != AppletHookType_OnPerformanceMode) return;
    
    /* To do: read config here to actually know the value to use with utilsOverclockSystem. */
    utilsOverclockSystem(false);
}
