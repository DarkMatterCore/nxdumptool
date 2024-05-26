/*
 * nxdt_utils.c
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

#include <sys/statvfs.h>

#include <core/nxdt_utils.h>
#include <core/keys.h>
#include <core/gamecard.h>
#include <core/services.h>
#include <core/nca.h>
#include <core/usb.h>
#include <core/title.h>
#include <core/bfttf.h>
#include <core/nxdt_bfsar.h>
#include <core/devoptab/nxdt_devoptab.h>
#include <core/fatfs/ff.h>

/* Type definitions. */

/* Reference: https://github.com/Atmosphere-NX/Atmosphere/blob/master/exosphere/program/source/smc/secmon_smc_info.hpp. */
typedef struct {
    SdkAddOnVersion target_firmware;
    u8 key_generation;
    u8 ams_ver_micro;
    u8 ams_ver_minor;
    u8 ams_ver_major;
} UtilsExosphereApiVersion;

NXDT_ASSERT(UtilsExosphereApiVersion, 0x8);

typedef struct {
    u32 major;
    u32 minor;
    u32 micro;
} UtilsApplicationVersion;

/* Global variables. */

extern int __system_argc;
extern char **__system_argv;

static bool g_resourcesInit = false;
static Mutex g_resourcesMutex = 0;

static const char *g_appLaunchPath = NULL;

static FsFileSystem *g_sdCardFileSystem = NULL;

static int g_nxLinkSocketFd = -1;

static u8 g_customFirmwareType = UtilsCustomFirmwareType_Unknown;

static u8 g_productModel = SetSysProductModel_Invalid;

static bool g_isTerraUnit = false, g_isDevUnit = false;

static AppletType g_programAppletType = AppletType_None;

static FsStorage g_emmcBisSystemPartitionStorage = {0};
static FATFS *g_emmcBisSystemPartitionFatFsObj = NULL;

static AppletHookCookie g_systemOverclockCookie = {0};

static bool g_longRunningProcess = false;

static const char *g_sizeSuffixes[] = { "B", "KiB", "MiB", "GiB", "TiB" };
static const u32 g_sizeSuffixesCount = MAX_ELEMENTS(g_sizeSuffixes);

static const char g_illegalFileSystemChars[] = "\\/:*?\"<>|";
static const size_t g_illegalFileSystemCharsLength = (MAX_ELEMENTS(g_illegalFileSystemChars) - 1);

static bool g_appUpdated = false;

static const SplConfigItem SplConfigItem_ExosphereApiVersion = (SplConfigItem)65000;

static UtilsExosphereApiVersion g_exosphereApiVersion = {0};

/* Function prototypes. */

static void _utilsGetLaunchPath(void);

static bool utilsGetExosphereApiVersion(void);

static void _utilsGetCustomFirmwareType(void);

static bool _utilsGetProductModel(void);

static bool utilsGetDevelopmentUnitFlag(void);

static bool utilsGetTerraUnitFlag(void);

static bool utilsMountEmmcBisSystemPartitionStorage(void);
static void utilsUnmountEmmcBisSystemPartitionStorage(void);

static void utilsOverclockSystem(bool overclock);
static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param);

static void utilsChangeHomeButtonBlockStatus(bool block);

static size_t utilsGetUtf8StringLimit(const char *str, size_t str_size, size_t byte_limit);

static char utilsConvertHexDigitToBinary(char c);

bool utilsInitializeResources(void)
{
    Result rc = 0;
    bool ret = false;

    SCOPED_LOCK(&g_resourcesMutex)
    {
        ret = g_resourcesInit;
        if (ret) break;

        /* Lock applet exit. */
        appletLockExit();

        /* Retrieve pointer to the application launch path. */
        _utilsGetLaunchPath();

        /* Retrieve pointer to the SD card FsFileSystem element. */
        if (!(g_sdCardFileSystem = fsdevGetDeviceFileSystem(DEVOPTAB_SDMC_DEVICE)))
        {
            LOG_MSG_ERROR("Failed to retrieve FsFileSystem object for the SD card!");
            break;
        }

        /*FsFileSystemAttribute fs_attr = {0};
        if (R_SUCCEEDED(fsFsGetFileSystemAttribute(g_sdCardFileSystem, &fs_attr)))
        {
            LOG_DATA_INFO(&fs_attr, sizeof(FsFileSystemAttribute), "SD card FS attributes:");
        }*/

        /* Initialize needed services. */
        if (!servicesInitialize()) break;

        /* Check if a valid nxlink host IP address was set by libnx. */
        /* If so, initialize nxlink connection without redirecting stdout and/or stderr. */
        if (__nxlink_host.s_addr != 0 && __nxlink_host.s_addr != INADDR_NONE) g_nxLinkSocketFd = nxlinkConnectToHost(false, false);

#if LOG_LEVEL <= LOG_LEVEL_INFO
        /* Log info messages. */
        u32 hos_version = hosversionGet();
        LOG_MSG_INFO(APP_TITLE " v" APP_VERSION " starting (" GIT_REV "). Built on " BUILD_TIMESTAMP ".");
        if (g_nxLinkSocketFd >= 0) LOG_MSG_INFO("nxlink enabled! Host IP address: %s.", inet_ntoa(__nxlink_host));
        LOG_MSG_INFO("Horizon OS version: %u.%u.%u.", HOSVER_MAJOR(hos_version), HOSVER_MINOR(hos_version), HOSVER_MICRO(hos_version));
#endif

        /* Retrieve Exosphère API version. */
        if (!utilsGetExosphereApiVersion())
        {
            LOG_MSG_ERROR("Failed to retrieve Exosphère API version!");
            break;
        }

        /* Retrieve custom firmware type. */
        _utilsGetCustomFirmwareType();
        if (g_customFirmwareType != UtilsCustomFirmwareType_Unknown) LOG_MSG_INFO("Detected %s CFW.", (g_customFirmwareType == UtilsCustomFirmwareType_Atmosphere ? "Atmosphère" : \
                                                                                  (g_customFirmwareType == UtilsCustomFirmwareType_SXOS ? "SX OS" : "ReiNX")));

        LOG_MSG_INFO("Exosphère API version info:\r\n" \
                     "- Release version: %u.%u.%u.\r\n" \
                     "- PKG1 key generation: %u (0x%02X).\r\n" \
                     "- Target firmware: %u.%u.%u.", \
                     g_exosphereApiVersion.ams_ver_major, g_exosphereApiVersion.ams_ver_minor, g_exosphereApiVersion.ams_ver_micro, \
                     g_exosphereApiVersion.key_generation, !g_exosphereApiVersion.key_generation ? g_exosphereApiVersion.key_generation : (g_exosphereApiVersion.key_generation + 1), \
                     g_exosphereApiVersion.target_firmware.major, g_exosphereApiVersion.target_firmware.minor, g_exosphereApiVersion.target_firmware.micro);

        /* Get product model. */
        if (!_utilsGetProductModel()) break;

        /* Get development unit flag. */
        if (!utilsGetDevelopmentUnitFlag()) break;

        /* Get Terra unit flag. */
        if (!utilsGetTerraUnitFlag()) break;

        /* Get applet type. */
        g_programAppletType = appletGetAppletType();

        LOG_MSG_INFO("Running under %s %s unit %s Terra flag in %s mode.", g_isDevUnit ? "development" : "retail", utilsIsMarikoUnit() ? "Mariko" : "Erista", \
                                                                           g_isTerraUnit ? "with" : "without", utilsIsAppletMode() ? "applet" : "title override");

        if (g_appLaunchPath)
        {
            LOG_MSG_INFO("Launch path: \"%s\".", g_appLaunchPath);

            /* Move NRO if the launch path isn't the right one, then return. */
            /* TODO: uncomment this block whenever we are ready for a release. */
            /*if (strcmp(g_appLaunchPath, NRO_PATH) != 0)
            {
                utilsCreateDirectoryTree(NRO_PATH, false);
                remove(NRO_PATH);
                rename(g_appLaunchPath, NRO_PATH);

                LOG_MSG_INFO("Moved NRO to \"%s\". Please reload the application.", NRO_PATH);
                break;
            }*/
        }

        /* Initialize HTTP interface. */
        /* cURL must be initialized before starting any other threads. */
        if (!httpInitialize()) break;

        /* Initialize USB interface. */
        if (!usbInitialize()) break;

        /* Initialize USB Mass Storage interface. */
        if (!umsInitialize()) break;

        /* Load keyset. */
        if (!keysLoadKeyset()) break;

        /* Allocate NCA crypto buffer. */
        if (!ncaAllocateCryptoBuffer())
        {
            LOG_MSG_ERROR("Unable to allocate memory for NCA crypto buffer!");
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
            LOG_MSG_ERROR("Failed to mount " APP_TITLE "'s RomFS container!");
            break;
        }

        /* Initialize configuration interface. */
        if (!configInitialize()) break;

        /* Setup an applet hook to change the hardware clocks after a system mode change (docked <-> undocked). */
        appletHook(&g_systemOverclockCookie, utilsOverclockSystemAppletHook, NULL);

        /* Enable video recording if we're running under title override mode. */
        if (!utilsIsAppletMode())
        {
            bool flag = false;
            rc = appletIsGamePlayRecordingSupported(&flag);
            if (R_SUCCEEDED(rc) && flag)
            {
                rc = appletInitializeGamePlayRecording();
                if (R_FAILED(rc)) LOG_MSG_ERROR("appletInitializeGamePlayRecording failed! (0x%X).", rc);
            } else {
                LOG_MSG_ERROR("appletIsGamePlayRecordingSupported returned [0x%X, %u].", rc, flag);
            }
        }

        /* Update flags. */
        ret = g_resourcesInit = true;
    }

    if (!ret)
    {
        char *msg = NULL;
        size_t msg_size = 0;

        /* Generate error message. */
        utilsAppendFormattedStringToBuffer(&msg, &msg_size, "An error occurred while initializing resources.");

#if LOG_LEVEL <= LOG_LEVEL_ERROR
        /* Get last log message. */
        char *log_msg = logGetLastMessage();
        if (log_msg)
        {
            utilsAppendFormattedStringToBuffer(&msg, &msg_size, "\n\n%s", log_msg);
            free(log_msg);
        }
#endif

        /* Print error message. */
        utilsPrintConsoleError(msg);

        /* Free error message. */
        if (msg) free(msg);
    }

    return ret;
}

void utilsCloseResources(void)
{
    SCOPED_LOCK(&g_resourcesMutex)
    {
        LOG_MSG_INFO("Shutting down...");

        /* Unmount all custom devoptab devices. */
        devoptabUnmountAllDevices();

        /* Unset long running process state. */
        utilsSetLongRunningProcessState(false);

        /* Unset our overclock applet hook. */
        appletUnhook(&g_systemOverclockCookie);

        /* Close configuration interface. */
        configExit();

        /* Unmount application RomFS. */
        romfsExit();

        /* Unmount eMMC BIS System partition. */
        utilsUnmountEmmcBisSystemPartitionStorage();

        /* Deinitialize BFSAR interface. */
        //bfsarExit();

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

        /* Close HTTP interface. */
        httpExit();

        /* Close nxlink socket. */
        if (g_nxLinkSocketFd >= 0)
        {
            close(g_nxLinkSocketFd);
            g_nxLinkSocketFd = -1;
        }

        /* Close initialized services. */
        servicesClose();

        /* Replace application NRO (if needed). */
        /* TODO: uncomment this block whenever we're ready for a release. */
        /*if (g_appUpdated)
        {
            remove(NRO_PATH);
            rename(NRO_TMP_PATH, NRO_PATH);
        }*/

#if LOG_LEVEL <= LOG_LEVEL_ERROR
        /* Close logfile. */
        logCloseLogFile();
#endif

        /* Unlock applet exit. */
        appletUnlockExit();

        g_resourcesInit = false;
    }
}

const char *utilsGetLaunchPath(void)
{
    return g_appLaunchPath;
}

int utilsGetNxLinkFileDescriptor(void)
{
    return g_nxLinkSocketFd;
}

FsFileSystem *utilsGetSdCardFileSystemObject(void)
{
    return g_sdCardFileSystem;
}

bool utilsCommitSdCardFileSystemChanges(void)
{
    return (g_sdCardFileSystem ? R_SUCCEEDED(fsFsCommit(g_sdCardFileSystem)) : false);
}

u32 utilsGetAtmosphereVersion(void)
{
    return MAKEHOSVERSION(g_exosphereApiVersion.ams_ver_major, g_exosphereApiVersion.ams_ver_minor, g_exosphereApiVersion.ams_ver_micro);
}

u8 utilsGetAtmosphereKeyGeneration(void)
{
    return g_exosphereApiVersion.key_generation;
}

void utilsGetAtmosphereTargetFirmware(SdkAddOnVersion *out)
{
    memcpy(out, &(g_exosphereApiVersion.target_firmware), sizeof(SdkAddOnVersion));
}

u8 utilsGetCustomFirmwareType(void)
{
    return g_customFirmwareType;
}

bool utilsIsMarikoUnit(void)
{
    return (g_productModel > SetSysProductModel_Copper);
}

bool utilsIsDevelopmentUnit(void)
{
    return g_isDevUnit;
}

bool utilsIsTerraUnit(void)
{
    return g_isTerraUnit;
}

bool utilsIsAppletMode(void)
{
    return (g_programAppletType > AppletType_Application && g_programAppletType < AppletType_SystemApplication);
}

FsStorage *utilsGetEmmcBisSystemPartitionStorage(void)
{
    return &g_emmcBisSystemPartitionStorage;
}

void utilsSetLongRunningProcessState(bool state)
{
    SCOPED_LOCK(&g_resourcesMutex)
    {
        /* Don't proceed if resources haven't been initialized, or if the requested state matches the current one. */
        if (!g_resourcesInit || state == g_longRunningProcess) break;

        /* Change HOME button block status. */
        utilsChangeHomeButtonBlockStatus(state);

        /* Enable/disable screen dimming and auto sleep. */
        appletSetMediaPlaybackState(state);

        /* Enable/disable system overclock. */
        utilsOverclockSystem(configGetBoolean("overclock") & state);

        /* Update flag. */
        g_longRunningProcess = state;
    }
}

bool utilsCreateThread(Thread *out_thread, ThreadFunc func, void *arg, int cpu_id)
{
    /* Core 3 is reserved for HOS, so we can only use cores 0, 1 and 2. */
    /* -2 can be provided to use the default process core. */
    if (!out_thread || !func || (cpu_id < 0 && cpu_id != -2) || cpu_id > 2)
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
        LOG_MSG_ERROR("svcGetInfo failed! (0x%X).", rc);
        goto end;
    }

    /* Create thread. */
    /* Enable preemptive multithreading by using priority 0x3B. */
    rc = threadCreate(out_thread, func, arg, NULL, stack_size, 0x3B, cpu_id);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("threadCreate failed! (0x%X).", rc);
        goto end;
    }

    /* Set thread core mask. */
    rc = svcSetThreadCoreMask(out_thread->handle, cpu_id == -2 ? -1 : cpu_id, core_mask);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("svcSetThreadCoreMask failed! (0x%X).", rc);
        goto end;
    }

    /* Start thread. */
    rc = threadStart(out_thread);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("threadStart failed! (0x%X).", rc);
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
        LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    Result rc = threadWaitForExit(thread);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("threadWaitForExit failed! (0x%X).", rc);
        return;
    }

    threadClose(thread);

    memset(thread, 0, sizeof(Thread));
}

__attribute__((format(printf, 3, 4))) bool utilsAppendFormattedStringToBuffer(char **dst, size_t *dst_size, const char *fmt, ...)
{
    bool use_log = false;
    SCOPED_LOCK(&g_resourcesMutex) use_log = g_resourcesInit;

    if (!dst || !dst_size || !fmt || !*fmt)
    {
        if (use_log) LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    va_list args;

    int formatted_str_len = 0;
    size_t formatted_str_len_cast = 0;

    char *dst_ptr = *dst, *tmp_str = NULL;
    size_t dst_cur_size = *dst_size, dst_str_len = (dst_ptr ? strlen(dst_ptr) : 0);

    bool success = false;

    /* Sanity check. */
    if (dst_cur_size && dst_str_len >= dst_cur_size)
    {
        if (use_log) LOG_MSG_ERROR("String length is equal to or greater than the provided buffer size! (0x%lX >= 0x%lX).", dst_str_len, dst_cur_size);
        return false;
    }

    va_start(args, fmt);

    /* Get formatted string length. */
    formatted_str_len = vsnprintf(NULL, 0, fmt, args);
    if (formatted_str_len <= 0)
    {
        if (use_log) LOG_MSG_ERROR("Failed to retrieve formatted string length!");
        goto end;
    }

    formatted_str_len_cast = (size_t)(formatted_str_len + 1);

    if (!dst_ptr || !dst_cur_size || formatted_str_len_cast > (dst_cur_size - dst_str_len))
    {
        /* Update buffer size. */
        dst_cur_size = (dst_str_len + formatted_str_len_cast);

        /* Reallocate buffer. */
        tmp_str = realloc(dst_ptr, dst_cur_size);
        if (!tmp_str)
        {
            if (use_log) LOG_MSG_ERROR("Failed to resize buffer to 0x%lX byte(s).", dst_cur_size);
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

    /* Update output flag. */
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
    bool repl = false;

    while(cur_pos < str_size)
    {
        units = decode_utf8(&code, ptr1);
        if (units < 0) break;

        if (memchr(g_illegalFileSystemChars, (int)code, g_illegalFileSystemCharsLength) || code < 0x20 || (!ascii_only && code == 0x7F) || (ascii_only && code >= 0x7F))
        {
            if (!repl)
            {
                *ptr2++ = '_';
                repl = true;
            }
        } else {
            if (ptr2 != ptr1) memmove(ptr2, ptr1, (size_t)units);
            ptr2 += units;
            repl = false;
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

void utilsGenerateHexString(char *dst, size_t dst_size, const void *src, size_t src_size, bool uppercase)
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

bool utilsParseHexString(void *dst, size_t dst_size, const char *src, size_t src_size)
{
    u8 *dst_u8 = (u8*)dst;
    bool success = true;

    if (!dst || !dst_size || !src || !*src || (!src_size && !(src_size = strlen(src))) || (src_size % 2) != 0 || dst_size < (src_size / 2))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    memset(dst, 0, dst_size);

    for(size_t i = 0; i < src_size; i++)
    {
        char val = utilsConvertHexDigitToBinary(src[i]);
        if (val == 'z')
        {
            LOG_MSG_ERROR("Invalid hex character in string \"%s\" at position %lu!", src, i);
            success = false;
            break;
        }

        if ((i & 1) == 0) val <<= 4;
        dst_u8[i >> 1] |= val;
    }

    return success;
}

void utilsGenerateFormattedSizeString(double size, char *dst, size_t dst_size)
{
    if (!dst || dst_size < 2) return;

    size = fabs(size);

    for(u32 i = 0; i < g_sizeSuffixesCount; i++)
    {
        if (size >= pow(1024.0, i + 1) && (i + 1) < g_sizeSuffixesCount) continue;

        size /= pow(1024.0, i);

        /* Don't display decimal places if we're dealing with plain bytes. */
        snprintf(dst, dst_size, "%.*f %s", i == 0 ? 0 : 2, size, g_sizeSuffixes[i]);

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
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    name_end += 2;
    snprintf(stat_path, MAX_ELEMENTS(stat_path), "%.*s", (int)(name_end - path), path);

    if ((ret = statvfs(stat_path, &info)) != 0)
    {
        LOG_MSG_ERROR("statvfs failed for \"%s\"! (%d) (errno: %d).", stat_path, ret, errno);
        return false;
    }

    if (out_total) *out_total = ((u64)info.f_blocks * (u64)info.f_frsize);
    if (out_free) *out_free = ((u64)info.f_bfree * (u64)info.f_frsize);

    return true;
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
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Safety measure: remove any existant file/directory at the destination path. */
    utilsRemoveConcatenationFile(path);

    /* Create ConcatenationFile. */
    /* If the call succeeds, the caller function will be able to operate on this file using stdio calls. */
    Result rc = fsdevCreateFile(path, 0, FsCreateOption_BigFile);
    if (R_FAILED(rc)) LOG_MSG_ERROR("fsdevCreateFile failed for \"%s\"! (0x%X).", path, rc);

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

bool utilsGetDirectorySize(const char *path, u64 *out_size)
{
    u64 total_size = 0;
    char *name_end = NULL, *entry_path = NULL;
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct stat st = {0};
    bool success = false;

    /* Sanity checks. */
    if (!path || !*path || !(name_end = strchr(path, ':')) || *(name_end + 1) != '/' || !*(name_end + 2) || !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    if (!(dir = opendir(path)))
    {
        LOG_MSG_ERROR("Failed to open directory \"%s\"! (%d).", path, errno);
        goto end;
    }

    if (!(entry_path = calloc(1, FS_MAX_PATH)))
    {
        LOG_MSG_ERROR("Failed to allocate memory for path buffer!");
        goto end;
    }

    /* Read directory entries. */
    while((entry = readdir(dir)))
    {
        /* Skip current directory and parent directory entries. */
        if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)) continue;

        /* Generate path to the current entry. */
        snprintf(entry_path, FS_MAX_PATH, "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR)
        {
            /* Get directory size. */
            u64 dir_size = 0;
            if (!utilsGetDirectorySize(entry_path, &dir_size)) goto end;

            /* Update size. */
            total_size += dir_size;
        } else {
            /* Get file properties. */
            if (stat(entry_path, &st) != 0)
            {
                LOG_MSG_ERROR("Failed to stat file \"%s\"! (%d).", entry_path, errno);
                goto end;
            }

            /* Update size. */
            total_size += st.st_size;
        }
    }

    /* Update output pointer. */
    *out_size = total_size;

    /* Update return value. */
    success = true;

end:
    if (entry_path) free(entry_path);

    if (dir) closedir(dir);

    return success;
}

bool utilsDeleteDirectoryRecursively(const char *path)
{
    char *name_end = NULL, *entry_path = NULL;
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    bool success = false;

    /* Sanity checks. */
    if (!path || !*path || !(name_end = strchr(path, ':')) || *(name_end + 1) != '/' || !*(name_end + 2))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    if (!(dir = opendir(path)))
    {
        LOG_MSG_ERROR("Failed to open directory \"%s\"! (%d).", path, errno);
        goto end;
    }

    if (!(entry_path = calloc(1, FS_MAX_PATH)))
    {
        LOG_MSG_ERROR("Failed to allocate memory for path buffer!");
        goto end;
    }

    /* Read directory entries. */
    while((entry = readdir(dir)))
    {
        int status = 0;

        /* Skip current directory and parent directory entries. */
        if (!strcmp(".", entry->d_name) || !strcmp("..", entry->d_name)) continue;

        /* Generate path to the current entry. */
        snprintf(entry_path, FS_MAX_PATH, "%s/%s", path, entry->d_name);

        if (entry->d_type == DT_DIR)
        {
            /* Delete directory contents. */
            if (!utilsDeleteDirectoryRecursively(entry_path)) goto end;

            /* Delete directory. */
            status = rmdir(entry_path);
        } else {
            /* Delete file. */
            status = unlink(entry_path);
        }

        if (status != 0)
        {
            LOG_MSG_ERROR("Failed to delete %s \"%s\"! (%d).", entry->d_type == DT_DIR ? "directory" : "file", entry_path, errno);
            goto end;
        }
    }

    /* Close topmost directory so we can delete it. */
    closedir(dir);
    dir = NULL;

    success = (rmdir(path) == 0);
    if (!success) LOG_MSG_ERROR("Failed to delete topmost directory \"%s\"! (%d).", path, errno);

end:
    if (entry_path) free(entry_path);

    if (dir) closedir(dir);

    return success;
}

char *utilsGeneratePath(const char *prefix, const char *filename, const char *extension)
{
    if (!filename || !*filename)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    bool use_prefix = (prefix && *prefix);
    size_t prefix_len = (use_prefix ? strlen(prefix) : 0);
    bool append_path_sep = (use_prefix && prefix[prefix_len - 1] != '/' && *filename != '/');

    bool use_extension = (extension && *extension);
    size_t extension_len = (use_extension ? strlen(extension) : 0);

    size_t path_len = (prefix_len + strlen(filename) + extension_len);
    if (append_path_sep) path_len++;

    const size_t max_filename_len = ((use_prefix && !strncmp(prefix, DEVOPTAB_SDMC_DEVICE, strlen(DEVOPTAB_SDMC_DEVICE))) ? SDMC_MAX_FILENAME_LENGTH : FS_MAX_FILENAME_LENGTH);

    char *path = NULL, *ptr1 = NULL, *ptr2 = NULL;
    bool filename_only = false, success = false;

    /* Allocate memory for the output path. */
    if (!(path = calloc(path_len + 1, sizeof(char))))
    {
        LOG_MSG_ERROR("Failed to allocate 0x%lX bytes for output path!", path_len);
        goto end;
    }

    /* Generate output path. */
    if (use_prefix) strcat(path, prefix);
    if (append_path_sep) strcat(path, "/");
    strcat(path, filename);
    if (use_extension) strcat(path, extension);

    /* Retrieve pointer to the first path separator. */
    ptr1 = strchr(path, '/');
    if (!ptr1)
    {
        filename_only = true;
        ptr1 = path;
    }

    /* Make sure each path element doesn't exceed our max filename length. */
    while(ptr1)
    {
        if (!filename_only)
        {
            /* End loop if we find a NULL terminator. */
            if (!*ptr1++) break;

            /* Get pointer to next path separator. */
            ptr2 = strchr(ptr1, '/');
        }

        /* Get current path element size. */
        size_t element_size = (ptr2 ? (size_t)(ptr2 - ptr1) : (path_len - (size_t)(ptr1 - path)));

        /* Get UTF-8 string limit. */
        /* Use our max filename length as the byte count limit. */
        size_t last_cp_pos = utilsGetUtf8StringLimit(ptr1, element_size, max_filename_len);
        if (last_cp_pos < element_size)
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
                if (extension_len >= last_cp_pos)
                {
                    LOG_MSG_ERROR("File extension length is >= truncated filename length! (0x%lX >= 0x%lX).", extension_len, last_cp_pos);
                    goto end;
                }

                memmove(ptr1 + last_cp_pos - extension_len, ptr1 + element_size - extension_len, extension_len);
            }

            path_len -= (element_size - last_cp_pos);
            path[path_len] = '\0';
        }

        ptr1 = ptr2;
    }

    /* Check if the full length for the generated path is >= FS_MAX_PATH. */
    if (path_len >= FS_MAX_PATH)
    {
        LOG_MSG_ERROR("Generated path length is >= FS_MAX_PATH! (0x%lX).", path_len);
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

void utilsPrintConsoleError(const char *msg)
{
    PadState pad = {0};

    /* Don't consider stick movement as button inputs. */
    u64 flag = ~(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | \
                 HidNpadButton_StickRUp | HidNpadButton_StickRDown);

    /* Configure input. */
    /* Up to 8 different, full controller inputs. */
    /* Individual Joy-Cons not supported. */
    padConfigureInput(8, HidNpadStyleSet_NpadFullCtrl);
    padInitializeWithMask(&pad, 0x1000000FFUL);

    /* Initialize console output. */
    consoleInit(NULL);

    /* Print message. */
    if (msg && *msg)
    {
        printf("%s", msg);
    } else {
        printf("An error occurred.");
    }

#if LOG_LEVEL < LOG_LEVEL_NONE
    printf("\n\nFor more information, please check the logfile. Press any button to exit.");
#else
    printf("\n\nPress any button to exit.");
#endif

    consoleUpdate(NULL);

    /* Wait until the user presses a button. */
    while(appletMainLoop())
    {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & flag) break;
        utilsAppletLoopDelay();
    }

    /* Deinitialize console output. */
    consoleExit(NULL);
}

bool utilsGetApplicationUpdatedState(void)
{
    bool ret = false;
    SCOPED_LOCK(&g_resourcesMutex) ret = g_appUpdated;
    return ret;
}

void utilsSetApplicationUpdatedState(void)
{
    SCOPED_LOCK(&g_resourcesMutex) g_appUpdated = true;
}

bool utilsParseGitHubReleaseJsonData(const char *json_buf, size_t json_buf_size, UtilsGitHubReleaseJsonData *out)
{
    if (!json_buf || !json_buf_size || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;
    const char *published_at = NULL;
    struct json_object *assets = NULL;

    /* Free output buffer beforehand. */
    utilsFreeGitHubReleaseJsonData(out);

    /* Parse JSON object. */
    out->obj = jsonParseFromString(json_buf, json_buf_size);
    if (!out->obj)
    {
        LOG_MSG_ERROR("Failed to parse JSON object!");
        return false;
    }

    /* Get required JSON elements. */
    out->version = jsonGetString(out->obj, "tag_name");
    out->commit_hash = jsonGetString(out->obj, "target_commitish");
    published_at = jsonGetString(out->obj, "published_at");
    out->changelog = jsonGetString(out->obj, "body");
    assets = jsonGetArray(out->obj, "assets");

    if (!out->version || !out->commit_hash || !published_at || !out->changelog || !assets)
    {
        LOG_MSG_ERROR("Failed to retrieve required elements from the provided JSON!");
        goto end;
    }

    /* Parse release date. */
    if (!strptime(published_at, "%Y-%m-%dT%H:%M:%SZ", &(out->date)))
    {
        LOG_MSG_ERROR("Failed to parse release date \"%s\"!", published_at);
        goto end;
    }

    /* Loop through the assets array until we find the NRO. */
    size_t assets_len = json_object_array_length(assets);
    for(size_t i = 0; i < assets_len; i++)
    {
        struct json_object *cur_asset = NULL;
        const char *asset_name = NULL;

        /* Get current asset object. */
        cur_asset = json_object_array_get_idx(assets, i);
        if (!cur_asset) continue;

        /* Get current asset name. */
        asset_name = jsonGetString(cur_asset, "name");
        if (!asset_name || strcmp(asset_name, NRO_NAME) != 0) continue;

        /* Jackpot. Get the download URL. */
        out->download_url = jsonGetString(cur_asset, "browser_download_url");
        break;
    }

    if (!out->download_url)
    {
        LOG_MSG_ERROR("Failed to retrieve required elements from the provided JSON!");
        goto end;
    }

    /* Update return value. */
    ret = true;

end:
    if (!ret) utilsFreeGitHubReleaseJsonData(out);

    return ret;
}

bool utilsIsApplicationUpdatable(const char *version, const char *commit_hash)
{
    if (!version || !*version || *version != 'v' || !commit_hash || !*commit_hash)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;
    UtilsApplicationVersion cur_version = { VERSION_MAJOR, VERSION_MINOR, VERSION_MICRO }, new_version = {0};

    /* Parse version string. */
    sscanf(version, "v%u.%u.%u", &(new_version.major), &(new_version.minor), &(new_version.micro));

    /* Compare versions. */
    if (cur_version.major == new_version.major)
    {
        if (cur_version.minor == new_version.minor)
        {
            if (cur_version.micro == new_version.micro)
            {
                /* Versions are equal. Let's compare the commit hashes and return true if they're different. */
                ret = (strncasecmp(commit_hash, GIT_COMMIT, 7) != 0);
            } else
            if (cur_version.micro < new_version.micro)
            {
                ret = true;
            }
        } else
        if (cur_version.minor < new_version.minor)
        {
            ret = true;
        }
    } else
    if (cur_version.major < new_version.major)
    {
        ret = true;
    }

    return ret;
}

static void _utilsGetLaunchPath(void)
{
    if (__system_argc <= 0 || !__system_argv) return;

    for(int i = 0; i < __system_argc; i++)
    {
        if (__system_argv[i] && !strncmp(__system_argv[i], DEVOPTAB_SDMC_DEVICE "/", strlen(DEVOPTAB_SDMC_DEVICE)))
        {
            g_appLaunchPath = __system_argv[i];
            break;
        }
    }
}

/* SMC config item available in Atmosphère and Atmosphère-based CFWs. */
static bool utilsGetExosphereApiVersion(void)
{
    Result rc = splGetConfig(SplConfigItem_ExosphereApiVersion, (u64*)&g_exosphereApiVersion);
    bool ret = R_SUCCEEDED(rc);
    if (!ret) LOG_MSG_ERROR("splGetConfig failed! (0x%X).", rc);
    return ret;
}

static void _utilsGetCustomFirmwareType(void)
{
    bool tx_srv = servicesCheckRunningServiceByName("tx");
    bool rnx_srv = servicesCheckRunningServiceByName("rnx");

    g_customFirmwareType = (rnx_srv ? UtilsCustomFirmwareType_ReiNX : (tx_srv ? UtilsCustomFirmwareType_SXOS : UtilsCustomFirmwareType_Atmosphere));
}

static bool _utilsGetProductModel(void)
{
    Result rc = 0;
    bool ret = false;
    SetSysProductModel model = SetSysProductModel_Invalid;

    rc = setsysGetProductModel(&model);
    if (R_SUCCEEDED(rc) && model != SetSysProductModel_Invalid)
    {
        g_productModel = model;
        ret = true;
    } else {
        LOG_MSG_ERROR("setsysGetProductModel failed! (0x%X) (%d).", rc, model);
    }

    return ret;
}

static bool utilsGetDevelopmentUnitFlag(void)
{
    Result rc = 0;
    bool tmp = false;

    rc = splIsDevelopment(&tmp);
    if (R_SUCCEEDED(rc))
    {
        g_isDevUnit = tmp;
    } else {
        LOG_MSG_ERROR("splIsDevelopment failed! (0x%X).", rc);
    }

    return R_SUCCEEDED(rc);
}

static bool utilsGetTerraUnitFlag(void)
{
    /* Return right away if we're running under a HOS version that's too low. */
    if (hosversionBefore(8, 0, 0)) return true;

    Result rc = 0;
    bool tmp = false;

    rc = setsysGetT(&tmp);
    if (R_SUCCEEDED(rc))
    {
        g_isTerraUnit = tmp;
    } else {
        LOG_MSG_ERROR("setsysGetT failed! (0x%X).", rc);
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
        LOG_MSG_ERROR("Failed to open eMMC BIS System partition storage! (0x%X).", rc);
        return false;
    }

    g_emmcBisSystemPartitionFatFsObj = calloc(1, sizeof(FATFS));
    if (!g_emmcBisSystemPartitionFatFsObj)
    {
        LOG_MSG_ERROR("Unable to allocate memory for FatFs element!");
        return false;
    }

    fr = f_mount(g_emmcBisSystemPartitionFatFsObj, BIS_SYSTEM_PARTITION_MOUNT_NAME, 1);
    if (fr != FR_OK)
    {
        LOG_MSG_ERROR("Failed to mount eMMC BIS System partition! (%u).", fr);
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

static void utilsOverclockSystem(bool overclock)
{
    u32 cpu_rate = ((overclock ? CPU_CLKRT_OVERCLOCKED : CPU_CLKRT_NORMAL) * 1000000);
    u32 mem_rate = ((overclock ? MEM_CLKRT_OVERCLOCKED : MEM_CLKRT_NORMAL) * 1000000);
    servicesChangeHardwareClockRates(cpu_rate, mem_rate);
}

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param)
{
    NX_IGNORE_ARG(param);

    /* Don't proceed if we're not dealing with a desired hook type. */
    if (hook != AppletHookType_OnOperationMode && hook != AppletHookType_OnPerformanceMode) return;

    /* Overclock the system based on the overclock setting and the current long running state value. */
    SCOPED_LOCK(&g_resourcesMutex) utilsOverclockSystem(configGetBoolean("overclock") & g_longRunningProcess);
}

static void utilsChangeHomeButtonBlockStatus(bool block)
{
    /* Only change HOME button blocking status if we're running as a regular application or a system application. */
    if (utilsIsAppletMode()) return;

    if (block)
    {
        appletBeginBlockingHomeButtonShortAndLongPressed(0);
    } else {
        appletEndBlockingHomeButtonShortAndLongPressed();
    }
}

static size_t utilsGetUtf8StringLimit(const char *str, size_t str_size, size_t byte_limit)
{
    if (!str || !*str || !str_size || !byte_limit) return 0;

    if (byte_limit > str_size) return str_size;

    u32 code = 0;
    ssize_t units = 0;
    size_t cur_pos = 0, last_cp_pos = 0;
    const u8 *str_u8 = (const u8*)str;

    while(cur_pos < str_size && cur_pos < byte_limit)
    {
        units = decode_utf8(&code, str_u8 + cur_pos);
        size_t new_pos = (cur_pos + (size_t)units);
        if (units < 0 || !code || new_pos > str_size) break;

        cur_pos = new_pos;
        if (cur_pos < byte_limit) last_cp_pos = cur_pos;
    }

    return last_cp_pos;
}

static char utilsConvertHexDigitToBinary(char c)
{
    if ('a' <= c && c <= 'f') return (c - 'a' + 0xA);
    if ('A' <= c && c <= 'F') return (c - 'A' + 0xA);
    if ('0' <= c && c <= '9') return (c - '0');
    return 'z';
}
