#pragma once

#ifndef __UTIL_H__
#define __UTIL_H__

#include <switch.h>
#include "nca.h"

#define HBLOADER_BASE_PATH              "sdmc:/switch/"
#define APP_BASE_PATH                   HBLOADER_BASE_PATH APP_TITLE "/"
#define XCI_DUMP_PATH                   APP_BASE_PATH "XCI/"
#define NSP_DUMP_PATH                   APP_BASE_PATH "NSP/"
#define HFS0_DUMP_PATH                  APP_BASE_PATH "HFS0/"
#define EXEFS_DUMP_PATH                 APP_BASE_PATH "ExeFS/"
#define ROMFS_DUMP_PATH                 APP_BASE_PATH "RomFS/"
#define CERT_DUMP_PATH                  APP_BASE_PATH "Certificate/"
#define BATCH_OVERRIDES_PATH            NSP_DUMP_PATH "BatchOverrides/"
#define TICKET_PATH                     APP_BASE_PATH "Ticket/"

#define CONFIG_PATH                     APP_BASE_PATH "config.bin"
#define NRO_NAME                        APP_TITLE ".nro"
#define NRO_PATH                        APP_BASE_PATH NRO_NAME
#define NSWDB_XML_PATH                  APP_BASE_PATH "NSWreleases.xml"
#define KEYS_FILE_PATH                  HBLOADER_BASE_PATH "prod.keys"

#define CFW_PATH_ATMOSPHERE             "sdmc:/atmosphere/contents/"
#define CFW_PATH_SXOS                   "sdmc:/sxos/titles/"
#define CFW_PATH_REINX                  "sdmc:/ReiNX/titles/"

#define HTTP_USER_AGENT                 APP_TITLE "/" APP_VERSION " (Nintendo Switch)"

#define GITHUB_API_URL                  "https://api.github.com/repos/DarkMatterCore/nxdumptool/releases/latest"
#define GITHUB_API_JSON_RELEASE_NAME    "name"
#define GITHUB_API_JSON_ASSETS          "assets"
#define GITHUB_API_JSON_ASSETS_NAME     "name"
#define GITHUB_API_JSON_ASSETS_DL_URL   "browser_download_url"

#define NOINTRO_DOM_CHECK_URL           "https://datomatic.no-intro.org/qchknsw.php"

#define NSWDB_XML_URL                   "http://nswdb.com/xml.php"
#define NSWDB_XML_ROOT                  "releases"
#define NSWDB_XML_CHILD                 "release"
#define NSWDB_XML_CHILD_TITLEID         "titleid"
#define NSWDB_XML_CHILD_IMGCRC          "imgcrc"
#define NSWDB_XML_CHILD_RELEASENAME     "releasename"

#define LOCKPICK_RCM_URL                "https://github.com/shchmue/Lockpick_RCM"

#define KiB                             (1024.0)
#define MiB                             (1024.0 * KiB)
#define GiB                             (1024.0 * MiB)

#define NAME_BUF_LEN                    2048

#define DUMP_BUFFER_SIZE                (u64)0x400000		                    // 4 MiB (4194304 bytes)

#define GAMECARD_READ_BUFFER_SIZE       DUMP_BUFFER_SIZE                        // 4 MiB (4194304 bytes)

#define NCA_CTR_BUFFER_SIZE             DUMP_BUFFER_SIZE                        // 4 MiB (4194304 bytes)

#define NSP_XML_BUFFER_SIZE             (u64)0xA00000                           // 10 MiB (10485760 bytes)

#define APPLICATION_PATCH_BITMASK       (u64)0x800
#define APPLICATION_ADDON_BITMASK       (u64)0xFFFFFFFFFFFF0000

#define NACP_APPNAME_LEN                0x200
#define NACP_AUTHOR_LEN                 0x100
#define VERSION_STR_LEN                 0x40

#define MEDIA_UNIT_SIZE                 0x200

#define ISTORAGE_PARTITION_CNT          2

#define GAMECARD_WAIT_TIME              3                                       // 3 seconds

#define GAMECARD_HEADER_MAGIC           (u32)0x48454144                         // "HEAD"

#define GAMECARD_SIZE_1GiB              (u64)0x40000000
#define GAMECARD_SIZE_2GiB              (u64)0x80000000
#define GAMECARD_SIZE_4GiB              (u64)0x100000000
#define GAMECARD_SIZE_8GiB              (u64)0x200000000
#define GAMECARD_SIZE_16GiB             (u64)0x400000000
#define GAMECARD_SIZE_32GiB             (u64)0x800000000

#define GAMECARD_UPDATE_TITLEID         (u64)0x0100000000000816

#define GAMECARD_ECC_BLOCK_SIZE         (u64)0x200                              // 512 bytes
#define GAMECARD_ECC_DATA_SIZE          (u64)0x24                               // 36 bytes

#define GAMECARD_TYPE1_PARTITION_CNT    3                                       // "update" (0), "normal" (1), "secure" (2)
#define GAMECARD_TYPE2_PARTITION_CNT    4                                       // "update" (0), "logo" (1), "normal" (2), "secure" (3)
#define GAMECARD_TYPE(x)                ((x) == GAMECARD_TYPE1_PARTITION_CNT ? "Type 0x01" : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? "Type 0x02" : "Unknown"))
#define GAMECARD_TYPE1_PART_NAMES(x)    ((x) == 0 ? "Update" : ((x) == 1 ? "Normal" : ((x) == 2 ? "Secure" : "Unknown")))
#define GAMECARD_TYPE2_PART_NAMES(x)    ((x) == 0 ? "Update" : ((x) == 1 ? "Logo" : ((x) == 2 ? "Normal" : ((x) == 3 ? "Secure" : "Unknown"))))
#define GAMECARD_PARTITION_NAME(x, y)   ((x) == GAMECARD_TYPE1_PARTITION_CNT ? GAMECARD_TYPE1_PART_NAMES(y) : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? GAMECARD_TYPE2_PART_NAMES(y) : "Unknown"))

#define HFS0_MAGIC                      (u32)0x48465330                         // "HFS0"

#define HFS0_TO_ISTORAGE_IDX(x, y)      ((x) == GAMECARD_TYPE1_PARTITION_CNT ? ((y) < (GAMECARD_TYPE1_PARTITION_CNT - 1) ? 0 : 1) : ((y) < (GAMECARD_TYPE2_PARTITION_CNT - 1) ? 0 : 1))

#define NACP_ICON_SQUARE_DIMENSION      256
#define NACP_ICON_DOWNSCALED            96

#define round_up(x, y)                  ((x) + (((y) - ((x) % (y))) % (y)))			// Aligns 'x' bytes to a 'y' bytes boundary

#define ORPHAN_ENTRY_TYPE_PATCH         1
#define ORPHAN_ENTRY_TYPE_ADDON         2

#define MAX_ELEMENTS(x)                 ((sizeof((x))) / (sizeof((x)[0])))          // Returns the max number of elements that can be stored in an array
#define MAX_CHARACTERS(x)               (MAX_ELEMENTS((x)) - 1)                     // Returns the max number of characters that can be stored in char array while also leaving space for a NULL terminator

#define BIS_MOUNT_NAME                  "sys:"
#define BIS_CERT_SAVE_NAME              BIS_MOUNT_NAME "/save/80000000000000e0"
#define BIS_COMMON_TIK_SAVE_NAME        BIS_MOUNT_NAME "/save/80000000000000e1"
#define BIS_PERSONALIZED_TIK_SAVE_NAME  BIS_MOUNT_NAME "/save/80000000000000e2"

#define SMOOTHING_FACTOR                (double)0.1

#define CANCEL_BTN_SEC_HOLD             2                           // The cancel button must be held for at least CANCEL_BTN_SEC_HOLD seconds to cancel an ongoing operation

typedef struct {
    u8 signature[0x100];
    u32 magic;
    u32 secureAreaStartAddr;
    u32 backupAreaStartAddr;
    u8 titleKeyIndex;
    u8 size;
    u8 headerVersion;
    u8 flags;
    u64 packageId;
    u64 validDataEndAddr;
    u8 iv[0x10];
    u64 rootHfs0HeaderOffset;
    u64 rootHfs0HeaderSize;
    u8 rootHfs0HeaderHash[SHA256_HASH_SIZE];
    u8 initialDataHash[SHA256_HASH_SIZE];
    u32 securityMode;
    u32 t1KeyIndex;
    u32 keyIndex;
    u32 normalAreaEndAddr;
    u8 encryptedInfoBlock[0x70];
} PACKED gamecard_header_t;

typedef struct {
    u64 offset;
    u64 size;
    u8 *header;
    u64 header_size;
    u32 file_cnt;
    u32 str_table_size;
} PACKED hfs0_partition_info;

typedef enum {
    ISTORAGE_PARTITION_NONE = 0,
    ISTORAGE_PARTITION_NORMAL,
    ISTORAGE_PARTITION_SECURE,
    ISTORAGE_PARTITION_INVALID
} openIStoragePartition;

typedef struct {
    FsDeviceOperator fsOperatorInstance;
    FsEventNotifier fsGameCardEventNotifier;
    Event fsGameCardKernelEvent;
    FsGameCardHandle fsGameCardHandle;
    FsStorage fsGameCardStorage;
    openIStoragePartition curIStorageIndex;
    volatile bool isInserted;
    gamecard_header_t header;
    u8 *rootHfs0Header;
    u32 hfs0PartitionCnt;
    hfs0_partition_info *hfs0Partitions;
    u64 size;
    char sizeStr[32];
    u64 trimmedSize;
    char trimmedSizeStr[32];
    u64 IStoragePartitionSizes[ISTORAGE_PARTITION_CNT];
    u64 updateTitleId;
    u32 updateVersion;
    char updateVersionStr[64];
} gamecard_ctx_t;

typedef struct {
    u64 titleId;
    u32 version;
    u32 ncmIndex;
    NcmStorageId storageId;
    char name[NACP_APPNAME_LEN];
    char fixedName[NACP_APPNAME_LEN];
    char author[NACP_AUTHOR_LEN];
    char versionStr[VERSION_STR_LEN];
    u8 *icon;
    u64 contentSize;
    char contentSizeStr[32];
} base_app_ctx_t;

typedef struct {
    u64 titleId;
    u32 version;
    u32 ncmIndex;
    NcmStorageId storageId;
    char versionStr[VERSION_STR_LEN];
    u64 contentSize;
    char contentSizeStr[32];
} patch_addon_ctx_t;

typedef struct {
    u32 index;
    u8 type; // 1 = Patch, 2 = AddOn
    char name[NACP_APPNAME_LEN];
    char fixedName[NACP_APPNAME_LEN];
    char orphanListStr[NACP_APPNAME_LEN * 2];
} orphan_patch_addon_entry;

typedef struct {
    u32 magic;
    u32 file_cnt;
    u32 str_table_size;
    u32 reserved;
} PACKED hfs0_header;

typedef struct {
    u64 file_offset;
    u64 file_size;
    u32 filename_offset;
    u32 hashed_region_size;
    u64 reserved;
    u8 hashed_region_sha256[0x20];
} PACKED hfs0_file_entry;

typedef struct {
    int line_offset;
    u64 totalSize;
    char totalSizeStr[32];
    u64 curOffset;
    char curOffsetStr[32];
    u64 seqDumpCurOffset;
    u8 progress;
    u64 start;
    u64 now;
    u64 remainingTime;
    char etaInfo[32];
    double lastSpeed;
    double averageSpeed;
    u32 cancelBtnState;
    u32 cancelBtnStatePrev;
    u64 cancelStartTmr;
    u64 cancelEndTmr;
} progress_ctx_t;

typedef enum {
    ROMFS_TYPE_APP = 0,
    ROMFS_TYPE_PATCH,
    ROMFS_TYPE_ADDON
} selectedRomFsType;

typedef enum {
    TICKET_TYPE_APP = 0,
    TICKET_TYPE_PATCH,
    TICKET_TYPE_ADDON
} selectedTicketType;

typedef struct {
    bool isFat32;
    bool setXciArchiveBit;
    bool keepCert;
    bool trimDump;
    bool calcCrc;
    bool useNoIntroLookup;
    bool useBrackets;
} PACKED xciOptions;

typedef struct {
    bool isFat32;
    bool useNoIntroLookup;
    bool removeConsoleData;
    bool tiklessDump;
    bool npdmAcidRsaPatch;
    bool dumpDeltaFragments;
    bool useBrackets;
} PACKED nspOptions;

typedef enum {
    BATCH_SOURCE_ALL = 0,
    BATCH_SOURCE_SDCARD,
    BATCH_SOURCE_EMMC,
    BATCH_SOURCE_CNT
} batchModeSourceStorage;

typedef struct {
    bool dumpAppTitles;
    bool dumpPatchTitles;
    bool dumpAddOnTitles;
    bool isFat32;
    bool removeConsoleData;
    bool tiklessDump;
    bool npdmAcidRsaPatch;
    bool dumpDeltaFragments;
    bool skipDumpedTitles;
    bool rememberDumpedTitles;
    bool haltOnErrors;
    bool useBrackets;
    batchModeSourceStorage batchModeSrc;
} PACKED batchOptions;

typedef struct {
    bool removeConsoleData;
} PACKED ticketOptions;

typedef struct {
    bool isFat32;
    bool useLayeredFSDir;
} PACKED ncaFsOptions;

typedef struct {
    xciOptions xciDumpCfg;
    nspOptions nspDumpCfg;
    batchOptions batchDumpCfg;
    ticketOptions tikDumpCfg;
    ncaFsOptions exeFsDumpCfg;
    ncaFsOptions romFsDumpCfg;
} PACKED dumpOptions;

void loadConfig();
void saveConfig();

void closeGameCardStoragePartition();
Result openGameCardStoragePartition(openIStoragePartition partitionIndex);
Result readGameCardStoragePartition(u64 off, void *buf, size_t len);

void delay(u8 seconds);

void convertSize(u64 size, char *out, size_t outSize);
void updateFreeSpace();

void freeFilenameBuffer(void);

void initExeFsContext();
void freeExeFsContext();

void initRomFsContext();
void freeRomFsContext();

void initBktrContext();
void freeBktrContext();

void freeRomFsBrowserEntries();
void freeHfs0ExeFsEntriesSizes();

u64 hidKeysAllDown();
u64 hidKeysAllHeld();

void consoleErrorScreen(const char *fmt, ...);
bool initApplicationResources(int argc, char **argv);
void deinitApplicationResources();

bool appletModeCheck();
void appletModeOperationWarning();
void changeHomeButtonBlockStatus(bool block);

void formatETAString(u64 curTime, char *out, size_t outSize);

void generateSdCardEmmcTitleList();

bool loadTitlesFromSdCardAndEmmc(NcmContentMetaType metaType);
void freeTitlesFromSdCardAndEmmc(NcmContentMetaType metaType);

void removeIllegalCharacters(char *name);

void strtrim(char *str);

void loadTitleInfo();

void truncateBrowserEntryName(char *str);

bool getHfs0FileList(u32 partition);

bool readFileFromSecureHfs0PartitionByName(const char *filename, u64 offset, void *outBuf, size_t bufSize);

bool calculateExeFsExtractedDataSize(u64 *out);

bool calculateRomFsFullExtractedSize(bool usePatch, u64 *out);

bool calculateRomFsExtractedDirSize(u32 dir_offset, bool usePatch, u64 *out);

bool retrieveContentInfosFromTitle(NcmStorageId storageId, NcmContentMetaType metaType, u32 titleCount, u32 titleIndex, NcmContentInfo **outContentInfos, u32 *outContentInfoCnt);

void removeConsoleDataFromTicket(title_rights_ctx *rights_info);

bool readNcaExeFsSection(u32 titleIndex, bool usePatch);

bool readNcaRomFsSection(u32 titleIndex, selectedRomFsType curRomFsType, int desiredIdOffset);

bool getExeFsFileList();

bool getRomFsFileList(u32 dir_offset, bool usePatch);

char *generateGameCardDumpName(bool useBrackets);

char *generateNSPDumpName(nspDumpType selectedNspDumpType, u32 titleIndex, bool useBrackets);

void retrieveDescriptionForPatchOrAddOn(u32 titleIndex, bool addOn, bool addAppName, const char *prefix, char *outBuf, size_t outBufSize);

u32 calculateOrphanPatchOrAddOnCount(bool addOn);

void generateOrphanPatchOrAddOnList();

bool checkIfBaseApplicationHasPatchOrAddOn(u32 appIndex, bool addOn);

bool checkIfPatchOrAddOnBelongsToBaseApplication(u32 titleIndex, u32 appIndex, bool addOn);

u32 retrieveFirstPatchOrAddOnIndexFromBaseApplication(u32 appIndex, bool addOn);

u32 retrievePreviousPatchOrAddOnIndexFromBaseApplication(u32 startTitleIndex, u32 appIndex, bool addOn);

u32 retrieveNextPatchOrAddOnIndexFromBaseApplication(u32 startTitleIndex, u32 appIndex, bool addOn);

u32 retrieveLastPatchOrAddOnIndexFromBaseApplication(u32 appIndex, bool addOn);

void waitForButtonPress();

void printProgressBar(progress_ctx_t *progressCtx, bool calcData, u64 chunkSize);

void setProgressBarError(progress_ctx_t *progressCtx);

bool cancelProcessCheck(progress_ctx_t *progressCtx);

void convertDataToHexString(const u8 *data, const u32 dataSize, char *outBuf, const u32 outBufSize);

bool checkIfFileExists(const char *path);

bool yesNoPrompt(const char *message);

bool checkIfDumpedXciContainsCertificate(const char *xciPath);

bool checkIfDumpedNspContainsConsoleData(const char *nspPath);

void removeDirectoryWithVerbose(const char *path, const char *msg);

void gameCardDumpNSWDBCheck(u32 crc);

void noIntroDumpCheck(bool isDigital, u32 crc);

void updateNSWDBXml();

bool updateApplication();

#endif
