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
#include "title.h"
#include "bfttf.h"
#include "fatfs/ff.h"

#define LOGFILE_PATH    "./" APP_TITLE ".log"

/* Global variables. */

static bool g_resourcesInitialized = false, g_isDevUnit = false;
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
static const char *g_logfileTimestampFormat = "%d-%02d-%02d %02d:%02d:%02d -> %s: ";
static const char *g_logfileLineBreak = "\r\n";

static const char *g_sizeSuffixes[] = { "B", "KiB", "MiB", "GiB" };
static const u32 g_sizeSuffixesCount = MAX_ELEMENTS(g_sizeSuffixes);

/* Function prototypes. */

static bool _utilsGetCustomFirmwareType(void);

static bool _utilsIsDevelopmentUnit(void);

static bool utilsMountEmmcBisSystemPartitionStorage(void);
static void utilsUnmountEmmcBisSystemPartitionStorage(void);

static bool utilsGetDeviceFileSystemAndFilePathFromAbsolutePath(const char *path, FsFileSystem **out_fs, char **out_filepath);

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param);

bool utilsInitializeResources(void)
{
    mutexLock(&g_resourcesMutex);
    
    bool ret = g_resourcesInitialized;
    if (ret) goto end;
    
    utilsWriteLogBufferToLogFile("________________________________________________________________\r\n");
    LOGFILE(APP_TITLE " v%u.%u.%u starting.", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    
    /* Log Horizon OS version. */
    u32 hos_version = hosversionGet();
    LOGFILE("Horizon OS version: %u.%u.%u.", HOSVER_MAJOR(hos_version), HOSVER_MINOR(hos_version), HOSVER_MICRO(hos_version));
    
    /* Retrieve custom firmware type. */
    if (!_utilsGetCustomFirmwareType()) goto end;
    LOGFILE("Detected %s CFW.", (g_customFirmwareType == UtilsCustomFirmwareType_Atmosphere ? "Atmosphere" : (g_customFirmwareType == UtilsCustomFirmwareType_SXOS ? "SX OS" : "ReiNX")));
    
    /* Initialize needed services. */
    if (!servicesInitialize())
    {
        LOGFILE("Failed to initialize needed services!");
        goto end;
    }
    
    /* Check if we're not running under a development unit. */
    if (!_utilsIsDevelopmentUnit()) goto end;
    LOGFILE("Running under %s unit.", g_isDevUnit ? "development" : "retail");
    
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
    
    /* Initialize title interface. */
    if (!titleInitialize())
    {
        LOGFILE("Failed to initialize the title interface!");
        goto end;
    }
    
    /* Initialize BFTTF interface. */
    if (!bfttfInitialize())
    {
        LOGFILE("Failed to initialize BFTTF interface!");
        goto end;
    }
    
    /* Retrieve pointer to the SD card FsFileSystem element. */
    if (!(g_sdCardFileSystem = fsdevGetDeviceFileSystem("sdmc:")))
    {
        LOGFILE("Failed to retrieve FsFileSystem from SD card!");
        goto end;
    }
    
    /* Mount eMMC BIS System partition. */
    if (!utilsMountEmmcBisSystemPartitionStorage()) goto end;
    
    /* Get applet type. */
    g_programAppletType = appletGetAppletType();
    LOGFILE("Running under %s mode.", utilsAppletModeCheck() ? "applet" : "title override");
    
    /* Disable screen dimming and auto sleep. */
    appletSetMediaPlaybackState(true);
    
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
    
    /* Deinitialize BFTTF interface. */
    bfttfExit();
    
    /* Deinitialize title interface. */
    titleExit();
    
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

bool utilsCreateThread(Thread *out_thread, ThreadFunc func, void *arg, int cpu_id)
{
    /* Core 3 is reserved for HOS, so we can only use cores 0, 1 and 2. */
    /* -2 can be provided to use the default process core. */
    if (!out_thread || !func || (cpu_id < 0 && cpu_id != -2) || cpu_id > 2)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 core_mask = 0;
    size_t stack_size = 0x20000; /* Same value as libnx's newlib. */
    bool success = false;
    
    memset(out_thread, 0, sizeof(Thread));
    
    /* Get process core mask. */
    rc = svcGetInfo(&core_mask, InfoType_CoreMask, CUR_PROCESS_HANDLE, 0);
    if (R_FAILED(rc))
    {
        LOGFILE("svcGetInfo failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Create thread. */
    /* Enable preemptive multithreading by using priority 0x3B. */
    rc = threadCreate(out_thread, func, arg, NULL, stack_size, 0x3B, cpu_id);
    if (R_FAILED(rc))
    {
        LOGFILE("threadCreate failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Set thread core mask. */
    rc = svcSetThreadCoreMask(out_thread->handle, cpu_id == -2 ? -1 : cpu_id, core_mask);
    if (R_FAILED(rc))
    {
        LOGFILE("svcSetThreadCoreMask failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Start thread. */
    rc = threadStart(out_thread);
    if (R_FAILED(rc))
    {
        LOGFILE("threadStart failed! (0x%08X).", rc);
        goto end;
    }
    
    success = true;
    
end:
    if (!success && out_thread->handle != INVALID_HANDLE) threadClose(out_thread);
    
    return success;
}

void utilsJoinThread(Thread *thread)
{
    if (!thread || thread->handle == INVALID_HANDLE)
    {
        LOGFILE("Invalid parameters!");
        return;
    }
    
    Result rc = threadWaitForExit(thread);
    if (R_FAILED(rc))
    {
        LOGFILE("threadWaitForExit failed! (0x%08X).", rc);
        return;
    }
    
    threadClose(thread);
    
    memset(thread, 0, sizeof(Thread));
}

bool utilsIsDevelopmentUnit(void)
{
    mutexLock(&g_resourcesMutex);
    bool ret = (g_resourcesInitialized && g_isDevUnit);
    mutexUnlock(&g_resourcesMutex);
    return ret;
}

u64 utilsHidKeysAllDown(void)
{
    u64 keys_down = 0;
    for(u32 i = 0; i < CONTROLLER_UNKNOWN; i++) keys_down |= hidKeysDown(i);
    return keys_down;
}

u64 utilsHidKeysAllHeld(void)
{
    u64 keys_held = 0;
    for(u32 i = 0; i < CONTROLLER_UNKNOWN; i++) keys_held |= hidKeysHeld(i);
    return keys_held;
}

void utilsWaitForButtonPress(u64 flag)
{
    /* Don't consider touch screen presses nor stick movement as button inputs. */
    if (!flag) flag = ~(KEY_TOUCH | KEY_LSTICK_LEFT | KEY_LSTICK_RIGHT | KEY_LSTICK_UP | KEY_LSTICK_DOWN | KEY_RSTICK_LEFT | KEY_RSTICK_RIGHT | KEY_RSTICK_UP | KEY_RSTICK_DOWN);
    
    while(appletMainLoop())
    {
        hidScanInput();
        u64 keys_down = utilsHidKeysAllDown();
        if (keys_down & flag) break;
    }
}

bool utilsAppendFormattedStringToBuffer(char **dst, size_t *dst_size, const char *fmt, ...)
{
    if (!dst || !dst_size || !fmt || !*fmt)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    va_list args;
    va_start(args, fmt);
    
    int formatted_str_len = 0;
    size_t required_dst_size = 0, dst_str_len = (*dst ? strlen(*dst) : 0);
    char *realloc_dst = NULL;
    
    bool success = false;
    
    if (dst_str_len > *dst_size)
    {
        **dst = '\0';
        dst_str_len = 0;
    }
    
    formatted_str_len = vsnprintf(NULL, 0, fmt, args);
    if (formatted_str_len <= 0)
    {
        LOGFILE("Failed to retrieve formatted string length!");
        goto end;
    }
    
    required_dst_size = (dst_str_len + (size_t)formatted_str_len + 1);
    if (required_dst_size > *dst_size)
    {
        realloc_dst = realloc(*dst, required_dst_size);
        if (!realloc_dst)
        {
            LOGFILE("Failed to reallocate destination buffer!");
            goto end;
        }
        
        *dst = realloc_dst;
        realloc_dst = NULL;
        
        memset(*dst + dst_str_len, 0, (size_t)formatted_str_len + 1);
        
        *dst_size = required_dst_size;
    }
    
    vsprintf(*dst + dst_str_len, fmt, args);
    success = true;
    
end:
    va_end(args);
    
    return success;
}

void utilsWriteMessageToLogFile(const char *func_name, const char *fmt, ...)
{
    if (!func_name || !*func_name || !fmt || !*fmt) return;
    
    mutexLock(&g_logfileMutex);
    
    va_list args;
    
    FILE *logfile = fopen(LOGFILE_PATH, "a+");
    if (!logfile) goto end;
    
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    fprintf(logfile, g_logfileTimestampFormat, ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    
    va_start(args, fmt);
    vfprintf(logfile, fmt, args);
    va_end(args);
    
    fprintf(logfile, g_logfileLineBreak);
    fclose(logfile);
    utilsCommitSdCardFileSystemChanges();
    
end:
    mutexUnlock(&g_logfileMutex);
}

void utilsWriteMessageToLogBuffer(char **dst, size_t *dst_size, const char *func_name, const char *fmt, ...)
{
    if (!dst || !dst_size || !func_name || !*func_name || !fmt || !*fmt) return;
    
    va_list args;
    va_start(args, fmt);
    
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    int timestamp_len = 0, formatted_str_len = 0;
    size_t required_dst_size = 0, dst_str_len = (*dst ? strlen(*dst) : 0);
    char *realloc_dst = NULL;
    
    if (dst_str_len > *dst_size)
    {
        **dst = '\0';
        dst_str_len = 0;
    }
    
    timestamp_len = snprintf(NULL, 0, g_logfileTimestampFormat, ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    if (timestamp_len <= 0) goto end;
    
    formatted_str_len = vsnprintf(NULL, 0, fmt, args);
    if (formatted_str_len <= 0) goto end;
    
    required_dst_size = (dst_str_len + (size_t)timestamp_len + (size_t)formatted_str_len + 3);
    if (required_dst_size > *dst_size)
    {
        realloc_dst = realloc(*dst, required_dst_size);
        if (!realloc_dst) goto end;
        
        *dst = realloc_dst;
        realloc_dst = NULL;
        
        memset(*dst + dst_str_len, 0, (size_t)timestamp_len + (size_t)formatted_str_len + 3);
        
        *dst_size = required_dst_size;
    }
    
    sprintf(*dst + dst_str_len, g_logfileTimestampFormat, ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    vsprintf(*dst + dst_str_len + (size_t)timestamp_len, fmt, args);
    sprintf(*dst + dst_str_len + (size_t)timestamp_len + (size_t)formatted_str_len, g_logfileLineBreak);
    
end:
    va_end(args);
}

void utilsWriteLogBufferToLogFile(const char *src)
{
    if (!src || !*src) return;
    
    mutexLock(&g_logfileMutex);
    
    FILE *logfile = fopen(LOGFILE_PATH, "a+");
    if (!logfile) goto end;
    
    fprintf(logfile, "%s", src);
    fclose(logfile);
    utilsCommitSdCardFileSystemChanges();
    
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

void utilsGenerateFormattedSizeString(u64 size, char *dst, size_t dst_size)
{
    if (!dst || dst_size < 2) return;
    
    double converted_size = (double)size;
    
    for(u32 i = 0; i < g_sizeSuffixesCount; i++)
    {
        if (converted_size >= pow(1024.0, i + 1) && (i + 1) < g_sizeSuffixesCount) continue;
        
        converted_size /= pow(1024.0, i);
        snprintf(dst, dst_size, "%.*f %s", (converted_size >= 100.0 ? 0 : (converted_size >= 10.0 ? 1 : 2)), converted_size, g_sizeSuffixes[i]);
        break;
    }
}

bool utilsGetFreeSpaceFromFileSystem(FsFileSystem *fs, u64 *out)
{
    if (!fs || !serviceIsActive(&(fs->s)) || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = fsFsGetFreeSpace(fs, "/", (s64*)out);
    if (R_FAILED(rc)) LOGFILE("fsFsGetFreeSpace failed! (0x%08X).", rc);
    
    return R_SUCCEEDED(rc);
}

bool utilsGetFreeSpaceFromFileSystemByPath(const char *path, u64 *out)
{
    FsFileSystem *fs = NULL;
    
    if (!utilsGetDeviceFileSystemAndFilePathFromAbsolutePath(path, &fs, NULL) || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    return utilsGetFreeSpaceFromFileSystem(fs, out);
}

bool utilsGetFreeSdCardFileSystemSpace(u64 *out)
{
    return utilsGetFreeSpaceFromFileSystem(g_sdCardFileSystem, out);
}

bool utilsCommitFileSystemChangesByPath(const char *path)
{
    Result rc = 0;
    FsFileSystem *fs = NULL;
    
    if (!utilsGetDeviceFileSystemAndFilePathFromAbsolutePath(path, &fs, NULL)) return false;
    
    rc = fsFsCommit(fs);
    return R_SUCCEEDED(rc);
}

bool utilsCommitSdCardFileSystemChanges(void)
{
    if (!g_sdCardFileSystem) return false;
    Result rc = fsFsCommit(g_sdCardFileSystem);
    return R_SUCCEEDED(rc);
}

bool utilsCheckIfFileExists(const char *path)
{
    if (!path || !*path) return false;
    
    FILE *chkfile = fopen(path, "rb");
    if (chkfile)
    {
        fclose(chkfile);
        return true;
    }
    
    return false;
}

void utilsRemoveConcatenationFile(const char *path)
{
    if (!path || !*path) return;
    remove(path);
    fsdevDeleteDirectoryRecursively(path);
}

bool utilsCreateConcatenationFile(const char *path)
{
    if (!path || !*path)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Safety check: remove any existant file/directory at the destination path. */
    utilsRemoveConcatenationFile(path);
    
    /* Create ConcatenationFile */
    /* If the call succeeds, the caller function will be able to operate on this file using stdio calls. */
    Result rc = fsdevCreateFile(path, 0, FsCreateOption_BigFile);
    if (R_FAILED(rc)) LOGFILE("fsdevCreateFile failed for \"%s\"! (0x%08X).", path, rc);
    
    utilsCommitFileSystemChangesByPath(path);
    
    return R_SUCCEEDED(rc);
}

void utilsCreateDirectoryTree(const char *path, bool create_last_element)
{
    char *ptr = NULL, *tmp = NULL;
    size_t path_len = 0;
    
    if (!path || !(path_len = strlen(path))) return;
    
    tmp = calloc(path_len + 1, sizeof(char));
    if (!tmp) return;
    
    ptr = strchr(path, '/');
    while(ptr)
    {
        sprintf(tmp, "%.*s", (int)(ptr - path), path);
        mkdir(tmp, 0777);
        ptr = strchr(++ptr, '/');
    }
    
    if (create_last_element) mkdir(path, 0777);
    
    free(tmp);
    
    utilsCommitFileSystemChangesByPath(path);
}

char *utilsGeneratePath(const char *prefix, const char *filename, const char *extension)
{
    if (!prefix || !*prefix || !filename || !*filename || !extension || !*extension)
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    char *path = NULL;
    size_t path_len = (strlen(prefix) + strlen(filename) + strlen(extension) + 1);
    
    if (!(path = calloc(path_len, sizeof(char))))
    {
        LOGFILE("Failed to allocate 0x%lX bytes for output path!", path_len);
        return NULL;
    }
    
    sprintf(path, "%s%s%s", prefix, filename, extension);
    
    return path;
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

static bool _utilsGetCustomFirmwareType(void)
{
    bool has_service = false, tx_srv = false, rnx_srv = false;
    
    /* First, check if we're running under Atmosphere or an Atmosphere-based CFW by using a SM API extension that's only provided by it. */
    if (R_SUCCEEDED(servicesAtmosphereHasService(&has_service, "ncm")))
    {
        /* We're running under Atmosphere or an Atmosphere-based CFW. Time to check which one is it. */
        tx_srv = (R_SUCCEEDED(servicesAtmosphereHasService(&has_service, "tx")) && has_service);
        rnx_srv = (R_SUCCEEDED(servicesAtmosphereHasService(&has_service, "rnx")) && has_service);
    } else {
        /* Odds are we're not running under Atmosphere, or maybe we're running under an old Atmosphere version without SM API extensions. */
        /* We'll use the smRegisterService() trick to check for running services. */
        /* But first, we need to re-initialize SM in order to avoid 0xF601 (port remote dead) errors. */
        smExit();
        
        Result rc = smInitialize();
        if (R_FAILED(rc))
        {
            LOGFILE("smInitialize failed! (0x%08X).", rc);
            return false;
        }
        
        tx_srv = servicesCheckRunningServiceByName("tx");
        rnx_srv = servicesCheckRunningServiceByName("rnx");
    }
    
    /* Finally, determine the CFW type. */
    g_customFirmwareType = (rnx_srv ? UtilsCustomFirmwareType_ReiNX : (tx_srv ? UtilsCustomFirmwareType_SXOS : UtilsCustomFirmwareType_Atmosphere));
    
    return true;
}

static bool _utilsIsDevelopmentUnit(void)
{
    Result rc = 0;
    bool tmp = false;
    
    rc = splIsDevelopment(&tmp);
    if (R_SUCCEEDED(rc))
    {
        g_isDevUnit = tmp;
    } else {
        LOGFILE("splIsDevelopment failed! (0x%08X).", rc);
    }
    
    return R_SUCCEEDED(rc);
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

static bool utilsGetDeviceFileSystemAndFilePathFromAbsolutePath(const char *path, FsFileSystem **out_fs, char **out_filepath)
{
    FsFileSystem *fs = NULL;
    char *name_end = NULL, *filepath = NULL, name[32] = {0};
    
    if (!path || !*path || !(name_end = strchr(path, ':')) || (size_t)(name_end - path) >= MAX_ELEMENTS(name) || (!out_fs && !out_filepath) || \
        (out_filepath && *(filepath = (name_end + 1)) != '/')) return false;
    
    sprintf(name, "%.*s", (int)(name_end - path), path);
    
    fs = fsdevGetDeviceFileSystem(name);
    if (!fs) return false;
    
    if (out_fs) *out_fs = fs;
    if (out_filepath) *out_filepath = filepath;
    
    return true;
}

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param)
{
    (void)param;
    
    if (hook != AppletHookType_OnOperationMode && hook != AppletHookType_OnPerformanceMode) return;
    
    /* To do: read config here to actually know the value to use with utilsOverclockSystem. */
    utilsOverclockSystem(false);
}
