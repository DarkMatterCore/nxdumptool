#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
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

FsStorage fatFsStorage = {0};
static FATFS *fatFsObj = NULL;

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

void appletModeOperationWarning()
{
    if (!appletModeCheck()) return;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
    breaks++;
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

int readNcaRomFsSection(u32 titleIndex, selectedRomFsType curRomFsType, int desiredIdOffset)
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
    
    int ret = -1;
    
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
        ret = parseRomFsEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys);
        if (ret == 0)
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
        // We'll proceed even if the Program NCA from the base application doesn't hold a RomFS section (ret == -2)
        ret = readNcaRomFsSection(appIndex, ROMFS_TYPE_APP, (int)titleContentInfos[contentIndex].id_offset);
        if (ret == -1) goto out;
        
        // Remove missing base RomFS error message if needed
        if (ret == -2) uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        
        // Update BKTR context to use the base RomFS if available
        bktrContext.use_base_romfs = (ret == 0);
        
        // Read BKTR entry data in the Program NCA from the update
        ret = (parseBktrEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys) ? 0 : -1);
        if (ret == 0)
        {
            bktrContext.storageId = curStorageId;
            bktrContext.idOffset = titleContentInfos[contentIndex].id_offset;
        }
    }
    
out:
    if (ret != 0)
    {
        ncmContentStorageClose(&ncmStorage);
        if (curStorageId == NcmStorageId_GameCard) closeGameCardStoragePartition();
    }
    
    if (titleContentInfos) free(titleContentInfos);
    
    return ret;
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
