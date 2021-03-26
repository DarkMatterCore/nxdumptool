/*
 * nxdt_utils.c
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

#include <sys/statvfs.h>

#include "nxdt_utils.h"
#include "keys.h"
#include "gamecard.h"
#include "services.h"
#include "nca.h"
#include "usb.h"
#include "title.h"
#include "bfttf.h"
#include "fatfs/ff.h"

/* Global variables. */

static bool g_resourcesInit = false, g_isDevUnit = false;
static Mutex g_resourcesMutex = 0;

static PadState g_padState = {0};

static FsFileSystem *g_sdCardFileSystem = NULL;

static FsStorage g_emmcBisSystemPartitionStorage = {0};
static FATFS *g_emmcBisSystemPartitionFatFsObj = NULL;

static AppletType g_programAppletType = 0;
static bool g_homeButtonBlocked = false;
static Mutex g_homeButtonMutex = 0;

static u8 g_customFirmwareType = UtilsCustomFirmwareType_Unknown;

static AppletHookCookie g_systemOverclockCookie = {0};

static const char *g_sizeSuffixes[] = { "B", "KiB", "MiB", "GiB" };
static const u32 g_sizeSuffixesCount = MAX_ELEMENTS(g_sizeSuffixes);

static const char *g_illegalFileSystemChars = "\\/:*?\"<>|^";

/* Function prototypes. */

static void _utilsGetCustomFirmwareType(void);

static bool _utilsIsDevelopmentUnit(void);

static bool utilsMountEmmcBisSystemPartitionStorage(void);
static void utilsUnmountEmmcBisSystemPartitionStorage(void);

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param);

static void utilsPrintConsoleError(void);

bool utilsInitializeResources(void)
{
    mutexLock(&g_resourcesMutex);
    
    bool ret = g_resourcesInit;
    if (ret) goto end;
    
    /* Configure input. */
    /* Up to 8 different, full controller inputs. */
    /* Individual Joy-Cons not supported. */
    padConfigureInput(8, HidNpadStyleSet_NpadFullCtrl);
    padInitializeWithMask(&g_padState, 0x1000000FFUL);
    
    /* Retrieve pointer to the application launch path. */
    if (g_argc && g_argv)
    {
        for(int i = 0; i < g_argc; i++)
        {
            if (g_argv[i] && !strncmp(g_argv[i], "sdmc:/", 6))
            {
                g_appLaunchPath = (const char*)g_argv[i];
                break;
            }
        }
    }
    
    /* Retrieve pointer to the SD card FsFileSystem element. */
    if (!(g_sdCardFileSystem = fsdevGetDeviceFileSystem("sdmc:"))) goto end;
    
    /* Create logfile. */
    logWriteStringToLogFile("________________________________________________________________\r\n");
    LOG_MSG(APP_TITLE " v%u.%u.%u starting. Built on " __DATE__ " - " __TIME__ ".", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
    if (g_appLaunchPath) LOG_MSG("Launch path: \"%s\".", g_appLaunchPath);
    
    /* Log Horizon OS version. */
    u32 hos_version = hosversionGet();
    LOG_MSG("Horizon OS version: %u.%u.%u.", HOSVER_MAJOR(hos_version), HOSVER_MINOR(hos_version), HOSVER_MICRO(hos_version));
    
    /* Retrieve custom firmware type. */
    _utilsGetCustomFirmwareType();
    LOG_MSG("Detected %s CFW.", (g_customFirmwareType == UtilsCustomFirmwareType_Atmosphere ? "Atmosphère" : (g_customFirmwareType == UtilsCustomFirmwareType_SXOS ? "SX OS" : "ReiNX")));
    
    /* Initialize needed services. */
    if (!servicesInitialize())
    {
        LOG_MSG("Failed to initialize needed services!");
        goto end;
    }
    
    /* Check if we're not running under a development unit. */
    if (!_utilsIsDevelopmentUnit()) goto end;
    LOG_MSG("Running under %s unit.", g_isDevUnit ? "development" : "retail");
    
    /* Get applet type. */
    g_programAppletType = appletGetAppletType();
    LOG_MSG("Running under %s mode.", utilsAppletModeCheck() ? "applet" : "title override");
    
    /* Initialize USB interface. */
    if (!usbInitialize())
    {
        LOG_MSG("Failed to initialize USB interface!");
        goto end;
    }
    
    /* Initialize USB Mass Storage interface. */
    if (!umsInitialize()) goto end;
    
    /* Load NCA keyset. */
    if (!keysLoadNcaKeyset())
    {
        LOG_MSG("Failed to load NCA keyset!");
        goto end;
    }
    
    /* Allocate NCA crypto buffer. */
    if (!ncaAllocateCryptoBuffer())
    {
        LOG_MSG("Unable to allocate memory for NCA crypto buffer!");
        goto end;
    }
    
    /* Initialize gamecard interface. */
    if (!gamecardInitialize())
    {
        LOG_MSG("Failed to initialize gamecard interface!");
        goto end;
    }
    
    /* Initialize title interface. */
    if (!titleInitialize())
    {
        LOG_MSG("Failed to initialize the title interface!");
        goto end;
    }
    
    /* Initialize BFTTF interface. */
    if (!bfttfInitialize())
    {
        LOG_MSG("Failed to initialize BFTTF interface!");
        goto end;
    }
    
    /* Mount eMMC BIS System partition. */
    if (!utilsMountEmmcBisSystemPartitionStorage()) goto end;
    
    /* Disable screen dimming and auto sleep. */
    appletSetMediaPlaybackState(true);
    
    /* Overclock system. */
    utilsOverclockSystem(true);
    
    /* Setup an applet hook to change the hardware clocks after a system mode change (docked <-> undocked). */
    appletHook(&g_systemOverclockCookie, utilsOverclockSystemAppletHook, NULL);
    
    ret = g_resourcesInit = true;
    
end:
    mutexUnlock(&g_resourcesMutex);
    
    if (!ret) utilsPrintConsoleError();
    
    return ret;
}

void utilsCloseResources(void)
{
    mutexLock(&g_resourcesMutex);
    
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
    
    /* Close USB Mass Storage interface. */
    umsExit();
    
    /* Close USB interface. */
    usbExit();
    
    /* Close initialized services. */
    servicesClose();
    
    /* Close logfile. */
    logCloseLogFile();
    
    g_resourcesInit = false;
    
    mutexUnlock(&g_resourcesMutex);
}

bool utilsCreateThread(Thread *out_thread, ThreadFunc func, void *arg, int cpu_id)
{
    /* Core 3 is reserved for HOS, so we can only use cores 0, 1 and 2. */
    /* -2 can be provided to use the default process core. */
    if (!out_thread || !func || (cpu_id < 0 && cpu_id != -2) || cpu_id > 2)
    {
        LOG_MSG("Invalid parameters!");
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
        LOG_MSG("svcGetInfo failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Create thread. */
    /* Enable preemptive multithreading by using priority 0x3B. */
    rc = threadCreate(out_thread, func, arg, NULL, stack_size, 0x3B, cpu_id);
    if (R_FAILED(rc))
    {
        LOG_MSG("threadCreate failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Set thread core mask. */
    rc = svcSetThreadCoreMask(out_thread->handle, cpu_id == -2 ? -1 : cpu_id, core_mask);
    if (R_FAILED(rc))
    {
        LOG_MSG("svcSetThreadCoreMask failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Start thread. */
    rc = threadStart(out_thread);
    if (R_FAILED(rc))
    {
        LOG_MSG("threadStart failed! (0x%08X).", rc);
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
        LOG_MSG("Invalid parameters!");
        return;
    }
    
    Result rc = threadWaitForExit(thread);
    if (R_FAILED(rc))
    {
        LOG_MSG("threadWaitForExit failed! (0x%08X).", rc);
        return;
    }
    
    threadClose(thread);
    
    memset(thread, 0, sizeof(Thread));
}

bool utilsIsDevelopmentUnit(void)
{
    mutexLock(&g_resourcesMutex);
    bool ret = (g_resourcesInit && g_isDevUnit);
    mutexUnlock(&g_resourcesMutex);
    return ret;
}

void utilsScanPads(void)
{
    padUpdate(&g_padState);
}

u64 utilsGetButtonsDown(void)
{
    return padGetButtonsDown(&g_padState);
}

u64 utilsGetButtonsHeld(void)
{
    return padGetButtons(&g_padState);
}

void utilsWaitForButtonPress(u64 flag)
{
    /* Don't consider stick movement as button inputs. */
    if (!flag) flag = ~(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | \
                        HidNpadButton_StickRUp | HidNpadButton_StickRDown);
    
    while(appletMainLoop())
    {
        utilsScanPads();
        if (utilsGetButtonsDown() & flag) break;
    }
}

bool utilsAppendFormattedStringToBuffer(char **dst, size_t *dst_size, const char *fmt, ...)
{
    if (!dst || !dst_size || (!*dst && *dst_size) || (*dst && !*dst_size) || !fmt || !*fmt)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    va_list args;
    
    int formatted_str_len = 0;
    size_t formatted_str_len_cast = 0;
    
    char *dst_ptr = *dst, *tmp_str = NULL;
    size_t dst_cur_size = *dst_size, dst_str_len = (dst_ptr ? strlen(dst_ptr) : 0);
    
    bool success = false;
    
    if (dst_cur_size && dst_str_len >= dst_cur_size)
    {
        LOG_MSG("String length is equal to or greater than the provided buffer size! (0x%lX >= 0x%lX).", dst_str_len, dst_cur_size);
        return false;
    }
    
    va_start(args, fmt);
    
    /* Get formatted string length. */
    formatted_str_len = vsnprintf(NULL, 0, fmt, args);
    if (formatted_str_len <= 0)
    {
        LOG_MSG("Failed to retrieve formatted string length!");
        goto end;
    }
    
    formatted_str_len_cast = (size_t)(formatted_str_len + 1);
    
    if (!dst_cur_size || formatted_str_len_cast > (dst_cur_size - dst_str_len))
    {
        /* Update buffer size. */
        dst_cur_size = (dst_str_len + formatted_str_len_cast);
        
        /* Reallocate buffer. */
        tmp_str = realloc(dst_ptr, dst_cur_size);
        if (!tmp_str)
        {
            LOG_MSG("Failed to resize buffer to 0x%lX byte(s).", dst_cur_size);
            goto end;
        }
        
        dst_ptr = tmp_str;
        tmp_str = NULL;
        
        /* Clear allocated area. */
        memset(dst_ptr + dst_str_len, 0, formatted_str_len_cast);
        
        /* Update pointers. */
        *dst = dst_ptr;
        *dst_size = dst_cur_size;
    }
    
    /* Generate formatted string. */
    vsprintf(dst_ptr + dst_str_len, fmt, args);
    success = true;
    
end:
    va_end(args);
    
    return success;
}

void utilsReplaceIllegalCharacters(char *str, bool ascii_only)
{
    size_t strsize = 0;
    
    if (!str || !(strsize = strlen(str))) return;
    
    for(size_t i = 0; i < strsize; i++)
    {
        if (memchr(g_illegalFileSystemChars, str[i], sizeof(g_illegalFileSystemChars) - 1) || str[i] < 0x20 || (!ascii_only && str[i] == 0x7F) || (ascii_only && str[i] >= 0x7F)) str[i] = '_';
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

bool utilsGetFileSystemStatsByPath(const char *path, u64 *out_total, u64 *out_free)
{
    char *name_end = NULL, stat_path[32] = {0};
    struct statvfs info = {0};
    int ret = -1;
    
    if (!path || !*path || !(name_end = strchr(path, ':')) || *(name_end + 1) != '/' || (!out_total && !out_free))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    name_end += 2;
    sprintf(stat_path, "%.*s", (int)(name_end - path), path);
    
    if ((ret = statvfs(stat_path, &info)) != 0)
    {
        LOG_MSG("statvfs failed! (%d) (errno: %d).", ret, errno);
        return false;
    }
    
    if (out_total) *out_total = ((u64)info.f_blocks * (u64)info.f_frsize);
    if (out_free) *out_free = ((u64)info.f_bfree * (u64)info.f_frsize);
    
    return true;
}

FsFileSystem *utilsGetSdCardFileSystemObject(void)
{
    return g_sdCardFileSystem;
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
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Safety check: remove any existant file/directory at the destination path. */
    utilsRemoveConcatenationFile(path);
    
    /* Create ConcatenationFile */
    /* If the call succeeds, the caller function will be able to operate on this file using stdio calls. */
    Result rc = fsdevCreateFile(path, 0, FsCreateOption_BigFile);
    if (R_FAILED(rc)) LOG_MSG("fsdevCreateFile failed for \"%s\"! (0x%08X).", path, rc);
    
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
}

char *utilsGeneratePath(const char *prefix, const char *filename, const char *extension)
{
    if (!filename || !*filename || !extension || !*extension)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }
    
    char *path = NULL;
    size_t path_len = (strlen(filename) + strlen(extension) + 1);
    if (prefix && *prefix) path_len += strlen(prefix);
    
    if (!(path = calloc(path_len, sizeof(char))))
    {
        LOG_MSG("Failed to allocate 0x%lX bytes for output path!", path_len);
        return NULL;
    }
    
    if (prefix && *prefix) strcat(path, prefix);
    strcat(path, filename);
    strcat(path, extension);
    
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

static void _utilsGetCustomFirmwareType(void)
{
    bool has_srv = false, tx_srv = false, rnx_srv = false;
    
    tx_srv = (R_SUCCEEDED(servicesHasService(&has_srv, "tx")) && has_srv);
    rnx_srv = (R_SUCCEEDED(servicesHasService(&has_srv, "rnx")) && has_srv);
    
    g_customFirmwareType = (rnx_srv ? UtilsCustomFirmwareType_ReiNX : (tx_srv ? UtilsCustomFirmwareType_SXOS : UtilsCustomFirmwareType_Atmosphere));
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
        LOG_MSG("splIsDevelopment failed! (0x%08X).", rc);
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
        LOG_MSG("Failed to open eMMC BIS System partition storage! (0x%08X).", rc);
        return false;
    }
    
    g_emmcBisSystemPartitionFatFsObj = calloc(1, sizeof(FATFS));
    if (!g_emmcBisSystemPartitionFatFsObj)
    {
        LOG_MSG("Unable to allocate memory for FatFs element!");
        return false;
    }
    
    fr = f_mount(g_emmcBisSystemPartitionFatFsObj, BIS_SYSTEM_PARTITION_MOUNT_NAME, 1);
    if (fr != FR_OK)
    {
        LOG_MSG("Failed to mount eMMC BIS System partition! (%u).", fr);
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

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param)
{
    (void)param;
    
    if (hook != AppletHookType_OnOperationMode && hook != AppletHookType_OnPerformanceMode) return;
    
    /* TO DO: read config here to actually know the value to use with utilsOverclockSystem. */
    utilsOverclockSystem(false);
}

static void utilsPrintConsoleError(void)
{
    char msg[0x100] = {0};
    logGetLastMessage(msg, sizeof(msg));
    
    consoleInit(NULL);
    
    printf("An error occurred while initializing resources.\n\n");
    if (*msg) printf("%s\n\n", msg);
    printf("For more information, please check the logfile. Press any button to exit.");
    
    consoleUpdate(NULL);
    
    utilsWaitForButtonPress(0);
    
    consoleExit(NULL);
}
