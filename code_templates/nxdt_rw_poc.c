/*
 * main.c
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

#include <core/nxdt_utils.h>
#include <core/gamecard.h>
#include <core/title.h>
#include <core/cnmt.h>
#include <core/program_info.h>
#include <core/nacp.h>
#include <core/legal_info.h>
#include <core/cert.h>
#include <core/usb.h>
#include <core/devoptab/nxdt_devoptab.h>

#define BLOCK_SIZE      USB_TRANSFER_BUFFER_SIZE
#define WAIT_TIME_LIMIT 30
#define OUTDIR          APP_TITLE

/* Type definitions. */

typedef struct _Menu Menu;

typedef u32  (*MenuElementOptionGetterFunction)(void);
typedef void (*MenuElementOptionSetterFunction)(u32 idx);
typedef bool (*MenuElementFunction)(void *userdata);

typedef struct {
    u32 selected;                                   ///< Used to keep track of the selected option.
    bool retrieved;                                 ///< Used to determine if the value for this option has already been retrieved from configuration.
    MenuElementOptionGetterFunction getter_func;    ///< Pointer to a function to be called the first time an option value is loaded. Should be set to NULL if not used.
    MenuElementOptionSetterFunction setter_func;    ///< Pointer to a function to be called each time a new option value is selected. Should be set to NULL if not used.
    char **options;                                 ///< Pointer to multiple char pointers with strings representing options. Last element must be set to NULL.
} MenuElementOption;

typedef struct {
    char *str;                                      ///< Pointer to a string to be printed for this menu element.
    Menu *child_menu;                               ///< Pointer to a child Menu element. Must be set to NULL if task_func != NULL.
    MenuElementFunction task_func;                  ///< Pointer to a function to be called by this element. Must be set to NULL if child_menu != NULL.
    MenuElementOption *element_options;             ///< Options for this menu element. Should be set to NULL if not used.
    void *userdata;                                 ///< Optional userdata pointer associated with this element. This is always passed to task_func as its only argument.
                                                    ///< This may or may be not used by the menu handler. Should be set to NULL if not used.
} MenuElement;

struct _Menu {
    u32 id;                                         ///< Identifier.
    struct _Menu *parent;                           ///< Set to NULL in the root menu element.
    u32 selected, scroll;                           ///< Used to keep track of the selected element and scroll values.
    MenuElement **elements;                         ///< Element info from this menu. Last element must be set to NULL.
};

typedef enum {
    MenuId_Root                 = 0,
    MenuId_GameCard             = 1,
    MenuId_XCI                  = 2,
    MenuId_DumpHFS              = 3,
    MenuId_BrowseHFS            = 4,
    MenuId_UserTitles           = 5,
    MenuId_UserTitlesSubMenu    = 6,
    MenuId_NSPTitleTypes        = 7,
    MenuId_NSP                  = 8,
    MenuId_TicketTitleTypes     = 9,
    MenuId_Ticket               = 10,
    MenuId_NcaTitleTypes        = 11,
    MenuId_Nca                  = 12,
    MenuId_NcaFsSections        = 13,
    MenuId_NcaFsSectionsSubMenu = 14,
    MenuId_SystemTitles         = 15,
    MenuId_Count                = 16
} MenuId;

typedef struct
{
    FILE *fp;
    void *data;
    size_t data_size;
    size_t data_written;
    size_t total_size;
    bool read_error;
    bool write_error;
    bool transfer_cancelled;
} SharedThreadData;

typedef struct {
    SharedThreadData shared_thread_data;
    u32 xci_crc, full_xci_crc;
} XciThreadData;

typedef struct {
    SharedThreadData shared_thread_data;
    HashFileSystemContext *hfs_ctx;
} HfsThreadData;

typedef struct {
    void *data;
    size_t data_written;
    size_t total_size;
    bool error;
    bool transfer_cancelled;
} NspThreadData;

typedef struct {
    TitleInfo *title_info;
    u32 content_idx;
} NcaUserData;

typedef struct {
    SharedThreadData shared_thread_data;
    NcaContext *nca_ctx;
} NcaThreadData;

typedef struct {
    SharedThreadData shared_thread_data;
    PartitionFileSystemContext *pfs_ctx;
    bool use_layeredfs_dir;
} PfsThreadData;

typedef struct {
    SharedThreadData shared_thread_data;
    RomFileSystemContext *romfs_ctx;
    bool use_layeredfs_dir;
} RomFsThreadData;

typedef struct {
    bool highlight;
    size_t size;
    char size_str[0x10];
    struct dirent dt;
} FsBrowserEntry;

typedef struct {
    SharedThreadData shared_thread_data;
    FILE *src;
} FsBrowserFileThreadData;

typedef struct {
    SharedThreadData shared_thread_data;
    const char *dir_path;
    const FsBrowserEntry *entries;
    u32 entries_count;
    const char *base_out_path;
} FsBrowserHighlightedEntriesThreadData;

/* Function prototypes. */

static void utilsScanPads(void);
static u64 utilsGetButtonsDown(void);
static u64 utilsGetButtonsHeld(void);
static u64 utilsWaitForButtonPress(u64 flag);

static void consolePrint(const char *text, ...);
static void consolePrintReversedColors(const char *text, ...);
static void consoleRefresh(void);

static u32 menuGetElementCount(const Menu *menu);
static void menuResetAttributes(Menu *cur_menu, u32 element_count);

void freeStorageList(void);
void updateStorageList(void);

void freeTitleList(Menu *menu);
void updateTitleList(Menu *menu, Menu *submenu, bool is_system);

static TitleInfo *getLatestTitleInfo(TitleInfo *title_info, u32 *out_idx, u32 *out_count);

void freeNcaList(void);
void updateNcaList(TitleInfo *title_info, u32 *element_count);
static void switchNcaListTitle(Menu **cur_menu, u32 *element_count, TitleInfo *title_info);

void freeNcaFsSectionsList(void);
void updateNcaFsSectionsList(NcaUserData *nca_user_data);

void freeNcaBasePatchList(void);
void updateNcaBasePatchList(TitleUserApplicationData *user_app_data, TitleInfo *title_info, NcaFsSectionContext *nca_fs_ctx);

NX_INLINE bool useUsbHost(void);

static bool waitForGameCard(void);
static bool waitForUsb(void);

static char *generateOutputGameCardFileName(const char *subdir, const char *extension, bool use_nacp_name);
static char *generateOutputTitleFileName(TitleInfo *title_info, const char *subdir, const char *extension);
static char *generateOutputLayeredFsFileName(u64 title_id, const char *subdir, const char *extension);

static bool dumpGameCardSecurityInformation(GameCardSecurityInformation *out);

static bool resetSettings(void *userdata);

static bool saveGameCardImage(void *userdata);
static bool saveGameCardHeader(void *userdata);
static bool saveGameCardCardInfo(void *userdata);
static bool saveGameCardCertificate(void *userdata);
static bool saveGameCardInitialData(void *userdata);
static bool saveGameCardSpecificData(void *userdata);
static bool saveGameCardIdSet(void *userdata);
static bool saveGameCardUid(void *userdata);
static bool saveGameCardHfsPartition(void *userdata);
static bool saveGameCardRawHfsPartition(HashFileSystemContext *hfs_ctx);
static bool saveGameCardExtractedHfsPartition(HashFileSystemContext *hfs_ctx);
static bool browseGameCardHfsPartition(void *userdata);

static bool saveConsoleLafwBlob(void *userdata);

static bool saveNintendoSubmissionPackage(void *userdata);

static bool saveTicket(void *userdata);

static bool saveNintendoContentArchive(void *userdata);
static bool saveNintendoContentArchiveFsSection(void *userdata);
static bool browseNintendoContentArchiveFsSection(void *userdata);

static bool fsBrowser(const char *mount_name, const char *base_out_path);
static bool fsBrowserGetDirEntries(const char *dir_path, FsBrowserEntry **out_entries, u32 *out_entry_count);
static bool fsBrowserDumpFile(const char *dir_path, const FsBrowserEntry *entry, const char *base_out_path);
static bool fsBrowserDumpHighlightedEntries(const char *dir_path, const FsBrowserEntry *entries, u32 entries_count, const char *base_out_path);

static bool initializeNcaFsContext(void *userdata, u8 *out_section_type, bool *out_use_layeredfs_dir, NcaContext **out_base_patch_nca_ctx, void **out_fs_ctx);

static bool saveRawPartitionFsSection(PartitionFileSystemContext *pfs_ctx, bool use_layeredfs_dir);
static bool saveExtractedPartitionFsSection(PartitionFileSystemContext *pfs_ctx, bool use_layeredfs_dir);

static bool saveRawRomFsSection(RomFileSystemContext *romfs_ctx, bool use_layeredfs_dir);
static bool saveExtractedRomFsSection(RomFileSystemContext *romfs_ctx, bool use_layeredfs_dir);

static void xciReadThreadFunc(void *arg);

static void rawHfsReadThreadFunc(void *arg);
static void extractedHfsReadThreadFunc(void *arg);

static void ncaReadThreadFunc(void *arg);

static void rawPartitionFsReadThreadFunc(void *arg);
static void extractedPartitionFsReadThreadFunc(void *arg);

static void rawRomFsReadThreadFunc(void *arg);
static void extractedRomFsReadThreadFunc(void *arg);

static void fsBrowserFileReadThreadFunc(void *arg);
static void fsBrowserHighlightedEntriesReadThreadFunc(void *arg);
static bool fsBrowserHighlightedEntriesReadThreadLoop(SharedThreadData *shared_thread_data, const char *dir_path, const FsBrowserEntry *entries, u32 entries_count, const char *base_out_path, void *buf1, void *buf2);

static void genericWriteThreadFunc(void *arg);

static bool spanDumpThreads(ThreadFunc read_func, ThreadFunc write_func, void *arg);

static void nspThreadFunc(void *arg);

static u32 getOutputStorageOption(void);
static void setOutputStorageOption(u32 idx);

static u32 getGameCardPrependKeyAreaOption(void);
static void setGameCardPrependKeyAreaOption(u32 idx);

static u32 getGameCardKeepCertificateOption(void);
static void setGameCardKeepCertificateOption(u32 idx);

static u32 getGameCardTrimDumpOption(void);
static void setGameCardTrimDumpOption(u32 idx);

static u32 getGameCardCalculateChecksumOption(void);
static void setGameCardCalculateChecksumOption(u32 idx);

static u32 getGameCardWriteRawHfsPartitionOption(void);
static void setGameCardWriteRawHfsPartitionOption(u32 idx);

static u32 getNspSetDownloadDistributionOption(void);
static void setNspSetDownloadDistributionOption(u32 idx);

static u32 getNspRemoveConsoleDataOption(void);
static void setNspRemoveConsoleDataOption(u32 idx);

static u32 getNspRemoveTitlekeyCryptoOption(void);
static void setNspRemoveTitlekeyCryptoOption(u32 idx);

static u32 getNspDisableLinkedAccountRequirementOption(void);
static void setNspDisableLinkedAccountRequirementOption(u32 idx);

static u32 getNspEnableScreenshotsOption(void);
static void setNspEnableScreenshotsOption(u32 idx);

static u32 getNspEnableVideoCaptureOption(void);
static void setNspEnableVideoCaptureOption(u32 idx);

static u32 getNspDisableHdcpOption(void);
static void setNspDisableHdcpOption(u32 idx);

static u32 getNspGenerateAuthoringToolDataOption(void);
static void setNspGenerateAuthoringToolDataOption(u32 idx);

static u32 getTicketRemoveConsoleDataOption(void);
static void setTicketRemoveConsoleDataOption(u32 idx);

static u32 getNcaFsWriteRawSectionOption(void);
static void setNcaFsWriteRawSectionOption(u32 idx);

static u32 getNcaFsUseLayeredFsDirOption(void);
static void setNcaFsUseLayeredFsDirOption(u32 idx);

/* Global variables. */

bool g_borealisInitialized = false;

static PadState g_padState = {0};

static char *g_noYesStrings[] = { "no", "yes", NULL };

static bool g_appletStatus = true;

static UsbHsFsDevice *g_umsDevices = NULL;
static u32 g_umsDeviceCount = 0;
static char **g_storageOptions = NULL;

static MenuElementOption g_storageMenuElementOption = {
    .selected = 0,
    .retrieved = false,
    .getter_func = &getOutputStorageOption,
    .setter_func = &setOutputStorageOption,
    .options = NULL // Dynamically set
};

static MenuElement g_storageMenuElement = {
    .str = "output storage",
    .child_menu = NULL,
    .task_func = NULL,
    .element_options = &g_storageMenuElementOption,
    .userdata = NULL
};

static MenuElement *g_xciMenuElements[] = {
    &(MenuElement){
        .str = "start xci dump",
        .child_menu = NULL,
        .task_func = &saveGameCardImage,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "prepend key area",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getGameCardPrependKeyAreaOption,
            .setter_func = &setGameCardPrependKeyAreaOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "keep certificate",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getGameCardKeepCertificateOption,
            .setter_func = &setGameCardKeepCertificateOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "trim dump",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getGameCardTrimDumpOption,
            .setter_func = &setGameCardTrimDumpOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "calculate checksum",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
            .retrieved = false,
            .getter_func = &getGameCardCalculateChecksumOption,
            .setter_func = &setGameCardCalculateChecksumOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &g_storageMenuElement,
    NULL
};

static u32 g_hfsRootPartition = HashFileSystemPartitionType_Root;
static u32 g_hfsUpdatePartition = HashFileSystemPartitionType_Update;
static u32 g_hfsLogoPartition = HashFileSystemPartitionType_Logo;
static u32 g_hfsNormalPartition = HashFileSystemPartitionType_Normal;
static u32 g_hfsSecurePartition = HashFileSystemPartitionType_Secure;

static MenuElement *g_gameCardHfsDumpMenuElements[] = {
    &(MenuElement){
        .str = "dump root hfs partition",
        .child_menu = NULL,
        .task_func = &saveGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsRootPartition
    },
    &(MenuElement){
        .str = "dump update hfs partition",
        .child_menu = NULL,
        .task_func = &saveGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsUpdatePartition
    },
    &(MenuElement){
        .str = "dump logo hfs partition",
        .child_menu = NULL,
        .task_func = &saveGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsLogoPartition
    },
    &(MenuElement){
        .str = "dump normal hfs partition",
        .child_menu = NULL,
        .task_func = &saveGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsNormalPartition
    },
    &(MenuElement){
        .str = "dump secure hfs partition",
        .child_menu = NULL,
        .task_func = &saveGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsSecurePartition
    },
    &(MenuElement){
        .str = "write raw hfs partition",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getGameCardWriteRawHfsPartitionOption,
            .setter_func = &setGameCardWriteRawHfsPartitionOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &g_storageMenuElement,
    NULL
};

static MenuElement *g_gameCardHfsBrowseMenuElements[] = {
    &(MenuElement){
        .str = "browse root hfs partition",
        .child_menu = NULL,
        .task_func = &browseGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsRootPartition
    },
    &(MenuElement){
        .str = "browse update hfs partition",
        .child_menu = NULL,
        .task_func = &browseGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsUpdatePartition
    },
    &(MenuElement){
        .str = "browse logo hfs partition",
        .child_menu = NULL,
        .task_func = &browseGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsLogoPartition
    },
    &(MenuElement){
        .str = "browse normal hfs partition",
        .child_menu = NULL,
        .task_func = &browseGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsNormalPartition
    },
    &(MenuElement){
        .str = "browse secure hfs partition",
        .child_menu = NULL,
        .task_func = &browseGameCardHfsPartition,
        .element_options = NULL,
        .userdata = &g_hfsSecurePartition
    },
    &g_storageMenuElement,
    NULL
};

static MenuElement *g_gameCardMenuElements[] = {
    &(MenuElement){
        .str = "dump gamecard image (xci)",
        .child_menu = &(Menu){
            .id = MenuId_XCI,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_xciMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard initial data",
        .child_menu = NULL,
        .task_func = &saveGameCardInitialData,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard certificate",
        .child_menu = NULL,
        .task_func = &saveGameCardCertificate,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard id set",
        .child_menu = NULL,
        .task_func = &saveGameCardIdSet,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard uid",
        .child_menu = NULL,
        .task_func = &saveGameCardUid,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard header (optional)",
        .child_menu = NULL,
        .task_func = &saveGameCardHeader,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard cardinfo (optional)",
        .child_menu = NULL,
        .task_func = &saveGameCardCardInfo,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard specific data (optional)",
        .child_menu = NULL,
        .task_func = &saveGameCardSpecificData,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump hfs partitions (optional)",
        .child_menu = &(Menu){
            .id = MenuId_DumpHFS,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_gameCardHfsDumpMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "browse hfs partitions (optional)",
        .child_menu = &(Menu){
            .id = MenuId_BrowseHFS,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_gameCardHfsBrowseMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump console lafw blob (optional)",
        .child_menu = NULL,
        .task_func = &saveConsoleLafwBlob,
        .element_options = NULL,
        .userdata = NULL
    },
    &g_storageMenuElement,
    NULL
};

static MenuElement *g_nspMenuElements[] = {
    &(MenuElement){
        .str = "start nsp dump",
        .child_menu = NULL,
        .task_func = &saveNintendoSubmissionPackage,
        .element_options = NULL,
        .userdata = NULL    // Dynamically set to the TitleInfo object from the title to dump
    },
    &(MenuElement){
        .str = "nca: set content distribution type to \"download\"",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getNspSetDownloadDistributionOption,
            .setter_func = &setNspSetDownloadDistributionOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "tik: remove console specific data",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getNspRemoveConsoleDataOption,
            .setter_func = &setNspRemoveConsoleDataOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nca/tik: remove titlekey crypto (overrides previous option)",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getNspRemoveTitlekeyCryptoOption,
            .setter_func = &setNspRemoveTitlekeyCryptoOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nacp: disable linked account requirement",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
            .retrieved = false,
            .getter_func = &getNspDisableLinkedAccountRequirementOption,
            .setter_func = &setNspDisableLinkedAccountRequirementOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nacp: enable screenshots",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
            .retrieved = false,
            .getter_func = &getNspEnableScreenshotsOption,
            .setter_func = &setNspEnableScreenshotsOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nacp: enable video capture",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
            .retrieved = false,
            .getter_func = &getNspEnableVideoCaptureOption,
            .setter_func = &setNspEnableVideoCaptureOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nacp: disable hdcp",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
            .retrieved = false,
            .getter_func = &getNspDisableHdcpOption,
            .setter_func = &setNspDisableHdcpOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nsp: generate authoringtool data",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
            .retrieved = false,
            .getter_func = &getNspGenerateAuthoringToolDataOption,
            .setter_func = &setNspGenerateAuthoringToolDataOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &g_storageMenuElement,
    NULL
};

static Menu g_nspMenu = {
    .id = MenuId_NSP,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_nspMenuElements
};

static MenuElement *g_ticketMenuElements[] = {
    &(MenuElement){
        .str = "start ticket dump",
        .child_menu = NULL,
        .task_func = &saveTicket,
        .element_options = NULL,
        .userdata = NULL    // Dynamically set to the TitleInfo object from the title to dump
    },
    &(MenuElement){
        .str = "remove console specific data",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getTicketRemoveConsoleDataOption,
            .setter_func = &setTicketRemoveConsoleDataOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &g_storageMenuElement,
    NULL
};

static Menu g_ticketMenu = {
    .id = MenuId_Ticket,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_ticketMenuElements
};

static TitleInfo *g_ncaUserTitleInfo = NULL, *g_ncaBasePatchTitleInfo = NULL, *g_ncaBasePatchTitleInfoBkp = NULL;
static char **g_ncaBasePatchOptions = NULL;

static MenuElementOption g_ncaFsSectionsSubMenuBasePatchElementOption = {
    .selected = 0,
    .retrieved = false,
    .getter_func = NULL,
    .setter_func = NULL,
    .options = NULL // Dynamically set
};

static MenuElement *g_ncaFsSectionsSubMenuElements[] = {
    &(MenuElement){
        .str = "start nca fs section dump",
        .child_menu = NULL,
        .task_func = &saveNintendoContentArchiveFsSection,
        .element_options = NULL,
        .userdata = NULL    // Dynamically set
    },
    &(MenuElement){
        .str = "browse nca fs section",
        .child_menu = NULL,
        .task_func = &browseNintendoContentArchiveFsSection,
        .element_options = NULL,
        .userdata = NULL    // Dynamically set
    },
    &(MenuElement){
        .str = "use base/patch title",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &g_ncaFsSectionsSubMenuBasePatchElementOption,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "write raw section",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getNcaFsWriteRawSectionOption,
            .setter_func = &setNcaFsWriteRawSectionOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "use layeredfs dir",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .retrieved = false,
            .getter_func = &getNcaFsUseLayeredFsDirOption,
            .setter_func = &setNcaFsUseLayeredFsDirOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &g_storageMenuElement,
    NULL
};

static Menu g_ncaFsSectionsSubMenu = {
    .id = MenuId_NcaFsSectionsSubMenu,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_ncaFsSectionsSubMenuElements
};

static bool g_ncaMenuRawMode = false;
static NcaContext *g_ncaFsSectionsMenuCtx = NULL;

static MenuElement **g_ncaFsSectionsMenuElements = NULL;

// Dynamically populated using g_ncaFsSectionsMenuElements.
static Menu g_ncaFsSectionsMenu = {
    .id = MenuId_NcaFsSections,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = NULL
};

static MenuElement **g_ncaMenuElements = NULL;

// Dynamically populated using g_ncaMenuElements.
static Menu g_ncaMenu = {
    .id = MenuId_Nca,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = NULL
};

static u32 g_metaTypeApplication = NcmContentMetaType_Application;
static u32 g_metaTypePatch = NcmContentMetaType_Patch;
static u32 g_metaTypeAOC = NcmContentMetaType_AddOnContent;
static u32 g_metaTypeAOCPatch = NcmContentMetaType_DataPatch;

static MenuElement *g_titleTypesMenuElements[] = {
    &(MenuElement){
        .str = "dump base application",
        .child_menu = NULL, // Dynamically set
        .task_func = NULL,
        .element_options = NULL,
        .userdata = &g_metaTypeApplication
    },
    &(MenuElement){
        .str = "dump update",
        .child_menu = NULL, // Dynamically set
        .task_func = NULL,
        .element_options = NULL,
        .userdata = &g_metaTypePatch
    },
    &(MenuElement){
        .str = "dump dlc",
        .child_menu = NULL, // Dynamically set
        .task_func = NULL,
        .element_options = NULL,
        .userdata = &g_metaTypeAOC
    },
    &(MenuElement){
        .str = "dump dlc update",
        .child_menu = NULL, // Dynamically set
        .task_func = NULL,
        .element_options = NULL,
        .userdata = &g_metaTypeAOCPatch
    },
    NULL
};

static MenuElement *g_userTitlesSubMenuElements[] = {
    &(MenuElement){
        .str = "nsp dump options",
        .child_menu = &(Menu){
            .id = MenuId_NSPTitleTypes,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_titleTypesMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "ticket dump options",
        .child_menu = &(Menu){
            .id = MenuId_TicketTitleTypes,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_titleTypesMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nca / nca fs dump options",
        .child_menu = &(Menu){
            .id = MenuId_NcaTitleTypes,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_titleTypesMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    NULL
};

// Dynamically set as child_menu for all g_userTitlesMenu entries.
static Menu g_userTitlesSubMenu = {
    .id = MenuId_UserTitlesSubMenu,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_userTitlesSubMenuElements
};

// Dynamically populated.
static Menu g_userTitlesMenu = {
    .id = MenuId_UserTitles,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = NULL
};

// Dynamically populated.
static Menu g_systemTitlesMenu = {
    .id = MenuId_SystemTitles,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = NULL
};

static MenuElement *g_rootMenuElements[] = {
    &(MenuElement){
        .str = "gamecard menu",
        .child_menu = &(Menu){
            .id = MenuId_GameCard,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_gameCardMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "user titles menu",
        .child_menu = &g_userTitlesMenu,
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "system titles menu",
        .child_menu = &g_systemTitlesMenu,
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "reset settings",
        .child_menu = NULL,
        .task_func = &resetSettings,
        .element_options = NULL,
        .userdata = NULL
    },
    NULL
};

static Menu g_rootMenu = {
    .id = MenuId_Root,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_rootMenuElements
};

static Mutex g_conMutex = 0, g_fileMutex = 0;
static CondVar g_readCondvar = 0, g_writeCondvar = 0;

static char path[FS_MAX_PATH * 2] = {0};

int main(int argc, char *argv[])
{
    NX_IGNORE_ARG(argc);
    NX_IGNORE_ARG(argv);

    int ret = EXIT_SUCCESS;

    if (!utilsInitializeResources())
    {
        ret = EXIT_FAILURE;
        goto end;
    }

    /* Configure input. */
    /* Up to 8 different, full controller inputs. */
    /* Individual Joy-Cons not supported. */
    padConfigureInput(8, HidNpadStyleSet_NpadFullCtrl);
    padInitializeWithMask(&g_padState, 0x1000000FFUL);

    consoleInit(NULL);

    updateStorageList();

    updateTitleList(&g_userTitlesMenu, &g_userTitlesSubMenu, false);
    updateTitleList(&g_systemTitlesMenu, &g_ncaMenu, true);

    Menu *cur_menu = &g_rootMenu;
    u32 element_count = menuGetElementCount(cur_menu), page_size = 20;

    TitleApplicationMetadata *app_metadata = NULL;

    TitleUserApplicationData user_app_data = {0};

    TitleInfo *title_info = NULL;
    u32 title_info_idx = 0, title_info_count = 0;

    bool is_system = false;

    while(appletMainLoop())
    {
        MenuElement *selected_element = ((cur_menu->elements && element_count && cur_menu->selected < element_count) ? cur_menu->elements[cur_menu->selected] : NULL);
        MenuElementOption *selected_element_options = (selected_element ? selected_element->element_options : NULL);

        if (cur_menu->id == MenuId_UserTitlesSubMenu && selected_element && selected_element->child_menu)
        {
            /* Set title types child menu pointer if we're currently at the user titles submenu. */
            u32 child_id = selected_element->child_menu->id;

            g_titleTypesMenuElements[0]->child_menu = g_titleTypesMenuElements[1]->child_menu = \
            g_titleTypesMenuElements[2]->child_menu = g_titleTypesMenuElements[3]->child_menu = (child_id == MenuId_NSPTitleTypes ? &g_nspMenu : \
                                                                                                (child_id == MenuId_TicketTitleTypes ? &g_ticketMenu : \
                                                                                                (child_id == MenuId_NcaTitleTypes ? &g_ncaMenu : NULL)));
        }

        consoleClear();

        consolePrint(APP_TITLE " v" APP_VERSION " (" GIT_REV ").\nBuilt on " BUILD_TIMESTAMP ".\n");
        consolePrint("______________________________\n\n");
        if (cur_menu->parent) consolePrint("press b to go back\n");
        if (g_umsDeviceCount) consolePrint("press x to safely remove all ums devices\n");
        consolePrint("use the sticks to scroll faster\n");
        consolePrint("press + to exit\n");
        consolePrint("______________________________\n\n");

        if ((cur_menu->id == MenuId_UserTitles || cur_menu->id == MenuId_SystemTitles) && selected_element)
        {
            app_metadata = (TitleApplicationMetadata*)selected_element->userdata;

            consolePrint("title: %u / %u\n", cur_menu->selected + 1, element_count);
            consolePrint("selected title: %016lX - %s\n", app_metadata->title_id, selected_element->str);
            consolePrint("______________________________\n\n");
        } else
        if (cur_menu->id >= MenuId_UserTitlesSubMenu && cur_menu->id < MenuId_SystemTitles)
        {
            if (!is_system)
            {
                consolePrint("title info:\n\n");
                consolePrint("name: %s\n", app_metadata->lang_entry.name);
                consolePrint("publisher: %s\n", app_metadata->lang_entry.author);
                if (cur_menu->id == MenuId_UserTitlesSubMenu || cur_menu->id == MenuId_NSPTitleTypes || cur_menu->id == MenuId_TicketTitleTypes || \
                    cur_menu->id == MenuId_NcaTitleTypes) consolePrint("title id: %016lX\n", app_metadata->title_id);
                consolePrint("______________________________\n\n");
            }

            if (cur_menu->id == MenuId_NSP || cur_menu->id == MenuId_Ticket || cur_menu->id == MenuId_Nca || \
                cur_menu->id == MenuId_NcaFsSections || cur_menu->id == MenuId_NcaFsSectionsSubMenu)
            {
                if (cur_menu->id != MenuId_NcaFsSections && cur_menu->id != MenuId_NcaFsSectionsSubMenu && (title_info->previous || title_info->next))
                {
                    consolePrintReversedColors("press l/zl/r/zr to change the selected title\n");
                    consolePrintReversedColors("title: %u / %u\n", title_info_idx + 1, title_info_count);
                    consolePrint("______________________________\n\n");
                }

                consolePrint("selected title info:\n\n");
                if (is_system) consolePrint("name: %s\n", app_metadata->lang_entry.name);
                consolePrint("title id: %016lX\n", title_info->meta_key.id);
                consolePrint("type: %s\n", titleGetNcmContentMetaTypeName(title_info->meta_key.type));
                consolePrint("source storage: %s\n", titleGetNcmStorageIdName(title_info->storage_id));
                consolePrint("version: %u (%u.%u.%u-%u.%u)\n", title_info->version.value, title_info->version.system_version.major, title_info->version.system_version.minor, \
                             title_info->version.system_version.micro, title_info->version.system_version.major_relstep, title_info->version.system_version.minor_relstep);
                consolePrint("content count: %u\n", title_info->content_count);
                consolePrint("size: %s\n", title_info->size_str);
                consolePrint("______________________________\n\n");

                if (cur_menu->id == MenuId_NSP) g_nspMenuElements[0]->userdata = title_info;

                if (cur_menu->id == MenuId_Ticket) g_ticketMenuElements[0]->userdata = title_info;

                if (cur_menu->id == MenuId_Nca)
                {
                    consolePrintReversedColors("current mode: %s\n", g_ncaMenuRawMode ? "raw nca" : "nca fs section");
                    consolePrintReversedColors("press y to switch to %s mode\n", g_ncaMenuRawMode ? "nca fs section" : "raw nca");
                    consolePrint("______________________________\n\n");
                }

                if (cur_menu->id == MenuId_NcaFsSections || cur_menu->id == MenuId_NcaFsSectionsSubMenu)
                {
                    consolePrint("selected nca info:\n\n");
                    consolePrint("content id: %s\n", g_ncaFsSectionsMenuCtx->content_id_str);
                    consolePrint("content type: %s\n", titleGetNcmContentTypeName(g_ncaFsSectionsMenuCtx->content_type));
                    consolePrint("id offset: %u\n", g_ncaFsSectionsMenuCtx->id_offset);
                    consolePrint("size: %s\n", g_ncaFsSectionsMenuCtx->content_size_str);
                    consolePrint("______________________________\n\n");
                }

                if (cur_menu->id == MenuId_NcaFsSectionsSubMenu)
                {
                    NcaFsSectionContext *nca_fs_ctx = (NcaFsSectionContext*)g_ncaFsSectionsSubMenuElements[0]->userdata;
                    consolePrint("selected nca fs section info:\n");
                    consolePrint("section index: %u\n", nca_fs_ctx->section_idx);
                    consolePrint("section type: %s\n", ncaGetFsSectionTypeName(nca_fs_ctx));
                    consolePrint("section size: %s\n", nca_fs_ctx->section_size_str);
                    consolePrint("______________________________\n\n");
                }
            }
        } else
        if (cur_menu->id == MenuId_GameCard) {
            consolePrint("For a full gamecard image: dump XCI, initial data, certificate, id set and uid.\n");
            consolePrint("______________________________\n\n");
        }

        for(u32 i = cur_menu->scroll; i < element_count; i++)
        {
            if (i >= (cur_menu->scroll + page_size)) break;

            MenuElement *cur_element = cur_menu->elements[i];
            MenuElementOption *cur_options = cur_element->element_options;
            TitleApplicationMetadata *cur_app_metadata = ((cur_menu->id == MenuId_UserTitles || cur_menu->id == MenuId_SystemTitles) ? (TitleApplicationMetadata*)cur_element->userdata : NULL);

            consolePrint("%s", i == cur_menu->selected ? " -> " : "    ");
            if (cur_app_metadata) consolePrint("%016lX - ", cur_app_metadata->title_id);
            consolePrint("%s", cur_element->str);

            if (cur_options)
            {
                if (cur_options->getter_func && !cur_options->retrieved)
                {
                    cur_options->selected = cur_options->getter_func();
                    cur_options->retrieved = true;
                }

                consolePrint(": ");
                if (cur_options->selected > 0) consolePrint("< ");
                consolePrint("%s", cur_options->options[cur_options->selected]);
                if (cur_options->options[cur_options->selected + 1]) consolePrint(" >");
            }

            consolePrint("\n");
        }

        if (!element_count) consolePrint("no elements available! press b to go back");

        consolePrint("\n");
        consoleRefresh();

        bool data_update = false;
        u64 btn_down = 0, btn_held = 0;

        while((g_appletStatus = appletMainLoop()))
        {
            utilsScanPads();
            btn_down = utilsGetButtonsDown();
            btn_held = utilsGetButtonsHeld();
            if (btn_down || btn_held) break;

            if (umsIsDeviceInfoUpdated())
            {
                updateStorageList();
                data_update = true;
                break;
            }

            if (titleIsGameCardInfoUpdated())
            {
                updateTitleList(&g_userTitlesMenu, &g_userTitlesSubMenu, false);
                if (cur_menu->id == MenuId_UserTitles) element_count = menuGetElementCount(cur_menu);
                g_userTitlesMenu.selected = g_userTitlesMenu.scroll = 0;
                data_update = true;
                break;
            }

            utilsAppletLoopDelay();
        }

        if (!g_appletStatus) break;

        if (data_update) continue;

        if ((btn_down & HidNpadButton_A) && selected_element)
        {
            Menu *child_menu = selected_element->child_menu;

            if (child_menu)
            {
                bool error = false;

                /* Only change menus if a valid ID was set. */
                if (child_menu->id == MenuId_Root || child_menu->id >= MenuId_Count || child_menu->id == cur_menu->id) continue;

                /* Retrieve extra data based on the current menu ID. */

                if (child_menu->id == MenuId_UserTitlesSubMenu)
                {
                    error = !titleGetUserApplicationData(app_metadata->title_id, &user_app_data);
                    if (error) consolePrint("\nfailed to get user application data for %016lX!\n", app_metadata->title_id);
                } else
                if (child_menu->id == MenuId_NSP || child_menu->id == MenuId_Ticket || child_menu->id == MenuId_Nca)
                {
                    u32 title_type = (cur_menu->id != MenuId_SystemTitles ? *((u32*)selected_element->userdata) : NcmContentMetaType_Unknown);

                    switch(title_type)
                    {
                        case NcmContentMetaType_Application:
                            title_info = user_app_data.app_info;
                            break;
                        case NcmContentMetaType_Patch:
                            title_info = user_app_data.patch_info;
                            break;
                        case NcmContentMetaType_AddOnContent:
                            title_info = user_app_data.aoc_info;
                            break;
                        case NcmContentMetaType_DataPatch:
                            title_info = user_app_data.aoc_patch_info;
                            break;
                        default:
                            /* Get TitleInfo element on demand. */
                            title_info = titleGetInfoFromStorageByTitleId(NcmStorageId_BuiltInSystem, app_metadata->title_id);
                            break;
                    }

                    if (title_info)
                    {
                        title_info = getLatestTitleInfo(title_info, &title_info_idx, &title_info_count);

                        if (child_menu->id == MenuId_Nca)
                        {
                            updateNcaList(title_info, &element_count);

                            if (!g_ncaMenuElements || !g_ncaMenuElements[0])
                            {
                                consolePrint("failed to generate nca list\n");
                                error = true;
                            }

                            if (!error && cur_menu->id == MenuId_SystemTitles) is_system = true;
                        }
                    } else {
                        if (cur_menu->id == MenuId_SystemTitles)
                        {
                            consolePrint("\nunable to retrieve data for system title %016lX\n", app_metadata->title_id);
                        } else {
                            consolePrint("\nthe selected title doesn't have available %s data\n", \
                                        title_type == NcmContentMetaType_Application ? "base application" : \
                                        (title_type == NcmContentMetaType_Patch ? "update" : (title_type == NcmContentMetaType_AddOnContent ? "dlc" : "dlc update")));
                        }

                        error = true;
                    }
                } else
                if (child_menu->id == MenuId_NcaFsSections)
                {
                    updateNcaFsSectionsList((NcaUserData*)selected_element->userdata);

                    if (!g_ncaFsSectionsMenuElements || !g_ncaFsSectionsMenuElements[0])
                    {
                        consolePrint("failed to generate nca fs sections list\n");
                        error = true;
                    }
                } else
                if (child_menu->id == MenuId_NcaFsSectionsSubMenu)
                {
                    NcaFsSectionContext *nca_fs_ctx = selected_element->userdata;
                    if (nca_fs_ctx->enabled)
                    {
                        updateNcaBasePatchList(&user_app_data, title_info, nca_fs_ctx);
                    } else {
                        consolePrint("can't dump an invalid nca fs section!\n");
                        error = true;
                    }
                }

                if (!error)
                {
                    child_menu->parent = cur_menu;
                    cur_menu = child_menu;
                    element_count = menuGetElementCount(cur_menu);
                } else {
                    consolePrint("press any button to go back\n");
                    consoleRefresh();
                    utilsWaitForButtonPress(0);
                }
            } else
            if (selected_element->task_func)
            {
                bool show_button_prompt = true;

                consoleClear();

                /* Wait for gamecard (if needed). */
                if (((cur_menu->id >= MenuId_GameCard && cur_menu->id <= MenuId_BrowseHFS) || (title_info && title_info->storage_id == NcmStorageId_GameCard)) && !waitForGameCard())
                {
                    if (g_appletStatus) continue;
                    break;
                }

                if ((cur_menu->id == MenuId_NcaFsSectionsSubMenu && cur_menu->selected == 1) || cur_menu->id == MenuId_BrowseHFS)
                {
                    show_button_prompt = false;

                    /* Ignore result. */
                    selected_element->task_func(selected_element->userdata);

                    /* Update free space. */
                    if (!useUsbHost()) updateStorageList();
                } else
                if (cur_menu->id > MenuId_Root)
                {
                    /* Wait for USB session (if needed). */
                    if (useUsbHost() && !waitForUsb())
                    {
                        if (g_appletStatus) continue;
                        break;
                    }

                    /* Run task. */
                    utilsSetLongRunningProcessState(true);

                    if (selected_element->task_func(selected_element->userdata))
                    {
                        if (!useUsbHost()) updateStorageList(); // update free space
                    }

                    utilsSetLongRunningProcessState(false);
                }

                if (g_appletStatus && show_button_prompt)
                {
                    /* Display prompt. */
                    consolePrint("press any button to continue");
                    utilsWaitForButtonPress(0);
                }
            }
        } else
        if (((btn_down & HidNpadButton_Down) || (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown))) && element_count)
        {
            cur_menu->selected++;

            if (!cur_menu->elements[cur_menu->selected])
            {
                if (btn_down & HidNpadButton_Down)
                {
                    cur_menu->selected = 0;
                    cur_menu->scroll = 0;
                } else {
                    cur_menu->selected--;
                }
            } else
            if (cur_menu->selected >= (cur_menu->scroll + (page_size / 2)) && element_count > (cur_menu->scroll + page_size))
            {
                cur_menu->scroll++;
            }
        } else
        if (((btn_down & HidNpadButton_Up) || (btn_held & (HidNpadButton_StickLUp | HidNpadButton_StickRUp))) && element_count)
        {
            cur_menu->selected--;

            if (cur_menu->selected == UINT32_MAX)
            {
                if (btn_down & HidNpadButton_Up)
                {
                    cur_menu->selected = (element_count - 1);
                    cur_menu->scroll = (element_count >= page_size ? (element_count - page_size) : 0);
                } else {
                    cur_menu->selected = 0;
                }
            } else
            if (cur_menu->selected < (cur_menu->scroll + (page_size / 2)) && cur_menu->scroll > 0)
            {
                cur_menu->scroll--;
            }
        } else
        if ((btn_down & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_StickRRight)) && selected_element_options)
        {
            /* Point to the next base/patch title. */
            if (cur_menu->id == MenuId_NcaFsSectionsSubMenu && cur_menu->selected == 2)
            {
                if (selected_element_options->selected == 0 && g_ncaBasePatchTitleInfoBkp)
                {
                    g_ncaBasePatchTitleInfo = g_ncaBasePatchTitleInfoBkp;
                    g_ncaBasePatchTitleInfoBkp = NULL;
                } else
                if (selected_element_options->selected > 0 && g_ncaBasePatchTitleInfo && g_ncaBasePatchTitleInfo->next)
                {
                    g_ncaBasePatchTitleInfo = g_ncaBasePatchTitleInfo->next;
                }
            }

            selected_element_options->selected++;
            if (!selected_element_options->options[selected_element_options->selected]) selected_element_options->selected--;
            if (selected_element_options->setter_func) selected_element_options->setter_func(selected_element_options->selected);
        } else
        if ((btn_down & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft)) && selected_element_options)
        {
            selected_element_options->selected--;
            if (selected_element_options->selected == UINT32_MAX) selected_element_options->selected = 0;
            if (selected_element_options->setter_func) selected_element_options->setter_func(selected_element_options->selected);

            /* Point to the previous base/patch title. */
            if (cur_menu->id == MenuId_NcaFsSectionsSubMenu && cur_menu->selected == 2)
            {
                if (selected_element_options->selected == 0 && g_ncaBasePatchTitleInfo)
                {
                    g_ncaBasePatchTitleInfoBkp = g_ncaBasePatchTitleInfo;
                    g_ncaBasePatchTitleInfo = NULL;
                } else
                if (selected_element_options->selected > 0 && g_ncaBasePatchTitleInfo && g_ncaBasePatchTitleInfo->previous)
                {
                    g_ncaBasePatchTitleInfo = g_ncaBasePatchTitleInfo->previous;
                }
            }
        } else
        if ((btn_down & HidNpadButton_B) && cur_menu->parent)
        {
            menuResetAttributes(cur_menu, element_count);

            if (cur_menu->id == MenuId_UserTitles || cur_menu->id == MenuId_SystemTitles)
            {
                app_metadata = NULL;
            } else
            if (cur_menu->id == MenuId_UserTitlesSubMenu)
            {
                titleFreeUserApplicationData(&user_app_data);
                g_titleTypesMenuElements[0]->child_menu = g_titleTypesMenuElements[1]->child_menu = \
                g_titleTypesMenuElements[2]->child_menu = g_titleTypesMenuElements[3]->child_menu = NULL;
            } else
            if (cur_menu->id == MenuId_NSPTitleTypes || cur_menu->id == MenuId_TicketTitleTypes || cur_menu->id == MenuId_NcaTitleTypes)
            {
                title_info = NULL;
                title_info_idx = title_info_count = 0;
            } else
            if (cur_menu->id == MenuId_NSP)
            {
                g_nspMenuElements[0]->userdata = NULL;
            } else
            if (cur_menu->id == MenuId_Ticket)
            {
                g_ticketMenuElements[0]->userdata = NULL;
            } else
            if (cur_menu->id == MenuId_Nca)
            {
                freeNcaList();

                if (is_system)
                {
                    titleFreeTitleInfo(&title_info);
                    is_system = false;
                }
            } else
            if (cur_menu->id == MenuId_NcaFsSections)
            {
                freeNcaFsSectionsList();
            } else
            if (cur_menu->id == MenuId_NcaFsSectionsSubMenu)
            {
                freeNcaBasePatchList();
            }

            cur_menu = cur_menu->parent;
            element_count = menuGetElementCount(cur_menu);
        } else
        if ((btn_down & HidNpadButton_X) && g_umsDeviceCount)
        {
            for(u32 i = 0; i < g_umsDeviceCount; i++) umsUnmountDevice(&(g_umsDevices[i]));
            updateStorageList();
        } else
        if (((btn_down & (HidNpadButton_L)) || (btn_held & HidNpadButton_ZL)) && (cur_menu->id == MenuId_NSP || cur_menu->id == MenuId_Ticket || cur_menu->id == MenuId_Nca) && title_info->previous)
        {
            title_info = title_info->previous;
            title_info_idx--;
            switchNcaListTitle(&cur_menu, &element_count, title_info);
        } else
        if (((btn_down & (HidNpadButton_R)) || (btn_held & HidNpadButton_ZR)) && (cur_menu->id == MenuId_NSP || cur_menu->id == MenuId_Ticket || cur_menu->id == MenuId_Nca) && title_info->next)
        {
            title_info = title_info->next;
            title_info_idx++;
            switchNcaListTitle(&cur_menu, &element_count, title_info);
        } else
        if ((btn_down & HidNpadButton_Y) && cur_menu->id == MenuId_Nca)
        {
            /* Change NCA menu element properties. */
            g_ncaMenuRawMode ^= 1;

            for(u32 i = 0; g_ncaMenuElements[i]; i++)
            {
                g_ncaMenuElements[i]->child_menu = (g_ncaMenuRawMode ? NULL : &g_ncaFsSectionsMenu);
                g_ncaMenuElements[i]->task_func = (g_ncaMenuRawMode ? &saveNintendoContentArchive : NULL);
            }
        } else
        if (btn_down & HidNpadButton_Plus)
        {
            break;
        }

        if (!g_appletStatus) break;

        utilsAppletLoopDelay();
    }

    freeNcaFsSectionsList();

    freeNcaList();

    freeTitleList(&g_systemTitlesMenu);
    freeTitleList(&g_userTitlesMenu);

    freeStorageList();

    titleFreeUserApplicationData(&user_app_data);

end:
    utilsCloseResources();

    consoleExit(NULL);

    return ret;
}

static void utilsScanPads(void)
{
    padUpdate(&g_padState);
}

static u64 utilsGetButtonsDown(void)
{
    return padGetButtonsDown(&g_padState);
}

static u64 utilsGetButtonsHeld(void)
{
    return padGetButtons(&g_padState);
}

static u64 utilsWaitForButtonPress(u64 flag)
{
    /* Don't consider stick movement as button inputs. */
    if (!flag) flag = ~(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | \
                        HidNpadButton_StickRUp | HidNpadButton_StickRDown);

    consoleRefresh();

    u64 btn_down = 0;

    while(appletMainLoop())
    {
        utilsScanPads();
        if ((btn_down = utilsGetButtonsDown()) & flag) break;
        utilsAppletLoopDelay();
    }

    return btn_down;
}

static void consolePrint(const char *text, ...)
{
    mutexLock(&g_conMutex);
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    mutexUnlock(&g_conMutex);
}

static void consolePrintReversedColors(const char *text, ...)
{
    mutexLock(&g_conMutex);

    printf(CONSOLE_ESC(7m));

    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);

    printf(CONSOLE_ESC(0m));

    mutexUnlock(&g_conMutex);
}

static void consoleRefresh(void)
{
    mutexLock(&g_conMutex);
    fflush(stdout);
    consoleUpdate(NULL);
    mutexUnlock(&g_conMutex);
}

static u32 menuGetElementCount(const Menu *menu)
{
    if (!menu || !menu->elements || !menu->elements[0]) return 0;

    u32 cnt;
    for(cnt = 0; menu->elements[cnt]; cnt++);
    return cnt;
}

static void menuResetAttributes(Menu *cur_menu, u32 element_count)
{
    if (!cur_menu) return;

    cur_menu->selected = 0;
    cur_menu->scroll = 0;

    for(u32 i = 0; i < element_count; i++)
    {
        MenuElement *cur_element = cur_menu->elements[i];
        MenuElementOption *cur_options = cur_element->element_options;
        if (cur_options && cur_options != &g_storageMenuElementOption) cur_options->retrieved = false;
    }
}

void freeStorageList(void)
{
    u32 elem_count = (2 + g_umsDeviceCount); // sd card, usb host, ums devices

    /* Free all previously allocated data. */
    if (g_storageOptions)
    {
        for(u32 i = 0; i < elem_count && g_storageOptions[i]; i++)
        {
            free(g_storageOptions[i]);
            g_storageOptions[i] = NULL;
        }

        free(g_storageOptions);
        g_storageOptions = NULL;
    }

    if (g_umsDevices)
    {
        free(g_umsDevices);
        g_umsDevices = NULL;
    }

    g_umsDeviceCount = 0;

    g_storageMenuElementOption.options = NULL;
}

void updateStorageList(void)
{
    u32 elem_count = 0, idx = 0;

    /* Free all previously allocated data. */
    freeStorageList();

    /* Get UMS devices. */
    g_umsDevices = umsGetDevices(&g_umsDeviceCount);
    elem_count = (2 + g_umsDeviceCount); // sd card, usb host, ums devices

    /* Allocate buffer. */
    g_storageOptions = calloc(elem_count + 1, sizeof(char*)); // NULL terminator

    /* Generate UMS device strings. */
    for(u32 i = 0; i < elem_count; i++)
    {
        u64 total = 0, free = 0;
        char total_str[36] = {0}, free_str[32] = {0};

        if (!g_storageOptions[idx])
        {
            g_storageOptions[idx] = calloc(sizeof(char), 0x300);
            if (!g_storageOptions[idx]) continue;
        }

        if (i == 1)
        {
            sprintf(g_storageOptions[idx], "usb host (pc)");
        } else {
            UsbHsFsDevice *ums_device = (i >= 2 ? &(g_umsDevices[i - 2]) : NULL);

            sprintf(total_str, "%s/", i == 0 ? DEVOPTAB_SDMC_DEVICE : ums_device->name);
            utilsGetFileSystemStatsByPath(total_str, &total, &free);
            utilsGenerateFormattedSizeString(total, total_str, sizeof(total_str));
            utilsGenerateFormattedSizeString(free, free_str, sizeof(free_str));

            if (i == 0)
            {
                sprintf(g_storageOptions[idx], DEVOPTAB_SDMC_DEVICE " (%s / %s)", free_str, total_str);
            } else {
                if (ums_device->product_name[0])
                {
                    sprintf(g_storageOptions[idx], "%s (%s, LUN %u, FS #%u, %s)", ums_device->name, ums_device->product_name, ums_device->lun, ums_device->fs_idx, LIBUSBHSFS_FS_TYPE_STR(ums_device->fs_type));
                } else {
                    sprintf(g_storageOptions[idx], "%s (LUN %u, FS #%u, %s)", ums_device->name, ums_device->lun, ums_device->fs_idx, LIBUSBHSFS_FS_TYPE_STR(ums_device->fs_type));
                }

                sprintf(g_storageOptions[idx] + strlen(g_storageOptions[idx]), " (%s / %s)", free_str, total_str);
            }
        }

        idx++;
    }

    /* Update storage menu element options. */
    if (g_storageMenuElementOption.selected >= elem_count)
    {
        g_storageMenuElementOption.selected = 0;
        setOutputStorageOption(0);
    }

    g_storageMenuElementOption.options = g_storageOptions;
}

void freeTitleList(Menu *menu)
{
    if (!menu) return;

    MenuElement **elements = menu->elements;

    /* Free all previously allocated data. */
    if (elements)
    {
        for(u32 i = 0; elements[i]; i++) free(elements[i]);
        free(elements);
    }

    menu->scroll = 0;
    menu->selected = 0;
    menu->elements = NULL;
}

void updateTitleList(Menu *menu, Menu *submenu, bool is_system)
{
    if (!menu || !submenu) return;

    u32 app_count = 0, idx = 0;
    TitleApplicationMetadata **app_metadata = NULL;
    MenuElement **elements = NULL;

    /* Free all previously allocated data. */
    freeTitleList(menu);

    /* Get application metadata entries. */
    app_metadata = titleGetApplicationMetadataEntries(is_system, &app_count);
    if (!app_metadata || !app_count) goto end;

    /* Allocate buffer. */
    elements = calloc(app_count + 1, sizeof(MenuElement*)); // NULL terminator

    /* Generate menu elements. */
    for(u32 i = 0; i < app_count; i++)
    {
        TitleApplicationMetadata *cur_app_metadata = app_metadata[i];

        if (!elements[idx])
        {
            elements[idx] = calloc(1, sizeof(MenuElement));
            if (!elements[idx]) continue;
        }

        elements[idx]->str = cur_app_metadata->lang_entry.name;
        elements[idx]->child_menu = submenu;
        elements[idx]->userdata = cur_app_metadata;

        idx++;
    }

    menu->elements = elements;

end:
    if (app_metadata) free(app_metadata);
}

static TitleInfo *getLatestTitleInfo(TitleInfo *title_info, u32 *out_idx, u32 *out_count)
{
    if (!title_info || !out_idx || !out_count || (title_info->meta_key.type != NcmContentMetaType_Patch && title_info->meta_key.type != NcmContentMetaType_DataPatch))
    {
        if (out_idx) *out_idx = 0;
        if (out_count) *out_count = titleGetCountFromInfoBlock(title_info);
        return title_info;
    }

    u32 idx = 0, count = 1;
    TitleInfo *cur_info = title_info->previous, *out = title_info;

    while(cur_info)
    {
        count++;

        if (cur_info->version.value > out->version.value)
        {
            out = cur_info;
            idx = count;
        }

        cur_info = cur_info->previous;
    }

    idx = (out != title_info ? (count - idx) : (count - 1));

    cur_info = title_info->next;

    while(cur_info)
    {
        count++;

        if (cur_info->version.value > out->version.value)
        {
            out = cur_info;
            idx = (count - 1);
        }

        cur_info = cur_info->next;
    }

    *out_idx = idx;
    *out_count = count;

    return out;
}

void freeNcaList(void)
{
    /* Free all previously allocated data. */
    if (g_ncaMenuElements)
    {
        u32 count = 0;

        for(count = 0; g_ncaMenuElements[count]; count++);

        for(u32 i = 0; count > 0 && i < (count - 1); i++) // Don't free output storage element
        {
            if (g_ncaMenuElements[i]->str) free(g_ncaMenuElements[i]->str);
            if (g_ncaMenuElements[i]->userdata) free(g_ncaMenuElements[i]->userdata);
            free(g_ncaMenuElements[i]);
        }

        free(g_ncaMenuElements);
        g_ncaMenuElements = NULL;
    }

    g_ncaMenu.scroll = 0;
    g_ncaMenu.selected = 0;
    g_ncaMenu.elements = NULL;
}

void updateNcaList(TitleInfo *title_info, u32 *element_count)
{
    u32 content_count = title_info->content_count, idx = 0;
    NcmContentInfo *content_infos = title_info->content_infos;
    char nca_id_str[0x21] = {0};

    /* Free all previously allocated data. */
    freeNcaList();

    /* Allocate buffer. */
    g_ncaMenuElements = calloc(content_count + 2, sizeof(MenuElement*)); // Output storage, NULL terminator
    if (!g_ncaMenuElements) return;

    /* Generate menu elements. */
    for(u32 i = 0; i < content_count; i++)
    {
        NcmContentInfo *cur_content_info = &(content_infos[i]);
        char *nca_info_str = NULL, nca_size_str[16] = {0};
        u64 nca_size = 0;
        NcaUserData *nca_user_data = NULL;

        if (!g_ncaMenuElements[idx])
        {
            g_ncaMenuElements[idx] = calloc(1, sizeof(MenuElement));
            if (!g_ncaMenuElements[idx]) continue;
        }

        nca_info_str = calloc(128, sizeof(char));
        nca_user_data = calloc(1, sizeof(NcaUserData));

        if (!nca_info_str || !nca_user_data)
        {
            if (nca_info_str) free(nca_info_str);
            if (nca_user_data) free(nca_user_data);
            continue;
        }

        utilsGenerateHexString(nca_id_str, sizeof(nca_id_str), cur_content_info->content_id.c, sizeof(cur_content_info->content_id.c), false);

        ncmContentInfoSizeToU64(cur_content_info, &nca_size);
        utilsGenerateFormattedSizeString((double)nca_size, nca_size_str, sizeof(nca_size_str));

        sprintf(nca_info_str, "%s #%u: %s (%s)", titleGetNcmContentTypeName(cur_content_info->content_type), cur_content_info->id_offset, nca_id_str, nca_size_str);

        nca_user_data->title_info = title_info;
        nca_user_data->content_idx = i;

        g_ncaMenuElements[idx]->str = nca_info_str;
        g_ncaMenuElements[idx]->child_menu = (g_ncaMenuRawMode ? NULL : &g_ncaFsSectionsMenu);
        g_ncaMenuElements[idx]->task_func = (g_ncaMenuRawMode ? &saveNintendoContentArchive : NULL);
        g_ncaMenuElements[idx]->userdata = nca_user_data;

        idx++;
    }

    if (idx > 0)
    {
        g_ncaMenuElements[idx] = &g_storageMenuElement;

        g_ncaMenu.elements = g_ncaMenuElements;

        if (element_count) *element_count = (idx + 1);
    }
}

static void switchNcaListTitle(Menu **cur_menu, u32 *element_count, TitleInfo *title_info)
{
    if (!cur_menu || !*cur_menu || (*cur_menu)->id != MenuId_Nca || !element_count || !title_info) return;

    updateNcaList(title_info, element_count);

    if (!g_ncaMenuElements || !g_ncaMenuElements[0])
    {
        freeNcaList();
        consolePrint("\nfailed to generate nca list for newly selected title\npress any button to go back\n");
        consoleRefresh();
        utilsWaitForButtonPress(0);

        (*cur_menu)->selected = 0;
        (*cur_menu)->scroll = 0;

        *cur_menu = (*cur_menu)->parent;
        *element_count = menuGetElementCount(*cur_menu);
    }
}

void freeNcaFsSectionsList(void)
{
    /* Free all previously allocated data. */
    if (g_ncaFsSectionsMenuCtx)
    {
        free(g_ncaFsSectionsMenuCtx);
        g_ncaFsSectionsMenuCtx = NULL;
    }

    if (g_ncaFsSectionsMenuElements)
    {
        for(u32 i = 0; g_ncaFsSectionsMenuElements[i] != NULL; i++)
        {
            if (g_ncaFsSectionsMenuElements[i]->str) free(g_ncaFsSectionsMenuElements[i]->str);
            free(g_ncaFsSectionsMenuElements[i]);
        }

        free(g_ncaFsSectionsMenuElements);
        g_ncaFsSectionsMenuElements = NULL;
    }

    g_ncaFsSectionsMenu.scroll = 0;
    g_ncaFsSectionsMenu.selected = 0;
    g_ncaFsSectionsMenu.elements = NULL;
}

void updateNcaFsSectionsList(NcaUserData *nca_user_data)
{
    TitleInfo *title_info = nca_user_data->title_info;
    NcmContentInfo *content_info = &(title_info->content_infos[nca_user_data->content_idx]);
    u32 idx = 0;

    /* Free all previously allocated data. */
    freeNcaFsSectionsList();

    /* Allocate buffer. */
    g_ncaFsSectionsMenuElements = calloc(NCA_FS_HEADER_COUNT + 1, sizeof(MenuElement*)); // NULL terminator

    /* Initialize NCA context. */
    g_ncaFsSectionsMenuCtx = calloc(1, sizeof(NcaContext));
    if (!ncaInitializeContext(g_ncaFsSectionsMenuCtx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0), \
                              &(title_info->meta_key), content_info, NULL)) return;

    /* Generate menu elements. */
    for(u32 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsSectionContext *cur_nca_fs_ctx = &(g_ncaFsSectionsMenuCtx->fs_ctx[i]);
        char *nca_fs_info_str = NULL;

        if (!g_ncaFsSectionsMenuElements[idx])
        {
            g_ncaFsSectionsMenuElements[idx] = calloc(1, sizeof(MenuElement));
            if (!g_ncaFsSectionsMenuElements[idx]) continue;
        }

        nca_fs_info_str = calloc(128, sizeof(char));
        if (!nca_fs_info_str) continue;

        if (cur_nca_fs_ctx->enabled)
        {
            sprintf(nca_fs_info_str, "FS section #%u: %s (%s)", i + 1, ncaGetFsSectionTypeName(cur_nca_fs_ctx), cur_nca_fs_ctx->section_size_str);
        } else {
            sprintf(nca_fs_info_str, "FS section #%u: %s", i + 1, ncaGetFsSectionTypeName(cur_nca_fs_ctx));
        }

        g_ncaFsSectionsMenuElements[idx]->str = nca_fs_info_str;
        g_ncaFsSectionsMenuElements[idx]->child_menu = &g_ncaFsSectionsSubMenu;
        g_ncaFsSectionsMenuElements[idx]->userdata = cur_nca_fs_ctx;

        idx++;
    }

    g_ncaFsSectionsMenu.elements = g_ncaFsSectionsMenuElements;
}

void freeNcaBasePatchList(void)
{
    /* Free all previously allocated data. */
    if (g_ncaBasePatchOptions)
    {
        /* Skip the first option. */
        for(u32 i = 1; g_ncaBasePatchOptions[i]; i++)
        {
            free(g_ncaBasePatchOptions[i]);
            g_ncaBasePatchOptions[i] = NULL;
        }

        free(g_ncaBasePatchOptions);
        g_ncaBasePatchOptions = NULL;
    }

    g_ncaFsSectionsSubMenuBasePatchElementOption.selected = 0;
    g_ncaFsSectionsSubMenuBasePatchElementOption.options = NULL;

    g_ncaFsSectionsSubMenuElements[0]->userdata = g_ncaFsSectionsSubMenuElements[1]->userdata = NULL;

    if (g_ncaBasePatchTitleInfo && (g_ncaBasePatchTitleInfo->meta_key.type == NcmContentMetaType_AddOnContent || g_ncaBasePatchTitleInfo->meta_key.type == NcmContentMetaType_DataPatch))
    {
        titleFreeTitleInfo(&g_ncaBasePatchTitleInfo);
    }

    g_ncaUserTitleInfo = g_ncaBasePatchTitleInfo = g_ncaBasePatchTitleInfoBkp = NULL;
}

void updateNcaBasePatchList(TitleUserApplicationData *user_app_data, TitleInfo *title_info, NcaFsSectionContext *nca_fs_ctx)
{
    u32 elem_count = 1, idx = 1; // "no" option
    TitleInfo *cur_title_info = NULL;

    u8 title_type = title_info->meta_key.type;
    u8 content_type = nca_fs_ctx->nca_ctx->content_type;
    u8 section_type = nca_fs_ctx->section_type;
    bool unsupported = false;

    u32 selected_version = 0;

    /* Free all previously allocated data. */
    freeNcaBasePatchList();

    /* Only enable base/patch list if we're dealing with supported content types and/or FS section types. */
    if ((content_type == NcmContentType_Program || content_type == NcmContentType_Data || content_type == NcmContentType_HtmlDocument) && section_type < NcaFsSectionType_Nca0RomFs)
    {
        /* Retrieve corresponding TitleInfo linked list for the current title type. */
        switch(title_type)
        {
            case NcmContentMetaType_Application:
                g_ncaBasePatchTitleInfo = user_app_data->patch_info;
                break;
            case NcmContentMetaType_Patch:
                g_ncaBasePatchTitleInfo = user_app_data->app_info;
                break;
            case NcmContentMetaType_AddOnContent:
            case NcmContentMetaType_DataPatch:
                g_ncaBasePatchTitleInfo = titleGetAddOnContentBaseOrPatchList(title_info);
                break;
            default:
                unsupported = true;
                break;
        }
    } else {
        unsupported = true;
    }

    /* Calculate element count. */
    elem_count += titleGetCountFromInfoBlock(g_ncaBasePatchTitleInfo);

    /* Allocate buffer. */
    g_ncaBasePatchOptions = calloc(elem_count + 1, sizeof(char*)); // NULL terminator

    /* Set first option. */
    g_ncaBasePatchOptions[0] = (unsupported ? "unsupported by this content/section type combo" : (elem_count < 2 ? "none available" : "no"));

    /* Generate base/patch strings. */
    cur_title_info = g_ncaBasePatchTitleInfo;
    while(cur_title_info)
    {
        if (!g_ncaBasePatchOptions[idx])
        {
            g_ncaBasePatchOptions[idx] = calloc(sizeof(char), 0x40);
            if (!g_ncaBasePatchOptions[idx])
            {
                cur_title_info = cur_title_info->next;
                continue;
            }
        }

        snprintf(g_ncaBasePatchOptions[idx], 0x40, "%s v%u (v%u.%u) (%s)", titleGetNcmContentMetaTypeName(cur_title_info->meta_key.type), \
                    cur_title_info->version.value, cur_title_info->version.application_version.release_ver, cur_title_info->version.application_version.private_ver, \
                    titleGetNcmStorageIdName(cur_title_info->storage_id));

        /* Make sure the highest available base/patch title is automatically selected. */
        if (cur_title_info->version.value >= selected_version && \
            (((title_type == NcmContentMetaType_Application || title_type == NcmContentMetaType_AddOnContent) && (!nca_fs_ctx->has_sparse_layer || cur_title_info->version.value >= title_info->version.value)) || \
            ((title_type == NcmContentMetaType_Patch || title_type == NcmContentMetaType_DataPatch) && cur_title_info->version.value <= title_info->version.value)))
        {
            g_ncaFsSectionsSubMenuBasePatchElementOption.selected = idx;
            selected_version = cur_title_info->version.value;
            g_ncaBasePatchTitleInfo = cur_title_info;
        }

        cur_title_info = cur_title_info->next;

        idx++;
    }

    g_ncaFsSectionsSubMenuBasePatchElementOption.options = g_ncaBasePatchOptions;

    g_ncaFsSectionsSubMenuElements[0]->userdata = g_ncaFsSectionsSubMenuElements[1]->userdata = nca_fs_ctx;

    g_ncaUserTitleInfo = title_info;

    g_ncaBasePatchTitleInfoBkp = (g_ncaFsSectionsSubMenuBasePatchElementOption.selected > 0 ? g_ncaBasePatchTitleInfo : NULL);
}

NX_INLINE bool useUsbHost(void)
{
    return (g_storageMenuElementOption.selected == 1);
}

static bool waitForGameCard(void)
{
    consolePrint("waiting for gamecard... ");
    consoleRefresh();

    time_t start = time(NULL);
    u8 status = GameCardStatus_NotInserted;

    while((g_appletStatus = appletMainLoop()))
    {
        if ((status = gamecardGetStatus()) > GameCardStatus_Processing) break;

        time_t now = time(NULL);
        time_t diff = (now - start);

        if (diff >= WAIT_TIME_LIMIT) break;

        consolePrint("%lu ", diff);
        consoleRefresh();

        utilsSleep(1);
    }

    consolePrint("\n");
    consoleRefresh();

    if (!g_appletStatus || status == GameCardStatus_NotInserted) return false;

    switch(status)
    {
        case GameCardStatus_NoGameCardPatchEnabled:
            consolePrint("\"nogc\" patch enabled, please disable it and reboot your console\n");
            break;
        case GameCardStatus_LotusAsicFirmwareUpdateRequired:
            consolePrint("gamecard controller firmware update required, please update your console\n");
            break;
        case GameCardStatus_InsertedAndInfoNotLoaded:
            consolePrint("unexpected I/O error occurred, please check the logfile\n");
            break;
        default:
            break;
    }

    if (status != GameCardStatus_InsertedAndInfoLoaded)
    {
        consolePrint("press any button to go back\n");
        utilsWaitForButtonPress(0);
        return false;
    }

    return true;
}

static bool waitForUsb(void)
{
    consolePrint("waiting for usb session... ");
    consoleRefresh();

    time_t start = time(NULL);
    u8 usb_host_speed = UsbHostSpeed_None;

    while((g_appletStatus = appletMainLoop()))
    {
        if ((usb_host_speed = usbIsReady()) != UsbHostSpeed_None) break;

        time_t now = time(NULL);
        time_t diff = (now - start);

        if (diff >= WAIT_TIME_LIMIT) break;

        consolePrint("%lu ", diff);
        consoleRefresh();

        utilsSleep(1);
    }

    consolePrint("\n");
    if (usb_host_speed != UsbHostSpeed_None) consolePrint("usb speed: %u.0\n", usb_host_speed);
    consoleRefresh();

    return (g_appletStatus && usb_host_speed != UsbHostSpeed_None);
}

static bool saveFileData(const char *filepath, void *data, size_t data_size)
{
    if (!filepath || !*filepath || !data || !data_size)
    {
        consolePrint("invalid parameters to save file data!\n");
        return false;
    }

    if (useUsbHost())
    {
        if (!usbSendFileProperties(data_size, filepath))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", filepath);
            return false;
        }

        if (!usbSendFileData(data, data_size))
        {
            consolePrint("failed to send file data for \"%s\"!\n", filepath);
            return false;
        }
    } else {
        utilsCreateDirectoryTree(filepath, false);

        FILE *fp = fopen(filepath, "wb");
        if (!fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", filepath);
            return false;
        }

        ftruncate(fileno(fp), (off_t)data_size);
        size_t ret = fwrite(data, 1, data_size, fp);
        fclose(fp);

        if (g_storageMenuElementOption.selected == 0) utilsCommitSdCardFileSystemChanges();

        if (ret != data_size)
        {
            consolePrint("failed to write 0x%lX byte(s) to \"%s\"! (%d)\n", data_size, filepath, errno);
            remove(filepath);
        }
    }

    return true;
}

static char *generateOutputGameCardFileName(const char *subdir, const char *extension, bool use_nacp_name)
{
    char *filename = NULL, *prefix = NULL, *output = NULL;
    u32 dev_idx = g_storageMenuElementOption.selected;

    if ((subdir && !*subdir) || !extension || !*extension || (use_nacp_name && !(filename = titleGenerateGameCardFileName(TitleNamingConvention_Full, dev_idx > 0 ? TitleFileNameIllegalCharReplaceType_IllegalFsChars : TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly))))
    {
        consolePrint("failed to generate gamecard filename!\n");
        goto end;
    }

    prefix = calloc(sizeof(char), FS_MAX_PATH);
    if (!prefix)
    {
        consolePrint("failed to generate prefix!\n");
        goto end;
    }

    if (dev_idx != 1) sprintf(prefix, "%s/" OUTDIR, dev_idx == 0 ? DEVOPTAB_SDMC_DEVICE : g_umsDevices[dev_idx - 2].name);

    if (subdir)
    {
        if (subdir[0] != '/') strcat(prefix, "/");
        strcat(prefix, subdir);
    }

    output = (use_nacp_name ? utilsGeneratePath(prefix, filename, extension) : utilsGeneratePath(prefix, extension, NULL));
    if (!output) consolePrint("failed to generate output filename!\n");

end:
    if (prefix) free(prefix);
    if (filename) free(filename);

    return output;
}

static char *generateOutputTitleFileName(TitleInfo *title_info, const char *subdir, const char *extension)
{
    char *filename = NULL, *prefix = NULL, *output = NULL;
    u32 dev_idx = g_storageMenuElementOption.selected;

    if (!title_info || (subdir && !*subdir) || !extension || !*extension || !(filename = titleGenerateFileName(title_info, TitleNamingConvention_Full, dev_idx > 0 ? TitleFileNameIllegalCharReplaceType_IllegalFsChars : TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly)))
    {
        consolePrint("failed to generate title filename!\n");
        goto end;
    }

    prefix = calloc(sizeof(char), FS_MAX_PATH);
    if (!prefix)
    {
        consolePrint("failed to generate prefix!\n");
        goto end;
    }

    if (dev_idx != 1) sprintf(prefix, "%s/" OUTDIR, dev_idx == 0 ? DEVOPTAB_SDMC_DEVICE : g_umsDevices[dev_idx - 2].name);

    if (subdir)
    {
        if (subdir[0] != '/') strcat(prefix, "/");
        strcat(prefix, subdir);
    }

    output = utilsGeneratePath(prefix, filename, extension);
    if (!output) consolePrint("failed to generate output filename!\n");

end:
    if (prefix) free(prefix);
    if (filename) free(filename);

    return output;
}

static char *generateOutputLayeredFsFileName(u64 title_id, const char *subdir, const char *extension)
{
    char *prefix = NULL, *output = NULL;
    u32 dev_idx = g_storageMenuElementOption.selected;

    if ((subdir && !*subdir) || !extension || !*extension)
    {
        consolePrint("failed to generate title filename!\n");
        goto end;
    }

    prefix = calloc(sizeof(char), FS_MAX_PATH);
    if (!prefix)
    {
        consolePrint("failed to generate prefix!\n");
        goto end;
    }

    if (dev_idx != 1) sprintf(prefix, "%s", dev_idx == 0 ? DEVOPTAB_SDMC_DEVICE : g_umsDevices[dev_idx - 2].name);

    sprintf(prefix + strlen(prefix), "/atmosphere/contents/%016lX", title_id);

    if (subdir)
    {
        if (subdir[0] != '/') strcat(prefix, "/");
        strcat(prefix, subdir);
    }

    output = utilsGeneratePath(prefix, extension, NULL);
    if (!output) consolePrint("failed to generate output filename!\n");

end:
    if (prefix) free(prefix);

    return output;
}

static bool dumpGameCardSecurityInformation(GameCardSecurityInformation *out)
{
    if (!out)
    {
        consolePrint("invalid parameters to dump gamecard security information!\n");
        return false;
    }

    if (!gamecardGetSecurityInformation(out))
    {
        consolePrint("failed to get gamecard security information\n");
        return false;
    }

    consolePrint("get gamecard security information ok\n");
    return true;
}

static bool resetSettings(void *userdata)
{
    consolePrint("are you sure you want to reset all settings to their default values?\n");
    consolePrint("press a to proceed, or b to cancel\n\n");

    u64 btn_down = utilsWaitForButtonPress(HidNpadButton_A | HidNpadButton_B);
    if (btn_down & HidNpadButton_A)
    {
        configResetSettings();
        consolePrint("settings successfully reset\n");
    }

    return false;
}

static bool saveGameCardImage(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    u64 gc_size = 0, free_space = 0;

    u32 key_area_crc = 0;
    GameCardKeyArea gc_key_area = {0};
    GameCardSecurityInformation gc_security_information = {0};

    XciThreadData xci_thread_data = {0};
    SharedThreadData *shared_thread_data = &(xci_thread_data.shared_thread_data);

    char *filename = NULL;
    u32 dev_idx = g_storageMenuElementOption.selected;

    bool prepend_key_area = (bool)getGameCardPrependKeyAreaOption();
    bool keep_certificate = (bool)getGameCardKeepCertificateOption();
    bool trim_dump = (bool)getGameCardTrimDumpOption();
    bool calculate_checksum = (bool)getGameCardCalculateChecksumOption();

    bool success = false;

    consolePrint("gamecard image dump\nprepend key area: %s | keep certificate: %s | trim dump: %s | calculate checksum: %s\n\n", prepend_key_area ? "yes" : "no", keep_certificate ? "yes" : "no", trim_dump ? "yes" : "no", calculate_checksum ? "yes" : "no");

    if ((!trim_dump && !gamecardGetTotalSize(&gc_size)) || (trim_dump && !gamecardGetTrimmedSize(&gc_size)) || !gc_size)
    {
        consolePrint("failed to get gamecard size!\n");
        goto end;
    }

    shared_thread_data->total_size = gc_size;

    consolePrint("gamecard size: 0x%lX\n", gc_size);

    if (prepend_key_area)
    {
        gc_size += sizeof(GameCardKeyArea);

        if (!dumpGameCardSecurityInformation(&gc_security_information)) goto end;

        memcpy(&(gc_key_area.initial_data), &(gc_security_information.initial_data), sizeof(GameCardInitialData));

        if (calculate_checksum)
        {
            key_area_crc = crc32Calculate(&gc_key_area, sizeof(GameCardKeyArea));
            xci_thread_data.full_xci_crc = key_area_crc;
        }

        consolePrint("gamecard size (with key area): 0x%lX\n", gc_size);
    }

    snprintf(path, MAX_ELEMENTS(path), " [%s][%s][%s].xci", prepend_key_area ? "KA" : "NKA", keep_certificate ? "C" : "NC", trim_dump ? "T" : "NT");
    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (dev_idx == 1)
    {
        if (!usbSendFileProperties(gc_size, filename))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", filename);
            goto end;
        }

        if (prepend_key_area && !usbSendFileData(&gc_key_area, sizeof(GameCardKeyArea)))
        {
            consolePrint("failed to send gamecard key area data!\n");
            goto end;
        }
    } else {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            goto end;
        }

        if (gc_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            goto end;
        }

        utilsCreateDirectoryTree(filename, false);

        if (dev_idx == 0)
        {
            if (gc_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(filename))
            {
                consolePrint("failed to create concatenation file for \"%s\"!\n", filename);
                goto end;
            }
        } else {
            if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && gc_size > FAT32_FILESIZE_LIMIT)
            {
                consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                goto end;
            }
        }

        shared_thread_data->fp = fopen(filename, "wb");
        if (!shared_thread_data->fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", filename);
            goto end;
        }

        setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
        ftruncate(fileno(shared_thread_data->fp), (off_t)shared_thread_data->total_size);

        if (prepend_key_area && fwrite(&gc_key_area, 1, sizeof(GameCardKeyArea), shared_thread_data->fp) != sizeof(GameCardKeyArea))
        {
            consolePrint("failed to write gamecard key area data!\n");
            goto end;
        }
    }

    consoleRefresh();

    success = spanDumpThreads(xciReadThreadFunc, genericWriteThreadFunc, &xci_thread_data);

    if (success)
    {
        consolePrint("successfully saved xci as \"%s\"\n", filename);

        if (calculate_checksum)
        {
            if (prepend_key_area) consolePrint("key area crc: %08X | ", key_area_crc);
            consolePrint("xci crc: %08X", xci_thread_data.xci_crc);
            if (prepend_key_area) consolePrint(" | xci crc (with key area): %08X", xci_thread_data.full_xci_crc);
            consolePrint("\n");
        }

        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if (!success && dev_idx != 1)
        {
            if (dev_idx == 0)
            {
                utilsRemoveConcatenationFile(filename);
                utilsCommitSdCardFileSystemChanges();
            } else {
                remove(filename);
            }
        }
    }

    if (filename) free(filename);

    return success;
}

static bool saveGameCardHeader(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    GameCardHeader gc_header = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!gamecardGetHeader(&gc_header))
    {
        consolePrint("failed to get gamecard header\n");
        goto end;
    }

    consolePrint("get gamecard header ok\n");

    crc = crc32Calculate(&gc_header, sizeof(GameCardHeader));
    snprintf(path, MAX_ELEMENTS(path), " (Header) (%08X).bin", crc);

    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (!saveFileData(filename, &gc_header, sizeof(GameCardHeader))) goto end;

    consolePrint("successfully saved header as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}

static bool saveGameCardCardInfo(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    GameCardInfo gc_cardinfo = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!gamecardGetPlaintextCardInfoArea(&gc_cardinfo))
    {
        consolePrint("failed to get gamecard cardinfo\n");
        goto end;
    }

    consolePrint("get gamecard cardinfo ok\n");

    crc = crc32Calculate(&gc_cardinfo, sizeof(GameCardInfo));
    snprintf(path, MAX_ELEMENTS(path), " (CardInfo) (%08X).bin", crc);

    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (!saveFileData(filename, &gc_cardinfo, sizeof(GameCardInfo))) goto end;

    consolePrint("successfully saved cardinfo dump as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}

static bool saveGameCardCertificate(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    FsGameCardCertificate gc_cert = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!gamecardGetCertificate(&gc_cert))
    {
        consolePrint("failed to get gamecard certificate\n");
        goto end;
    }

    consolePrint("get gamecard certificate ok\n");

    crc = crc32Calculate(&gc_cert, sizeof(FsGameCardCertificate));
    snprintf(path, MAX_ELEMENTS(path), " (Certificate) (%08X).bin", crc);

    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (!saveFileData(filename, &gc_cert, sizeof(FsGameCardCertificate))) goto end;

    consolePrint("successfully saved certificate as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}

static bool saveGameCardInitialData(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    GameCardSecurityInformation gc_security_information = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!dumpGameCardSecurityInformation(&gc_security_information)) goto end;

    crc = crc32Calculate(&(gc_security_information.initial_data), sizeof(GameCardInitialData));
    snprintf(path, MAX_ELEMENTS(path), " (Initial Data) (%08X).bin", crc);

    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (!saveFileData(filename, &(gc_security_information.initial_data), sizeof(GameCardInitialData))) goto end;

    consolePrint("successfully saved initial data as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}

/* This will save the Gamecard Specific Data. Its format is specific and internal to the current LAFW firmware version and session of the GCBRG ASIC. */
/* Depending on which Switch system version the gamecard was dumped from, this data can change. */
/* Even re-inserting the gamecard will change parts of this data. */
/* For this reason the gamecard specific data is mostly uninteresting for gamecard preservation. */
/* Instead, take a look at saveGameCardIdSet and saveGameCardUid which is a more standardised format of the Gamecard ID data. */
static bool saveGameCardSpecificData(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    GameCardSecurityInformation gc_security_information = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!dumpGameCardSecurityInformation(&gc_security_information)) goto end;

    crc = crc32Calculate(&(gc_security_information.specific_data), sizeof(GameCardSpecificData));
    snprintf(path, MAX_ELEMENTS(path), " (Specific Data) (%08X).bin", crc);

    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (!saveFileData(filename, &(gc_security_information.specific_data), sizeof(GameCardSpecificData))) goto end;

    consolePrint("successfully saved specific data as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}

static bool saveGameCardIdSet(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    FsGameCardIdSet id_set = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!gamecardGetCardIdSet(&id_set))
    {
        consolePrint("failed to get gamecard id set\n");
        goto end;
    }

    crc = crc32Calculate(&id_set, sizeof(FsGameCardIdSet));
    snprintf(path, MAX_ELEMENTS(path), " (Card ID Set) (%08X).bin", crc);

    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (!saveFileData(filename, &id_set, sizeof(FsGameCardIdSet))) goto end;

    consolePrint("successfully saved gamecard id set as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}


static bool saveGameCardUid(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    GameCardSecurityInformation gc_security_information = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!gamecardGetSecurityInformation(&gc_security_information))
    {
        consolePrint("failed to get gamecard security information\n");
        goto end;
    }

    crc = crc32Calculate(gc_security_information.specific_data.card_uid, sizeof(gc_security_information.specific_data.card_uid));
    snprintf(path, MAX_ELEMENTS(path), " (Card UID) (%08X).bin", crc);

    filename = generateOutputGameCardFileName("Gamecard", path, true);
    if (!filename) goto end;

    if (!saveFileData(filename, gc_security_information.specific_data.card_uid, sizeof(gc_security_information.specific_data.card_uid))) goto end;

    consolePrint("successfully saved gamecard uid as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}

static bool saveGameCardHfsPartition(void *userdata)
{
    u32 hfs_partition_type = (userdata ? *((u32*)userdata) : HashFileSystemPartitionType_None);
    bool write_raw_hfs_partition = (bool)getGameCardWriteRawHfsPartitionOption();
    HashFileSystemContext hfs_ctx = {0};

    bool success = false;

    if (hfs_partition_type < HashFileSystemPartitionType_Root || hfs_partition_type > HashFileSystemPartitionType_Secure)
    {
        consolePrint("invalid hfs partition type! (%u)\n", hfs_partition_type);
        goto end;
    }

    if (!gamecardGetHashFileSystemContext(hfs_partition_type, &hfs_ctx))
    {
        consolePrint("get hfs ctx failed! this partition type may not exist within the inserted gamecard\n");
        goto end;
    }

    success = (write_raw_hfs_partition ? saveGameCardRawHfsPartition(&hfs_ctx) : saveGameCardExtractedHfsPartition(&hfs_ctx));

end:
    hfsFreeContext(&hfs_ctx);

    return success;
}

static bool saveGameCardRawHfsPartition(HashFileSystemContext *hfs_ctx)
{
    u64 free_space = 0;

    HfsThreadData hfs_thread_data = {0};
    SharedThreadData *shared_thread_data = &(hfs_thread_data.shared_thread_data);

    char *filename = NULL;
    u32 dev_idx = g_storageMenuElementOption.selected;

    bool success = false;

    hfs_thread_data.hfs_ctx = hfs_ctx;
    shared_thread_data->total_size = hfs_ctx->size;

    consolePrint("raw %s hfs partition size: 0x%lX\n", hfs_ctx->name, hfs_ctx->size);

    snprintf(path, MAX_ELEMENTS(path), "/%s.hfs0", hfs_ctx->name);
    filename = generateOutputGameCardFileName("HFS/Raw", path, true);
    if (!filename) goto end;

    if (dev_idx == 1)
    {
        if (!usbSendFileProperties(shared_thread_data->total_size, filename))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", filename);
            goto end;
        }
    } else {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            goto end;
        }

        if (shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            goto end;
        }

        utilsCreateDirectoryTree(filename, false);

        if (dev_idx == 0)
        {
            if (shared_thread_data->total_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(filename))
            {
                consolePrint("failed to create concatenation file for \"%s\"!\n", filename);
                goto end;
            }
        } else {
            if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && shared_thread_data->total_size > FAT32_FILESIZE_LIMIT)
            {
                consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                goto end;
            }
        }

        shared_thread_data->fp = fopen(filename, "wb");
        if (!shared_thread_data->fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", filename);
            goto end;
        }

        setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
        ftruncate(fileno(shared_thread_data->fp), (off_t)shared_thread_data->total_size);
    }

    consoleRefresh();

    success = spanDumpThreads(rawHfsReadThreadFunc, genericWriteThreadFunc, &hfs_thread_data);

    if (success)
    {
        consolePrint("successfully saved raw hfs partition as \"%s\"\n", filename);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if (!success && dev_idx != 1)
        {
            if (dev_idx == 0)
            {
                utilsRemoveConcatenationFile(filename);
                utilsCommitSdCardFileSystemChanges();
            } else {
                remove(filename);
            }
        }
    }

    if (filename) free(filename);

    return success;
}

static bool saveGameCardExtractedHfsPartition(HashFileSystemContext *hfs_ctx)
{
    u64 data_size = 0;

    HfsThreadData hfs_thread_data = {0};
    SharedThreadData *shared_thread_data = &(hfs_thread_data.shared_thread_data);

    bool success = false;

    if (!hfsGetTotalDataSize(hfs_ctx, &data_size))
    {
        consolePrint("failed to calculate extracted %s hfs partition size!\n", hfs_ctx->name);
        goto end;
    }

    if (!data_size)
    {
        consolePrint("%s hfs partition is empty!\n", hfs_ctx->name);
        goto end;
    }

    hfs_thread_data.hfs_ctx = hfs_ctx;
    shared_thread_data->total_size = data_size;

    consolePrint("extracted %s hfs partition size: 0x%lX\n", hfs_ctx->name, data_size);
    consoleRefresh();

    success = spanDumpThreads(extractedHfsReadThreadFunc, genericWriteThreadFunc, &hfs_thread_data);

end:
    return success;
}

static bool browseGameCardHfsPartition(void *userdata)
{
    u32 hfs_partition_type = (userdata ? *((u32*)userdata) : HashFileSystemPartitionType_None);
    HashFileSystemContext hfs_ctx = {0};
    char mount_name[DEVOPTAB_MOUNT_NAME_LENGTH] = {0}, subdir[0x20] = {0}, *base_out_path = NULL;

    bool success = false;

    if (hfs_partition_type < HashFileSystemPartitionType_Root || hfs_partition_type > HashFileSystemPartitionType_Secure)
    {
        consolePrint("invalid hfs partition type! (%u)\n", hfs_partition_type);
        goto end;
    }

    if (!gamecardGetHashFileSystemContext(hfs_partition_type, &hfs_ctx))
    {
        consolePrint("get hfs ctx failed! this partition type may not exist within the inserted gamecard\n");
        goto end;
    }

    /* Mount devoptab device. */
    snprintf(mount_name, MAX_ELEMENTS(mount_name), "hfs%s", hfs_ctx.name);

    if (!devoptabMountHashFileSystemDevice(&hfs_ctx, mount_name))
    {
        consolePrint("hfs ctx devoptab mount failed!\n");
        goto end;
    }

    /* Generate output base path. */
    snprintf(subdir, MAX_ELEMENTS(subdir), "/%s", hfs_ctx.name);
    base_out_path = generateOutputGameCardFileName("HFS/Extracted", subdir, true);
    if (!base_out_path) goto end;

    /* Display file browser. */
    success = fsBrowser(mount_name, base_out_path);

    /* Unmount devoptab device. */
    devoptabUnmountDevice(mount_name);

end:
    /* Free data. */
    if (base_out_path) free(base_out_path);
    hfsFreeContext(&hfs_ctx);

    if (!success && g_appletStatus)
    {
        consolePrint("press any button to continue\n");
        utilsWaitForButtonPress(0);
    }

    return success;
}

static bool saveConsoleLafwBlob(void *userdata)
{
    NX_IGNORE_ARG(userdata);

    u64 lafw_version = 0;
    LotusAsicFirmwareBlob lafw_blob = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;
    const char *fw_type_str = NULL, *dev_type_str = NULL;

    if (!gamecardGetLotusAsicFirmwareBlob(&lafw_blob, &lafw_version))
    {
        consolePrint("failed to get console lafw blob\n");
        goto end;
    }

    fw_type_str = gamecardGetLafwTypeString(lafw_blob.fw_type);
    if (!fw_type_str) fw_type_str = "Unknown";

    dev_type_str = gamecardGetLafwDeviceTypeString(lafw_blob.device_type);
    if (!dev_type_str) dev_type_str = "Unknown";

    consolePrint("get console lafw blob ok\n");

    crc = crc32Calculate(&lafw_blob, sizeof(LotusAsicFirmwareBlob));
    snprintf(path, MAX_ELEMENTS(path), "LAFW (%s) (%s) (v%lu) (%08X).bin", fw_type_str, dev_type_str, lafw_version, crc);

    filename = generateOutputGameCardFileName(NULL, path, false);
    if (!filename) goto end;

    if (!saveFileData(filename, &lafw_blob, sizeof(LotusAsicFirmwareBlob))) goto end;

    consolePrint("successfully saved lafw blob as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    return success;
}

static bool saveNintendoSubmissionPackage(void *userdata)
{
    if (!userdata) return false;

    TitleInfo *title_info = (TitleInfo*)userdata;
    TitleApplicationMetadata *app_metadata = title_info->app_metadata;

    NspThreadData nsp_thread_data = {0};
    Thread dump_thread = {0};

    time_t start = 0, btn_cancel_start_tmr = 0, btn_cancel_end_tmr = 0;
    bool btn_cancel_cur_state = false, btn_cancel_prev_state = false, success = false;

    u64 prev_size = 0;
    u8 prev_time = 0, percent = 0;

    consolePrint("%s info:\n\n", title_info->meta_key.type == NcmContentMetaType_Application ? "base application" : \
                                (title_info->meta_key.type == NcmContentMetaType_Patch ? "update" : \
                                (title_info->meta_key.type == NcmContentMetaType_AddOnContent ? "dlc" : "dlc update")));

    if (app_metadata)
    {
        consolePrint("name: %s\n", app_metadata->lang_entry.name);
        consolePrint("publisher: %s\n", app_metadata->lang_entry.author);
    }

    consolePrint("source storage: %s\n", titleGetNcmStorageIdName(title_info->storage_id));
    consolePrint("title id: %016lX\n", title_info->meta_key.id);
    consolePrint("version: %u (%u.%u.%u-%u.%u)\n", title_info->version.value, title_info->version.system_version.major, title_info->version.system_version.minor, \
                                                   title_info->version.system_version.micro, title_info->version.system_version.major_relstep, \
                                                   title_info->version.system_version.minor_relstep);
    consolePrint("content count: %u\n", title_info->content_count);
    consolePrint("size: %s\n", title_info->size_str);
    consolePrint("______________________________\n\n");

    consoleRefresh();

    /* Create dump thread. */
    nsp_thread_data.data = title_info;
    utilsCreateThread(&dump_thread, nspThreadFunc, &nsp_thread_data, 2);

    /* Wait until the background thread calculates the NSP size. */
    while(!nsp_thread_data.total_size && !nsp_thread_data.error) utilsAppletLoopDelay();

    if (nsp_thread_data.error)
    {
        utilsJoinThread(&dump_thread);
        return false;
    }

    /* Start dump. */
    start = time(NULL);

    while(nsp_thread_data.data_written < nsp_thread_data.total_size)
    {
        g_appletStatus = appletMainLoop();
        if (!g_appletStatus)
        {
            mutexLock(&g_fileMutex);
            nsp_thread_data.transfer_cancelled = true;
            mutexUnlock(&g_fileMutex);
        }

        if (nsp_thread_data.error || nsp_thread_data.transfer_cancelled) break;

        struct tm ts = {0};
        time_t now = time(NULL);
        localtime_r(&now, &ts);

        size_t size = nsp_thread_data.data_written;

        utilsScanPads();
        btn_cancel_cur_state = (utilsGetButtonsHeld() & HidNpadButton_B);

        if (btn_cancel_cur_state && btn_cancel_cur_state != btn_cancel_prev_state)
        {
            btn_cancel_start_tmr = now;
        } else
        if (btn_cancel_cur_state && btn_cancel_cur_state == btn_cancel_prev_state)
        {
            btn_cancel_end_tmr = now;
            if ((btn_cancel_end_tmr - btn_cancel_start_tmr) >= 3)
            {
                mutexLock(&g_fileMutex);
                nsp_thread_data.transfer_cancelled = true;
                mutexUnlock(&g_fileMutex);
                break;
            }
        } else {
            btn_cancel_start_tmr = btn_cancel_end_tmr = 0;
        }

        btn_cancel_prev_state = btn_cancel_cur_state;

        if (prev_time == ts.tm_sec || prev_size == size) continue;

        percent = (u8)((size * 100) / nsp_thread_data.total_size);

        prev_time = ts.tm_sec;
        prev_size = size;

        consolePrint("%lu / %lu (%u%%) | Time elapsed: %lu\n", size, nsp_thread_data.total_size, percent, (now - start));
        consoleRefresh();

        utilsAppletLoopDelay();
    }

    consolePrint("\nwaiting for thread to join\n");
    consoleRefresh();

    utilsJoinThread(&dump_thread);
    consolePrint("dump_thread done: %lu\n", time(NULL));

    if (nsp_thread_data.error)
    {
        consolePrint("i/o error\n");
    } else
    if (nsp_thread_data.transfer_cancelled)
    {
        consolePrint("process cancelled\n");
    } else {
        start = (time(NULL) - start);
        consolePrint("process completed in %lu seconds\n", start);
        success = true;
    }

    consoleRefresh();

    return success;
}

static bool saveTicket(void *userdata)
{
    TitleInfo *title_info = (TitleInfo*)userdata;

    u8 content_type = 0;
    NcmContentInfo *content_info = NULL;
    NcaContext *nca_ctx = NULL;

    Ticket tik = {0};

    u32 crc = 0;
    char *filename = NULL;

    bool remove_console_data = (bool)getTicketRemoveConsoleDataOption();
    bool success = false;

    if (!title_info || title_info->meta_key.type < NcmContentMetaType_Application || title_info->meta_key.type == NcmContentMetaType_Delta || \
        title_info->meta_key.type > NcmContentMetaType_DataPatch)
    {
        consolePrint("invalid title info object\n");
        return false;
    }

    /* Get a NcmContentInfo entry for a potential NCA with a rights ID. */
    content_type = ((title_info->meta_key.type == NcmContentMetaType_Application || title_info->meta_key.type == NcmContentMetaType_Patch) ? NcmContentType_Program : NcmContentType_Data);
    content_info = titleGetContentInfoByTypeAndIdOffset(title_info, content_type, 0);
    if (!content_info)
    {
        consolePrint("content info entry with type 0x%X unavailable\n", content_type);
        return false;
    }

    /* Allocate buffer for NCA context. */
    if (!(nca_ctx = calloc(1, sizeof(NcaContext))))
    {
        consolePrint("nca ctx calloc failed\n");
        goto end;
    }

    /* Initialize NCA context. */
    if (!ncaInitializeContext(nca_ctx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0), \
                              &(title_info->meta_key), content_info, &tik))
    {
        consolePrint("nca initialize ctx failed\n");
        goto end;
    }

    /* Check if a ticket was retrieved. */
    if (!nca_ctx->rights_id_available)
    {
        consolePrint("rights id unavailable in target title -- this title doesn't use titlekey crypto\nthere's no ticket to be retrieved\n");
        goto end;
    }

    if (!nca_ctx->titlekey_retrieved)
    {
        consolePrint("failed to retrieve ticket (unavailable?)\ntry launching nxdumptool while overriding the title you wish to dump a ticket from\n");
        goto end;
    }

    /* Remove console-specific data, if needed. */
    if (remove_console_data && tikIsPersonalizedTicket(&tik) && !tikConvertPersonalizedTicketToCommonTicket(&tik, NULL, NULL))
    {
        consolePrint("failed to convert personalized ticket to common ticket\n");
        goto end;
    }

    /* Save ticket. */
    crc = crc32Calculate(tik.data, tik.size);
    snprintf(path, MAX_ELEMENTS(path), " (%08X).tik", crc);

    filename = generateOutputTitleFileName(title_info, "Ticket", path);
    if (!filename) goto end;

    if (!saveFileData(filename, tik.data, tik.size)) goto end;

    consolePrint("rights id: %s\n", tik.rights_id_str);
    consolePrint("encrypted titlekey: %s\n", tik.enc_titlekey_str);
    consolePrint("decrypted titlekey: %s\n\n", tik.dec_titlekey_str);

    consolePrint("successfully saved ticket as \"%s\"\n", filename);
    success = true;

end:
    if (filename) free(filename);

    if (nca_ctx) free(nca_ctx);

    return success;
}

static bool saveNintendoContentArchive(void *userdata)
{
    if (!userdata) return false;

    NcaUserData *nca_user_data = (NcaUserData*)userdata;
    TitleInfo *title_info = nca_user_data->title_info;
    NcmContentInfo *content_info = &(title_info->content_infos[nca_user_data->content_idx]);

    NcaThreadData nca_thread_data = {0};
    SharedThreadData *shared_thread_data = &(nca_thread_data.shared_thread_data);

    u64 free_space = 0;
    char *filename = NULL, subdir[0x20] = {0};
    u32 dev_idx = g_storageMenuElementOption.selected;

    bool success = false;

    /* Allocate buffer for NCA context. */
    if (!(nca_thread_data.nca_ctx = calloc(1, sizeof(NcaContext))))
    {
        consolePrint("nca ctx calloc failed\n");
        goto end;
    }

    /* Initialize NCA context. */
    if (!ncaInitializeContext(nca_thread_data.nca_ctx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0), \
                              &(title_info->meta_key), content_info, NULL))
    {
        consolePrint("nca initialize ctx failed\n");
        goto end;
    }

    shared_thread_data->total_size = nca_thread_data.nca_ctx->content_size;

    consolePrint("nca size: 0x%lX\n", shared_thread_data->total_size);

    snprintf(subdir, MAX_ELEMENTS(subdir), "NCA/%s", nca_thread_data.nca_ctx->storage_id == NcmStorageId_BuiltInSystem ? "System" : "User");
    snprintf(path, MAX_ELEMENTS(path), "/%s.%s", nca_thread_data.nca_ctx->content_id_str, content_info->content_type == NcmContentType_Meta ? "cnmt.nca" : "nca");

    filename = generateOutputTitleFileName(title_info, subdir, path);
    if (!filename) goto end;

    if (dev_idx == 1)
    {
        if (!usbSendFileProperties(shared_thread_data->total_size, filename))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", filename);
            goto end;
        }
    } else {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            goto end;
        }

        if (shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            goto end;
        }

        utilsCreateDirectoryTree(filename, false);

        if (dev_idx == 0)
        {
            if (shared_thread_data->total_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(filename))
            {
                consolePrint("failed to create concatenation file for \"%s\"!\n", filename);
                goto end;
            }
        } else {
            if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && shared_thread_data->total_size > FAT32_FILESIZE_LIMIT)
            {
                consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                goto end;
            }
        }

        shared_thread_data->fp = fopen(filename, "wb");
        if (!shared_thread_data->fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", filename);
            goto end;
        }

        setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
        ftruncate(fileno(shared_thread_data->fp), (off_t)shared_thread_data->total_size);
    }

    consoleRefresh();

    success = spanDumpThreads(ncaReadThreadFunc, genericWriteThreadFunc, &nca_thread_data);

    if (success)
    {
        consolePrint("successfully saved nca as \"%s\"\n", filename);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if (!success && dev_idx != 1)
        {
            if (dev_idx == 0)
            {
                utilsRemoveConcatenationFile(filename);
                utilsCommitSdCardFileSystemChanges();
            } else {
                remove(filename);
            }
        }
    }

    if (filename) free(filename);

    if (nca_thread_data.nca_ctx) free(nca_thread_data.nca_ctx);

    return success;
}

static bool saveNintendoContentArchiveFsSection(void *userdata)
{
    u8 section_type = 0;
    bool use_layeredfs_dir = false;
    NcaContext *base_patch_nca_ctx = NULL;
    void *fs_ctx = NULL;

    bool write_raw_section = (bool)getNcaFsWriteRawSectionOption();
    bool success = false;

    /* Initialize NCA FS section context. */
    if (!initializeNcaFsContext(userdata, &section_type, &use_layeredfs_dir, &base_patch_nca_ctx, &fs_ctx)) return false;

    /* Perform requested operation. */
    if (section_type == NcaFsSectionType_PartitionFs)
    {
        PartitionFileSystemContext *pfs_ctx = (PartitionFileSystemContext*)fs_ctx;
        success = (write_raw_section ? saveRawPartitionFsSection(pfs_ctx, use_layeredfs_dir) : saveExtractedPartitionFsSection(pfs_ctx, use_layeredfs_dir));
        pfsFreeContext(pfs_ctx);
    } else {
        RomFileSystemContext *romfs_ctx = (RomFileSystemContext*)fs_ctx;
        success = (write_raw_section ? saveRawRomFsSection(romfs_ctx, use_layeredfs_dir) : saveExtractedRomFsSection(romfs_ctx, use_layeredfs_dir));
        romfsFreeContext(romfs_ctx);
    }

    /* Free data. */
    free(fs_ctx);
    free(base_patch_nca_ctx);

    return success;
}

static bool browseNintendoContentArchiveFsSection(void *userdata)
{
    u8 section_type = 0;
    bool use_layeredfs_dir = false;
    NcaContext *base_patch_nca_ctx = NULL;
    void *fs_ctx = NULL;

    PartitionFileSystemContext *pfs_ctx = NULL;
    RomFileSystemContext *romfs_ctx = NULL;

    NcaFsSectionContext *nca_fs_ctx = NULL;
    NcaContext *nca_ctx = NULL;

    u64 title_id = 0;
    u8 title_type = 0;

    char mount_name[DEVOPTAB_MOUNT_NAME_LENGTH] = {0}, subdir[0x20] = {0}, extension[FS_MAX_PATH] = {0};
    char *base_out_path = NULL;

    bool success = false;

    /* Initialize NCA FS section context. */
    if (!initializeNcaFsContext(userdata, &section_type, &use_layeredfs_dir, &base_patch_nca_ctx, &fs_ctx)) goto end;

    /* Mount devoptab device. */
    if (section_type == NcaFsSectionType_PartitionFs)
    {
        pfs_ctx = (PartitionFileSystemContext*)fs_ctx;
        nca_fs_ctx = pfs_ctx->nca_fs_ctx;

        snprintf(mount_name, MAX_ELEMENTS(mount_name), "%s", pfs_ctx->is_exefs ? "ncaexefs" : "ncapfs");

        if (!devoptabMountPartitionFileSystemDevice(pfs_ctx, mount_name))
        {
            consolePrint("pfs ctx devoptab mount failed!\n");
            goto end;
        }
    } else {
        romfs_ctx = (RomFileSystemContext*)fs_ctx;
        nca_fs_ctx = romfs_ctx->default_storage_ctx->nca_fs_ctx;

        snprintf(mount_name, MAX_ELEMENTS(mount_name), "ncaromfs");

        if (!devoptabMountRomFileSystemDevice(romfs_ctx, mount_name))
        {
            consolePrint("romfs ctx devoptab mount failed!\n");
            goto end;
        }
    }

    /* Generate output base path. */
    nca_ctx = nca_fs_ctx->nca_ctx;
    title_id = nca_ctx->title_id;
    title_type = nca_ctx->title_type;

    if (use_layeredfs_dir)
    {
        /* Only use base title IDs if we're dealing with patches. */
        title_id = (title_type == NcmContentMetaType_Patch ? titleGetApplicationIdByPatchId(title_id) : \
                   (title_type == NcmContentMetaType_DataPatch ? titleGetAddOnContentIdByDataPatchId(title_id) : title_id));

        base_out_path = generateOutputLayeredFsFileName(title_id + nca_ctx->id_offset, NULL, section_type == NcaFsSectionType_PartitionFs ? "exefs" : "romfs");
    } else {
        snprintf(subdir, MAX_ELEMENTS(subdir), "NCA FS/%s/Extracted", nca_ctx->storage_id == NcmStorageId_BuiltInSystem ? "System" : "User");
        snprintf(extension, MAX_ELEMENTS(extension), "/%s #%u/%u", titleGetNcmContentTypeName(nca_ctx->content_type), nca_ctx->id_offset, nca_fs_ctx->section_idx);

        TitleInfo *title_info = (title_id == g_ncaUserTitleInfo->meta_key.id ? g_ncaUserTitleInfo : g_ncaBasePatchTitleInfo);
        base_out_path = generateOutputTitleFileName(title_info, subdir, extension);
    }

    if (!base_out_path) goto end;

    /* Display file browser. */
    success = fsBrowser(mount_name, base_out_path);

    /* Unmount devoptab device. */
    devoptabUnmountDevice(mount_name);

end:
    /* Free data. */
    if (base_out_path) free(base_out_path);
    if (pfs_ctx) pfsFreeContext(pfs_ctx);
    if (romfs_ctx) romfsFreeContext(romfs_ctx);
    if (fs_ctx) free(fs_ctx);
    if (base_patch_nca_ctx) free(base_patch_nca_ctx);

    if (!success && g_appletStatus)
    {
        consolePrint("press any button to continue\n");
        utilsWaitForButtonPress(0);
    }

    return success;
}

static bool fsBrowser(const char *mount_name, const char *base_out_path)
{
    char dir_path[FS_MAX_PATH] = {0};
    size_t dir_path_len = 0;

    FsBrowserEntry *entries = NULL;
    u32 entries_count = 0, depth = 0;

    u32 scroll = 0, selected = 0, highlighted = 0, page_size = 20;

    bool success = true;

    /* Get root directory entries. */
    snprintf(dir_path, MAX_ELEMENTS(dir_path), "%s:/", mount_name);
    dir_path_len = strlen(dir_path);

    if (!(success = fsBrowserGetDirEntries(dir_path, &entries, &entries_count))) goto end;

    while((g_appletStatus = appletMainLoop()))
    {
        consoleClear();

        consolePrint("press a to enter a directory / dump a file\n");
        consolePrint("press b to %s\n", depth > 0 ? "move back to the parent dir" : "exit the fs browser");
        consolePrint("press r to (un)highlight the selected entry\n");
        consolePrint("press l to invert the current selection\n");
        consolePrint("press zr to highlight all entries\n");
        consolePrint("press zl to unhighlight all entries\n");
        consolePrint("press y to dump the highlighted entries\n");
        consolePrint("use the sticks to scroll faster\n");
        consolePrint("press + to exit\n");
        consolePrint("______________________________\n\n");

        consolePrint("entry: %u / %u\n", selected + 1, entries_count);
        consolePrint("highlighted: %u / %u\n", highlighted, entries_count);
        consolePrint("current path: %s\n", dir_path);
        consolePrint("______________________________\n\n");

        for(u32 i = scroll; i < entries_count; i++)
        {
            if (i >= (scroll + page_size)) break;

            FsBrowserEntry *cur_entry = &(entries[i]);

            consolePrint("%s", i == selected ? " -> " : "    ");

            if (cur_entry->highlight)
            {
                consolePrintReversedColors("[%c] %s", cur_entry->dt.d_type == DT_DIR ? 'D' : 'F', cur_entry->dt.d_name);
                if (cur_entry->dt.d_type == DT_REG) consolePrintReversedColors(" (%s)", cur_entry->size_str);
            } else {
                consolePrint("[%c] %s", cur_entry->dt.d_type == DT_DIR ? 'D' : 'F', cur_entry->dt.d_name);
                if (cur_entry->dt.d_type == DT_REG) consolePrint(" (%s)", cur_entry->size_str);
            }

            consolePrint("\n");
        }

        if (!entries_count) consolePrint("no elements available!");

        consolePrint("\n");
        consoleRefresh();

        u64 btn_down = 0, btn_held = 0;

        while((g_appletStatus = appletMainLoop()))
        {
            utilsScanPads();
            btn_down = utilsGetButtonsDown();
            btn_held = utilsGetButtonsHeld();
            if (btn_down || btn_held) break;

            utilsAppletLoopDelay();
        }

        if (!g_appletStatus) break;

        if ((btn_down & HidNpadButton_A) && entries_count)
        {
            FsBrowserEntry *selected_entry = &(entries[selected]);

            if (selected_entry->dt.d_type == DT_DIR)
            {
                /* Change directory. */
                snprintf(dir_path + dir_path_len, MAX_ELEMENTS(dir_path) - dir_path_len, "%s%s", depth > 0 ? "/" : "", selected_entry->dt.d_name);

                if (!(success = fsBrowserGetDirEntries(dir_path, &entries, &entries_count))) break;

                /* Update variables. */
                dir_path_len = strlen(dir_path);
                scroll = selected = highlighted = 0;
                depth++;
            } else {
                /* Dump file. */
                utilsSetLongRunningProcessState(true);
                fsBrowserDumpFile(dir_path, selected_entry, base_out_path);
                utilsSetLongRunningProcessState(false);
            }
        } else
        if (btn_down & HidNpadButton_B)
        {
            if (depth > 0)
            {
                /* Go back to the parent directory. */
                char *ptr = strrchr(dir_path, '/');

                if (depth > 1)
                {
                    *ptr = '\0';
                } else {
                    *(++ptr) = '\0';
                }

                if (!(success = fsBrowserGetDirEntries(dir_path, &entries, &entries_count))) break;

                /* Update variables. */
                dir_path_len = strlen(dir_path);
                scroll = selected = highlighted = 0;
                depth--;
            } else {
                break;
            }
        } else
        if ((btn_down & HidNpadButton_R) && entries_count)
        {
            /* (Un)highlight the selected entry. */
            FsBrowserEntry *selected_entry = &(entries[selected]);
            selected_entry->highlight ^= 1;
            highlighted += (selected_entry->highlight ? 1 : -1);
        } else
        if ((btn_down & HidNpadButton_L) && entries_count)
        {
            /* Invert current selection. */
            for(u32 i = 0; i < entries_count; i++)
            {
                FsBrowserEntry *cur_entry = &(entries[i]);
                cur_entry->highlight ^= 1;
                highlighted += (cur_entry->highlight ? 1 : -1);
            }
        } else
        if ((btn_down & HidNpadButton_ZR) && entries_count)
        {
            /* Highlight all entries. */
            for(u32 i = 0; i < entries_count; i++) entries[i].highlight = true;

            /* Update counter. */
            highlighted = entries_count;
        } else
        if ((btn_down & HidNpadButton_ZL) && entries_count)
        {
            /* Unhighlight all entries. */
            for(u32 i = 0; i < entries_count; i++) entries[i].highlight = false;

            /* Reset counter. */
            highlighted = 0;
        } else
        if ((btn_down & HidNpadButton_Y) && entries_count && highlighted)
        {
            /* Dump highlighted entries. */
            utilsSetLongRunningProcessState(true);
            fsBrowserDumpHighlightedEntries(dir_path, entries, entries_count, base_out_path);
            utilsSetLongRunningProcessState(false);

            /* Unhighlight all entries. */
            for(u32 i = 0; i < entries_count; i++) entries[i].highlight = false;

            /* Reset counter. */
            highlighted = 0;
        } else
        if (((btn_down & HidNpadButton_Down) || (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown))) && entries_count)
        {
            selected++;

            if (selected >= entries_count)
            {
                if (btn_down & HidNpadButton_Down)
                {
                    scroll = 0;
                    selected = 0;
                } else {
                    selected--;
                }
            } else
            if (selected >= (scroll + (page_size / 2)) && entries_count > (scroll + page_size))
            {
                scroll++;
            }
        } else
        if (((btn_down & HidNpadButton_Up) || (btn_held & (HidNpadButton_StickLUp | HidNpadButton_StickRUp))) && entries_count)
        {
            selected--;

            if (selected == UINT32_MAX)
            {
                if (btn_down & HidNpadButton_Up)
                {
                    selected = (entries_count - 1);
                    scroll = (entries_count >= page_size ? (entries_count - page_size) : 0);
                } else {
                    selected = 0;
                }
            } else
            if (selected < (scroll + (page_size / 2)) && scroll > 0)
            {
                scroll--;
            }
        } else
        if (btn_down & HidNpadButton_Plus)
        {
            g_appletStatus = false;
            break;
        }

        utilsAppletLoopDelay();
    }

end:
    if (entries) free(entries);

    return success;
}

static bool fsBrowserGetDirEntries(const char *dir_path, FsBrowserEntry **out_entries, u32 *out_entry_count)
{
    DIR *dp = NULL;
    struct dirent *dt = NULL;
    struct stat st = {0};
    FsBrowserEntry *entries = NULL, *entries_tmp = NULL;
    char tmp_path[FS_MAX_PATH] = {0};
    u32 count = 0;
    bool append_path_sep = (dir_path[strlen(dir_path) - 1] != '/');
    bool success = false;

    /* Free input pointer, if needed. */
    if (*out_entries)
    {
        free(*out_entries);
        *out_entries = NULL;
    }

    /* Open directory. */
    dp = opendir(dir_path);
    if (!dp)
    {
        consolePrint("failed to open dir \"%s\"\n", dir_path);
        goto end;
    }

    /* Get entry count. */
    while((dt = readdir(dp)))
    {
        /* Skip "." and ".." entries. */
        if (!strcmp(dt->d_name, ".") || !strcmp(dt->d_name, "..")) continue;

        /* Reallocate directory entries buffer. */
        if (!(entries_tmp = realloc(entries, (count + 1) * sizeof(FsBrowserEntry))))
        {
            consolePrint("failed to allocate memory for dir entries in \"%s\"\n", dir_path);
            goto end;
        }

        entries = entries_tmp;
        entries_tmp = NULL;

        /* Store entry data. */
        FsBrowserEntry *cur_entry = &(entries[count++]);

        memset(cur_entry, 0, sizeof(FsBrowserEntry));

        if (dt->d_type == DT_REG)
        {
            /* Get file size. */
            snprintf(tmp_path, MAX_ELEMENTS(tmp_path), "%s%s%s", dir_path, append_path_sep ? "/" : "", dt->d_name);
            stat(tmp_path, &st);
            cur_entry->size = st.st_size;
            utilsGenerateFormattedSizeString((double)st.st_size, cur_entry->size_str, sizeof(cur_entry->size_str));
        }

        memcpy(&(cur_entry->dt), dt, sizeof(struct dirent));
    }

    /* Short-circuit: handle empty directories. */
    if (!entries)
    {
        *out_entry_count = 0;
        success = true;
        goto end;
    }

    /* Update output pointers. */
    *out_entries = entries;
    *out_entry_count = count;

    /* Update return value. */
    success = true;

end:
    if (dp) closedir(dp);

    if (!success && entries) free(entries);

    return success;
}

static bool fsBrowserDumpFile(const char *dir_path, const FsBrowserEntry *entry, const char *base_out_path)
{
    u64 free_space = 0;

    FsBrowserFileThreadData fs_browser_thread_data = {0};
    SharedThreadData *shared_thread_data = &(fs_browser_thread_data.shared_thread_data);

    u32 dev_idx = g_storageMenuElementOption.selected;

    bool success = false;

    shared_thread_data->total_size = entry->size;

    snprintf(path, MAX_ELEMENTS(path), "%s%s%s", dir_path, dir_path[strlen(dir_path) - 1] != '/' ? "/" : "", entry->dt.d_name);

    consoleClear();
    consolePrint("file path: %s\n", path);
    consolePrint("file size: 0x%lX\n\n", entry->size);

    /* Open input file. */
    fs_browser_thread_data.src = fopen(path, "rb");
    if (!fs_browser_thread_data.src)
    {
        consolePrint("failed to open input file!\n");
        goto end;
    }

    setvbuf(fs_browser_thread_data.src, NULL, _IONBF, 0);

    const char *dir_path_start = (strchr(dir_path, '/') + 1);
    if (*dir_path_start)
    {
        snprintf(path, MAX_ELEMENTS(path), "%s/%s/%s", base_out_path, dir_path_start, entry->dt.d_name);
    } else {
        snprintf(path, MAX_ELEMENTS(path), "%s/%s", base_out_path, entry->dt.d_name);
    }

    if (dev_idx == 1)
    {
        if (!waitForUsb()) goto end;

        if (!usbSendFileProperties(shared_thread_data->total_size, path))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", path);
            goto end;
        }
    } else {
        if (!utilsGetFileSystemStatsByPath(path, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            goto end;
        }

        if (shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            goto end;
        }

        utilsCreateDirectoryTree(path, false);

        if (dev_idx == 0)
        {
            if (shared_thread_data->total_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(path))
            {
                consolePrint("failed to create concatenation file for \"%s\"!\n", path);
                goto end;
            }
        } else {
            if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && shared_thread_data->total_size > FAT32_FILESIZE_LIMIT)
            {
                consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                goto end;
            }
        }

        shared_thread_data->fp = fopen(path, "wb");
        if (!shared_thread_data->fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", path);
            goto end;
        }

        setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
        ftruncate(fileno(shared_thread_data->fp), (off_t)shared_thread_data->total_size);
    }

    consoleRefresh();

    success = spanDumpThreads(fsBrowserFileReadThreadFunc, genericWriteThreadFunc, &fs_browser_thread_data);

    if (success)
    {
        consolePrint("successfully saved file to \"%s\"\n", path);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if (!success && dev_idx != 1)
        {
            if (dev_idx == 0)
            {
                utilsRemoveConcatenationFile(path);
                utilsCommitSdCardFileSystemChanges();
            } else {
                remove(path);
            }
        }
    }

    if (fs_browser_thread_data.src) fclose(fs_browser_thread_data.src);

    consolePrint("press any button to continue\n");
    utilsWaitForButtonPress(0);

    return success;
}

static bool fsBrowserDumpHighlightedEntries(const char *dir_path, const FsBrowserEntry *entries, u32 entries_count, const char *base_out_path)
{
    bool append_path_sep = (dir_path[strlen(dir_path) - 1] != '/');
    u64 data_size = 0;

    FsBrowserHighlightedEntriesThreadData fs_browser_thread_data = {0};
    SharedThreadData *shared_thread_data = &(fs_browser_thread_data.shared_thread_data);

    bool success = false;

    consoleClear();
    consolePrint("calculating dump size...\n");
    consoleRefresh();

    /* Calculate dump size. */
    for(u32 i = 0; i < entries_count; i++)
    {
        const FsBrowserEntry *cur_entry = &(entries[i]);
        if (!cur_entry->highlight) continue;

        if (cur_entry->dt.d_type == DT_DIR)
        {
            /* Get directory size. */
            u64 dir_size = 0;
            snprintf(path, MAX_ELEMENTS(path), "%s%s%s", dir_path, append_path_sep ? "/" : "", cur_entry->dt.d_name);

            if (!utilsGetDirectorySize(path, &dir_size))
            {
                consolePrint("failed to calculate size for dir \"%s\"\n", path);
                goto end;
            }

            /* Update dump size. */
            data_size += dir_size;
        } else {
            /* Update dump size. */
            data_size += cur_entry->size;
        }
    }

    fs_browser_thread_data.dir_path = dir_path;
    fs_browser_thread_data.entries = entries;
    fs_browser_thread_data.entries_count = entries_count;
    fs_browser_thread_data.base_out_path = base_out_path;
    shared_thread_data->total_size = data_size;

    consolePrint("dump size: 0x%lX\n", data_size);
    consoleRefresh();

    success = spanDumpThreads(fsBrowserHighlightedEntriesReadThreadFunc, genericWriteThreadFunc, &fs_browser_thread_data);

end:
    consolePrint("press any button to continue\n");
    utilsWaitForButtonPress(0);

    return success;
}

static bool initializeNcaFsContext(void *userdata, u8 *out_section_type, bool *out_use_layeredfs_dir, NcaContext **out_base_patch_nca_ctx, void **out_fs_ctx)
{
    NcaFsSectionContext *nca_fs_ctx = (NcaFsSectionContext*)userdata;
    NcaContext *nca_ctx = (nca_fs_ctx ? nca_fs_ctx->nca_ctx : NULL);

    /* Sanity checks. */

    if (!g_ncaUserTitleInfo || !nca_fs_ctx || !nca_ctx || !nca_fs_ctx->enabled || nca_fs_ctx->section_type > NcaFsSectionType_Nca0RomFs || \
        (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs && g_ncaBasePatchTitleInfo))
    {
        consolePrint("invalid nca fs parameters!\n");
        return false;
    }

    if (nca_fs_ctx->has_sparse_layer)
    {
        if (!g_ncaBasePatchTitleInfo)
        {
            consolePrint("the selected nca fs section holds a sparse storage\na matching patch of at least v%u must be selected\n", nca_ctx->title_version.value);
            return false;
        } else
        if (g_ncaBasePatchTitleInfo->version.value < nca_ctx->title_version.value)
        {
            consolePrint("the selected patch doesn't meet the sparse storage version requirement!\nv%u < v%u\n", g_ncaBasePatchTitleInfo->version.value, nca_ctx->title_version.value);
            return false;
        }
    }

    if (nca_fs_ctx->section_type == NcaFsSectionType_PatchRomFs && !g_ncaBasePatchTitleInfo)
    {
        consolePrint("patch romfs section selected but no base app provided\n");
        return false;
    }

    u8 title_type = nca_ctx->title_type;
    u8 content_type = nca_ctx->content_type;
    u8 section_type = nca_fs_ctx->section_type;

    NcmContentInfo *base_patch_content_info = (g_ncaBasePatchTitleInfo ? titleGetContentInfoByTypeAndIdOffset(g_ncaBasePatchTitleInfo, content_type, nca_ctx->id_offset) : NULL);
    NcaContext *base_patch_nca_ctx = NULL;
    NcaFsSectionContext *base_patch_nca_fs_ctx = NULL;

    bool use_layeredfs_dir = (bool)getNcaFsUseLayeredFsDirOption();
    bool success = false;

    /* Override LayeredFS flag, if needed. */
    if (use_layeredfs_dir && \
        (title_type == NcmContentMetaType_Unknown || (title_type > NcmContentMetaType_SystemData && title_type < NcmContentMetaType_Application) || \
        (title_type == NcmContentMetaType_SystemProgram && (content_type != NcmContentType_Program || nca_fs_ctx->section_idx != 0)) || \
        (title_type == NcmContentMetaType_SystemData && (content_type != NcmContentType_Data || nca_fs_ctx->section_idx != 0)) || \
        ((title_type == NcmContentMetaType_Application || title_type == NcmContentMetaType_Patch) && (content_type != NcmContentType_Program || nca_fs_ctx->section_idx > 1)) || \
        ((title_type == NcmContentMetaType_AddOnContent || title_type == NcmContentMetaType_DataPatch) && (content_type != NcmContentType_Data || nca_fs_ctx->section_idx != 0))))
    {
        consolePrint("layeredfs setting disabled (unsupported by current content/section type combo)\n");
        use_layeredfs_dir = false;
    }

    /* Initialize base/patch NCA context, if needed. */
    if (base_patch_content_info)
    {
        base_patch_nca_ctx = calloc(1, sizeof(NcaContext));
        if (!base_patch_nca_ctx)
        {
            consolePrint("failed to allocate memory for base/patch nca ctx!\n");
            goto end;
        }

        if (!ncaInitializeContext(base_patch_nca_ctx, g_ncaBasePatchTitleInfo->storage_id, (g_ncaBasePatchTitleInfo->storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0), \
                                  &(g_ncaBasePatchTitleInfo->meta_key), base_patch_content_info, NULL))
        {
            consolePrint("failed to initialize base/patch nca ctx!\n");
            goto end;
        }

        /* Use a matching NCA FS section entry. */
        base_patch_nca_fs_ctx = &(base_patch_nca_ctx->fs_ctx[nca_fs_ctx->section_idx]);
    }

    if (section_type == NcaFsSectionType_PartitionFs)
    {
        /* Select the right NCA FS section context, depending on the sparse layer flag. */
        NcaFsSectionContext *pfs_nca_fs_ctx = ((title_type == NcmContentMetaType_Application && base_patch_nca_fs_ctx && base_patch_nca_fs_ctx->enabled) ? base_patch_nca_fs_ctx : nca_fs_ctx);

        /* Initialize PartitionFS context. */
        PartitionFileSystemContext *pfs_ctx = calloc(1, sizeof(PartitionFileSystemContext));
        if (!pfs_ctx)
        {
            consolePrint("pfs ctx alloc failed!\n");
            goto end;
        }

        if (!pfsInitializeContext(pfs_ctx, pfs_nca_fs_ctx))
        {
            consolePrint("pfs initialize ctx failed!\n");
            free(pfs_ctx);
            goto end;
        }

        *out_fs_ctx = pfs_ctx;
    } else {
        /* Select the right base/patch NCA FS section contexts. */
        NcaFsSectionContext *base_nca_fs_ctx = (section_type == NcaFsSectionType_PatchRomFs ? base_patch_nca_fs_ctx : nca_fs_ctx);
        NcaFsSectionContext *patch_nca_fs_ctx = (section_type == NcaFsSectionType_PatchRomFs ? nca_fs_ctx : base_patch_nca_fs_ctx);

        /* Initialize RomFS context. */
        RomFileSystemContext *romfs_ctx = calloc(1, sizeof(RomFileSystemContext));
        if (!romfs_ctx)
        {
            consolePrint("romfs ctx alloc failed!\n");
            goto end;
        }

        if (!romfsInitializeContext(romfs_ctx, base_nca_fs_ctx, patch_nca_fs_ctx))
        {
            consolePrint("romfs initialize ctx failed!\n");
            free(romfs_ctx);
            goto end;
        }

        *out_fs_ctx = romfs_ctx;
    }

    /* Update output pointers. */
    *out_section_type = section_type;
    *out_use_layeredfs_dir = use_layeredfs_dir;
    *out_base_patch_nca_ctx = base_patch_nca_ctx;

    success = true;

end:
    if (!success && base_patch_nca_ctx) free(base_patch_nca_ctx);

    return success;
}

static bool saveRawPartitionFsSection(PartitionFileSystemContext *pfs_ctx, bool use_layeredfs_dir)
{
    u64 free_space = 0;

    PfsThreadData pfs_thread_data = {0};
    SharedThreadData *shared_thread_data = &(pfs_thread_data.shared_thread_data);

    NcaFsSectionContext *nca_fs_ctx = pfs_ctx->nca_fs_ctx;
    NcaContext *nca_ctx = nca_fs_ctx->nca_ctx;

    u64 title_id = nca_ctx->title_id;
    u8 title_type = nca_ctx->title_type;

    char subdir[0x20] = {0}, *filename = NULL;
    u32 dev_idx = g_storageMenuElementOption.selected;

    bool success = false;

    pfs_thread_data.pfs_ctx = pfs_ctx;
    pfs_thread_data.use_layeredfs_dir = use_layeredfs_dir;
    shared_thread_data->total_size = pfs_ctx->size;

    consolePrint("raw partitionfs section size: 0x%lX\n", pfs_ctx->size);

    if (use_layeredfs_dir)
    {
        /* Only use base title IDs if we're dealing with patches. */
        title_id = (title_type == NcmContentMetaType_Patch ? titleGetApplicationIdByPatchId(title_id) : \
                   (title_type == NcmContentMetaType_DataPatch ? titleGetAddOnContentIdByDataPatchId(title_id) : title_id));

        filename = generateOutputLayeredFsFileName(title_id + nca_ctx->id_offset, NULL, "exefs.nsp");
    } else {
        snprintf(subdir, MAX_ELEMENTS(subdir), "NCA FS/%s/Raw", nca_ctx->storage_id == NcmStorageId_BuiltInSystem ? "System" : "User");
        snprintf(path, MAX_ELEMENTS(path), "/%s #%u/%u.nsp", titleGetNcmContentTypeName(nca_ctx->content_type), nca_ctx->id_offset, nca_fs_ctx->section_idx);

        TitleInfo *title_info = (title_id == g_ncaUserTitleInfo->meta_key.id ? g_ncaUserTitleInfo : g_ncaBasePatchTitleInfo);
        filename = generateOutputTitleFileName(title_info, subdir, path);
    }

    if (!filename) goto end;

    if (dev_idx == 1)
    {
        if (!usbSendFileProperties(shared_thread_data->total_size, filename))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", filename);
            goto end;
        }
    } else {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            goto end;
        }

        if (shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            goto end;
        }

        utilsCreateDirectoryTree(filename, false);

        if (dev_idx == 0)
        {
            if (shared_thread_data->total_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(filename))
            {
                consolePrint("failed to create concatenation file for \"%s\"!\n", filename);
                goto end;
            }
        } else {
            if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && shared_thread_data->total_size > FAT32_FILESIZE_LIMIT)
            {
                consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                goto end;
            }
        }

        shared_thread_data->fp = fopen(filename, "wb");
        if (!shared_thread_data->fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", filename);
            goto end;
        }

        setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
        ftruncate(fileno(shared_thread_data->fp), (off_t)shared_thread_data->total_size);
    }

    consoleRefresh();

    success = spanDumpThreads(rawPartitionFsReadThreadFunc, genericWriteThreadFunc, &pfs_thread_data);

    if (success)
    {
        consolePrint("successfully saved raw partitionfs section as \"%s\"\n", filename);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if (!success && dev_idx != 1)
        {
            if (dev_idx == 0)
            {
                utilsRemoveConcatenationFile(filename);
                utilsCommitSdCardFileSystemChanges();
            } else {
                remove(filename);
            }
        }
    }

    if (filename) free(filename);

    return success;
}

static bool saveExtractedPartitionFsSection(PartitionFileSystemContext *pfs_ctx, bool use_layeredfs_dir)
{
    u64 data_size = 0;

    PfsThreadData pfs_thread_data = {0};
    SharedThreadData *shared_thread_data = &(pfs_thread_data.shared_thread_data);

    bool success = false;

    if (!pfsGetTotalDataSize(pfs_ctx, &data_size))
    {
        consolePrint("failed to calculate extracted partitionfs section size!\n");
        goto end;
    }

    if (!data_size)
    {
        consolePrint("partitionfs section is empty!\n");
        goto end;
    }

    pfs_thread_data.pfs_ctx = pfs_ctx;
    pfs_thread_data.use_layeredfs_dir = use_layeredfs_dir;
    shared_thread_data->total_size = data_size;

    consolePrint("extracted partitionfs section size: 0x%lX\n", data_size);
    consoleRefresh();

    success = spanDumpThreads(extractedPartitionFsReadThreadFunc, genericWriteThreadFunc, &pfs_thread_data);

end:
    return success;
}

static bool saveRawRomFsSection(RomFileSystemContext *romfs_ctx, bool use_layeredfs_dir)
{
    u64 free_space = 0;

    RomFsThreadData romfs_thread_data = {0};
    SharedThreadData *shared_thread_data = &(romfs_thread_data.shared_thread_data);

    NcaFsSectionContext *nca_fs_ctx = romfs_ctx->default_storage_ctx->nca_fs_ctx;
    NcaContext *nca_ctx = nca_fs_ctx->nca_ctx;

    u64 title_id = nca_ctx->title_id;
    u8 title_type = nca_ctx->title_type;

    char subdir[0x20] = {0}, *filename = NULL;
    u32 dev_idx = g_storageMenuElementOption.selected;

    bool success = false;

    romfs_thread_data.romfs_ctx = romfs_ctx;
    romfs_thread_data.use_layeredfs_dir = use_layeredfs_dir;
    shared_thread_data->total_size = romfs_ctx->size;

    consolePrint("raw romfs section size: 0x%lX\n", romfs_ctx->size);

    if (use_layeredfs_dir)
    {
        /* Only use base title IDs if we're dealing with patches. */
        title_id = (title_type == NcmContentMetaType_Patch ? titleGetApplicationIdByPatchId(title_id) : \
                   (title_type == NcmContentMetaType_DataPatch ? titleGetAddOnContentIdByDataPatchId(title_id) : title_id));

        filename = generateOutputLayeredFsFileName(title_id + nca_ctx->id_offset, NULL, "romfs.bin");
    } else {
        snprintf(subdir, MAX_ELEMENTS(subdir), "NCA FS/%s/Raw", nca_ctx->storage_id == NcmStorageId_BuiltInSystem ? "System" : "User");
        snprintf(path, MAX_ELEMENTS(path), "/%s #%u/%u.bin", titleGetNcmContentTypeName(nca_ctx->content_type), nca_ctx->id_offset, nca_fs_ctx->section_idx);

        TitleInfo *title_info = (title_id == g_ncaUserTitleInfo->meta_key.id ? g_ncaUserTitleInfo : g_ncaBasePatchTitleInfo);
        filename = generateOutputTitleFileName(title_info, subdir, path);
    }

    if (!filename) goto end;

    if (dev_idx == 1)
    {
        if (!usbSendFileProperties(shared_thread_data->total_size, filename))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", filename);
            goto end;
        }
    } else {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            goto end;
        }

        if (shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            goto end;
        }

        utilsCreateDirectoryTree(filename, false);

        if (dev_idx == 0)
        {
            if (shared_thread_data->total_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(filename))
            {
                consolePrint("failed to create concatenation file for \"%s\"!\n", filename);
                goto end;
            }
        } else {
            if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && shared_thread_data->total_size > FAT32_FILESIZE_LIMIT)
            {
                consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                goto end;
            }
        }

        shared_thread_data->fp = fopen(filename, "wb");
        if (!shared_thread_data->fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", filename);
            goto end;
        }

        setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
        ftruncate(fileno(shared_thread_data->fp), (off_t)shared_thread_data->total_size);
    }

    consoleRefresh();

    success = spanDumpThreads(rawRomFsReadThreadFunc, genericWriteThreadFunc, &romfs_thread_data);

    if (success)
    {
        consolePrint("successfully saved raw romfs section as \"%s\"\n", filename);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if (!success && dev_idx != 1)
        {
            if (dev_idx == 0)
            {
                utilsRemoveConcatenationFile(filename);
                utilsCommitSdCardFileSystemChanges();
            } else {
                remove(filename);
            }
        }
    }

    if (filename) free(filename);

    return success;
}

static bool saveExtractedRomFsSection(RomFileSystemContext *romfs_ctx, bool use_layeredfs_dir)
{
    u64 data_size = 0;

    RomFsThreadData romfs_thread_data = {0};
    SharedThreadData *shared_thread_data = &(romfs_thread_data.shared_thread_data);

    bool success = false;

    if (!romfsGetTotalDataSize(romfs_ctx, false, &data_size))
    {
        consolePrint("failed to calculate extracted romfs section size!\n");
        goto end;
    }

    if (!data_size)
    {
        consolePrint("romfs section is empty!\n");
        goto end;
    }

    romfs_thread_data.romfs_ctx = romfs_ctx;
    romfs_thread_data.use_layeredfs_dir = use_layeredfs_dir;
    shared_thread_data->total_size = data_size;

    consolePrint("extracted romfs section size: 0x%lX\n", data_size);
    consoleRefresh();

    success = spanDumpThreads(extractedRomFsReadThreadFunc, genericWriteThreadFunc, &romfs_thread_data);

end:
    return success;
}

static void xciReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    XciThreadData *xci_thread_data = (XciThreadData*)arg;
    SharedThreadData *shared_thread_data = &(xci_thread_data->shared_thread_data);

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!shared_thread_data->total_size || !buf1 || !buf2)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    shared_thread_data->data = NULL;
    shared_thread_data->data_size = 0;

    bool prepend_key_area = (bool)getGameCardPrependKeyAreaOption();
    bool keep_certificate = (bool)getGameCardKeepCertificateOption();
    bool calculate_checksum = (bool)getGameCardCalculateChecksumOption();

    for(u64 offset = 0, blksize = BLOCK_SIZE; offset < shared_thread_data->total_size; offset += blksize)
    {
        if (blksize > (shared_thread_data->total_size - offset)) blksize = (shared_thread_data->total_size - offset);

        /* Check if the transfer has been cancelled by the user */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Read current data chunk */
        shared_thread_data->read_error = !gamecardReadStorage(buf1, blksize, offset);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Remove certificate */
        if (!keep_certificate && offset == 0) memset((u8*)buf1 + GAMECARD_CERT_OFFSET, 0xFF, sizeof(FsGameCardCertificate));

        /* Update checksum */
        if (calculate_checksum)
        {
            xci_thread_data->xci_crc = crc32CalculateWithSeed(xci_thread_data->xci_crc, buf1, blksize);
            if (prepend_key_area) xci_thread_data->full_xci_crc = crc32CalculateWithSeed(xci_thread_data->full_xci_crc, buf1, blksize);
        }

        /* Wait until the previous data chunk has been written */
        mutexLock(&g_fileMutex);

        if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

        if (shared_thread_data->write_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }

        /* Update shared object. */
        shared_thread_data->data = buf1;
        shared_thread_data->data_size = blksize;

        /* Swap buffers. */
        buf1 = buf2;
        buf2 = shared_thread_data->data;

        /* Wake up the write thread to continue writing data. */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_writeCondvar);
    }

end:
    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void rawHfsReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    HfsThreadData *hfs_thread_data = (HfsThreadData*)arg;
    SharedThreadData *shared_thread_data = &(hfs_thread_data->shared_thread_data);
    HashFileSystemContext *hfs_ctx = hfs_thread_data->hfs_ctx;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!shared_thread_data->total_size || !hfs_ctx || !buf1 || !buf2)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    shared_thread_data->data = NULL;
    shared_thread_data->data_size = 0;

    for(u64 offset = 0, blksize = BLOCK_SIZE; offset < shared_thread_data->total_size; offset += blksize)
    {
        if (blksize > (shared_thread_data->total_size - offset)) blksize = (shared_thread_data->total_size - offset);

        /* Check if the transfer has been cancelled by the user */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Read current data chunk */
        shared_thread_data->read_error = !hfsReadPartitionData(hfs_ctx, buf1, blksize, offset);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Wait until the previous data chunk has been written */
        mutexLock(&g_fileMutex);

        if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

        if (shared_thread_data->write_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }

        /* Update shared object. */
        shared_thread_data->data = buf1;
        shared_thread_data->data_size = blksize;

        /* Swap buffers. */
        buf1 = buf2;
        buf2 = shared_thread_data->data;

        /* Wake up the write thread to continue writing data. */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_writeCondvar);
    }

end:
    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void extractedHfsReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    HfsThreadData *hfs_thread_data = (HfsThreadData*)arg;
    SharedThreadData *shared_thread_data = &(hfs_thread_data->shared_thread_data);

    HashFileSystemContext *hfs_ctx = hfs_thread_data->hfs_ctx;
    u32 hfs_entry_count = hfsGetEntryCount(hfs_ctx);

    char hfs_path[FS_MAX_PATH] = {0}, *filename = NULL;
    size_t filename_len = 0;

    HashFileSystemEntry *hfs_entry = NULL;
    char *hfs_entry_name = NULL;

    u64 free_space = 0;
    u32 dev_idx = g_storageMenuElementOption.selected;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    snprintf(hfs_path, MAX_ELEMENTS(hfs_path), "/%s", hfs_ctx->name);
    filename = generateOutputGameCardFileName("HFS/Extracted", hfs_path, true);
    filename_len = (filename ? strlen(filename) : 0);

    if (!shared_thread_data->total_size || !hfs_entry_count || !buf1 || !buf2 || !filename)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    if (dev_idx != 1)
    {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            shared_thread_data->read_error = true;
        }

        if (!shared_thread_data->read_error && shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            shared_thread_data->read_error = true;
        }
    } else {
        if (!usbStartExtractedFsDump(shared_thread_data->total_size, filename))
        {
            consolePrint("failed to send extracted fs info to host\n");
            shared_thread_data->read_error = true;
        }
    }

    if (shared_thread_data->read_error)
    {
        condvarWakeAll(&g_writeCondvar);
        goto end;
    }

    /* Loop through all file entries. */
    for(u32 i = 0; i < hfs_entry_count; i++)
    {
        /* Check if the transfer has been cancelled by the user. */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        if (dev_idx != 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Close file. */
            if (shared_thread_data->fp)
            {
                fclose(shared_thread_data->fp);
                shared_thread_data->fp = NULL;
                if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
            }
        }

        /* Retrieve Hash FS file entry information. */
        shared_thread_data->read_error = ((hfs_entry = hfsGetEntryByIndex(hfs_ctx, i)) == NULL || (hfs_entry_name = hfsGetEntryName(hfs_ctx, hfs_entry)) == NULL);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Generate output path. */
        snprintf(hfs_path, MAX_ELEMENTS(hfs_path), "%s/%s", filename, hfs_entry_name);
        utilsReplaceIllegalCharacters(hfs_path + filename_len + 1, dev_idx == 0);

        if (dev_idx == 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Send current file properties */
            shared_thread_data->read_error = !usbSendFileProperties(hfs_entry->size, hfs_path);
        } else {
            /* Create directory tree. */
            utilsCreateDirectoryTree(hfs_path, false);

            if (dev_idx == 0)
            {
                /* Create ConcatenationFile if we're dealing with a big file + SD card as the output storage. */
                if (hfs_entry->size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(hfs_path))
                {
                    consolePrint("failed to create concatenation file for \"%s\"!\n", hfs_path);
                    shared_thread_data->read_error = true;
                }
            } else {
                /* Don't handle file chunks on FAT12/FAT16/FAT32 formatted UMS devices. */
                if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && hfs_entry->size > FAT32_FILESIZE_LIMIT)
                {
                    consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                    shared_thread_data->read_error = true;
                }
            }

            if (!shared_thread_data->read_error)
            {
                /* Open output file. */
                shared_thread_data->read_error = ((shared_thread_data->fp = fopen(hfs_path, "wb")) == NULL);
                if (!shared_thread_data->read_error)
                {
                    /* Set file size. */
                    setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
                    ftruncate(fileno(shared_thread_data->fp), (off_t)hfs_entry->size);
                } else {
                    consolePrint("failed to open \"%s\" for writing!\n", hfs_path);
                }
            }
        }

        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        for(u64 offset = 0, blksize = BLOCK_SIZE; offset < hfs_entry->size; offset += blksize)
        {
            if (blksize > (hfs_entry->size - offset)) blksize = (hfs_entry->size - offset);

            /* Check if the transfer has been cancelled by the user. */
            if (shared_thread_data->transfer_cancelled)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Read current file data chunk. */
            shared_thread_data->read_error = !hfsReadEntryData(hfs_ctx, hfs_entry, buf1, blksize, offset);
            if (shared_thread_data->read_error)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Wait until the previous file data chunk has been written. */
            mutexLock(&g_fileMutex);

            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

            if (shared_thread_data->write_error)
            {
                mutexUnlock(&g_fileMutex);
                break;
            }

            /* Update shared object. */
            shared_thread_data->data = buf1;
            shared_thread_data->data_size = blksize;

            /* Swap buffers. */
            buf1 = buf2;
            buf2 = shared_thread_data->data;

            /* Wake up the write thread to continue writing data. */
            mutexUnlock(&g_fileMutex);
            condvarWakeAll(&g_writeCondvar);
        }

        if (shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) break;
    }

    if (!shared_thread_data->read_error && !shared_thread_data->write_error && !shared_thread_data->transfer_cancelled)
    {
        /* Wait until the previous file data chunk has been written. */
        mutexLock(&g_fileMutex);
        if (shared_thread_data->data_size) condvarWait(&g_readCondvar, &g_fileMutex);
        mutexUnlock(&g_fileMutex);

        if (dev_idx == 1) usbEndExtractedFsDump();

        consolePrint("successfully saved extracted hfs partition data to \"%s\"\n", filename);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if ((shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) && dev_idx != 1)
        {
            utilsDeleteDirectoryRecursively(filename);
            if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
        }
    }

    if (filename) free(filename);

    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void ncaReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    NcaThreadData *nca_thread_data = (NcaThreadData*)arg;
    SharedThreadData *shared_thread_data = &(nca_thread_data->shared_thread_data);
    NcaContext *nca_ctx = nca_thread_data->nca_ctx;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!shared_thread_data->total_size || !nca_ctx || !buf1 || !buf2)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    shared_thread_data->data = NULL;
    shared_thread_data->data_size = 0;

    for(u64 offset = 0, blksize = BLOCK_SIZE; offset < shared_thread_data->total_size; offset += blksize)
    {
        if (blksize > (shared_thread_data->total_size - offset)) blksize = (shared_thread_data->total_size - offset);

        /* Check if the transfer has been cancelled by the user */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Read current data chunk */
        shared_thread_data->read_error = !ncaReadContentFile(nca_ctx, buf1, blksize, offset);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Wait until the previous data chunk has been written */
        mutexLock(&g_fileMutex);

        if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

        if (shared_thread_data->write_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }

        /* Update shared object. */
        shared_thread_data->data = buf1;
        shared_thread_data->data_size = blksize;

        /* Swap buffers. */
        buf1 = buf2;
        buf2 = shared_thread_data->data;

        /* Wake up the write thread to continue writing data. */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_writeCondvar);
    }

end:
    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void rawPartitionFsReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    PfsThreadData *pfs_thread_data = (PfsThreadData*)arg;
    SharedThreadData *shared_thread_data = &(pfs_thread_data->shared_thread_data);
    PartitionFileSystemContext *pfs_ctx = pfs_thread_data->pfs_ctx;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!shared_thread_data->total_size || !pfs_ctx || !buf1 || !buf2)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    shared_thread_data->data = NULL;
    shared_thread_data->data_size = 0;

    for(u64 offset = 0, blksize = BLOCK_SIZE; offset < shared_thread_data->total_size; offset += blksize)
    {
        if (blksize > (shared_thread_data->total_size - offset)) blksize = (shared_thread_data->total_size - offset);

        /* Check if the transfer has been cancelled by the user */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Read current data chunk */
        shared_thread_data->read_error = !pfsReadPartitionData(pfs_ctx, buf1, blksize, offset);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Wait until the previous data chunk has been written */
        mutexLock(&g_fileMutex);

        if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

        if (shared_thread_data->write_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }

        /* Update shared object. */
        shared_thread_data->data = buf1;
        shared_thread_data->data_size = blksize;

        /* Swap buffers. */
        buf1 = buf2;
        buf2 = shared_thread_data->data;

        /* Wake up the write thread to continue writing data. */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_writeCondvar);
    }

end:
    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void extractedPartitionFsReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    PfsThreadData *pfs_thread_data = (PfsThreadData*)arg;
    SharedThreadData *shared_thread_data = &(pfs_thread_data->shared_thread_data);

    PartitionFileSystemContext *pfs_ctx = pfs_thread_data->pfs_ctx;
    u32 pfs_entry_count = pfsGetEntryCount(pfs_ctx);

    char pfs_path[FS_MAX_PATH] = {0}, subdir[0x20] = {0}, *filename = NULL;
    size_t filename_len = 0;

    PartitionFileSystemEntry *pfs_entry = NULL;
    char *pfs_entry_name = NULL;

    NcaFsSectionContext *nca_fs_ctx = pfs_ctx->nca_fs_ctx;
    NcaContext *nca_ctx = nca_fs_ctx->nca_ctx;

    u64 title_id = nca_ctx->title_id;
    u8 title_type = nca_ctx->title_type;

    u64 free_space = 0;
    u32 dev_idx = g_storageMenuElementOption.selected;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (pfs_thread_data->use_layeredfs_dir)
    {
        /* Only use base title IDs if we're dealing with patches. */
        title_id = (title_type == NcmContentMetaType_Patch ? titleGetApplicationIdByPatchId(title_id) : \
                   (title_type == NcmContentMetaType_DataPatch ? titleGetAddOnContentIdByDataPatchId(title_id) : title_id));

        filename = generateOutputLayeredFsFileName(title_id + nca_ctx->id_offset, NULL, "exefs");
    } else {
        snprintf(subdir, MAX_ELEMENTS(subdir), "NCA FS/%s/Extracted", nca_ctx->storage_id == NcmStorageId_BuiltInSystem ? "System" : "User");
        snprintf(pfs_path, MAX_ELEMENTS(pfs_path), "/%s #%u/%u", titleGetNcmContentTypeName(nca_ctx->content_type), nca_ctx->id_offset, nca_fs_ctx->section_idx);

        TitleInfo *title_info = (title_id == g_ncaUserTitleInfo->meta_key.id ? g_ncaUserTitleInfo : g_ncaBasePatchTitleInfo);
        filename = generateOutputTitleFileName(title_info, subdir, pfs_path);
    }

    filename_len = (filename ? strlen(filename) : 0);

    if (!shared_thread_data->total_size || !pfs_entry_count || !buf1 || !buf2 || !filename)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    if (dev_idx != 1)
    {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            shared_thread_data->read_error = true;
        }

        if (!shared_thread_data->read_error && shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            shared_thread_data->read_error = true;
        }
    } else {
        if (!usbStartExtractedFsDump(shared_thread_data->total_size, filename))
        {
            consolePrint("failed to send extracted fs info to host\n");
            shared_thread_data->read_error = true;
        }
    }

    if (shared_thread_data->read_error)
    {
        condvarWakeAll(&g_writeCondvar);
        goto end;
    }

    /* Loop through all file entries. */
    for(u32 i = 0; i < pfs_entry_count; i++)
    {
        /* Check if the transfer has been cancelled by the user. */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        if (dev_idx != 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Close file. */
            if (shared_thread_data->fp)
            {
                fclose(shared_thread_data->fp);
                shared_thread_data->fp = NULL;
                if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
            }
        }

        /* Retrieve Partition FS file entry information. */
        shared_thread_data->read_error = ((pfs_entry = pfsGetEntryByIndex(pfs_ctx, i)) == NULL || (pfs_entry_name = pfsGetEntryName(pfs_ctx, pfs_entry)) == NULL);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Generate output path. */
        snprintf(pfs_path, MAX_ELEMENTS(pfs_path), "%s/%s", filename, pfs_entry_name);
        utilsReplaceIllegalCharacters(pfs_path + filename_len + 1, dev_idx == 0);

        if (dev_idx == 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Send current file properties */
            shared_thread_data->read_error = !usbSendFileProperties(pfs_entry->size, pfs_path);
        } else {
            /* Create directory tree. */
            utilsCreateDirectoryTree(pfs_path, false);

            if (dev_idx == 0)
            {
                /* Create ConcatenationFile if we're dealing with a big file + SD card as the output storage. */
                if (pfs_entry->size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(pfs_path))
                {
                    consolePrint("failed to create concatenation file for \"%s\"!\n", pfs_path);
                    shared_thread_data->read_error = true;
                }
            } else {
                /* Don't handle file chunks on FAT12/FAT16/FAT32 formatted UMS devices. */
                if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && pfs_entry->size > FAT32_FILESIZE_LIMIT)
                {
                    consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                    shared_thread_data->read_error = true;
                }
            }

            if (!shared_thread_data->read_error)
            {
                /* Open output file. */
                shared_thread_data->read_error = ((shared_thread_data->fp = fopen(pfs_path, "wb")) == NULL);
                if (!shared_thread_data->read_error)
                {
                    /* Set file size. */
                    setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
                    ftruncate(fileno(shared_thread_data->fp), (off_t)pfs_entry->size);
                } else {
                    consolePrint("failed to open \"%s\" for writing!\n", pfs_path);
                }
            }
        }

        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        for(u64 offset = 0, blksize = BLOCK_SIZE; offset < pfs_entry->size; offset += blksize)
        {
            if (blksize > (pfs_entry->size - offset)) blksize = (pfs_entry->size - offset);

            /* Check if the transfer has been cancelled by the user. */
            if (shared_thread_data->transfer_cancelled)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Read current file data chunk. */
            shared_thread_data->read_error = !pfsReadEntryData(pfs_ctx, pfs_entry, buf1, blksize, offset);
            if (shared_thread_data->read_error)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Wait until the previous file data chunk has been written. */
            mutexLock(&g_fileMutex);

            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

            if (shared_thread_data->write_error)
            {
                mutexUnlock(&g_fileMutex);
                break;
            }

            /* Update shared object. */
            shared_thread_data->data = buf1;
            shared_thread_data->data_size = blksize;

            /* Swap buffers. */
            buf1 = buf2;
            buf2 = shared_thread_data->data;

            /* Wake up the write thread to continue writing data. */
            mutexUnlock(&g_fileMutex);
            condvarWakeAll(&g_writeCondvar);
        }

        if (shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) break;
    }

    if (!shared_thread_data->read_error && !shared_thread_data->write_error && !shared_thread_data->transfer_cancelled)
    {
        /* Wait until the previous file data chunk has been written. */
        mutexLock(&g_fileMutex);
        if (shared_thread_data->data_size) condvarWait(&g_readCondvar, &g_fileMutex);
        mutexUnlock(&g_fileMutex);

        if (dev_idx == 1) usbEndExtractedFsDump();

        consolePrint("successfully saved extracted partitionfs section data to \"%s\"\n", filename);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if ((shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) && dev_idx != 1)
        {
            utilsDeleteDirectoryRecursively(filename);
            if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
        }
    }

    if (filename) free(filename);

    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void rawRomFsReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    RomFsThreadData *romfs_thread_data = (RomFsThreadData*)arg;
    SharedThreadData *shared_thread_data = &(romfs_thread_data->shared_thread_data);
    RomFileSystemContext *romfs_ctx = romfs_thread_data->romfs_ctx;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!shared_thread_data->total_size || !romfs_ctx || !buf1 || !buf2)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    shared_thread_data->data = NULL;
    shared_thread_data->data_size = 0;

    for(u64 offset = 0, blksize = BLOCK_SIZE; offset < shared_thread_data->total_size; offset += blksize)
    {
        if (blksize > (shared_thread_data->total_size - offset)) blksize = (shared_thread_data->total_size - offset);

        /* Check if the transfer has been cancelled by the user */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Read current data chunk */
        shared_thread_data->read_error = !romfsReadFileSystemData(romfs_ctx, buf1, blksize, offset);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Wait until the previous data chunk has been written */
        mutexLock(&g_fileMutex);

        if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

        if (shared_thread_data->write_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }

        /* Update shared object. */
        shared_thread_data->data = buf1;
        shared_thread_data->data_size = blksize;

        /* Swap buffers. */
        buf1 = buf2;
        buf2 = shared_thread_data->data;

        /* Wake up the write thread to continue writing data. */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_writeCondvar);
    }

end:
    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void extractedRomFsReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    RomFsThreadData *romfs_thread_data = (RomFsThreadData*)arg;
    SharedThreadData *shared_thread_data = &(romfs_thread_data->shared_thread_data);

    RomFileSystemContext *romfs_ctx = romfs_thread_data->romfs_ctx;
    RomFileSystemFileEntry *romfs_file_entry = NULL;
    u64 cur_entry_offset = 0;

    char romfs_path[FS_MAX_PATH] = {0}, subdir[0x20] = {0}, *filename = NULL;
    size_t filename_len = 0;

    NcaFsSectionContext *nca_fs_ctx = romfs_ctx->default_storage_ctx->nca_fs_ctx;
    NcaContext *nca_ctx = nca_fs_ctx->nca_ctx;

    u64 title_id = nca_ctx->title_id;
    u8 title_type = nca_ctx->title_type;

    u64 free_space = 0;
    u32 dev_idx = g_storageMenuElementOption.selected;
    u8 romfs_illegal_char_replace_type = (dev_idx != 0 ? RomFileSystemPathIllegalCharReplaceType_IllegalFsChars : RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly);

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (romfs_thread_data->use_layeredfs_dir)
    {
        /* Only use base title IDs if we're dealing with patches. */
        title_id = (title_type == NcmContentMetaType_Patch ? titleGetApplicationIdByPatchId(title_id) : \
                   (title_type == NcmContentMetaType_DataPatch ? titleGetAddOnContentIdByDataPatchId(title_id) : title_id));

        filename = generateOutputLayeredFsFileName(title_id + nca_ctx->id_offset, NULL, "romfs");
    } else {
        snprintf(subdir, MAX_ELEMENTS(subdir), "NCA FS/%s/Extracted", nca_ctx->storage_id == NcmStorageId_BuiltInSystem ? "System" : "User");
        snprintf(romfs_path, MAX_ELEMENTS(romfs_path), "/%s #%u/%u", titleGetNcmContentTypeName(nca_ctx->content_type), nca_ctx->id_offset, nca_fs_ctx->section_idx);

        TitleInfo *title_info = (title_id == g_ncaUserTitleInfo->meta_key.id ? g_ncaUserTitleInfo : g_ncaBasePatchTitleInfo);
        filename = generateOutputTitleFileName(title_info, subdir, romfs_path);
    }

    filename_len = (filename ? strlen(filename) : 0);

    if (!shared_thread_data->total_size || !buf1 || !buf2 || !filename)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    snprintf(romfs_path, MAX_ELEMENTS(romfs_path), "%s", filename);

    if (dev_idx != 1)
    {
        if (!utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            shared_thread_data->read_error = true;
        }

        if (!shared_thread_data->read_error && shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            shared_thread_data->read_error = true;
        }
    } else {
        if (!usbStartExtractedFsDump(shared_thread_data->total_size, filename))
        {
            consolePrint("failed to send extracted fs info to host\n");
            shared_thread_data->read_error = true;
        }
    }

    if (shared_thread_data->read_error)
    {
        condvarWakeAll(&g_writeCondvar);
        goto end;
    }

    /* Loop through all file entries. */
    while(shared_thread_data->data_written < shared_thread_data->total_size && cur_entry_offset < romfs_ctx->file_table_size)
    {
        /* Check if the transfer has been cancelled by the user. */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        if (dev_idx != 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Close file. */
            if (shared_thread_data->fp)
            {
                fclose(shared_thread_data->fp);
                shared_thread_data->fp = NULL;
                if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
            }
        }

        /* Retrieve RomFS file entry information and generate output path. */
        shared_thread_data->read_error = (!(romfs_file_entry = romfsGetFileEntryByOffset(romfs_ctx, cur_entry_offset)) || \
                                           !romfsGeneratePathFromFileEntry(romfs_ctx, romfs_file_entry, romfs_path + filename_len, sizeof(romfs_path) - filename_len, romfs_illegal_char_replace_type));
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        if (dev_idx == 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Send current file properties */
            shared_thread_data->read_error = !usbSendFileProperties(romfs_file_entry->size, romfs_path);
        } else {
            /* Create directory tree. */
            utilsCreateDirectoryTree(romfs_path, false);

            if (dev_idx == 0)
            {
                /* Create ConcatenationFile if we're dealing with a big file + SD card as the output storage. */
                if (romfs_file_entry->size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(romfs_path))
                {
                    consolePrint("failed to create concatenation file for \"%s\"!\n", romfs_path);
                    shared_thread_data->read_error = true;
                }
            } else {
                /* Don't handle file chunks on FAT12/FAT16/FAT32 formatted UMS devices. */
                if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && romfs_file_entry->size > FAT32_FILESIZE_LIMIT)
                {
                    consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                    shared_thread_data->read_error = true;
                }
            }

            if (!shared_thread_data->read_error)
            {
                /* Open output file. */
                shared_thread_data->read_error = ((shared_thread_data->fp = fopen(romfs_path, "wb")) == NULL);
                if (!shared_thread_data->read_error)
                {
                    /* Set file size. */
                    setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
                    ftruncate(fileno(shared_thread_data->fp), (off_t)romfs_file_entry->size);
                } else {
                    consolePrint("failed to open \"%s\" for writing!\n", romfs_path);
                }
            }
        }

        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        for(u64 offset = 0, blksize = BLOCK_SIZE; offset < romfs_file_entry->size; offset += blksize)
        {
            if (blksize > (romfs_file_entry->size - offset)) blksize = (romfs_file_entry->size - offset);

            /* Check if the transfer has been cancelled by the user. */
            if (shared_thread_data->transfer_cancelled)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Read current file data chunk. */
            shared_thread_data->read_error = !romfsReadFileEntryData(romfs_ctx, romfs_file_entry, buf1, blksize, offset);
            if (shared_thread_data->read_error)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Wait until the previous file data chunk has been written. */
            mutexLock(&g_fileMutex);

            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

            if (shared_thread_data->write_error)
            {
                mutexUnlock(&g_fileMutex);
                break;
            }

            /* Update shared object. */
            shared_thread_data->data = buf1;
            shared_thread_data->data_size = blksize;

            /* Swap buffers. */
            buf1 = buf2;
            buf2 = shared_thread_data->data;

            /* Wake up the write thread to continue writing data. */
            mutexUnlock(&g_fileMutex);
            condvarWakeAll(&g_writeCondvar);
        }

        if (shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) break;

        /* Get the offset for the next file entry. */
        cur_entry_offset += ALIGN_UP(sizeof(RomFileSystemFileEntry) + romfs_file_entry->name_length, ROMFS_TABLE_ENTRY_ALIGNMENT);
    }

    if (!shared_thread_data->read_error && !shared_thread_data->write_error && !shared_thread_data->transfer_cancelled)
    {
        /* Wait until the previous file data chunk has been written. */
        mutexLock(&g_fileMutex);
        if (shared_thread_data->data_size) condvarWait(&g_readCondvar, &g_fileMutex);
        mutexUnlock(&g_fileMutex);

        if (dev_idx == 1) usbEndExtractedFsDump();

        consolePrint("successfully saved extracted romfs section data to \"%s\"\n", filename);
        consoleRefresh();
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if ((shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) && dev_idx != 1)
        {
            utilsDeleteDirectoryRecursively(filename);
            if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
        }
    }

    if (filename) free(filename);

    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void fsBrowserFileReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    FsBrowserFileThreadData *fs_browser_thread_data = (FsBrowserFileThreadData*)arg;
    SharedThreadData *shared_thread_data = &(fs_browser_thread_data->shared_thread_data);
    FILE *src = fs_browser_thread_data->src;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!shared_thread_data->total_size || !src || !buf1 || !buf2)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    shared_thread_data->data = NULL;
    shared_thread_data->data_size = 0;

    for(u64 offset = 0, blksize = BLOCK_SIZE; offset < shared_thread_data->total_size; offset += blksize)
    {
        if (blksize > (shared_thread_data->total_size - offset)) blksize = (shared_thread_data->total_size - offset);

        /* Check if the transfer has been cancelled by the user */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Read current data chunk */
        shared_thread_data->read_error = (fread(buf1, 1, blksize, src) != blksize);
        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Wait until the previous data chunk has been written */
        mutexLock(&g_fileMutex);

        if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

        if (shared_thread_data->write_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }

        /* Update shared object. */
        shared_thread_data->data = buf1;
        shared_thread_data->data_size = blksize;

        /* Swap buffers. */
        buf1 = buf2;
        buf2 = shared_thread_data->data;

        /* Wake up the write thread to continue writing data. */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_writeCondvar);
    }

end:
    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static void fsBrowserHighlightedEntriesReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    FsBrowserHighlightedEntriesThreadData *fs_browser_thread_data = (FsBrowserHighlightedEntriesThreadData*)arg;
    SharedThreadData *shared_thread_data = &(fs_browser_thread_data->shared_thread_data);

    const char *dir_path = fs_browser_thread_data->dir_path;
    const FsBrowserEntry *entries = fs_browser_thread_data->entries;
    u32 entries_count = fs_browser_thread_data->entries_count;
    const char *base_out_path = fs_browser_thread_data->base_out_path;

    u32 dev_idx = g_storageMenuElementOption.selected;

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!shared_thread_data->total_size || !dir_path || !*dir_path || !entries || !entries_count || !base_out_path || !*base_out_path || !buf1 || !buf2)
    {
        shared_thread_data->read_error = true;
        goto end;
    }

    if (dev_idx != 1)
    {
        u64 free_space = 0;

        if (!utilsGetFileSystemStatsByPath(base_out_path, NULL, &free_space))
        {
            consolePrint("failed to retrieve free space from selected device\n");
            shared_thread_data->read_error = true;
        }

        if (!shared_thread_data->read_error && shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            shared_thread_data->read_error = true;
        }
    } else {
        if (!usbStartExtractedFsDump(shared_thread_data->total_size, base_out_path))
        {
            consolePrint("failed to send extracted fs info to host\n");
            shared_thread_data->read_error = true;
        }
    }

    if (!shared_thread_data->read_error)
    {
        /* Dump highlighted entries. */
        fsBrowserHighlightedEntriesReadThreadLoop(shared_thread_data, dir_path, entries, entries_count, base_out_path, buf1, buf2);

        if (!shared_thread_data->read_error && !shared_thread_data->write_error && !shared_thread_data->transfer_cancelled)
        {
            if (dev_idx == 1) usbEndExtractedFsDump();

            consolePrint("successfully saved dumped data to \"%s\"\n", base_out_path);
            consoleRefresh();
        }
    } else {
        condvarWakeAll(&g_writeCondvar);
    }

end:
    if (buf2) free(buf2);
    if (buf1) free(buf1);

    threadExit();
}

static bool fsBrowserHighlightedEntriesReadThreadLoop(SharedThreadData *shared_thread_data, const char *dir_path, const FsBrowserEntry *entries, u32 entries_count, const char *base_out_path, void *buf1, void *buf2)
{
    bool append_path_sep = (dir_path[strlen(dir_path) - 1] != '/');
    u32 dev_idx = g_storageMenuElementOption.selected;
    bool is_topmost = (entries && entries_count); /* If entry data is provided, it means we're dealing with the topmost directory. */
    const char *dir_path_start = (strchr(dir_path, '/') + 1);

    char *tmp_path = NULL;
    FILE *src = NULL;

    /* Allocate memory for our temporary path. */
    tmp_path = calloc(sizeof(char), FS_MAX_PATH);
    if ((shared_thread_data->read_error = (tmp_path == NULL)))
    {
        consolePrint("failed to allocate memory for path!\n");
        condvarWakeAll(&g_writeCondvar);
        goto end;
    }

    /* Get directory entries, if needed. */
    if (!is_topmost && (shared_thread_data->read_error = !fsBrowserGetDirEntries(dir_path, (FsBrowserEntry**)&entries, &entries_count)))
    {
        condvarWakeAll(&g_writeCondvar);
        goto end;
    }

    /* Loop through all highlighted entries. */
    for(u32 i = 0; i < entries_count; i++)
    {
        /* Get current entry. */
        const FsBrowserEntry *entry = &(entries[i]);
        if (is_topmost && !entry->highlight) continue;

        /* Check if the transfer has been cancelled by the user. */
        if (shared_thread_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        if (dev_idx != 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Close file. */
            if (shared_thread_data->fp)
            {
                fclose(shared_thread_data->fp);
                shared_thread_data->fp = NULL;
                if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
            }
        }

        /* Generate input path. */
        snprintf(tmp_path, FS_MAX_PATH, "%s%s%s", dir_path, append_path_sep ? "/" : "", entry->dt.d_name);

        if (entry->dt.d_type == DT_DIR)
        {
            /* Dump directory. */
            if (!fsBrowserHighlightedEntriesReadThreadLoop(shared_thread_data, tmp_path, NULL, 0, base_out_path, buf1, buf2)) break;
            continue;
        }

        /* Open input file. */
        src = fopen(tmp_path, "rb");
        if ((shared_thread_data->read_error = (src == NULL)))
        {
            consolePrint("failed to open file \"%s\" for reading!\n", tmp_path);
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        setvbuf(src, NULL, _IONBF, 0);

        /* Generate output path. */
        if (*dir_path_start)
        {
            snprintf(tmp_path, FS_MAX_PATH, "%s/%s/%s", base_out_path, dir_path_start, entry->dt.d_name);
        } else {
            snprintf(tmp_path, FS_MAX_PATH, "%s/%s", base_out_path, entry->dt.d_name);
        }

        if (dev_idx == 1)
        {
            /* Wait until the previous data chunk has been written */
            mutexLock(&g_fileMutex);
            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
            mutexUnlock(&g_fileMutex);

            if (shared_thread_data->write_error) break;

            /* Send current file properties */
            shared_thread_data->read_error = !usbSendFileProperties(entry->size, tmp_path);
        } else {
            /* Create directory tree. */
            utilsCreateDirectoryTree(tmp_path, false);

            if (dev_idx == 0)
            {
                /* Create ConcatenationFile if we're dealing with a big file + SD card as the output storage. */
                if (entry->size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(tmp_path))
                {
                    consolePrint("failed to create concatenation file for \"%s\"!\n", tmp_path);
                    shared_thread_data->read_error = true;
                }
            } else {
                /* Don't handle file chunks on FAT12/FAT16/FAT32 formatted UMS devices. */
                if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && entry->size > FAT32_FILESIZE_LIMIT)
                {
                    consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                    shared_thread_data->read_error = true;
                }
            }

            if (!shared_thread_data->read_error)
            {
                /* Open output file. */
                shared_thread_data->read_error = ((shared_thread_data->fp = fopen(tmp_path, "wb")) == NULL);
                if (!shared_thread_data->read_error)
                {
                    /* Set file size. */
                    setvbuf(shared_thread_data->fp, NULL, _IONBF, 0);
                    ftruncate(fileno(shared_thread_data->fp), (off_t)entry->size);
                } else {
                    consolePrint("failed to open \"%s\" for writing!\n", tmp_path);
                }
            }
        }

        if (shared_thread_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Dump file. */
        for(u64 offset = 0, blksize = BLOCK_SIZE; offset < entry->size; offset += blksize)
        {
            if (blksize > (entry->size - offset)) blksize = (entry->size - offset);

            /* Check if the transfer has been cancelled by the user. */
            if (shared_thread_data->transfer_cancelled)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Read current file data chunk. */
            shared_thread_data->read_error = (fread(buf1, 1, blksize, src) != blksize);
            if (shared_thread_data->read_error)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Wait until the previous file data chunk has been written. */
            mutexLock(&g_fileMutex);

            if (shared_thread_data->data_size && !shared_thread_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

            if (shared_thread_data->write_error)
            {
                mutexUnlock(&g_fileMutex);
                break;
            }

            /* Update shared object. */
            shared_thread_data->data = buf1;
            shared_thread_data->data_size = blksize;

            /* Swap buffers. */
            buf1 = buf2;
            buf2 = shared_thread_data->data;

            /* Wake up the write thread to continue writing data. */
            mutexUnlock(&g_fileMutex);
            condvarWakeAll(&g_writeCondvar);
        }

        /* Close input file. */
        fclose(src);
        src = NULL;

        if (shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) break;
    }

    if (!shared_thread_data->read_error && !shared_thread_data->write_error && !shared_thread_data->transfer_cancelled)
    {
        /* Wait until the previous file data chunk has been written. */
        mutexLock(&g_fileMutex);
        if (shared_thread_data->data_size) condvarWait(&g_readCondvar, &g_fileMutex);
        mutexUnlock(&g_fileMutex);
    }

end:
    if (shared_thread_data->fp)
    {
        fclose(shared_thread_data->fp);
        shared_thread_data->fp = NULL;

        if ((shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) && dev_idx != 1)
        {
            utilsDeleteDirectoryRecursively(base_out_path);
            if (dev_idx == 0) utilsCommitSdCardFileSystemChanges();
        }
    }

    if (src) fclose(src);

    if (!is_topmost && entries) free((FsBrowserEntry*)entries);

    if (tmp_path) free(tmp_path);

    return !shared_thread_data->read_error;
}

static void genericWriteThreadFunc(void *arg)
{
    SharedThreadData *shared_thread_data = (SharedThreadData*)arg; // UB but we don't care

    while(shared_thread_data->data_written < shared_thread_data->total_size)
    {
        /* Wait until the current file data chunk has been read */
        mutexLock(&g_fileMutex);

        if (!shared_thread_data->data_size && !shared_thread_data->read_error) condvarWait(&g_writeCondvar, &g_fileMutex);

        if (shared_thread_data->read_error || shared_thread_data->transfer_cancelled || (!useUsbHost() && !shared_thread_data->fp))
        {
            if (useUsbHost() && shared_thread_data->transfer_cancelled) usbCancelFileTransfer();
            mutexUnlock(&g_fileMutex);
            break;
        }

        /* Write current file data chunk */
        if (useUsbHost())
        {
            shared_thread_data->write_error = !usbSendFileData(shared_thread_data->data, shared_thread_data->data_size);
        } else {
            shared_thread_data->write_error = (fwrite(shared_thread_data->data, 1, shared_thread_data->data_size, shared_thread_data->fp) != shared_thread_data->data_size);
        }

        if (!shared_thread_data->write_error)
        {
            shared_thread_data->data_written += shared_thread_data->data_size;
            shared_thread_data->data_size = 0;
        }

        /* Wake up the read thread to continue reading data */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_readCondvar);

        if (shared_thread_data->write_error) break;
    }

    threadExit();
}

static bool spanDumpThreads(ThreadFunc read_func, ThreadFunc write_func, void *arg)
{
    SharedThreadData *shared_thread_data = (SharedThreadData*)arg; // UB but we don't care
    Thread read_thread = {0}, write_thread = {0};

    time_t start = 0, btn_cancel_start_tmr = 0, btn_cancel_end_tmr = 0;
    bool btn_cancel_cur_state = false, btn_cancel_prev_state = false, success = false;

    u64 prev_size = 0;
    u8 prev_time = 0, percent = 0;

    consolePrint("creating threads\n");
    utilsCreateThread(&read_thread, read_func, arg, 2);
    utilsCreateThread(&write_thread, write_func, arg, 2);

    consolePrint("hold b to cancel\n\n");
    consoleRefresh();

    start = time(NULL);

    while(shared_thread_data->data_written < shared_thread_data->total_size)
    {
        g_appletStatus = appletMainLoop();
        if (!g_appletStatus)
        {
            mutexLock(&g_fileMutex);
            shared_thread_data->transfer_cancelled = true;
            mutexUnlock(&g_fileMutex);
        }

        if (shared_thread_data->read_error || shared_thread_data->write_error || shared_thread_data->transfer_cancelled) break;

        struct tm ts = {0};
        time_t now = time(NULL);
        localtime_r(&now, &ts);

        size_t size = shared_thread_data->data_written;

        utilsScanPads();
        btn_cancel_cur_state = (utilsGetButtonsHeld() & HidNpadButton_B);

        if (btn_cancel_cur_state && btn_cancel_cur_state != btn_cancel_prev_state)
        {
            btn_cancel_start_tmr = now;
        } else
        if (btn_cancel_cur_state && btn_cancel_cur_state == btn_cancel_prev_state)
        {
            btn_cancel_end_tmr = now;
            if ((btn_cancel_end_tmr - btn_cancel_start_tmr) >= 3)
            {
                mutexLock(&g_fileMutex);
                shared_thread_data->transfer_cancelled = true;
                mutexUnlock(&g_fileMutex);
                break;
            }
        } else {
            btn_cancel_start_tmr = btn_cancel_end_tmr = 0;
        }

        btn_cancel_prev_state = btn_cancel_cur_state;

        if (prev_time == ts.tm_sec || prev_size == size) continue;

        percent = (u8)((size * 100) / shared_thread_data->total_size);

        prev_time = ts.tm_sec;
        prev_size = size;

        consolePrint("%lu / %lu (%u%%) | Time elapsed: %lu\n", size, shared_thread_data->total_size, percent, (now - start));
        consoleRefresh();

        utilsAppletLoopDelay();
    }

    consolePrint("\nwaiting for threads to join\n");
    consoleRefresh();

    utilsJoinThread(&read_thread);
    consolePrint("read_thread done: %lu\n", time(NULL));

    utilsJoinThread(&write_thread);
    consolePrint("write_thread done: %lu\n", time(NULL));

    if (shared_thread_data->read_error || shared_thread_data->write_error)
    {
        consolePrint("i/o error\n");
    } else
    if (shared_thread_data->transfer_cancelled)
    {
        consolePrint("process cancelled\n");
    } else {
        start = (time(NULL) - start);
        consolePrint("process completed in %lu seconds\n", start);
        success = true;
    }

    consoleRefresh();

    return success;
}

static void nspThreadFunc(void *arg)
{
    NspThreadData *nsp_thread_data = (NspThreadData*)arg;

    TitleInfo *title_info = NULL;

    bool set_download_type = (bool)getNspSetDownloadDistributionOption();
    bool remove_console_data = (bool)getNspRemoveConsoleDataOption();
    bool remove_titlekey_crypto = (bool)getNspRemoveTitlekeyCryptoOption();
    bool patch_sua = (bool)getNspDisableLinkedAccountRequirementOption();
    bool patch_screenshot = (bool)getNspEnableScreenshotsOption();
    bool patch_video_capture = (bool)getNspEnableVideoCaptureOption();
    bool patch_hdcp = (bool)getNspDisableHdcpOption();
    bool generate_authoringtool_data = (bool)getNspGenerateAuthoringToolDataOption();
    bool success = false, no_titlekey_confirmation = false;

    u64 free_space = 0;
    u32 dev_idx = g_storageMenuElementOption.selected;

    u8 *buf = NULL;
    char *filename = NULL;
    FILE *fp = NULL;

    NcaContext *nca_ctx = NULL;

    NcaContext *meta_nca_ctx = NULL;
    ContentMetaContext cnmt_ctx = {0};

    ProgramInfoContext *program_info_ctx = NULL;
    u32 program_idx = 0, program_count = 0;

    NacpContext *nacp_ctx = NULL;
    u32 control_idx = 0, control_count = 0;

    LegalInfoContext *legal_info_ctx = NULL;
    u32 legal_info_idx = 0, legal_info_count = 0;

    Ticket tik = {0};
    TikCommonBlock *tik_common_block = NULL;

    u8 *raw_cert_chain = NULL;
    u64 raw_cert_chain_size = 0;

    PartitionFileSystemImageContext pfs_img_ctx = {0};
    pfsInitializeImageContext(&pfs_img_ctx);

    char entry_name[64] = {0};
    u64 nsp_header_size = 0, nsp_size = 0, nsp_offset = 0;
    char *tmp_name = NULL;

    Sha256Context clean_sha256_ctx = {0}, dirty_sha256_ctx = {0};
    u8 clean_sha256_hash[SHA256_HASH_SIZE] = {0}, dirty_sha256_hash[SHA256_HASH_SIZE] = {0};

    if (!nsp_thread_data || !(title_info = (TitleInfo*)nsp_thread_data->data) || !title_info->content_count || !title_info->content_infos) goto end;

    /* Allocate memory for the dump process. */
    if (!(buf = usbAllocatePageAlignedBuffer(BLOCK_SIZE)))
    {
        consolePrint("buf alloc failed\n");
        goto end;
    }

    /* Generate output path. */
    filename = generateOutputTitleFileName(title_info, "NSP", ".nsp");
    if (!filename) goto end;

    /* Get free space on output storage. */
    if (dev_idx != 1 && !utilsGetFileSystemStatsByPath(filename, NULL, &free_space))
    {
        consolePrint("failed to retrieve free space from selected device\n");
        goto end;
    }

    if (!(nca_ctx = calloc(title_info->content_count, sizeof(NcaContext))))
    {
        consolePrint("nca ctx calloc failed\n");
        goto end;
    }

    // determine if we should initialize programinfo ctx
    if (generate_authoringtool_data)
    {
        program_count = titleGetContentCountByType(title_info, NcmContentType_Program);
        if (program_count && !(program_info_ctx = calloc(program_count, sizeof(ProgramInfoContext))))
        {
            consolePrint("program info ctx calloc failed\n");
            goto end;
        }
    }

    // determine if we should initialize nacp ctx
    if (patch_sua || patch_screenshot || patch_video_capture || patch_hdcp || generate_authoringtool_data)
    {
        control_count = titleGetContentCountByType(title_info, NcmContentType_Control);
        if (control_count && !(nacp_ctx = calloc(control_count, sizeof(NacpContext))))
        {
            consolePrint("nacp ctx calloc failed\n");
            goto end;
        }
    }

    // determine if we should initialize legalinfo ctx
    if (generate_authoringtool_data)
    {
        legal_info_count = titleGetContentCountByType(title_info, NcmContentType_LegalInformation);
        if (legal_info_count && !(legal_info_ctx = calloc(legal_info_count, sizeof(LegalInfoContext))))
        {
            consolePrint("legal info ctx calloc failed\n");
            goto end;
        }
    }

    // set meta nca as the last nca
    meta_nca_ctx = &(nca_ctx[title_info->content_count - 1]);

    if (!ncaInitializeContext(meta_nca_ctx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0), \
                              &(title_info->meta_key), titleGetContentInfoByTypeAndIdOffset(title_info, NcmContentType_Meta, 0), &tik))
    {
        consolePrint("meta nca initialize ctx failed\n");
        goto end;
    }

    consolePrint("meta nca initialize ctx succeeded\n");

    if (!cnmtInitializeContext(&cnmt_ctx, meta_nca_ctx))
    {
        consolePrint("cnmt initialize ctx failed\n");
        goto end;
    }

    consolePrint("cnmt initialize ctx succeeded (%s)\n", meta_nca_ctx->content_id_str);

    // initialize nca context
    // initialize content type context
    // generate nca patches (if needed)
    // generate content type xml
    for(u32 i = 0, j = 0; i < title_info->content_count; i++)
    {
        // skip meta nca since we already initialized it
        NcmContentInfo *content_info = &(title_info->content_infos[i]);
        if (content_info->content_type == NcmContentType_Meta) continue;

        NcaContext *cur_nca_ctx = &(nca_ctx[j]);
        if (!ncaInitializeContext(cur_nca_ctx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0), \
                                  &(title_info->meta_key), content_info, &tik))
        {
            consolePrint("%s #%u initialize nca ctx failed\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
            goto end;
        }

        consolePrint("%s #%u initialize nca ctx succeeded\n", titleGetNcmContentTypeName(cur_nca_ctx->content_type), cur_nca_ctx->id_offset);

        // don't go any further with this nca if we can't access its fs data because it's pointless
        if (cur_nca_ctx->rights_id_available && !cur_nca_ctx->titlekey_retrieved && !no_titlekey_confirmation)
        {
            consolePrintReversedColors("\nunable to retrieve titlekey for the selected title");
            consolePrintReversedColors("\nif you proceed, nca modifications will be disabled, and content decryption");
            consolePrintReversedColors("\nwill not be possible for external tools (e.g. emulators, etc.)\n");

            consolePrintReversedColors("\nthis may occur because of different reasons:\n");

            consolePrintReversedColors("\n1. you haven't launched this game/dlc at least once since you downloaded it");
            consolePrintReversedColors("\n2. this is a shared game/dlc across different switch consoles using the");
            consolePrintReversedColors("\n   same nintendo account and you're using the secondary console");
            consolePrintReversedColors("\n3. you downloaded this game/dlc onto your sd card using your sysmmc, then");
            consolePrintReversedColors("\n   copied the 'nintendo' folder data into the 'emummc' folder (or viceversa)\n");

            consolePrintReversedColors("\ncases 1 and 2 can be fixed by exiting nxdumptool, launching the game");
            consolePrintReversedColors("\nand then running nxdumptool once again\n");

            consolePrintReversedColors("\ncase 3 can be fixed by running nxdumptool directly under the emmc that was");
            consolePrintReversedColors("\nused to download the game/dlc\n");

            consolePrintReversedColors("\npress a to proceed anyway, or b to cancel\n\n");

            u64 btn_down = utilsWaitForButtonPress(HidNpadButton_A | HidNpadButton_B);
            if (btn_down & HidNpadButton_A)
            {
                j++;
                no_titlekey_confirmation = true;
                continue;
            }

            goto end;
        }

        // set download distribution type
        // has no effect if this nca uses NcaDistributionType_Download
        if (set_download_type) ncaSetDownloadDistributionType(cur_nca_ctx);

        // remove titlekey crypto
        // has no effect if this nca doesn't use titlekey crypto
        if (remove_titlekey_crypto && !ncaRemoveTitleKeyCrypto(cur_nca_ctx))
        {
            consolePrint("nca remove titlekey crypto failed\n");
            goto end;
        }

        if (!cur_nca_ctx->fs_ctx[0].has_sparse_layer)
        {
            switch(content_info->content_type)
            {
                case NcmContentType_Program:
                {
                    // don't proceed if we didn't allocate programinfo ctx
                    if (!program_count || !program_info_ctx) break;

                    ProgramInfoContext *cur_program_info_ctx = &(program_info_ctx[program_idx]);

                    if (!programInfoInitializeContext(cur_program_info_ctx, cur_nca_ctx))
                    {
                        consolePrint("initialize program info ctx failed (%s)\n", cur_nca_ctx->content_id_str);
                        goto end;
                    }

                    if (!programInfoGenerateAuthoringToolXml(cur_program_info_ctx))
                    {
                        consolePrint("program info xml failed (%s)\n", cur_nca_ctx->content_id_str);
                        goto end;
                    }

                    program_idx++;

                    consolePrint("initialize program info ctx succeeded (%s)\n", cur_nca_ctx->content_id_str);

                    break;
                }
                case NcmContentType_Control:
                {
                    // don't proceed if we didn't allocate nacp ctx
                    if (!control_count || !nacp_ctx) break;

                    NacpContext *cur_nacp_ctx = &(nacp_ctx[control_idx]);

                    if (!nacpInitializeContext(cur_nacp_ctx, cur_nca_ctx))
                    {
                        consolePrint("initialize nacp ctx failed (%s)\n", cur_nca_ctx->content_id_str);
                        goto end;
                    }

                    if (!nacpGenerateNcaPatch(cur_nacp_ctx, patch_sua, patch_screenshot, patch_video_capture, patch_hdcp))
                    {
                        consolePrint("nacp nca patch failed (%s)\n", cur_nca_ctx->content_id_str);
                        goto end;
                    }

                    if (generate_authoringtool_data && !nacpGenerateAuthoringToolXml(cur_nacp_ctx, title_info->version.value, cnmtGetRequiredTitleVersion(&cnmt_ctx)))
                    {
                        consolePrint("nacp xml failed (%s)\n", cur_nca_ctx->content_id_str);
                        goto end;
                    }

                    control_idx++;

                    consolePrint("initialize nacp ctx succeeded (%s)\n", cur_nca_ctx->content_id_str);

                    break;
                }
                case NcmContentType_LegalInformation:
                {
                    // don't proceed if we didn't allocate legalinfo ctx
                    if (!legal_info_count || !legal_info_ctx) break;

                    LegalInfoContext *cur_legal_info_ctx = &(legal_info_ctx[legal_info_idx]);

                    if (!legalInfoInitializeContext(cur_legal_info_ctx, cur_nca_ctx))
                    {
                        consolePrint("initialize legal info ctx failed (%s)\n", cur_nca_ctx->content_id_str);
                        goto end;
                    }

                    legal_info_idx++;

                    consolePrint("initialize legal info ctx succeeded (%s)\n", cur_nca_ctx->content_id_str);

                    break;
                }
                default:
                    break;
            }
        }

        if (!ncaEncryptHeader(cur_nca_ctx))
        {
            consolePrint("%s #%u encrypt nca header failed\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
            goto end;
        }

        j++;
    }

    consoleRefresh();

    // generate cnmt xml right away even though we don't yet have all the data we need
    // This is because we need its size to calculate the full nsp size
    if (generate_authoringtool_data && !cnmtGenerateAuthoringToolXml(&cnmt_ctx, nca_ctx, title_info->content_count))
    {
        consolePrint("cnmt xml #1 failed\n");
        goto end;
    }

    bool retrieve_tik_cert = (!remove_titlekey_crypto && tikIsValidTicket(&tik));
    if (retrieve_tik_cert)
    {
        if (!(tik_common_block = tikGetCommonBlockFromTicket(&tik)))
        {
            consolePrint("tik common block failed");
            goto end;
        }

        if (remove_console_data && tik_common_block->titlekey_type == TikTitleKeyType_Personalized)
        {
            if (!tikConvertPersonalizedTicketToCommonTicket(&tik, &raw_cert_chain, &raw_cert_chain_size))
            {
                consolePrint("tik convert failed\n");
                goto end;
            }
        } else {
            raw_cert_chain = (title_info->storage_id == NcmStorageId_GameCard ? certRetrieveRawCertificateChainFromGameCardByRightsId(&(tik_common_block->rights_id), &raw_cert_chain_size) : \
                                                                                certGenerateRawCertificateChainBySignatureIssuer(tik_common_block->issuer, &raw_cert_chain_size));
            if (!raw_cert_chain)
            {
                consolePrint("cert failed\n");
                goto end;
            }
        }
    }

    // add nca info
    for(u32 i = 0; i < title_info->content_count; i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        sprintf(entry_name, "%s.%s", cur_nca_ctx->content_id_str, cur_nca_ctx->content_type == NcmContentType_Meta ? "cnmt.nca" : "nca");

        if (!pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, cur_nca_ctx->content_size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }

    // add cnmt xml info
    if (generate_authoringtool_data)
    {
        sprintf(entry_name, "%s.cnmt.xml", meta_nca_ctx->content_id_str);
        if (!pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, cnmt_ctx.authoring_tool_xml_size, &(meta_nca_ctx->content_type_ctx_data_idx)))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }

    // add content type ctx data info
    u32 limit = generate_authoringtool_data ? (title_info->content_count - 1) : 0;
    for(u32 i = 0; i < limit; i++)
    {
        bool ret = false;
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        if (!cur_nca_ctx->content_type_ctx) continue;

        switch(cur_nca_ctx->content_type)
        {
            case NcmContentType_Program:
            {
                ProgramInfoContext *cur_program_info_ctx = (ProgramInfoContext*)cur_nca_ctx->content_type_ctx;
                sprintf(entry_name, "%s.programinfo.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, cur_program_info_ctx->authoring_tool_xml_size, &(cur_nca_ctx->content_type_ctx_data_idx));
                break;
            }
            case NcmContentType_Control:
            {
                NacpContext *cur_nacp_ctx = (NacpContext*)cur_nca_ctx->content_type_ctx;

                for(u8 j = 0; j < cur_nacp_ctx->icon_count; j++)
                {
                    NacpIconContext *icon_ctx = &(cur_nacp_ctx->icon_ctx[j]);
                    sprintf(entry_name, "%s.nx.%s.jpg", cur_nca_ctx->content_id_str, nacpGetLanguageString(icon_ctx->language));
                    if (!pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, icon_ctx->icon_size, j == 0 ? &(cur_nca_ctx->content_type_ctx_data_idx) : NULL))
                    {
                        consolePrint("pfs add entry failed: %s\n", entry_name);
                        goto end;
                    }
                }

                sprintf(entry_name, "%s.nacp.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, cur_nacp_ctx->authoring_tool_xml_size, !cur_nacp_ctx->icon_count ? &(cur_nca_ctx->content_type_ctx_data_idx) : NULL);
                break;
            }
            case NcmContentType_LegalInformation:
            {
                LegalInfoContext *cur_legal_info_ctx = (LegalInfoContext*)cur_nca_ctx->content_type_ctx;
                sprintf(entry_name, "%s.legalinfo.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, cur_legal_info_ctx->authoring_tool_xml_size, &(cur_nca_ctx->content_type_ctx_data_idx));
                break;
            }
            default:
                break;
        }

        if (!ret)
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }

    // add ticket and cert info
    if (retrieve_tik_cert)
    {
        sprintf(entry_name, "%s.tik", tik.rights_id_str);
        if (!pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, tik.size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }

        sprintf(entry_name, "%s.cert", tik.rights_id_str);
        if (!pfsAddEntryInformationToImageContext(&pfs_img_ctx, entry_name, raw_cert_chain_size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }

    // write pfs header to memory buffer
    if (!pfsWriteImageContextHeaderToMemoryBuffer(&pfs_img_ctx, buf, BLOCK_SIZE, &nsp_header_size))
    {
        consolePrint("pfs write header to mem #1 failed\n");
        goto end;
    }

    nsp_size = (nsp_header_size + pfs_img_ctx.fs_size);
    consolePrint("nsp header size: 0x%lX | nsp size: 0x%lX\n", nsp_header_size, nsp_size);
    consoleRefresh();

    if (dev_idx == 1)
    {
        if (!usbSendNspProperties(nsp_size, filename, (u32)nsp_header_size))
        {
            consolePrint("usb send nsp properties failed\n");
            goto end;
        }
    } else {
        if (nsp_size >= free_space)
        {
            consolePrint("nsp size exceeds free space\n");
            goto end;
        }

        utilsCreateDirectoryTree(filename, false);

        if (dev_idx == 0)
        {
            if (nsp_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(filename))
            {
                consolePrint("failed to create concatenation file for \"%s\"!\n", filename);
                goto end;
            }
        } else {
            if (g_umsDevices[dev_idx - 2].fs_type < UsbHsFsDeviceFileSystemType_exFAT && nsp_size > FAT32_FILESIZE_LIMIT)
            {
                consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
                goto end;
            }
        }

        if (!(fp = fopen(filename, "wb")))
        {
            consolePrint("fopen failed\n");
            goto end;
        }

        // set file size
        setvbuf(fp, NULL, _IONBF, 0);
        ftruncate(fileno(fp), (off_t)nsp_size);

        // write placeholder header
        memset(buf, 0, nsp_header_size);
        fwrite(buf, 1, nsp_header_size, fp);
    }

    consolePrint("dump process started, please wait. hold b to cancel.\n");
    consoleRefresh();

    nsp_offset += nsp_header_size;

    // set nsp size
    nsp_thread_data->total_size = nsp_size;

    // write ncas
    for(u32 i = 0; i < title_info->content_count; i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        u64 blksize = BLOCK_SIZE;

        sha256ContextCreate(&clean_sha256_ctx);
        sha256ContextCreate(&dirty_sha256_ctx);

        if (cur_nca_ctx->content_type == NcmContentType_Meta && (!cnmtGenerateNcaPatch(&cnmt_ctx) || !ncaEncryptHeader(cur_nca_ctx)))
        {
            consolePrint("cnmt generate patch failed\n");
            goto end;
        }

        bool dirty_header = ncaIsHeaderDirty(cur_nca_ctx);

        if (dev_idx == 1)
        {
            tmp_name = pfsGetEntryNameByIndexFromImageContext(&pfs_img_ctx, i);
            if (!usbSendFileProperties(cur_nca_ctx->content_size, tmp_name))
            {
                consolePrint("usb send file properties \"%s\" failed\n", tmp_name);
                goto end;
            }
        }

        for(u64 offset = 0; offset < cur_nca_ctx->content_size; offset += blksize, nsp_offset += blksize, nsp_thread_data->data_written += blksize)
        {
            mutexLock(&g_fileMutex);
            bool cancelled = nsp_thread_data->transfer_cancelled;
            mutexUnlock(&g_fileMutex);

            if (cancelled) goto end;

            if ((cur_nca_ctx->content_size - offset) < blksize) blksize = (cur_nca_ctx->content_size - offset);

            // read nca chunk
            if (!ncaReadContentFile(cur_nca_ctx, buf, blksize, offset))
            {
                consolePrint("nca read failed at 0x%lX for \"%s\"\n", offset, cur_nca_ctx->content_id_str);
                goto end;
            }

            // update clean hash calculation
            sha256ContextUpdate(&clean_sha256_ctx, buf, blksize);

            if ((offset + blksize) >= cur_nca_ctx->content_size)
            {
                // get clean hash
                sha256ContextGetHash(&clean_sha256_ctx, clean_sha256_hash);

                // validate clean hash
                if (!cnmtVerifyContentHash(&cnmt_ctx, cur_nca_ctx, clean_sha256_hash))
                {
                    consolePrint("sha256 checksum mismatch for nca \"%s\"\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
            }

            if (dirty_header)
            {
                // write re-encrypted headers
                if (!cur_nca_ctx->header_written) ncaWriteEncryptedHeaderDataToMemoryBuffer(cur_nca_ctx, buf, blksize, offset);

                if (cur_nca_ctx->content_type_ctx_patch)
                {
                    // write content type context patch
                    switch(cur_nca_ctx->content_type)
                    {
                        case NcmContentType_Meta:
                            cnmtWriteNcaPatch(&cnmt_ctx, buf, blksize, offset);
                            break;
                        case NcmContentType_Control:
                            nacpWriteNcaPatch((NacpContext*)cur_nca_ctx->content_type_ctx, buf, blksize, offset);
                            break;
                        default:
                            break;
                    }
                }

                // update flag to avoid entering this code block if it's not needed anymore
                dirty_header = (!cur_nca_ctx->header_written || cur_nca_ctx->content_type_ctx_patch);
            }

            // update dirty hash calculation
            sha256ContextUpdate(&dirty_sha256_ctx, buf, blksize);

            // write nca chunk
            if (dev_idx == 1)
            {
                if (!usbSendFileData(buf, blksize))
                {
                    consolePrint("send file data failed\n");
                    goto end;
                }
            } else {
                fwrite(buf, 1, blksize, fp);
            }
        }

        // get dirty hash
        sha256ContextGetHash(&dirty_sha256_ctx, dirty_sha256_hash);

        if (memcmp(clean_sha256_hash, dirty_sha256_hash, SHA256_HASH_SIZE) != 0)
        {
            // update content id and hash
            ncaUpdateContentIdAndHash(cur_nca_ctx, dirty_sha256_hash);

            // update cnmt
            if (!cnmtUpdateContentInfo(&cnmt_ctx, cur_nca_ctx))
            {
                consolePrint("cnmt update content info failed\n");
                goto end;
            }

            // update pfs entry name
            if (!pfsUpdateEntryNameFromImageContext(&pfs_img_ctx, i, cur_nca_ctx->content_id_str))
            {
                consolePrint("pfs update entry name failed for nca \"%s\"\n", cur_nca_ctx->content_id_str);
                goto end;
            }
        }
    }

    if (generate_authoringtool_data)
    {
        // regenerate cnmt xml
        if (!cnmtGenerateAuthoringToolXml(&cnmt_ctx, nca_ctx, title_info->content_count))
        {
            consolePrint("cnmt xml #2 failed\n");
            goto end;
        }

        // write cnmt xml
        if (dev_idx == 1)
        {
            tmp_name = pfsGetEntryNameByIndexFromImageContext(&pfs_img_ctx, meta_nca_ctx->content_type_ctx_data_idx);
            if (!usbSendFileProperties(cnmt_ctx.authoring_tool_xml_size, tmp_name) || !usbSendFileData(cnmt_ctx.authoring_tool_xml, cnmt_ctx.authoring_tool_xml_size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(cnmt_ctx.authoring_tool_xml, 1, cnmt_ctx.authoring_tool_xml_size, fp);
        }

        nsp_offset += cnmt_ctx.authoring_tool_xml_size;
        nsp_thread_data->data_written += cnmt_ctx.authoring_tool_xml_size;

        // update cnmt xml pfs entry name
        if (!pfsUpdateEntryNameFromImageContext(&pfs_img_ctx, meta_nca_ctx->content_type_ctx_data_idx, meta_nca_ctx->content_id_str))
        {
            consolePrint("pfs update entry name cnmt xml failed\n");
            goto end;
        }
    }

    // write content type ctx data
    for(u32 i = 0; i < limit; i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        if (!cur_nca_ctx->content_type_ctx) continue;

        char *authoring_tool_xml = NULL;
        u64 authoring_tool_xml_size = 0;
        u32 data_idx = cur_nca_ctx->content_type_ctx_data_idx;

        switch(cur_nca_ctx->content_type)
        {
            case NcmContentType_Program:
            {
                ProgramInfoContext *cur_program_info_ctx = (ProgramInfoContext*)cur_nca_ctx->content_type_ctx;
                authoring_tool_xml = cur_program_info_ctx->authoring_tool_xml;
                authoring_tool_xml_size = cur_program_info_ctx->authoring_tool_xml_size;
                break;
            }
            case NcmContentType_Control:
            {
                NacpContext *cur_nacp_ctx = (NacpContext*)cur_nca_ctx->content_type_ctx;
                authoring_tool_xml = cur_nacp_ctx->authoring_tool_xml;
                authoring_tool_xml_size = cur_nacp_ctx->authoring_tool_xml_size;

                // loop through available icons
                for(u8 j = 0; j < cur_nacp_ctx->icon_count; j++)
                {
                    NacpIconContext *icon_ctx = &(cur_nacp_ctx->icon_ctx[j]);

                    // write icon
                    if (dev_idx == 1)
                    {
                        tmp_name = pfsGetEntryNameByIndexFromImageContext(&pfs_img_ctx, data_idx);
                        if (!usbSendFileProperties(icon_ctx->icon_size, tmp_name) || !usbSendFileData(icon_ctx->icon_data, icon_ctx->icon_size))
                        {
                            consolePrint("send \"%s\" failed\n", tmp_name);
                            goto end;
                        }
                    } else {
                        fwrite(icon_ctx->icon_data, 1, icon_ctx->icon_size, fp);
                    }

                    nsp_offset += icon_ctx->icon_size;
                    nsp_thread_data->data_written += icon_ctx->icon_size;

                    // update pfs entry name
                    if (!pfsUpdateEntryNameFromImageContext(&pfs_img_ctx, data_idx++, cur_nca_ctx->content_id_str))
                    {
                        consolePrint("pfs update entry name failed for icon \"%s\" (%u)\n", cur_nca_ctx->content_id_str, icon_ctx->language);
                        goto end;
                    }
                }

                break;
            }
            case NcmContentType_LegalInformation:
            {
                LegalInfoContext *cur_legal_info_ctx = (LegalInfoContext*)cur_nca_ctx->content_type_ctx;
                authoring_tool_xml = cur_legal_info_ctx->authoring_tool_xml;
                authoring_tool_xml_size = cur_legal_info_ctx->authoring_tool_xml_size;
                break;
            }
            default:
                break;
        }

        // write xml
        if (dev_idx == 1)
        {
            tmp_name = pfsGetEntryNameByIndexFromImageContext(&pfs_img_ctx, data_idx);
            if (!usbSendFileProperties(authoring_tool_xml_size, tmp_name) || !usbSendFileData(authoring_tool_xml, authoring_tool_xml_size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(authoring_tool_xml, 1, authoring_tool_xml_size, fp);
        }

        nsp_offset += authoring_tool_xml_size;
        nsp_thread_data->data_written += authoring_tool_xml_size;

        // update pfs entry name
        if (!pfsUpdateEntryNameFromImageContext(&pfs_img_ctx, data_idx, cur_nca_ctx->content_id_str))
        {
            consolePrint("pfs update entry name failed for xml \"%s\"\n", cur_nca_ctx->content_id_str);
            goto end;
        }
    }

    if (retrieve_tik_cert)
    {
        // write ticket
        if (dev_idx == 1)
        {
            tmp_name = pfsGetEntryNameByIndexFromImageContext(&pfs_img_ctx, pfs_img_ctx.header.entry_count - 2);
            if (!usbSendFileProperties(tik.size, tmp_name) || !usbSendFileData(tik.data, tik.size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(tik.data, 1, tik.size, fp);
        }

        nsp_offset += tik.size;
        nsp_thread_data->data_written += tik.size;

        // write cert
        if (dev_idx == 1)
        {
            tmp_name = pfsGetEntryNameByIndexFromImageContext(&pfs_img_ctx, pfs_img_ctx.header.entry_count - 1);
            if (!usbSendFileProperties(raw_cert_chain_size, tmp_name) || !usbSendFileData(raw_cert_chain, raw_cert_chain_size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(raw_cert_chain, 1, raw_cert_chain_size, fp);
        }

        nsp_offset += raw_cert_chain_size;
        nsp_thread_data->data_written += raw_cert_chain_size;
    }

    // write new pfs0 header
    if (!pfsWriteImageContextHeaderToMemoryBuffer(&pfs_img_ctx, buf, BLOCK_SIZE, &nsp_header_size))
    {
        consolePrint("pfs write header to mem #2 failed\n");
        goto end;
    }

    if (dev_idx == 1)
    {
        if (!usbSendNspHeader(buf, (u32)nsp_header_size))
        {
            consolePrint("send nsp header failed\n");
            goto end;
        }
    } else {
        rewind(fp);
        fwrite(buf, 1, nsp_header_size, fp);
    }

    nsp_thread_data->data_written += nsp_header_size;

    success = true;

end:
    consoleRefresh();

    mutexLock(&g_fileMutex);
    if (!success && !nsp_thread_data->transfer_cancelled) nsp_thread_data->error = true;
    mutexUnlock(&g_fileMutex);

    if (fp)
    {
        fclose(fp);

        if (!success)
        {
            if (dev_idx == 0)
            {
                utilsRemoveConcatenationFile(filename);
                utilsCommitSdCardFileSystemChanges();
            } else {
                remove(filename);
            }
        }
    }

    if (!success && dev_idx == 1) usbCancelFileTransfer();

    pfsFreeImageContext(&pfs_img_ctx);

    if (raw_cert_chain) free(raw_cert_chain);

    if (legal_info_ctx)
    {
        for(u32 i = 0; i < legal_info_count; i++) legalInfoFreeContext(&(legal_info_ctx[i]));
        free(legal_info_ctx);
    }

    if (nacp_ctx)
    {
        for(u32 i = 0; i < control_count; i++) nacpFreeContext(&(nacp_ctx[i]));
        free(nacp_ctx);
    }

    if (program_info_ctx)
    {
        for(u32 i = 0; i < program_count; i++) programInfoFreeContext(&(program_info_ctx[i]));
        free(program_info_ctx);
    }

    cnmtFreeContext(&cnmt_ctx);

    if (nca_ctx) free(nca_ctx);

    if (filename) free(filename);

    if (buf) free(buf);

    threadExit();
}

static u32 getOutputStorageOption(void)
{
    return (u32)configGetInteger("output_storage");
}

static void setOutputStorageOption(u32 idx)
{
    if (idx < ConfigOutputStorage_Count) configSetInteger("output_storage", (int)idx);
}

static u32 getGameCardPrependKeyAreaOption(void)
{
    return (u32)configGetBoolean("gamecard/prepend_key_area");
}

static void setGameCardPrependKeyAreaOption(u32 idx)
{
    configSetBoolean("gamecard/prepend_key_area", (bool)idx);
}

static u32 getGameCardKeepCertificateOption(void)
{
    return (u32)configGetBoolean("gamecard/keep_certificate");
}

static void setGameCardKeepCertificateOption(u32 idx)
{
    configSetBoolean("gamecard/keep_certificate", (bool)idx);
}

static u32 getGameCardTrimDumpOption(void)
{
    return (u32)configGetBoolean("gamecard/trim_dump");
}

static void setGameCardTrimDumpOption(u32 idx)
{
    configSetBoolean("gamecard/trim_dump", (bool)idx);
}

static u32 getGameCardCalculateChecksumOption(void)
{
    return (u32)configGetBoolean("gamecard/calculate_checksum");
}

static void setGameCardCalculateChecksumOption(u32 idx)
{
    configSetBoolean("gamecard/calculate_checksum", (bool)idx);
}

static u32 getGameCardWriteRawHfsPartitionOption(void)
{
    return (u32)configGetBoolean("gamecard/write_raw_hfs_partition");
}

static void setGameCardWriteRawHfsPartitionOption(u32 idx)
{
    configSetBoolean("gamecard/write_raw_hfs_partition", (bool)idx);
}

static u32 getNspSetDownloadDistributionOption(void)
{
    return (u32)configGetBoolean("nsp/set_download_distribution");
}

static void setNspSetDownloadDistributionOption(u32 idx)
{
    configSetBoolean("nsp/set_download_distribution", (bool)idx);
}

static u32 getNspRemoveConsoleDataOption(void)
{
    return (u32)configGetBoolean("nsp/remove_console_data");
}

static void setNspRemoveConsoleDataOption(u32 idx)
{
    configSetBoolean("nsp/remove_console_data", (bool)idx);
}

static u32 getNspRemoveTitlekeyCryptoOption(void)
{
    return (u32)configGetBoolean("nsp/remove_titlekey_crypto");
}

static void setNspRemoveTitlekeyCryptoOption(u32 idx)
{
    configSetBoolean("nsp/remove_titlekey_crypto", (bool)idx);
}

static u32 getNspDisableLinkedAccountRequirementOption(void)
{
    return (u32)configGetBoolean("nsp/disable_linked_account_requirement");
}

static void setNspDisableLinkedAccountRequirementOption(u32 idx)
{
    configSetBoolean("nsp/disable_linked_account_requirement", (bool)idx);
}

static u32 getNspEnableScreenshotsOption(void)
{
    return (u32)configGetBoolean("nsp/enable_screenshots");
}

static void setNspEnableScreenshotsOption(u32 idx)
{
    configSetBoolean("nsp/enable_screenshots", (bool)idx);
}

static u32 getNspEnableVideoCaptureOption(void)
{
    return (u32)configGetBoolean("nsp/enable_video_capture");
}

static void setNspEnableVideoCaptureOption(u32 idx)
{
    configSetBoolean("nsp/enable_video_capture", (bool)idx);
}

static u32 getNspDisableHdcpOption(void)
{
    return (u32)configGetBoolean("nsp/disable_hdcp");
}

static void setNspDisableHdcpOption(u32 idx)
{
    configSetBoolean("nsp/disable_hdcp", (bool)idx);
}

static u32 getNspGenerateAuthoringToolDataOption(void)
{
    return (u32)configGetBoolean("nsp/generate_authoringtool_data");
}

static void setNspGenerateAuthoringToolDataOption(u32 idx)
{
    configSetBoolean("nsp/generate_authoringtool_data", (bool)idx);
}

static u32 getTicketRemoveConsoleDataOption(void)
{
    return (u32)configGetBoolean("ticket/remove_console_data");
}

static void setTicketRemoveConsoleDataOption(u32 idx)
{
    configSetBoolean("ticket/remove_console_data", (bool)idx);
}

static u32 getNcaFsWriteRawSectionOption(void)
{
    return (u32)configGetBoolean("nca_fs/write_raw_section");
}

static void setNcaFsWriteRawSectionOption(u32 idx)
{
    configSetBoolean("nca_fs/write_raw_section", (bool)idx);
}

static u32 getNcaFsUseLayeredFsDirOption(void)
{
    return (u32)configGetBoolean("nca_fs/use_layeredfs_dir");
}

static void setNcaFsUseLayeredFsDirOption(u32 idx)
{
    configSetBoolean("nca_fs/use_layeredfs_dir", (bool)idx);
}
