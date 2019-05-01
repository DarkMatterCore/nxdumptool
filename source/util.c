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
#include <mbedtls/sha256.h>

#include "dumper.h"
#include "fsext.h"
#include "ui.h"
#include "util.h"
#include "aes.h"
#include "extkeys.h"

/* Extern variables */

extern int breaks;
extern int font_height;

extern int cursor;
extern int scroll;

/* Constants */

const char *nswReleasesXmlUrl = "http://nswdb.com/xml.php";
const char *nswReleasesXmlTmpPath = "sdmc:/NSWreleases.xml.tmp";
const char *nswReleasesXmlPath = "sdmc:/NSWreleases.xml";
const char *nswReleasesRootElement = "releases";
const char *nswReleasesChildren = "release";
const char *nswReleasesChildrenImageSize = "imagesize";
const char *nswReleasesChildrenTitleID = "titleid";
const char *nswReleasesChildrenImgCrc = "imgcrc";
const char *nswReleasesChildrenReleaseName = "releasename";

const char *githubReleasesApiUrl = "https://api.github.com/repos/DarkMatterCore/gcdumptool/releases/latest";
const char *gcDumpToolTmpPath = "sdmc:/switch/gcdumptool.nro.tmp";
const char *gcDumpToolPath = "sdmc:/switch/gcdumptool.nro";

const char *userAgent = "gcdumptool/" APP_VERSION " (Nintendo Switch)";

const char *keysFilePath = "sdmc:/switch/prod.keys";

/* Statically allocated variables */

nca_keyset_t nca_keyset;

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

bool gameCardInserted;

u64 gameCardSize = 0, trimmedCardSize = 0;
char gameCardSizeStr[32] = {'\0'}, trimmedCardSizeStr[32] = {'\0'};

char *hfs0_header = NULL;
u64 hfs0_offset = 0, hfs0_size = 0;
u32 hfs0_partition_cnt = 0;

char *partitionHfs0Header = NULL;
u64 partitionHfs0HeaderOffset = 0, partitionHfs0HeaderSize = 0;
u32 partitionHfs0FileCount = 0, partitionHfs0StrTableSize = 0;

u32 gameCardAppCount = 0;
u64 *gameCardTitleID = NULL;
u32 *gameCardVersion = NULL;

char **gameCardName = NULL;
char **fixedGameCardName = NULL;
char **gameCardAuthor = NULL;
char **gameCardVersionStr = NULL;

u64 gameCardUpdateTitleID = 0;
u32 gameCardUpdateVersion = 0;
char gameCardUpdateVersionStr[128] = {'\0'};

char strbuf[NAME_BUF_LEN * 4] = {'\0'};

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

void delay(u8 seconds)
{
    if (!seconds) return;
    
    u64 nanoseconds = seconds * (u64)1000000000;
    svcSleepThread(nanoseconds);
    
    uiRefreshDisplay();
}

bool getGameCardTitleIDAndVersion()
{
    bool proceed = true;
    bool success = false;
    
    Result result;
    NcmContentMetaDatabase ncmDb;
    
    NcmApplicationContentMetaKey *appList = NULL;
    NcmApplicationContentMetaKey *appListTmp = NULL;
    size_t appListSize = sizeof(NcmApplicationContentMetaKey);
    
    u32 written = 0;
    u32 total = 0;
    
    if (gameCardTitleID != NULL)
    {
        free(gameCardTitleID);
        gameCardTitleID = NULL;
    }
    
    if (gameCardVersion != NULL)
    {
        free(gameCardVersion);
        gameCardVersion = NULL;
    }
    
    appList = (NcmApplicationContentMetaKey*)calloc(1, appListSize);
    if (appList)
    {
        if (R_SUCCEEDED(result = ncmOpenContentMetaDatabase(FsStorageId_GameCard, &ncmDb)))
        {
            if (R_SUCCEEDED(result = ncmContentMetaDatabaseListApplication(&ncmDb, META_DB_REGULAR_APPLICATION, appList, appListSize, &written, &total)))
            {
                if (written && total)
                {
                    if (total > written)
                    {
                        appListSize *= total;
                        appListTmp = (NcmApplicationContentMetaKey*)realloc(appList, appListSize);
                        if (appListTmp)
                        {
                            appList = appListTmp;
                            memset(appList, 0, appListSize);
                            
                            if (R_SUCCEEDED(result = ncmContentMetaDatabaseListApplication(&ncmDb, META_DB_REGULAR_APPLICATION, appList, appListSize, &written, &total)))
                            {
                                if (written != total)
                                {
                                    uiStatusMsg("getGameCardTitleIDAndVersion: application count mismatch in ncmContentMetaDatabaseListApplication (%u != %u)", written, total);
                                    proceed = false;
                                }
                            } else {
                                uiStatusMsg("getGameCardTitleIDAndVersion: ncmContentMetaDatabaseListApplication failed! (0x%08X)", result);
                                proceed = false;
                            }
                        } else {
                            uiStatusMsg("getGameCardTitleIDAndVersion: error reallocating output buffer for ncmContentMetaDatabaseListApplication (%u %s).", total, (total == 1 ? "entry" : "entries"));
                            proceed = false;
                        }
                    }
                    
                    if (proceed)
                    {
                        gameCardTitleID = (u64*)calloc(total, sizeof(u64));
                        gameCardVersion = (u32*)calloc(total, sizeof(u32));
                        
                        if (gameCardTitleID != NULL && gameCardVersion != NULL)
                        {
                            u32 i;
                            for(i = 0; i < total; i++)
                            {
                                gameCardTitleID[i] = appList[i].metaRecord.titleId;
                                gameCardVersion[i] = appList[i].metaRecord.version;
                            }
                            
                            gameCardAppCount = total;
                            
                            success = true;
                        } else {
                            if (gameCardTitleID != NULL)
                            {
                                free(gameCardTitleID);
                                gameCardTitleID = NULL;
                            }
                            
                            if (gameCardVersion != NULL)
                            {
                                free(gameCardVersion);
                                gameCardVersion = NULL;
                            }
                            
                            uiStatusMsg("getGameCardTitleIDAndVersion: failed to allocate memory for TID/version buffer!");
                        }
                    }
                } else {
                    uiStatusMsg("getGameCardTitleIDAndVersion: ncmContentMetaDatabaseListApplication wrote no entries to output buffer!");
                }
            } else {
                uiStatusMsg("getGameCardTitleIDAndVersion: ncmContentMetaDatabaseListApplication failed! (0x%08X)", result);
            }
        } else {
            uiStatusMsg("getGameCardTitleIDAndVersion: ncmOpenContentMetaDatabase failed! (0x%08X)", result);
        }
        
        free(appList);
    } else {
        uiStatusMsg("getGameCardTitleIDAndVersion: unable to allocate memory for the ApplicationContentMetaKey struct.");
    }
    
    return success;
}

void convertTitleVersionToDecimal(u32 version, char *versionBuf, int versionBufSize)
{
    u8 major = (u8)((version >> 26) & 0x3F);
    u8 middle = (u8)((version >> 20) & 0x3F);
    u8 minor = (u8)((version >> 16) & 0xF);
    u16 build = (u16)version;
    
    snprintf(versionBuf, versionBufSize, "%u (%u.%u.%u.%u)", version, major, middle, minor, build);
}

bool getGameCardControlNacp(u64 titleID, char *nameBuf, int nameBufSize, char *authorBuf, int authorBufSize)
{
    if (titleID == 0) return false;
    
    bool success = false;
    Result result;
    size_t outsize = 0;
    NsApplicationControlData *buf = NULL;
    NacpLanguageEntry *langentry = NULL;
    
    buf = (NsApplicationControlData*)calloc(1, sizeof(NsApplicationControlData));
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
                    success = true;
                } else {
                    uiStatusMsg("getGameCardControlNacp: GetLanguageEntry failed! (0x%08X)", result);
                }
            } else {
                uiStatusMsg("getGameCardControlNacp: Control.nacp buffer size (%u bytes) is too small! Expected: %u bytes", outsize, sizeof(buf->nacp));
            }
        } else {
            uiStatusMsg("getGameCardControlNacp: GetApplicationControlData failed! (0x%08X)", result);
        }
        
        free(buf);
    } else {
        uiStatusMsg("getGameCardControlNacp: Unable to allocate memory for the ns service operations.");
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

void freeStringsPtr(char **var)
{
	if (var)
	{
		u64 i;
		for(i = 0; var[i]; i++) free(var[i]);
		free(var);
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
    
    if (gameCardTitleID != NULL)
    {
        free(gameCardTitleID);
        gameCardTitleID = NULL;
    }
    
    if (gameCardVersion != NULL)
    {
        free(gameCardVersion);
        gameCardVersion = NULL;
    }
    
    if (gameCardName != NULL)
    {
        freeStringsPtr(gameCardName);
        gameCardName = NULL;
    }
    
    if (fixedGameCardName != NULL)
    {
        freeStringsPtr(fixedGameCardName);
        fixedGameCardName = NULL;
    }
    
    if (gameCardAuthor != NULL)
    {
        freeStringsPtr(gameCardAuthor);
        gameCardAuthor = NULL;
    }
    
    if (gameCardVersionStr != NULL)
    {
        freeStringsPtr(gameCardVersionStr);
        gameCardVersionStr = NULL;
    }
    
    gameCardUpdateTitleID = 0;
    gameCardUpdateVersion = 0;
    memset(gameCardUpdateVersionStr, 0, sizeof(gameCardUpdateVersionStr));
}

bool getRootHfs0Header()
{
    u32 magic;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    char gamecard_header[GAMECARD_HEADER_SIZE] = {'\0'};
    
    hfs0_partition_cnt = 0;
    
    workaroundPartitionZeroAccess(&fsOperatorInstance);
    
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
            uiStatusMsg("getRootHfs0Header: Invalid game card size value: 0x%02X", cardSize);
            fsStorageClose(&gameCardStorage);
            return false;
    }
    
    convertSize(gameCardSize, gameCardSizeStr, sizeof(gameCardSizeStr) / sizeof(gameCardSizeStr[0]));
    
    memcpy(&trimmedCardSize, gamecard_header + GAMECARD_DATAEND_ADDR, sizeof(u64));
    trimmedCardSize = (GAMECARD_HEADER_SIZE + (trimmedCardSize * MEDIA_UNIT_SIZE));
    convertSize(trimmedCardSize, trimmedCardSizeStr, sizeof(trimmedCardSizeStr) / sizeof(trimmedCardSizeStr[0]));
    
    memcpy(&hfs0_offset, gamecard_header + HFS0_OFFSET_ADDR, sizeof(u64));
    memcpy(&hfs0_size, gamecard_header + HFS0_SIZE_ADDR, sizeof(u64));
    
    hfs0_header = (char*)malloc(hfs0_size);
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

void loadGameCardInfo()
{
    bool freeBuf = false;
    
    if (gameCardInserted)
    {
        if (hfs0_header == NULL)
        {
            /* Don't access the gamecard immediately to avoid conflicts with the fsp-srv, ncm and ns services */
            uiPleaseWait(GAMECARD_WAIT_TIME);
            
            if (getRootHfs0Header())
            {
                getGameCardUpdateInfo();
                
                if (getGameCardTitleIDAndVersion())
                {
                    gameCardName = calloc(gameCardAppCount + 1, sizeof(char*));
                    fixedGameCardName = calloc(gameCardAppCount + 1, sizeof(char*));
                    gameCardAuthor = calloc(gameCardAppCount + 1, sizeof(char*));
                    gameCardVersionStr = calloc(gameCardAppCount + 1, sizeof(char*));
                    
                    if (gameCardName != NULL && fixedGameCardName != NULL && gameCardAuthor != NULL && gameCardVersionStr != NULL)
                    {
                        u32 i;
                        for(i = 0; i < gameCardAppCount; i++)
                        {
                            gameCardName[i] = calloc(NACP_APPNAME_LEN + 1, sizeof(char));
                            fixedGameCardName[i] = calloc(NACP_APPNAME_LEN + 1, sizeof(char));
                            gameCardAuthor[i] = calloc(NACP_AUTHOR_LEN + 1, sizeof(char));
                            gameCardVersionStr[i] = calloc(VERSION_STR_LEN + 1, sizeof(char));
                            
                            if (gameCardName[i] != NULL && fixedGameCardName[i] != NULL && gameCardAuthor[i] != NULL && gameCardVersionStr[i] != NULL)
                            {
                                convertTitleVersionToDecimal(gameCardVersion[i], gameCardVersionStr[i], VERSION_STR_LEN);
                                
                                getGameCardControlNacp(gameCardTitleID[i], gameCardName[i], NACP_APPNAME_LEN, gameCardAuthor[i], NACP_AUTHOR_LEN);
                                strtrim(gameCardName[i]);
                                strtrim(gameCardAuthor[i]);
                                
                                if (strlen(gameCardName[i]))
                                {
                                    snprintf(fixedGameCardName[i], NACP_APPNAME_LEN, gameCardName[i]);
                                    removeIllegalCharacters(fixedGameCardName[i]);
                                }
                            } else {
                                freeBuf = true;
                                break;
                            }
                        }
                    } else {
                        freeBuf = true;
                    }
                    
                    if (freeBuf)
                    {
                        uiStatusMsg("loadGameCardInfo: error allocating memory for gamecard information.");
                        
                        if (gameCardName != NULL)
                        {
                            freeStringsPtr(gameCardName);
                            gameCardName = NULL;
                        }
                        
                        if (fixedGameCardName != NULL)
                        {
                            freeStringsPtr(fixedGameCardName);
                            fixedGameCardName = NULL;
                        }
                        
                        if (gameCardAuthor != NULL)
                        {
                            freeStringsPtr(gameCardAuthor);
                            gameCardAuthor = NULL;
                        }
                        
                        if (gameCardVersionStr != NULL)
                        {
                            freeStringsPtr(gameCardVersionStr);
                            gameCardVersionStr = NULL;
                        }
                    }
                }
            }
            
            uiPrintHeadline();
        }
    } else {
        freeGameCardInfo();
    }
}

bool getHfs0EntryDetails(char *hfs0Header, u64 hfs0HeaderOffset, u64 hfs0HeaderSize, u32 num_entries, u32 entry_idx, bool isRoot, u32 partitionIndex, u64 *out_offset, u64 *out_size)
{
    if (hfs0Header == NULL) return false;
    
    if (entry_idx > (num_entries - 1)) return false;
    
    if ((HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * num_entries)) > hfs0HeaderSize) return false;
    
    hfs0_entry_table *entryTable = (hfs0_entry_table*)malloc(sizeof(hfs0_entry_table) * num_entries);
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
    
    char *buf = NULL;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    u64 partitionSize = 0;
    u32 magic = 0;
    bool success = false;
    
    if (getHfs0EntryDetails(hfs0_header, hfs0_offset, hfs0_size, hfs0_partition_cnt, partition, true, 0, &partitionHfs0HeaderOffset, &partitionSize))
    {
        workaroundPartitionZeroAccess(&fsOperatorInstance);
        
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            // Same ugly hack from dumpRawPartition()
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
            {
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;*/
                
                buf = (char*)malloc(MEDIA_UNIT_SIZE);
                if (buf)
                {
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
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u HFS0 header size: %lu bytes", partition, partitionHfs0HeaderSize);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u file count: %u", partition, partitionHfs0FileCount);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u string table size: %u bytes", partition, partitionHfs0StrTableSize);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            uiRefreshDisplay();*/
                            
                            // Round up the partition HFS0 header size to a MEDIA_UNIT_SIZE bytes boundary
                            partitionHfs0HeaderSize = round_up(partitionHfs0HeaderSize, MEDIA_UNIT_SIZE);
                            
                            partitionHfs0Header = (char*)malloc(partitionHfs0HeaderSize);
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
                                        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                                    }
                                }
                                
                                /*if (success)
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u HFS0 header successfully retrieved!", partition);
                                    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                                }*/
                            } else {
                                partitionHfs0HeaderOffset = 0;
                                partitionHfs0HeaderSize = 0;
                                partitionHfs0FileCount = 0;
                                partitionHfs0StrTableSize = 0;
                                
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to allocate memory for the HFS0 header from partition #%u!", partition);
                                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                            }
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Magic word mismatch! 0x%08X != 0x%08X", magic, HFS0_MAGIC);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                        }
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionHfs0HeaderOffset);
                        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                    }
                    
                    free(buf);
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to allocate memory for the HFS0 header from partition #%u!", partition);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                }
                
                fsStorageClose(&gameCardStorage);
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            }
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        }
    } else {
        uiDrawString("Error: unable to get partition details from the root HFS0 header!", 0, breaks * font_height, 255, 0, 0);
    }
    
    if (!success) breaks += 2;
    
    return success;
}

bool getHfs0FileList(u32 partition)
{
    if (!getPartitionHfs0Header(partition)) return false;
    
    if (!partitionHfs0Header)
    {
        uiDrawString("HFS0 partition header information unavailable!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (!partitionHfs0FileCount)
    {
        uiDrawString("The selected partition is empty!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (partitionHfs0FileCount > FILENAME_MAX_CNT)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "HFS0 partition contains more than %u files! (%u entries)", FILENAME_MAX_CNT, partitionHfs0FileCount);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    hfs0_entry_table *entryTable = (hfs0_entry_table*)malloc(sizeof(hfs0_entry_table) * partitionHfs0FileCount);
    if (!entryTable)
    {
        uiDrawString("Unable to allocate memory for the HFS0 file entries!", 0, breaks * font_height, 255, 0, 0);
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
        addStringToFilenameBuffer(partitionHfs0Header + filename_offset, &nextFilename);
    }
    
    free(entryTable);
    
    return true;
}

int getSdCardFreeSpace(u64 *out)
{
    struct statvfs st;
    int rc;
    
    rc = statvfs("sdmc:/", &st);
    if (rc != 0)
    {
        uiStatusMsg("getSdCardFreeSpace: Unable to get SD card filesystem stats! statvfs: %d (%s).", errno, strerror(errno));
    } else {
        *out = (u64)(st.f_bsize * st.f_bfree);
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

char *generateDumpName()
{
    if (!gameCardAppCount || !fixedGameCardName || !gameCardVersion || !gameCardTitleID) return NULL;
    
    u32 i;
    
    char tmp[512] = {'\0'};
    char *fullname = NULL;
    char *fullnameTmp = NULL;
    
    size_t strsize = NAME_BUF_LEN;
    fullname = (char*)malloc(strsize);
    if (!fullname) return NULL;
    
    memset(fullname, 0, strsize);
    
    for(i = 0; i < gameCardAppCount; i++)
    {
        snprintf(tmp, sizeof(tmp) / sizeof(tmp[0]), "%s v%u (%016lX)", fixedGameCardName[i], gameCardVersion[i], gameCardTitleID[i]);
        
        if ((strlen(fullname) + strlen(tmp) + 4) > strsize)
        {
            size_t fullname_len = strlen(fullname);
            
            strsize = (fullname_len + strlen(tmp) + 4);
            fullnameTmp = (char*)realloc(fullname, strsize);
            if (fullnameTmp)
            {
                fullname = fullnameTmp;
                memset(fullname + fullname_len, 0, strlen(tmp) + 1);
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

void waitForButtonPress()
{
    uiDrawString("Press any button to continue", 0, breaks * font_height, 255, 255, 255);
    
    uiRefreshDisplay();
    
    while(true)
    {
        hidScanInput();
        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        if (keysDown && !((keysDown & KEY_TOUCH) || (keysDown & KEY_LSTICK_LEFT) || (keysDown & KEY_LSTICK_RIGHT) || (keysDown & KEY_LSTICK_UP) || (keysDown & KEY_LSTICK_DOWN) || \
            (keysDown & KEY_RSTICK_LEFT) || (keysDown & KEY_RSTICK_RIGHT) || (keysDown & KEY_RSTICK_UP) || (keysDown & KEY_RSTICK_DOWN))) break;
    }
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

void convertNcaSizeToU64(const u8 size[0x6], u64 *out)
{
    if (!size || !out) return;
    
    u64 tmp = 0;
    
    tmp |= (((u64)size[5] << 40) & (u64)0xFF0000000000);
    tmp |= (((u64)size[4] << 32) & (u64)0x00FF00000000);
    tmp |= (((u64)size[3] << 24) & (u64)0x0000FF000000);
    tmp |= (((u64)size[2] << 16) & (u64)0x000000FF0000);
    tmp |= (((u64)size[1] << 8)  & (u64)0x00000000FF00);
    tmp |= ((u64)size[0]         & (u64)0x0000000000FF);
    
    *out = tmp;
}

bool loadNcaKeyset()
{
    // Keyset already loaded
    if (nca_keyset.key_cnt > 0) return true;
    
    // Open keys file
    FILE *keysFile = fopen(keysFilePath, "rb");
    if (!keysFile)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to open \"%s\" to retrieve NCA keyset!", keysFilePath);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    // Load keys
    int ret = extkeys_initialize_keyset(&nca_keyset, keysFile);
    fclose(keysFile);
    
    if (ret < 1)
    {
        if (ret == -1)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to parse necessary keys from \"%s\"! (keys file empty?)", keysFilePath);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        }
        
        return false;
    }
    
    return true;
}

bool encryptNcaHeader(nca_header_t *input, u8 *outBuf, u64 outBufSize)
{
    if (!input || !outBuf || !outBufSize || outBufSize < NCA_FULL_HEADER_LENGTH || (bswap_32(input->magic) != NCA3_MAGIC && bswap_32(input->magic) != NCA2_MAGIC))
    {
        uiDrawString("Error: invalid NCA header encryption parameters.", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    u32 i;
    aes_ctx_t *hdr_aes_ctx = NULL;
    aes_ctx_t *aes_ctx = NULL;
    
    u8 crypto_type = (input->crypto_type2 > input->crypto_type ? input->crypto_type2 : input->crypto_type);
    if (crypto_type) crypto_type--;
    
    aes_ctx = new_aes_ctx(nca_keyset.key_area_keys[crypto_type][input->kaek_ind], 16, AES_MODE_ECB);
    if (!aes_ctx) return false;
    
    if (!aes_encrypt(aes_ctx, input->nca_keys, input->nca_keys, NCA_KEY_AREA_SIZE))
    {
        free_aes_ctx(aes_ctx);
        return false;
    }
    
    free_aes_ctx(aes_ctx);
    
    hdr_aes_ctx = new_aes_ctx(nca_keyset.header_key, 32, AES_MODE_XTS);
    if (!hdr_aes_ctx) return false;
    
    if (bswap_32(input->magic) == NCA3_MAGIC)
    {
        if (!aes_xts_encrypt(hdr_aes_ctx, outBuf, input, NCA_FULL_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE))
        {
            free_aes_ctx(hdr_aes_ctx);
            return false;
        }
    } else
    if (bswap_32(input->magic) == NCA2_MAGIC)
    {
        if (!aes_xts_encrypt(hdr_aes_ctx, outBuf, input, NCA_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE))
        {
            free_aes_ctx(hdr_aes_ctx);
            return false;
        }
        
        for(i = 0; i < NCA_SECTION_HEADER_CNT; i++)
        {
            if (!aes_xts_encrypt(hdr_aes_ctx, outBuf + NCA_HEADER_LENGTH + (i * NCA_SECTION_HEADER_LENGTH), &(input->fs_headers[i]), NCA_SECTION_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE))
            {
                free_aes_ctx(hdr_aes_ctx);
                return false;
            }
        }
    }
    
    free_aes_ctx(hdr_aes_ctx);
    
    return true;
}

bool decryptNcaHeader(const char *ncaBuf, u64 ncaBufSize, nca_header_t *out)
{
    if (!ncaBuf || !ncaBufSize || ncaBufSize < NCA_FULL_HEADER_LENGTH)
    {
        uiDrawString("Error: invalid NCA header decryption parameters.", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    u32 i;
    aes_ctx_t *hdr_aes_ctx = NULL;
    aes_ctx_t *aes_ctx = NULL;
    
    u8 crypto_type;
    bool has_rights_id = false;
    
    u64 section_offset;
    u64 section_size;
    
    hdr_aes_ctx = new_aes_ctx(nca_keyset.header_key, 32, AES_MODE_XTS);
    if (!hdr_aes_ctx) return false;
    
    if (!aes_xts_decrypt(hdr_aes_ctx, out, ncaBuf, NCA_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE))
    {
        free_aes_ctx(hdr_aes_ctx);
        return false;
    }
    
    if (bswap_32(out->magic) == NCA3_MAGIC)
    {
        if (!aes_xts_decrypt(hdr_aes_ctx, out, ncaBuf, NCA_FULL_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE))
        {
            free_aes_ctx(hdr_aes_ctx);
            return false;
        }
    } else
    if (bswap_32(out->magic) == NCA2_MAGIC)
    {
        for(i = 0; i < NCA_SECTION_HEADER_CNT; i++)
        {
            if (out->fs_headers[i]._0x148[0] != 0 || memcmp(out->fs_headers[i]._0x148, out->fs_headers[i]._0x148 + 1, 0xB7))
            {
                if (!aes_xts_decrypt(hdr_aes_ctx, &(out->fs_headers[i]), ncaBuf + NCA_HEADER_LENGTH + (i * NCA_SECTION_HEADER_LENGTH), NCA_SECTION_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE))
                {
                    free_aes_ctx(hdr_aes_ctx);
                    return false;
                }
            } else {
                memset(&(out->fs_headers[i]), 0, sizeof(nca_fs_header_t));
            }
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid NCA magic word! Wrong keys? (0x%08X)", out->magic);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        free_aes_ctx(hdr_aes_ctx);
        return false;
    }
    
    free_aes_ctx(hdr_aes_ctx);
    
    crypto_type = (out->crypto_type2 > out->crypto_type ? out->crypto_type2 : out->crypto_type);
    if (crypto_type) crypto_type--;
    
    for(i = 0; i < 0x10; i++)
    {
        if (out->rights_id[i] != 0)
        {
            has_rights_id = true;
            break;
        }
    }
    
    if (has_rights_id)
    {
        uiDrawString("Error: Rights ID field in NCA header not empty!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    section_offset = (out->section_entries[0].media_start_offset * MEDIA_UNIT_SIZE);
    section_size = ((out->section_entries[0].media_end_offset * MEDIA_UNIT_SIZE) - section_offset);
    
    if (!section_offset || !section_size || section_offset < NCA_FULL_HEADER_LENGTH)
    {
        uiDrawString("Error: invalid size/offset for NCA section #0!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    aes_ctx = new_aes_ctx(nca_keyset.key_area_keys[crypto_type][out->kaek_ind], 16, AES_MODE_ECB);
    if (!aes_ctx) return false;
    
    if (!aes_decrypt(aes_ctx, out->nca_keys, out->nca_keys, NCA_KEY_AREA_SIZE))
    {
        free_aes_ctx(aes_ctx);
        return false;
    }
    
    free_aes_ctx(aes_ctx);
    
    return true;
}

bool decryptCnmtNca(char *ncaBuf, u64 ncaBufSize)
{
    u32 i;
    nca_header_t dec_header;
    aes_ctx_t *aes_ctx = NULL;
    
    u8 crypto_type;
    
    u64 section_offset;
    u64 section_size;
    char *section_data = NULL;
    
    pfs0_header nca_pfs0_header;
    
    if (!decryptNcaHeader(ncaBuf, ncaBufSize, &dec_header)) return false;
    
    if (dec_header.fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_header.fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString("Error: CNMT NCA section #0 doesn't hold a PFS0 partition.", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (!dec_header.fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString("Error: invalid size for PFS0 partition in CNMT NCA section #0.", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    crypto_type = (dec_header.crypto_type2 > dec_header.crypto_type ? dec_header.crypto_type2 : dec_header.crypto_type);
    if (crypto_type) crypto_type--;
    
    section_offset = (dec_header.section_entries[0].media_start_offset * MEDIA_UNIT_SIZE);
    section_size = ((dec_header.section_entries[0].media_end_offset * MEDIA_UNIT_SIZE) - section_offset);
    
    section_data = (char*)calloc(section_size, sizeof(char));
    if (!section_data)
    {
        uiDrawString("Error: unable to allocate memory for the decrypted CNMT NCA section #0.", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (dec_header.fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_NONE)
    {
        if (dec_header.fs_headers[0].crypt_type == NCA_FS_HEADER_CRYPT_CTR || dec_header.fs_headers[0].crypt_type == NCA_FS_HEADER_CRYPT_BKTR)
        {
            aes_ctx = new_aes_ctx(dec_header.nca_keys[2], 16, AES_MODE_CTR);
            if (!aes_ctx)
            {
                free(section_data);
                return false;
            }
            
            unsigned char ctr[0x10];
            u64 ofs = (section_offset >> 4);
            
            for(i = 0; i < 0x8; i++)
            {
                ctr[i] = dec_header.fs_headers[0].section_ctr[0x08 - i - 1];
                ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
                ofs >>= 8;
            }
            
            if (!aes_setiv(aes_ctx, ctr, 0x10))
            {
                free_aes_ctx(aes_ctx);
                free(section_data);
                return false;
            }
            
            if (!aes_decrypt(aes_ctx, section_data, ncaBuf + section_offset, section_size))
            {
                free_aes_ctx(aes_ctx);
                free(section_data);
                return false;
            }
            
            free_aes_ctx(aes_ctx);
        } else
        if (dec_header.fs_headers[0].crypt_type == NCA_FS_HEADER_CRYPT_XTS)
        {
            aes_ctx = new_aes_ctx(dec_header.nca_keys, 32, AES_MODE_XTS);
            if (!aes_ctx)
            {
                free(section_data);
                return false;
            }
            
            if (!aes_xts_decrypt(aes_ctx, section_data, ncaBuf + section_offset, section_size, 0, NCA_AES_XTS_SECTOR_SIZE))
            {
                free_aes_ctx(aes_ctx);
                free(section_data);
                return false;
            }
            
            free_aes_ctx(aes_ctx);
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid crypt type for CNMT NCA section #0! (0x%02X)", dec_header.fs_headers[0].crypt_type);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            free(section_data);
            return false;
        }
    } else {
        memcpy(section_data, ncaBuf + section_offset, section_size);
    }
    
    memcpy(&nca_pfs0_header, section_data + dec_header.fs_headers[0].pfs0_superblock.hash_table_offset + dec_header.fs_headers[0].pfs0_superblock.pfs0_offset, sizeof(pfs0_header));
    
    if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid magic word for CNMT NCA section #0 PFS0 partition! Wrong keys? (0x%08X)", nca_pfs0_header.magic);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        free(section_data);
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString("Error: CNMT NCA section #0 PFS0 partition is empty! Wrong keys? (0x%08X)", 0, breaks * font_height, 255, 0, 0);
        free(section_data);
        return false;
    }
    
    // Replace input buffer data in-place
    memset(ncaBuf, 0, ncaBufSize);
    memcpy(ncaBuf, &dec_header, sizeof(nca_header_t));
    memcpy(ncaBuf + section_offset, section_data, section_size);
    
    /*FILE *nca_file = fopen("sdmc:/decrypted_cnmt_nca.bin", "wb");
    if (nca_file)
    {
        fwrite(ncaBuf, 1, ncaBufSize, nca_file);
        fclose(nca_file);
    }*/
    
    free(section_data);
    
    return true;
}

bool calculateSHA256(const u8 *data, const u32 dataSize, u8 out[32])
{
    int ret;
    mbedtls_sha256_context sha256_ctx;
    
    mbedtls_sha256_init(&sha256_ctx);
    
    ret = mbedtls_sha256_starts_ret(&sha256_ctx, 0);
    if (ret < 0)
    {
        mbedtls_sha256_free(&sha256_ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to start SHA-256 calculation! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    ret = mbedtls_sha256_update_ret(&sha256_ctx, data, dataSize);
    if (ret < 0)
    {
        mbedtls_sha256_free(&sha256_ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to update SHA-256 calculation! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    ret = mbedtls_sha256_finish_ret(&sha256_ctx, out);
    if (ret < 0)
    {
        mbedtls_sha256_free(&sha256_ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to finish SHA-256 calculation! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    mbedtls_sha256_free(&sha256_ctx);
    
    return true;
}

void generateCnmtMetadataXml(cnmt_xml_program_info *xml_program_info, cnmt_xml_content_info *xml_content_info, char *out)
{
    if (!xml_program_info || !xml_content_info || !xml_program_info->nca_cnt || !out) return;
    
    u32 i;
    char tmp[NAME_BUF_LEN] = {'\0'};
    
    const char *contentTypes[] = { "Meta", "Program", "Data", "Control", "HtmlDocument", "LegalInformation", "DeltaFragment" };
    const u32 contentTypesCnt = (sizeof(contentTypes) / sizeof(contentTypes[0]));
    
    sprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" \
                 "<ContentMeta>\r\n" \
                 "  <Type>Application</Type>\r\n" \
                 "  <Id>0x%016lx</Id>\r\n" \
                 "  <Version>%u</Version>\r\n" \
                 "  <RequiredDownloadSystemVersion>%u</RequiredDownloadSystemVersion>\r\n", \
                 xml_program_info->title_id, \
                 xml_program_info->version, \
                 xml_program_info->required_dl_sysver);
    
    for(i = 0; i < xml_program_info->nca_cnt; i++)
    {
        sprintf(tmp, "  <Content>\r\n" \
                     "    <Type>%s</Type>\r\n" \
                     "    <Id>%s</Id>\r\n" \
                     "    <Size>%lu</Size>\r\n" \
                     "    <Hash>%s</Hash>\r\n" \
                     "    <KeyGeneration>%u</KeyGeneration>\r\n" \
                     "  </Content>\r\n", \
                     (xml_content_info[i].type < contentTypesCnt ? contentTypes[xml_content_info[i].type] : "Unknown"), \
                     xml_content_info[i].nca_id_str, \
                     xml_content_info[i].size, \
                     xml_content_info[i].hash_str, \
                     xml_content_info[i].keyblob); \
        
        strcat(out, tmp);
    }
    
    sprintf(tmp, "  <Digest>%s</Digest>\r\n" \
                 "  <KeyGenerationMin>%u</KeyGenerationMin>\r\n" \
                 "  <RequiredSystemVersion>%u</RequiredSystemVersion>\r\n" \
                 "  <PatchId>0x%016lx</PatchId>\r\n" \
                 "</ContentMeta>", \
                 xml_program_info->digest_str, \
                 xml_program_info->min_keyblob, \
                 xml_program_info->min_sysver, \
                 xml_program_info->patch_tid);
    
    strcat(out, tmp);
}

void addStringToFilenameBuffer(const char *string, char **nextFilename)
{
    filenames[filenamesCount++] = *nextFilename;
    strcpy(*nextFilename, string);
    *nextFilename += (strlen(string) + 1);
}

void removeDirectory(const char *path)
{
    struct dirent* ent;
    char cur_path[NAME_BUF_LEN] = {'\0'};
    
    DIR *dir = opendir(path);
    if (dir)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if ((strlen(ent->d_name) == 1 && !strcmp(ent->d_name, ".")) || (strlen(ent->d_name) == 2 && !strcmp(ent->d_name, ".."))) continue;
            
            snprintf(cur_path, sizeof(cur_path) / sizeof(cur_path[0]), "%s/%s", path, ent->d_name);
            
            if (ent->d_type == DT_DIR)
            {
                removeDirectory(cur_path);
            } else {
                remove(cur_path);
            }
        }
        
        closedir(dir);
        
        rmdir(path);
    }
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
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XML Title ID: %016lX.", xmlTitleID);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XML Image CRC32: %08X.", xmlCrc);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XML Release Name: %s.", xmlReleaseName);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks += 2;
    }*/
    
    if (xmlImageSize == imageSize && xmlTitleID == gc_tid && xmlCrc == crc)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Found matching Scene release: \"%s\" (CRC32: %08X). This is a good dump!", xmlReleaseName, xmlCrc);
        uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
        found = true;
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump doesn't match Scene release: \"%s\"! (CRC32: %08X)", xmlReleaseName, xmlCrc);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
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
    if (!gameCardAppCount || !gameCardTitleID || !hfs0_partition_cnt || !crc) return;
    
    xmlDocPtr doc = NULL;
    bool found = false;
    
    doc = xmlParseFile(nswReleasesXmlPath);
    if (doc)
    {
        u32 i;
        for(i = 0; i < gameCardAppCount; i++)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "//%s/%s[.//%s='%016lX']", nswReleasesRootElement, nswReleasesChildren, nswReleasesChildrenTitleID, gameCardTitleID[i]);
            xmlXPathObjectPtr nodeSet = getNodeSet(doc, (xmlChar*)strbuf);
            if (nodeSet)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Found %d %s with Title ID \"%016lX\".", nodeSet->nodesetval->nodeNr, (nodeSet->nodesetval->nodeNr > 1 ? "releases" : "release"), gameCardTitleID[i]);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                uiRefreshDisplay();
                
                u32 i;
                for(i = 0; i < nodeSet->nodesetval->nodeNr; i++)
                {
                    xmlNodePtr node = nodeSet->nodesetval->nodeTab[i]->xmlChildrenNode;
                    
                    found = parseNSWDBRelease(doc, node, gameCardTitleID[i], crc);
                    if (found) break;
                }
                
                xmlXPathFreeObject(nodeSet);
                
                if (!found)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "No checksum matches found in XML document for Title ID \"%016lX\"!", gameCardTitleID[i]);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                    if ((i + 1) < gameCardAppCount) breaks += 2;
                } else {
                    breaks--;
                    break;
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "No records with Title ID \"%016lX\" found within the XML document!", gameCardTitleID[i]);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                if ((i + 1) < gameCardAppCount) breaks += 2;
            }
        }
        
        xmlFreeDoc(doc);
        
        if (!found)
        {
            breaks++;
            uiDrawString("This could either be a bad dump or an undumped cartridge.", 0, breaks * font_height, 255, 0, 0);
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to open and/or parse \"%s\"!", nswReleasesXmlPath);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
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
        result_buf = (char*)malloc(result_sz);
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
        char *new_buf = (char*)realloc(result_buf, result_sz);
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
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks += 2;
                
                if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                {
                    uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
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
                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                    success = true;
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to request XML database! HTTP status code: %ld", http_code);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                }
                
                fclose(nswdbXml);
                
                if (success)
                {
                    remove(nswReleasesXmlPath);
                    rename(nswReleasesXmlTmpPath, nswReleasesXmlPath);
                } else {
                    remove(nswReleasesXmlTmpPath);
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to open \"%s\" in write mode!", nswReleasesXmlTmpPath);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            }
            
            curl_easy_cleanup(curl);
        } else {
            uiDrawString("Error: failed to initialize CURL context!", 0, breaks * font_height, 255, 0, 0);
        }
        
        networkDeinit();
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to initialize socket! (%08X)", result);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
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
    Result result;
    CURL *curl;
    CURLcode res;
    long http_code = 0;
    double size = 0.0;
    char downloadUrl[512] = {'\0'}, releaseTag[32] = {'\0'};
    bool success = false;
    struct json_object *jobj, *name, *assets;
    FILE *gcDumpToolNro = NULL;
    
    if (R_SUCCEEDED(result = networkInit()))
    {
        curl = curl_easy_init();
        if (curl)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Requesting latest release information from \"%s\"...", githubReleasesApiUrl);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
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
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                uiRefreshDisplay();
                
                jobj = json_tokener_parse(result_buf);
                if (jobj != NULL)
                {
                    if (json_object_object_get_ex(jobj, "name", &name) && json_object_get_type(name) == json_type_string)
                    {
                        snprintf(releaseTag, sizeof(releaseTag) / sizeof(releaseTag[0]), json_object_get_string(name));
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Latest release: %s.", releaseTag);
                        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
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
                                        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                                        breaks++;
                                        
                                        uiDrawString("Please wait...", 0, breaks * font_height, 255, 255, 255);
                                        breaks += 2;
                                        
                                        if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                                        {
                                            uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                                            breaks += 2;
                                        }
                                        
                                        uiRefreshDisplay();
                                        
                                        gcDumpToolNro = fopen(gcDumpToolTmpPath, "wb");
                                        if (gcDumpToolNro)
                                        {
                                            curl_easy_reset(curl);
                                            
                                            curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 102400L);
                                            curl_easy_setopt(curl, CURLOPT_URL, downloadUrl);
                                            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCurlFile);
                                            curl_easy_setopt(curl, CURLOPT_WRITEDATA, gcDumpToolNro);
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
                                                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                                                breaks++;
                                                
                                                uiDrawString("Please restart the application to reflect the changes.", 0, breaks * font_height, 0, 255, 0);
                                            } else {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to request latest update binary! HTTP status code: %ld", http_code);
                                                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                                            }
                                            
                                            fclose(gcDumpToolNro);
                                            
                                            if (success)
                                            {
                                                remove(gcDumpToolPath);
                                                rename(gcDumpToolTmpPath, gcDumpToolPath);
                                            } else {
                                                remove(gcDumpToolTmpPath);
                                            }
                                        } else {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to open \"%s\" in write mode!", gcDumpToolTmpPath);
                                            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                                        }
                                    } else {
                                        uiDrawString("Error: unable to parse download URL from JSON response!", 0, breaks * font_height, 255, 0, 0);
                                    }
                                } else {
                                    uiDrawString("Error: unable to parse object at index 0 from \"assets\" array in JSON response!", 0, breaks * font_height, 255, 0, 0);
                                }
                            } else {
                                uiDrawString("Error: unable to parse \"assets\" array from JSON response!", 0, breaks * font_height, 255, 0, 0);
                            }
                        } else {
                            uiDrawString("You already have the latest version!", 0, breaks * font_height, 255, 255, 255);
                        }
                    } else {
                        uiDrawString("Error: unable to parse version tag from JSON response!", 0, breaks * font_height, 255, 0, 0);
                    }
                    
                    json_object_put(jobj);
                } else {
                    uiDrawString("Error: unable to parse JSON response!", 0, breaks * font_height, 255, 0, 0);
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to request latest release information! HTTP status code: %ld", http_code);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            }
            
            if (result_buf) free(result_buf);
            
            curl_easy_cleanup(curl);
        } else {
            uiDrawString("Error: failed to initialize CURL context!", 0, breaks * font_height, 255, 0, 0);
        }
        
        networkDeinit();
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to initialize socket! (%08X)", result);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
    }
    
    breaks += 2;
}
