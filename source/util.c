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

#include "dumper.h"
#include "fs_ext.h"
#include "ui.h"
#include "util.h"

/* Extern variables */

extern int breaks;
extern int font_height;

extern int cursor;
extern int scroll;

extern curMenuType menuType;

/* Constants */

const char *nswReleasesXmlUrl = "http://nswdb.com/xml.php";
const char *nswReleasesXmlTmpPath = OUTPUT_DUMP_BASE_PATH "NSWreleases.xml.tmp";
const char *nswReleasesXmlPath = OUTPUT_DUMP_BASE_PATH "NSWreleases.xml";
const char *nswReleasesRootElement = "releases";
const char *nswReleasesChildren = "release";
const char *nswReleasesChildrenImageSize = "imagesize";
const char *nswReleasesChildrenTitleID = "titleid";
const char *nswReleasesChildrenImgCrc = "imgcrc";
const char *nswReleasesChildrenReleaseName = "releasename";

const char *githubReleasesApiUrl = "https://api.github.com/repos/DarkMatterCore/nxdumptool/releases/latest";
const char *nxDumpToolPath = "sdmc:/switch/nxdumptool.nro";
const char *userAgent = "nxdumptool/" APP_VERSION " (Nintendo Switch)";

/* Statically allocated variables */

static char *result_buf = NULL;
static size_t result_sz = 0;
static size_t result_written = 0;

char *filenameBuffer = NULL;
char *filenames[FILENAME_MAX_CNT];
int filenamesCount = 0;

FsDeviceOperator fsOperatorInstance;
FsEventNotifier fsGameCardEventNotifier;
Handle fsGameCardEventHandle;
Event fsGameCardKernelEvent;
UEvent exitEvent;

AppletType programAppletType;

bool runningSxOs = false;

bool gameCardInserted;

u64 gameCardSize = 0, trimmedCardSize = 0;
char gameCardSizeStr[32] = {'\0'}, trimmedCardSizeStr[32] = {'\0'};

u64 gameCardUpdateTitleID = 0;
u32 gameCardUpdateVersion = 0;
char gameCardUpdateVersionStr[128] = {'\0'};

u8 *hfs0_header = NULL;
u64 hfs0_offset = 0, hfs0_size = 0;
u32 hfs0_partition_cnt = 0;

u8 *partitionHfs0Header = NULL;
u64 partitionHfs0HeaderOffset = 0, partitionHfs0HeaderSize = 0;
u32 partitionHfs0FileCount = 0, partitionHfs0StrTableSize = 0;

u32 titleAppCount = 0;
u64 *titleAppTitleID = NULL;
u32 *titleAppVersion = NULL;
FsStorageId *titleAppStorageId = NULL;

u32 titlePatchCount = 0;
u64 *titlePatchTitleID = NULL;
u32 *titlePatchVersion = NULL;
FsStorageId *titlePatchStorageId = NULL;

u32 titleAddOnCount = 0;
u64 *titleAddOnTitleID = NULL;
u32 *titleAddOnVersion = NULL;
FsStorageId *titleAddOnStorageId = NULL;

u32 sdCardTitleAppCount = 0;
u32 sdCardTitlePatchCount = 0;
u32 sdCardTitleAddOnCount = 0;

u32 nandUserTitleAppCount = 0;
u32 nandUserTitlePatchCount = 0;
u32 nandUserTitleAddOnCount = 0;

static bool sdCardAndEmmcTitleInfoLoaded = false;

u32 gameCardSdCardEmmcPatchCount = 0;

char **titleName = NULL;
char **fixedTitleName = NULL;
char **titleAuthor = NULL;
char **titleAppVersionStr = NULL;
u8 **titleIcon = NULL;

exefs_ctx_t exeFsContext;
romfs_ctx_t romFsContext;
bktr_ctx_t bktrContext;

char curRomFsPath[NAME_BUF_LEN] = {'\0'};
u32 curRomFsDirOffset = 0;
romfs_browser_entry *romFsBrowserEntries = NULL;

orphan_patch_addon_entry *orphanEntries = NULL;

char strbuf[NAME_BUF_LEN * 4] = {'\0'};

char appLaunchPath[NAME_BUF_LEN] = {'\0'};

bool isGameCardInserted()
{
    bool inserted;
    if (R_FAILED(fsDeviceOperatorIsGameCardInserted(&fsOperatorInstance, &inserted))) return false;
    return inserted;
}

void fsGameCardDetectionThreadFunc(void *arg)
{
    int idx;
    Result rc;
    
    while(true)
    {
        rc = waitMulti(&idx, -1, waiterForEvent(&fsGameCardKernelEvent), waiterForUEvent(&exitEvent));
        if (R_SUCCEEDED(rc))
        {
            if (idx == 0)
            {
                // Retrieve current gamecard status
                gameCardInserted = isGameCardInserted();
                eventClear(&fsGameCardKernelEvent);
            } else {
                break;
            }
        }
    }
    
    waitMulti(&idx, 0, waiterForEvent(&fsGameCardKernelEvent), waiterForUEvent(&exitEvent));
}

bool isServiceRunning(const char *serviceName)
{
    if (!serviceName || !strlen(serviceName)) return false;
    
    Handle handle;
    bool running = R_FAILED(smRegisterService(&handle, serviceName, false, 1));
    
    svcCloseHandle(handle);
    
    if (!running) smUnregisterService(serviceName);
    
    return running;
}

bool checkSxOsServices()
{
    return (isServiceRunning("tx") && !isServiceRunning("rnx"));
}

void delay(u8 seconds)
{
    if (!seconds) return;
    
    u64 nanoseconds = seconds * (u64)1000000000;
    svcSleepThread(nanoseconds);
    
    uiRefreshDisplay();
}

void formatETAString(u64 curTime, char *output, u32 outSize)
{
    if (!output || !outSize) return;
    
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
    
    snprintf(output, outSize, "%02luH%02luM%02luS", hour, min, sec);
}

void freeStringsPtr(char **var)
{
    if (var)
	{
		u64 i;
		for(i = 0; var[i]; i++) free(var[i]);
		free(var);
	}
}

void initExeFsContext()
{
    memset(&(exeFsContext.ncmStorage), 0, sizeof(NcmContentStorage));
    memset(&(exeFsContext.ncaId), 0, sizeof(NcmNcaId));
    memset(&(exeFsContext.aes_ctx), 0, sizeof(Aes128CtrContext));
    exeFsContext.exefs_offset = 0;
    exeFsContext.exefs_size = 0;
    memset(&(exeFsContext.exefs_header), 0, sizeof(pfs0_header));
    exeFsContext.exefs_entries_offset = 0;
    exeFsContext.exefs_entries = NULL;
    exeFsContext.exefs_str_table_offset = 0;
    exeFsContext.exefs_str_table = NULL;
    exeFsContext.exefs_data_offset = 0;
}

void freeExeFsContext()
{
    // Remember to close this NCM service resource
    serviceClose(&(exeFsContext.ncmStorage.s));
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
    memset(&(romFsContext.ncmStorage), 0, sizeof(NcmContentStorage));
    memset(&(romFsContext.ncaId), 0, sizeof(NcmNcaId));
    memset(&(romFsContext.aes_ctx), 0, sizeof(Aes128CtrContext));
    romFsContext.section_offset = 0;
    romFsContext.section_size = 0;
    romFsContext.romfs_offset = 0;
    romFsContext.romfs_size = 0;
    romFsContext.romfs_dirtable_offset = 0;
    romFsContext.romfs_dirtable_size = 0;
    romFsContext.romfs_dir_entries = NULL;
    romFsContext.romfs_filetable_offset = 0;
    romFsContext.romfs_filetable_size = 0;
    romFsContext.romfs_file_entries = NULL;
    romFsContext.romfs_filedata_offset = 0;
}

void freeRomFsContext()
{
    // Remember to close this NCM service resource
    serviceClose(&(romFsContext.ncmStorage.s));
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
    memset(&(bktrContext.ncmStorage), 0, sizeof(NcmContentStorage));
    memset(&(bktrContext.ncaId), 0, sizeof(NcmNcaId));
    memset(&(bktrContext.aes_ctx), 0, sizeof(Aes128CtrContext));
    bktrContext.section_offset = 0;
    bktrContext.section_size = 0;
    memset(&(bktrContext.superblock), 0, sizeof(bktr_superblock_t));
    bktrContext.relocation_block = NULL;
    bktrContext.subsection_block = NULL;
    bktrContext.virtual_seek = 0;
    bktrContext.bktr_seek = 0;
    bktrContext.base_seek = 0;
    bktrContext.romfs_offset = 0;
    bktrContext.romfs_size = 0;
    bktrContext.romfs_dirtable_offset = 0;
    bktrContext.romfs_dirtable_size = 0;
    bktrContext.romfs_dir_entries = NULL;
    bktrContext.romfs_filetable_offset = 0;
    bktrContext.romfs_filetable_size = 0;
    bktrContext.romfs_file_entries = NULL;
    bktrContext.romfs_filedata_offset = 0;
}

void freeBktrContext()
{
    // Remember to close this NCM service resource
    serviceClose(&(bktrContext.ncmStorage.s));
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

void freeGameCardInfo()
{
    if (hfs0_header != NULL)
    {
        free(hfs0_header);
        hfs0_header = NULL;
    }
    
    hfs0_offset = 0;
    hfs0_size = 0;
    hfs0_partition_cnt = 0;
    
    if (partitionHfs0Header != NULL)
    {
        free(partitionHfs0Header);
        partitionHfs0Header = NULL;
    }
    
    partitionHfs0HeaderOffset = 0;
    partitionHfs0HeaderSize = 0;
    partitionHfs0FileCount = 0;
    partitionHfs0StrTableSize = 0;
    
    gameCardSize = 0;
    memset(gameCardSizeStr, 0, sizeof(gameCardSizeStr));
    
    trimmedCardSize = 0;
    memset(trimmedCardSizeStr, 0, sizeof(trimmedCardSizeStr));
    
    gameCardUpdateTitleID = 0;
    gameCardUpdateVersion = 0;
    memset(gameCardUpdateVersionStr, 0, sizeof(gameCardUpdateVersionStr));
}

void freeTitleInfo()
{
    titleAppCount = 0;
    
    if (titleAppTitleID != NULL)
    {
        free(titleAppTitleID);
        titleAppTitleID = NULL;
    }
    
    if (titleAppVersion != NULL)
    {
        free(titleAppVersion);
        titleAppVersion = NULL;
    }
    
    if (titleAppVersionStr != NULL)
    {
        freeStringsPtr(titleAppVersionStr);
        titleAppVersionStr = NULL;
    }
    
    if (titleAppStorageId != NULL)
    {
        free(titleAppStorageId);
        titleAppStorageId = NULL;
    }
    
    titlePatchCount = 0;
    
    if (titlePatchTitleID != NULL)
    {
        free(titlePatchTitleID);
        titlePatchTitleID = NULL;
    }
    
    if (titlePatchVersion != NULL)
    {
        free(titlePatchVersion);
        titlePatchVersion = NULL;
    }
    
    if (titlePatchStorageId != NULL)
    {
        free(titlePatchStorageId);
        titlePatchStorageId = NULL;
    }
    
    titleAddOnCount = 0;
    
    if (titleAddOnTitleID != NULL)
    {
        free(titleAddOnTitleID);
        titleAddOnTitleID = NULL;
    }
    
    if (titleAddOnVersion != NULL)
    {
        free(titleAddOnVersion);
        titleAddOnVersion = NULL;
    }
    
    if (titleAddOnStorageId != NULL)
    {
        free(titleAddOnStorageId);
        titleAddOnStorageId = NULL;
    }
    
    sdCardTitleAppCount = 0;
    sdCardTitlePatchCount = 0;
    sdCardTitleAddOnCount = 0;
    
    nandUserTitleAppCount = 0;
    nandUserTitlePatchCount = 0;
    nandUserTitleAddOnCount = 0;
    
    if (titleName != NULL)
    {
        freeStringsPtr(titleName);
        titleName = NULL;
    }
    
    if (fixedTitleName != NULL)
    {
        freeStringsPtr(fixedTitleName);
        fixedTitleName = NULL;
    }
    
    if (titleAuthor != NULL)
    {
        freeStringsPtr(titleAuthor);
        titleAuthor = NULL;
    }
    
    if (titleIcon != NULL)
    {
        freeStringsPtr((char**)titleIcon);
        titleIcon = NULL;
    }
}

void freeGlobalData()
{
    freeGameCardInfo();
    
    freeTitleInfo();
    
    freeExeFsContext();
    
    freeRomFsContext();
    
    freeBktrContext();
    
    if (romFsBrowserEntries != NULL)
    {
        free(romFsBrowserEntries);
        romFsBrowserEntries = NULL;
    }
    
    if (orphanEntries != NULL)
    {
        free(orphanEntries);
        orphanEntries = NULL;
    }
}

bool listTitlesByType(NcmContentMetaDatabase *ncmDb, u8 filter)
{
    if (!ncmDb || (filter != META_DB_REGULAR_APPLICATION && filter != META_DB_PATCH && filter != META_DB_ADDON))
    {
        uiStatusMsg("listTitlesByType: invalid parameters (0x%02X filter).", filter);
        return false;
    }
    
    bool success = false, proceed = true, memError = false;
    
    Result result;
    
    NcmApplicationContentMetaKey *titleList = NULL;
    NcmApplicationContentMetaKey *titleListTmp = NULL;
    size_t titleListSize = sizeof(NcmApplicationContentMetaKey);
    
    u32 written = 0, total = 0;
    
    u64 *titleIDs = NULL, *tmpTIDs = NULL;
    u32 *versions = NULL, *tmpVersions = NULL;
    
    titleList = calloc(1, titleListSize);
    if (titleList)
    {
        if (R_SUCCEEDED(result = ncmContentMetaDatabaseListApplication(ncmDb, filter, titleList, titleListSize, &written, &total)))
        {
            if (written && total)
            {
                if (total > written)
                {
                    titleListSize *= total;
                    titleListTmp = realloc(titleList, titleListSize);
                    if (titleListTmp)
                    {
                        titleList = titleListTmp;
                        memset(titleList, 0, titleListSize);
                        
                        if (R_SUCCEEDED(result = ncmContentMetaDatabaseListApplication(ncmDb, filter, titleList, titleListSize, &written, &total)))
                        {
                            if (written != total)
                            {
                                uiStatusMsg("listTitlesByType: title count mismatch in ncmContentMetaDatabaseListApplication (%u != %u) (0x%02X filter).", written, total, filter);
                                proceed = false;
                            }
                        } else {
                            uiStatusMsg("listTitlesByType: ncmContentMetaDatabaseListApplication failed! (0x%08X) (0x%02X filter).", result, filter);
                            proceed = false;
                        }
                    } else {
                        uiStatusMsg("listTitlesByType: error reallocating output buffer for ncmContentMetaDatabaseListApplication (%u %s) (0x%02X filter).", total, (total == 1 ? "entry" : "entries"), filter);
                        proceed = false;
                    }
                }
                
                if (proceed)
                {
                    titleIDs = calloc(total, sizeof(u64));
                    versions = calloc(total, sizeof(u32));
                    
                    if (titleIDs != NULL && versions != NULL)
                    {
                        u32 i;
                        for(i = 0; i < total; i++)
                        {
                            titleIDs[i] = titleList[i].metaRecord.titleId;
                            versions[i] = titleList[i].metaRecord.version;
                        }
                        
                        if (filter == META_DB_REGULAR_APPLICATION)
                        {
                            // If ptr == NULL, realloc will essentially act as a malloc
                            tmpTIDs = realloc(titleAppTitleID, (titleAppCount + total) * sizeof(u64));
                            tmpVersions = realloc(titleAppVersion, (titleAppCount + total) * sizeof(u32));
                            
                            if (tmpTIDs != NULL && tmpVersions != NULL)
                            {
                                titleAppTitleID = tmpTIDs;
                                memcpy(titleAppTitleID + titleAppCount, titleIDs, total * sizeof(u64));
                                
                                titleAppVersion = tmpVersions;
                                memcpy(titleAppVersion + titleAppCount, versions, total * sizeof(u32));
                                
                                titleAppCount += total;
                                
                                success = true;
                            } else {
                                if (tmpTIDs != NULL) titleAppTitleID = tmpTIDs;
                                
                                if (tmpVersions != NULL) titleAppVersion = tmpVersions;
                                
                                memError = true;
                            }
                        } else
                        if (filter == META_DB_PATCH)
                        {
                            // If ptr == NULL, realloc will essentially act as a malloc
                            tmpTIDs = realloc(titlePatchTitleID, (titlePatchCount + total) * sizeof(u64));
                            tmpVersions = realloc(titlePatchVersion, (titlePatchCount + total) * sizeof(u32));
                            
                            if (tmpTIDs != NULL && tmpVersions != NULL)
                            {
                                titlePatchTitleID = tmpTIDs;
                                memcpy(titlePatchTitleID + titlePatchCount, titleIDs, total * sizeof(u64));
                                
                                titlePatchVersion = tmpVersions;
                                memcpy(titlePatchVersion + titlePatchCount, versions, total * sizeof(u32));
                                
                                titlePatchCount += total;
                                
                                success = true;
                            } else {
                                if (tmpTIDs != NULL) titlePatchTitleID = tmpTIDs;
                                
                                if (tmpVersions != NULL) titlePatchVersion = tmpVersions;
                                
                                memError = true;
                            }
                        } else
                        if (filter == META_DB_ADDON)
                        {
                            // If ptr == NULL, realloc will essentially act as a malloc
                            tmpTIDs = realloc(titleAddOnTitleID, (titleAddOnCount + total) * sizeof(u64));
                            tmpVersions = realloc(titleAddOnVersion, (titleAddOnCount + total) * sizeof(u32));
                            
                            if (tmpTIDs != NULL && tmpVersions != NULL)
                            {
                                titleAddOnTitleID = tmpTIDs;
                                memcpy(titleAddOnTitleID + titleAddOnCount, titleIDs, total * sizeof(u64));
                                
                                titleAddOnVersion = tmpVersions;
                                memcpy(titleAddOnVersion + titleAddOnCount, versions, total * sizeof(u32));
                                
                                titleAddOnCount += total;
                                
                                success = true;
                            } else {
                                if (tmpTIDs != NULL) titleAddOnTitleID = tmpTIDs;
                                
                                if (tmpVersions != NULL) titleAddOnVersion = tmpVersions;
                                
                                memError = true;
                            }
                        }
                    } else {
                        memError = true;
                    }
                    
                    if (titleIDs != NULL) free(titleIDs);
                    
                    if (versions != NULL) free(versions);
                    
                    if (memError) uiStatusMsg("listTitlesByType: failed to allocate memory for TID/version buffer! (0x%02X filter).", filter);
                }
            } else {
                // There are no titles that match the provided filter in the opened storage device
                success = true;
            }
        } else {
            uiStatusMsg("listTitlesByType: ncmContentMetaDatabaseListApplication failed! (0x%08X) (0x%02X filter).", result, filter);
        }
        
        free(titleList);
    } else {
        uiStatusMsg("listTitlesByType: unable to allocate memory for the ApplicationContentMetaKey struct (0x%02X filter).", filter);
    }
    
    return success;
}

bool getTitleIDAndVersionList(FsStorageId curStorageId)
{
    if (curStorageId != FsStorageId_GameCard && curStorageId != FsStorageId_SdCard && curStorageId != FsStorageId_NandUser)
    {
        uiStatusMsg("getTitleIDAndVersionList: invalid storage ID!");
        return false;
    }
    
    /* Check if the SD card is really mounted */
    if (curStorageId == FsStorageId_SdCard && fsdevGetDefaultFileSystem() == NULL) return true;
    
    bool listApp = false, listPatch = false, listAddOn = false, success = false;
    
    Result result;
    NcmContentMetaDatabase ncmDb;
    
    u32 i;
    FsStorageId *tmpStorages = NULL;
    u32 curAppCount = titleAppCount, curPatchCount = titlePatchCount, curAddOnCount = titleAddOnCount;
    
    if (R_SUCCEEDED(result = ncmOpenContentMetaDatabase(curStorageId, &ncmDb)))
    {
        listApp = listTitlesByType(&ncmDb, META_DB_REGULAR_APPLICATION);
        if (listApp && titleAppCount > curAppCount)
        {
            tmpStorages = realloc(titleAppStorageId, titleAppCount * sizeof(FsStorageId));
            if (tmpStorages)
            {
                titleAppStorageId = tmpStorages;
                
                tmpStorages = NULL;
                
                for(i = curAppCount; i < titleAppCount; i++) titleAppStorageId[i] = curStorageId;
            } else {
                titleAppCount = curAppCount;
                listApp = false;
            }
        }
        
        listPatch = listTitlesByType(&ncmDb, META_DB_PATCH);
        if (listPatch && titlePatchCount > curPatchCount)
        {
            tmpStorages = realloc(titlePatchStorageId, titlePatchCount * sizeof(FsStorageId));
            if (tmpStorages)
            {
                titlePatchStorageId = tmpStorages;
                
                tmpStorages = NULL;
                
                for(i = curPatchCount; i < titlePatchCount; i++) titlePatchStorageId[i] = curStorageId;
            } else {
                titlePatchCount = curPatchCount;
                listPatch = false;
            }
        }
        
        listAddOn = listTitlesByType(&ncmDb, META_DB_ADDON);
        if (listAddOn && titleAddOnCount > curAddOnCount)
        {
            tmpStorages = realloc(titleAddOnStorageId, titleAddOnCount * sizeof(FsStorageId));
            if (tmpStorages)
            {
                titleAddOnStorageId = tmpStorages;
                
                tmpStorages = NULL;
                
                for(i = curAddOnCount; i < titleAddOnCount; i++) titleAddOnStorageId[i] = curStorageId;
            } else {
                titleAddOnCount = curAddOnCount;
                listAddOn = false;
            }
        }
        
        success = (listApp || listPatch || listAddOn);
        
        serviceClose(&(ncmDb.s));
    } else {
        if (curStorageId == FsStorageId_SdCard && result == 0x21005)
        {
            // If the SD card is mounted, but is isn't currently used by HOS because of some weird reason, just filter this particular error and continue
            // This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted
            success = true;
        } else {
            uiStatusMsg("getTitleIDAndVersionList: ncmOpenContentMetaDatabase failed! (0x%08X)", result);
        }
    }
    
    return success;
}

bool loadPatchesFromSdCardAndEmmc()
{
    if (menuType != MENUTYPE_GAMECARD || !titleAppCount || !titleAppTitleID) return false;
    
    u32 i, j;
    
    Result result;
    NcmContentMetaDatabase ncmDb;
    
    NcmApplicationContentMetaKey *titleList;
    NcmApplicationContentMetaKey *titleListTmp;
    size_t titleListSize = sizeof(NcmApplicationContentMetaKey);
    
    u32 written, total;
    
    u64 *titleIDs, *tmpTIDs;
    u32 *versions, *tmpVersions;
    FsStorageId *tmpStorages;
    
    bool proceed;
    
    for(i = 0; i < 2; i++)
    {
        FsStorageId curStorageId = (i == 0 ? FsStorageId_SdCard : FsStorageId_NandUser);
        
        /* Check if the SD card is really mounted */
        if (curStorageId == FsStorageId_SdCard && fsdevGetDefaultFileSystem() == NULL) continue;
        
        memset(&ncmDb, 0, sizeof(NcmContentMetaDatabase));
        
        titleList = titleListTmp = NULL;
        
        written = total = 0;
        
        titleIDs = tmpTIDs = NULL;
        versions = tmpVersions = NULL;
        tmpStorages = NULL;
        
        proceed = true;
        
        if (R_SUCCEEDED(result = ncmOpenContentMetaDatabase(curStorageId, &ncmDb)))
        {
            titleList = calloc(1, titleListSize);
            if (titleList)
            {
                if (R_SUCCEEDED(result = ncmContentMetaDatabaseListApplication(&ncmDb, META_DB_PATCH, titleList, titleListSize, &written, &total)) && written && total)
                {
                    if (total > written)
                    {
                        titleListSize *= total;
                        titleListTmp = realloc(titleList, titleListSize);
                        if (titleListTmp)
                        {
                            titleList = titleListTmp;
                            memset(titleList, 0, titleListSize);
                            
                            if (R_SUCCEEDED(result = ncmContentMetaDatabaseListApplication(&ncmDb, META_DB_PATCH, titleList, titleListSize, &written, &total)))
                            {
                                if (written != total) proceed = false;
                            } else {
                                proceed = false;
                            }
                        } else {
                            proceed = false;
                        }
                    }
                    
                    if (proceed)
                    {
                        titleIDs = calloc(total, sizeof(u64));
                        versions = calloc(total, sizeof(u32));
                        
                        if (titleIDs != NULL && versions != NULL)
                        {
                            for(j = 0; j < total; j++)
                            {
                                titleIDs[j] = titleList[j].metaRecord.titleId;
                                versions[j] = titleList[j].metaRecord.version;
                            }
                            
                            // If ptr == NULL, realloc will essentially act as a malloc
                            tmpTIDs = realloc(titlePatchTitleID, (titlePatchCount + total) * sizeof(u64));
                            tmpVersions = realloc(titlePatchVersion, (titlePatchCount + total) * sizeof(u32));
                            tmpStorages = realloc(titlePatchStorageId, (titlePatchCount + total) * sizeof(FsStorageId));
                            
                            if (tmpTIDs != NULL && tmpVersions != NULL && tmpStorages != NULL)
                            {
                                titlePatchTitleID = tmpTIDs;
                                memcpy(titlePatchTitleID + titlePatchCount, titleIDs, total * sizeof(u64));
                                
                                titlePatchVersion = tmpVersions;
                                memcpy(titlePatchVersion + titlePatchCount, versions, total * sizeof(u32));
                                
                                titlePatchStorageId = tmpStorages;
                                for(j = titlePatchCount; j < (titlePatchCount + total); j++) titlePatchStorageId[j] = curStorageId;
                                
                                titlePatchCount += total;
                                
                                gameCardSdCardEmmcPatchCount += total;
                                
                                if (curStorageId == FsStorageId_SdCard)
                                {
                                    sdCardTitlePatchCount = total;
                                } else {
                                    nandUserTitlePatchCount = total;
                                }
                            } else {
                                if (tmpTIDs != NULL) titlePatchTitleID = tmpTIDs;
                                
                                if (tmpVersions != NULL) titlePatchVersion = tmpVersions;
                                
                                if (tmpStorages != NULL) titlePatchStorageId = tmpStorages;
                            }
                        }
                        
                        if (titleIDs != NULL) free(titleIDs);
                        
                        if (versions != NULL) free(versions);
                    }
                }
                
                free(titleList);
            }
            
            serviceClose(&(ncmDb.s));
        }
    }
    
    if (gameCardSdCardEmmcPatchCount) return true;
    
    return false;
}

void freePatchesFromSdCardAndEmmc()
{
    if (menuType != MENUTYPE_GAMECARD || !titleAppCount || !titleAppTitleID || !titlePatchCount || !titlePatchTitleID || !titlePatchVersion || !titlePatchStorageId || !gameCardSdCardEmmcPatchCount) return;
    
    u64 *tmpTIDs = NULL;
    u32 *tmpVersions = NULL;
    FsStorageId *tmpStorages = NULL;
    
    if ((titlePatchCount - gameCardSdCardEmmcPatchCount) > 0)
    {
        tmpTIDs = realloc(titlePatchTitleID, (titlePatchCount - gameCardSdCardEmmcPatchCount) * sizeof(u64));
        tmpVersions = realloc(titlePatchVersion, (titlePatchCount - gameCardSdCardEmmcPatchCount) * sizeof(u32));
        tmpStorages = realloc(titlePatchStorageId, (titlePatchCount - gameCardSdCardEmmcPatchCount) * sizeof(FsStorageId));
        
        if (tmpTIDs != NULL && tmpVersions != NULL && tmpStorages != NULL)
        {
            titlePatchTitleID = tmpTIDs;
            
            titlePatchVersion = tmpVersions;
            
            titlePatchStorageId = tmpStorages;
        } else {
            if (tmpTIDs != NULL) titlePatchTitleID = tmpTIDs;
            
            if (tmpVersions != NULL) titlePatchVersion = tmpVersions;
            
            if (tmpStorages != NULL) titlePatchStorageId = tmpStorages;
        }
    } else {
        free(titlePatchTitleID);
        titlePatchTitleID = NULL;
        
        free(titlePatchVersion);
        titlePatchVersion = NULL;
        
        free(titlePatchStorageId);
        titlePatchStorageId = NULL;
    }
    
    titlePatchCount -= gameCardSdCardEmmcPatchCount;
    
    gameCardSdCardEmmcPatchCount = 0;
    
    sdCardTitlePatchCount = 0;
    
    nandUserTitlePatchCount = 0;
}

void convertTitleVersionToDecimal(u32 version, char *versionBuf, size_t versionBufSize)
{
    u8 major = (u8)((version >> 26) & 0x3F);
    u8 middle = (u8)((version >> 20) & 0x3F);
    u8 minor = (u8)((version >> 16) & 0xF);
    u16 build = (u16)version;
    
    snprintf(versionBuf, versionBufSize, "%u (%u.%u.%u.%u)", version, major, middle, minor, build);
}

bool getTitleControlNacp(u64 titleID, char *nameBuf, int nameBufSize, char *authorBuf, int authorBufSize, u8 **iconBuf)
{
    if (!titleID || !nameBuf || !nameBufSize || !authorBuf || !authorBufSize || !iconBuf)
    {
        uiStatusMsg("getTitleControlNacp: invalid parameters to retrieve Control.nacp.");
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
        if (R_SUCCEEDED(result = nsGetApplicationControlData(1, titleID, buf, sizeof(NsApplicationControlData), &outsize)))
        {
            if (outsize >= sizeof(buf->nacp))
            {
                if (R_SUCCEEDED(result = nacpGetLanguageEntry(&buf->nacp, &langentry)))
                {
                    strncpy(nameBuf, langentry->name, nameBufSize);
                    strncpy(authorBuf, langentry->author, authorBufSize);
                    getNameAndAuthor = true;
                } else {
                    uiStatusMsg("getTitleControlNacp: GetLanguageEntry failed! (0x%08X)", result);
                }
                
                getIcon = uiLoadJpgFromMem(buf->icon, sizeof(buf->icon), NACP_ICON_SQUARE_DIMENSION, NACP_ICON_SQUARE_DIMENSION, NACP_ICON_DOWNSCALED, NACP_ICON_DOWNSCALED, iconBuf);
                if (!getIcon) uiStatusMsg(strbuf);
                
                success = (getNameAndAuthor && getIcon);
            } else {
                uiStatusMsg("getTitleControlNacp: Control.nacp buffer size (%u bytes) is too small! Expected: %u bytes", outsize, sizeof(buf->nacp));
            }
        } else {
            uiStatusMsg("getTitleControlNacp: GetApplicationControlData failed! (0x%08X)", result);
        }
        
        free(buf);
    } else {
        uiStatusMsg("getTitleControlNacp: Unable to allocate memory for the ns service operations.");
    }
    
    return success;
}

void removeIllegalCharacters(char *name)
{
    u32 i, len = strlen(name);
    for (i = 0; i < len; i++)
    {
        if (memchr("?[]/\\=+<>:;\",*|^", name[i], sizeof("?[]/\\=+<>:;\",*|^") - 1) || name[i] < 0x20 || name[i] > 0x7E) name[i] = '_';
    }
}

void createOutputDirectories()
{
    mkdir(OUTPUT_DUMP_BASE_PATH, 0744);
    mkdir(XCI_DUMP_PATH, 0744); 
    mkdir(NSP_DUMP_PATH, 0744);
    mkdir(HFS0_DUMP_PATH, 0744);
    mkdir(EXEFS_DUMP_PATH, 0744);
    mkdir(ROMFS_DUMP_PATH, 0744);
    mkdir(CERT_DUMP_PATH, 0744);
}

void strtrim(char *str)
{
    if (!str || !*str) return;
    
    char *start = str;
    char *end = start + strlen(str);
    
    while(--end >= start)
    {
        if (!isspace(*end)) break;
    }
    
    *(++end) = '\0';
    
    while(isspace(*start)) start++;
    
    if (start != str) memmove(str, start, end - start + 1);
}

bool getRootHfs0Header()
{
    u32 magic;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    char gamecard_header[GAMECARD_HEADER_SIZE] = {'\0'};
    
    hfs0_partition_cnt = 0;
    
    workaroundPartitionZeroAccess();
    
    if (R_FAILED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
    {
        uiStatusMsg("getRootHfs0Header: GetGameCardHandle failed! (0x%08X)", result);
        return false;
    }
    
    if (R_FAILED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, 0)))
    {
        uiStatusMsg("getRootHfs0Header: OpenGameCardStorage failed! (0x%08X)", result);
        return false;
    }
    
    if (R_FAILED(result = fsStorageRead(&gameCardStorage, 0, gamecard_header, GAMECARD_HEADER_SIZE)))
    {
        uiStatusMsg("getRootHfs0Header: StorageRead failed to read %u-byte chunk from offset 0x%016lX! (0x%08X)", GAMECARD_HEADER_SIZE, 0, result);
        fsStorageClose(&gameCardStorage);
        return false;
    }
    
    u8 cardSize = (u8)gamecard_header[GAMECARD_SIZE_ADDR];
    
    switch(cardSize)
    {
        case 0xFA: // 1 GiB
            gameCardSize = GAMECARD_SIZE_1GiB;
            break;
        case 0xF8: // 2 GiB
            gameCardSize = GAMECARD_SIZE_2GiB;
            break;
        case 0xF0: // 4 GiB
            gameCardSize = GAMECARD_SIZE_4GiB;
            break;
        case 0xE0: // 8 GiB
            gameCardSize = GAMECARD_SIZE_8GiB;
            break;
        case 0xE1: // 16 GiB
            gameCardSize = GAMECARD_SIZE_16GiB;
            break;
        case 0xE2: // 32 GiB
            gameCardSize = GAMECARD_SIZE_32GiB;
            break;
        default:
            uiStatusMsg("getRootHfs0Header: Invalid gamecard size value: 0x%02X", cardSize);
            fsStorageClose(&gameCardStorage);
            return false;
    }
    
    convertSize(gameCardSize, gameCardSizeStr, sizeof(gameCardSizeStr) / sizeof(gameCardSizeStr[0]));
    
    memcpy(&trimmedCardSize, gamecard_header + GAMECARD_DATAEND_ADDR, sizeof(u64));
    trimmedCardSize = (GAMECARD_HEADER_SIZE + (trimmedCardSize * MEDIA_UNIT_SIZE));
    convertSize(trimmedCardSize, trimmedCardSizeStr, sizeof(trimmedCardSizeStr) / sizeof(trimmedCardSizeStr[0]));
    
    memcpy(&hfs0_offset, gamecard_header + HFS0_OFFSET_ADDR, sizeof(u64));
    memcpy(&hfs0_size, gamecard_header + HFS0_SIZE_ADDR, sizeof(u64));
    
    hfs0_header = malloc(hfs0_size);
    if (!hfs0_header)
    {
        uiStatusMsg("getRootHfs0Header: Unable to allocate memory for the root HFS0 header!");
        
        gameCardSize = 0;
        memset(gameCardSizeStr, 0, sizeof(gameCardSizeStr));
        
        trimmedCardSize = 0;
        memset(trimmedCardSizeStr, 0, sizeof(trimmedCardSizeStr));
        
        hfs0_offset = 0;
        hfs0_size = 0;
        
        fsStorageClose(&gameCardStorage);
        
        return false;
    }
    
    if (R_FAILED(result = fsStorageRead(&gameCardStorage, hfs0_offset, hfs0_header, hfs0_size)))
    {
        uiStatusMsg("getRootHfs0Header: StorageRead failed to read %u-byte chunk from offset 0x%016lX! (0x%08X)", hfs0_size, hfs0_offset, result);
        
        gameCardSize = 0;
        memset(gameCardSizeStr, 0, sizeof(gameCardSizeStr));
        
        trimmedCardSize = 0;
        memset(trimmedCardSizeStr, 0, sizeof(trimmedCardSizeStr));
        
        free(hfs0_header);
        hfs0_header = NULL;
        hfs0_offset = 0;
        hfs0_size = 0;
        
        fsStorageClose(&gameCardStorage);
        
        return false;
    }
    
    memcpy(&magic, hfs0_header, sizeof(u32));
    magic = bswap_32(magic);
    if (magic != HFS0_MAGIC)
    {
        uiStatusMsg("getRootHfs0Header: Magic word mismatch! 0x%08X != 0x%08X", magic, HFS0_MAGIC);
        
        gameCardSize = 0;
        memset(gameCardSizeStr, 0, sizeof(gameCardSizeStr));
        
        trimmedCardSize = 0;
        memset(trimmedCardSizeStr, 0, sizeof(trimmedCardSizeStr));
        
        free(hfs0_header);
        hfs0_header = NULL;
        hfs0_offset = 0;
        hfs0_size = 0;
        
        fsStorageClose(&gameCardStorage);
        
        return false;
    }
    
    memcpy(&hfs0_partition_cnt, hfs0_header + HFS0_FILE_COUNT_ADDR, sizeof(u32));
    
    fsStorageClose(&gameCardStorage);
    
    return true;
}

void getGameCardUpdateInfo()
{
    Result result;
    FsGameCardHandle handle;
    
    gameCardUpdateTitleID = 0;
    gameCardUpdateVersion = 0;
    
    if (R_FAILED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
    {
        uiStatusMsg("getGameCardUpdateInfo: GetGameCardHandle failed! (0x%08X)", result);
        return;
    }
    
    // Get bundled FW version update
    if (R_SUCCEEDED(result = fsDeviceOperatorUpdatePartitionInfo(&fsOperatorInstance, &handle, &gameCardUpdateVersion, &gameCardUpdateTitleID)))
    {
        if (gameCardUpdateTitleID == GAMECARD_UPDATE_TITLEID)
        {
            char decimalVersion[64] = {'\0'};
            convertTitleVersionToDecimal(gameCardUpdateVersion, decimalVersion, sizeof(decimalVersion));
            
            switch(gameCardUpdateVersion)
            {
                case SYSUPDATE_100:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "1.0.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_200:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "2.0.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_210:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "2.1.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_220:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "2.2.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_230:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "2.3.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_300:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "3.0.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_301:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "3.0.1 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_302:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "3.0.2 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_400:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "4.0.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_401:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "4.0.1 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_410:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "4.1.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_500:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "5.0.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_501:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "5.0.1 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_502:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "5.0.2 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_510:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "5.1.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_600:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "6.0.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_601:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "6.0.1 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_610:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "6.1.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_620:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "6.2.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_700:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "7.0.0 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_701:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "7.0.1 - v%s", decimalVersion);
                    break;
                case SYSUPDATE_800:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "8.0.0 - v%s", decimalVersion);
                    break;
                default:
                    snprintf(gameCardUpdateVersionStr, sizeof(gameCardUpdateVersionStr) / sizeof(gameCardUpdateVersionStr[0]), "UNKNOWN - v%s", decimalVersion);
                    break;
            }
        } else {
            uiStatusMsg("getGameCardUpdateInfo: update Title ID mismatch! %016lX != %016lX", gameCardUpdateTitleID, GAMECARD_UPDATE_TITLEID);
        }
    } else {
        uiStatusMsg("getGameCardUpdateInfo: UpdatePartitionInfo failed! (0x%08X)", result);
    }
}

void loadTitleInfo()
{
    if (menuType == MENUTYPE_MAIN)
    {
        freeGlobalData();
        sdCardAndEmmcTitleInfoLoaded = false;
        return;
    }
    
    bool proceed = false, freeBuf = false;
    
    if (menuType == MENUTYPE_GAMECARD)
    {
        if (gameCardInserted)
        {
            if (hfs0_header != NULL) return;
            
            /* Don't access the gamecard immediately to avoid conflicts with the fsp-srv, ncm and ns services */
            uiPleaseWait(GAMECARD_WAIT_TIME);
            
            if (!getRootHfs0Header())
            {
                uiPrintHeadline();
                return;
            }
            
            getGameCardUpdateInfo();
            
            freeTitleInfo();
            
            proceed = getTitleIDAndVersionList(FsStorageId_GameCard);
        } else {
            freeGlobalData();
            return;
        }
    } else
    if (menuType == MENUTYPE_SDCARD_EMMC)
    {
        if (titleAppCount || titlePatchCount || titleAddOnCount || sdCardAndEmmcTitleInfoLoaded) return;
        
        uiPleaseWait(1);
        
        freeTitleInfo();
        
        if (getTitleIDAndVersionList(FsStorageId_SdCard))
        {
            sdCardTitleAppCount = titleAppCount;
            sdCardTitlePatchCount = titlePatchCount;
            sdCardTitleAddOnCount = titleAddOnCount;
            
            if (getTitleIDAndVersionList(FsStorageId_NandUser))
            {
                nandUserTitleAppCount = (titleAppCount - sdCardTitleAppCount);
                nandUserTitlePatchCount = (titlePatchCount - sdCardTitlePatchCount);
                nandUserTitleAddOnCount = (titleAddOnCount - sdCardTitleAddOnCount);
                
                proceed = true;
            }
        }
        
        sdCardAndEmmcTitleInfoLoaded = true;
    }
    
    if (proceed && titleAppCount > 0)
    {
        titleName = calloc(titleAppCount + 1, sizeof(char*));
        fixedTitleName = calloc(titleAppCount + 1, sizeof(char*));
        titleAuthor = calloc(titleAppCount + 1, sizeof(char*));
        titleAppVersionStr = calloc(titleAppCount + 1, sizeof(char*));
        titleIcon = calloc(titleAppCount + 1, sizeof(u8*));
        
        if (titleName != NULL && fixedTitleName != NULL && titleAuthor != NULL && titleAppVersionStr != NULL && titleIcon != NULL)
        {
            u32 i;
            for(i = 0; i < titleAppCount; i++)
            {
                titleName[i] = calloc(NACP_APPNAME_LEN + 1, sizeof(char));
                fixedTitleName[i] = calloc(NACP_APPNAME_LEN + 1, sizeof(char));
                titleAuthor[i] = calloc(NACP_AUTHOR_LEN + 1, sizeof(char));
                titleAppVersionStr[i] = calloc(VERSION_STR_LEN + 1, sizeof(char));
                
                if (titleName[i] != NULL && fixedTitleName[i] != NULL && titleAuthor[i] != NULL && titleAppVersionStr[i] != NULL)
                {
                    convertTitleVersionToDecimal(titleAppVersion[i], titleAppVersionStr[i], VERSION_STR_LEN);
                    
                    if (getTitleControlNacp(titleAppTitleID[i], titleName[i], NACP_APPNAME_LEN, titleAuthor[i], NACP_AUTHOR_LEN, &(titleIcon[i])))
                    {
                        strtrim(titleName[i]);
                        
                        strtrim(titleAuthor[i]);
                        
                        snprintf(fixedTitleName[i], NACP_APPNAME_LEN, titleName[i]);
                        removeIllegalCharacters(fixedTitleName[i]);
                    } else {
                        freeBuf = true;
                        break;
                    }
                } else {
                    uiStatusMsg("loadTitleInfo: error allocating memory for title information (application #%u).", i + 1);
                    freeBuf = true;
                    break;
                }
            }
        } else {
            uiStatusMsg("loadTitleInfo: error allocating memory for title information.");
            freeBuf = true;
        }
        
        if (freeBuf) freeTitleInfo();
    }
    
    uiPrintHeadline();
}

bool getHfs0EntryDetails(u8 *hfs0Header, u64 hfs0HeaderOffset, u64 hfs0HeaderSize, u32 num_entries, u32 entry_idx, bool isRoot, u32 partitionIndex, u64 *out_offset, u64 *out_size)
{
    if (hfs0Header == NULL) return false;
    
    if (entry_idx > (num_entries - 1)) return false;
    
    if ((HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * num_entries)) > hfs0HeaderSize) return false;
    
    hfs0_entry_table *entryTable = calloc(num_entries, sizeof(hfs0_entry_table));
    if (!entryTable) return false;
    
    memcpy(entryTable, hfs0Header + HFS0_ENTRY_TABLE_ADDR, sizeof(hfs0_entry_table) * num_entries);
    
    // Determine the partition index that's going to be used for offset calculation
    // If we're dealing with a root HFS0 header, just use entry_idx
    // Otherwise, partitionIndex must be used, because entry_idx represents the file entry we must look for in the provided HFS0 partition header
    u32 part_idx = (isRoot ? entry_idx : partitionIndex);
    
    switch(part_idx)
    {
        case 0: // Update (contained within IStorage instance with partition ID 0)
        case 1: // Normal or Logo (depending on the gamecard type) (contained within IStorage instance with partition ID 0)
            // Root HFS0: the header offset used to calculate the partition offset is relative to the true gamecard image start
            // Partition HFS0: the header offset used to calculate the file offset is also relative to the true gamecard image start (but it was calculated in a previous call to this function)
            *out_offset = (hfs0HeaderOffset + hfs0HeaderSize + entryTable[entry_idx].file_offset);
            break;
        case 2:
            // Check if we're dealing with a type 0x01 gamecard
            if (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT)
            {
                // Secure (contained within IStorage instance with partition ID 1)
                // Root HFS0: the resulting partition offset must be zero, because the secure partition is stored in a different IStorage instance
                // Partition HFS0: the resulting file offset is relative to the start of the IStorage instance. Thus, it isn't necessary to use the header offset as part of the calculation
                *out_offset = (isRoot ? 0 : (hfs0HeaderSize + entryTable[entry_idx].file_offset));
            } else {
                // Normal (contained within IStorage instance with partition ID 0)
                // Root HFS0: the header offset used to calculate the partition offset is relative to the true gamecard image start
                // Partition HFS0: the header offset used to calculate the file offset is also relative to the true gamecard image start (but it was calculated in a previous call to this function)
                *out_offset = (hfs0HeaderOffset + hfs0HeaderSize + entryTable[entry_idx].file_offset);
            }
            break;
        case 3: // Secure (gamecard type 0x02) (contained within IStorage instance with partition ID 1)
            // Root HFS0: the resulting partition offset must be zero, because the secure partition is stored in a different IStorage instance
            // Partition HFS0: the resulting file offset is relative to the start of the IStorage instance. Thus, it isn't necessary to use the header offset as part of the calculation
            *out_offset = (isRoot ? 0 : (hfs0HeaderSize + entryTable[entry_idx].file_offset));
            break;
        default:
            break;
    }
    
    // Store the file size for the desired HFS0 entry
    *out_size = entryTable[entry_idx].file_size;
    
    free(entryTable);
    
    return true;
}

bool getPartitionHfs0Header(u32 partition)
{
    if (hfs0_header == NULL) return false;
    
    if (partitionHfs0Header != NULL)
    {
        free(partitionHfs0Header);
        partitionHfs0Header = NULL;
        partitionHfs0HeaderOffset = 0;
        partitionHfs0HeaderSize = 0;
        partitionHfs0FileCount = 0;
        partitionHfs0StrTableSize = 0;
    }
    
    u8 buf[MEDIA_UNIT_SIZE] = {0};
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    u64 partitionSize = 0;
    u32 magic = 0;
    bool success = false;
    
    if (getHfs0EntryDetails(hfs0_header, hfs0_offset, hfs0_size, hfs0_partition_cnt, partition, true, 0, &partitionHfs0HeaderOffset, &partitionSize))
    {
        workaroundPartitionZeroAccess();
        
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            breaks++;*/
            
            // Same ugly hack from dumpRawHfs0Partition()
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
            {
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks++;*/
                
                // First read MEDIA_UNIT_SIZE bytes
                if (R_SUCCEEDED(result = fsStorageRead(&gameCardStorage, partitionHfs0HeaderOffset, buf, MEDIA_UNIT_SIZE)))
                {
                    // Check the HFS0 magic word
                    memcpy(&magic, buf, sizeof(u32));
                    magic = bswap_32(magic);
                    if (magic == HFS0_MAGIC)
                    {
                        // Calculate the size for the partition HFS0 header
                        memcpy(&partitionHfs0FileCount, buf + HFS0_FILE_COUNT_ADDR, sizeof(u32));
                        memcpy(&partitionHfs0StrTableSize, buf + HFS0_STR_TABLE_SIZE_ADDR, sizeof(u32));
                        partitionHfs0HeaderSize = (HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * partitionHfs0FileCount) + partitionHfs0StrTableSize);
                        
                        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u HFS0 header offset (relative to IStorage instance): 0x%016lX", partition, partitionHfs0HeaderOffset);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        breaks++;
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u HFS0 header size: %lu bytes", partition, partitionHfs0HeaderSize);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        breaks++;
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u file count: %u", partition, partitionHfs0FileCount);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        breaks++;
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u string table size: %u bytes", partition, partitionHfs0StrTableSize);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        breaks++;
                        
                        uiRefreshDisplay();*/
                        
                        // Round up the partition HFS0 header size to a MEDIA_UNIT_SIZE bytes boundary
                        partitionHfs0HeaderSize = round_up(partitionHfs0HeaderSize, MEDIA_UNIT_SIZE);
                        
                        partitionHfs0Header = malloc(partitionHfs0HeaderSize);
                        if (partitionHfs0Header)
                        {
                            // Check if we were dealing with the correct header size all along
                            if (partitionHfs0HeaderSize == MEDIA_UNIT_SIZE)
                            {
                                // Just copy what we already have
                                memcpy(partitionHfs0Header, buf, MEDIA_UNIT_SIZE);
                                success = true;
                            } else {
                                // Read the whole HFS0 header
                                if (R_SUCCEEDED(result = fsStorageRead(&gameCardStorage, partitionHfs0HeaderOffset, partitionHfs0Header, partitionHfs0HeaderSize)))
                                {
                                    success = true;
                                } else {
                                    free(partitionHfs0Header);
                                    partitionHfs0Header = NULL;
                                    partitionHfs0HeaderOffset = 0;
                                    partitionHfs0HeaderSize = 0;
                                    partitionHfs0FileCount = 0;
                                    partitionHfs0StrTableSize = 0;
                                    
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionHfs0HeaderOffset);
                                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                }
                            }
                            
                            /*if (success)
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u HFS0 header successfully retrieved!", partition);
                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                            }*/
                        } else {
                            partitionHfs0HeaderOffset = 0;
                            partitionHfs0HeaderSize = 0;
                            partitionHfs0FileCount = 0;
                            partitionHfs0StrTableSize = 0;
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to allocate memory for the HFS0 header from partition #%u!", partition);
                            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        }
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Magic word mismatch! 0x%08X != 0x%08X", magic, HFS0_MAGIC);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    }
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionHfs0HeaderOffset);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                }
                
                fsStorageClose(&gameCardStorage);
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    } else {
        uiDrawString("Error: unable to get partition details from the root HFS0 header!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    return success;
}

bool getHfs0FileList(u32 partition)
{
    if (!getPartitionHfs0Header(partition))
    {
        breaks += 2;
        return false;
    }
    
    if (!partitionHfs0Header)
    {
        uiDrawString("HFS0 partition header information unavailable!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (!partitionHfs0FileCount)
    {
        uiDrawString("The selected partition is empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (partitionHfs0FileCount > FILENAME_MAX_CNT)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "HFS0 partition contains more than %u files! (%u entries)", FILENAME_MAX_CNT, partitionHfs0FileCount);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    hfs0_entry_table *entryTable = calloc(partitionHfs0FileCount, sizeof(hfs0_entry_table));
    if (!entryTable)
    {
        uiDrawString("Unable to allocate memory for the HFS0 file entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    memcpy(entryTable, partitionHfs0Header + HFS0_ENTRY_TABLE_ADDR, sizeof(hfs0_entry_table) * partitionHfs0FileCount);
    
    memset(filenameBuffer, 0, FILENAME_BUFFER_SIZE);
    
    int i;
    int max_elements = (int)partitionHfs0FileCount;
    char *nextFilename = filenameBuffer;
    
    filenamesCount = 0;
    
    for(i = 0; i < max_elements; i++)
    {
        u32 filename_offset = (HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * partitionHfs0FileCount) + entryTable[i].filename_offset);
        addStringToFilenameBuffer((char*)partitionHfs0Header + filename_offset, &nextFilename);
    }
    
    free(entryTable);
    
    breaks += 2;
    
    return true;
}

// Used to retrieve tik/cert files from the HFS0 Secure partition
bool getPartitionHfs0FileByName(FsStorage *gameCardStorage, const char *filename, u8 *outBuf, u64 outBufSize)
{
    if (!partitionHfs0Header || !partitionHfs0FileCount || !partitionHfs0HeaderSize || !gameCardStorage || !filename || !outBuf || !outBufSize)
    {
        uiDrawString("Error: invalid parameters to retrieve file from HFS0 partition!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return NULL;
    }
    
    bool success = false, proceed = true, found = false;
    
    u32 i;
    Result result;
    hfs0_entry_table tmp_hfs0_entry;
    
    for(i = 0; i < partitionHfs0FileCount; i++)
    {
        memcpy(&tmp_hfs0_entry, partitionHfs0Header + (u64)HFS0_ENTRY_TABLE_ADDR + ((u64)i * sizeof(hfs0_entry_table)), sizeof(hfs0_entry_table));
        
        if (!strncasecmp((char*)partitionHfs0Header + (u64)HFS0_ENTRY_TABLE_ADDR + ((u64)partitionHfs0FileCount * sizeof(hfs0_entry_table)) + (u64)tmp_hfs0_entry.filename_offset, filename, strlen(filename)))
        {
            found = true;
            
            if (outBufSize > tmp_hfs0_entry.file_size)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: file \"%s\" is smaller than expected! (0x%016lX < 0x%016lX)", filename, tmp_hfs0_entry.file_size, outBufSize);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                proceed = false;
                break;
            }
            
            if (R_FAILED(result = fsStorageRead(gameCardStorage, partitionHfs0HeaderSize + tmp_hfs0_entry.file_offset, outBuf, outBufSize)))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to read file \"%s\" from the HFS0 partition!", filename);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                proceed = false;
                break;
            }
            
            success = true;
            
            break;
        }
    }
    
    if (proceed && !found)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to find file \"%s\" in the HFS0 partition!", filename);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    return success;
}

bool calculateExeFsExtractedDataSize(u64 *out)
{
    if (!exeFsContext.exefs_header.file_cnt || !exeFsContext.exefs_entries || !out)
    {
        uiDrawString("Error: invalid parameters to calculate extracted data size for the ExeFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
        uiDrawString("Error: invalid parameters to calculate extracted data size for the RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
        uiDrawString("Error: invalid parameters to calculate extracted size for the current RomFS directory!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u64 totalSize = 0, childDirSize = 0;
    romfs_file *fileEntry = NULL;
    romfs_dir *childDirEntry = NULL;
    romfs_dir *dirEntry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
    
    // Check if we're dealing with a nameless directory that's not the root directory
    if (!dirEntry->nameLen && dir_offset > 0)
    {
        uiDrawString("Error: directory entry without name in RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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

bool readProgramNcaExeFsOrRomFs(u32 titleIndex, bool usePatch, bool readRomFs)
{
    Result result;
    u32 i = 0;
    u32 written = 0;
    u32 total = 0;
    u32 titleCount = 0;
    u32 ncmTitleIndex = 0;
    u32 titleNcaCount = 0;
    u32 partition = 0;
    
    FsStorageId curStorageId;
    
    NcmContentMetaDatabase ncmDb;
    memset(&ncmDb, 0, sizeof(NcmContentMetaDatabase));
    
    NcmContentMetaRecordsHeader contentRecordsHeader;
    memset(&contentRecordsHeader, 0, sizeof(NcmContentMetaRecordsHeader));
    
    u64 contentRecordsHeaderReadSize = 0;
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    NcmApplicationContentMetaKey *titleList = NULL;
    NcmContentRecord *titleContentRecords = NULL;
    size_t titleListSize = sizeof(NcmApplicationContentMetaKey);
    
    NcmNcaId ncaId;
    char ncaIdStr[33] = {'\0'};
    u8 ncaHeader[NCA_FULL_HEADER_LENGTH] = {0};
    nca_header_t dec_nca_header;
    
    u8 decrypted_nca_keys[NCA_KEY_AREA_SIZE];
    
    bool success = false, foundProgram = false;
    
    if ((!usePatch && !titleAppStorageId) || (usePatch && !titlePatchStorageId))
    {
        uiDrawString("Error: title storage ID unavailable!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    curStorageId = (!usePatch ? titleAppStorageId[titleIndex] : titlePatchStorageId[titleIndex]);
    
    if (curStorageId != FsStorageId_GameCard && curStorageId != FsStorageId_SdCard && curStorageId != FsStorageId_NandUser)
    {
        uiDrawString("Error: invalid title storage ID!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    switch(curStorageId)
    {
        case FsStorageId_GameCard:
            titleCount = (!usePatch ? titleAppCount : titlePatchCount);
            ncmTitleIndex = titleIndex;
            break;
        case FsStorageId_SdCard:
            titleCount = (!usePatch ? sdCardTitleAppCount : sdCardTitlePatchCount);
            
            if (menuType == MENUTYPE_SDCARD_EMMC)
            {
                ncmTitleIndex = titleIndex;
            } else {
                // Patches loaded using loadPatchesFromSdCardAndEmmc()
                ncmTitleIndex = (titleIndex - (titlePatchCount - gameCardSdCardEmmcPatchCount)); // Substract gamecard patch count
            }
            
            break;
        case FsStorageId_NandUser:
            titleCount = (!usePatch ? nandUserTitleAppCount : nandUserTitlePatchCount);
            
            if (menuType == MENUTYPE_SDCARD_EMMC)
            {
                ncmTitleIndex = (titleIndex - (!usePatch ? sdCardTitleAppCount : sdCardTitlePatchCount)); // Substract SD card patch count
            } else {
                // Patches loaded using loadPatchesFromSdCardAndEmmc()
                ncmTitleIndex = (titleIndex - ((titlePatchCount - gameCardSdCardEmmcPatchCount) + sdCardTitlePatchCount)); // Substract gamecard + SD card patch count
            }
            
            break;
        default:
            break;
    }
    
    if (!titleCount)
    {
        uiDrawString("Error: invalid title type count!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (ncmTitleIndex > (titleCount - 1))
    {
        uiDrawString("Error: invalid title index!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    titleListSize *= titleCount;
    
    // If we're dealing with a gamecard, call workaroundPartitionZeroAccess() and read the secure partition header. Otherwise, ncmContentStorageReadContentIdFile() will fail with error 0x00171002
    if (curStorageId == FsStorageId_GameCard && !partitionHfs0Header)
    {
        partition = (hfs0_partition_cnt - 1); // Select the secure partition
        
        workaroundPartitionZeroAccess();
        
        if (!getPartitionHfs0Header(partition))
        {
            breaks += 2;
            return false;
        }
        
        if (!partitionHfs0FileCount)
        {
            uiDrawString("The Secure HFS0 partition is empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
    }
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Looking for the Program NCA (%s)...", (!usePatch ? "base application" : "update"));
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    titleList = calloc(1, titleListSize);
    if (!titleList)
    {
        uiDrawString("Error: unable to allocate memory for the ApplicationContentMetaKey struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentMetaDatabase(curStorageId, &ncmDb)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmOpenContentMetaDatabase failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    u8 filter = (!usePatch ? META_DB_REGULAR_APPLICATION : META_DB_PATCH);
    
    if (R_FAILED(result = ncmContentMetaDatabaseListApplication(&ncmDb, filter, titleList, titleListSize, &written, &total)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseListApplication failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (!written || !total)
    {
        uiDrawString("Error: ncmContentMetaDatabaseListApplication wrote no entries to output buffer!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (written != total)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: title count mismatch in ncmContentMetaDatabaseListApplication (%u != %u)", written, total);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseGet(&ncmDb, &(titleList[ncmTitleIndex].metaRecord), sizeof(NcmContentMetaRecordsHeader), &contentRecordsHeader, &contentRecordsHeaderReadSize)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseGet failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    titleNcaCount = (u32)(contentRecordsHeader.numContentRecords);
    
    titleContentRecords = calloc(titleNcaCount, sizeof(NcmContentRecord));
    if (!titleContentRecords)
    {
        uiDrawString("Error: unable to allocate memory for the ContentRecord struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseListContentInfo(&ncmDb, &(titleList[ncmTitleIndex].metaRecord), 0, titleContentRecords, titleNcaCount * sizeof(NcmContentRecord), &written)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseListContentInfo failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentStorage(curStorageId, &ncmStorage)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmOpenContentStorage failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    for(i = 0; i < titleNcaCount; i++)
    {
        if (titleContentRecords[i].type == NcmContentType_Program)
        {
            memcpy(&ncaId, &(titleContentRecords[i].ncaId), sizeof(NcmNcaId));
            convertDataToHexString(titleContentRecords[i].ncaId.c, 16, ncaIdStr, 33);
            foundProgram = true;
            break;
        }
    }
    
    if (!foundProgram)
    {
        uiDrawString("Error: unable to find Program NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Found Program NCA: \"%s.nca\".", ncaIdStr);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks += 2;
    
    /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Retrieving %s...", (!readRomFs ? "ExeFS entries" : "RomFS entry tables"));
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;*/
    
    if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, 0, ncaHeader, NCA_FULL_HEADER_LENGTH)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read header from Program NCA! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Decrypt the NCA header
    if (!decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header, NULL, decrypted_nca_keys, (curStorageId != FsStorageId_GameCard || (curStorageId == FsStorageId_GameCard && usePatch)))) goto out;
    
    if (curStorageId == FsStorageId_GameCard && !usePatch)
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
            uiDrawString("Error: Rights ID field in Program NCA header not empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
    }
    
    if (!readRomFs)
    {
        // Read file entries from the ExeFS section
        if (!readExeFsEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys)) goto out;
    } else {
        if (!usePatch)
        {
            // Read directory and file tables from the RomFS section
            if (!readRomFsEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys)) goto out;
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
            
            if (i == titleAppCount)
            {
                uiDrawString("Error: unable to find base application title index for the selected update!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                goto out;
            }
            
            // Read directory and file tables from the RomFS section in the Program NCA from the base application
            if (!readProgramNcaExeFsOrRomFs(appIndex, false, true)) goto out;
            
            // Read BKTR entry data in the Program NCA from the update
            if (!readBktrEntryFromNca(&ncmStorage, &ncaId, &dec_nca_header, decrypted_nca_keys)) goto out;
        }
    }
    
    success = true;
    
out:
    if (titleContentRecords) free(titleContentRecords);
    
    if (!success) serviceClose(&(ncmStorage.s));
    
    serviceClose(&(ncmDb.s));
    
    if (titleList) free(titleList);
    
    if (curStorageId == FsStorageId_GameCard)
    {
        if (partitionHfs0Header)
        {
            free(partitionHfs0Header);
            partitionHfs0Header = NULL;
            partitionHfs0HeaderOffset = 0;
            partitionHfs0HeaderSize = 0;
            partitionHfs0FileCount = 0;
            partitionHfs0StrTableSize = 0;
        }
    }
    
    return success;
}

bool getExeFsFileList()
{
    if (!exeFsContext.exefs_header.file_cnt || !exeFsContext.exefs_entries || !exeFsContext.exefs_str_table)
    {
        uiDrawString("Error: invalid parameters to retrieve ExeFS section filelist!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (exeFsContext.exefs_header.file_cnt > FILENAME_MAX_CNT)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "ExeFS section contains more than %u entries! (%u entries)", FILENAME_MAX_CNT, exeFsContext.exefs_header.file_cnt);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u32 i;
    
    memset(filenameBuffer, 0, FILENAME_BUFFER_SIZE);
    filenamesCount = 0;
    
    char *nextFilename = filenameBuffer;
    
    for(i = 0; i < exeFsContext.exefs_header.file_cnt; i++)
    {
        char *cur_filename = (exeFsContext.exefs_str_table + exeFsContext.exefs_entries[i].filename_offset);
        addStringToFilenameBuffer(cur_filename, &nextFilename);
    }
    
    return true;
}

bool getRomFsParentDir(u32 dir_offset, bool usePatch, u32 *out)
{
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries)))
    {
        uiDrawString("Error: invalid parameters to retrieve parent RomFS section directory!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
        uiDrawString("Error: invalid parameters to generate current RomFS section path!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Generate current path if we're not dealing with the root directory
    if (dir_offset)
    {
        romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
        
        if (!entry->nameLen)
        {
            uiDrawString("Error: directory entry without name in RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
    
    if (romFsBrowserEntries != NULL)
    {
        free(romFsBrowserEntries);
        romFsBrowserEntries = NULL;
    }
    
    memset(curRomFsPath, 0, NAME_BUF_LEN);
    
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries || !bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)))
    {
        uiDrawString("Error: invalid parameters to retrieve RomFS section filelist!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
            uiDrawString("Error: directory entry without name in RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
            uiDrawString("Error: file entry without name in RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
        
        // Only add entries inside the directory we're looking in
        if (entry->parent == dir_offset) fileEntryCnt++;
        
        entryOffset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
    }
    
    totalEntryCnt = (dirEntryCnt + fileEntryCnt);
    
    if (totalEntryCnt > FILENAME_MAX_CNT)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Current RomFS dir contains more than %u entries! (%u entries)", FILENAME_MAX_CNT, totalEntryCnt);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Allocate memory for our entries
    romFsBrowserEntries = calloc(totalEntryCnt, sizeof(romfs_browser_entry));
    if (!romFsBrowserEntries)
    {
        uiDrawString("Error: unable to allocate memory for file/dir attributes in RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    memset(filenameBuffer, 0, FILENAME_BUFFER_SIZE);
    filenamesCount = 0;
    
    char *nextFilename = filenameBuffer;
    
    char curName[NAME_BUF_LEN] = {'\0'};
    
    // Add parent directory entry ("..")
    romFsBrowserEntries[0].type = ROMFS_ENTRY_DIR;
    romFsBrowserEntries[0].offset = romFsParentDir;
    addStringToFilenameBuffer("..", &nextFilename);
    
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
                u32 strWidth = uiGetStrWidth(curName);
                
                if ((BROWSER_ICON_DIMENSION + 16 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    while((BROWSER_ICON_DIMENSION + 16 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                    {
                        curName[strlen(curName) - 1] = '\0';
                        strWidth = uiGetStrWidth(curName);
                    }
                    
                    strcat(curName, "...");
                }
                
                addStringToFilenameBuffer(curName, &nextFilename);
                
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
                
                snprintf(curName, entry->nameLen + 1, (char*)entry->name);
                
                // Fix entry name length
                u32 strWidth = uiGetStrWidth(curName);
                
                if ((BROWSER_ICON_DIMENSION + 16 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    while((BROWSER_ICON_DIMENSION + 16 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                    {
                        curName[strlen(curName) - 1] = '\0';
                        strWidth = uiGetStrWidth(curName);
                    }
                    
                    strcat(curName, "...");
                }
                
                addStringToFilenameBuffer(curName, &nextFilename);
                
                i++;
            }
            
            entryOffset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
        }
    }
    
    // Update current RomFS directory offset
    curRomFsDirOffset = dir_offset;
    
    return true;
}

int getSdCardFreeSpace(u64 *out)
{
    Result result;
    FsFileSystem *sdfs = NULL;
    u64 size = 0;
    int rc = 0;
    
    sdfs = fsdevGetDefaultFileSystem();
    if (!sdfs)
    {
        uiStatusMsg("getSdCardFreeSpace: fsdevGetDefaultFileSystem failed!");
        return rc;
    }
    
    if (R_SUCCEEDED(result = fsFsGetFreeSpace(sdfs, "/", &size)))
    {
        *out = size;
        rc = 1;
    } else {
        uiStatusMsg("getSdCardFreeSpace: fsFsGetFreeSpace failed! (0x%08X)", result);
    }
    
    return rc;
}

void convertSize(u64 size, char *out, int bufsize)
{
    char buffer[16];
    double bytes = (double)size;
    
    if (bytes < 1000.0)
    {
        snprintf(buffer, sizeof(buffer), "%.0lf B", bytes);
    } else
    if (bytes < 10.0*KiB)
    {
        snprintf(buffer, sizeof(buffer), "%.2lf KiB", floor((bytes*100.0)/KiB)/100.0);
    } else
    if (bytes < 100.0*KiB)
    {
        snprintf(buffer, sizeof(buffer), "%.1lf KiB", floor((bytes*10.0)/KiB)/10.0);
    } else
    if (bytes < 1000.0*KiB)
    {
        snprintf(buffer, sizeof(buffer), "%.0lf KiB", floor(bytes/KiB));
    } else
    if (bytes < 10.0*MiB)
    {
        snprintf(buffer, sizeof(buffer), "%.2lf MiB", floor((bytes*100.0)/MiB)/100.0);
    } else
    if (bytes < 100.0*MiB)
    {
        snprintf(buffer, sizeof(buffer), "%.1lf MiB", floor((bytes*10.0)/MiB)/10.0);
    } else
    if (bytes < 1000.0*MiB)
    {
        snprintf(buffer, sizeof(buffer), "%.0lf MiB", floor(bytes/MiB));
    } else
    if (bytes < 10.0*GiB)
    {
        snprintf(buffer, sizeof(buffer), "%.2lf GiB", floor((bytes*100.0)/GiB)/100.0);
    } else
    if (bytes < 100.0*GiB)
    {
        snprintf(buffer, sizeof(buffer), "%.1lf GiB", floor((bytes*10.0)/GiB)/10.0);
    } else {
        snprintf(buffer, sizeof(buffer), "%.0lf GiB", floor(bytes/GiB));
    }
    
    snprintf(out, bufsize, "%s", buffer);
}

void addStringToFilenameBuffer(const char *string, char **nextFilename)
{
    filenames[filenamesCount++] = *nextFilename;
    snprintf(*nextFilename, FILENAME_LENGTH, string);
    *nextFilename += FILENAME_LENGTH;
}

char *generateFullDumpName()
{
    if (!titleAppCount || !fixedTitleName || !titleAppTitleID || !titleAppVersion) return NULL;
    
    u32 i, j;
    
    char tmp[512] = {'\0'};
    char *fullname = NULL;
    char *fullnameTmp = NULL;
    
    size_t strsize = NAME_BUF_LEN;
    fullname = calloc(strsize, sizeof(char));
    if (!fullname) return NULL;
    
    for(i = 0; i < titleAppCount; i++)
    {
        u32 highestVersion = titleAppVersion[i];
        
        // Check if our current gamecard has any bundled updates for this application. If so, use the highest update version available
        if (titlePatchCount > 0 && titlePatchTitleID != NULL && titlePatchVersion != NULL)
        {
            for(j = 0; j < titlePatchCount; j++)
            {
                if (titlePatchTitleID[j] == (titleAppTitleID[i] | APPLICATION_PATCH_BITMASK) && titlePatchVersion[j] > highestVersion) highestVersion = titlePatchVersion[j];
            }
        }
        
        snprintf(tmp, sizeof(tmp) / sizeof(tmp[0]), "%s v%u (%016lX)", fixedTitleName[i], highestVersion, titleAppTitleID[i]);
        
        if ((strlen(fullname) + strlen(tmp) + 4) > strsize)
        {
            size_t fullname_len = strlen(fullname);
            
            strsize = (fullname_len + strlen(tmp) + 4);
            
            fullnameTmp = realloc(fullname, strsize);
            if (fullnameTmp)
            {
                fullname = fullnameTmp;
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

char *generateNSPDumpName(nspDumpType selectedNspDumpType, u32 titleIndex)
{
    if (!titleAppCount || !fixedTitleName || !titleAppTitleID || (selectedNspDumpType == DUMP_APP_NSP && !titleAppVersion) || (selectedNspDumpType == DUMP_PATCH_NSP && (!titlePatchCount || !titlePatchTitleID || !titlePatchVersion)) || (selectedNspDumpType == DUMP_ADDON_NSP && (!titleAddOnCount || !titleAddOnTitleID || !titleAddOnVersion))) return NULL;
    
    u32 app;
    bool foundApp = false;
    
    size_t strsize = NAME_BUF_LEN;
    char *fullname = calloc(strsize, sizeof(char));
    if (!fullname) return NULL;
    
    if (selectedNspDumpType == DUMP_APP_NSP)
    {
        snprintf(fullname, strsize, "%s v%u (%016lX) (BASE)", fixedTitleName[titleIndex], titleAppVersion[titleIndex], titleAppTitleID[titleIndex]);
    } else
    if (selectedNspDumpType == DUMP_PATCH_NSP)
    {
        for(app = 0; app < titleAppCount; app++)
        {
            if (titlePatchTitleID[titleIndex] == (titleAppTitleID[app] | APPLICATION_PATCH_BITMASK))
            {
                foundApp = true;
                break;
            }
        }
        
        if (foundApp)
        {
            snprintf(fullname, strsize, "%s v%u (%016lX) (UPD)", fixedTitleName[app], titlePatchVersion[titleIndex], titlePatchTitleID[titleIndex]);
        } else {
            snprintf(fullname, strsize, "%016lX v%u (UPD)", titlePatchTitleID[titleIndex], titlePatchVersion[titleIndex]);
        }
    } else
    if (selectedNspDumpType == DUMP_ADDON_NSP)
    {
        for(app = 0; app < titleAppCount; app++)
        {
            if ((titleAddOnTitleID[titleIndex] & APPLICATION_ADDON_BITMASK) == (titleAppTitleID[app] & APPLICATION_ADDON_BITMASK))
            {
                foundApp = true;
                break;
            }
        }
        
        if (foundApp)
        {
            snprintf(fullname, strsize, "%s v%u (%016lX) (DLC)", fixedTitleName[app], titleAddOnVersion[titleIndex], titleAddOnTitleID[titleIndex]);
        } else {
            snprintf(fullname, strsize, "%016lX v%u (DLC)", titleAddOnTitleID[titleIndex], titleAddOnVersion[titleIndex]);
        }
    } else {
        free(fullname);
        fullname = NULL;
    }
    
    return fullname;
}

void retrieveDescriptionForPatchOrAddOn(u64 titleID, u32 version, bool addOn, bool addAppName, const char *prefix, char *outBuf, size_t outBufSize)
{
    if (!outBuf || !outBufSize) return;
    
    char versionStr[128] = {'\0'};
    convertTitleVersionToDecimal(version, versionStr, sizeof(versionStr));
    
    if (!titleAppCount || !titleAppTitleID || !titleName || !*titleName || !addAppName)
    {
        if (prefix)
        {
            snprintf(outBuf, outBufSize, "%s%016lX v%s", prefix, titleID, versionStr);
        } else {
            snprintf(outBuf, outBufSize, "%016lX v%s", titleID, versionStr);
        }
        
        return;
    }
    
    u32 app;
    bool foundApp = false;
    
    for(app = 0; app < titleAppCount; app++)
    {
        if ((!addOn && titleID == (titleAppTitleID[app] | APPLICATION_PATCH_BITMASK)) || (addOn && (titleID & APPLICATION_ADDON_BITMASK) == (titleAppTitleID[app] & APPLICATION_ADDON_BITMASK)))
        {
            foundApp = true;
            break;
        }
    }
    
    if (foundApp)
    {
        if (prefix)
        {
            snprintf(outBuf, outBufSize, "%s%s | %016lX v%s", prefix, titleName[app], titleID, versionStr);
        } else {
            snprintf(outBuf, outBufSize, "%s | %016lX v%s", titleName[app], titleID, versionStr);
        }
    } else {
        if (prefix)
        {
            snprintf(outBuf, outBufSize, "%s%016lX v%s", prefix, titleID, versionStr);
        } else {
            snprintf(outBuf, outBufSize, "%016lX v%s", titleID, versionStr);
        }
    }
}

bool checkOrphanPatchOrAddOn(bool addOn)
{
    if (!titleAppCount || !titleAppTitleID || (!addOn && (!titlePatchCount || !titlePatchTitleID)) || (addOn && (!titleAddOnCount || !titleAddOnTitleID))) return false;
    
    u32 i, j;
    u32 count = (!addOn ? titlePatchCount : titleAddOnCount);
    
    for(i = 0; i < count; i++)
    {
        bool foundMatch = false;
        
        for(j = 0; j < titleAppCount; j++)
        {
            if ((!addOn && titlePatchTitleID[i] == (titleAppTitleID[j] | APPLICATION_PATCH_BITMASK)) || (addOn && (titleAddOnTitleID[i] & APPLICATION_ADDON_BITMASK) == (titleAppTitleID[j] & APPLICATION_ADDON_BITMASK)))
            {
                foundMatch = true;
                break;
            }
        }
        
        if (!foundMatch) return true;
    }
    
    return false;
}

void generateOrphanPatchOrAddOnList()
{
    u32 i, j;
    char *nextFilename = filenameBuffer;
    filenamesCount = 0;
    
    bool foundMatch;
    char versionStr[128] = {'\0'};
    u32 orphanEntryIndex = 0, patchCount = 0, addOnCount = 0;
    orphan_patch_addon_entry *orphanPatchEntries = NULL, *orphanAddOnEntries = NULL;
    
    if (orphanEntries != NULL)
    {
        free(orphanEntries);
        orphanEntries = NULL;
    }
    
    if ((!titlePatchCount || !titlePatchTitleID || !titlePatchVersion) && (!titleAddOnCount || !titleAddOnTitleID || !titleAddOnVersion)) return;
    
    if (titlePatchCount)
    {
        orphanPatchEntries = calloc(titlePatchCount, sizeof(orphan_patch_addon_entry));
        if (!orphanPatchEntries) return;
        
        for(i = 0; i < titlePatchCount; i++)
        {
            foundMatch = false;
            
            if (titleAppCount && titleAppTitleID)
            {
                for(j = 0; j < titleAppCount; j++)
                {
                    if (titlePatchTitleID[i] == (titleAppTitleID[j] | APPLICATION_PATCH_BITMASK))
                    {
                        foundMatch = true;
                        break;
                    }
                }
            }
            
            if (!foundMatch)
            {
                convertTitleVersionToDecimal(titlePatchVersion[i], versionStr, sizeof(versionStr));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%016lX v%s (Update)", titlePatchTitleID[i], versionStr);
                addStringToFilenameBuffer(strbuf, &nextFilename);
                
                orphanPatchEntries[orphanEntryIndex].index = i;
                orphanPatchEntries[orphanEntryIndex].type = ORPHAN_ENTRY_TYPE_PATCH;
                
                orphanEntryIndex++;
                patchCount++;
            }
        }
    }
    
    if (titleAddOnCount)
    {
        orphanAddOnEntries = calloc(titleAddOnCount, sizeof(orphan_patch_addon_entry));
        if (!orphanAddOnEntries)
        {
            if (orphanPatchEntries) free(orphanPatchEntries);
            return;
        }
        
        orphanEntryIndex = 0;
        
        for(i = 0; i < titleAddOnCount; i++)
        {
            foundMatch = false;
            
            if (titleAppCount && titleAppTitleID)
            {
                for(j = 0; j < titleAppCount; j++)
                {
                    if ((titleAddOnTitleID[i] & APPLICATION_ADDON_BITMASK) == (titleAppTitleID[j] & APPLICATION_ADDON_BITMASK))
                    {
                        foundMatch = true;
                        break;
                    }
                }
            }
            
            if (!foundMatch)
            {
                convertTitleVersionToDecimal(titleAddOnVersion[i], versionStr, sizeof(versionStr));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%016lX v%s (DLC)", titleAddOnTitleID[i], versionStr);
                addStringToFilenameBuffer(strbuf, &nextFilename);
                
                orphanAddOnEntries[orphanEntryIndex].index = i;
                orphanAddOnEntries[orphanEntryIndex].type = ORPHAN_ENTRY_TYPE_ADDON;
                
                orphanEntryIndex++;
                addOnCount++;
            }
        }
    }
    
    filenamesCount = (patchCount + addOnCount);
    
    orphanEntries = calloc(filenamesCount, sizeof(orphan_patch_addon_entry));
    if (!orphanEntries)
    {
        filenamesCount = 0;
        if (orphanPatchEntries) free(orphanPatchEntries);
        if (orphanAddOnEntries) free(orphanAddOnEntries);
        return;
    }
    
    if (titlePatchCount)
    {
        if (patchCount) memcpy(orphanEntries, orphanPatchEntries, patchCount * sizeof(orphan_patch_addon_entry));
        
        free(orphanPatchEntries);
    }
    
    if (titleAddOnCount)
    {
        if (addOnCount) memcpy(orphanEntries + patchCount, orphanAddOnEntries, addOnCount * sizeof(orphan_patch_addon_entry));
        
        free(orphanAddOnEntries);
    }
}

bool checkIfBaseApplicationHasPatchOrAddOn(u32 appIndex, bool addOn)
{
    if (!titleAppCount || appIndex > (titleAppCount - 1) || !titleAppTitleID || (!addOn && (!titlePatchCount || !titlePatchTitleID)) || (addOn && (!titleAddOnCount || !titleAddOnTitleID))) return false;
    
    u32 i;
    u32 count = (!addOn ? titlePatchCount : titleAddOnCount);
    
    for(i = 0; i < count; i++)
    {
        if ((!addOn && (titleAppTitleID[appIndex] | APPLICATION_PATCH_BITMASK) == titlePatchTitleID[i]) || (addOn && (titleAppTitleID[appIndex] & APPLICATION_ADDON_BITMASK) == (titleAddOnTitleID[i] & APPLICATION_ADDON_BITMASK))) return true;
    }
    
    return false;
}

bool checkIfPatchOrAddOnBelongsToBaseApplication(u32 titleIndex, u32 appIndex, bool addOn)
{
    if (!titleAppCount || appIndex > (titleAppCount - 1) || !titleAppTitleID || (!addOn && (!titlePatchCount || titleIndex > (titlePatchCount - 1) || !titlePatchTitleID)) || (addOn && (!titleAddOnCount || titleIndex > (titleAddOnCount - 1) || !titleAddOnTitleID))) return false;
    
    if ((!addOn && titlePatchTitleID[titleIndex] == (titleAppTitleID[appIndex] | APPLICATION_PATCH_BITMASK)) || (addOn && (titleAddOnTitleID[titleIndex] & APPLICATION_ADDON_BITMASK) == (titleAppTitleID[appIndex] & APPLICATION_ADDON_BITMASK))) return true;
    
    return false;
}

u32 retrieveFirstPatchOrAddOnIndexFromBaseApplication(u32 appIndex, bool addOn)
{
    if (!titleAppCount || appIndex > (titleAppCount - 1) || !titleAppTitleID || (!addOn && (!titlePatchCount || !titlePatchTitleID)) || (addOn && (!titleAddOnCount || !titleAddOnTitleID))) return 0;
    
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
    
    if (!titleAppCount || appIndex > (titleAppCount - 1) || !titleAppTitleID || !startTitleIndex || startTitleIndex >= count || (!addOn && (!titlePatchCount || !titlePatchTitleID)) || (addOn && (!titleAddOnCount || !titleAddOnTitleID))) return retTitleIndex;
    
    for(curTitleIndex = startTitleIndex; curTitleIndex > 0; curTitleIndex--)
    {
        if (checkIfPatchOrAddOnBelongsToBaseApplication((curTitleIndex - 1), appIndex, addOn))
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
    
    if (!titleAppCount || appIndex > (titleAppCount - 1) || !titleAppTitleID || startTitleIndex >= count || (!addOn && (!titlePatchCount || !titlePatchTitleID)) || (addOn && (!titleAddOnCount || !titleAddOnTitleID))) return retTitleIndex;
    
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

void waitForButtonPress()
{
    uiDrawString("Press any button to continue", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    
    while(true)
    {
        hidScanInput();
        
        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        
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
        
        progressCtx->lastSpeed = (((double)(progressCtx->curOffset + chunkSize) / (double)DUMP_BUFFER_SIZE) / (double)(progressCtx->now - progressCtx->start));
        progressCtx->averageSpeed = ((SMOOTHING_FACTOR * progressCtx->lastSpeed) + ((1 - SMOOTHING_FACTOR) * progressCtx->averageSpeed));
        if (!isnormal(progressCtx->averageSpeed)) progressCtx->averageSpeed = SMOOTHING_FACTOR; // Very low values
        
        progressCtx->remainingTime = (u64)(((double)(progressCtx->totalSize - (progressCtx->curOffset + chunkSize)) / (double)DUMP_BUFFER_SIZE) / progressCtx->averageSpeed);
        
        progressCtx->progress = (u8)(((progressCtx->curOffset + chunkSize) * 100) / progressCtx->totalSize);
    }
    
    formatETAString(progressCtx->remainingTime, progressCtx->etaInfo, sizeof(progressCtx->etaInfo) / sizeof(progressCtx->etaInfo[0]));
    
    convertSize(progressCtx->curOffset + chunkSize, progressCtx->curOffsetStr, sizeof(progressCtx->curOffsetStr) / sizeof(progressCtx->curOffsetStr[0]));
    
    uiFill(0, (progressCtx->line_offset * (font_height + (font_height / 4))) + 8, FB_WIDTH / 4, (font_height + (font_height / 4)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%.2lf MiB/s [ETA: %s]", progressCtx->averageSpeed, progressCtx->etaInfo);
    uiDrawString(strbuf, font_height * 2, (progressCtx->line_offset * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    uiFill(FB_WIDTH / 4, (progressCtx->line_offset * (font_height + (font_height / 4))) + 10, FB_WIDTH / 2, (font_height + (font_height / 4)), 0, 0, 0);
    uiFill(FB_WIDTH / 4, (progressCtx->line_offset * (font_height + (font_height / 4))) + 10, (((progressCtx->curOffset + chunkSize) * (u64)(FB_WIDTH / 2)) / progressCtx->totalSize), (font_height + (font_height / 4)), 0, 255, 0);
    
    uiFill(FB_WIDTH - (FB_WIDTH / 4), (progressCtx->line_offset * (font_height + (font_height / 4))) + 8, FB_WIDTH / 4, (font_height + (font_height / 4)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progressCtx->progress, progressCtx->curOffsetStr, progressCtx->totalSizeStr);
    uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (progressCtx->line_offset * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    uiRefreshDisplay();
}

void setProgressBarError(progress_ctx_t *progressCtx)
{
    if (!progressCtx) return;
    
    uiFill(FB_WIDTH / 4, (progressCtx->line_offset * (font_height + (font_height / 4))) + 10, FB_WIDTH / 2, (font_height + (font_height / 4)), 0, 0, 0);
    uiFill(FB_WIDTH / 4, (progressCtx->line_offset * (font_height + (font_height / 4))) + 10, (((u32)progressCtx->progress * ((u32)FB_WIDTH / 2)) / 100), (font_height + (font_height / 4)), 255, 0, 0);
}

bool cancelProcessCheck(progress_ctx_t *progressCtx)
{
    if (!progressCtx) return false;
    
    hidScanInput();
    
    progressCtx->cancelBtnState = (hidKeysHeld(CONTROLLER_P1_AUTO) & KEY_B);
    
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
        uiDrawString(message, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;
    }
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "[ %s ] Yes | [ %s ] No", NINTENDO_FONT_A, NINTENDO_FONT_B);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    uiRefreshDisplay();
    
    bool ret = false;
    
    while(true)
    {
        hidScanInput();
        
        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        
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
    pfs0_entry_table *nspEntries = NULL;
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
    
    if (read_bytes != sizeof(pfs0_header) || bswap_32(nspHeader.magic) != PFS0_MAGIC || nspSize < (sizeof(pfs0_header) + (sizeof(pfs0_entry_table) * (u64)nspHeader.file_cnt) + (u64)nspHeader.str_table_size))
    {
        fclose(nspFile);
        return false;
    }
    
    nspEntries = calloc((u64)nspHeader.file_cnt, sizeof(pfs0_entry_table));
    if (!nspEntries)
    {
        fclose(nspFile);
        return false;
    }
    
    read_bytes = fread(nspEntries, 1, sizeof(pfs0_entry_table) * (u64)nspHeader.file_cnt, nspFile);
    
    if (read_bytes != (sizeof(pfs0_entry_table) * (u64)nspHeader.file_cnt))
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
            tikOffset = (sizeof(pfs0_header) + (sizeof(pfs0_entry_table) * (u64)nspHeader.file_cnt) + (u64)nspHeader.str_table_size + nspEntries[i].file_offset);
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
    
    if (!strncmp(tikData.sig_issuer, "Root-CA00000003-XS00000021", 26) || memcmp(titlekey_block_0x190_hash, titlekey_block_0x190_empty_hash, 0x20) != 0 || tikData.titlekey_type != ETICKET_TITLEKEY_COMMON || tikData.ticket_id != 0 || tikData.device_id != 0 || tikData.account_id != 0) return true;
    
    return false;
}

void removeDirectoryWithVerbose(const char *path, const char *msg)
{
    if (!path || !strlen(path) || !msg || !strlen(msg)) return;
    
    breaks += 2;
    
    uiDrawString(msg, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    
    fsdevDeleteDirectoryRecursively(path);
    
    uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 2)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    uiRefreshDisplay();
    
    breaks -= 2;
}

bool parseNSWDBRelease(xmlDocPtr doc, xmlNodePtr cur, u64 gc_tid, u32 crc)
{
    if (!doc || !cur) return false;
    
    xmlChar *key;
    xmlNodePtr node = cur;
    
    u8 imageSize = (u8)(gameCardSize / GAMECARD_SIZE_1GiB);
    
    u8 xmlImageSize = 0;
    u64 xmlTitleID = 0;
    u32 xmlCrc = 0;
    char xmlReleaseName[256] = {'\0'};
    
    bool found = false;
    
    while(node != NULL)
    {
        if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenImageSize)))
        {
            key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (key)
            {
                xmlImageSize = (u8)atoi((const char*)key);
                xmlFree(key);
            }
        } else
        if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenTitleID)))
        {
            key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (key)
            {
                xmlTitleID = strtoull((const char*)key, NULL, 16);
                xmlFree(key);
            }
        } else
        if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenImgCrc)))
        {
            key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (key)
            {
                xmlCrc = strtoul((const char*)key, NULL, 16);
                xmlFree(key);
            }
        }
        if ((!xmlStrcmp(node->name, (const xmlChar *)nswReleasesChildrenReleaseName)))
        {
            key = xmlNodeListGetString(doc, node->xmlChildrenNode, 1);
            if (key)
            {
                snprintf(xmlReleaseName, sizeof(xmlReleaseName) / sizeof(xmlReleaseName[0]), "%s", (char*)key);
                xmlFree(key);
            }
        }
        
        node = node->next;
    }
    
    /*if (xmlImageSize && xmlTitleID && strlen(xmlReleaseName))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XML Image Size: %u.", xmlImageSize);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XML Title ID: %016lX.", xmlTitleID);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XML Image CRC32: %08X.", xmlCrc);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XML Release Name: %s.", xmlReleaseName);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks += 2;
    }*/
    
    if (xmlImageSize == imageSize && xmlTitleID == gc_tid && xmlCrc == crc)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Found matching Scene release: \"%s\" (CRC32: %08X). This is a good dump!", xmlReleaseName, xmlCrc);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
        found = true;
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump doesn't match Scene release: \"%s\"! (CRC32: %08X)", xmlReleaseName, xmlCrc);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    breaks++;
    
    return found;
}

xmlXPathObjectPtr getNodeSet(xmlDocPtr doc, xmlChar *xpath)
{
    xmlXPathContextPtr context = NULL;
    xmlXPathObjectPtr result = NULL;
    
    context = xmlXPathNewContext(doc);
    result = xmlXPathEvalExpression(xpath, context);
    
    if (xmlXPathNodeSetIsEmpty(result->nodesetval))
    {
        xmlXPathFreeObject(result);
        return NULL;
    }
    
    return result;
}

void gameCardDumpNSWDBCheck(u32 crc)
{
    if (!titleAppCount || !titleAppTitleID || !hfs0_partition_cnt) return;
    
    xmlDocPtr doc = NULL;
    bool found = false;
    
    doc = xmlParseFile(nswReleasesXmlPath);
    if (doc)
    {
        u32 i;
        for(i = 0; i < titleAppCount; i++)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "//%s/%s[.//%s='%016lX']", nswReleasesRootElement, nswReleasesChildren, nswReleasesChildrenTitleID, titleAppTitleID[i]);
            xmlXPathObjectPtr nodeSet = getNodeSet(doc, (xmlChar*)strbuf);
            if (nodeSet)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Found %d %s with Title ID \"%016lX\".", nodeSet->nodesetval->nodeNr, (nodeSet->nodesetval->nodeNr > 1 ? "releases" : "release"), titleAppTitleID[i]);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks++;
                
                uiRefreshDisplay();
                
                u32 i;
                for(i = 0; i < nodeSet->nodesetval->nodeNr; i++)
                {
                    xmlNodePtr node = nodeSet->nodesetval->nodeTab[i]->xmlChildrenNode;
                    
                    found = parseNSWDBRelease(doc, node, titleAppTitleID[i], crc);
                    if (found) break;
                }
                
                xmlXPathFreeObject(nodeSet);
                
                if (!found)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "No checksum matches found in XML document for Title ID \"%016lX\"!", titleAppTitleID[i]);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    if ((i + 1) < titleAppCount) breaks += 2;
                } else {
                    breaks--;
                    break;
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "No records with Title ID \"%016lX\" found within the XML document!", titleAppTitleID[i]);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                if ((i + 1) < titleAppCount) breaks += 2;
            }
        }
        
        xmlFreeDoc(doc);
        
        if (!found)
        {
            breaks++;
            uiDrawString("This could either be a bad dump or an undumped cartridge.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to open and/or parse \"%s\"!", nswReleasesXmlPath);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
}

Result networkInit()
{
    Result result = socketInitializeDefault();
    if (R_SUCCEEDED(result)) curl_global_init(CURL_GLOBAL_ALL);
    return result;
}

void networkDeinit()
{
    curl_global_cleanup();
    socketExit();
}

size_t writeCurlFile(char *buffer, size_t size, size_t number_of_items, void *input_stream)
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
    
    while (result_written + bsz > result_sz) 
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

void updateNSWDBXml()
{
    Result result;
    CURL *curl;
    CURLcode res;
    long http_code = 0;
    double size = 0.0;
    bool success = false;
    
    if (R_SUCCEEDED(result = networkInit()))
    {
        curl = curl_easy_init();
        if (curl)
        {
            FILE *nswdbXml = fopen(nswReleasesXmlTmpPath, "wb");
            if (nswdbXml)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Downloading XML database from \"%s\", please wait...", nswReleasesXmlUrl);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks += 2;
                
                if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                {
                    uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    breaks += 2;
                }
                
                uiRefreshDisplay();
                
                curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
                curl_easy_setopt(curl, CURLOPT_URL, nswReleasesXmlUrl);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCurlFile);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, nswdbXml);
                curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
                curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
                curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
                
                res = curl_easy_perform(curl);
                
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &size);
                
                if (res == CURLE_OK && http_code >= 200 && http_code <= 299 && size > 0)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Successfully downloaded %.0lf bytes!", size);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                    success = true;
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to request XML database! HTTP status code: %ld", http_code);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                }
                
                fclose(nswdbXml);
                
                if (success)
                {
                    unlink(nswReleasesXmlPath);
                    rename(nswReleasesXmlTmpPath, nswReleasesXmlPath);
                } else {
                    unlink(nswReleasesXmlTmpPath);
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to open \"%s\" in write mode!", nswReleasesXmlTmpPath);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
            
            curl_easy_cleanup(curl);
        } else {
            uiDrawString("Error: failed to initialize CURL context!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
        
        networkDeinit();
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to initialize socket! (%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    breaks += 2;
}

int versionNumCmp(char *ver1, char *ver2)
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

void updateApplication()
{
    if (envIsNso())
    {
        uiDrawString("Error: unable to update application. It is not running as a NRO.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return;
    }
    
    Result result;
    CURL *curl;
    CURLcode res;
    long http_code = 0;
    double size = 0.0;
    char downloadUrl[512] = {'\0'}, releaseTag[32] = {'\0'};
    bool success = false;
    struct json_object *jobj, *name, *assets;
    FILE *nxDumpToolNro = NULL;
    char nroPath[NAME_BUF_LEN] = {'\0'};
    
    if (R_SUCCEEDED(result = networkInit()))
    {
        curl = curl_easy_init();
        if (curl)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Requesting latest release information from \"%s\"...", githubReleasesApiUrl);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            breaks++;
            
            uiRefreshDisplay();
            
            curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
            curl_easy_setopt(curl, CURLOPT_URL, githubReleasesApiUrl);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCurlBuffer);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent);
            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
            curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
            curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
            
            res = curl_easy_perform(curl);
            
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &size);
            
            if (res == CURLE_OK && http_code >= 200 && http_code <= 299 && size > 0)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Parsing response JSON data from \"%s\"...", githubReleasesApiUrl);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks++;
                
                uiRefreshDisplay();
                
                jobj = json_tokener_parse(result_buf);
                if (jobj != NULL)
                {
                    if (json_object_object_get_ex(jobj, "name", &name) && json_object_get_type(name) == json_type_string)
                    {
                        snprintf(releaseTag, sizeof(releaseTag) / sizeof(releaseTag[0]), json_object_get_string(name));
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Latest release: %s.", releaseTag);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        breaks++;
                        
                        uiRefreshDisplay();
                        
                        // Compare versions
                        if (releaseTag[0] == 'v' || releaseTag[0] == 'V' || releaseTag[0] == 'r' || releaseTag[0] == 'R')
                        {
                            u32 releaseTagLen = strlen(releaseTag);
                            memmove(releaseTag, releaseTag + 1, releaseTagLen - 1);
                            releaseTag[releaseTagLen - 1] = '\0';
                        }
                        
                        if (versionNumCmp(releaseTag, APP_VERSION) > 0)
                        {
                            if (json_object_object_get_ex(jobj, "assets", &assets) && json_object_get_type(assets) == json_type_array)
                            {
                                assets = json_object_array_get_idx(assets, 0);
                                if (assets != NULL)
                                {
                                    if (json_object_object_get_ex(assets, "browser_download_url", &assets) && json_object_get_type(assets) == json_type_string)
                                    {
                                        snprintf(downloadUrl, sizeof(downloadUrl) / sizeof(downloadUrl[0]), json_object_get_string(assets));
                                        
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Download URL: \"%s\".", downloadUrl);
                                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                        breaks++;
                                        
                                        uiDrawString("Please wait...", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                        breaks += 2;
                                        
                                        if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                                        {
                                            uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            breaks += 2;
                                        }
                                        
                                        uiRefreshDisplay();
                                        
                                        snprintf(nroPath, sizeof(nroPath) / sizeof(nroPath[0]), "%s.tmp", (strlen(appLaunchPath) ? appLaunchPath : nxDumpToolPath));
                                        
                                        nxDumpToolNro = fopen(nroPath, "wb");
                                        if (nxDumpToolNro)
                                        {
                                            curl_easy_reset(curl);
                                            
                                            curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
                                            curl_easy_setopt(curl, CURLOPT_URL, downloadUrl);
                                            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCurlFile);
                                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, nxDumpToolNro);
                                            curl_easy_setopt(curl, CURLOPT_USERAGENT, userAgent);
                                            curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                                            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
                                            curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
                                            curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
                                            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                                            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                                            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 50L);
                                            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_2TLS);
                                            
                                            res = curl_easy_perform(curl);
                                            
                                            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                                            curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &size);
                                            
                                            if (res == CURLE_OK && http_code >= 200 && http_code <= 299 && size > 0)
                                            {
                                                success = true;
                                                
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Successfully downloaded %.0lf bytes!", size);
                                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                                                breaks++;
                                                
                                                uiDrawString("Please restart the application to reflect the changes.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                                            } else {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to request latest update binary! HTTP status code: %ld", http_code);
                                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            }
                                            
                                            fclose(nxDumpToolNro);
                                            
                                            if (success)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), nroPath);
                                                nroPath[strlen(nroPath) - 4] = '\0';
                                                
                                                unlink(nroPath);
                                                rename(strbuf, nroPath);
                                            } else {
                                                unlink(nroPath);
                                            }
                                        } else {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to open \"%s\" in write mode!", nroPath);
                                            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                        }
                                    } else {
                                        uiDrawString("Error: unable to parse download URL from JSON response!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                    }
                                } else {
                                    uiDrawString("Error: unable to parse object at index 0 from \"assets\" array in JSON response!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                }
                            } else {
                                uiDrawString("Error: unable to parse \"assets\" array from JSON response!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            }
                        } else {
                            uiDrawString("You already have the latest version!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        }
                    } else {
                        uiDrawString("Error: unable to parse version tag from JSON response!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    }
                    
                    json_object_put(jobj);
                } else {
                    uiDrawString("Error: unable to parse JSON response!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to request latest release information! HTTP status code: %ld", http_code);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
            
            if (result_buf) free(result_buf);
            
            curl_easy_cleanup(curl);
        } else {
            uiDrawString("Error: failed to initialize CURL context!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
        
        networkDeinit();
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to initialize socket! (%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    breaks += 2;
}
