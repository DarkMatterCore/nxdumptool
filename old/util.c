#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <dirent.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/socket.h>
#include <switch/services/ncm.h>
#include <switch/services/ns.h>
#include <libxml2/libxml/globals.h>
#include <libxml2/libxml/xpath.h>
#include <curl/curl.h>
#include <json-c/json.h>
#include <pthread.h>

#include "dumper.h"
#include "fs_ext.h"
#include "keys.h"
#include "ui.h"
#include "util.h"
#include "fatfs/ff.h"

/* Extern variables */

extern bool highlight;

extern int breaks;
extern int font_height;

extern int cursor;
extern int scroll;

extern curMenuType menuType;

extern u8 *fileNormalIconBuf;
extern u8 *fileHighlightIconBuf;

extern nca_keyset_t nca_keyset;

/* Statically allocated variables */

static bool initNcm = false, initNs = false, initCsrng = false, initSpl = false, initPmdmnt = false, initPl = false, initNet = false;
static bool openFsDevOp = false, openGcEvtNotifier = false, loadGcKernEvt = false, gcThreadInit = false, homeBtnBlocked = false;

dumpOptions dumpCfg;

bool keysFileAvailable = false;

static pthread_t gameCardDetectionThread;
static UEvent exitEvent;

static AppletType programAppletType;

char cfwDirStr[32] = {'\0'};

gamecard_ctx_t gameCardInfo;

u32 titleAppCount = 0, titlePatchCount = 0, titleAddOnCount = 0;
u32 sdCardTitleAppCount = 0, sdCardTitlePatchCount = 0, sdCardTitleAddOnCount = 0;
u32 emmcTitleAppCount = 0, emmcTitlePatchCount = 0, emmcTitleAddOnCount = 0;
u32 gameCardSdCardEmmcPatchCount = 0, gameCardSdCardEmmcAddOnCount = 0;

base_app_ctx_t *baseAppEntries = NULL;
patch_addon_ctx_t *patchEntries = NULL, *addOnEntries = NULL;

static volatile bool gameCardInfoLoaded = false;
static bool sdCardAndEmmcTitleInfoLoaded = false;

exefs_ctx_t exeFsContext;
romfs_ctx_t romFsContext;
bktr_ctx_t bktrContext;

char curRomFsPath[NAME_BUF_LEN] = {'\0'};
u32 curRomFsDirOffset = 0;
romfs_browser_entry *romFsBrowserEntries = NULL;

static char *result_buf = NULL;
static size_t result_sz = 0;
static size_t result_written = 0;

char **filenameBuffer = NULL;
int filenameCount = 0, filenameIndex = 0;

u8 *dumpBuf = NULL;
u8 *gcReadBuf = NULL;
u8 *ncaCtrBuf = NULL;

orphan_patch_addon_entry *orphanEntries = NULL;
u32 orphanEntriesCnt = 0;

char strbuf[NAME_BUF_LEN] = {'\0'};

static const char *appLaunchPath = NULL;

FsStorage fatFsStorage;
static bool openBis = false, mountBisFatFs = false;

u64 freeSpace = 0;
char freeSpaceStr[32] = {'\0'};

browser_entry_size_info *hfs0ExeFsEntriesSizes = NULL;

void loadConfig()
{
    // Set default configuration values
    memset(&dumpCfg, 0x00, sizeof(dumpOptions));
    
    dumpCfg.xciDumpCfg.isFat32 = true;
    dumpCfg.xciDumpCfg.calcCrc = true;
    
    dumpCfg.nspDumpCfg.isFat32 = true;
    dumpCfg.nspDumpCfg.useNoIntroLookup = true;
    dumpCfg.nspDumpCfg.npdmAcidRsaPatch = true;
    
    dumpCfg.batchDumpCfg.isFat32 = true;
    dumpCfg.batchDumpCfg.npdmAcidRsaPatch = true;
    dumpCfg.batchDumpCfg.skipDumpedTitles = true;
    dumpCfg.batchDumpCfg.haltOnErrors = true;
    dumpCfg.batchDumpCfg.batchModeSrc = BATCH_SOURCE_ALL;
    
    dumpCfg.exeFsDumpCfg.isFat32 = true;
    
    dumpCfg.romFsDumpCfg.isFat32 = true;
    
    FILE *configFile = fopen(CONFIG_PATH, "rb");
    if (!configFile) return;
    
    fseek(configFile, 0, SEEK_END);
    size_t configFileSize = ftell(configFile);
    rewind(configFile);
    
    if (configFileSize != sizeof(dumpOptions))
    {
        fclose(configFile);
        remove(CONFIG_PATH);
        return;
    }
    
    dumpOptions tmpCfg;
    size_t read_res = fread(&tmpCfg, 1, sizeof(dumpOptions), configFile);
    fclose(configFile);
    
    if (read_res != sizeof(dumpOptions))
    {
        remove(CONFIG_PATH);
        return;
    }
    
    memcpy(&dumpCfg, &tmpCfg, sizeof(dumpOptions));
    
    // Check if the configuration is correct
    if (dumpCfg.xciDumpCfg.setXciArchiveBit && !dumpCfg.xciDumpCfg.isFat32) dumpCfg.xciDumpCfg.setXciArchiveBit = false;
    
    if (dumpCfg.nspDumpCfg.tiklessDump && !dumpCfg.nspDumpCfg.removeConsoleData) dumpCfg.nspDumpCfg.tiklessDump = false;
    
    if (dumpCfg.batchDumpCfg.tiklessDump && !dumpCfg.batchDumpCfg.removeConsoleData) dumpCfg.batchDumpCfg.tiklessDump = false;
    
    if (dumpCfg.batchDumpCfg.batchModeSrc >= BATCH_SOURCE_CNT) dumpCfg.batchDumpCfg.batchModeSrc = BATCH_SOURCE_ALL;
}

void saveConfig()
{
    FILE *configFile = fopen(CONFIG_PATH, "wb");
    if (!configFile) return;
    
    size_t write_res = fwrite(&dumpCfg, 1, sizeof(dumpOptions), configFile);
    fclose(configFile);
    
    if (write_res != sizeof(dumpOptions)) remove(CONFIG_PATH);
}

static bool isGameCardInserted()
{
    bool inserted = false;
    fsDeviceOperatorIsGameCardInserted(&(gameCardInfo.fsOperatorInstance), &inserted);
    return inserted;
}

static void changeAtomicBool(volatile bool *ptr, bool value)
{
    if (!ptr) return;
    
    if (value)
    {
        __atomic_test_and_set(ptr, __ATOMIC_SEQ_CST);
    } else {
        __atomic_clear(ptr, __ATOMIC_SEQ_CST);
    }
}

static void *fsGameCardDetectionThreadFunc(void *arg)
{
    (void)arg;
    
    Result result = 0;
    int idx = 0;
    
    Waiter gameCardEventWaiter = waiterForEvent(&(gameCardInfo.fsGameCardKernelEvent));
    Waiter exitEventWaiter = waiterForUEvent(&exitEvent);
    
    changeAtomicBool(&gameCardInfoLoaded, false);
    
    /* Retrieve initial gamecard status */
    bool curGcStatus = isGameCardInserted();
    changeAtomicBool(&(gameCardInfo.isInserted), curGcStatus);
    
    while(true)
    {
        // Wait until an event is triggered
        result = waitMulti(&idx, -1, gameCardEventWaiter, exitEventWaiter);
        if (R_FAILED(result)) continue;
        
        // Exit event triggered
        if (idx == 1) break;
        
        // Retrieve current gamecard status
        // Only proceed if we're dealing with a status change
        curGcStatus = isGameCardInserted();
        changeAtomicBool(&(gameCardInfo.isInserted), curGcStatus);
        if (!curGcStatus && gameCardInfoLoaded) changeAtomicBool(&gameCardInfoLoaded, false);
    }
    
    waitMulti(&idx, 0, gameCardEventWaiter, exitEventWaiter);
    
    return 0;
}

static bool createGameCardDetectionThread()
{
    int ret1 = 0, ret2 = 0;
    pthread_attr_t attr;
    
    ret1 = pthread_attr_init(&attr);
    if (ret1 != 0)
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to initialize thread attributes! (%d)", __func__, ret1);
        return false;
    }
    
    ret1 = pthread_create(&gameCardDetectionThread, &attr, &fsGameCardDetectionThreadFunc, NULL);
    if (ret1 != 0) uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to create thread! (%d)", __func__, ret1);
    
    ret2 = pthread_attr_destroy(&attr);
    if (ret2 != 0) uiDrawString(STRING_X_POS, (ret1 == 0 ? 8 : STRING_Y_POS(1)), FONT_COLOR_ERROR_RGB, "%s: failed to destroy thread attributes! (%d)", __func__, ret2);
    
    if (ret1 != 0 || ret2 != 0) return false;
    
    return true;
}

static void closeGameCardHandle()
{
    svcCloseHandle(gameCardInfo.fsGameCardHandle.value);
    gameCardInfo.fsGameCardHandle.value = 0;
}

void closeGameCardStoragePartition()
{
    if (!gameCardInfo.curIStorageIndex || gameCardInfo.curIStorageIndex >= ISTORAGE_PARTITION_INVALID) return;
    
    fsStorageClose(&(gameCardInfo.fsGameCardStorage));
    memset(&(gameCardInfo.fsGameCardStorage), 0, sizeof(FsStorage));
    
    closeGameCardHandle();
    
    gameCardInfo.curIStorageIndex = ISTORAGE_PARTITION_NONE;
}

Result openGameCardStoragePartition(openIStoragePartition partitionIndex)
{
    // Check if the provided IStorage index is valid
    if (!partitionIndex || partitionIndex >= ISTORAGE_PARTITION_INVALID) return MAKERESULT(Module_Libnx, LibnxError_IoError);
    
    // Safety check: check if we have already opened an IStorage instance
    if (gameCardInfo.curIStorageIndex && gameCardInfo.curIStorageIndex < ISTORAGE_PARTITION_INVALID)
    {
        // If the opened IStorage instance is the same as the requested one, just return right away
        if (gameCardInfo.curIStorageIndex == partitionIndex) return 0;
        
        // Otherwise, close the current IStorage instance
        closeGameCardStoragePartition();
    }
    
    u8 i;
    u32 idx = (u32)(partitionIndex - 1);
    Result res1 = 0, res2 = 0, out = 0;
    
    // 10 tries
    for(i = 0; i < 10; i++)
    {
        // First try to retrieve the IStorage partition handle using the current gamecard handle
        res1 = fsOpenGameCardStorage(&(gameCardInfo.fsGameCardStorage), &(gameCardInfo.fsGameCardHandle), idx);
        if (R_SUCCEEDED(res1)) break;
        
        // If the previous call failed, we may have an invalid handle, so let's close the current one and try to retrieve a new one
        closeGameCardHandle();
        res2 = fsDeviceOperatorGetGameCardHandle(&(gameCardInfo.fsOperatorInstance), &(gameCardInfo.fsGameCardHandle));
    }
    
    if (R_SUCCEEDED(res1) && R_SUCCEEDED(res2))
    {
        // Update current IStorage index
        gameCardInfo.curIStorageIndex = partitionIndex;
    } else {
        // res2 takes precedence over res1
        out = (R_FAILED(res2) ? res2 : res1);
        
        // Close leftover gamecard handle
        closeGameCardHandle();
    }
    
    // If everything worked properly, a functional gamecard handle + IStorage handle are guaranteed up to this point
    return out;
}

Result readGameCardStoragePartition(u64 off, void *buf, size_t len)
{
    if (!gameCardInfo.curIStorageIndex || gameCardInfo.curIStorageIndex >= ISTORAGE_PARTITION_INVALID || !buf || !len) return MAKERESULT(Module_Libnx, LibnxError_IoError);
    
    // Optimization for reads that are already aligned to MEDIA_UNIT_SIZE bytes
    if (!(off % MEDIA_UNIT_SIZE) && !(len % MEDIA_UNIT_SIZE)) return fsStorageRead(&(gameCardInfo.fsGameCardStorage), off, buf, len);
    
    Result result;
    u8 *outBuf = (u8*)buf;
    
    u64 block_start_offset = (off - (off % MEDIA_UNIT_SIZE));
    u64 block_end_offset = (u64)round_up(off + len, MEDIA_UNIT_SIZE);
    u64 block_size = (block_end_offset - block_start_offset);
    
    u64 block_size_used = (block_size > GAMECARD_READ_BUFFER_SIZE ? GAMECARD_READ_BUFFER_SIZE : block_size);
    u64 output_block_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? (GAMECARD_READ_BUFFER_SIZE - (off - block_start_offset)) : len);
    
    result = fsStorageRead(&(gameCardInfo.fsGameCardStorage), block_start_offset, gcReadBuf, block_size_used);
    if (R_FAILED(result)) return result;
    
    memcpy(outBuf, gcReadBuf + (off - block_start_offset), output_block_size);
    
    if (block_size > GAMECARD_READ_BUFFER_SIZE) return readGameCardStoragePartition(off + output_block_size, outBuf + output_block_size, len - output_block_size);
    
    return result;
}

static Result getGameCardStoragePartitionSize(u64 *out)
{
    if (!gameCardInfo.curIStorageIndex || gameCardInfo.curIStorageIndex >= ISTORAGE_PARTITION_INVALID || !out) return MAKERESULT(Module_Libnx, LibnxError_IoError);
    
    return fsStorageGetSize(&(gameCardInfo.fsGameCardStorage), (s64*)out);
}

bool mountSysEmmcPartition()
{
    FATFS fs;
    
    Result result = fsOpenBisStorage(&fatFsStorage, FsBisPartitionId_System);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to open BIS System partition! (0x%08X)", __func__, result);
        return false;
    }
    
    openBis = true;
    
    FRESULT fr = f_mount(&fs, BIS_MOUNT_NAME, 1);
    if (fr != FR_OK)
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to mount BIS System partition! (%u)", __func__, fr);
        return false;
    }
    
    mountBisFatFs = true;
    
    return true;
}

void unmountSysEmmcPartition()
{
    if (mountBisFatFs) f_unmount(BIS_MOUNT_NAME);
    if (openBis) fsStorageClose(&fatFsStorage);
}

static bool isServiceRunning(const char *name)
{
    if (!name || !strlen(name)) return false;
    
    Handle handle;
    SmServiceName serviceName = smEncodeName(name);
    Result result = smRegisterService(&handle, serviceName, false, 1);
    bool running = R_FAILED(result);
    
    svcCloseHandle(handle);
    
    if (!running) smUnregisterService(serviceName);
    
    return running;
}

static void retrieveRunningCfwDir()
{
    bool txService = isServiceRunning("tx");
    bool rnxService = isServiceRunning("rnx");
    
    if (!txService && !rnxService)
    {
        // Atmosphere
        snprintf(cfwDirStr, MAX_CHARACTERS(cfwDirStr), CFW_PATH_ATMOSPHERE);
    } else
    if (txService && !rnxService)
    {
        // SX OS
        snprintf(cfwDirStr, MAX_CHARACTERS(cfwDirStr), CFW_PATH_SXOS);
    } else {
        // ReiNX
        snprintf(cfwDirStr, MAX_CHARACTERS(cfwDirStr), CFW_PATH_REINX);
    }
}

void delay(u8 seconds)
{
    if (!seconds) return;
    
    u64 nanoseconds = (seconds * (u64)1000000000);
    svcSleepThread(nanoseconds);
    
    uiRefreshDisplay();
}

static void createOutputDirectories()
{
    mkdir(HBLOADER_BASE_PATH, 0744);
    mkdir(APP_BASE_PATH, 0744);
    mkdir(XCI_DUMP_PATH, 0744); 
    mkdir(NSP_DUMP_PATH, 0744);
    mkdir(HFS0_DUMP_PATH, 0744);
    mkdir(EXEFS_DUMP_PATH, 0744);
    mkdir(ROMFS_DUMP_PATH, 0744);
    mkdir(CERT_DUMP_PATH, 0744);
    mkdir(BATCH_OVERRIDES_PATH, 0744);
    mkdir(TICKET_PATH, 0744);
}

static bool getSdCardFreeSpace(u64 *out)
{
    Result result;
    FsFileSystem *sdfs = NULL;
    u64 size = 0;
    
    sdfs = fsdevGetDeviceFileSystem("sdmc:");
    if (!sdfs)
    {
        uiStatusMsg("%s: fsdevGetDefaultFileSystem failed!", __func__);
        return false;
    }
    
    result = fsFsGetFreeSpace(sdfs, "/", (s64*)&size);
    if (R_FAILED(result))
    {
        uiStatusMsg("%s: fsFsGetFreeSpace failed! (0x%08X)", __func__, result);
        return false;
    }
    
    *out = size;
    
    return true;
}

void convertSize(u64 size, char *out, size_t outSize)
{
    if (!out || !outSize) return;
    
    double bytes = (double)size;
    
    if (bytes < 1000.0)
    {
        snprintf(out, outSize, "%.0lf B", bytes);
    } else
    if (bytes < (10.0 * KiB))
    {
        snprintf(out, outSize, "%.2lf KiB", floor((bytes * 100.0) / KiB) / 100.0);
    } else
    if (bytes < (100.0 * KiB))
    {
        snprintf(out, outSize, "%.1lf KiB", floor((bytes * 10.0) / KiB) / 10.0);
    } else
    if (bytes < (1000.0 * KiB))
    {
        snprintf(out, outSize, "%.0lf KiB", floor(bytes / KiB));
    } else
    if (bytes < (10.0 * MiB))
    {
        snprintf(out, outSize, "%.2lf MiB", floor((bytes * 100.0) / MiB) / 100.0);
    } else
    if (bytes < (100.0 * MiB))
    {
        snprintf(out, outSize, "%.1lf MiB", floor((bytes * 10.0) / MiB) / 10.0);
    } else
    if (bytes < (1000.0 * MiB))
    {
        snprintf(out, outSize, "%.0lf MiB", floor(bytes / MiB));
    } else
    if (bytes < (10.0 * GiB))
    {
        snprintf(out, outSize, "%.2lf GiB", floor((bytes * 100.0) / GiB) / 100.0);
    } else
    if (bytes < (100.0 * GiB))
    {
        snprintf(out, outSize, "%.1lf GiB", floor((bytes * 10.0) / GiB) / 10.0);
    } else {
        snprintf(out, outSize, "%.0lf GiB", floor(bytes / GiB));
    }
}

void updateFreeSpace()
{
    getSdCardFreeSpace(&freeSpace);
    convertSize(freeSpace, freeSpaceStr, MAX_CHARACTERS(freeSpaceStr));
}

void freeFilenameBuffer(void)
{
    if (!filenameBuffer) return;
    
    for(int i = 0; i < filenameCount; i++)
    {
        if (filenameBuffer[i]) free(filenameBuffer[i]);
    }
    
    filenameCount = filenameIndex = 0;
    
    free(filenameBuffer);
    filenameBuffer = NULL;
}

static bool allocateFilenameBuffer(u32 cnt)
{
    if (!cnt) return false;
    
    freeFilenameBuffer();
    
    filenameBuffer = calloc(cnt, sizeof(char*));
    if (!filenameBuffer) return false;
    
    filenameCount = (int)cnt;
    
    return true;
}

static bool addStringToFilenameBuffer(const char *str)
{
    if (!str || !strlen(str) || (filenameIndex + 1) > filenameCount) return false;
    
    filenameBuffer[filenameIndex] = strdup(str);
    if (!filenameBuffer[filenameIndex])
    {
        freeFilenameBuffer();
        return false;
    }
    
    filenameIndex++;
    
    return true;
}

void initExeFsContext()
{
    memset(&exeFsContext, 0, sizeof(exefs_ctx_t));
}

void freeExeFsContext()
{
    if (exeFsContext.storageId == NcmStorageId_GameCard) closeGameCardStoragePartition();
    exeFsContext.storageId = NcmStorageId_None;
    
    // Remember to close this NCM service resource
    ncmContentStorageClose(&(exeFsContext.ncmStorage));
    memset(&(exeFsContext.ncmStorage), 0, sizeof(NcmContentStorage));
    
    if (exeFsContext.exefs_entries != NULL)
    {
        free(exeFsContext.exefs_entries);
        exeFsContext.exefs_entries = NULL;
    }
    
    if (exeFsContext.exefs_str_table != NULL)
    {
        free(exeFsContext.exefs_str_table);
        exeFsContext.exefs_str_table = NULL;
    }
}

void initRomFsContext()
{
    memset(&romFsContext, 0, sizeof(romfs_ctx_t));
}

void freeRomFsContext()
{
    if (romFsContext.storageId == NcmStorageId_GameCard) closeGameCardStoragePartition();
    romFsContext.storageId = NcmStorageId_None;
    
    // Remember to close this NCM service resource
    ncmContentStorageClose(&(romFsContext.ncmStorage));
    memset(&(romFsContext.ncmStorage), 0, sizeof(NcmContentStorage));
    
    if (romFsContext.romfs_dir_entries != NULL)
    {
        free(romFsContext.romfs_dir_entries);
        romFsContext.romfs_dir_entries = NULL;
    }
    
    if (romFsContext.romfs_file_entries != NULL)
    {
        free(romFsContext.romfs_file_entries);
        romFsContext.romfs_file_entries = NULL;
    }
}

void initBktrContext()
{
    memset(&bktrContext, 0, sizeof(bktr_ctx_t));
}

void freeBktrContext()
{
    if (bktrContext.storageId == NcmStorageId_GameCard) closeGameCardStoragePartition();
    bktrContext.storageId = NcmStorageId_None;
    
    // Remember to close this NCM service resource
    ncmContentStorageClose(&(bktrContext.ncmStorage));
    memset(&(bktrContext.ncmStorage), 0, sizeof(NcmContentStorage));
    
    if (bktrContext.relocation_block != NULL)
    {
        free(bktrContext.relocation_block);
        bktrContext.relocation_block = NULL;
    }
    
    if (bktrContext.subsection_block != NULL)
    {
        free(bktrContext.subsection_block);
        bktrContext.subsection_block = NULL;
    }
    
    if (bktrContext.romfs_dir_entries != NULL)
    {
        free(bktrContext.romfs_dir_entries);
        bktrContext.romfs_dir_entries = NULL;
    }
    
    if (bktrContext.romfs_file_entries != NULL)
    {
        free(bktrContext.romfs_file_entries);
        bktrContext.romfs_file_entries = NULL;
    }
}

static void freeGameCardInfo()
{
    u32 i;
    
    memset(&(gameCardInfo.header), 0, sizeof(gamecard_header_t));
    
    if (gameCardInfo.rootHfs0Header)
    {
        free(gameCardInfo.rootHfs0Header);
        gameCardInfo.rootHfs0Header = NULL;
    }
    
    if (gameCardInfo.hfs0Partitions)
    {
        for(i = 0; i < gameCardInfo.hfs0PartitionCnt; i++)
        {
            if (gameCardInfo.hfs0Partitions[i].header) free(gameCardInfo.hfs0Partitions[i].header);
        }
        
        free(gameCardInfo.hfs0Partitions);
        gameCardInfo.hfs0Partitions = NULL;
    }
    
    gameCardInfo.hfs0PartitionCnt = 0;
    
    gameCardInfo.size = 0;
    memset(gameCardInfo.sizeStr, 0, sizeof(gameCardInfo.sizeStr));
    
    gameCardInfo.trimmedSize = 0;
    memset(gameCardInfo.trimmedSizeStr, 0, sizeof(gameCardInfo.trimmedSizeStr));
    
    for(i = 0; i < ISTORAGE_PARTITION_CNT; i++) gameCardInfo.IStoragePartitionSizes[i] = 0;
    
    gameCardInfo.updateTitleId = 0;
    gameCardInfo.updateVersion = 0;
    memset(gameCardInfo.updateVersionStr, 0, sizeof(gameCardInfo.updateVersionStr));
    
    closeGameCardStoragePartition();
}

static void freeOrphanPatchOrAddOnList()
{
    if (orphanEntries != NULL)
    {
        free(orphanEntries);
        orphanEntries = NULL;
    }
    
    orphanEntriesCnt = 0;
}

static void freeTitleInfo()
{
    u32 i;
    
    if (baseAppEntries && titleAppCount)
    {
        for(i = 0; i < titleAppCount; i++)
        {
            if (baseAppEntries[i].icon) free(baseAppEntries[i].icon);
        }
    }
    
    if (baseAppEntries)
    {
        free(baseAppEntries);
        baseAppEntries = NULL;
    }
    
    if (patchEntries)
    {
        free(patchEntries);
        patchEntries = NULL;
    }
    
    if (addOnEntries)
    {
        free(addOnEntries);
        addOnEntries = NULL;
    }
    
    titleAppCount = 0;
    titlePatchCount = 0;
    titleAddOnCount = 0;
    
    sdCardTitleAppCount = 0;
    sdCardTitlePatchCount = 0;
    sdCardTitleAddOnCount = 0;
    
    emmcTitleAppCount = 0;
    emmcTitlePatchCount = 0;
    emmcTitleAddOnCount = 0;
    
    gameCardSdCardEmmcPatchCount = 0;
    gameCardSdCardEmmcAddOnCount = 0;
    
    freeOrphanPatchOrAddOnList();
}

void freeRomFsBrowserEntries()
{
    if (romFsBrowserEntries != NULL)
    {
        free(romFsBrowserEntries);
        romFsBrowserEntries = NULL;
    }
}

void freeHfs0ExeFsEntriesSizes()
{
    if (hfs0ExeFsEntriesSizes)
    {
        free(hfs0ExeFsEntriesSizes);
        hfs0ExeFsEntriesSizes = NULL;
    }
}

static void freeGlobalData()
{
    freeGameCardInfo();
    
    freeTitleInfo();
    
    freeExeFsContext();
    
    freeRomFsContext();
    
    freeBktrContext();
    
    freeRomFsBrowserEntries();
    
    freeHfs0ExeFsEntriesSizes();
    
    freeFilenameBuffer();
}

u64 hidKeysAllDown()
{
    u8 controller;
    u64 keysDown = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keysDown |= hidKeysDown((HidControllerID)controller);
    
    return keysDown;
}

u64 hidKeysAllHeld()
{
    u8 controller;
    u64 keysHeld = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keysHeld |= hidKeysHeld((HidControllerID)controller);
    
    return keysHeld;
}

void consoleErrorScreen(const char *fmt, ...)
{
    consoleInit(NULL);
    
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    
    printf("\nPress any button to exit.\n");
    
    while(appletMainLoop())
    {
        hidScanInput();
        
        u64 keysDown = hidKeysAllDown(CONTROLLER_P1_AUTO);
        
        if (keysDown && !((keysDown & KEY_TOUCH) || (keysDown & KEY_LSTICK_LEFT) || (keysDown & KEY_LSTICK_RIGHT) || (keysDown & KEY_LSTICK_UP) || (keysDown & KEY_LSTICK_DOWN) || \
            (keysDown & KEY_RSTICK_LEFT) || (keysDown & KEY_RSTICK_RIGHT) || (keysDown & KEY_RSTICK_UP) || (keysDown & KEY_RSTICK_DOWN))) break;
        
        consoleUpdate(NULL);
    }
    
    consoleExit(NULL);
}

static bool initServices()
{
    Result result;
    
    /* Initialize ncm service */
    result = ncmInitialize();
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: failed to initialize ncm service! (0x%08X)", __func__, result);
        return false;
    }
    
    initNcm = true;
    
    /* Initialize ns service */
    result = nsInitialize();
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: failed to initialize ns service! (0x%08X)", __func__, result);
        return false;
    }
    
    initNs = true;
    
    /* Initialize csrng service */
    result = csrngInitialize();
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: failed to initialize csrng service! (0x%08X)", __func__, result);
        return false;
    }
    
    initCsrng = true;
    
    /* Initialize spl service */
    result = splInitialize();
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: failed to initialize spl service! (0x%08X)", __func__, result);
        return false;
    }
    
    initSpl = true;
    
    /* Initialize pm:dmnt service */
    result = pmdmntInitialize();
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: failed to initialize pm:dmnt service! (0x%08X)", __func__, result);
        return false;
    }
    
    initPmdmnt = true;
    
    /* Initialize pl service */
    result = plInitialize();
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: failed to initialize pl service! (0x%08X)", __func__, result);
        return false;
    }
    
    initPl = true;
    
    return true;
}

static void deinitServices()
{
    /* Denitialize pl service */
    if (initPl) plExit();
    
    /* Denitialize pm:dmnt service */
    if (initPmdmnt) pmdmntExit();
    
    /* Denitialize spl service */
    if (initSpl) splExit();
    
    /* Denitialize csrng service */
    if (initCsrng) csrngExit();
    
    /* Denitialize ns service */
    if (initNs) nsExit();
    
    /* Denitialize ncm service */
    if (initNcm) ncmExit();
}

bool initApplicationResources(int argc, char **argv)
{
    Result result = 0;
    bool success = false;
    
    /* Copy launch path */
    if (argc > 0 && argv && !envIsNso())
    {
        for(int i = 0; i < argc; i++)
        {
            if (argv[i] && strlen(argv[i]) > 10 && !strncasecmp(argv[i], "sdmc:/", 6) && !strncasecmp(argv[i] + strlen(argv[i]) - 4, ".nro", 4))
            {
                appLaunchPath = (const char*)argv[i];
                break;
            }
        }
    }
    
    /* Initialize services */
    if (!initServices()) return false;
    
    /* Initialize UI */
    if (!uiInit()) return false;
    
    /* Zero out gamecard info struct */
    memset(&gameCardInfo, 0, sizeof(gamecard_ctx_t));
    
    /* Zero out NCA keyset */
    memset(&nca_keyset, 0, sizeof(nca_keyset_t));
    
    /* Init ExeFS context */
    initExeFsContext();
    
    /* Init RomFS context */
    initRomFsContext();
    
    /* Init BKTR context */
    initBktrContext();
    
    /* Make sure output directories exist */
    createOutputDirectories();
    
    /* Check if the Lockpick_RCM keys file is available */
    keysFileAvailable = checkIfFileExists(KEYS_FILE_PATH);
    
    /* Retrieve running CFW directory */
    retrieveRunningCfwDir();
    
    /* Get applet type */
    programAppletType = appletGetAppletType();
    
    /* Disable screen dimming and auto sleep */
    appletSetMediaPlaybackState(true);
    
    /* Enable CPU boost mode */
    appletSetCpuBoostMode(ApmCpuBoostMode_Type1);
    
    /* Mount eMMC BIS System partition */
    if (!mountSysEmmcPartition()) goto out;
    
    /* Allocate memory for the general purpose dump buffer */
    dumpBuf = calloc(DUMP_BUFFER_SIZE, sizeof(u8));
    if (!dumpBuf)
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for the dump buffer!", __func__);
        goto out;
    }
    
    /* Allocate memory for the gamecard read buffer */
    gcReadBuf = calloc(GAMECARD_READ_BUFFER_SIZE, sizeof(u8));
    if (!gcReadBuf)
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for the gamecard read buffer!", __func__);
        goto out;
    }
    
    /* Allocate memory for the NCA AES-CTR operation buffer */
    ncaCtrBuf = calloc(NCA_CTR_BUFFER_SIZE, sizeof(u8));
    if (!ncaCtrBuf)
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for the NCA AES-CTR operation buffer!", __func__);
        goto out;
    }
    
    /* Open device operator */
    result = fsOpenDeviceOperator(&(gameCardInfo.fsOperatorInstance));
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to open device operator! (0x%08X)", __func__, result);
        goto out;
    }
    
    openFsDevOp = true;
    
    /* Open gamecard detection event notifier */
    result = fsOpenGameCardDetectionEventNotifier(&(gameCardInfo.fsGameCardEventNotifier));
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to open gamecard detection event notifier! (0x%08X)", __func__, result);
        goto out;
    }
    
    openGcEvtNotifier = true;
    
    /* Retrieve gamecard detection kernel event */
    result = fsEventNotifierGetEventHandle(&(gameCardInfo.fsGameCardEventNotifier), &(gameCardInfo.fsGameCardKernelEvent), true);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "%s: failed to retrieve gamecard detection event handle! (0x%08X)", __func__, result);
        goto out;
    }
    
    loadGcKernEvt = true;
    
    /* Create usermode exit event */
    ueventCreate(&exitEvent, false);
    
    /* Create and start gamecard detection thread */
    if (!createGameCardDetectionThread()) goto out;
    
    gcThreadInit = true;
    
    /* Load settings from configuration file */
    loadConfig();
    
    /* Update free space */
    updateFreeSpace();
    
    /* Set output status */
    success = true;
    
out:
    if (!success)
    {
        uiRefreshDisplay();
        delay(5);
    }
    
    return success;
}

void deinitApplicationResources()
{
    /* Free global resources */
    freeGlobalData();
    
    /* Save current settings to configuration file */
    saveConfig();
    
    if (gcThreadInit)
    {
        /* Signal the exit event to terminate the gamecard detection thread */
        ueventSignal(&exitEvent);
        
        /* Wait for the gamecard detection thread to exit */
        pthread_join(gameCardDetectionThread, NULL);
    }
    
    /* Close gamecard detection kernel event */
    if (loadGcKernEvt) eventClose(&(gameCardInfo.fsGameCardKernelEvent));
    
    /* Close gamecard detection event notifier */
    if (openGcEvtNotifier) fsEventNotifierClose(&(gameCardInfo.fsGameCardEventNotifier));
    
    /* Close device operator */
    if (openFsDevOp) fsDeviceOperatorClose(&(gameCardInfo.fsOperatorInstance));
    
    /* Free NCA AES-CTR operation buffer */
    if (ncaCtrBuf) free(ncaCtrBuf);
    
    /* Free gamecard read buffer */
    if (gcReadBuf) free(gcReadBuf);
    
    /* Free general purpose dump buffer */
    if (dumpBuf) free(dumpBuf);
    
    /* Unmount eMMC BIS System partition */
    unmountSysEmmcPartition();
    
    /* Disable CPU boost mode */
    appletSetCpuBoostMode(ApmCpuBoostMode_Disabled);
    
    /* Enable screen dimming and auto sleep */
    appletSetMediaPlaybackState(false);
    
    /* Deinitialize UI */
    uiDeinit();
    
    /* Deinitialize services */
    deinitServices();
}

bool appletModeCheck()
{
    return (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication);
}

void appletModeOperationWarning()
{
    if (!appletModeCheck()) return;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
    breaks++;
}

void changeHomeButtonBlockStatus(bool block)
{
    // Only change HOME button blocking status if we're running as a regular application or a system application, and if it's current blocking status is different than the requested one
    if (appletModeCheck() || block == homeBtnBlocked) return;
    
    if (block)
    {
        appletBeginBlockingHomeButtonShortAndLongPressed(0);
    } else {
        appletEndBlockingHomeButtonShortAndLongPressed();
    }
    
    homeBtnBlocked = block;
}

void formatETAString(u64 curTime, char *out, size_t outSize)
{
    if (!out || !outSize) return;
    
    u64 i, hour = 0, min = 0, sec = 0;
    
    for(i = 0; i < curTime; i++)
    {
        sec++;
        if (sec == 60)
        {
            sec = 0;
            min++;
            if (min == 60)
            {
                min = 0;
                hour++;
            }
        }
    }
    
    snprintf(out, outSize, "%02luH%02luM%02luS", hour, min, sec);
}

void generateSdCardEmmcTitleList()
{
    if (!titleAppCount || !baseAppEntries) return;
    
    if (!allocateFilenameBuffer(titleAppCount)) return;
    
    for(u32 i = 0; i < titleAppCount; i++)
    {
        if (!addStringToFilenameBuffer(baseAppEntries[i].name)) return;
    }
}

static void convertTitleVersionToDotNotation(u32 titleVersion, u8 *outMajor, u8 *outMinor, u8 *outMicro, u16 *outBugfix)
{
    if (!outMajor || !outMinor || !outMicro || !outBugfix) return;
    
    *outMajor = (u8)((titleVersion >> 26) & 0x3F);
    *outMinor = (u8)((titleVersion >> 20) & 0x3F);
    *outMicro = (u8)((titleVersion >> 16) & 0xF);
    *outBugfix = (u16)titleVersion;
}

static void generateVersionDottedStr(u32 titleVersion, char *outBuf, size_t outBufSize)
{
    if (!outBuf || !outBufSize) return;
    
    u8 major = 0, minor = 0, micro = 0;
    u16 bugfix = 0;
    
    convertTitleVersionToDotNotation(titleVersion, &major, &minor, &micro, &bugfix);
    snprintf(outBuf, outBufSize, "%u (%u.%u.%u.%u)", titleVersion, major, minor, micro, bugfix);
}

static bool listTitlesByType(NcmContentMetaDatabase *ncmDb, NcmContentMetaType metaType)
{
    if (!ncmDb || (metaType != NcmContentMetaType_Application && metaType != NcmContentMetaType_Patch && metaType != NcmContentMetaType_AddOnContent))
    {
        uiStatusMsg("%s: invalid parameters to list titles by type from storage!", __func__);
        return false;
    }
    
    bool success = false, proceed = true, memError = false;
    
    Result result;
    
    NcmApplicationContentMetaKey *titleList = NULL, *titleListTmp = NULL;
    size_t titleListSize = sizeof(NcmApplicationContentMetaKey);
    
    u32 i, written = 0, total = 0;
    
    base_app_ctx_t *tmpAppEntries = NULL;
    patch_addon_ctx_t *tmpPatchAddOnEntries = NULL;
    
    titleList = calloc(1, titleListSize);
    if (!titleList)
    {
        uiStatusMsg("%s: unable to allocate memory for the ApplicationContentMetaKey struct! (meta type: 0x%02X).", __func__, (u8)metaType);
        goto out;
    }
    
    result = ncmContentMetaDatabaseListApplication(ncmDb, (s32*)&total, (s32*)&written, titleList, 1, metaType);
    if (R_FAILED(result))
    {
        uiStatusMsg("%s: ncmContentMetaDatabaseListApplication failed! (0x%08X) (meta type: 0x%02X).", __func__, result, (u8)metaType);
        goto out;
    }
    
    if (!written || !total)
    {
        // There are no titles that match the provided filter in the opened storage device
        success = true;
        goto out;
    }
    
    if (total > written)
    {
        titleListSize *= total;
        titleListTmp = realloc(titleList, titleListSize);
        if (titleListTmp)
        {
            titleList = titleListTmp;
            memset(titleList, 0, titleListSize);
            
            result = ncmContentMetaDatabaseListApplication(ncmDb, (s32*)&total, (s32*)&written, titleList, (s32)total, metaType);
            if (R_SUCCEEDED(result))
            {
                if (written != total)
                {
                    uiStatusMsg("%s: title count mismatch in ncmContentMetaDatabaseListApplication! (%u != %u) (meta type: 0x%02X).", __func__, written, total, (u8)metaType);
                    proceed = false;
                }
            } else {
                uiStatusMsg("%s: ncmContentMetaDatabaseListApplication failed! (0x%08X) (meta type: 0x%02X).", __func__, result, (u8)metaType);
                proceed = false;
            }
        } else {
            uiStatusMsg("%s: error reallocating output buffer for ncmContentMetaDatabaseListApplication! (%u %s) (meta type: 0x%02X).", __func__, total, (total == 1 ? "entry" : "entries"), (u8)metaType);
            proceed = false;
        }
    }
    
    if (!proceed) goto out;
    
    if (metaType == NcmContentMetaType_Application)
    {
        // If ptr == NULL, realloc will essentially act as a malloc
        tmpAppEntries = realloc(baseAppEntries, (titleAppCount + total) * sizeof(base_app_ctx_t));
        if (tmpAppEntries)
        {
            baseAppEntries = tmpAppEntries;
            tmpAppEntries = NULL;
            
            memset(baseAppEntries + titleAppCount, 0, total * sizeof(base_app_ctx_t));
            
            for(i = 0; i < total; i++)
            {
                baseAppEntries[titleAppCount + i].titleId = titleList[i].key.id;
                baseAppEntries[titleAppCount + i].version = titleList[i].key.version;
                baseAppEntries[titleAppCount + i].ncmIndex = i;
                generateVersionDottedStr(titleList[i].key.version, baseAppEntries[titleAppCount + i].versionStr, VERSION_STR_LEN);
            }
            
            titleAppCount += total;
            
            success = true;
        } else {
            memError = true;
        }
    } else
    if (metaType == NcmContentMetaType_Patch)
    {
        // If ptr == NULL, realloc will essentially act as a malloc
        tmpPatchAddOnEntries = realloc(patchEntries, (titlePatchCount + total) * sizeof(patch_addon_ctx_t));
        if (tmpPatchAddOnEntries)
        {
            patchEntries = tmpPatchAddOnEntries;
            tmpPatchAddOnEntries = NULL;
            
            memset(patchEntries + titlePatchCount, 0, total * sizeof(patch_addon_ctx_t));
            
            for(i = 0; i < total; i++)
            {
                patchEntries[titlePatchCount + i].titleId = titleList[i].key.id;
                patchEntries[titlePatchCount + i].version = titleList[i].key.version;
                patchEntries[titlePatchCount + i].ncmIndex = i;
                generateVersionDottedStr(titleList[i].key.version, patchEntries[titlePatchCount + i].versionStr, VERSION_STR_LEN);
            }
            
            titlePatchCount += total;
            
            success = true;
        } else {
            memError = true;
        }
    } else
    if (metaType == NcmContentMetaType_AddOnContent)
    {
        // If ptr == NULL, realloc will essentially act as a malloc
        tmpPatchAddOnEntries = realloc(addOnEntries, (titleAddOnCount + total) * sizeof(patch_addon_ctx_t));
        if (tmpPatchAddOnEntries)
        {
            addOnEntries = tmpPatchAddOnEntries;
            tmpPatchAddOnEntries = NULL;
            
            memset(addOnEntries + titleAddOnCount, 0, total * sizeof(patch_addon_ctx_t));
            
            for(i = 0; i < total; i++)
            {
                addOnEntries[titleAddOnCount + i].titleId = titleList[i].key.id;
                addOnEntries[titleAddOnCount + i].version = titleList[i].key.version;
                addOnEntries[titleAddOnCount + i].ncmIndex = i;
                generateVersionDottedStr(titleList[i].key.version, addOnEntries[titleAddOnCount + i].versionStr, VERSION_STR_LEN);
            }
            
            titleAddOnCount += total;
            
            success = true;
        } else {
            memError = true;
        }
    }
    
out:
    if (memError) uiStatusMsg("%s: failed to reallocate entry buffer! (meta type: 0x%02X).", __func__, (u8)metaType);
    
    if (titleList) free(titleList);
    
    return success;
}

static bool getTitleIDAndVersionList(NcmStorageId storageId, bool loadBaseApps, bool loadPatches, bool loadAddOns)
{
    if ((storageId != NcmStorageId_GameCard && storageId != NcmStorageId_SdCard && storageId != NcmStorageId_BuiltInUser) || (!loadBaseApps && !loadPatches && !loadAddOns))
    {
        uiStatusMsg("%s: invalid parameters to retrieve Title ID + version list!", __func__);
        return false;
    }
    
    /* Check if the SD card is really mounted */
    if (storageId == NcmStorageId_SdCard && fsdevGetDeviceFileSystem("sdmc:") == NULL) return true;
    
    bool listApp = false, listPatch = false, listAddOn = false, success = false;
    
    Result result;
    NcmContentMetaDatabase ncmDb;
    
    u32 i;
    u32 curAppCount = titleAppCount, curPatchCount = titlePatchCount, curAddOnCount = titleAddOnCount;
    
    result = ncmOpenContentMetaDatabase(&ncmDb, storageId);
    if (R_FAILED(result))
    {
        if (storageId == NcmStorageId_SdCard && result == 0x21005)
        {
            // If the SD card is mounted, but is isn't currently used by HOS because of some weird reason, just filter this particular error and continue
            // This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted
            return true;
        } else {
            uiStatusMsg("%s: ncmOpenContentMetaDatabase failed for storage ID %u! (0x%08X)", __func__, result, storageId);
            return false;
        }
    }
    
    if (loadBaseApps)
    {
        listApp = listTitlesByType(&ncmDb, NcmContentMetaType_Application);
        if (listApp && titleAppCount > curAppCount)
        {
            for(i = curAppCount; i < titleAppCount; i++) baseAppEntries[i].storageId = storageId;
        }
    }
    
    if (loadPatches)
    {
        listPatch = listTitlesByType(&ncmDb, NcmContentMetaType_Patch);
        if (listPatch && titlePatchCount > curPatchCount)
        {
            for(i = curPatchCount; i < titlePatchCount; i++) patchEntries[i].storageId = storageId;
        }
    }
    
    if (loadAddOns)
    {
        listAddOn = listTitlesByType(&ncmDb, NcmContentMetaType_AddOnContent);
        if (listAddOn && titleAddOnCount > curAddOnCount)
        {
            for(i = curAddOnCount; i < titleAddOnCount; i++) addOnEntries[i].storageId = storageId;
        }
    }
    
    success = (listApp || listPatch || listAddOn);
    
    ncmContentMetaDatabaseClose(&ncmDb);
    
    return success;
}

bool loadTitlesFromSdCardAndEmmc(NcmContentMetaType metaType)
{
    if (menuType != MENUTYPE_GAMECARD || (metaType != NcmContentMetaType_Patch && metaType != NcmContentMetaType_AddOnContent)) return false;
    
    if ((metaType == NcmContentMetaType_Patch && gameCardSdCardEmmcPatchCount) || (metaType == NcmContentMetaType_AddOnContent && gameCardSdCardEmmcAddOnCount)) return true;
    
    u8 i;
    u32 curPatchCount = titlePatchCount, curAddOnCount = titleAddOnCount;
    
    for(i = 0; i < 2; i++)
    {
        NcmStorageId curStorageId = (i == 0 ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser);
        
        if (!getTitleIDAndVersionList(curStorageId, false, (metaType == NcmContentMetaType_Patch), (metaType == NcmContentMetaType_AddOnContent))) continue;
        
        if (metaType == NcmContentMetaType_Patch)
        {
            if (titlePatchCount > curPatchCount)
            {
                u32 newPatchCount = (titlePatchCount - curPatchCount);
                
                gameCardSdCardEmmcPatchCount = newPatchCount;
                
                if (curStorageId == NcmStorageId_SdCard)
                {
                    sdCardTitlePatchCount = newPatchCount;
                } else {
                    emmcTitlePatchCount = newPatchCount;
                }
            }
        } else
        if (metaType == NcmContentMetaType_AddOnContent)
        {
            if (titleAddOnCount > curAddOnCount)
            {
                u32 newAddOnCount = (titleAddOnCount - curAddOnCount);
                
                gameCardSdCardEmmcAddOnCount = newAddOnCount;
                
                if (curStorageId == NcmStorageId_SdCard)
                {
                    sdCardTitleAddOnCount = newAddOnCount;
                } else {
                    emmcTitleAddOnCount = newAddOnCount;
                }
            }
        }
    }
    
    if ((metaType == NcmContentMetaType_Patch && gameCardSdCardEmmcPatchCount) || (metaType == NcmContentMetaType_AddOnContent && gameCardSdCardEmmcAddOnCount)) return true;
    
    return false;
}

void freeTitlesFromSdCardAndEmmc(NcmContentMetaType metaType)
{
    if (menuType != MENUTYPE_GAMECARD || (metaType != NcmContentMetaType_Patch && metaType != NcmContentMetaType_AddOnContent) || (metaType == NcmContentMetaType_Patch && (!titlePatchCount || !gameCardSdCardEmmcPatchCount)) || (metaType == NcmContentMetaType_AddOnContent && (!titleAddOnCount || !gameCardSdCardEmmcAddOnCount))) return;
    
    patch_addon_ctx_t *tmpPatchAddOnEntries = NULL;
    
    if (metaType == NcmContentMetaType_Patch)
    {
        if ((titlePatchCount - gameCardSdCardEmmcPatchCount) > 0)
        {
            tmpPatchAddOnEntries = realloc(patchEntries, (titlePatchCount - gameCardSdCardEmmcPatchCount) * sizeof(patch_addon_ctx_t));
            if (tmpPatchAddOnEntries != NULL)
            {
                patchEntries = tmpPatchAddOnEntries;
                tmpPatchAddOnEntries = NULL;
            }
        } else {
            free(patchEntries);
            patchEntries = NULL;
        }
        
        titlePatchCount -= gameCardSdCardEmmcPatchCount;
        
        gameCardSdCardEmmcPatchCount = 0;
        sdCardTitlePatchCount = 0;
        emmcTitlePatchCount = 0;
    } else {
        if ((titleAddOnCount - gameCardSdCardEmmcAddOnCount) > 0)
        {
            tmpPatchAddOnEntries = realloc(addOnEntries, (titleAddOnCount - gameCardSdCardEmmcAddOnCount) * sizeof(patch_addon_ctx_t));
            if (tmpPatchAddOnEntries != NULL)
            {
                addOnEntries = tmpPatchAddOnEntries;
                tmpPatchAddOnEntries = NULL;
            }
        } else {
            free(addOnEntries);
            addOnEntries = NULL;
        }
        
        titleAddOnCount -= gameCardSdCardEmmcAddOnCount;
        
        gameCardSdCardEmmcAddOnCount = 0;
        sdCardTitleAddOnCount = 0;
        emmcTitleAddOnCount = 0;
    }
}

static bool getCachedBaseApplicationNacpMetadata(u64 titleID, char *nameBuf, size_t nameBufSize, char *authorBuf, size_t authorBufSize, u8 **iconBuf)
{
    // At least the name must be retrieved
    if (!nameBuf || !nameBufSize || (authorBuf && !authorBufSize))
    {
        uiStatusMsg("%s: invalid parameters to retrieve Control.nacp!", __func__);
        return false;
    }
    
    bool getNameAndAuthor = false, getIcon = false, success = false;
    Result result;
    size_t outsize = 0;
    NsApplicationControlData *buf = NULL;
    NacpLanguageEntry *langentry = NULL;
    
    buf = calloc(1, sizeof(NsApplicationControlData));
    if (buf)
    {
        result = nsGetApplicationControlData(NsApplicationControlSource_Storage, titleID, buf, sizeof(NsApplicationControlData), &outsize);
        if (R_SUCCEEDED(result))
        {
            if (outsize >= sizeof(buf->nacp))
            {
                result = nacpGetLanguageEntry(&buf->nacp, &langentry);
                if (R_SUCCEEDED(result))
                {
                    snprintf(nameBuf, nameBufSize, langentry->name);
                    if (authorBuf && authorBufSize) snprintf(authorBuf, authorBufSize, langentry->author);
                    getNameAndAuthor = true;
                } else {
                    uiStatusMsg("%s: GetLanguageEntry failed! (0x%08X)", __func__, result);
                }
                
                if (iconBuf != NULL)
                {
                    getIcon = uiLoadJpgFromMem(buf->icon, sizeof(buf->icon), NACP_ICON_SQUARE_DIMENSION, NACP_ICON_SQUARE_DIMENSION, NACP_ICON_DOWNSCALED, NACP_ICON_DOWNSCALED, iconBuf);
                    if (!getIcon) uiStatusMsg(strbuf);
                }
                
                success = (iconBuf != NULL ? (getNameAndAuthor && getIcon) : getNameAndAuthor);
            } else {
                uiStatusMsg("%s: Control.nacp buffer size (%u bytes) is too small! Expected: %u bytes", __func__, outsize, sizeof(buf->nacp));
            }
        } else {
            uiStatusMsg("%s: GetApplicationControlData failed! (0x%08X)", __func__, result);
        }
        
        free(buf);
    } else {
        uiStatusMsg("%s: unable to allocate memory for the ns service operations!", __func__);
    }
    
    return success;
}

void removeIllegalCharacters(char *name)
{
    if (!name || !strlen(name)) return;
    
    u32 i, len = strlen(name);
    for (i = 0; i < len; i++)
    {
        if (memchr("?[]/\\=+<>:;\",*|^", name[i], sizeof("?[]/\\=+<>:;\",*|^") - 1) || name[i] < 0x20 || name[i] > 0x7E) name[i] = '_';
    }
}

void strtrim(char *str)
{
    if (!str || !strlen(str)) return;
    
    char *start = str;
    char *end = (start + strlen(str));
    
    while(--end >= start)
    {
        if (!isspace((unsigned char)*end)) break;
    }
    
    *(++end) = '\0';
    
    while(isspace((unsigned char)*start)) start++;
    
    if (start != str) memmove(str, start, end - start + 1);
}

bool retrieveGameCardInfo()
{
    Result result;
    bool success = false;
    
    u32 i;
    hfs0_header header;
    hfs0_file_entry entry;
    
    u8 major = 0, minor = 0, micro = 0;
    u16 bugfix = 0;
    
    // Open normal IStorage partition
    result = openGameCardStoragePartition(ISTORAGE_PARTITION_NORMAL);
    if (R_FAILED(result))
    {
        uiStatusMsg("%s: failed to open normal IStorage partition! (0x%08X)", __func__, result);
        return false;
    }
    
    // Retrieve normal IStorage partition size
    result = getGameCardStoragePartitionSize(&(gameCardInfo.IStoragePartitionSizes[0]));
    if (R_FAILED(result))
    {
        uiStatusMsg("%s: failed to retrieve size for normal IStorage partition! (0x%08X)", __func__, result);
        return false;
    }
    
    // Read gamecard header
    result = readGameCardStoragePartition(0, &(gameCardInfo.header), sizeof(gamecard_header_t));
    if (R_FAILED(result))
    {
        uiStatusMsg("%s: failed to read %lu bytes long gamecard header! (0x%08X)", __func__, sizeof(gamecard_header_t), result);
        goto out;
    }
    
    if (__builtin_bswap32(gameCardInfo.header.magic) != GAMECARD_HEADER_MAGIC)
    {
        uiStatusMsg("%s: invalid gamecard header magic word! (0x%08X)", __func__, __builtin_bswap32(gameCardInfo.header.magic));
        goto out;
    }
    
    switch(gameCardInfo.header.size)
    {
        case 0xFA: // 1 GiB
            gameCardInfo.size = GAMECARD_SIZE_1GiB;
            break;
        case 0xF8: // 2 GiB
            gameCardInfo.size = GAMECARD_SIZE_2GiB;
            break;
        case 0xF0: // 4 GiB
            gameCardInfo.size = GAMECARD_SIZE_4GiB;
            break;
        case 0xE0: // 8 GiB
            gameCardInfo.size = GAMECARD_SIZE_8GiB;
            break;
        case 0xE1: // 16 GiB
            gameCardInfo.size = GAMECARD_SIZE_16GiB;
            break;
        case 0xE2: // 32 GiB
            gameCardInfo.size = GAMECARD_SIZE_32GiB;
            break;
        default:
            uiStatusMsg("%s: invalid gamecard size value! (0x%02X)", __func__, gameCardInfo.header.size);
            goto out;
    }
    
    convertSize(gameCardInfo.size, gameCardInfo.sizeStr, MAX_CHARACTERS(gameCardInfo.sizeStr));
    
    gameCardInfo.trimmedSize = (sizeof(gamecard_header_t) + (gameCardInfo.header.validDataEndAddr * MEDIA_UNIT_SIZE));
    convertSize(gameCardInfo.trimmedSize, gameCardInfo.trimmedSizeStr, MAX_CHARACTERS(gameCardInfo.trimmedSizeStr));
    
    gameCardInfo.rootHfs0Header = calloc(1, gameCardInfo.header.rootHfs0HeaderSize);
    if (!gameCardInfo.rootHfs0Header)
    {
        uiStatusMsg("%s: unable to allocate memory for the root HFS0 header!", __func__);
        goto out;
    }
    
    result = readGameCardStoragePartition(gameCardInfo.header.rootHfs0HeaderOffset, gameCardInfo.rootHfs0Header, gameCardInfo.header.rootHfs0HeaderSize);
    if (R_FAILED(result))
    {
        uiStatusMsg("%s: failed to read %lu bytes long root HFS0 header! (0x%08X)", __func__, gameCardInfo.header.rootHfs0HeaderSize, result);
        goto out;
    }
    
    memcpy(&header, gameCardInfo.rootHfs0Header, sizeof(hfs0_header));
    
    if (__builtin_bswap32(header.magic) != HFS0_MAGIC)
    {
        uiStatusMsg("%s: invalid magic word in root HFS0 header! (0x%08X)", __func__, __builtin_bswap32(header.magic));
        goto out;
    }
    
    if (!header.file_cnt)
    {
        uiStatusMsg("%s: invalid file count in root HFS0 header!", __func__);
        goto out;
    }
    
    if (!header.str_table_size)
    {
        uiStatusMsg("%s: invalid string table size in root HFS0 header!", __func__);
        goto out;
    }
    
    gameCardInfo.hfs0PartitionCnt = header.file_cnt;
    
    // Retrieve partition data
    gameCardInfo.hfs0Partitions = calloc(gameCardInfo.hfs0PartitionCnt, sizeof(hfs0_partition_info));
    if (!gameCardInfo.hfs0Partitions)
    {
        uiStatusMsg("%s: unable to allocate memory for HFS0 partition headers!", __func__);
        goto out;
    }
    
    for(i = 0; i < gameCardInfo.hfs0PartitionCnt; i++)
    {
        memcpy(&entry, gameCardInfo.rootHfs0Header + sizeof(hfs0_header) + (i * sizeof(hfs0_file_entry)), sizeof(hfs0_file_entry));
        
        if (!entry.file_size)
        {
            uiStatusMsg("%s: invalid size for %s HFS0 partition!", __func__, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, i));
            goto out;
        }
        
        gameCardInfo.hfs0Partitions[i].size = entry.file_size;
        
        // Check if we're dealing with the secure HFS0 partition
        if (i == (gameCardInfo.hfs0PartitionCnt - 1))
        {
            // The partition offset must be zero, because the secure HFS0 partition is stored at the start of the secure IStorage partition
            gameCardInfo.hfs0Partitions[i].offset = 0;
            
            // Open secure IStorage partition
            result = openGameCardStoragePartition(ISTORAGE_PARTITION_SECURE);
            if (R_FAILED(result))
            {
                uiStatusMsg("%s: failed to open secure IStorage partition! (0x%08X)", __func__, result);
                goto out;
            }
            
            if (strncmp(cfwDirStr, CFW_PATH_SXOS, strlen(CFW_PATH_SXOS)) != 0)
            {
                // Retrieve secure IStorage partition size
                result = getGameCardStoragePartitionSize(&(gameCardInfo.IStoragePartitionSizes[1]));
                if (R_FAILED(result))
                {
                    uiStatusMsg("%s: failed to retrieve size for secure IStorage partition! (0x%08X)", __func__, result);
                    goto out;
                }
            } else {
                // Total size for the secure IStorage partition is maxed out under SX OS, so let's try to calculate it manually
                gameCardInfo.IStoragePartitionSizes[1] = ((gameCardInfo.size - ((gameCardInfo.size / GAMECARD_ECC_BLOCK_SIZE) * GAMECARD_ECC_DATA_SIZE)) - gameCardInfo.IStoragePartitionSizes[0]);
            }
        } else {
            // The partition offset is relative to the start of the normal IStorage partition (true gamecard image start)
            gameCardInfo.hfs0Partitions[i].offset = (gameCardInfo.header.rootHfs0HeaderOffset + gameCardInfo.header.rootHfs0HeaderSize + entry.file_offset);
        }
        
        // Partially read the current HFS0 partition header
        result = readGameCardStoragePartition(gameCardInfo.hfs0Partitions[i].offset, &header, sizeof(hfs0_header));
        if (R_FAILED(result))
        {
            uiStatusMsg("%s: failed to read %lu bytes long chunk from %s HFS0 partition! (0x%08X)", __func__, sizeof(hfs0_header), GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, i), result);
            goto out;
        }
        
        // Check the HFS0 magic word
        if (__builtin_bswap32(header.magic) != HFS0_MAGIC)
        {
            uiStatusMsg("%s: invalid magic word in %s HFS0 partition header! (0x%08X)", __func__, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, i), __builtin_bswap32(header.magic));
            goto out;
        }
        
        if (!header.str_table_size)
        {
            uiStatusMsg("%s: invalid string table size in %s HFS0 partition header!", __func__, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, i));
            goto out;
        }
        
        // Calculate the size for the HFS0 partition header and round it to a MEDIA_UNIT_SIZE bytes boundary
        gameCardInfo.hfs0Partitions[i].header_size = (sizeof(hfs0_header) + (header.file_cnt * sizeof(hfs0_file_entry)) + header.str_table_size);
        gameCardInfo.hfs0Partitions[i].header_size = round_up(gameCardInfo.hfs0Partitions[i].header_size, MEDIA_UNIT_SIZE);
        
        gameCardInfo.hfs0Partitions[i].file_cnt = header.file_cnt;
        gameCardInfo.hfs0Partitions[i].str_table_size = header.str_table_size;
        
        gameCardInfo.hfs0Partitions[i].header = calloc(1, gameCardInfo.hfs0Partitions[i].header_size);
        if (!gameCardInfo.hfs0Partitions[i].header)
        {
            uiStatusMsg("%s: unable to allocate memory for %s HFS0 partition header!", __func__, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, i));
            goto out;
        }
        
        // Finally, read the full HFS0 partition header
        result = readGameCardStoragePartition(gameCardInfo.hfs0Partitions[i].offset, gameCardInfo.hfs0Partitions[i].header, gameCardInfo.hfs0Partitions[i].header_size);
        if (R_FAILED(result))
        {
            uiStatusMsg("%s: failed to read %lu bytes long %s HFS0 partition header! (0x%08X)", __func__, gameCardInfo.hfs0Partitions[i].header_size, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, i), result);
            goto out;
        }
    }
    
    // Get bundled FW version update
    result = fsDeviceOperatorUpdatePartitionInfo(&(gameCardInfo.fsOperatorInstance), &(gameCardInfo.fsGameCardHandle), &(gameCardInfo.updateVersion), &(gameCardInfo.updateTitleId));
    if (R_SUCCEEDED(result))
    {
        if (gameCardInfo.updateTitleId == GAMECARD_UPDATE_TITLEID)
        {
            convertTitleVersionToDotNotation(gameCardInfo.updateVersion, &major, &minor, &micro, &bugfix);
            snprintf(gameCardInfo.updateVersionStr, MAX_CHARACTERS(gameCardInfo.updateVersionStr), "%u.%u.%u (v%u)", major, minor, micro, gameCardInfo.updateVersion);
        } else {
            uiStatusMsg("%s: update Title ID mismatch! (%016lX != %016lX)", __func__, gameCardInfo.updateTitleId, GAMECARD_UPDATE_TITLEID);
        }
    } else {
        uiStatusMsg("%s: UpdatePartitionInfo failed! (0x%08X)", __func__, result);
    }
    
    success = true;
    
out:
    if (success)
    {
        closeGameCardStoragePartition();
    } else {
        freeGameCardInfo();
    }
    
    return success;
}

u64 calculateSizeFromContentRecords(NcmStorageId curStorageId, NcmContentMetaType metaType, u32 ncmTitleCount, u32 ncmTitleIndex)
{
    if ((curStorageId != NcmStorageId_GameCard && curStorageId != NcmStorageId_SdCard && curStorageId != NcmStorageId_BuiltInUser) || (metaType != NcmContentMetaType_Application && metaType != NcmContentMetaType_Patch && metaType != NcmContentMetaType_AddOnContent) || ncmTitleIndex >= ncmTitleCount) return 0;
    
    NcmContentInfo *titleContentInfos = NULL;
    u32 i, titleContentInfoCnt = 0;
    u64 tmp = 0, outSize = 0;
    
    if (!retrieveContentInfosFromTitle(curStorageId, metaType, ncmTitleCount, ncmTitleIndex, &titleContentInfos, &titleContentInfoCnt)) return 0;
    
    for(i = 0; i < titleContentInfoCnt; i++) 
    {
        if (titleContentInfos[i].content_type >= NcmContentType_DeltaFragment) continue;
        
        convertNcaSizeToU64(titleContentInfos[i].size, &tmp);
        outSize += tmp;
    }
    
    free(titleContentInfos);
    
    return outSize;
}

int baseAppCmp(const void *a, const void *b)
{
	base_app_ctx_t *baseApp1 = (base_app_ctx_t*)a;
	base_app_ctx_t *baseApp2 = (base_app_ctx_t*)b;
	
	return strcasecmp(baseApp1->name, baseApp2->name);
}

int orphanEntryCmp(const void *a, const void *b)
{
	orphan_patch_addon_entry *orphanEntry1 = (orphan_patch_addon_entry*)a;
	orphan_patch_addon_entry *orphanEntry2 = (orphan_patch_addon_entry*)b;
	
	return strcasecmp(orphanEntry1->orphanListStr, orphanEntry2->orphanListStr);
}

void loadTitleInfo()
{
    if (menuType == MENUTYPE_MAIN)
    {
        freeGlobalData();
        changeAtomicBool(&gameCardInfoLoaded, false);
        sdCardAndEmmcTitleInfoLoaded = false;
        return;
    }
    
    bool proceed = false;
    
    if (menuType == MENUTYPE_GAMECARD)
    {
        if (gameCardInfo.isInserted && gameCardInfoLoaded) return;
        
        freeGlobalData();
        
        if (!gameCardInfo.isInserted) return;
        
        /* Don't access the gamecard immediately to avoid conflicts with the fsp-srv, ncm and ns services */
        uiPleaseWait(GAMECARD_WAIT_TIME);
        
        proceed = retrieveGameCardInfo();
        changeAtomicBool(&gameCardInfoLoaded, true);
        
        if (proceed) proceed = getTitleIDAndVersionList(NcmStorageId_GameCard, true, true, true);
    } else
    if (menuType == MENUTYPE_SDCARD_EMMC)
    {
        if (sdCardAndEmmcTitleInfoLoaded) return;
        
        uiPleaseWait(0);
        
        freeTitleInfo();
        
        if (getTitleIDAndVersionList(NcmStorageId_SdCard, true, true, true))
        {
            sdCardTitleAppCount = titleAppCount;
            sdCardTitlePatchCount = titlePatchCount;
            sdCardTitleAddOnCount = titleAddOnCount;
            
            if (getTitleIDAndVersionList(NcmStorageId_BuiltInUser, true, true, true))
            {
                emmcTitleAppCount = (titleAppCount - sdCardTitleAppCount);
                emmcTitlePatchCount = (titlePatchCount - sdCardTitlePatchCount);
                emmcTitleAddOnCount = (titleAddOnCount - sdCardTitleAddOnCount);
                
                proceed = true;
            }
        }
        
        sdCardAndEmmcTitleInfoLoaded = true;
    }
    
    if (proceed)
    {
        u32 i, ncmTitleCount;
        
        for(i = 0; i < titleAppCount; i++)
        {
            // Retrieve base application name, author and icon
            if (getCachedBaseApplicationNacpMetadata(baseAppEntries[i].titleId, baseAppEntries[i].name, MAX_CHARACTERS(baseAppEntries[i].name), baseAppEntries[i].author, MAX_CHARACTERS(baseAppEntries[i].author), &(baseAppEntries[i].icon)))
            {
                strtrim(baseAppEntries[i].name);
                strtrim(baseAppEntries[i].author);
                snprintf(baseAppEntries[i].fixedName, MAX_CHARACTERS(baseAppEntries[i].fixedName), baseAppEntries[i].name);
                removeIllegalCharacters(baseAppEntries[i].fixedName);
            }
            
            // Retrieve base application content size
            ncmTitleCount = (baseAppEntries[i].storageId == NcmStorageId_GameCard ? titleAppCount : (baseAppEntries[i].storageId == NcmStorageId_SdCard ? sdCardTitleAppCount : emmcTitleAppCount));
            baseAppEntries[i].contentSize = calculateSizeFromContentRecords(baseAppEntries[i].storageId, NcmContentMetaType_Application, ncmTitleCount, baseAppEntries[i].ncmIndex);
            convertSize(baseAppEntries[i].contentSize, baseAppEntries[i].contentSizeStr, MAX_CHARACTERS(baseAppEntries[i].contentSizeStr));
        }
        
        // Sort base applications by name
        if (titleAppCount) qsort(baseAppEntries, titleAppCount, sizeof(base_app_ctx_t), baseAppCmp);
        
        for(i = 0; i < titlePatchCount; i++)
        {
            // Retrieve patch content size
            ncmTitleCount = (patchEntries[i].storageId == NcmStorageId_GameCard ? titlePatchCount : (patchEntries[i].storageId == NcmStorageId_SdCard ? sdCardTitlePatchCount : emmcTitlePatchCount));
            patchEntries[i].contentSize = calculateSizeFromContentRecords(patchEntries[i].storageId, NcmContentMetaType_Patch, ncmTitleCount, patchEntries[i].ncmIndex);
            convertSize(patchEntries[i].contentSize, patchEntries[i].contentSizeStr, MAX_CHARACTERS(patchEntries[i].contentSizeStr));
        }
        
        for(i = 0; i < titleAddOnCount; i++)
        {
            // Retrieve add-on content size
            ncmTitleCount = (addOnEntries[i].storageId == NcmStorageId_GameCard ? titleAddOnCount : (addOnEntries[i].storageId == NcmStorageId_SdCard ? sdCardTitleAddOnCount : emmcTitleAddOnCount));
            addOnEntries[i].contentSize = calculateSizeFromContentRecords(addOnEntries[i].storageId, NcmContentMetaType_AddOnContent, ncmTitleCount, addOnEntries[i].ncmIndex);
            convertSize(addOnEntries[i].contentSize, addOnEntries[i].contentSizeStr, MAX_CHARACTERS(addOnEntries[i].contentSizeStr));
        }
        
        // Generate orphan content list
        // If orphanEntries == NULL or if orphanEntriesCnt == 0, both variables will be regenerated
        // Otherwise, this will only fill filenameBuffer
        if (menuType == MENUTYPE_SDCARD_EMMC) generateOrphanPatchOrAddOnList();
    }
    
    uiPrintHeadline();
}

void truncateBrowserEntryName(char *str)
{
    if (!str || !strlen(str)) return;
    
    u32 strWidth = uiGetStrWidth(str);
    u32 limit = (u32)(FB_WIDTH - (font_height * 8));
    
    if ((BROWSER_ICON_DIMENSION + 16 + strWidth) >= limit)
    {
        while((BROWSER_ICON_DIMENSION + 16 + strWidth) >= limit)
        {
            str[strlen(str) - 1] = '\0';
            strWidth = uiGetStrWidth(str);
        }
        
        strcat(str, "...");
    }
}

bool getHfs0FileList(u32 partition)
{
    if (partition >= gameCardInfo.hfs0PartitionCnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid HFS0 partition index!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!gameCardInfo.hfs0Partitions || !gameCardInfo.hfs0Partitions[partition].header || !gameCardInfo.hfs0Partitions[partition].header_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: HFS0 partition header information unavailable!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!gameCardInfo.hfs0Partitions[partition].file_cnt || !gameCardInfo.hfs0Partitions[partition].str_table_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: the selected HFS0 partition is empty!", __func__);
        breaks += 2;
        return false;
    }
    
    u32 i;
    hfs0_file_entry entry;
    char curName[NAME_BUF_LEN] = {'\0'};
    
    freeHfs0ExeFsEntriesSizes();
    
    hfs0ExeFsEntriesSizes = calloc(gameCardInfo.hfs0Partitions[partition].file_cnt, sizeof(browser_entry_size_info));
    if (!hfs0ExeFsEntriesSizes)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for HFS0 entries size info!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!allocateFilenameBuffer(gameCardInfo.hfs0Partitions[partition].file_cnt))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for the filename buffer!", __func__);
        breaks += 2;
        return false;
    }
    
    for(i = 0; i < gameCardInfo.hfs0Partitions[partition].file_cnt; i++)
    {
        memcpy(&entry, gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (i * sizeof(hfs0_file_entry)), sizeof(hfs0_file_entry));
        
        char *cur_filename = (char*)(gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (gameCardInfo.hfs0Partitions[partition].file_cnt * sizeof(hfs0_file_entry)) + entry.filename_offset);
        
        snprintf(curName, MAX_CHARACTERS(curName), cur_filename);
        
        // Fix entry name length
        truncateBrowserEntryName(curName);
        
        if (!addStringToFilenameBuffer(curName))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for filename entry in filename buffer!", __func__);
            breaks += 2;
            freeHfs0ExeFsEntriesSizes();
            return false;
        }
        
        // Save entry size
        hfs0ExeFsEntriesSizes[i].size = entry.file_size;
        convertSize(hfs0ExeFsEntriesSizes[i].size, hfs0ExeFsEntriesSizes[i].sizeStr, MAX_CHARACTERS(hfs0ExeFsEntriesSizes[i].sizeStr));
    }
    
    return true;
}

// Used to retrieve data from files in the HFS0 Secure partition
// An IStorage instance must have been opened beforehand
bool readFileFromSecureHfs0PartitionByName(const char *filename, u64 offset, void *outBuf, size_t bufSize)
{
    if (!gameCardInfo.hfs0PartitionCnt || !gameCardInfo.hfs0Partitions || !gameCardInfo.hfs0Partitions[gameCardInfo.hfs0PartitionCnt - 1].header || !gameCardInfo.hfs0Partitions[gameCardInfo.hfs0PartitionCnt - 1].header_size || !gameCardInfo.hfs0Partitions[gameCardInfo.hfs0PartitionCnt - 1].file_cnt || !gameCardInfo.hfs0Partitions[gameCardInfo.hfs0PartitionCnt - 1].str_table_size || !filename || !strlen(filename) || !outBuf || !bufSize)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to read file from Secure HFS0 partition!", __func__);
        return false;
    }
    
    u32 i;
    Result result;
    hfs0_file_entry entry;
    
    bool proceed = true, found = false;
    
    u32 partition = (gameCardInfo.hfs0PartitionCnt - 1); // Select the Secure HFS0 partition
    
    for(i = 0; i < gameCardInfo.hfs0Partitions[partition].file_cnt; i++)
    {
        memcpy(&entry, gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (i * sizeof(hfs0_file_entry)), sizeof(hfs0_file_entry));
        
        char *cur_filename = (char*)(gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (gameCardInfo.hfs0Partitions[partition].file_cnt * sizeof(hfs0_file_entry)) + entry.filename_offset);
        
        if (strncasecmp(cur_filename, filename, strlen(filename)) != 0) continue;
        
        found = true;
        
        if (!entry.file_size || (offset + bufSize) > entry.file_size)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid file size for \"%s\"!", __func__, filename);
            proceed = false;
        }
        
        break;
    }
    
    if (!proceed || !found)
    {
        if (proceed && !found) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find file \"%s\" in Secure HFS0 partition!", __func__, filename);
        return false;
    }
    
    result = readGameCardStoragePartition(gameCardInfo.hfs0Partitions[partition].header_size + entry.file_offset + offset, outBuf, bufSize);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read file \"%s\"! (0x%08X)", __func__, filename, result);
        return false;
    }
    
    return true;
}

bool calculateExeFsExtractedDataSize(u64 *out)
{
    if (!exeFsContext.exefs_header.file_cnt || !exeFsContext.exefs_entries || !out)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to calculate extracted data size for the ExeFS section!", __func__);
        return false;
    }
    
    u32 i;
    u64 totalSize = 0;
    
    for(i = 0; i < exeFsContext.exefs_header.file_cnt; i++) totalSize += exeFsContext.exefs_entries[i].file_size;
    
    *out = totalSize;
    
    return true;
}

bool calculateRomFsFullExtractedSize(bool usePatch, u64 *out)
{
    if ((!usePatch && (!romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || !out)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to calculate extracted data size for the RomFS section!", __func__);
        return false;
    }
    
    u64 offset = 0;
    u64 totalSize = 0;
    
    u64 filetableSize = (!usePatch ? romFsContext.romfs_filetable_size : bktrContext.romfs_filetable_size);
    romfs_file *fileEntries = (!usePatch ? romFsContext.romfs_file_entries : bktrContext.romfs_file_entries);
    
    while(offset < filetableSize)
    {
        romfs_file *entry = (romfs_file*)((u8*)fileEntries + offset);
        
        totalSize += entry->dataSize;
        
        offset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
    }
    
    *out = totalSize;
    
    return true;
}

bool calculateRomFsExtractedDirSize(u32 dir_offset, bool usePatch, u64 *out)
{
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries || dir_offset > romFsContext.romfs_dirtable_size)) || (usePatch && (!bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries || !bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries || dir_offset > bktrContext.romfs_dirtable_size)) || !out)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to calculate extracted size for the current RomFS directory!", __func__);
        return false;
    }
    
    u64 totalSize = 0, childDirSize = 0;
    romfs_file *fileEntry = NULL;
    romfs_dir *childDirEntry = NULL;
    romfs_dir *dirEntry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
    
    // Check if we're dealing with a nameless directory that's not the root directory
    if (!dirEntry->nameLen && dir_offset > 0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: directory entry without name in RomFS section!", __func__);
        return false;
    }
    
    if (dirEntry->childFile != ROMFS_ENTRY_EMPTY)
    {
        fileEntry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + dirEntry->childFile) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + dirEntry->childFile));
        
        totalSize += fileEntry->dataSize;
        
        while(fileEntry->sibling != ROMFS_ENTRY_EMPTY)
        {
            fileEntry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + fileEntry->sibling) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + fileEntry->sibling));
            
            totalSize += fileEntry->dataSize;
        }
    }
    
    if (dirEntry->childDir != ROMFS_ENTRY_EMPTY)
    {
        if (!calculateRomFsExtractedDirSize(dirEntry->childDir, usePatch, &childDirSize)) return false;
        
        totalSize += childDirSize;
        
        childDirEntry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dirEntry->childDir) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dirEntry->childDir));
        
        while(childDirEntry->sibling != ROMFS_ENTRY_EMPTY)
        {
            if (!calculateRomFsExtractedDirSize(childDirEntry->sibling, usePatch, &childDirSize)) return false;
            
            totalSize += childDirSize;
            
            childDirEntry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + childDirEntry->sibling) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + childDirEntry->sibling));
        }
    }
    
    *out = totalSize;
    
    return true;
}

bool retrieveContentInfosFromTitle(NcmStorageId storageId, NcmContentMetaType metaType, u32 titleCount, u32 titleIndex, NcmContentInfo **outContentInfos, u32 *outContentInfoCnt)
{
    Result result;
    
    NcmContentMetaDatabase ncmDb;
    memset(&ncmDb, 0, sizeof(NcmContentMetaDatabase));
    
    NcmContentMetaHeader cnmtHeader;
    memset(&cnmtHeader, 0, sizeof(NcmContentMetaHeader));
    u64 cnmtHeaderReadSize = 0;
    
    NcmApplicationContentMetaKey *titleList = NULL;
    size_t titleListSize = (sizeof(NcmApplicationContentMetaKey) * titleCount);
    
    NcmContentInfo *titleContentInfos = NULL;
    u32 titleContentInfoCnt = 0;
    
    u32 written = 0, total = 0;
    
    bool success = false;
    
    if (storageId != NcmStorageId_GameCard && storageId != NcmStorageId_SdCard && storageId != NcmStorageId_BuiltInUser)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid title storage ID!", __func__);
        goto out;
    }
    
    if (metaType != NcmContentMetaType_Application && metaType != NcmContentMetaType_Patch && metaType != NcmContentMetaType_AddOnContent)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid title meta type!", __func__);
        goto out;
    }
    
    if (!titleCount)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid title type count!", __func__);
        goto out;
    }
    
    if (titleIndex >= titleCount)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid title index!", __func__);
        goto out;
    }
    
    if (!outContentInfos || !outContentInfoCnt)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid output parameters!", __func__);
        goto out;
    }
    
    titleList = calloc(1, titleListSize);
    if (!titleList)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: unable to allocate memory for the ApplicationContentMetaKey struct!", __func__);
        goto out;
    }
    
    result = ncmOpenContentMetaDatabase(&ncmDb, storageId);
    if (R_FAILED(result))
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: ncmOpenContentMetaDatabase failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    result = ncmContentMetaDatabaseListApplication(&ncmDb, (s32*)&total, (s32*)&written, titleList, (s32)titleCount, metaType);
    if (R_FAILED(result))
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: ncmContentMetaDatabaseListApplication failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    if (!written || !total)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: ncmContentMetaDatabaseListApplication wrote no entries to output buffer!", __func__);
        goto out;
    }
    
    if (written != total)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: title count mismatch in ncmContentMetaDatabaseListApplication! (%u != %u)", __func__, written, total);
        goto out;
    }
    
    if (titleIndex >= total)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: provided title index exceeds title count from ncmContentMetaDatabaseListApplication!", __func__);
        goto out;
    }
    
    result = ncmContentMetaDatabaseGet(&ncmDb, &(titleList[titleIndex].key), &cnmtHeaderReadSize, &cnmtHeader, sizeof(NcmContentMetaHeader));
    if (R_FAILED(result))
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: ncmContentMetaDatabaseGet failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    titleContentInfoCnt = (u32)(cnmtHeader.content_count);
    
    titleContentInfos = calloc(titleContentInfoCnt, sizeof(NcmContentInfo));
    if (!titleContentInfos)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: unable to allocate memory for the title content information struct!", __func__);
        goto out;
    }
    
    written = 0;
    
    result = ncmContentMetaDatabaseListContentInfo(&ncmDb, (s32*)&written, titleContentInfos, (s32)titleContentInfoCnt, &(titleList[titleIndex].key), 0);
    if (R_FAILED(result))
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: ncmContentMetaDatabaseListContentInfo failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    if (written != titleContentInfoCnt)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: title content count mismatch in ncmContentMetaDatabaseListContentInfo! (%u != %u)", __func__, written, titleContentInfoCnt);
        goto out;
    }
    
    success = true;
    
    // Update output parameters
    *outContentInfos = titleContentInfos;
    *outContentInfoCnt = titleContentInfoCnt;
    
out:
    if (!success && titleContentInfos) free(titleContentInfos);
    
    ncmContentMetaDatabaseClose(&ncmDb);
    
    if (titleList) free(titleList);
    
    return success;
}

void removeConsoleDataFromTicket(title_rights_ctx *rights_info)
{
    if (!rights_info || !rights_info->has_rights_id || !rights_info->retrieved_tik || rights_info->missing_tik || rights_info->tik_data.titlekey_type != ETICKET_TITLEKEY_PERSONALIZED) return;
    
    memset(rights_info->tik_data.signature, 0xFF, 0x100);
    
    memset(rights_info->tik_data.sig_issuer, 0, 0x40);
    sprintf(rights_info->tik_data.sig_issuer, "Root-CA00000003-XS00000020");
    
    memset(rights_info->tik_data.titlekey_block, 0, 0x100);
    memcpy(rights_info->tik_data.titlekey_block, rights_info->enc_titlekey, 0x10);
    
    rights_info->tik_data.titlekey_type = ETICKET_TITLEKEY_COMMON;
    rights_info->tik_data.ticket_id = 0;
    rights_info->tik_data.device_id = 0;
    rights_info->tik_data.account_id = 0;
}

bool listDesiredNcaType(NcmContentInfo *titleContentInfos, u32 titleContentInfoCnt, u8 type, int desiredIdOffset, u32 *outIndex, u32 *outCount)
{
    if (!titleContentInfos || !titleContentInfoCnt || type > NcmContentType_DeltaFragment || !outIndex || !outCount)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters.", __func__);
        return false;
    }
    
    int idx = -1;
    u32 i, cnt = 0;
    bool success = false;
    u32 *indexes = NULL, *tmpIndexes = NULL;
    
    int cur_breaks, initial_breaks = breaks;
    u32 selectedContent = 0;
    u64 keysDown = 0, keysHeld = 0;
    
    char nca_id[SHA256_HASH_SIZE + 1] = {'\0'};
    
    for(i = 0; i < titleContentInfoCnt; i++)
    {
        if (titleContentInfos[i].content_type == type)
        {
            if (desiredIdOffset >= 0)
            {
                if (titleContentInfos[i].id_offset == (u8)desiredIdOffset)
                {
                    // Save the index for the content with the desired ID offset
                    idx = (int)i;
                }
            } else {
                tmpIndexes = realloc(indexes, (cnt + 1) * sizeof(u32));
                if (!tmpIndexes)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to reallocate indexes buffer!", __func__);
                    goto out;
                }
                
                indexes = tmpIndexes;
                tmpIndexes = NULL;
                
                indexes[cnt] = i;
            }
            
            cnt++;
        }
    }
    
    if (!cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find any %s NCAs!", __func__, getContentType(type));
        goto out;
    }
    
    if (desiredIdOffset >= 0)
    {
        if (idx < 0)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find %s NCA with ID offset %d!", __func__, getContentType(type), desiredIdOffset);
            goto out;
        }
    } else {
        // If only a single NCA with the desired content type was detected, save its index right away
        if (cnt == 1) idx = (int)indexes[0];
    }
    
    // Return immediately if necessary
    if (idx >= 0)
    {
        success = true;
        goto out;
    }
    
    // Display a selection list
    breaks++;
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Select one of the available %s NCAs from the list below:", getContentType(type));
    breaks += 2;
    
    while(true)
    {
        cur_breaks = breaks;
        
        uiFill(0, 8 + (cur_breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (cur_breaks * LINE_HEIGHT)), BG_COLOR_RGB);
        
        for(i = 0; i < cnt; i++)
        {
            u32 xpos = STRING_X_POS;
            u32 ypos = (8 + (cur_breaks * LINE_HEIGHT) + (i * (font_height + 12)) + 6);
            
            if (i == selectedContent)
            {
                highlight = true;
                uiFill(0, ypos - 6, FB_WIDTH, font_height + 12, HIGHLIGHT_BG_COLOR_RGB);
            }
            
            uiDrawIcon((highlight ? fileHighlightIconBuf : fileNormalIconBuf), BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, xpos, ypos);
            xpos += (BROWSER_ICON_DIMENSION + 8);
            
            convertDataToHexString(titleContentInfos[indexes[i]].content_id.c, SHA256_HASH_SIZE / 2, nca_id, SHA256_HASH_SIZE + 1);
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s.nca (ID Offset: %u)", nca_id, titleContentInfos[indexes[i]].id_offset);
            
            if (highlight)
            {
                uiDrawString(xpos, ypos, HIGHLIGHT_FONT_COLOR_RGB, strbuf);
            } else {
                uiDrawString(xpos, ypos, FONT_COLOR_RGB, strbuf);
            }
            
            if (i == selectedContent) highlight = false;
        }
        
        while(true)
        {
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            hidScanInput();
            
            keysDown = hidKeysAllDown(CONTROLLER_P1_AUTO);
            keysHeld = hidKeysAllHeld(CONTROLLER_P1_AUTO);
            
            if ((keysDown && !(keysDown & KEY_TOUCH)) || (keysHeld && !(keysHeld & KEY_TOUCH))) break;
        }
        
        if (keysDown & KEY_A)
        {
            idx = (int)indexes[selectedContent];
            break;
        }
        
        if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
        {
            if (selectedContent > 0) selectedContent--;
        }
        
        if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
        {
            if (selectedContent < (cnt - 1)) selectedContent++;
        }
    }
    
    breaks = initial_breaks;
    uiFill(0, 8 + (breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (breaks * LINE_HEIGHT)), BG_COLOR_RGB);
    uiRefreshDisplay();
    
    success = true;
    
out:
    if (indexes) free(indexes);
    
    if (success)
    {
        *outIndex = (u32)idx;
        *outCount = cnt;
    }
    
    return success;
}

bool readNcaExeFsSection(u32 titleIndex, bool usePatch)
{
    u32 i = 0;
    Result result;
    
    NcmStorageId curStorageId = NcmStorageId_None;
    NcmContentMetaType metaType;
    u32 titleCount = 0, ncmTitleIndex = 0;
    
    NcmContentInfo *titleContentInfos = NULL;
    u32 titleContentInfoCnt = 0;
    
    u32 contentIndex = 0, desiredNcaTypeCount = 0;
    
    NcmContentId ncaId;
    char ncaIdStr[SHA256_HASH_SIZE + 1] = {'\0'};
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    u8 ncaHeader[NCA_FULL_HEADER_LENGTH] = {0};
    nca_header_t dec_nca_header;
    
    title_rights_ctx rights_info;
    memset(&rights_info, 0, sizeof(title_rights_ctx));
    
    u8 decrypted_nca_keys[NCA_KEY_AREA_SIZE];
    
    bool success = false;
    
    if ((!usePatch && !baseAppEntries) || (usePatch && !patchEntries))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: title storage ID unavailable!", __func__);
        goto out;
    }
    
    if ((!usePatch && titleIndex >= titleAppCount) || (usePatch && titleIndex >= titlePatchCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title index!", __func__);
        goto out;
    }
    
    curStorageId = (!usePatch ? baseAppEntries[titleIndex].storageId : patchEntries[titleIndex].storageId);
    
    ncmTitleIndex = (!usePatch ? baseAppEntries[titleIndex].ncmIndex : patchEntries[titleIndex].ncmIndex);
    
    metaType = (!usePatch ? NcmContentMetaType_Application : NcmContentMetaType_Patch);
    
    switch(curStorageId)
    {
        case NcmStorageId_GameCard:
            titleCount = (!usePatch ? titleAppCount : titlePatchCount);
            break;
        case NcmStorageId_SdCard:
            titleCount = (!usePatch ? sdCardTitleAppCount : sdCardTitlePatchCount);
            break;
        case NcmStorageId_BuiltInUser:
            titleCount = (!usePatch ? emmcTitleAppCount : emmcTitlePatchCount);
            break;
        default:
            break;
    }
    
    // If we're dealing with a gamecard, open the Secure HFS0 partition (IStorage partition #1)
    if (curStorageId == NcmStorageId_GameCard)
    {
        result = openGameCardStoragePartition(ISTORAGE_PARTITION_SECURE);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open IStorage partition #1! (0x%08X)", __func__, result);
            goto out;
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Looking for the Program NCA (%s)...", (!usePatch ? "base application" : "update"));
    uiRefreshDisplay();
    breaks++;
    
    if (!retrieveContentInfosFromTitle(curStorageId, metaType, titleCount, ncmTitleIndex, &titleContentInfos, &titleContentInfoCnt))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
        goto out;
    }
    
    if (!listDesiredNcaType(titleContentInfos, titleContentInfoCnt, NcmContentType_Program, -1, &contentIndex, &desiredNcaTypeCount)) goto out;
    
    memcpy(&ncaId, &(titleContentInfos[contentIndex].content_id), sizeof(NcmContentId));
    convertDataToHexString(titleContentInfos[contentIndex].content_id.c, SHA256_HASH_SIZE / 2, ncaIdStr, SHA256_HASH_SIZE + 1);
    
    if (desiredNcaTypeCount == 1)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Found Program NCA: \"%s.nca\".", ncaIdStr);
        uiRefreshDisplay();
        breaks += 2;
    }
    
    /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Retrieving ExeFS entries...");
    uiRefreshDisplay();
    breaks++;*/
    
    result = ncmOpenContentStorage(&ncmStorage, curStorageId);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: ncmOpenContentStorage failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    if (!readNcaDataByContentId(&ncmStorage, &ncaId, 0, ncaHeader, NCA_FULL_HEADER_LENGTH))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read header from Program NCA!", __func__);
        goto out;
    }
    
    // Decrypt the NCA header
    if (!decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header, &rights_info, decrypted_nca_keys, (curStorageId != NcmStorageId_GameCard || (curStorageId == NcmStorageId_GameCard && usePatch)))) goto out;
    
    if (curStorageId == NcmStorageId_GameCard)
    {
        bool has_rights_id = false;
        
        for(i = 0; i < 0x10; i++)
        {
            if (dec_nca_header.rights_id[i] != 0)
            {
                has_rights_id = true;
                break;
            }
        }
        
        if (has_rights_id)
        {
            if (usePatch)
            {
                // Retrieve the ticket from the HFS0 partition in the gamecard
                if (!retrieveTitleKeyFromGameCardTicket(&rights_info, decrypted_nca_keys)) goto out;
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: Rights ID field in Program NCA header not empty!", __func__);
                goto out;
            }
        }
    }
    
    // Read file entries from the ExeFS section
    success = parseExeFsEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys);
    if (success)
    {
        exeFsContext.storageId = curStorageId;
        exeFsContext.idOffset = titleContentInfos[contentIndex].id_offset;
    }
    
out:
    if (!success)
    {
        ncmContentStorageClose(&ncmStorage);
        if (curStorageId == NcmStorageId_GameCard) closeGameCardStoragePartition();
    }
    
    if (titleContentInfos) free(titleContentInfos);
    
    return success;
}

bool readNcaRomFsSection(u32 titleIndex, selectedRomFsType curRomFsType, int desiredIdOffset)
{
    u32 i = 0;
    Result result;
    
    NcmStorageId curStorageId = NcmStorageId_None;
    NcmContentMetaType metaType;
    u32 titleCount = 0, ncmTitleIndex = 0;
    
    NcmContentInfo *titleContentInfos = NULL;
    u32 titleContentInfoCnt = 0;
    
    u32 contentIndex = 0, desiredNcaTypeCount = 0;
    
    NcmContentId ncaId;
    char ncaIdStr[SHA256_HASH_SIZE + 1] = {'\0'};
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    u8 ncaHeader[NCA_FULL_HEADER_LENGTH] = {0};
    nca_header_t dec_nca_header;
    
    title_rights_ctx rights_info;
    memset(&rights_info, 0, sizeof(title_rights_ctx));
    
    u8 decrypted_nca_keys[NCA_KEY_AREA_SIZE];
    
    bool success = false;
    
    if (curRomFsType != ROMFS_TYPE_APP && curRomFsType != ROMFS_TYPE_PATCH && curRomFsType != ROMFS_TYPE_ADDON)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid RomFS title type!", __func__);
        goto out;
    }
    
    if ((curRomFsType == ROMFS_TYPE_APP && !baseAppEntries) || (curRomFsType == ROMFS_TYPE_PATCH && !patchEntries) || (curRomFsType == ROMFS_TYPE_ADDON && !addOnEntries))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: title storage ID unavailable!", __func__);
        goto out;
    }
    
    if ((curRomFsType == ROMFS_TYPE_APP && titleIndex >= titleAppCount) || (curRomFsType == ROMFS_TYPE_PATCH && titleIndex >= titlePatchCount) || (curRomFsType == ROMFS_TYPE_ADDON && titleIndex >= titleAddOnCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title index!", __func__);
        goto out;
    }
    
    curStorageId = (curRomFsType == ROMFS_TYPE_APP ? baseAppEntries[titleIndex].storageId : (curRomFsType == ROMFS_TYPE_PATCH ? patchEntries[titleIndex].storageId : addOnEntries[titleIndex].storageId));
    
    ncmTitleIndex = (curRomFsType == ROMFS_TYPE_APP ? baseAppEntries[titleIndex].ncmIndex : (curRomFsType == ROMFS_TYPE_PATCH ? patchEntries[titleIndex].ncmIndex : addOnEntries[titleIndex].ncmIndex));
    
    metaType = (curRomFsType == ROMFS_TYPE_APP ? NcmContentMetaType_Application : (curRomFsType == ROMFS_TYPE_PATCH ? NcmContentMetaType_Patch : NcmContentMetaType_AddOnContent));
    
    switch(curStorageId)
    {
        case NcmStorageId_GameCard:
            titleCount = (curRomFsType == ROMFS_TYPE_APP ? titleAppCount : (curRomFsType == ROMFS_TYPE_PATCH ? titlePatchCount : titleAddOnCount));
            break;
        case NcmStorageId_SdCard:
            titleCount = (curRomFsType == ROMFS_TYPE_APP ? sdCardTitleAppCount : (curRomFsType == ROMFS_TYPE_PATCH ? sdCardTitlePatchCount : sdCardTitleAddOnCount));
            break;
        case NcmStorageId_BuiltInUser:
            titleCount = (curRomFsType == ROMFS_TYPE_APP ? emmcTitleAppCount : (curRomFsType == ROMFS_TYPE_PATCH ? emmcTitlePatchCount : emmcTitleAddOnCount));
            break;
        default:
            break;
    }
    
    // If we're dealing with a gamecard, open the Secure HFS0 partition (IStorage partition #1)
    if (curStorageId == NcmStorageId_GameCard)
    {
        result = openGameCardStoragePartition(ISTORAGE_PARTITION_SECURE);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open IStorage partition #1! (0x%08X)", __func__, result);
            goto out;
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Looking for the %s NCA (%s)...", (curRomFsType == ROMFS_TYPE_ADDON ? "Data" : "Program"), (curRomFsType == ROMFS_TYPE_APP ? "base application" : (curRomFsType == ROMFS_TYPE_PATCH ? "update" : "DLC")));
    uiRefreshDisplay();
    breaks++;
    
    if (!retrieveContentInfosFromTitle(curStorageId, metaType, titleCount, ncmTitleIndex, &titleContentInfos, &titleContentInfoCnt))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
        goto out;
    }
    
    if (!listDesiredNcaType(titleContentInfos, titleContentInfoCnt, (curRomFsType == ROMFS_TYPE_ADDON ? NcmContentType_Data : NcmContentType_Program), desiredIdOffset, &contentIndex, &desiredNcaTypeCount)) goto out;
    
    memcpy(&ncaId, &(titleContentInfos[contentIndex].content_id), sizeof(NcmContentId));
    convertDataToHexString(titleContentInfos[contentIndex].content_id.c, SHA256_HASH_SIZE / 2, ncaIdStr, SHA256_HASH_SIZE + 1);
    
    if (desiredNcaTypeCount == 1)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Found %s NCA: \"%s.nca\".", (curRomFsType == ROMFS_TYPE_ADDON ? "Data" : "Program"), ncaIdStr);
        uiRefreshDisplay();
        breaks += 2;
    }
    
    /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Retrieving RomFS entry tables...");
    uiRefreshDisplay();
    breaks++;*/
    
    result = ncmOpenContentStorage(&ncmStorage, curStorageId);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: ncmOpenContentStorage failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    if (!readNcaDataByContentId(&ncmStorage, &ncaId, 0, ncaHeader, NCA_FULL_HEADER_LENGTH))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read header from %s NCA!", __func__, (curRomFsType == ROMFS_TYPE_ADDON ? "Data" : "Program"));
        goto out;
    }
    
    // Decrypt the NCA header
    if (!decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header, &rights_info, decrypted_nca_keys, (curStorageId != NcmStorageId_GameCard || (curStorageId == NcmStorageId_GameCard && curRomFsType == ROMFS_TYPE_PATCH)))) goto out;
    
    if (curStorageId == NcmStorageId_GameCard)
    {
        bool has_rights_id = false;
        
        for(i = 0; i < 0x10; i++)
        {
            if (dec_nca_header.rights_id[i] != 0)
            {
                has_rights_id = true;
                break;
            }
        }
        
        if (has_rights_id)
        {
            if (curRomFsType == ROMFS_TYPE_PATCH)
            {
                // Retrieve the ticket from the HFS0 partition in the gamecard
                if (!retrieveTitleKeyFromGameCardTicket(&rights_info, decrypted_nca_keys)) goto out;
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: Rights ID field in %s NCA header not empty!", __func__, (curRomFsType == ROMFS_TYPE_ADDON ? "Data" : "Program"));
                goto out;
            }
        }
    }
    
    if (curRomFsType != ROMFS_TYPE_PATCH)
    {
        // Read directory and file tables from the RomFS section
        success = parseRomFsEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys);
        if (success)
        {
            romFsContext.storageId = curStorageId;
            romFsContext.idOffset = titleContentInfos[contentIndex].id_offset;
        }
    } else {
        // Look for the base application title index
        u32 appIndex;
        
        for(i = 0; i < titleAppCount; i++)
        {
            if (checkIfPatchOrAddOnBelongsToBaseApplication(titleIndex, i, false))
            {
                appIndex = i;
                break;
            }
        }
        
        if (i >= titleAppCount)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find base application title index for the selected update!", __func__);
            goto out;
        }
        
        // Read directory and file tables from the RomFS section in the Program NCA from the base application
        if (!readNcaRomFsSection(appIndex, ROMFS_TYPE_APP, (int)titleContentInfos[contentIndex].id_offset)) goto out;
        
        // Read BKTR entry data in the Program NCA from the update
        success = parseBktrEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys);
        if (success)
        {
            bktrContext.storageId = curStorageId;
            bktrContext.idOffset = titleContentInfos[contentIndex].id_offset;
        }
    }
    
out:
    if (!success)
    {
        ncmContentStorageClose(&ncmStorage);
        if (curStorageId == NcmStorageId_GameCard) closeGameCardStoragePartition();
    }
    
    if (titleContentInfos) free(titleContentInfos);
    
    return success;
}

bool getExeFsFileList()
{
    if (!exeFsContext.exefs_header.file_cnt || !exeFsContext.exefs_entries || !exeFsContext.exefs_str_table)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve ExeFS section filelist!", __func__);
        return false;
    }
    
    u32 i;
    char curName[NAME_BUF_LEN] = {'\0'};
    
    freeHfs0ExeFsEntriesSizes();
    
    hfs0ExeFsEntriesSizes = calloc(exeFsContext.exefs_header.file_cnt, sizeof(browser_entry_size_info));
    if (!hfs0ExeFsEntriesSizes)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for ExeFS entries size info!", __func__);
        return false;
    }
    
    if (!allocateFilenameBuffer(exeFsContext.exefs_header.file_cnt))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for the filename buffer!", __func__);
        return false;
    }
    
    for(i = 0; i < exeFsContext.exefs_header.file_cnt; i++)
    {
        char *cur_filename = (exeFsContext.exefs_str_table + exeFsContext.exefs_entries[i].filename_offset);
        
        snprintf(curName, MAX_CHARACTERS(curName), cur_filename);
        
        // Fix entry name length
        truncateBrowserEntryName(curName);
        
        if (!addStringToFilenameBuffer(curName))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for filename entry in filename buffer!", __func__);
            freeHfs0ExeFsEntriesSizes();
            return false;
        }
        
        // Save entry size
        hfs0ExeFsEntriesSizes[i].size = exeFsContext.exefs_entries[i].file_size;
        convertSize(hfs0ExeFsEntriesSizes[i].size, hfs0ExeFsEntriesSizes[i].sizeStr, MAX_CHARACTERS(hfs0ExeFsEntriesSizes[i].sizeStr));
    }
    
    return true;
}

bool getRomFsParentDir(u32 dir_offset, bool usePatch, u32 *out)
{
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve parent RomFS section directory!", __func__);
        return false;
    }
    
    romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
    
    *out = entry->parent;
    
    return true;
}

bool generateCurrentRomFsPath(u32 dir_offset, bool usePatch)
{
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to generate current RomFS section path!", __func__);
        return false;
    }
    
    // Generate current path if we're not dealing with the root directory
    if (dir_offset)
    {
        romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
        
        if (!entry->nameLen)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: directory entry without name in RomFS section!", __func__);
            return false;
        }
        
        // Check if we're not a root dir child
        if (entry->parent)
        {
            if (!generateCurrentRomFsPath(entry->parent, usePatch)) return false;
        }
        
        // Concatenate entry name
        strcat(curRomFsPath, "/");
        strncat(curRomFsPath, (char*)entry->name, entry->nameLen);
    } else {
        strcat(curRomFsPath, "/");
    }
    
    return true;
}

bool getRomFsFileList(u32 dir_offset, bool usePatch)
{
    u64 entryOffset = 0;
    u32 dirEntryCnt = 1; // Always add the parent directory entry ("..")
    u32 fileEntryCnt = 0;
    u32 totalEntryCnt = 0;
    u32 i = 1;
    u32 romFsParentDir = 0;
    
    u64 dirTableSize;
    u64 fileTableSize;
    
    freeRomFsBrowserEntries();
    
    memset(curRomFsPath, 0, NAME_BUF_LEN);
    
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries || !bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve RomFS section filelist!", __func__);
        return false;
    }
    
    if (!getRomFsParentDir(dir_offset, usePatch, &romFsParentDir)) return false;
    
    if (!generateCurrentRomFsPath(dir_offset, usePatch)) return false;
    
    dirTableSize = (!usePatch ? romFsContext.romfs_dirtable_size : bktrContext.romfs_dirtable_size);
    fileTableSize = (!usePatch ? romFsContext.romfs_filetable_size : bktrContext.romfs_filetable_size);
    
    // First count the directory entries
    entryOffset = ROMFS_NONAME_DIRENTRY_SIZE; // Always skip the first entry (root directory)
    
    while(entryOffset < dirTableSize)
    {
        romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + entryOffset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + entryOffset));
        
        if (!entry->nameLen)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: directory entry without name in RomFS section!", __func__);
            return false;
        }
        
        // Only add entries inside the directory we're looking in
        if (entry->parent == dir_offset) dirEntryCnt++;
        
        entryOffset += round_up(ROMFS_NONAME_DIRENTRY_SIZE + entry->nameLen, 4);
    }
    
    // Now count the file entries
    entryOffset = 0;
    
    while(entryOffset < fileTableSize)
    {
        romfs_file *entry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + entryOffset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + entryOffset));
        
        if (!entry->nameLen)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: file entry without name in RomFS section!", __func__);
            return false;
        }
        
        // Only add entries inside the directory we're looking in
        if (entry->parent == dir_offset) fileEntryCnt++;
        
        entryOffset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
    }
    
    totalEntryCnt = (dirEntryCnt + fileEntryCnt);
    
    char curName[NAME_BUF_LEN] = {'\0'};
    
    // Silently return true if we're dealing with an empty directory
    if (!totalEntryCnt) goto out;
    
    // Allocate memory for our entries
    romFsBrowserEntries = calloc(totalEntryCnt, sizeof(romfs_browser_entry));
    if (!romFsBrowserEntries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for file/dir attributes in RomFS section!", __func__);
        return false;
    }
    
    if (!allocateFilenameBuffer(totalEntryCnt))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for the filename buffer!", __func__);
        freeRomFsBrowserEntries();
        return false;
    }
    
    if (!addStringToFilenameBuffer(".."))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for parent dir entry in filename buffer!", __func__);
        freeRomFsBrowserEntries();
        return false;
    }
    
    // Add parent directory entry ("..")
    romFsBrowserEntries[0].type = ROMFS_ENTRY_DIR;
    romFsBrowserEntries[0].offset = romFsParentDir;
    
    // First add the directory entries
    if ((!romFsParentDir && dirEntryCnt > 1) || (romFsParentDir && dirEntryCnt > 0))
    {
        entryOffset = ROMFS_NONAME_DIRENTRY_SIZE; // Always skip the first entry (root directory)
        
        while(entryOffset < dirTableSize)
        {
            romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + entryOffset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + entryOffset));
            
            // Only add entries inside the directory we're looking in
            if (entry->parent == dir_offset)
            {
                romFsBrowserEntries[i].type = ROMFS_ENTRY_DIR;
                romFsBrowserEntries[i].offset = entryOffset;
                
                snprintf(curName, entry->nameLen + 1, (char*)entry->name);
                
                // Fix entry name length
                truncateBrowserEntryName(curName);
                
                if (!addStringToFilenameBuffer(curName))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for filename entry in filename buffer!", __func__);
                    freeRomFsBrowserEntries();
                    return false;
                }
                
                i++;
            }
            
            entryOffset += round_up(ROMFS_NONAME_DIRENTRY_SIZE + entry->nameLen, 4);
        }
    }
    
    // Now add the file entries
    if (fileEntryCnt > 0)
    {
        entryOffset = 0;
        
        while(entryOffset < fileTableSize)
        {
            romfs_file *entry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + entryOffset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + entryOffset));
            
            // Only add entries inside the directory we're looking in
            if (entry->parent == dir_offset)
            {
                romFsBrowserEntries[i].type = ROMFS_ENTRY_FILE;
                romFsBrowserEntries[i].offset = entryOffset;
                romFsBrowserEntries[i].sizeInfo.size = entry->dataSize;
                convertSize(entry->dataSize, romFsBrowserEntries[i].sizeInfo.sizeStr, MAX_CHARACTERS(romFsBrowserEntries[i].sizeInfo.sizeStr));
                
                snprintf(curName, entry->nameLen + 1, (char*)entry->name);
                
                // Fix entry name length
                truncateBrowserEntryName(curName);
                
                if (!addStringToFilenameBuffer(curName))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for filename entry in filename buffer!", __func__);
                    freeRomFsBrowserEntries();
                    return false;
                }
                
                i++;
            }
            
            entryOffset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
        }
    }
    
out:
    // Update current RomFS directory offset
    curRomFsDirOffset = dir_offset;
    
    return true;
}

char *generateGameCardDumpName(bool useBrackets)
{
    if (menuType != MENUTYPE_GAMECARD || !titleAppCount || !baseAppEntries) return NULL;
    
    u32 i, j;
    
    char tmp[NAME_BUF_LEN / 2] = {'\0'};
    char *fullname = NULL;
    char *fullnameTmp = NULL;
    
    size_t strsize = NAME_BUF_LEN;
    
    fullname = calloc(strsize + 1, sizeof(char));
    if (!fullname) return NULL;
    
    for(i = 0; i < titleAppCount; i++)
    {
        u32 highestVersion = baseAppEntries[i].version;
        
        // Check if our current gamecard has any bundled updates for this application. If so, use the highest update version available
        if (titlePatchCount && patchEntries)
        {
            for(j = 0; j < titlePatchCount; j++)
            {
                if (checkIfPatchOrAddOnBelongsToBaseApplication(j, i, false) && patchEntries[j].version > highestVersion) highestVersion = patchEntries[j].version;
            }
        }
        
        if (useBrackets)
        {
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s [%016lX][v%u]", baseAppEntries[i].fixedName, baseAppEntries[i].titleId, highestVersion);
        } else {
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s v%u (%016lX)", baseAppEntries[i].fixedName, highestVersion, baseAppEntries[i].titleId);
        }
        
        if ((strlen(fullname) + strlen(tmp) + 4) > strsize)
        {
            size_t fullname_len = strlen(fullname);
            
            strsize = (fullname_len + strlen(tmp) + 4);
            
            fullnameTmp = realloc(fullname, strsize);
            if (fullnameTmp)
            {
                fullname = fullnameTmp;
                fullnameTmp = NULL;
                memset(fullname + fullname_len, 0, strlen(tmp) + 4);
            } else {
                free(fullname);
                fullname = NULL;
                break;
            }
        }
        
        if (i > 0) strcat(fullname, " + ");
        
        strcat(fullname, tmp);
    }
    
    return fullname;
}

char *generateNSPDumpName(nspDumpType selectedNspDumpType, u32 titleIndex, bool useBrackets)
{
    if ((selectedNspDumpType == DUMP_APP_NSP && (!titleAppCount || !baseAppEntries || titleIndex >= titleAppCount)) || (selectedNspDumpType == DUMP_PATCH_NSP && (!titlePatchCount || !patchEntries || titleIndex >= titlePatchCount)) || (selectedNspDumpType == DUMP_ADDON_NSP && (!titleAddOnCount || !addOnEntries || titleIndex >= titleAddOnCount))) return NULL;
    
    u32 i;
    size_t strsize = NAME_BUF_LEN;
    
    patch_addon_ctx_t *ptr = NULL;
    
    char *fullname = calloc(strsize + 1, sizeof(char));
    if (!fullname) return NULL;
    
    switch(selectedNspDumpType)
    {
        case DUMP_APP_NSP:
            if (useBrackets)
            {
                snprintf(fullname, strsize, "%s [%016lX][v%u][BASE]", baseAppEntries[titleIndex].fixedName, baseAppEntries[titleIndex].titleId, baseAppEntries[titleIndex].version);
            } else {
                snprintf(fullname, strsize, "%s v%u (%016lX) (BASE)", baseAppEntries[titleIndex].fixedName, baseAppEntries[titleIndex].version, baseAppEntries[titleIndex].titleId);
            }
            break;
        case DUMP_PATCH_NSP:
        case DUMP_ADDON_NSP:
            ptr = (selectedNspDumpType == DUMP_PATCH_NSP ? &(patchEntries[titleIndex]) : &(addOnEntries[titleIndex]));
            
            // Look for the parent base application name
            if (titleAppCount && baseAppEntries)
            {
                for(i = 0; i < titleAppCount; i++)
                {
                    if (checkIfPatchOrAddOnBelongsToBaseApplication(titleIndex, i, (selectedNspDumpType == DUMP_ADDON_NSP)))
                    {
                        if (useBrackets)
                        {
                            snprintf(fullname, strsize, "%s [%016lX][v%u][%s]", baseAppEntries[i].fixedName, ptr->titleId, ptr->version, (selectedNspDumpType == DUMP_PATCH_NSP ? "UPD" : "DLC"));
                        } else {
                            snprintf(fullname, strsize, "%s v%u (%016lX) (%s)", baseAppEntries[i].fixedName, ptr->version, ptr->titleId, (selectedNspDumpType == DUMP_PATCH_NSP ? "UPD" : "DLC"));
                        }
                        
                        break;
                    }
                }
            }
            
            if (!strlen(fullname))
            {
                // Look for the parent base application name in orphan entries
                if (orphanEntries && orphanEntriesCnt)
                {
                    for(i = 0; i < orphanEntriesCnt; i++)
                    {
                        if (orphanEntries[i].index == titleIndex && strlen(orphanEntries[i].fixedName) && ((selectedNspDumpType == DUMP_PATCH_NSP && orphanEntries[i].type == ORPHAN_ENTRY_TYPE_PATCH) || (selectedNspDumpType == DUMP_ADDON_NSP && orphanEntries[i].type == ORPHAN_ENTRY_TYPE_ADDON)))
                        {
                            if (useBrackets)
                            {
                                snprintf(fullname, strsize, "%s [%016lX][v%u][%s]", orphanEntries[i].fixedName, ptr->titleId, ptr->version, (selectedNspDumpType == DUMP_PATCH_NSP ? "UPD" : "DLC"));
                            } else {
                                snprintf(fullname, strsize, "%s v%u (%016lX) (%s)", orphanEntries[i].fixedName, ptr->version, ptr->titleId, (selectedNspDumpType == DUMP_PATCH_NSP ? "UPD" : "DLC"));
                            }
                            
                            break;
                        }
                    }
                }
                
                if (!strlen(fullname))
                {
                    // Nothing worked, just print the Title ID + version
                    if (useBrackets)
                    {
                        snprintf(fullname, strsize, "[%016lX][v%u][%s]", ptr->titleId, ptr->version, (selectedNspDumpType == DUMP_PATCH_NSP ? "UPD" : "DLC"));
                    } else {
                        snprintf(fullname, strsize, "%016lX v%u (%s)", ptr->titleId, ptr->version, (selectedNspDumpType == DUMP_PATCH_NSP ? "UPD" : "DLC"));
                    }
                }
            }
            
            break;
        default:
            free(fullname);
            fullname = NULL;
            break;
    }
    
    return fullname;
}

void retrieveDescriptionForPatchOrAddOn(u32 titleIndex, bool addOn, bool addAppName, const char *prefix, char *outBuf, size_t outBufSize)
{
    if ((!addOn && (!titlePatchCount || !patchEntries || titleIndex >= titlePatchCount)) || (addOn && (!titleAddOnCount || !addOnEntries || titleIndex >= titleAddOnCount)) || !outBuf || !outBufSize) return;
    
    u32 i;
    bool addPrefix = (prefix && strlen(prefix));
    patch_addon_ctx_t *ptr = (!addOn ? &(patchEntries[titleIndex]) : &(addOnEntries[titleIndex]));
    
    // Check if we need to add the base application name
    if (!addAppName || !titleAppCount || !baseAppEntries)
    {
        if (addPrefix)
        {
            snprintf(outBuf, outBufSize, "%s%016lX v%s", prefix, ptr->titleId, ptr->versionStr);
        } else {
            snprintf(outBuf, outBufSize, "%016lX v%s", ptr->titleId, ptr->versionStr);
        }
        
        return;
    }
    
    // Look for the parent base application name
    for(i = 0; i < titleAppCount; i++)
    {
        if (checkIfPatchOrAddOnBelongsToBaseApplication(titleIndex, i, addOn))
        {
            if (addPrefix)
            {
                snprintf(outBuf, outBufSize, "%s%s | %016lX v%s", prefix, baseAppEntries[i].name, ptr->titleId, ptr->versionStr);
            } else {
                snprintf(outBuf, outBufSize, "%s | %016lX v%s", baseAppEntries[i].name, ptr->titleId, ptr->versionStr);
            }
            
            return;
        }
    }
    
    // Look for the parent base application name in orphan entries
    if (orphanEntries != NULL && orphanEntriesCnt)
    {
        for(i = 0; i < orphanEntriesCnt; i++)
        {
            if (orphanEntries[i].index == titleIndex && strlen(orphanEntries[i].name) && ((!addOn && orphanEntries[i].type == ORPHAN_ENTRY_TYPE_PATCH) || (addOn && orphanEntries[i].type == ORPHAN_ENTRY_TYPE_ADDON)))
            {
                if (addPrefix)
                {
                    snprintf(outBuf, outBufSize, "%s%s | %016lX v%s", prefix, orphanEntries[i].name, ptr->titleId, ptr->versionStr);
                } else {
                    snprintf(outBuf, outBufSize, "%s | %016lX v%s", orphanEntries[i].name, ptr->titleId, ptr->versionStr);
                }
                
                return;
            }
        }
    }
    
    // Nothing worked, just print the Title ID + version
    if (addPrefix)
    {
        snprintf(outBuf, outBufSize, "%s%016lX v%s", prefix, ptr->titleId, ptr->versionStr);
    } else {
        snprintf(outBuf, outBufSize, "%016lX v%s", ptr->titleId, ptr->versionStr);
    }
}

u32 calculateOrphanPatchOrAddOnCount(bool addOn)
{
    if ((!addOn && (!titlePatchCount || !patchEntries)) || (addOn && (!titleAddOnCount || !addOnEntries))) return 0;
    
    if ((!titleAppCount || !baseAppEntries) && ((!addOn && titlePatchCount && patchEntries) || (addOn && titleAddOnCount && addOnEntries))) return (!addOn ? titlePatchCount : titleAddOnCount);
    
    u32 i, j;
    u32 titleCount = (!addOn ? titlePatchCount : titleAddOnCount);
    u32 orphanCnt = 0;
    
    for(i = 0; i < titleCount; i++)
    {
        bool foundMatch = false;
        
        for(j = 0; j < titleAppCount; j++)
        {
            if ((!addOn && patchEntries[i].titleId == (baseAppEntries[j].titleId | APPLICATION_PATCH_BITMASK)) || (addOn && (addOnEntries[i].titleId & APPLICATION_ADDON_BITMASK) == (baseAppEntries[j].titleId & APPLICATION_ADDON_BITMASK)))
            {
                foundMatch = true;
                break;
            }
        }
        
        if (foundMatch) continue;
        
        orphanCnt++;
    }
    
    return orphanCnt;
}

void generateOrphanPatchOrAddOnList()
{
    Result result;
    u32 nsAppRecordCnt = 0;
    
    bool foundMatch;
    u32 i, j, k;
    
    u32 orphanEntryIndex = 0;
    u32 orphanPatchCount = calculateOrphanPatchOrAddOnCount(false);
    u32 orphanAddOnCount = calculateOrphanPatchOrAddOnCount(true);
    
    if (!orphanPatchCount && !orphanAddOnCount) return;
    
    if (orphanEntries && orphanEntriesCnt && orphanEntriesCnt == (orphanPatchCount + orphanAddOnCount)) goto out;
    
    freeOrphanPatchOrAddOnList();
    
    // Retrieve all cached Application IDs (assuming no one has more than 2048 cached base applications...)
    NsApplicationRecord *appRecords = calloc(2048, sizeof(NsApplicationRecord));
    if (!appRecords) return;
    
    result = nsListApplicationRecord(appRecords, 2048, 0, (s32*)&nsAppRecordCnt);
    if (R_FAILED(result))
    {
        free(appRecords);
        return;
    }
    
    // Allocate memory for our orphan entries
    orphanEntries = calloc(orphanPatchCount + orphanAddOnCount, sizeof(orphan_patch_addon_entry));
    if (!orphanEntries)
    {
        free(appRecords);
        return;
    }
    
    // Save orphan patch & add-on data
    for(i = 0; i < 2; i++)
    {
        u32 titleCount = (i == 0 ? titlePatchCount : titleAddOnCount);
        
        for(j = 0; j < titleCount; j++)
        {
            foundMatch = false;
            
            if (titleAppCount && baseAppEntries)
            {
                for(k = 0; k < titleAppCount; k++)
                {
                    if (checkIfPatchOrAddOnBelongsToBaseApplication(j, k, (i == 1)))
                    {
                        foundMatch = true;
                        break;
                    }
                }
            }
            
            if (foundMatch) continue;
            
            patch_addon_ctx_t *ptr = (i == 0 ? &(patchEntries[j]) : &(addOnEntries[j]));
            
            // Look for a matching Application ID in our NS records
            for(k = 0; k < nsAppRecordCnt; k++)
            {
                if ((i == 0 && ptr->titleId == (appRecords[k].application_id | APPLICATION_PATCH_BITMASK)) || (i == 1 && (ptr->titleId & APPLICATION_ADDON_BITMASK) == (appRecords[k].application_id & APPLICATION_ADDON_BITMASK)))
                {
                    if (getCachedBaseApplicationNacpMetadata(appRecords[k].application_id, orphanEntries[orphanEntryIndex].name, MAX_CHARACTERS(orphanEntries[orphanEntryIndex].name), NULL, 0, NULL))
                    {
                        strtrim(orphanEntries[orphanEntryIndex].name);
                        snprintf(orphanEntries[orphanEntryIndex].fixedName, MAX_CHARACTERS(orphanEntries[orphanEntryIndex].fixedName), orphanEntries[orphanEntryIndex].name);
                        removeIllegalCharacters(orphanEntries[orphanEntryIndex].fixedName);
                    }
                    
                    break;
                }
            }
            
            if (strlen(orphanEntries[orphanEntryIndex].name))
            {
                snprintf(orphanEntries[orphanEntryIndex].orphanListStr, MAX_CHARACTERS(orphanEntries[orphanEntryIndex].orphanListStr), "%s v%u (%016lX) (%s)", orphanEntries[orphanEntryIndex].name, ptr->version, ptr->titleId, (i == 0 ? "Update" : "DLC"));
            } else {
                snprintf(orphanEntries[orphanEntryIndex].orphanListStr, MAX_CHARACTERS(orphanEntries[orphanEntryIndex].orphanListStr), "%016lX v%u (%s)", ptr->titleId, ptr->version, (i == 0 ? "Update" : "DLC"));
            }
            
            orphanEntries[orphanEntryIndex].index = j;
            orphanEntries[orphanEntryIndex].type = (i == 0 ? ORPHAN_ENTRY_TYPE_PATCH : ORPHAN_ENTRY_TYPE_ADDON);
            
            orphanEntryIndex++;
        }
    }
    
    orphanEntriesCnt = (orphanPatchCount + orphanAddOnCount);
    
    free(appRecords);
    
    // Sort orphan titles by name
    qsort(orphanEntries, orphanEntriesCnt, sizeof(orphan_patch_addon_entry), orphanEntryCmp);
    
out:
    if (!allocateFilenameBuffer(orphanEntriesCnt)) return;
    
    for(i = 0; i < orphanEntriesCnt; i++)
    {
        if (!addStringToFilenameBuffer(orphanEntries[i].orphanListStr)) return;
    }
}

bool checkIfBaseApplicationHasPatchOrAddOn(u32 appIndex, bool addOn)
{
    if (!titleAppCount || !baseAppEntries || appIndex >= titleAppCount || (!addOn && (!titlePatchCount || !patchEntries)) || (addOn && (!titleAddOnCount || !addOnEntries))) return false;
    
    u32 i;
    u32 count = (!addOn ? titlePatchCount : titleAddOnCount);
    
    for(i = 0; i < count; i++)
    {
        if ((!addOn && (baseAppEntries[appIndex].titleId | APPLICATION_PATCH_BITMASK) == patchEntries[i].titleId) || (addOn && (baseAppEntries[appIndex].titleId & APPLICATION_ADDON_BITMASK) == (addOnEntries[i].titleId & APPLICATION_ADDON_BITMASK))) return true;
    }
    
    return false;
}

bool checkIfPatchOrAddOnBelongsToBaseApplication(u32 titleIndex, u32 appIndex, bool addOn)
{
    if (!titleAppCount || !baseAppEntries || appIndex >= titleAppCount || (!addOn && (!titlePatchCount || !patchEntries || titleIndex >= titlePatchCount)) || (addOn && (!titleAddOnCount || !addOnEntries || titleIndex >= titleAddOnCount))) return false;
    
    if ((!addOn && patchEntries[titleIndex].titleId == (baseAppEntries[appIndex].titleId | APPLICATION_PATCH_BITMASK)) || (addOn && (addOnEntries[titleIndex].titleId & APPLICATION_ADDON_BITMASK) == (baseAppEntries[appIndex].titleId & APPLICATION_ADDON_BITMASK))) return true;
    
    return false;
}

u32 retrieveFirstPatchOrAddOnIndexFromBaseApplication(u32 appIndex, bool addOn)
{
    if (!titleAppCount || !baseAppEntries || appIndex >= titleAppCount || (!addOn && (!titlePatchCount || !patchEntries)) || (addOn && (!titleAddOnCount || !addOnEntries))) return 0;
    
    u32 titleIndex;
    u32 count = (!addOn ? titlePatchCount : titleAddOnCount);
    
    for(titleIndex = 0; titleIndex < count; titleIndex++)
    {
        if (checkIfPatchOrAddOnBelongsToBaseApplication(titleIndex, appIndex, addOn)) return titleIndex;
    }
    
    return 0;
}

u32 retrievePreviousPatchOrAddOnIndexFromBaseApplication(u32 startTitleIndex, u32 appIndex, bool addOn)
{
    u32 count = (!addOn ? titlePatchCount : titleAddOnCount);
    u32 retTitleIndex = startTitleIndex;
    u32 curTitleIndex = 0;
    
    if (!startTitleIndex || startTitleIndex >= count || !titleAppCount || !baseAppEntries || appIndex >= titleAppCount || (!addOn && (!titlePatchCount || !patchEntries)) || (addOn && (!titleAddOnCount || !addOnEntries))) return retTitleIndex;
    
    for(curTitleIndex = startTitleIndex; curTitleIndex > 0; curTitleIndex--)
    {
        if (checkIfPatchOrAddOnBelongsToBaseApplication(curTitleIndex - 1, appIndex, addOn))
        {
            retTitleIndex = (curTitleIndex - 1);
            break;
        }
    }
    
    return retTitleIndex;
}

u32 retrieveNextPatchOrAddOnIndexFromBaseApplication(u32 startTitleIndex, u32 appIndex, bool addOn)
{
    u32 count = (!addOn ? titlePatchCount : titleAddOnCount);
    u32 retTitleIndex = startTitleIndex;
    u32 curTitleIndex = 0;
    
    if (startTitleIndex >= count || !titleAppCount || !baseAppEntries || appIndex >= titleAppCount || (!addOn && (!titlePatchCount || !patchEntries)) || (addOn && (!titleAddOnCount || !addOnEntries))) return retTitleIndex;
    
    for(curTitleIndex = (startTitleIndex + 1); curTitleIndex < count; curTitleIndex++)
    {
        if (checkIfPatchOrAddOnBelongsToBaseApplication(curTitleIndex, appIndex, addOn))
        {
            retTitleIndex = curTitleIndex;
            break;
        }
    }
    
    return retTitleIndex;
}

u32 retrieveLastPatchOrAddOnIndexFromBaseApplication(u32 appIndex, bool addOn)
{
    if (!titleAppCount || !baseAppEntries || appIndex >= titleAppCount || (!addOn && (!titlePatchCount || !patchEntries)) || (addOn && (!titleAddOnCount || !addOnEntries))) return 0;
    
    u32 titleIndex;
    u32 count = (!addOn ? titlePatchCount : titleAddOnCount);
    
    for(titleIndex = count; titleIndex > 0; titleIndex--)
    {
        if (checkIfPatchOrAddOnBelongsToBaseApplication(titleIndex - 1, appIndex, addOn)) return (titleIndex - 1);
    }
    
    return 0;
}

void waitForButtonPress()
{
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Press any button to continue");
    
    while(true)
    {
        uiUpdateStatusMsg();
        uiRefreshDisplay();
        
        hidScanInput();
        
        u64 keysDown = hidKeysAllDown(CONTROLLER_P1_AUTO);
        
        if (keysDown && !((keysDown & KEY_TOUCH) || (keysDown & KEY_LSTICK_LEFT) || (keysDown & KEY_LSTICK_RIGHT) || (keysDown & KEY_LSTICK_UP) || (keysDown & KEY_LSTICK_DOWN) || \
            (keysDown & KEY_RSTICK_LEFT) || (keysDown & KEY_RSTICK_RIGHT) || (keysDown & KEY_RSTICK_UP) || (keysDown & KEY_RSTICK_DOWN))) break;
    }
}

void printProgressBar(progress_ctx_t *progressCtx, bool calcData, u64 chunkSize)
{
    if (!progressCtx) return;
    
    if (calcData)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx->now));
        
        // Workaround to properly calculate speed for sequential dumps
        u64 speedCurOffset = (progressCtx->seqDumpCurOffset ? progressCtx->seqDumpCurOffset : progressCtx->curOffset);
        
        progressCtx->lastSpeed = (((double)(speedCurOffset + chunkSize) / (double)MiB) / (double)(progressCtx->now - progressCtx->start));
        progressCtx->averageSpeed = ((SMOOTHING_FACTOR * progressCtx->lastSpeed) + ((1 - SMOOTHING_FACTOR) * progressCtx->averageSpeed));
        if (!isnormal(progressCtx->averageSpeed)) progressCtx->averageSpeed = SMOOTHING_FACTOR; // Very low values
        
        progressCtx->remainingTime = (u64)(((double)(progressCtx->totalSize - (progressCtx->curOffset + chunkSize)) / (double)MiB) / progressCtx->averageSpeed);
        
        progressCtx->progress = (u8)(((progressCtx->curOffset + chunkSize) * 100) / progressCtx->totalSize);
    }
    
    formatETAString(progressCtx->remainingTime, progressCtx->etaInfo, MAX_CHARACTERS(progressCtx->etaInfo));
    
    convertSize(progressCtx->curOffset + chunkSize, progressCtx->curOffsetStr, MAX_CHARACTERS(progressCtx->curOffsetStr));
    
    uiFill(0, (progressCtx->line_offset * LINE_HEIGHT) + 8, FB_WIDTH / 4, LINE_HEIGHT * 2, BG_COLOR_RGB);
    uiDrawString(font_height * 2, STRING_Y_POS(progressCtx->line_offset), FONT_COLOR_RGB, "%.2lf MiB/s [ETA: %s]", progressCtx->averageSpeed, progressCtx->etaInfo);
    
    if (progressCtx->totalSize && (progressCtx->curOffset + chunkSize) < progressCtx->totalSize)
    {
        uiFill(FB_WIDTH / 4, (progressCtx->line_offset * LINE_HEIGHT) + 10, FB_WIDTH / 2, LINE_HEIGHT, EMPTY_BAR_COLOR_RGB);
        uiFill(FB_WIDTH / 4, (progressCtx->line_offset * LINE_HEIGHT) + 10, (((progressCtx->curOffset + chunkSize) * (u64)(FB_WIDTH / 2)) / progressCtx->totalSize), LINE_HEIGHT, FONT_COLOR_SUCCESS_RGB);
    } else {
        uiFill(FB_WIDTH / 4, (progressCtx->line_offset * LINE_HEIGHT) + 10, FB_WIDTH / 2, LINE_HEIGHT, FONT_COLOR_SUCCESS_RGB);
    }
    
    uiFill(FB_WIDTH - (FB_WIDTH / 4), (progressCtx->line_offset * LINE_HEIGHT) + 8, FB_WIDTH / 4, LINE_HEIGHT * 2, BG_COLOR_RGB);
    uiDrawString(FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), STRING_Y_POS(progressCtx->line_offset), FONT_COLOR_RGB, "%u%% [%s / %s]", progressCtx->progress, progressCtx->curOffsetStr, progressCtx->totalSizeStr);
    
    uiRefreshDisplay();
    uiUpdateStatusMsg();
}

void setProgressBarError(progress_ctx_t *progressCtx)
{
    if (!progressCtx) return;
    
    if (progressCtx->totalSize && progressCtx->curOffset < progressCtx->totalSize)
    {
        uiFill(FB_WIDTH / 4, (progressCtx->line_offset * LINE_HEIGHT) + 10, FB_WIDTH / 2, LINE_HEIGHT, EMPTY_BAR_COLOR_RGB);
        uiFill(FB_WIDTH / 4, (progressCtx->line_offset * LINE_HEIGHT) + 10, ((progressCtx->curOffset * (u64)(FB_WIDTH / 2)) / progressCtx->totalSize), LINE_HEIGHT, FONT_COLOR_ERROR_RGB);
    } else {
        uiFill(FB_WIDTH / 4, (progressCtx->line_offset * LINE_HEIGHT) + 10, FB_WIDTH / 2, LINE_HEIGHT, FONT_COLOR_ERROR_RGB);
    }
}

bool cancelProcessCheck(progress_ctx_t *progressCtx)
{
    if (!progressCtx) return false;
    
    hidScanInput();
    
    progressCtx->cancelBtnState = (hidKeysAllHeld(CONTROLLER_P1_AUTO) & KEY_B);
    
    if (progressCtx->cancelBtnState && progressCtx->cancelBtnState != progressCtx->cancelBtnStatePrev)
    {
        // Cancel button has just been pressed
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx->cancelStartTmr));
    } else
    if (progressCtx->cancelBtnState && progressCtx->cancelBtnState == progressCtx->cancelBtnStatePrev && progressCtx->cancelStartTmr)
    {
        // If the cancel button has been held up to this point, check if at least CANCEL_BTN_SEC_HOLD seconds have passed
        // Only perform this check if cancelStartTmr has already been set to a value greater than zero
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx->cancelEndTmr));
        
        if ((progressCtx->cancelEndTmr - progressCtx->cancelStartTmr) >= CANCEL_BTN_SEC_HOLD) return true;
    } else {
        progressCtx->cancelStartTmr = progressCtx->cancelEndTmr = 0;
    }
    
    progressCtx->cancelBtnStatePrev = progressCtx->cancelBtnState;
    
    return false;
}

void convertDataToHexString(const u8 *data, const u32 dataSize, char *outBuf, const u32 outBufSize)
{
    if (!data || !dataSize || !outBuf || !outBufSize || outBufSize < ((dataSize * 2) + 1)) return;
    
    u32 i;
    char tmp[3] = {'\0'};
    
    memset(outBuf, 0, outBufSize);
    
    for(i = 0; i < dataSize; i++)
    {
        sprintf(tmp, "%02x", data[i]);
        strcat(outBuf, tmp);
    }
}

bool checkIfFileExists(const char *path)
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

bool yesNoPrompt(const char *message)
{
    if (message && strlen(message))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, message);
        breaks++;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "[ %s ] Yes | [ %s ] No", NINTENDO_FONT_A, NINTENDO_FONT_B);
    breaks += 2;
    
    bool ret = false;
    
    while(true)
    {
        uiUpdateStatusMsg();
        uiRefreshDisplay();
        
        hidScanInput();
        
        u64 keysDown = hidKeysAllDown(CONTROLLER_P1_AUTO);
        
        if (keysDown & KEY_A)
        {
            ret = true;
            break;
        } else
        if (keysDown & KEY_B)
        {
            ret = false;
            break;
        }
    }
    
    return ret;
}

bool checkIfDumpedXciContainsCertificate(const char *xciPath)
{
    if (!xciPath || !strlen(xciPath)) return false;
    
    FILE *xciFile = NULL;
    u64 xciSize = 0;
    
    size_t read_bytes;
    
    u8 xci_cert[CERT_SIZE];
    
    u8 xci_cert_wiped[CERT_SIZE];
    memset(xci_cert_wiped, 0xFF, CERT_SIZE);
    
    xciFile = fopen(xciPath, "rb");
    if (!xciFile) return false;
    
    fseek(xciFile, 0, SEEK_END);
    xciSize = ftell(xciFile);
    rewind(xciFile);
    
    if (xciSize < (size_t)(CERT_OFFSET + CERT_SIZE))
    {
        fclose(xciFile);
        return false;
    }
    
    fseek(xciFile, CERT_OFFSET, SEEK_SET);
    
    read_bytes = fread(xci_cert, 1, CERT_SIZE, xciFile);
    
    fclose(xciFile);
    
    if (read_bytes != (size_t)CERT_SIZE) return false;
    
    if (memcmp(xci_cert, xci_cert_wiped, CERT_SIZE) != 0) return true;
    
    return false;
}

bool checkIfDumpedNspContainsConsoleData(const char *nspPath)
{
    if (!nspPath || !strlen(nspPath)) return false;
    
    FILE *nspFile = NULL;
    u64 nspSize = 0;
    
    size_t read_bytes;
    pfs0_header nspHeader;
    pfs0_file_entry *nspEntries = NULL;
    char *nspStrTable = NULL;
    
    u32 i;
    bool foundTik = false;
    u64 tikOffset = 0, tikSize = 0;
    rsa2048_sha256_ticket tikData;
    
    const u8 titlekey_block_0x190_empty_hash[0x20] = {
        0x2D, 0xFB, 0xA6, 0x33, 0x81, 0x70, 0x46, 0xC7, 0xF5, 0x59, 0xED, 0x4B, 0x93, 0x07, 0x60, 0x48,
        0x43, 0x5F, 0x7E, 0x1A, 0x90, 0xF1, 0x4E, 0xB8, 0x03, 0x5C, 0x04, 0xB9, 0xEB, 0xAE, 0x25, 0x37
    };
    
    u8 titlekey_block_0x190_hash[0x20];
    
    nspFile = fopen(nspPath, "rb");
    if (!nspFile) return false;
    
    fseek(nspFile, 0, SEEK_END);
    nspSize = ftell(nspFile);
    rewind(nspFile);
    
    if (nspSize < sizeof(pfs0_header))
    {
        fclose(nspFile);
        return false;
    }
    
    read_bytes = fread(&nspHeader, 1, sizeof(pfs0_header), nspFile);
    
    if (read_bytes != sizeof(pfs0_header) || __builtin_bswap32(nspHeader.magic) != PFS0_MAGIC || nspSize < (sizeof(pfs0_header) + (sizeof(pfs0_file_entry) * (u64)nspHeader.file_cnt) + (u64)nspHeader.str_table_size))
    {
        fclose(nspFile);
        return false;
    }
    
    nspEntries = calloc((u64)nspHeader.file_cnt, sizeof(pfs0_file_entry));
    if (!nspEntries)
    {
        fclose(nspFile);
        return false;
    }
    
    read_bytes = fread(nspEntries, 1, sizeof(pfs0_file_entry) * (u64)nspHeader.file_cnt, nspFile);
    
    if (read_bytes != (sizeof(pfs0_file_entry) * (u64)nspHeader.file_cnt))
    {
        free(nspEntries);
        fclose(nspFile);
        return false;
    }
    
    nspStrTable = calloc((u64)nspHeader.str_table_size, sizeof(char));
    if (!nspStrTable)
    {
        free(nspEntries);
        fclose(nspFile);
        return false;
    }
    
    read_bytes = fread(nspStrTable, 1, (u64)nspHeader.str_table_size, nspFile);
    
    if (read_bytes != (u64)nspHeader.str_table_size)
    {
        free(nspStrTable);
        free(nspEntries);
        fclose(nspFile);
        return false;
    }
    
    for(i = 0; i < nspHeader.file_cnt; i++)
    {
        char *curFilename = (nspStrTable + nspEntries[i].filename_offset);
        
        if (!strncasecmp(curFilename + strlen(curFilename) - 4, ".tik", 4))
        {
            tikOffset = (sizeof(pfs0_header) + (sizeof(pfs0_file_entry) * (u64)nspHeader.file_cnt) + (u64)nspHeader.str_table_size + nspEntries[i].file_offset);
            tikSize = nspEntries[i].file_size;
            foundTik = true;
            break;
        }
    }
    
    free(nspStrTable);
    free(nspEntries);
    
    if (!foundTik || tikSize != ETICKET_TIK_FILE_SIZE || nspSize < (tikOffset + tikSize))
    {
        fclose(nspFile);
        return false;
    }
    
    fseek(nspFile, tikOffset, SEEK_SET);
    
    read_bytes = fread(&tikData, 1, ETICKET_TIK_FILE_SIZE, nspFile);
    
    fclose(nspFile);
    
    if (read_bytes != ETICKET_TIK_FILE_SIZE) return false;
    
    sha256CalculateHash(titlekey_block_0x190_hash, tikData.titlekey_block + 0x10, 0xF0);
    
    if (strncmp(tikData.sig_issuer, "Root-CA00000003-XS00000020", 26) != 0 || memcmp(titlekey_block_0x190_hash, titlekey_block_0x190_empty_hash, 0x20) != 0 || tikData.titlekey_type != ETICKET_TITLEKEY_COMMON || tikData.ticket_id != 0 || tikData.device_id != 0 || tikData.account_id != 0) return true;
    
    return false;
}

void removeDirectoryWithVerbose(const char *path, const char *msg)
{
    if (!path || !strlen(path) || !msg || !strlen(msg)) return;
    
    int initial_breaks = breaks;
    
    breaks += 2;
    
    if (yesNoPrompt("Do you wish to delete the data dumped up to this point? This may take a while."))
    {
        breaks = initial_breaks;
        uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        
        breaks += 2;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, msg);
        uiRefreshDisplay();
        
        fsdevDeleteDirectoryRecursively(path);
    }
    
    breaks = initial_breaks;
    uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
    uiRefreshDisplay();
}

static bool parseNSWDBRelease(xmlDocPtr doc, xmlNodePtr cur, u32 crc)
{
    if (!doc || !cur) return false;
    
    xmlChar *key = NULL;
    xmlNodePtr node = cur;
    
    u32 xmlCrc = 0;
    char xmlReleaseName[256] = {'\0'};
    
    bool found = false;
    
    while(node)
    {
        if ((!xmlStrcmp(node->name, (const xmlChar*)NSWDB_XML_CHILD_IMGCRC)))
        {
            key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (key) xmlCrc = strtoul((const char*)key, NULL, 16);
        } else
        if ((!xmlStrcmp(node->name, (const xmlChar*)NSWDB_XML_CHILD_RELEASENAME)))
        {
            key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (key) snprintf(xmlReleaseName, MAX_CHARACTERS(xmlReleaseName), "%s", (const char*)key);
        }
        
        if (key)
        {
            xmlFree(key);
            key = NULL;
        }
        
        node = node->next;
    }
    
    if (strlen(xmlReleaseName) && xmlCrc == crc)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Found matching Scene release: \"%s\" (CRC32: %08X). This is likely a good dump!", xmlReleaseName, xmlCrc);
        found = true;
    }
    
    return found;
}

static xmlXPathObjectPtr getXPathNodeSet(xmlDocPtr doc, char *xpathExpr)
{
    if (!doc || !xpathExpr || !strlen(xpathExpr)) return NULL;
    
    xmlXPathContextPtr context = NULL;
    xmlXPathObjectPtr result = NULL;
    
    context = xmlXPathNewContext(doc);
    if (!context) return NULL;
    
    result = xmlXPathEvalExpression((xmlChar*)xpathExpr, context);
    
    xmlXPathFreeContext(context);
    
    if (!result) return NULL;
    
    if (xmlXPathNodeSetIsEmpty(result->nodesetval))
    {
        xmlXPathFreeObject(result);
        return NULL;
    }
    
    return result;
}

void gameCardDumpNSWDBCheck(u32 crc)
{
    if (menuType != MENUTYPE_GAMECARD || !titleAppCount || !baseAppEntries || !gameCardInfo.hfs0PartitionCnt) return;
    
    u32 i, j;
    xmlDocPtr doc = NULL;
    bool found = false;
    
    doc = xmlParseFile(NSWDB_XML_PATH);
    if (!doc)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open and/or parse \"%s\"!", __func__, NSWDB_XML_PATH);
        return;
    }
    
    for(i = 0; i < titleAppCount; i++)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "//%s/%s[.//%s[contains(.,'%016lX')]]", NSWDB_XML_ROOT, NSWDB_XML_CHILD, NSWDB_XML_CHILD_TITLEID, baseAppEntries[i].titleId);
        
        xmlXPathObjectPtr nodeSet = getXPathNodeSet(doc, strbuf);
        if (!nodeSet) continue;
        
        for(j = 0; j < (u32)nodeSet->nodesetval->nodeNr; j++)
        {
            xmlNodePtr node = nodeSet->nodesetval->nodeTab[j]->xmlChildrenNode;
            found = parseNSWDBRelease(doc, node, crc);
            if (found) break;
        }
        
        xmlXPathFreeObject(nodeSet);
        
        if (found) break;
    }
    
    xmlFreeDoc(doc);
    
    if (!found) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "No match found in NSWDB.COM XML database! This could either be a bad dump or an undumped gamecard.");
}

static Result networkInit()
{
    if (initNet) return 0;
    
    Result result = socketInitializeDefault();
    if (R_SUCCEEDED(result))
    {
        curl_global_init(CURL_GLOBAL_ALL);
        initNet = true;
    }
    
    return result;
}

static void networkExit()
{
    if (!initNet) return;
    
    curl_global_cleanup();
    socketExit();
    initNet = false;
}

static size_t writeCurlFile(char *buffer, size_t size, size_t number_of_items, void *input_stream)
{
    size_t total_size = (size * number_of_items);
    if (fwrite(buffer, 1, total_size, input_stream) != total_size) return 0;
    return total_size;
}

static size_t writeCurlBuffer(char *buffer, size_t size, size_t number_of_items, void *input_stream)
{
    (void) input_stream;
    const size_t bsz = (size * number_of_items);
    
    if (result_sz == 0 || !result_buf)
    {
        result_sz = 0x1000;
        result_buf = malloc(result_sz);
        if (!result_buf) return 0;
    }
    
    bool need_realloc = false;
    
    while((result_written + bsz) > result_sz) 
    {
        result_sz <<= 1;
        need_realloc = true;
    }
    
    if (need_realloc)
    {
        char *new_buf = realloc(result_buf, result_sz);
        if (!new_buf) return 0;
        result_buf = new_buf;
    }
    
    memcpy(result_buf + result_written, buffer, bsz);
    result_written += bsz;
    return bsz;
}

static bool performCurlRequest(CURL *curl, const char *url, FILE *filePtr, bool forceHttps, bool verbose)
{
    if (!curl || !url || !strlen(url))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to perform CURL request!", __func__);
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, HTTP_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
    if (forceHttps) curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
    
    if (filePtr)
    {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCurlFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, filePtr);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCurlBuffer);
    }
    
    CURLcode res;
    long http_code = 0;
    double size = 0.0;
    bool success = false;
    
    res = curl_easy_perform(curl);
    
    result_sz = result_written = 0;
    
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &size);
    
    if (res == CURLE_OK && http_code >= 200 && http_code <= 299 && size > 0)
    {
        if (verbose) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Successfully downloaded %.0lf bytes!", size);
        success = true;
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: CURL request failed for \"%s\" endpoint!\nHTTP status code: %ld", __func__, url, http_code);
    }
    
    return success;
}

void noIntroDumpCheck(bool isDigital, u32 crc)
{
    Result result;
    CURL *curl = NULL;
    char noIntroUrl[128] = {'\0'};
    
    // Build URL
    // f = "cart" (XCI) or "dlc" (NSP)
    // c = search by code (Title ID or serial)
    // crc = search by CRC32 checksum
    snprintf(noIntroUrl, MAX_CHARACTERS(noIntroUrl), "%s?f=%s&crc=%08X", NOINTRO_DOM_CHECK_URL, (isDigital ? "dlc" : "cart"), crc);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Performing CRC32 checksum lookup against No-Intro, please wait...");
    uiRefreshDisplay();
    breaks++;
    
    result = networkInit();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize socket! (%08X)", __func__, result);
        goto out;
    }
    
    curl = curl_easy_init();
    if (!curl)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize CURL context!", __func__);
        goto out;
    }
    
    if (!performCurlRequest(curl, noIntroUrl, NULL, true, false)) goto out;
    
    if (!strlen(result_buf) || !strncmp(result_buf, "unknown crc32", 13))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "No match found in No-Intro database! This could either be a bad dump or an undumped %s.", (isDigital ? "digital title" : "gamecard"));
        goto out;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Found matching No-Intro database entry: \"%s\". This is likely a good dump!", result_buf);
    
out:
    if (result_buf)
    {
        free(result_buf);
        result_buf = NULL;
    }
    
    if (curl) curl_easy_cleanup(curl);
    
    if (R_SUCCEEDED(result)) networkExit();
}

void updateNSWDBXml()
{
    Result result;
    CURL *curl = NULL;
    bool success = false;
    FILE *nswdbXml = NULL;
    
    result = networkInit();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize socket! (%08X)", __func__, result);
        goto out;
    }
    
    char xmlPath[256] = {'\0'};
    snprintf(xmlPath, MAX_CHARACTERS(xmlPath), "%s.tmp", NSWDB_XML_PATH);
    
    curl = curl_easy_init();
    if (!curl)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize CURL context!", __func__);
        goto out;
    }
    
    nswdbXml = fopen(xmlPath, "wb");
    if (!nswdbXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open \"%s\" in write mode!", __func__, NSWDB_XML_URL);
        goto out;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Downloading XML database from \"%s\", please wait...", NSWDB_XML_URL);
    breaks++;
    
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    success = performCurlRequest(curl, NSWDB_XML_URL, nswdbXml, false, true);
    
    changeHomeButtonBlockStatus(false);
    
out:
    if (nswdbXml) fclose(nswdbXml);
    
    if (success)
    {
        remove(NSWDB_XML_PATH);
        rename(xmlPath, NSWDB_XML_PATH);
    } else {
        remove(xmlPath);
    }
    
    if (curl) curl_easy_cleanup(curl);
    
    if (R_SUCCEEDED(result)) networkExit();
    
    breaks += 2;
}

static int versionNumCmp(char *ver1, char *ver2)
{
    int i, curPart, res;
    char *token = NULL;
    
    // Define a struct for comparison purposes
    typedef struct {
        int major;
        int minor;
        int build;
    } version_t;
    
    version_t versionNum1, versionNum2;
    memset(&versionNum1, 0, sizeof(version_t));
    memset(&versionNum2, 0, sizeof(version_t));
    
    // Create copies of the version strings to avoid modifications by strtok()
    char ver1tok[64] = {'\0'};
    snprintf(ver1tok, 63, ver1);
    
    char ver2tok[64] = {'\0'};
    snprintf(ver2tok, 63, ver2);
    
    // Parse version string 1
    i = 0;
    token = strtok(ver1tok, ".");
    while(token != NULL && i < 3)
    {
        curPart = atoi(token);
        
        switch(i)
        {
            case 0:
                versionNum1.major = curPart;
                break;
            case 1:
                versionNum1.minor = curPart;
                break;
            case 2:
                versionNum1.build = curPart;
                break;
            default:
                break;
        }
        
        token = strtok(NULL, ".");
        
        i++;
    }
    
    // Parse version string 2
    i = 0;
    token = strtok(ver2tok, ".");
    while(token != NULL && i < 3)
    {
        curPart = atoi(token);
        
        switch(i)
        {
            case 0:
                versionNum2.major = curPart;
                break;
            case 1:
                versionNum2.minor = curPart;
                break;
            case 2:
                versionNum2.build = curPart;
                break;
            default:
                break;
        }
        
        token = strtok(NULL, ".");
        
        i++;
    }
    
    // Compare version_t structs
    if (versionNum1.major == versionNum2.major)
    {
        if (versionNum1.minor == versionNum2.minor)
        {
            if (versionNum1.build == versionNum2.build)
            {
                res = 0;
            } else
            if (versionNum1.build < versionNum2.build)
            {
                res = -1;
            } else {
                res = 1;
            }
        } else
        if (versionNum1.minor < versionNum2.minor)
        {
            res = -1;
        } else {
            res = 1;
        }
    } else
    if (versionNum1.major < versionNum2.major)
    {
        res = -1;
    } else {
        res = 1;
    }
    
    return res;
}

static struct json_object *retrieveJsonObjMemberByNameAndType(struct json_object *jobj, char *memberName, json_type memberType)
{
    if (!jobj || !memberName || !strlen(memberName))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve member by name and type from JSON object!", __func__);
        return NULL;
    }
    
    struct json_object *memberObj = NULL;
    json_type memberObjType;
    
    if (!json_object_object_get_ex(jobj, memberName, &memberObj))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to retrieve member \"%s\" from JSON object!", __func__, memberName);
        return NULL;
    }
    
    memberObjType = json_object_get_type(memberObj);
    if (memberObjType != memberType)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid type for member \"%s\" in JSON object! (got \"%s\", expected \"%s\")", __func__, memberName, json_type_to_name(memberObjType), json_type_to_name(memberType));
        return NULL;
    }
    
    return memberObj;
}

static const char *retrieveJsonObjStrMemberContentsByName(struct json_object *jobj, char *memberName)
{
    if (!jobj || !memberName || !strlen(memberName))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve string member contents by name from JSON object!", __func__);
        return NULL;
    }
    
    struct json_object *memberObj = retrieveJsonObjMemberByNameAndType(jobj, memberName, json_type_string);
    if (!memberObj) return NULL;
    
    const char *memberObjStr = json_object_get_string(memberObj);
    if (!memberObjStr || !strlen(memberObjStr))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: string member \"%s\" from JSON object is empty!", __func__, memberName);
        return NULL;
    }
    
    return memberObjStr;
}

static struct json_object *retrieveJsonObjArrayMemberByName(struct json_object *jobj, char *memberName, size_t *outputArrayLength)
{
    if (!jobj || !memberName || !strlen(memberName) || !outputArrayLength)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve array member by name from JSON object!", __func__);
        return NULL;
    }
    
    struct json_object *memberObj = retrieveJsonObjMemberByNameAndType(jobj, memberName, json_type_array);
    if (memberObj) *outputArrayLength = json_object_array_length(memberObj);
    
    return memberObj;
}

static struct json_object *retrieveJsonObjArrayElementByIndex(struct json_object *jobj, size_t idx)
{
    if (!jobj || json_object_get_type(jobj) != json_type_array)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve element by index from JSON array object!", __func__);
        return NULL;
    }
    
    struct json_object *memberObjArrayElement = json_object_array_get_idx(jobj, idx);
    if (!memberObjArrayElement) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to retrieve element at index %lu from JSON array object!", __func__, idx);
    
    return memberObjArrayElement;
}

bool updateApplication()
{
    if (envIsNso())
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to update application. Not running as a NRO.", __func__);
        breaks += 2;
        return false;
    }
    
    Result result;
    CURL *curl = NULL;
    FILE *nxDumpToolNro = NULL;
    
    char releaseTag[32] = {'\0'};
    bool success = false;
    
    size_t i, assetsCnt = 0;
    struct json_object *jobj = NULL, *assets = NULL;
    const char *releaseNameObjStr = NULL, *dlUrlObjStr = NULL;
    
    char nroPath[NAME_BUF_LEN] = {'\0'};
    snprintf(nroPath, MAX_CHARACTERS(nroPath), "%s.tmp", (appLaunchPath ? appLaunchPath : NRO_PATH));
    
    result = networkInit();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize socket! (%08X)", __func__, result);
        goto out;
    }
    
    curl = curl_easy_init();
    if (!curl)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize CURL context!", __func__);
        goto out;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Requesting latest release information from \"%s\"...", GITHUB_API_URL);
    breaks++;
    
    uiRefreshDisplay();
    
    if (!performCurlRequest(curl, GITHUB_API_URL, NULL, true, false)) goto out;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Parsing response JSON data...");
    breaks++;
    
    uiRefreshDisplay();
    
    jobj = json_tokener_parse(result_buf);
    if (!jobj)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to parse JSON response!", __func__);
        goto out;
    }
    
    releaseNameObjStr = retrieveJsonObjStrMemberContentsByName(jobj, GITHUB_API_JSON_RELEASE_NAME);
    if (!releaseNameObjStr) goto out;
    
    snprintf(releaseTag, MAX_CHARACTERS(releaseTag), releaseNameObjStr);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Latest release: %s.", releaseTag);
    breaks++;
    
    uiRefreshDisplay();
    
    // Remove the first character from the release name if it's v/V/r/R
    if (releaseTag[0] == 'v' || releaseTag[0] == 'V' || releaseTag[0] == 'r' || releaseTag[0] == 'R')
    {
        u32 releaseTagLen = strlen(releaseTag);
        memmove(releaseTag, releaseTag + 1, releaseTagLen - 1);
        releaseTag[releaseTagLen - 1] = '\0';
    }
    
    // Compare versions
    if (versionNumCmp(releaseTag, APP_VERSION) <= 0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "You already have the latest version!");
        breaks += 2;
        
        // Ask the user if they want to perform a forced update
        int cur_breaks = breaks;
        
        if (yesNoPrompt("Do you want to perform a forced update?"))
        {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
            uiRefreshDisplay();
        } else {
            breaks -= 2;
            goto out;
        }
    }
    
    assets = retrieveJsonObjArrayMemberByName(jobj, GITHUB_API_JSON_ASSETS, &assetsCnt);
    if (!assets) goto out;
    
    // Cycle through the assets to find the right download URL
    for(i = 0; i < assetsCnt; i++)
    {
        struct json_object *assetElement = retrieveJsonObjArrayElementByIndex(assets, i);
        if (!assetElement) break;
        
        const char *assetName = retrieveJsonObjStrMemberContentsByName(assetElement, GITHUB_API_JSON_ASSETS_NAME);
        if (!assetName) break;
        
        if (!strncmp(assetName, NRO_NAME, strlen(assetName)))
        {
            // Found it
            dlUrlObjStr = retrieveJsonObjStrMemberContentsByName(assetElement, GITHUB_API_JSON_ASSETS_DL_URL);
            break;
        }
    }
    
    if (!dlUrlObjStr)
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to locate NRO download URL!", __func__);
        goto out;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Download URL: \"%s\".", dlUrlObjStr);
    breaks++;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Please wait...");
    breaks++;
    
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    nxDumpToolNro = fopen(nroPath, "wb");
    if (!nxDumpToolNro)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open \"%s\" in write mode!", __func__, nroPath);
        goto out;
    }
    
    curl_easy_reset(curl);
    
    success = performCurlRequest(curl, dlUrlObjStr, nxDumpToolNro, true, true);
    if (!success) goto out;
    
    breaks++;
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Please restart the application to reflect the changes.");
    
out:
    if (nxDumpToolNro) fclose(nxDumpToolNro);
    
    if (strlen(nroPath))
    {
        if (success)
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), nroPath);
            nroPath[strlen(nroPath) - 4] = '\0';
            
            remove(nroPath);
            rename(strbuf, nroPath);
        } else {
            remove(nroPath);
        }
    }
    
    if (jobj) json_object_put(jobj);
    
    if (result_buf)
    {
        free(result_buf);
        result_buf = NULL;
    }
    
    if (curl) curl_easy_cleanup(curl);
    
    if (R_SUCCEEDED(result)) networkExit();
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}
