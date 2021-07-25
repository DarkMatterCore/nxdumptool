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
#include "nxdt_bfsar.h"
#include "fatfs/ff.h"

/* Reference: https://docs.microsoft.com/en-us/windows/win32/fileio/filesystem-functionality-comparison#limits. */
#define NT_MAX_FILENAME_LENGTH  255

/* Global variables. */

static bool g_resourcesInit = false;
static Mutex g_resourcesMutex = 0;

static FsFileSystem *g_sdCardFileSystem = NULL;

static const char *g_appLaunchPath = NULL;

static u8 g_customFirmwareType = UtilsCustomFirmwareType_Unknown;

static bool g_isDevUnit = false;

static AppletType g_programAppletType = AppletType_None;

static FsStorage g_emmcBisSystemPartitionStorage = {0};
static FATFS *g_emmcBisSystemPartitionFatFsObj = NULL;

static AppletHookCookie g_systemOverclockCookie = {0};

static bool g_homeButtonBlocked = false;

static int g_nxLinkSocketFd = -1;

static const char *g_sizeSuffixes[] = { "B", "KiB", "MiB", "GiB" };
static const u32 g_sizeSuffixesCount = MAX_ELEMENTS(g_sizeSuffixes);

static const char g_illegalFileSystemChars[] = "\\/:*?\"<>|";
static const size_t g_illegalFileSystemCharsLength = (MAX_ELEMENTS(g_illegalFileSystemChars) - 1);

static const char *g_outputDirs[] = {
    HBMENU_BASE_PATH,
    APP_BASE_PATH,
    GAMECARD_PATH,
    CERT_PATH,
    HFS_PATH,
    NSP_PATH,
    TICKET_PATH,
    NCA_PATH,
    NCA_FS_PATH
};

static const size_t g_outputDirsCount = MAX_ELEMENTS(g_outputDirs);

/* Function prototypes. */

static void _utilsGetLaunchPath(int program_argc, const char **program_argv);

static void _utilsGetCustomFirmwareType(void);

static bool _utilsIsDevelopmentUnit(void);

static bool _utilsAppletModeCheck(void);

static bool utilsMountEmmcBisSystemPartitionStorage(void);
static void utilsUnmountEmmcBisSystemPartitionStorage(void);

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param);

static void utilsPrintConsoleError(void);

static size_t utilsGetUtf8CodepointCount(const char *str, size_t str_size, size_t cp_limit, size_t *last_cp_pos);

bool utilsInitializeResources(const int program_argc, const char **program_argv)
{
    Result rc = 0;
    bool ret = false;
    
    SCOPED_LOCK(&g_resourcesMutex)
    {
        ret = g_resourcesInit;
        if (ret) break;
        
        /* Retrieve pointer to the application launch path. */
        _utilsGetLaunchPath(program_argc, program_argv);
        
        /* Retrieve pointer to the SD card FsFileSystem element. */
        if (!(g_sdCardFileSystem = fsdevGetDeviceFileSystem("sdmc:")))
        {
            LOG_MSG("Failed to retrieve FsFileSystem object for the SD card!");
            break;
        }
        
        /* Create logfile. */
        logWriteStringToLogFile("________________________________________________________________\r\n");
        LOG_MSG(APP_TITLE " v%u.%u.%u starting (" GIT_REV "). Built on " __DATE__ " - " __TIME__ ".", VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO);
        if (g_appLaunchPath) LOG_MSG("Launch path: \"%s\".", g_appLaunchPath);
        
        /* Log Horizon OS version. */
        u32 hos_version = hosversionGet();
        LOG_MSG("Horizon OS version: %u.%u.%u.", HOSVER_MAJOR(hos_version), HOSVER_MINOR(hos_version), HOSVER_MICRO(hos_version));
        
        /* Initialize needed services. */
        if (!servicesInitialize()) break;
        
        /* Retrieve custom firmware type. */
        _utilsGetCustomFirmwareType();
        if (g_customFirmwareType != UtilsCustomFirmwareType_Unknown) LOG_MSG("Detected %s CFW.", (g_customFirmwareType == UtilsCustomFirmwareType_Atmosphere ? "Atmosphère" : \
                                                                                                 (g_customFirmwareType == UtilsCustomFirmwareType_SXOS ? "SX OS" : "ReiNX")));
        
        /* Check if we're not running under a development unit. */
        if (!_utilsIsDevelopmentUnit()) break;
        LOG_MSG("Running under %s unit.", g_isDevUnit ? "development" : "retail");
        
        /* Get applet type. */
        g_programAppletType = appletGetAppletType();
        LOG_MSG("Running under %s mode.", _utilsAppletModeCheck() ? "applet" : "title override");
        
        /* Create output directories (SD card only). */
        utilsCreateOutputDirectories(NULL);
        
        /* Initialize USB interface. */
        if (!usbInitialize()) break;
        
        /* Initialize USB Mass Storage interface. */
        if (!umsInitialize()) break;
        
        /* Load keyset. */
        if (!keysLoadKeyset())
        {
            LOG_MSG("Failed to load keyset!\nUpdate your keys file with Lockpick_RCM:\n" LOCKPICK_RCM_URL);
            break;
        }
        
        /* Allocate NCA crypto buffer. */
        if (!ncaAllocateCryptoBuffer())
        {
            LOG_MSG("Unable to allocate memory for NCA crypto buffer!");
            break;
        }
        
        /* Initialize gamecard interface. */
        if (!gamecardInitialize()) break;
        
        /* Initialize title interface. */
        if (!titleInitialize()) break;
        
        /* Initialize BFTTF interface. */
        if (!bfttfInitialize()) break;
        
        /* Initialize BFSAR interface. */
        //if (!bfsarInitialize()) break;
        
        /* Mount eMMC BIS System partition. */
        if (!utilsMountEmmcBisSystemPartitionStorage()) break;
        
        /* Mount application RomFS. */
        rc = romfsInit();
        if (R_FAILED(rc))
        {
            LOG_MSG("Failed to mount RomFS container!");
            break;
        }
        
        /* Load configuration. */
        if (!configInitialize()) break;
        
        /* Overclock system. */
        utilsOverclockSystem(configGetBoolean("overclock"));
        
        /* Setup an applet hook to change the hardware clocks after a system mode change (docked <-> undocked). */
        appletHook(&g_systemOverclockCookie, utilsOverclockSystemAppletHook, NULL);
        
        /* Enable video recording if we're running under title override mode. */
        if (!_utilsAppletModeCheck())
        {
            bool flag = false;
            rc = appletIsGamePlayRecordingSupported(&flag);
            if (R_SUCCEEDED(rc) && flag) appletInitializeGamePlayRecording();
        }
        
        /* Disable screen dimming and auto sleep. */
        /* TODO: only use this function while dealing with a dump process - make sure to handle power button presses as well. */
        appletSetMediaPlaybackState(true);
        
        /* Initialize socket driver. */
        rc = socketInitializeDefault();
        if (R_FAILED(rc))
        {
            LOG_MSG("socketInitializeDefault failed! (0x%08X).", rc);
            break;
        }
        
        /* Initialize CURL. */
        curl_global_init(CURL_GLOBAL_ALL);
        
        /* Redirect stdout and stderr over network to nxlink. */
        g_nxLinkSocketFd = nxlinkConnectToHost(true, true);
        
        /* Update flags. */
        ret = g_resourcesInit = true;
    }
    
    if (!ret) utilsPrintConsoleError();
    
    return ret;
}

void utilsCloseResources(void)
{
    SCOPED_LOCK(&g_resourcesMutex)
    {
        /* Cleanup CURL. */
        curl_global_cleanup();
        
        /* Close nxlink socket. */
        if (g_nxLinkSocketFd >= 0)
        {
            close(g_nxLinkSocketFd);
            g_nxLinkSocketFd = -1;
        }
        
        /* Deinitialize socket driver. */
        socketExit();
        
        /* Enable screen dimming and auto sleep. */
        /* TODO: only use this function while dealing with a dump process - make sure to handle power button presses as well. */
        appletSetMediaPlaybackState(false);
        
        /* Unblock HOME button presses. */
        utilsChangeHomeButtonBlockStatus(false);
        
        /* Unset our overclock applet hook. */
        appletUnhook(&g_systemOverclockCookie);
        
        /* Restore hardware clocks. */
        utilsOverclockSystem(false);
        
        /* Close configuration interface. */
        configExit();
        
        /* Unmount application RomFS. */
        romfsExit();
        
        /* Unmount eMMC BIS System partition. */
        utilsUnmountEmmcBisSystemPartitionStorage();
        
        /* Deinitialize BFSAR interface. */
        bfsarExit();
        
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
    }
}

const char *utilsGetLaunchPath(void)
{
    return g_appLaunchPath;
}

FsFileSystem *utilsGetSdCardFileSystemObject(void)
{
    return g_sdCardFileSystem;
}

bool utilsCommitSdCardFileSystemChanges(void)
{
    return (g_sdCardFileSystem ? R_SUCCEEDED(fsFsCommit(g_sdCardFileSystem)) : false);
}

u8 utilsGetCustomFirmwareType(void)
{
    return g_customFirmwareType;
}

bool utilsIsDevelopmentUnit(void)
{
    return g_isDevUnit;
}

bool utilsAppletModeCheck(void)
{
    return _utilsAppletModeCheck();
}

FsStorage *utilsGetEmmcBisSystemPartitionStorage(void)
{
    return &g_emmcBisSystemPartitionStorage;
}

void utilsOverclockSystem(bool overclock)
{
    u32 cpu_rate = ((overclock ? CPU_CLKRT_OVERCLOCKED : CPU_CLKRT_NORMAL) * 1000000);
    u32 mem_rate = ((overclock ? MEM_CLKRT_OVERCLOCKED : MEM_CLKRT_NORMAL) * 1000000);
    servicesChangeHardwareClockRates(cpu_rate, mem_rate);
}

void utilsChangeHomeButtonBlockStatus(bool block)
{
    SCOPED_LOCK(&g_resourcesMutex)
    {
        /* Only change HOME button blocking status if we're running as a regular application or a system application, and if its current blocking status is different than the requested one. */
        if (_utilsAppletModeCheck() || block == g_homeButtonBlocked) break;
        
        if (block)
        {
            appletBeginBlockingHomeButtonShortAndLongPressed(0);
        } else {
            appletEndBlockingHomeButtonShortAndLongPressed();
        }
        
        g_homeButtonBlocked = block;
    }
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

__attribute__((format(printf, 3, 4))) bool utilsAppendFormattedStringToBuffer(char **dst, size_t *dst_size, const char *fmt, ...)
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
    size_t str_size = 0, cur_pos = 0;
    
    if (!str || !(str_size = strlen(str))) return;
    
    u8 *ptr1 = (u8*)str, *ptr2 = ptr1;
    ssize_t units = 0;
    u32 code = 0;
    
    while(cur_pos < str_size)
    {
        units = decode_utf8(&code, ptr1);
        if (units < 0) break;
        
        if (memchr(g_illegalFileSystemChars, (int)code, g_illegalFileSystemCharsLength) || code < 0x20 || (!ascii_only && code == 0x7F) || (ascii_only && code >= 0x7F))
        {
            *ptr2++ = '_';
        } else {
            if (ptr2 != ptr1) memmove(ptr2, ptr1, (size_t)units);
            ptr2 += units;
        }
        
        ptr1 += units;
        cur_pos += (size_t)units;
    }
    
    *ptr2 = '\0';
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

void utilsGenerateHexStringFromData(char *dst, size_t dst_size, const void *src, size_t src_size, bool uppercase)
{
    if (!src || !src_size || !dst || dst_size < ((src_size * 2) + 1)) return;
    
    size_t i, j;
    const u8 *src_u8 = (const u8*)src;
    
    for(i = 0, j = 0; i < src_size; i++)
    {
        char h_nib = ((src_u8[i] >> 4) & 0xF);
        char l_nib = (src_u8[i] & 0xF);
        
        dst[j++] = (h_nib + (h_nib < 0xA ? 0x30 : (uppercase ? 0x37 : 0x57)));
        dst[j++] = (l_nib + (l_nib < 0xA ? 0x30 : (uppercase ? 0x37 : 0x57)));
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

void utilsCreateOutputDirectories(const char *device)
{
    size_t device_len = 0;
    char path[FS_MAX_PATH] = {0};
    
    if (device && (!(device_len = strlen(device)) || device[device_len - 1] != ':'))
    {
        LOG_MSG("Invalid parameters!");
        return;
    }
    
    for(size_t i = 0; i < g_outputDirsCount; i++)
    {
        sprintf(path, "%s%s", (device ? device : "sdmc:"), g_outputDirs[i]);
        mkdir(path, 0744);
    }
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
    
    /* Safety measure: remove any existant file/directory at the destination path. */
    utilsRemoveConcatenationFile(path);
    
    /* Create ConcatenationFile. */
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
    if (!filename || !*filename)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }
    
    bool use_prefix = (prefix && *prefix);
    size_t prefix_len = (use_prefix ? strlen(prefix) : 0);
    bool append_path_sep = (use_prefix && prefix[prefix_len - 1] != '/');
    
    bool use_extension = (extension && *extension);
    size_t extension_len = (use_extension ? strlen(extension) : 0);
    bool append_dot = (use_extension && *extension != '.');
    
    size_t path_len = (prefix_len + strlen(filename) + extension_len);
    if (append_path_sep) path_len++;
    if (append_dot) path_len++;
    
    char *path = NULL, *ptr1 = NULL, *ptr2 = NULL;
    bool filename_only = false, success = false;
    
    /* Allocate memory for the output path. */
    if (!(path = calloc(path_len + 1, sizeof(char))))
    {
        LOG_MSG("Failed to allocate 0x%lX bytes for output path!", path_len);
        goto end;
    }
    
    /* Generate output path. */
    if (use_prefix) strcat(path, prefix);
    if (append_path_sep) strcat(path, "/");
    strcat(path, filename);
    if (append_dot) strcat(path, ".");
    if (use_extension) strcat(path, extension);
    
    /* Retrieve pointer to the first path separator. */
    ptr1 = strchr(path, '/');
    if (!ptr1)
    {
        filename_only = true;
        ptr1 = path;
    }
    
    /* Make sure each path element doesn't exceed NT_MAX_FILENAME_LENGTH. */
    while(ptr1)
    {
        /* End loop if we find a NULL terminator. */
        if (!filename_only && !*ptr1++) break;
        
        /* Get pointer to next path separator. */
        ptr2 = strchr(ptr1, '/');
        
        /* Get current path element size. */
        size_t element_size = (ptr2 ? (size_t)(ptr2 - ptr1) : (path_len - (size_t)(ptr1 - path)));
        
        /* Get UTF-8 codepoint count. */
        /* Use NT_MAX_FILENAME_LENGTH as the codepoint count limit. */
        size_t last_cp_pos = 0;
        size_t cp_count = utilsGetUtf8CodepointCount(ptr1, element_size, NT_MAX_FILENAME_LENGTH, &last_cp_pos);
        if (cp_count > NT_MAX_FILENAME_LENGTH)
        {
            if (ptr2)
            {
                /* Truncate current element by moving the rest of the path to the current position. */
                memmove(ptr1 + last_cp_pos, ptr2, path_len - (size_t)(ptr2 - path));
                
                /* Update pointer. */
                ptr2 -= (element_size - last_cp_pos);
            } else
            if (use_extension)
            {
                /* Truncate last element. Make sure to preserve the provided file extension. */
                size_t diff = extension_len;
                if (append_dot) diff++;
                
                if (diff >= last_cp_pos)
                {
                    LOG_MSG("File extension length is >= truncated filename length! (0x%lX >= 0x%lX) (#1).", diff, last_cp_pos);
                    goto end;
                }
                
                memmove(ptr1 + last_cp_pos - diff, ptr1 + element_size - diff, diff);
            }
            
            path_len -= (element_size - last_cp_pos);
            path[path_len] = '\0';
        }
        
        ptr1 = ptr2;
    }
    
    /* Check if the full length for the generated path is >= FS_MAX_PATH. */
    if (path_len >= FS_MAX_PATH)
    {
        LOG_MSG("Generated path length is >= FS_MAX_PATH! (0x%lX).", path_len);
        goto end;
    }
    
    /* Update flag. */
    success = true;
    
end:
    if (!success && path)
    {
        free(path);
        path = NULL;
    }
    
    return path;
}

static void _utilsGetLaunchPath(int program_argc, const char **program_argv)
{
    if (program_argc <= 0 || !program_argv) return;
    
    for(int i = 0; i < program_argc; i++)
    {
        if (program_argv[i] && !strncmp(program_argv[i], "sdmc:/", 6))
        {
            g_appLaunchPath = program_argv[i];
            break;
        }
    }
}

static void _utilsGetCustomFirmwareType(void)
{
    bool tx_srv = servicesCheckRunningServiceByName("tx");
    bool rnx_srv = servicesCheckRunningServiceByName("rnx");
    
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

static bool _utilsAppletModeCheck(void)
{
    return (g_programAppletType > AppletType_Application && g_programAppletType < AppletType_SystemApplication);
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
    
    utilsOverclockSystem(configGetBoolean("overclock"));
}

static void utilsPrintConsoleError(void)
{
    PadState pad = {0};
    char msg[0x100] = {0};
    
    /* Don't consider stick movement as button inputs. */
    u64 flag = ~(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | \
                 HidNpadButton_StickRUp | HidNpadButton_StickRDown);
    
    /* Configure input. */
    /* Up to 8 different, full controller inputs. */
    /* Individual Joy-Cons not supported. */
    padConfigureInput(8, HidNpadStyleSet_NpadFullCtrl);
    padInitializeWithMask(&pad, 0x1000000FFUL);
    
    /* Get last log message. */
    logGetLastMessage(msg, sizeof(msg));
    
    consoleInit(NULL);
    
    printf("An error occurred while initializing resources.\n\n");
    if (*msg) printf("%s\n\n", msg);
    printf("For more information, please check the logfile. Press any button to exit.");
    
    consoleUpdate(NULL);
    
    while(appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & flag) break;
    }
    
    consoleExit(NULL);
}

static size_t utilsGetUtf8CodepointCount(const char *str, size_t str_size, size_t cp_limit, size_t *last_cp_pos)
{
    if (!str || !*str || !str_size || (!cp_limit && last_cp_pos) || (cp_limit && !last_cp_pos)) return 0;
    
    u32 code = 0;
    ssize_t units = 0;
    size_t cur_pos = 0, cp_count = 0;
    const u8 *str_u8 = (const u8*)str;
    
    while(cur_pos < str_size)
    {
        units = decode_utf8(&code, str_u8 + cur_pos);
        size_t new_pos = (cur_pos + (size_t)units);
        if (units < 0 || !code || new_pos > str_size) break;
        
        cp_count++;
        cur_pos = new_pos;
        if (cp_limit && last_cp_pos && cp_count < cp_limit) *last_cp_pos = cur_pos;
    }
    
    return cp_count;
}
