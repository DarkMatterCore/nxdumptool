/*
 * main.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include "nxdt_utils.h"
#include "gamecard.h"
#include "title.h"
#include "cnmt.h"
#include "program_info.h"
#include "nacp.h"
#include "legal_info.h"
#include "cert.h"
#include "usb.h"

#define BLOCK_SIZE      USB_TRANSFER_BUFFER_SIZE
#define WAIT_TIME_LIMIT 30
#define OUTDIR          "nxdt_rw_poc"

/* Type definitions. */

typedef struct _Menu Menu;

typedef u32  (*MenuElementOptionGetterFunction)(void);
typedef void (*MenuElementOptionSetterFunction)(u32 idx);
typedef bool (*MenuElementFunction)(void *userdata);

typedef struct {
    u32 selected;                                   ///< Used to keep track of the selected option.
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
    MenuId_Root              = 0,
    MenuId_GameCard          = 1,
    MenuId_XCI               = 2,
    MenuId_HFS               = 3,
    MenuId_UserTitles        = 4,
    MenuId_UserTitlesSubMenu = 5,
    MenuId_NSPTitleTypes     = 6,
    MenuId_NSP               = 7,
    MenuId_TicketTitleTypes  = 8,
    MenuId_Ticket            = 9,
    MenuId_NCATitleTypes     = 10,
    MenuId_NCA               = 11,
    MenuId_NCAFsSection      = 12,
    MenuId_Count             = 13
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

/* Function prototypes. */

static void utilsScanPads(void);
static u64 utilsGetButtonsDown(void);
static u64 utilsGetButtonsHeld(void);
static void utilsWaitForButtonPress(u64 flag);

static void consolePrint(const char *text, ...);
static void consoleRefresh(void);

static u32 menuGetElementCount(const Menu *menu);

void freeStorageList(void);
void updateStorageList(void);

void freeTitleList(void);
void updateTitleList(void);

void freeNcaList(void);
void updateNcaList(TitleInfo *title_info);

NX_INLINE bool useUsbHost(void);

static bool waitForGameCard(void);
static bool waitForUsb(void);

static char *generateOutputGameCardFileName(const char *subdir, const char *extension, bool use_nacp_name);
static char *generateOutputTitleFileName(TitleInfo *title_info, const char *subdir, const char *extension);

static bool dumpGameCardSecurityInformation(GameCardSecurityInformation *out);

static bool saveGameCardImage(void *userdata);
static bool saveGameCardHeader(void *userdata);
static bool saveGameCardCardInfo(void *userdata);
static bool saveGameCardCertificate(void *userdata);
static bool saveGameCardInitialData(void *userdata);
static bool saveGameCardSpecificData(void *userdata);
static bool saveGameCardIdSet(void *userdata);
static bool saveGameCardHfsPartition(void *userdata);
static bool saveGameCardRawHfsPartition(HashFileSystemContext *hfs_ctx);
static bool saveGameCardExtractedHfsPartition(HashFileSystemContext *hfs_ctx);

static bool saveConsoleLafwBlob(void *userdata);

static bool saveNintendoSubmissionPackage(void *userdata);

static bool saveTicket(void *userdata);

static bool saveNintendoContentArchive(void *userdata);

static void xciReadThreadFunc(void *arg);

static void rawHfsReadThreadFunc(void *arg);
static void extractedHfsReadThreadFunc(void *arg);

static void ncaReadThreadFunc(void *arg);

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

static u32 getNspAppendAuthoringToolDataOption(void);
static void setNspAppendAuthoringToolDataOption(u32 idx);

static u32 getTicketRemoveConsoleDataOption(void);
static void setTicketRemoveConsoleDataOption(u32 idx);

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
    .getter_func = &getOutputStorageOption,
    .setter_func = &setOutputStorageOption,
    .options = NULL
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
            .getter_func = &getGameCardCalculateChecksumOption,
            .setter_func = &setGameCardCalculateChecksumOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "output storage",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &g_storageMenuElementOption,
        .userdata = NULL
    },
    NULL
};

static u32 g_hfsRootPartition = HashFileSystemPartitionType_Root;
static u32 g_hfsUpdatePartition = HashFileSystemPartitionType_Update;
static u32 g_hfsLogoPartition = HashFileSystemPartitionType_Logo;
static u32 g_hfsNormalPartition = HashFileSystemPartitionType_Normal;
static u32 g_hfsSecurePartition = HashFileSystemPartitionType_Secure;

static MenuElement *g_gameCardHfsMenuElements[] = {
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
            .getter_func = &getGameCardWriteRawHfsPartitionOption,
            .setter_func = &setGameCardWriteRawHfsPartitionOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "output storage",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &g_storageMenuElementOption,
        .userdata = NULL
    },
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
        .str = "dump gamecard header",
        .child_menu = NULL,
        .task_func = &saveGameCardHeader,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard cardinfo",
        .child_menu = NULL,
        .task_func = &saveGameCardCardInfo,
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
        .str = "dump gamecard initial data",
        .child_menu = NULL,
        .task_func = &saveGameCardInitialData,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump gamecard specific data",
        .child_menu = NULL,
        .task_func = &saveGameCardSpecificData,
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
        .str = "dump hfs partitions",
        .child_menu = &(Menu){
            .id = MenuId_HFS,
            .parent = NULL,
            .selected = 0,
            .scroll = 0,
            .elements = g_gameCardHfsMenuElements
        },
        .task_func = NULL,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "dump console lafw blob",
        .child_menu = NULL,
        .task_func = &saveConsoleLafwBlob,
        .element_options = NULL,
        .userdata = NULL
    },
    &(MenuElement){
        .str = "output storage",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &g_storageMenuElementOption,
        .userdata = NULL
    },
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
            .getter_func = &getNspDisableHdcpOption,
            .setter_func = &setNspDisableHdcpOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "nsp: append authoringtool data",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
            .getter_func = &getNspAppendAuthoringToolDataOption,
            .setter_func = &setNspAppendAuthoringToolDataOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "output storage",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &g_storageMenuElementOption,
        .userdata = NULL
    },
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
            .getter_func = &getTicketRemoveConsoleDataOption,
            .setter_func = &setTicketRemoveConsoleDataOption,
            .options = g_noYesStrings
        },
        .userdata = NULL
    },
    &(MenuElement){
        .str = "output storage",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &g_storageMenuElementOption,
        .userdata = NULL
    },
    NULL
};

static Menu g_ticketMenu = {
    .id = MenuId_Ticket,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_ticketMenuElements
};

static MenuElement **g_ncaMenuElements = NULL;

// Dynamically populated using g_ncaMenuElements.
static Menu g_ncaMenu = {
    .id = MenuId_NCA,
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
        .str = "nca dump options",
        .child_menu = &(Menu){
            .id = MenuId_NCATitleTypes,
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

// Dynamically set as child_menu for all g_userTitlesMenuElements entries.
static Menu g_userTitlesSubMenu = {
    .id = MenuId_UserTitlesSubMenu,
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_userTitlesSubMenuElements
};

static MenuElement **g_userTitlesMenuElements = NULL;

// Dynamically populated using g_userTitlesMenuElements.
static Menu g_userTitlesMenu = {
    .id = MenuId_UserTitles,
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
        .child_menu = NULL,
        .task_func = NULL,
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

static char path[FS_MAX_PATH] = {0};

int main(int argc, char *argv[])
{
    int ret = 0;

    if (!utilsInitializeResources(argc, (const char**)argv))
    {
        ret = -1;
        goto end;
    }

    /* Configure input. */
    /* Up to 8 different, full controller inputs. */
    /* Individual Joy-Cons not supported. */
    padConfigureInput(8, HidNpadStyleSet_NpadFullCtrl);
    padInitializeWithMask(&g_padState, 0x1000000FFUL);

    consoleInit(NULL);

    updateStorageList();

    updateTitleList();

    Menu *cur_menu = &g_rootMenu;
    u32 element_count = menuGetElementCount(cur_menu), page_size = 30;

    TitleApplicationMetadata *app_metadata = NULL;

    TitleUserApplicationData user_app_data = {0};

    TitleInfo *title_info = NULL;
    u32 title_info_idx = 0, title_info_count = 0;

    while(appletMainLoop())
    {
        MenuElement *selected_element = cur_menu->elements[cur_menu->selected];
        MenuElementOption *selected_element_options = selected_element->element_options;

        if (cur_menu->id == MenuId_UserTitlesSubMenu && selected_element->child_menu)
        {
            /* Set title types child menu pointer if we're currently at the user titles submenu. */
            u32 child_id = selected_element->child_menu->id;

            g_titleTypesMenuElements[0]->child_menu = g_titleTypesMenuElements[1]->child_menu = \
            g_titleTypesMenuElements[2]->child_menu = g_titleTypesMenuElements[3]->child_menu = (child_id == MenuId_NSPTitleTypes ? &g_nspMenu : \
                                                                                                (child_id == MenuId_TicketTitleTypes ? &g_ticketMenu : \
                                                                                                (child_id == MenuId_NCATitleTypes ? &g_ncaMenu : NULL)));
        }

        consoleClear();
        consolePrint("______________________________\n\n");
        consolePrint("press b to %s.\n", cur_menu->parent ? "go back" : "exit");
        if (g_umsDeviceCount) consolePrint("press x to safely remove all ums devices\n");
        consolePrint("______________________________\n\n");

        if (cur_menu->id == MenuId_UserTitles)
        {
            app_metadata = (TitleApplicationMetadata*)selected_element->userdata;

            consolePrint("title: %u / %u\n", cur_menu->selected + 1, element_count);
            consolePrint("selected title: %016lX - %s\n", app_metadata->title_id, selected_element->str);
            consolePrint("______________________________\n\n");
        } else
        if (cur_menu->id >= MenuId_UserTitlesSubMenu && cur_menu->id < MenuId_Count)
        {
            consolePrint("title info:\n\n");
            consolePrint("name: %s\n", app_metadata->lang_entry.name);
            consolePrint("publisher: %s\n", app_metadata->lang_entry.author);
            consolePrint("title id: %016lX\n", app_metadata->title_id);
            consolePrint("______________________________\n\n");

            if (cur_menu->id == MenuId_NSP || cur_menu->id == MenuId_Ticket || cur_menu->id == MenuId_NCA)
            {
                if (title_info->previous || title_info->next)
                {
                    consolePrint("press l/zl and/or r/zr to change the selected title\n");
                    consolePrint("title: %u / %u\n", title_info_idx, title_info_count);
                    consolePrint("______________________________\n\n");
                }

                consolePrint("selected title info:\n\n");
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
            }
        }

        for(u32 i = cur_menu->scroll; i < element_count; i++)
        {
            if (i >= (cur_menu->scroll + page_size)) break;

            MenuElement *cur_element = cur_menu->elements[i];
            MenuElementOption *cur_options = cur_element->element_options;
            TitleApplicationMetadata *cur_app_metadata = (cur_menu->id == MenuId_UserTitles ? (TitleApplicationMetadata*)cur_element->userdata : NULL);

            consolePrint("%s", i == cur_menu->selected ? " -> " : "    ");
            if (cur_app_metadata) consolePrint("%016lX - ", cur_app_metadata->title_id);
            consolePrint("%s", cur_element->str);

            if (cur_options)
            {
                if (cur_options->getter_func)
                {
                    cur_options->selected = cur_options->getter_func();
                    cur_options->getter_func = NULL;
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
                updateTitleList();
                data_update = true;
                break;
            }
        }

        if (!g_appletStatus) break;

        if (data_update) continue;

        if (btn_down & HidNpadButton_A)
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
                if (child_menu->id == MenuId_NSP || child_menu->id == MenuId_Ticket || child_menu->id == MenuId_NCA)
                {
                    u32 title_type = *((u32*)selected_element->userdata);

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
                            title_info = NULL;
                            break;
                    }

                    if (title_info)
                    {
                        if (child_menu->id == MenuId_NCA)
                        {
                            updateNcaList(title_info);

                            if (!g_ncaMenuElements || !g_ncaMenuElements[0])
                            {
                                consolePrint("failed to generate nca list\n");
                                error = true;
                            }
                        }

                        if (!error)
                        {
                            title_info_count = titleGetCountFromInfoBlock(title_info);
                            title_info_idx = 1;
                        }
                    } else {
                        consolePrint("\nthe selected title doesn't have available %s data\n", \
                                    title_type == NcmContentMetaType_Application ? "base application" : \
                                    (title_type == NcmContentMetaType_Patch ? "update" : (title_type == NcmContentMetaType_AddOnContent ? "dlc" : "dlc update")));

                        error = true;
                    }
                }

                if (!error)
                {
                    child_menu->parent = cur_menu;
                    child_menu->selected = child_menu->scroll = 0;

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
                consoleClear();

                /* Wait for gamecard (if needed). */
                if (((cur_menu->id >= MenuId_GameCard && cur_menu->id <= MenuId_HFS) || (title_info && title_info->storage_id == NcmStorageId_GameCard)) && !waitForGameCard())
                {
                    if (g_appletStatus) continue;
                    break;
                }

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

                /* Display prompt. */
                consolePrint("press any button to continue");
                utilsWaitForButtonPress(0);
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
            selected_element_options->selected++;
            if (!selected_element_options->options[selected_element_options->selected]) selected_element_options->selected--;
            if (selected_element_options->setter_func) selected_element_options->setter_func(selected_element_options->selected);
        } else
        if ((btn_down & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft)) && selected_element_options)
        {
            selected_element_options->selected--;
            if (selected_element_options->selected == UINT32_MAX) selected_element_options->selected = 0;
            if (selected_element_options->setter_func) selected_element_options->setter_func(selected_element_options->selected);
        } else
        if (btn_down & HidNpadButton_B)
        {
            if (!cur_menu->parent) break;

            if (cur_menu->id == MenuId_UserTitles)
            {
                app_metadata = NULL;
            } else
            if (cur_menu->id == MenuId_UserTitlesSubMenu)
            {
                titleFreeUserApplicationData(&user_app_data);
                g_titleTypesMenuElements[0]->child_menu = g_titleTypesMenuElements[1]->child_menu = \
                g_titleTypesMenuElements[2]->child_menu = g_titleTypesMenuElements[3]->child_menu = NULL;
            } else
            if (cur_menu->id == MenuId_NSPTitleTypes || cur_menu->id == MenuId_TicketTitleTypes || cur_menu->id == MenuId_NCATitleTypes)
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
            if (cur_menu->id == MenuId_NCA)
            {
                freeNcaList();
            }

            cur_menu->selected = 0;
            cur_menu->scroll = 0;

            cur_menu = cur_menu->parent;
            element_count = menuGetElementCount(cur_menu);
        } else
        if ((btn_down & HidNpadButton_X) && g_umsDeviceCount)
        {
            for(u32 i = 0; i < g_umsDeviceCount; i++) usbHsFsUnmountDevice(&(g_umsDevices[i]), false);
            updateStorageList();
        } else
        if (((btn_down & (HidNpadButton_L)) || (btn_held & HidNpadButton_ZL)) && (cur_menu->id == MenuId_NSP || cur_menu->id == MenuId_Ticket || cur_menu->id == MenuId_NCA) && title_info->previous)
        {
            title_info = title_info->previous;
            title_info_idx--;

            if (cur_menu->id == MenuId_NCA)
            {
                updateNcaList(title_info);
                if (!g_ncaMenuElements || !g_ncaMenuElements[0])
                {
                    freeNcaList();
                    consolePrint("\nfailed to generate nca list for newly selected title\npress any button to go back\n");
                    consoleRefresh();
                    utilsWaitForButtonPress(0);

                    cur_menu->selected = 0;
                    cur_menu->scroll = 0;

                    cur_menu = cur_menu->parent;
                    element_count = menuGetElementCount(cur_menu);
                }
            }
        } else
        if (((btn_down & (HidNpadButton_R)) || (btn_held & HidNpadButton_ZR)) && (cur_menu->id == MenuId_NSP || cur_menu->id == MenuId_Ticket || cur_menu->id == MenuId_NCA) && title_info->next)
        {
            title_info = title_info->next;
            title_info_idx++;

            if (cur_menu->id == MenuId_NCA)
            {
                updateNcaList(title_info);
                if (!g_ncaMenuElements || !g_ncaMenuElements[0])
                {
                    freeNcaList();
                    consolePrint("\nfailed to generate nca list for newly selected title\npress any button to go back\n");
                    consoleRefresh();
                    utilsWaitForButtonPress(0);

                    cur_menu->selected = 0;
                    cur_menu->scroll = 0;

                    cur_menu = cur_menu->parent;
                    element_count = menuGetElementCount(cur_menu);
                }
            }
        }

        if (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown | HidNpadButton_StickLUp | HidNpadButton_StickRUp | HidNpadButton_ZL | HidNpadButton_ZR)) svcSleepThread(50000000); // 50 ms
    }

end:
    titleFreeUserApplicationData(&user_app_data);

    freeNcaList();

    freeTitleList();

    freeStorageList();

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

static void utilsWaitForButtonPress(u64 flag)
{
    /* Don't consider stick movement as button inputs. */
    if (!flag) flag = ~(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | \
                        HidNpadButton_StickRUp | HidNpadButton_StickRDown);

    consoleRefresh();

    while(appletMainLoop())
    {
        utilsScanPads();
        if (utilsGetButtonsDown() & flag) break;
    }
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
}

void updateStorageList(void)
{
    char **tmp = NULL;
    u32 elem_count = 0, idx = 0;

    /* Free all previously allocated data. */
    freeStorageList();

    /* Get UMS devices. */
    g_umsDevices = umsGetDevices(&g_umsDeviceCount);
    elem_count = (2 + g_umsDeviceCount); // sd card, usb host, ums devices

    /* Reallocate buffer. */
    tmp = realloc(g_storageOptions, (elem_count + 1) * sizeof(char*)); // NULL terminator

    g_storageOptions = tmp;
    tmp = NULL;

    memset(g_storageOptions, 0, (elem_count + 1) * sizeof(char*)); // NULL terminator

    /* Generate UMS device strings. */
    for(u32 i = 0; i < elem_count; i++)
    {
        u64 total = 0, free = 0;
        char total_str[36] = {0}, free_str[32] = {0};

        g_storageOptions[idx] = calloc(sizeof(char), 0x300);
        if (!g_storageOptions[idx]) continue;

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

void freeTitleList(void)
{
    /* Free all previously allocated data. */
    if (g_userTitlesMenuElements)
    {
        for(u32 i = 0; g_userTitlesMenuElements[i] != NULL; i++) free(g_userTitlesMenuElements[i]);

        free(g_userTitlesMenuElements);
        g_userTitlesMenuElements = NULL;
    }

    g_userTitlesMenu.scroll = 0;
    g_userTitlesMenu.selected = 0;
    g_userTitlesMenu.elements = NULL;
}

void updateTitleList(void)
{
    u32 app_count = 0, idx = 0;
    TitleApplicationMetadata **app_metadata = NULL;
    MenuElement **tmp = NULL;

    /* Free all previously allocated data. */
    freeTitleList();

    /* Get application metadata entries. */
    app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
    if (!app_metadata || !app_count) goto end;

    /* Reallocate buffer. */
    tmp = realloc(g_userTitlesMenuElements, (app_count + 1) * sizeof(MenuElement*)); // NULL terminator

    g_userTitlesMenuElements = tmp;
    tmp = NULL;

    memset(g_userTitlesMenuElements, 0, (app_count + 1) * sizeof(MenuElement*)); // NULL terminator

    /* Generate menu elements. */
    for(u32 i = 0; i < app_count; i++)
    {
        TitleApplicationMetadata *cur_app_metadata = app_metadata[i];

        g_userTitlesMenuElements[idx] = calloc(1, sizeof(MenuElement));
        if (!g_userTitlesMenuElements[idx]) continue;

        g_userTitlesMenuElements[idx]->str = cur_app_metadata->lang_entry.name;
        g_userTitlesMenuElements[idx]->child_menu = &g_userTitlesSubMenu;
        g_userTitlesMenuElements[idx]->userdata = cur_app_metadata;

        idx++;
    }

    g_userTitlesMenu.elements = g_userTitlesMenuElements;

end:
    if (app_metadata) free(app_metadata);
}

void freeNcaList(void)
{
    /* Free all previously allocated data. */
    if (g_ncaMenuElements)
    {
        for(u32 i = 0; g_ncaMenuElements[i] != NULL; i++)
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

void updateNcaList(TitleInfo *title_info)
{
    u32 content_count = title_info->content_count, idx = 0;
    NcmContentInfo *content_infos = title_info->content_infos;
    MenuElement **tmp = NULL;
    char nca_id_str[0x21] = {0};

    /* Free all previously allocated data. */
    freeNcaList();

    /* Reallocate buffer. */
    tmp = realloc(g_ncaMenuElements, (content_count + 1) * sizeof(MenuElement*)); // NULL terminator

    g_ncaMenuElements = tmp;
    tmp = NULL;

    memset(g_ncaMenuElements, 0, (content_count + 1) * sizeof(MenuElement*)); // NULL terminator

    /* Generate menu elements. */
    for(u32 i = 0; i < content_count; i++)
    {
        NcmContentInfo *cur_content_info = &(content_infos[i]);
        char *nca_info_str = NULL, nca_size_str[16] = {0};
        u64 nca_size = 0;
        NcaUserData *nca_user_data = NULL;

        g_ncaMenuElements[idx] = calloc(1, sizeof(MenuElement));
        if (!g_ncaMenuElements[idx]) continue;

        nca_info_str = calloc(128, sizeof(char));
        nca_user_data = calloc(1, sizeof(NcaUserData));

        if (!nca_info_str || !nca_user_data)
        {
            if (nca_info_str) free(nca_info_str);
            if (nca_user_data) free(nca_user_data);
            continue;
        }

        utilsGenerateHexStringFromData(nca_id_str, sizeof(nca_id_str), cur_content_info->content_id.c, sizeof(cur_content_info->content_id.c), false);

        ncmContentInfoSizeToU64(cur_content_info, &nca_size);
        utilsGenerateFormattedSizeString((double)nca_size, nca_size_str, sizeof(nca_size_str));

        sprintf(nca_info_str, "%s #%u: %s (%s)", titleGetNcmContentTypeName(cur_content_info->content_type), cur_content_info->id_offset, nca_id_str, nca_size_str);

        nca_user_data->title_info = title_info;
        nca_user_data->content_idx = i;

        g_ncaMenuElements[idx]->str = nca_info_str;
        //g_ncaMenuElements[idx]->child_menu = NULL;
        g_ncaMenuElements[idx]->task_func = &saveNintendoContentArchive;
        g_ncaMenuElements[idx]->userdata = nca_user_data;

        idx++;
    }

    g_ncaMenu.elements = g_ncaMenuElements;
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
        consolePrint("press any button\n");
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

    if (dev_idx == 1)
    {
        if (subdir) sprintf(prefix, "/%s", subdir);
    } else {
        sprintf(prefix, "%s/" OUTDIR, dev_idx == 0 ? DEVOPTAB_SDMC_DEVICE : g_umsDevices[dev_idx - 2].name);

        if (subdir)
        {
            if (subdir[0] != '/') strcat(prefix, "/");
            strcat(prefix, subdir);
        }
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

    if (dev_idx == 1)
    {
        if (subdir) sprintf(prefix, "/%s", subdir);
    } else {
        sprintf(prefix, "%s/" OUTDIR, dev_idx == 0 ? DEVOPTAB_SDMC_DEVICE : g_umsDevices[dev_idx - 2].name);

        if (subdir)
        {
            if (subdir[0] != '/') strcat(prefix, "/");
            strcat(prefix, subdir);
        }
    }

    output = utilsGeneratePath(prefix, filename, extension);
    if (!output) consolePrint("failed to generate output filename!\n");

end:
    if (prefix) free(prefix);
    if (filename) free(filename);

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

static bool saveGameCardImage(void *userdata)
{
    (void)userdata;

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

    snprintf(path, MAX_ELEMENTS(path), " (%s) (%s) (%s).xci", prepend_key_area ? "keyarea" : "keyarealess", keep_certificate ? "cert" : "certless", trim_dump ? "trimmed" : "untrimmed");
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
    (void)userdata;

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
    (void)userdata;

    GameCardInfo gc_cardinfo = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!gamecardGetDecryptedCardInfoArea(&gc_cardinfo))
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
    (void)userdata;

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
    (void)userdata;

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

static bool saveGameCardSpecificData(void *userdata)
{
    (void)userdata;

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
    (void)userdata;

    FsGameCardIdSet id_set = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;

    if (!gamecardGetIdSet(&id_set))
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

static bool saveConsoleLafwBlob(void *userdata)
{
    (void)userdata;

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
    while(!nsp_thread_data.total_size && !nsp_thread_data.error) svcSleepThread(10000000); // 10 ms

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
    TikCommonBlock *tik_common_block = NULL;
    char enc_titlekey_str[33] = {0};

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
        content_info, title_info->version.value, &tik))
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

    if (!tik.size)
    {
        consolePrint("failed to retrieve ticket (unavailable?)\ntry launching nxdumptool while overriding the title you wish to dump a ticket from\n");
        goto end;
    }

    /* Retrieve ticket common block. */
    if (!(tik_common_block = tikGetCommonBlock(tik.data)))
    {
        consolePrint("failed to get tik common block\n");
        goto end;
    }

    /* Remove console-specific data, if needed. */
    if (remove_console_data && tik_common_block->titlekey_type == TikTitleKeyType_Personalized && !tikConvertPersonalizedTicketToCommonTicket(&tik, NULL, NULL))
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

    utilsGenerateHexStringFromData(enc_titlekey_str, MAX_ELEMENTS(enc_titlekey_str), tik.enc_titlekey, sizeof(tik.enc_titlekey), false);

    consolePrint("rights id: %s\n", tik.rights_id_str);
    consolePrint("titlekey: %s\n\n", enc_titlekey_str);

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
    char *filename = NULL;
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
        content_info, title_info->version.value, NULL))
    {
        consolePrint("nca initialize ctx failed\n");
        goto end;
    }

    shared_thread_data->total_size = nca_thread_data.nca_ctx->content_size;

    consolePrint("nca size: 0x%lX\n", shared_thread_data->total_size);

    snprintf(path, MAX_ELEMENTS(path), "/%s.%s", nca_thread_data.nca_ctx->content_id_str, content_info->content_type == NcmContentType_Meta ? "cnmt.nca" : "nca");
    filename = generateOutputTitleFileName(title_info, "NCA/User", path);
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

static void xciReadThreadFunc(void *arg)
{
    void *buf1 = NULL, *buf2 = NULL;
    XciThreadData *xci_thread_data = (XciThreadData*)arg;
    SharedThreadData *shared_thread_data = (xci_thread_data ? &(xci_thread_data->shared_thread_data) : NULL);

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!xci_thread_data || !shared_thread_data || !shared_thread_data->total_size || !buf1 || !buf2)
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
        if (!keep_certificate && offset == 0) memset((u8*)buf1 + GAMECARD_CERTIFICATE_OFFSET, 0xFF, sizeof(FsGameCardCertificate));

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
    SharedThreadData *shared_thread_data = (hfs_thread_data ? &(hfs_thread_data->shared_thread_data) : NULL);
    HashFileSystemContext *hfs_ctx = (hfs_thread_data ? hfs_thread_data->hfs_ctx : NULL);

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!hfs_thread_data || !shared_thread_data || !shared_thread_data->total_size || !hfs_ctx || !buf1 || !buf2)
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
    SharedThreadData *shared_thread_data = (hfs_thread_data ? &(hfs_thread_data->shared_thread_data) : NULL);

    HashFileSystemContext *hfs_ctx = (hfs_thread_data ? hfs_thread_data->hfs_ctx : NULL);
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

    if (!hfs_thread_data || !shared_thread_data || !shared_thread_data->total_size || !hfs_ctx || !hfs_entry_count || !buf1 || !buf2 || !filename)
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
            goto end;
        }

        if (shared_thread_data->total_size >= free_space)
        {
            consolePrint("dump size exceeds free space\n");
            shared_thread_data->read_error = true;
            goto end;
        }
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
                utilsCommitSdCardFileSystemChanges();
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
    SharedThreadData *shared_thread_data = (nca_thread_data ? &(nca_thread_data->shared_thread_data) : NULL);
    NcaContext *nca_ctx = (nca_thread_data ? nca_thread_data->nca_ctx : NULL);

    buf1 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    buf2 = usbAllocatePageAlignedBuffer(BLOCK_SIZE);

    if (!nca_thread_data || !shared_thread_data || !shared_thread_data->total_size || !nca_ctx || !buf1 || !buf2)
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

static void genericWriteThreadFunc(void *arg)
{
    SharedThreadData *shared_thread_data = (SharedThreadData*)arg; // UB but we don't care
    if (!shared_thread_data)
    {
        shared_thread_data->write_error = true;
        goto end;
    }

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

end:
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
    bool append_authoringtool_data = (bool)getNspAppendAuthoringToolDataOption();
    bool success = false;

    u64 free_space = 0;
    u32 dev_idx = g_storageMenuElementOption.selected;

    u8 *buf = NULL;
    char *filename = NULL;
    FILE *fd = NULL;

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

    PartitionFileSystemFileContext pfs_file_ctx = {0};
    pfsInitializeFileContext(&pfs_file_ctx);

    char entry_name[64] = {0};
    u64 nsp_header_size = 0, nsp_size = 0, nsp_offset = 0;
    char *tmp_name = NULL;

    Sha256Context sha256_ctx = {0};
    u8 sha256_hash[SHA256_HASH_SIZE] = {0};

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
    if (append_authoringtool_data)
    {
        program_count = titleGetContentCountByType(title_info, NcmContentType_Program);
        if (program_count && !(program_info_ctx = calloc(program_count, sizeof(ProgramInfoContext))))
        {
            consolePrint("program info ctx calloc failed\n");
            goto end;
        }
    }

    // determine if we should initialize nacp ctx
    if (patch_sua || patch_screenshot || patch_video_capture || patch_hdcp || append_authoringtool_data)
    {
        control_count = titleGetContentCountByType(title_info, NcmContentType_Control);
        if (control_count && !(nacp_ctx = calloc(control_count, sizeof(NacpContext))))
        {
            consolePrint("nacp ctx calloc failed\n");
            goto end;
        }
    }

    // determine if we should initialize legalinfo ctx
    if (append_authoringtool_data)
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
        titleGetContentInfoByTypeAndIdOffset(title_info, NcmContentType_Meta, 0), title_info->version.value, &tik))
    {
        consolePrint("Meta nca initialize ctx failed\n");
        goto end;
    }

    consolePrint("Meta nca initialize ctx succeeded\n");

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
        if (!ncaInitializeContext(cur_nca_ctx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0), content_info, title_info->version.value, &tik))
        {
            consolePrint("%s #%u initialize nca ctx failed\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
            goto end;
        }

        consolePrint("%s #%u initialize nca ctx succeeded\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);

        // don't go any further with this nca if we can't access its fs data because it's pointless
        // TODO: add preload warning
        if (cur_nca_ctx->rights_id_available && !cur_nca_ctx->titlekey_retrieved)
        {
            j++;
            continue;
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
                    // don't proceed if we didn't allocate programinfo ctx or if we're dealing with a sparse layer
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

                    if (append_authoringtool_data && !nacpGenerateAuthoringToolXml(cur_nacp_ctx, title_info->version.value, cnmtGetRequiredTitleVersion(&cnmt_ctx)))
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
    if (append_authoringtool_data && !cnmtGenerateAuthoringToolXml(&cnmt_ctx, nca_ctx, title_info->content_count))
    {
        consolePrint("cnmt xml #1 failed\n");
        goto end;
    }

    bool retrieve_tik_cert = (!remove_titlekey_crypto && tik.size > 0);
    if (retrieve_tik_cert)
    {
        if (!(tik_common_block = tikGetCommonBlock(tik.data)))
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

        if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_nca_ctx->content_size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }

    // add cnmt xml info
    if (append_authoringtool_data)
    {
        sprintf(entry_name, "%s.cnmt.xml", meta_nca_ctx->content_id_str);
        if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cnmt_ctx.authoring_tool_xml_size, &(meta_nca_ctx->content_type_ctx_data_idx)))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }

    // add content type ctx data info
    u32 limit = append_authoringtool_data ? (title_info->content_count - 1) : 0;
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
                ret = pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_program_info_ctx->authoring_tool_xml_size, &(cur_nca_ctx->content_type_ctx_data_idx));
                break;
            }
            case NcmContentType_Control:
            {
                NacpContext *cur_nacp_ctx = (NacpContext*)cur_nca_ctx->content_type_ctx;

                for(u8 j = 0; j < cur_nacp_ctx->icon_count; j++)
                {
                    NacpIconContext *icon_ctx = &(cur_nacp_ctx->icon_ctx[j]);
                    sprintf(entry_name, "%s.nx.%s.jpg", cur_nca_ctx->content_id_str, nacpGetLanguageString(icon_ctx->language));
                    if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, icon_ctx->icon_size, j == 0 ? &(cur_nca_ctx->content_type_ctx_data_idx) : NULL))
                    {
                        consolePrint("pfs add entry failed: %s\n", entry_name);
                        goto end;
                    }
                }

                sprintf(entry_name, "%s.nacp.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_nacp_ctx->authoring_tool_xml_size, !cur_nacp_ctx->icon_count ? &(cur_nca_ctx->content_type_ctx_data_idx) : NULL);
                break;
            }
            case NcmContentType_LegalInformation:
            {
                LegalInfoContext *cur_legal_info_ctx = (LegalInfoContext*)cur_nca_ctx->content_type_ctx;
                sprintf(entry_name, "%s.legalinfo.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_legal_info_ctx->authoring_tool_xml_size, &(cur_nca_ctx->content_type_ctx_data_idx));
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
        if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, tik.size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }

        sprintf(entry_name, "%s.cert", tik.rights_id_str);
        if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, raw_cert_chain_size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }

    // write buffer to memory buffer
    if (!pfsWriteFileContextHeaderToMemoryBuffer(&pfs_file_ctx, buf, BLOCK_SIZE, &nsp_header_size))
    {
        consolePrint("pfs write header to mem #1 failed\n");
        goto end;
    }

    nsp_size = (nsp_header_size + pfs_file_ctx.fs_size);
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

        if (!(fd = fopen(filename, "wb")))
        {
            consolePrint("fopen failed\n");
            goto end;
        }

        // set file size
        ftruncate(fileno(fd), (off_t)nsp_size);

        // write placeholder header
        memset(buf, 0, nsp_header_size);
        fwrite(buf, 1, nsp_header_size, fd);
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

        memset(&sha256_ctx, 0, sizeof(Sha256Context));
        sha256ContextCreate(&sha256_ctx);

        if (cur_nca_ctx->content_type == NcmContentType_Meta && (!cnmtGenerateNcaPatch(&cnmt_ctx) || !ncaEncryptHeader(cur_nca_ctx)))
        {
            consolePrint("cnmt generate patch failed\n");
            goto end;
        }

        bool dirty_header = ncaIsHeaderDirty(cur_nca_ctx);

        if (dev_idx == 1)
        {
            tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, i);
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

            if (cancelled)
            {
                if (dev_idx == 1) usbCancelFileTransfer();
                goto end;
            }

            if ((cur_nca_ctx->content_size - offset) < blksize) blksize = (cur_nca_ctx->content_size - offset);

            // read nca chunk
            if (!ncaReadContentFile(cur_nca_ctx, buf, blksize, offset))
            {
                consolePrint("nca read failed at 0x%lX for \"%s\"\n", offset, cur_nca_ctx->content_id_str);
                goto end;
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

            // update hash calculation
            sha256ContextUpdate(&sha256_ctx, buf, blksize);

            // write nca chunk
            if (dev_idx == 1)
            {
                if (!usbSendFileData(buf, blksize))
                {
                    consolePrint("send file data failed\n");
                    goto end;
                }
            } else {
                fwrite(buf, 1, blksize, fd);
            }
        }

        // get hash
        sha256ContextGetHash(&sha256_ctx, sha256_hash);

        // update content id and hash
        ncaUpdateContentIdAndHash(cur_nca_ctx, sha256_hash);

        // update cnmt
        if (!cnmtUpdateContentInfo(&cnmt_ctx, cur_nca_ctx))
        {
            consolePrint("cnmt update content info failed\n");
            goto end;
        }

        // update pfs entry name
        if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, i, cur_nca_ctx->content_id_str))
        {
            consolePrint("pfs update entry name failed for nca \"%s\"\n", cur_nca_ctx->content_id_str);
            goto end;
        }
    }

    if (append_authoringtool_data)
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
            tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, meta_nca_ctx->content_type_ctx_data_idx);
            if (!usbSendFileProperties(cnmt_ctx.authoring_tool_xml_size, tmp_name) || !usbSendFileData(cnmt_ctx.authoring_tool_xml, cnmt_ctx.authoring_tool_xml_size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(cnmt_ctx.authoring_tool_xml, 1, cnmt_ctx.authoring_tool_xml_size, fd);
        }

        nsp_offset += cnmt_ctx.authoring_tool_xml_size;
        nsp_thread_data->data_written += cnmt_ctx.authoring_tool_xml_size;

        // update cnmt xml pfs entry name
        if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, meta_nca_ctx->content_type_ctx_data_idx, meta_nca_ctx->content_id_str))
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
                        tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, data_idx);
                        if (!usbSendFileProperties(icon_ctx->icon_size, tmp_name) || !usbSendFileData(icon_ctx->icon_data, icon_ctx->icon_size))
                        {
                            consolePrint("send \"%s\" failed\n", tmp_name);
                            goto end;
                        }
                    } else {
                        fwrite(icon_ctx->icon_data, 1, icon_ctx->icon_size, fd);
                    }

                    nsp_offset += icon_ctx->icon_size;
                    nsp_thread_data->data_written += icon_ctx->icon_size;

                    // update pfs entry name
                    if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, data_idx++, cur_nca_ctx->content_id_str))
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
            tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, data_idx);
            if (!usbSendFileProperties(authoring_tool_xml_size, tmp_name) || !usbSendFileData(authoring_tool_xml, authoring_tool_xml_size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(authoring_tool_xml, 1, authoring_tool_xml_size, fd);
        }

        nsp_offset += authoring_tool_xml_size;
        nsp_thread_data->data_written += authoring_tool_xml_size;

        // update pfs entry name
        if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, data_idx, cur_nca_ctx->content_id_str))
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
            tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, pfs_file_ctx.header.entry_count - 2);
            if (!usbSendFileProperties(tik.size, tmp_name) || !usbSendFileData(tik.data, tik.size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(tik.data, 1, tik.size, fd);
        }

        nsp_offset += tik.size;
        nsp_thread_data->data_written += tik.size;

        // write cert
        if (dev_idx == 1)
        {
            tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, pfs_file_ctx.header.entry_count - 1);
            if (!usbSendFileProperties(raw_cert_chain_size, tmp_name) || !usbSendFileData(raw_cert_chain, raw_cert_chain_size))
            {
                consolePrint("send \"%s\" failed\n", tmp_name);
                goto end;
            }
        } else {
            fwrite(raw_cert_chain, 1, raw_cert_chain_size, fd);
        }

        nsp_offset += raw_cert_chain_size;
        nsp_thread_data->data_written += raw_cert_chain_size;
    }

    // write new pfs0 header
    if (!pfsWriteFileContextHeaderToMemoryBuffer(&pfs_file_ctx, buf, BLOCK_SIZE, &nsp_header_size))
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
        rewind(fd);
        fwrite(buf, 1, nsp_header_size, fd);
    }

    nsp_thread_data->data_written += nsp_header_size;

    success = true;

end:
    consoleRefresh();

    mutexLock(&g_fileMutex);
    if (!success && !nsp_thread_data->transfer_cancelled) nsp_thread_data->error = true;
    mutexUnlock(&g_fileMutex);

    if (fd)
    {
        fclose(fd);

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

    pfsFreeFileContext(&pfs_file_ctx);

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

static u32 getNspAppendAuthoringToolDataOption(void)
{
    return (u32)configGetBoolean("nsp/append_authoringtool_data");
}

static void setNspAppendAuthoringToolDataOption(u32 idx)
{
    configSetBoolean("nsp/append_authoringtool_data", (bool)idx);
}

static u32 getTicketRemoveConsoleDataOption(void)
{
    return (u32)configGetBoolean("ticket/remove_console_data");
}

static void setTicketRemoveConsoleDataOption(u32 idx)
{
    configSetBoolean("ticket/remove_console_data", (bool)idx);
}
