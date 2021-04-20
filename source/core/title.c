/*
 * title.c
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

#include "nxdt_utils.h"
#include "title.h"
#include "gamecard.h"

#define NS_APPLICATION_RECORD_LIMIT 4096

/* Type definitions. */

typedef struct {
    u64 title_id;
    char name[32];
} TitleSystemEntry;

/* Global variables. */

static Mutex g_titleMutex = 0;
static Thread g_titleGameCardInfoThread = {0};
static UEvent g_titleGameCardInfoThreadExitEvent = {0}, *g_titleGameCardStatusChangeUserEvent = NULL, g_titleGameCardUpdateInfoUserEvent = {0};
static CondVar g_gameCardCondVar = 0;

static bool g_titleInterfaceInit = false, g_titleGameCardInfoThreadCreated = false, g_titleGameCardAvailable = false, g_titleGameCardInfoUpdated = false;

static NsApplicationControlData *g_nsAppControlData = NULL;

static TitleApplicationMetadata **g_appMetadata = NULL;
static u32 g_appMetadataCount = 0;

static NcmContentMetaDatabase g_ncmDbGameCard = {0}, g_ncmDbEmmcSystem = {0}, g_ncmDbEmmcUser = {0}, g_ncmDbSdCard = {0};
static NcmContentStorage g_ncmStorageGameCard = {0}, g_ncmStorageEmmcSystem = {0}, g_ncmStorageEmmcUser = {0}, g_ncmStorageSdCard = {0};

static TitleInfo **g_titleInfo = NULL;
static u32 g_titleInfoCount = 0;

static u32 g_titleInfoBuiltInSystemCount = 0;  /* Start index is always zero. */
static u32 g_titleInfoBuiltInUserStartIndex = 0, g_titleInfoBuiltInUserCount = 0;
static u32 g_titleInfoSdCardStartIndex = 0, g_titleInfoSdCardCount = 0;
static u32 g_titleInfoGameCardStartIndex = 0, g_titleInfoGameCardCount = 0;

static TitleInfo **g_orphanTitleInfo = NULL;
static u32 g_orphanTitleInfoCount = 0;

static const char *g_titleNcmContentTypeNames[] = {
    [NcmContentType_Meta]              = "Meta",
    [NcmContentType_Program]           = "Program",
    [NcmContentType_Data]              = "Data",
    [NcmContentType_Control]           = "Control",
    [NcmContentType_HtmlDocument]      = "HtmlDocument",
    [NcmContentType_LegalInformation]  = "LegalInformation",
    [NcmContentType_DeltaFragment]     = "DeltaFragment"
};

static const char *g_titleNcmContentMetaTypeNames[] = {
    [NcmContentMetaType_Unknown]              = "Unknown",
    [NcmContentMetaType_SystemProgram]        = "SystemProgram",
    [NcmContentMetaType_SystemData]           = "SystemData",
    [NcmContentMetaType_SystemUpdate]         = "SystemUpdate",
    [NcmContentMetaType_BootImagePackage]     = "BootImagePackage",
    [NcmContentMetaType_BootImagePackageSafe] = "BootImagePackageSafe",
    [NcmContentMetaType_Application - 0x7A]   = "Application",
    [NcmContentMetaType_Patch - 0x7A]         = "Patch",
    [NcmContentMetaType_AddOnContent - 0x7A]  = "AddOnContent",
    [NcmContentMetaType_Delta - 0x7A]         = "Delta"
};

static const char *g_filenameTypeStrings[] = {
    [NcmContentMetaType_Application - 0x80]  = "BASE",
    [NcmContentMetaType_Patch - 0x80]        = "UPD",
    [NcmContentMetaType_AddOnContent - 0x80] = "DLC",
    [NcmContentMetaType_Delta - 0x80]        = "DELTA"
};

/* Info retrieved from https://switchbrew.org/wiki/Title_list. */
/* Titles bundled with the kernel are excluded. */
static const TitleSystemEntry g_systemTitles[] = {
    /* System modules. */
    /* Meta + Program NCAs. */
    { 0x0100000000000006, "usb" },
    { 0x0100000000000007, "tma" },
    { 0x0100000000000008, "boot2" },
    { 0x0100000000000009, "settings" },
    { 0x010000000000000A, "bus" },
    { 0x010000000000000B, "bluetooth" },
    { 0x010000000000000C, "bcat" },
    { 0x010000000000000D, "dmnt" },
    { 0x010000000000000E, "friends" },
    { 0x010000000000000F, "nifm" },
    { 0x0100000000000010, "ptm" },
    { 0x0100000000000011, "shell" },
    { 0x0100000000000012, "bsdsockets" },
    { 0x0100000000000013, "hid" },
    { 0x0100000000000014, "audio" },
    { 0x0100000000000015, "LogManager" },
    { 0x0100000000000016, "wlan" },
    { 0x0100000000000017, "cs" },
    { 0x0100000000000018, "ldn" },
    { 0x0100000000000019, "nvservices" },
    { 0x010000000000001A, "pcv" },
    { 0x010000000000001B, "capmtp" },
    { 0x010000000000001C, "nvnflinger" },
    { 0x010000000000001D, "pcie" },
    { 0x010000000000001E, "account" },
    { 0x010000000000001F, "ns" },
    { 0x0100000000000020, "nfc" },
    { 0x0100000000000021, "psc" },
    { 0x0100000000000022, "capsrv" },
    { 0x0100000000000023, "am" },
    { 0x0100000000000024, "ssl" },
    { 0x0100000000000025, "nim" },
    { 0x0100000000000026, "cec" },
    { 0x0100000000000027, "tspm" },
    { 0x0100000000000029, "lbl" },
    { 0x010000000000002A, "btm" },
    { 0x010000000000002B, "erpt" },
    { 0x010000000000002C, "time" },
    { 0x010000000000002D, "vi" },
    { 0x010000000000002E, "pctl" },
    { 0x010000000000002F, "npns" },
    { 0x0100000000000030, "eupld" },
    { 0x0100000000000031, "glue" },
    { 0x0100000000000032, "eclct" },
    { 0x0100000000000033, "es" },
    { 0x0100000000000034, "fatal" },
    { 0x0100000000000035, "grc" },
    { 0x0100000000000036, "creport" },
    { 0x0100000000000037, "ro" },
    { 0x0100000000000038, "profiler" },
    { 0x0100000000000039, "sdb" },
    { 0x010000000000003A, "migration" },
    { 0x010000000000003B, "jit" },
    { 0x010000000000003C, "jpegdec" },
    { 0x010000000000003D, "safemode" },
    { 0x010000000000003E, "olsc" },
    { 0x010000000000003F, "dt" },
    { 0x0100000000000040, "nd" },
    { 0x0100000000000041, "ngct" },
    { 0x0100000000000042, "pgl" },
    
    /* System data archives. */
    /* Meta + Data NCAs. */
    { 0x0100000000000800, "CertStore" },
    { 0x0100000000000801, "ErrorMessage" },
    { 0x0100000000000802, "MiiModel" },
    { 0x0100000000000803, "BrowserDll" },
    { 0x0100000000000804, "Help" },
    { 0x0100000000000805, "SharedFont" },
    { 0x0100000000000806, "NgWord" },
    { 0x0100000000000807, "SsidList" },
    { 0x0100000000000808, "Dictionary" },
    { 0x0100000000000809, "SystemVersion" },
    { 0x010000000000080A, "AvatarImage" },
    { 0x010000000000080B, "LocalNews" },
    { 0x010000000000080C, "Eula" },
    { 0x010000000000080D, "UrlBlackList" },
    { 0x010000000000080E, "TimeZoneBinary" },
    { 0x010000000000080F, "CertStoreCruiser" },
    { 0x0100000000000810, "FontNintendoExtension" },
    { 0x0100000000000811, "FontStandard" },
    { 0x0100000000000812, "FontKorean" },
    { 0x0100000000000813, "FontChineseTraditional" },
    { 0x0100000000000814, "FontChineseSimple" },
    { 0x0100000000000815, "FontBfcpx" },
    { 0x0100000000000816, "SystemUpdate" },
    { 0x0100000000000818, "FirmwareDebugSettings" },
    { 0x0100000000000819, "BootImagePackage" },
    { 0x010000000000081A, "BootImagePackageSafe" },
    { 0x010000000000081B, "BootImagePackageExFat" },
    { 0x010000000000081C, "BootImagePackageExFatSafe" },
    { 0x010000000000081D, "FatalMessage" },
    { 0x010000000000081E, "ControllerIcon" },
    { 0x010000000000081F, "PlatformConfigIcosa" },
    { 0x0100000000000820, "PlatformConfigCopper" },
    { 0x0100000000000821, "PlatformConfigHoag" },
    { 0x0100000000000822, "ControllerFirmware" },
    { 0x0100000000000823, "NgWord2" },
    { 0x0100000000000824, "PlatformConfigIcosaMariko" },
    { 0x0100000000000825, "ApplicationBlackList" },
    { 0x0100000000000826, "RebootlessSystemUpdateVersion" },
    { 0x0100000000000827, "ContentActionTable" },
    { 0x0100000000000828, "FunctionBlackList" },
    { 0x0100000000000829, "PlatformConfigCalcio" },
    { 0x0100000000000830, "NgWordT" },
    { 0x0100000000000831, "PlatformConfigAula" },
    { 0x0100000000000832, "CradleFirmwareAula" },   ///< Placeholder.
    
    /* System applets. */
    /* Meta + Program NCAs. */
    { 0x0100000000001000, "qlaunch" },
    { 0x0100000000001001, "auth" },
    { 0x0100000000001002, "cabinet" },
    { 0x0100000000001003, "controller" },
    { 0x0100000000001004, "dataErase" },
    { 0x0100000000001005, "error" },
    { 0x0100000000001006, "netConnect" },
    { 0x0100000000001007, "playerSelect" },
    { 0x0100000000001008, "swkbd" },
    { 0x0100000000001009, "miiEdit" },
    { 0x010000000000100A, "web" },
    { 0x010000000000100B, "shop" },
    { 0x010000000000100C, "overlayDisp" },
    { 0x010000000000100D, "photoViewer" },
    { 0x010000000000100E, "set" },
    { 0x010000000000100F, "offlineWeb" },
    { 0x0100000000001010, "loginShare" },
    { 0x0100000000001011, "wifiWebAuth" },
    { 0x0100000000001012, "starter" },
    { 0x0100000000001013, "myPage" },
    { 0x0100000000001014, "PlayReport" },
    { 0x0100000000001015, "MaintenanceMenu" },
    { 0x010000000000101A, "gift" },
    { 0x010000000000101B, "DummyECApplet" },
    { 0x010000000000101C, "userMigration" },
    { 0x010000000000101D, "EncounterSys" },
    { 0x0100000000001020, "story" },
    { 0x0100000000001023, "statistics" },
    { 0x0100000000001033, "promotion" },
    { 0x0100000000001038, "sample" },
    { 0x0100000000001FFF, "EndOceanProgramId" },
    
    /* System debug applets. */
    { 0x0100000000002000, "A2BoardFunction" },
    { 0x0100000000002001, "A3Wireless" },
    { 0x0100000000002002, "C1LcdAndKey" },
    { 0x0100000000002003, "C2UsbHpmic" },
    { 0x0100000000002004, "C3Aging" },
    { 0x0100000000002005, "C4SixAxis" },
    { 0x0100000000002006, "C5Wireless" },
    { 0x0100000000002007, "C7FinalCheck" },
    { 0x010000000000203F, "AutoCapture" },
    { 0x0100000000002040, "DevMenuCommandSystem" },
    { 0x0100000000002041, "recovery" },
    { 0x0100000000002042, "DevMenuSystem" },
    { 0x0100000000002044, "HB-TBIntegrationTest" },
    { 0x010000000000204D, "BackupSaveData" },
    { 0x010000000000204E, "A4BoardCalWriti" },
    { 0x0100000000002054, "RepairSslCertificate" },
    { 0x0100000000002055, "GameCardWriter" },
    { 0x0100000000002056, "UsbPdTestTool" },
    { 0x0100000000002057, "RepairDeletePctl" },
    { 0x0100000000002058, "RepairBackup" },
    { 0x0100000000002059, "RepairRestore" },
    { 0x010000000000205A, "RepairAccountTransfer" },
    { 0x010000000000205B, "RepairAutoNetworkUpdater" },
    { 0x010000000000205C, "RefurbishReset" },
    { 0x010000000000205D, "RepairAssistCup" },
    { 0x010000000000205E, "RepairPairingCutter" },
    { 0x0100000000002064, "DevMenu" },
    { 0x0100000000002065, "DevMenuApp" },
    { 0x0100000000002066, "GetGameCardAsicInfo" },
    { 0x0100000000002068, "NfpDebugToolSystem" },
    { 0x0100000000002069, "AlbumSynchronizer" },
    { 0x0100000000002071, "SnapShotDumper" },
    { 0x0100000000002073, "DevMenuSystemApp" },
    { 0x0100000000002099, "DevOverlayDisp" },
    { 0x010000000000209A, "NandVerifier" },
    { 0x010000000000209B, "GpuCoreDumper" },
    { 0x010000000000209C, "TestApplication" },
    { 0x010000000000209E, "HelloWorld" },
    { 0x01000000000020A0, "XcieWriter" },
    { 0x01000000000020A1, "GpuOverrunNotifier" },
    { 0x01000000000020C8, "NfpDebugTool" },
    { 0x01000000000020CA, "NoftWriter" },
    { 0x01000000000020D0, "BcatSystemDebugTool" },
    { 0x01000000000020D1, "DevSafeModeUpdater" },
    { 0x01000000000020D3, "ControllerConnectionAnalyzer" },
    { 0x01000000000020D4, "DevKitUpdater" },
    { 0x01000000000020D6, "RepairTimeReviser" },
    { 0x01000000000020D7, "RepairReinitializeFuelGauge" },
    { 0x01000000000020DA, "RepairAbortMigration" },
    { 0x01000000000020DC, "RepairShowDeviceId" },
    { 0x01000000000020DD, "RepairSetCycleCountReliability" },
    { 0x01000000000020E0, "Interface" },
    { 0x01000000000020E1, "AlbumDownloader" },
    { 0x01000000000020E3, "FuelGaugeDumper" },
    { 0x01000000000020E4, "UnsafeExtract" },
    { 0x01000000000020E5, "UnsafeEngrave" },
    { 0x01000000000020EE, "BluetoothSettingTool" },
    { 0x01000000000020F0, "ApplicationInstallerRomfs" },
    { 0x0100000000002100, "DevMenuLotcheckDownloader" },
    { 0x0100000000002101, "DevMenuCommand" },
    { 0x0100000000002102, "ExportPartition" },
    { 0x0100000000002103, "SystemInitializer" },
    { 0x0100000000002104, "SystemUpdaterHostFs" },
    { 0x0100000000002105, "WriteToStorage" },
    { 0x0100000000002106, "CalWriter" },
    { 0x0100000000002107, "SettingsManager" },
    { 0x0100000000002109, "testBuildSystemIris" },
    { 0x010000000000210A, "SystemUpdater" },
    { 0x010000000000210B, "nvnflinger_util" },
    { 0x010000000000210C, "ControllerFirmwareUpdater" },
    { 0x010000000000210D, "testBuildSystemNintendoWare" },
    { 0x0100000000002110, "TestSaveDataCreator" },
    { 0x0100000000002111, "C9LcdSpker" },
    { 0x0100000000002114, "RankTurn" },
    { 0x0100000000002116, "BleTestTool" },
    { 0x010000000000211A, "PreinstallAppWriter" },
    { 0x010000000000211C, "ControllerSerialFlashTool" },
    { 0x010000000000211D, "ControllerFlashWriter" },
    { 0x0100000000002120, "ControllerTestApp" },
    { 0x0100000000002121, "HidInspectionTool" },
    { 0x0100000000002124, "BatteryCyclesEditor" },
    { 0x0100000000002125, "UsbFirmwareUpdater" },
    { 0x0100000000002126, "PalmaSerialCodeTool" },
    { 0x0100000000002127, "renderdoccmd" },
    { 0x0100000000002128, "HidInspectionToolProd" },
    { 0x010000000000212C, "ExhibitionMenu" },
    { 0x010000000000212F, "ExhibitionSaveData" },
    { 0x0100000000002130, "LuciaConverter" },
    { 0x0100000000002133, "CalDumper" },
    { 0x0100000000002134, "AnalogStickEvaluationTool" },
    
    /* System debug modules. */
    { 0x0100000000003002, "DummyProcess" },
    { 0x0100000000003003, "DebugMonitor0" },
    { 0x0100000000003004, "SystemHelloWorld" },
    
    /* Target tools. */
    { 0x1000000000000001, "SystemInitializer" },
    { 0x1000000000000004, "CalWriter" },
    { 0x1000000000000005, "DevMenuCommand" },
    { 0x1000000000000006, "SettingsManager" },
    { 0x1000000000000007, "ApplicationLauncer" },
    { 0x100000000000000B, "SnapShotDumper" },
    { 0x100000000000000C, "SystemUpdater" },
    { 0x100000000000000E, "ControllerFirmwareUpdater" },
    
    /* Factory system modules. */
    { 0x010000000000B120, "nvdbgsvc" },
    { 0x010000000000B14A, "manu" },
    { 0x010000000000B14B, "ManuUsbLoopBack" },
    { 0x010000000000B1B8, "DevFwdbgHbPackage" },
    { 0x010000000000B1B9, "DevFwdbgUsbPackage" },
    { 0x010000000000B1BA, "ProdFwdbgPackage" },
    { 0x010000000000B22A, "scs" },
    { 0x010000000000B22B, "ControllerFirmwareDebug" },
    { 0x010000000000B240, "htc.stub" },
    { 0x010000000000C600, "BdkSample01" },
    { 0x010000000000C601, "BdkSample02" },
    { 0x010000000000C602, "BdkSample03" },
    { 0x010000000000C603, "BdkSample04" },
    { 0x010000000000D609, "dmnt.gen2" },
    { 0x010000000000D623, "DevServer" },
    
    /* System applications. */
    { 0x01008BB00013C000, "flog" },
    { 0x0100069000078000, "RetailInteractiveDisplayMenu" },
    { 0x010000B003486000, "AudioUsbMicDebugTool" },
    { 0x0100458001E04000, "BcatTestApp01" },
    { 0x0100F910020F8000, "BcatTestApp02" },
    { 0x0100B7D0020FC000, "BcatTestApp03" },
    { 0x0100132002100000, "BcatTestApp04" },
    { 0x0100935002116000, "BcatTestApp05" },
    { 0x0100DA4002130000, "BcatTestApp06" },
    { 0x0100B0F002104000, "BcatTestApp07" },
    { 0x010051E002132000, "BcatTestApp08" },
    { 0x01004CB0015C8000, "BcatTestApp09" },
    { 0x01009720015CA000, "BcatTestApp10" },
    { 0x01002F20015C6000, "BcatTestApp11" },
    { 0x0100204001F90000, "BcatTestApp12" },
    { 0x0100060001F92000, "BcatTestApp13" },
    { 0x0100C26001F94000, "BcatTestApp14" },
    { 0x0100462001F96000, "BcatTestApp15" },
    { 0x01005C6001F98000, "BcatTestApp16" },
    { 0x010070000E3C0000, "EncounterUsr" },
    { 0x010086000E49C000, "EncounterUsrDummy" },
    { 0x0100810002D5A000, "ShopMonitaringTool" },
    { 0x010023D002B98000, "DeltaStress" }
};

static const u32 g_systemTitlesCount = MAX_ELEMENTS(g_systemTitles);

/* Function prototypes. */

NX_INLINE void titleFreeApplicationMetadata(void);
static bool titleReallocateApplicationMetadata(u32 extra_app_count, bool free_entries);

NX_INLINE void titleFreeTitleInfo(void);
static bool titleReallocateTitleInfo(u32 extra_title_count, bool free_entries);

NX_INLINE void titleFreeOrphanTitleInfo(void);
static void titleAddOrphanTitleInfoEntry(TitleInfo *orphan_title);

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id);

static bool titleGenerateMetadataEntriesFromSystemTitles(void);
static bool titleGenerateMetadataEntriesFromNsRecords(void);
static bool titleRetrieveApplicationMetadataByTitleId(u64 title_id, TitleApplicationMetadata *out);

static bool titleOpenNcmDatabases(void);
static void titleCloseNcmDatabases(void);

static bool titleOpenNcmStorages(void);
static void titleCloseNcmStorages(void);

static bool titleOpenNcmDatabaseAndStorageFromGameCard(void);
static void titleCloseNcmDatabaseAndStorageFromGameCard(void);

static bool titleLoadPersistentStorageTitleInfo(void);
static bool titleGenerateTitleInfoFromStorage(u8 storage_id);
static bool titleGetContentInfosFromTitle(u8 storage_id, const NcmContentMetaKey *meta_key, NcmContentInfo **out_content_infos, u32 *out_content_count);
static void titleUpdateTitleInfoLinkedLists(void);

static bool titleCreateGameCardInfoThread(void);
static void titleDestroyGameCardInfoThread(void);
static void titleGameCardInfoThreadFunc(void *arg);

static bool titleRefreshGameCardTitleInfo(void);
static void titleRemoveGameCardTitleInfoEntries(void);

static bool titleIsUserApplicationContentAvailable(u64 app_id);
static TitleInfo *_titleGetInfoFromStorageByTitleId(u8 storage_id, u64 title_id, bool lock);

static int titleSystemTitleMetadataEntrySortFunction(const void *a, const void *b);
static int titleUserApplicationMetadataEntrySortFunction(const void *a, const void *b);
static int titleOrphanTitleInfoSortFunction(const void *a, const void *b);

bool titleInitialize(void)
{
    mutexLock(&g_titleMutex);
    
    bool ret = g_titleInterfaceInit;
    if (ret) goto end;
    
    /* Allocate memory for the ns application control data. */
    /* This will be used each time we need to retrieve the metadata from an application. */
    g_nsAppControlData = calloc(1, sizeof(NsApplicationControlData));
    if (!g_nsAppControlData)
    {
        LOG_MSG("Failed to allocate memory for the ns application control data!");
        goto end;
    }
    
    /* Generate application metadata entries from hardcoded system titles, since we can't retrieve their names via ns. */
    if (!titleGenerateMetadataEntriesFromSystemTitles())
    {
        LOG_MSG("Failed to generate application metadata from hardcoded system titles!");
        goto end;
    }
    
    /* Generate application metadata entries from ns records. */
    /* Theoretically speaking, we should only need to do this once. */
    /* However, if any new gamecard is inserted while the application is running, we *will* have to retrieve the metadata from its application(s). */
    if (!titleGenerateMetadataEntriesFromNsRecords())
    {
        LOG_MSG("Failed to generate application metadata from ns records!");
        goto end;
    }
    
    /* Open eMMC System, eMMC User and SD card ncm databases. */
    if (!titleOpenNcmDatabases())
    {
        LOG_MSG("Failed to open ncm databases!");
        goto end;
    }
    
    /* Open eMMC System, eMMC User and SD card ncm storages. */
    if (!titleOpenNcmStorages())
    {
        LOG_MSG("Failed to open ncm storages!");
        goto end;
    }
    
    /* Load title info by retrieving content meta keys from available eMMC System, eMMC User and SD card titles. */
    if (!titleLoadPersistentStorageTitleInfo())
    {
        LOG_MSG("Failed to load persistent storage title info!");
        goto end;
    }
    
    /* Create user-mode exit event. */
    ueventCreate(&g_titleGameCardInfoThreadExitEvent, true);
    
    /* Retrieve gamecard status change user event. */
    g_titleGameCardStatusChangeUserEvent = gamecardGetStatusChangeUserEvent();
    if (!g_titleGameCardStatusChangeUserEvent)
    {
        LOG_MSG("Failed to retrieve gamecard status change user event!");
        goto end;
    }
    
    /* Create user-mode gamecard update info event. */
    ueventCreate(&g_titleGameCardUpdateInfoUserEvent, true);
    
    /* Create gamecard title info thread. */
    if (!(g_titleGameCardInfoThreadCreated = titleCreateGameCardInfoThread())) goto end;
    
    ret = g_titleInterfaceInit = true;
    
end:
    mutexUnlock(&g_titleMutex);
    
    return ret;
}

void titleExit(void)
{
    mutexLock(&g_titleMutex);
    
    /* Destroy gamecard detection thread. */
    if (g_titleGameCardInfoThreadCreated)
    {
        titleDestroyGameCardInfoThread();
        g_titleGameCardInfoThreadCreated = false;
    }
    
    /* Free title info. */
    titleFreeTitleInfo();
    
    /* Close gamecard ncm database and storage (if needed). */
    titleCloseNcmDatabaseAndStorageFromGameCard();
    
    /* Close eMMC System, eMMC User and SD card ncm storages. */
    titleCloseNcmStorages();
    
    /* Close eMMC System, eMMC User and SD card ncm databases. */
    titleCloseNcmDatabases();
    
    /* Free application metadata. */
    titleFreeApplicationMetadata();
    
    /* Free ns application control data. */
    if (g_nsAppControlData) free(g_nsAppControlData);
    
    g_titleInterfaceInit = false;
    
    mutexUnlock(&g_titleMutex);
}

NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    switch(storage_id)
    {
        case NcmStorageId_GameCard:
            ncm_db = &g_ncmDbGameCard;
            break;
        case NcmStorageId_BuiltInSystem:
            ncm_db = &g_ncmDbEmmcSystem;
            break;
        case NcmStorageId_BuiltInUser:
            ncm_db = &g_ncmDbEmmcUser;
            break;
        case NcmStorageId_SdCard:
            ncm_db = &g_ncmDbSdCard;
            break;
        default:
            break;
    }
    
    return ncm_db;
}

NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id)
{
    NcmContentStorage *ncm_storage = NULL;
    
    switch(storage_id)
    {
        case NcmStorageId_GameCard:
            ncm_storage = &g_ncmStorageGameCard;
            break;
        case NcmStorageId_BuiltInSystem:
            ncm_storage = &g_ncmStorageEmmcSystem;
            break;
        case NcmStorageId_BuiltInUser:
            ncm_storage = &g_ncmStorageEmmcUser;
            break;
        case NcmStorageId_SdCard:
            ncm_storage = &g_ncmStorageSdCard;
            break;
        default:
            break;
    }
    
    return ncm_storage;
}

TitleApplicationMetadata **titleGetApplicationMetadataEntries(bool is_system, u32 *out_count)
{
    mutexLock(&g_titleMutex);
    
    u32 start_idx = (is_system ? 0 : g_systemTitlesCount);
    u32 max_val = (is_system ? g_systemTitlesCount : g_appMetadataCount);
    
    u32 app_count = 0;
    TitleApplicationMetadata **app_metadata = NULL, **tmp_app_metadata = NULL;
    
    if (!g_titleInterfaceInit || !g_appMetadata || !*g_appMetadata || (is_system && g_appMetadataCount < g_systemTitlesCount) || (!is_system && g_appMetadataCount == g_systemTitlesCount) || !out_count)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    for(u32 i = start_idx; i < max_val; i++)
    {
        TitleApplicationMetadata *cur_app_metadata = g_appMetadata[i];
        if (!cur_app_metadata) continue;
        
        /* Skip current metadata entry if content data for this title isn't available. */
        if ((is_system && !_titleGetInfoFromStorageByTitleId(NcmStorageId_BuiltInSystem, cur_app_metadata->title_id, false)) || \
            (!is_system && !titleIsUserApplicationContentAvailable(cur_app_metadata->title_id))) continue;
        
        /* Reallocate application metadata pointer array. */
        tmp_app_metadata = realloc(app_metadata, (app_count + 1) * sizeof(TitleApplicationMetadata*));
        if (!tmp_app_metadata)
        {
            LOG_MSG("Failed to reallocate application metadata pointer array!");
            if (app_metadata) free(app_metadata);
            app_metadata = NULL;
            goto end;
        }
        
        app_metadata = tmp_app_metadata;
        tmp_app_metadata = NULL;
        
        /* Set current pointer and increase counter. */
        app_metadata[app_count++] = cur_app_metadata;
    }
    
    /* Update output counter. */
    *out_count = app_count;
    
    if (!app_metadata || !app_count) LOG_MSG("No content data found for %s!", is_system ? "system titles" : "user applications");
    
end:
    mutexUnlock(&g_titleMutex);
    
    return app_metadata;
}

TitleInfo *titleGetInfoFromStorageByTitleId(u8 storage_id, u64 title_id)
{
    return _titleGetInfoFromStorageByTitleId(storage_id, title_id, true);
}

bool titleGetUserApplicationData(u64 app_id, TitleUserApplicationData *out)
{
    mutexLock(&g_titleMutex);
    
    bool success = false;
    
    if (!g_titleInterfaceInit || !g_titleInfo || !*g_titleInfo || !g_titleInfoCount || !app_id || !out)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    /* Clear output. */
    memset(out, 0, sizeof(TitleUserApplicationData));
    
    /* Get first user application title info. */
    out->app_info = _titleGetInfoFromStorageByTitleId(NcmStorageId_Any, app_id, false);
    
    /* Get first patch title info. */
    out->patch_info = _titleGetInfoFromStorageByTitleId(NcmStorageId_Any, titleGetPatchIdByApplicationId(app_id), false);
    
    /* Get first add-on content title info. */
    for(u32 i = g_titleInfoBuiltInUserStartIndex; i < g_titleInfoCount; i++)
    {
        TitleInfo *title_info = g_titleInfo[i];
        if (title_info && title_info->meta_key.type == NcmContentMetaType_AddOnContent && titleCheckIfAddOnContentIdBelongsToApplicationId(app_id, title_info->meta_key.id))
        {
            out->aoc_info = title_info;
            break;
        }
    }
    
    /* Check retrieved title info. */
    success = (out->app_info || out->patch_info || out->aoc_info);
    if (!success)
    {
        LOG_MSG("Failed to retrieve user application data for ID \"%016lX\"!", app_id);
        goto end;
    }
    
end:
    mutexUnlock(&g_titleMutex);
    
    return success;
}

bool titleAreOrphanTitlesAvailable(void)
{
    mutexLock(&g_titleMutex);
    bool ret = (g_titleInterfaceInit && g_orphanTitleInfo && *g_orphanTitleInfo && g_orphanTitleInfoCount > 0);
    mutexUnlock(&g_titleMutex);
    return ret;
}

TitleInfo **titleGetInfoFromOrphanTitles(u32 *out_count)
{
    mutexLock(&g_titleMutex);
    
    TitleInfo **orphan_info = NULL;
    
    if (!g_titleInterfaceInit || !g_titleInfo || !*g_titleInfo || !g_titleInfoCount || !g_orphanTitleInfo || !*g_orphanTitleInfo || !g_orphanTitleInfoCount || !out_count)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    /* Allocate orphan title info pointer array. */
    orphan_info = calloc(g_orphanTitleInfoCount, sizeof(TitleInfo*));
    if (!orphan_info)
    {
        LOG_MSG("Failed to allocate memory for orphan title info pointer array!");
        goto end;
    }
    
    /* Get pointers to orphan title info entries. */
    for(u32 i = 0; i < g_orphanTitleInfoCount; i++) orphan_info[i] = g_orphanTitleInfo[i];
    
    /* Update output counter. */
    *out_count = g_orphanTitleInfoCount;
    
end:
    mutexUnlock(&g_titleMutex);
    
    return orphan_info;
}

bool titleIsGameCardInfoUpdated(void)
{
    mutexLock(&g_titleMutex);
    
    /* Check if the gamecard thread detected a gamecard status change. */
    bool ret = (g_titleInterfaceInit && g_titleGameCardInfoThreadCreated && g_titleGameCardInfoUpdated);
    if (!ret) goto end;
    
    /* Signal the gamecard update info user event. */
    ueventSignal(&g_titleGameCardUpdateInfoUserEvent);
    
    /* Wait for the gamecard thread to wake us up. */
    condvarWait(&g_gameCardCondVar, &g_titleMutex);
    
    /* Update output value and gamecard info updated flag (if needed). */
    ret = g_titleGameCardInfoUpdated;
    if (ret) g_titleGameCardInfoUpdated = false;
    
end:
    mutexUnlock(&g_titleMutex);
    return ret;
}

char *titleGenerateFileName(const TitleInfo *title_info, u8 name_convention, u8 illegal_char_replace_type)
{
    mutexLock(&g_titleMutex);
    
    char *filename = NULL;
    char title_name[0x400] = {0};
    
    if (!title_info || title_info->meta_key.type < NcmContentMetaType_Application || title_info->meta_key.type > NcmContentMetaType_Delta || name_convention > TitleFileNameConvention_IdAndVersionOnly || \
        (name_convention == TitleFileNameConvention_Full && illegal_char_replace_type > TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly))
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    u8 type = (title_info->meta_key.type - 0x80);
    
    /* Generate filename for this title. */
    if (name_convention == TitleFileNameConvention_Full)
    {
        if (title_info->app_metadata && *(title_info->app_metadata->lang_entry.name))
        {
            sprintf(title_name, "%s ", title_info->app_metadata->lang_entry.name);
            if (illegal_char_replace_type) utilsReplaceIllegalCharacters(title_name, illegal_char_replace_type == TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly);
        }
        
        sprintf(title_name + strlen(title_name), "[%016lX][v%u][%s]", title_info->meta_key.id, title_info->meta_key.version, g_filenameTypeStrings[type]);
    } else
    if (name_convention == TitleFileNameConvention_IdAndVersionOnly)
    {
        sprintf(title_name, "%016lX_v%u_%s", title_info->meta_key.id, title_info->meta_key.version, g_filenameTypeStrings[type]);
    }
    
    /* Duplicate generated filename. */
    filename = strdup(title_name);
    if (!filename) LOG_MSG("Failed to duplicate generated filename!");
    
end:
    mutexUnlock(&g_titleMutex);
    
    return filename;
}

char *titleGenerateGameCardFileName(u8 name_convention, u8 illegal_char_replace_type)
{
    mutexLock(&g_titleMutex);
    
    size_t cur_filename_len = 0;
    char *filename = NULL, *tmp_filename = NULL;
    char app_name[0x400] = {0};
    
    if (!g_titleInterfaceInit || !g_titleInfo || !*g_titleInfo || !g_titleInfoCount || !g_titleGameCardAvailable || !g_titleInfoGameCardCount || \
        name_convention > TitleFileNameConvention_IdAndVersionOnly || (name_convention == TitleFileNameConvention_Full && \
        illegal_char_replace_type > TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly))
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    for(u32 i = g_titleInfoGameCardStartIndex; i < g_titleInfoCount; i++)
    {
        TitleInfo *app_info = g_titleInfo[i];
        if (!app_info || app_info->meta_key.type != NcmContentMetaType_Application) continue;
        
        u32 app_version = app_info->meta_key.version;
        
        /* Check if the inserted gamecard holds any bundled patches for the current user application. */
        /* If so, we'll use the highest patch version available as part of the filename. */
        for(u32 j = g_titleInfoGameCardStartIndex; j < g_titleInfoCount; j++)
        {
            if (j == i) continue;
            
            TitleInfo *patch_info = g_titleInfo[j];
            if (!patch_info || patch_info->meta_key.type != NcmContentMetaType_Patch || !titleCheckIfPatchIdBelongsToApplicationId(app_info->meta_key.id, patch_info->meta_key.id) || \
                patch_info->meta_key.version <= app_version) continue;
            
            app_version = patch_info->meta_key.version;
        }
        
        /* Generate current user application name. */
        *app_name = '\0';
        
        if (name_convention == TitleFileNameConvention_Full)
        {
            if (cur_filename_len) strcat(app_name, " + ");
            
            if (app_info->app_metadata && *(app_info->app_metadata->lang_entry.name))
            {
                sprintf(app_name + strlen(app_name), "%s ", app_info->app_metadata->lang_entry.name);
                if (illegal_char_replace_type) utilsReplaceIllegalCharacters(app_name, illegal_char_replace_type == TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly);
            }
            
            sprintf(app_name + strlen(app_name), "[%016lX][v%u]", app_info->meta_key.id, app_version);
        } else
        if (name_convention == TitleFileNameConvention_IdAndVersionOnly)
        {
            if (cur_filename_len) strcat(app_name, "+");
            sprintf(app_name + strlen(app_name), "%016lX_v%u", app_info->meta_key.id, app_version);
        }
        
        /* Reallocate output buffer. */
        size_t app_name_len = strlen(app_name);
        
        tmp_filename = realloc(filename, (cur_filename_len + app_name_len + 1) * sizeof(char));
        if (!tmp_filename)
        {
            LOG_MSG("Failed to reallocate filename buffer!");
            if (filename) free(filename);
            filename = NULL;
            goto end;
        }
        
        filename = tmp_filename;
        tmp_filename = NULL;
        
        /* Concatenate current user application name. */
        filename[cur_filename_len] = '\0';
        strcat(filename, app_name);
        cur_filename_len += app_name_len;
    }
    
    if (!filename) LOG_MSG("Error: the inserted gamecard doesn't hold any user applications!");
    
end:
    mutexUnlock(&g_titleMutex);
    
    /* Fallback string if any errors occur. */
    /* This function is guaranteed to fail with Kiosk / Quest gamecards, so that's why this is needed. */
    if (!filename) filename = strdup("gamecard");
    
    return filename;
}

const char *titleGetNcmContentTypeName(u8 content_type)
{
    return (content_type <= NcmContentType_DeltaFragment ? g_titleNcmContentTypeNames[content_type] : NULL);
}

const char *titleGetNcmContentMetaTypeName(u8 content_meta_type)
{
    if ((content_meta_type > NcmContentMetaType_BootImagePackageSafe && content_meta_type < NcmContentMetaType_Application) || content_meta_type > NcmContentMetaType_Delta) return NULL;
    return (content_meta_type <= NcmContentMetaType_BootImagePackageSafe ? g_titleNcmContentMetaTypeNames[content_meta_type] : g_titleNcmContentMetaTypeNames[content_meta_type - 0x7A]);
}

NX_INLINE void titleFreeApplicationMetadata(void)
{
    if (g_appMetadata)
    {
        for(u32 i = 0; i < g_appMetadataCount; i++)
        {
            TitleApplicationMetadata *cur_app_metadata = g_appMetadata[i];
            if (cur_app_metadata)
            {
                if (cur_app_metadata->icon) free(cur_app_metadata->icon);
                free(cur_app_metadata);
            }
        }
        
        free(g_appMetadata);
        g_appMetadata = NULL;
    }
    
    g_appMetadataCount = 0;
}

static bool titleReallocateApplicationMetadata(u32 extra_app_count, bool free_entries)
{
    if (free_entries && !g_appMetadata)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    TitleApplicationMetadata **tmp_app_metadata = NULL;
    u32 realloc_app_count = (!free_entries ? (g_appMetadataCount + extra_app_count) : g_appMetadataCount);
    bool success = false;
    
    if (free_entries)
    {
        /* Free previously allocated application metadata entries. */
        for(u32 i = 0; i <= extra_app_count; i++)
        {
            TitleApplicationMetadata *cur_app_metadata = g_appMetadata[g_appMetadataCount + i];
            if (cur_app_metadata)
            {
                if (cur_app_metadata->icon) free(cur_app_metadata->icon);
                free(cur_app_metadata);
                cur_app_metadata = NULL;
            }
        }
    }
    
    if (realloc_app_count)
    {
        /* Reallocate application metadata pointer array. */
        tmp_app_metadata = realloc(g_appMetadata, realloc_app_count * sizeof(TitleApplicationMetadata*));
        if (tmp_app_metadata)
        {
            /* Update application metadata pointer. */
            g_appMetadata = tmp_app_metadata;
            tmp_app_metadata = NULL;
            
            /* Clear new application metadata pointer array area (if needed). */
            if (!free_entries && extra_app_count) memset(g_appMetadata + g_appMetadataCount, 0, extra_app_count * sizeof(TitleApplicationMetadata*));
        } else {
            LOG_MSG("Failed to reallocate application metadata pointer array! (%u element[s]).", realloc_app_count);
            goto end;
        }
    } else {
        /* Free application metadata pointer array. */
        free(g_appMetadata);
        g_appMetadata = NULL;
    }
    
    /* Update flag. */
    success = true;
    
end:
    return success;
}

NX_INLINE void titleFreeTitleInfo(void)
{
    if (g_titleInfo)
    {
        for(u32 i = 0; i < g_titleInfoCount; i++)
        {
            TitleInfo *cur_title_info = g_titleInfo[i];
            if (cur_title_info)
            {
                if (cur_title_info->content_infos) free(cur_title_info->content_infos);
                free(cur_title_info);
            }
        }
        
        free(g_titleInfo);
        g_titleInfo = NULL;
    }
    
    titleFreeOrphanTitleInfo();
    
    g_titleInfoCount = g_titleInfoBuiltInSystemCount = 0;
    g_titleInfoBuiltInUserStartIndex = g_titleInfoBuiltInUserCount = 0;
    g_titleInfoSdCardStartIndex = g_titleInfoSdCardCount = 0;
    g_titleInfoGameCardStartIndex = g_titleInfoGameCardCount = 0;
}

static bool titleReallocateTitleInfo(u32 extra_title_count, bool free_entries)
{
    if (free_entries && !g_titleInfo)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    TitleInfo **tmp_title_info = NULL;
    u32 realloc_title_count = (!free_entries ? (g_titleInfoCount + extra_title_count) : g_titleInfoCount);
    bool success = false;
    
    if (free_entries)
    {
        /* Free previously allocated title info entries. */
        for(u32 i = 0; i <= extra_title_count; i++)
        {
            TitleInfo *cur_title_info = g_titleInfo[g_titleInfoCount + i];
            if (cur_title_info)
            {
                if (cur_title_info->content_infos) free(cur_title_info->content_infos);
                free(cur_title_info);
                cur_title_info = NULL;
            }
        }
    }
    
    if (realloc_title_count)
    {
        /* Reallocate title info pointer array. */
        tmp_title_info = realloc(g_titleInfo, realloc_title_count * sizeof(TitleInfo*));
        if (tmp_title_info)
        {
            /* Update title info pointer. */
            g_titleInfo = tmp_title_info;
            tmp_title_info = NULL;
            
            /* Clear new title info pointer array area (if needed). */
            if (!free_entries && extra_title_count) memset(g_titleInfo + g_titleInfoCount, 0, extra_title_count * sizeof(TitleInfo*));
        } else {
            LOG_MSG("Failed to reallocate title info pointer array! (%u element[s]).", realloc_title_count);
            goto end;
        }
    } else {
        /* Free title info pointer array. */
        free(g_titleInfo);
        g_titleInfo = NULL;
    }
    
    /* Update flag. */
    success = true;
    
end:
    return success;
}

NX_INLINE void titleFreeOrphanTitleInfo(void)
{
    if (g_orphanTitleInfo)
    {
        free(g_orphanTitleInfo);
        g_orphanTitleInfo = NULL;
    }
    
    g_orphanTitleInfoCount = 0;
}

static void titleAddOrphanTitleInfoEntry(TitleInfo *orphan_title)
{
    if (!orphan_title) return;
    
    /* Reallocate orphan title info pointer array. */
    TitleInfo **tmp_orphan_info = realloc(g_orphanTitleInfo, (g_orphanTitleInfoCount + 1) * sizeof(TitleInfo*));
    if (!tmp_orphan_info)
    {
        LOG_MSG("Failed to reallocate orphan title info pointer array!");
        return;
    }
    
    g_orphanTitleInfo = tmp_orphan_info;
    tmp_orphan_info = NULL;
    
    /* Set orphan title info entry pointer. */
    g_orphanTitleInfo[g_orphanTitleInfoCount++] = orphan_title;
    
    /* Sort orphan title info entries by title ID. */
    if (g_orphanTitleInfoCount > 1) qsort(g_orphanTitleInfo, g_orphanTitleInfoCount, sizeof(TitleInfo*), &titleOrphanTitleInfoSortFunction);
}

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id)
{
    if (!g_appMetadata || !*g_appMetadata || !g_appMetadataCount || !title_id) return NULL;
    
    for(u32 i = 0; i < g_appMetadataCount; i++)
    {
        TitleApplicationMetadata *cur_app_metadata = g_appMetadata[i];
        if (cur_app_metadata && cur_app_metadata->title_id == title_id) return cur_app_metadata;
    }
    
    return NULL;
}

static bool titleGenerateMetadataEntriesFromSystemTitles(void)
{
    u32 extra_app_count = 0;
    bool success = false;
    
    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(g_systemTitlesCount, false))
    {
        LOG_MSG("Failed to reallocate application metadata pointer array for system titles!");
        return false;
    }
    
    /* Fill new application metadata entries. */
    for(extra_app_count = 0; extra_app_count < g_systemTitlesCount; extra_app_count++)
    {
        /* Allocate memory for the current entry. */
        TitleApplicationMetadata *cur_app_metadata = calloc(1, sizeof(TitleApplicationMetadata));
        if (!cur_app_metadata)
        {
            LOG_MSG("Failed to allocate memory for application metadata entry #%u!", extra_app_count);
            goto end;
        }
        
        /* Fill information. */
        const TitleSystemEntry *system_title = &(g_systemTitles[extra_app_count]);
        cur_app_metadata->title_id = system_title->title_id;
        sprintf(cur_app_metadata->lang_entry.name, system_title->name);
        
        /* Set application metadata entry pointer. */
        g_appMetadata[g_appMetadataCount + extra_app_count] = cur_app_metadata;
    }
    
    /* Sort metadata entries by title ID. */
    if (g_systemTitlesCount > 1) qsort(g_appMetadata + g_appMetadataCount, g_systemTitlesCount, sizeof(TitleApplicationMetadata*), &titleSystemTitleMetadataEntrySortFunction);
    
    /* Update application metadata count. */
    g_appMetadataCount += g_systemTitlesCount;
    
    /* Update flag. */
    success = true;
    
end:
    /* Free previously allocated application metadata pointers. Ignore return value. */
    if (!success) titleReallocateApplicationMetadata(extra_app_count, true);
    
    return success;
}

static bool titleGenerateMetadataEntriesFromNsRecords(void)
{
    Result rc = 0;
    
    NsApplicationRecord *app_records = NULL;
    u32 app_records_count = 0, extra_app_count = 0;
    
    bool success = false, free_entries = false;
    
    /* Allocate memory for the NS application records. */
    app_records = calloc(NS_APPLICATION_RECORD_LIMIT, sizeof(NsApplicationRecord));
    if (!app_records)
    {
        LOG_MSG("Failed to allocate memory for NS application records!");
        return false;
    }
    
    /* Retrieve NS application records. */
    rc = nsListApplicationRecord(app_records, NS_APPLICATION_RECORD_LIMIT, 0, (s32*)&app_records_count);
    if (R_FAILED(rc))
    {
        LOG_MSG("nsListApplicationRecord failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Return right away if no records were retrieved. */
    if (!app_records_count)
    {
        success = true;
        goto end;
    }
    
    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(app_records_count, false))
    {
        LOG_MSG("Failed to reallocate application metadata pointer array for NS records!");
        goto end;
    }
    
    free_entries = true;
    
    /* Retrieve application metadata for each NS application record. */
    for(u32 i = 0; i < app_records_count; i++)
    {
        TitleApplicationMetadata *cur_app_metadata = g_appMetadata[g_appMetadataCount + extra_app_count];
        if (!cur_app_metadata)
        {
            /* Allocate memory for a new application metadata entry. */
            cur_app_metadata = calloc(1, sizeof(TitleApplicationMetadata));
            if (!cur_app_metadata)
            {
                LOG_MSG("Failed to allocate memory for application metadata entry #%u! (%u / %u).", extra_app_count, i + 1, app_records_count);
                goto end;
            }
            
            /* Set application metadata entry pointer. */
            g_appMetadata[g_appMetadataCount + extra_app_count] = cur_app_metadata;
        }
        
        /* Retrieve application metadata. */
        if (!titleRetrieveApplicationMetadataByTitleId(app_records[i].application_id, cur_app_metadata)) continue;
        
        /* Increase extra application metadata counter. */
        extra_app_count++;
    }
    
    /* Check retrieved application metadata count. */
    if (!extra_app_count)
    {
        LOG_MSG("Unable to retrieve application metadata from NS application records! (%u element[s]).", app_records_count);
        goto end;
    }
    
    /* Sort application metadata entries by name. */
    if (extra_app_count > 1) qsort(g_appMetadata + g_appMetadataCount, extra_app_count, sizeof(TitleApplicationMetadata*), &titleUserApplicationMetadataEntrySortFunction);
    
    /* Update application metadata count. */
    g_appMetadataCount += extra_app_count;
    
    /* Free extra allocated pointers if we didn't use them. */
    if (extra_app_count < app_records_count) titleReallocateApplicationMetadata(0, false);
    
    /* Update flag. */
    success = true;
    
end:
    if (app_records) free(app_records);
    
    /* Free previously allocated application metadata pointers. Ignore return value. */
    if (!success && free_entries) titleReallocateApplicationMetadata(extra_app_count, true);
    
    return success;
}

static bool titleRetrieveApplicationMetadataByTitleId(u64 title_id, TitleApplicationMetadata *out)
{
    if (!g_nsAppControlData || !title_id || !out)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 write_size = 0;
    NacpLanguageEntry *lang_entry = NULL;
    
    u32 icon_size = 0;
    u8 *icon = NULL;
    
    /* Retrieve ns application control data. */
    rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, g_nsAppControlData, sizeof(NsApplicationControlData), &write_size);
    if (R_FAILED(rc))
    {
        LOG_MSG("nsGetApplicationControlData failed for title ID \"%016lX\"! (0x%08X).", title_id, rc);
        return false;
    }
    
    if (write_size < sizeof(NacpStruct))
    {
        LOG_MSG("Retrieved application control data buffer is too small! (0x%lX).", write_size);
        return false;
    }
    
    /* Get language entry. */
    rc = nacpGetLanguageEntry(&(g_nsAppControlData->nacp), &lang_entry);
    if (R_FAILED(rc))
    {
        LOG_MSG("nacpGetLanguageEntry failed! (0x%08X).", rc);
        return false;
    }
    
    /* Get icon. */
    icon_size = (u32)(write_size - sizeof(NacpStruct));
    if (icon_size)
    {
        icon = malloc(icon_size);
        if (!icon)
        {
            LOG_MSG("Error allocating memory for the icon buffer! (0x%X).", icon_size);
            return false;
        }
        
        memcpy(icon, g_nsAppControlData->icon, icon_size);
    }
    
    /* Copy data. */
    out->title_id = title_id;
    
    memcpy(&(out->lang_entry), lang_entry, sizeof(NacpLanguageEntry));
    utilsTrimString(out->lang_entry.name);
    utilsTrimString(out->lang_entry.author);
    
    out->icon_size = icon_size;
    out->icon = icon;
    
    return true;
}

static bool titleOpenNcmDatabases(void)
{
    Result rc = 0;
    NcmContentMetaDatabase *ncm_db = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm database pointer. */
        ncm_db = titleGetNcmDatabaseByStorageId(i);
        if (!ncm_db)
        {
            LOG_MSG("Failed to retrieve ncm database pointer for storage ID %u!", i);
            return false;
        }
        
        /* Check if the ncm database handle has already been retrieved. */
        if (serviceIsActive(&(ncm_db->s))) continue;
        
        /* Open ncm database. */
        rc = ncmOpenContentMetaDatabase(ncm_db, i);
        if (R_FAILED(rc))
        {
            /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
            /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
            LOG_MSG("ncmOpenContentMetaDatabase failed for storage ID %u! (0x%08X).", i, rc);
            if (i == NcmStorageId_SdCard && rc == 0x21005) continue;
            return false;
        }
    }
    
    return true;
}

static void titleCloseNcmDatabases(void)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm database pointer. */
        ncm_db = titleGetNcmDatabaseByStorageId(i);
        if (!ncm_db) continue;
        
        /* Check if the ncm database handle has already been retrieved. */
        if (serviceIsActive(&(ncm_db->s))) ncmContentMetaDatabaseClose(ncm_db);
    }
}

static bool titleOpenNcmStorages(void)
{
    Result rc = 0;
    NcmContentStorage *ncm_storage = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm storage pointer. */
        ncm_storage = titleGetNcmStorageByStorageId(i);
        if (!ncm_storage)
        {
            LOG_MSG("Failed to retrieve ncm storage pointer for storage ID %u!", i);
            return false;
        }
        
        /* Check if the ncm storage handle has already been retrieved. */
        if (serviceIsActive(&(ncm_storage->s))) continue;
        
        /* Open ncm storage. */
        rc = ncmOpenContentStorage(ncm_storage, i);
        if (R_FAILED(rc))
        {
            /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
            /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
            LOG_MSG("ncmOpenContentStorage failed for storage ID %u! (0x%08X).", i, rc);
            if (i == NcmStorageId_SdCard && rc == 0x21005) continue;
            return false;
        }
    }
    
    return true;
}

static void titleCloseNcmStorages(void)
{
    NcmContentStorage *ncm_storage = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm storage pointer. */
        ncm_storage = titleGetNcmStorageByStorageId(i);
        if (!ncm_storage) continue;
        
        /* Check if the ncm storage handle has already been retrieved. */
        if (serviceIsActive(&(ncm_storage->s))) ncmContentStorageClose(ncm_storage);
    }
}

static bool titleOpenNcmDatabaseAndStorageFromGameCard(void)
{
    Result rc = 0;
    NcmContentMetaDatabase *ncm_db = &g_ncmDbGameCard;
    NcmContentStorage *ncm_storage = &g_ncmStorageGameCard;
    
    /* Open ncm database. */
    rc = ncmOpenContentMetaDatabase(ncm_db, NcmStorageId_GameCard);
    if (R_FAILED(rc))
    {
        LOG_MSG("ncmOpenContentMetaDatabase failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Open ncm storage. */
    rc = ncmOpenContentStorage(ncm_storage, NcmStorageId_GameCard);
    if (R_FAILED(rc))
    {
        LOG_MSG("ncmOpenContentStorage failed! (0x%08X).", rc);
        goto end;
    }
    
end:
    return R_SUCCEEDED(rc);
}

static void titleCloseNcmDatabaseAndStorageFromGameCard(void)
{
    NcmContentMetaDatabase *ncm_db = &g_ncmDbGameCard;
    NcmContentStorage *ncm_storage = &g_ncmStorageGameCard;
    
    /* Check if the ncm database handle has already been retrieved. */
    if (serviceIsActive(&(ncm_db->s))) ncmContentMetaDatabaseClose(ncm_db);
    
    /* Check if the ncm storage handle has already been retrieved. */
    if (serviceIsActive(&(ncm_storage->s))) ncmContentStorageClose(ncm_storage);
}

static bool titleLoadPersistentStorageTitleInfo(void)
{
    /* Return right away if title info has already been retrieved. */
    if ((g_titleInfo && *g_titleInfo) || g_titleInfoCount) return true;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        u32 cur_title_count = 0;
        
        /* Generate title info from the current storage. */
        if (!titleGenerateTitleInfoFromStorage(i))
        {
            LOG_MSG("Failed to generate title info from storage ID %u!", i);
            return false;
        }
        
        /* Update title info counters and indexes. */
        switch(i)
        {
            case NcmStorageId_BuiltInSystem:
                g_titleInfoBuiltInSystemCount = g_titleInfoCount;
                cur_title_count = g_titleInfoBuiltInSystemCount;
                break;
            case NcmStorageId_BuiltInUser:
                g_titleInfoBuiltInUserStartIndex = g_titleInfoBuiltInSystemCount;
                g_titleInfoBuiltInUserCount = (g_titleInfoCount - g_titleInfoBuiltInUserStartIndex);
                cur_title_count = g_titleInfoBuiltInUserCount;
                break;
            case NcmStorageId_SdCard:
                g_titleInfoSdCardStartIndex = (g_titleInfoBuiltInUserStartIndex + g_titleInfoBuiltInUserCount);
                g_titleInfoSdCardCount = (g_titleInfoCount - g_titleInfoSdCardStartIndex);
                cur_title_count = g_titleInfoSdCardCount;
                break;
            default:
                break;
        }
        
        LOG_MSG("Loaded info for %u title(s) from storage ID %u.", cur_title_count, i);
    }
    
    return true;
}

static bool titleGenerateTitleInfoFromStorage(u8 storage_id)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    if (!(ncm_db = titleGetNcmDatabaseByStorageId(storage_id)) || !serviceIsActive(&(ncm_db->s)))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    
    u32 written = 0, total = 0, extra_title_count = 0;
    NcmContentMetaKey *meta_keys = NULL, *meta_keys_tmp = NULL;
    size_t meta_keys_size = sizeof(NcmContentMetaKey);
    
    bool success = false, free_entries = false;
    
    /* Allocate memory for the ncm application content meta keys. */
    meta_keys = calloc(1, meta_keys_size);
    if (!meta_keys)
    {
        LOG_MSG("Unable to allocate memory for the ncm application meta keys!");
        goto end;
    }
    
    /* Get a full list of all titles available in this storage. */
    /* Meta type '0' means all title types will be retrieved. */
    rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, 1, 0, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
    if (R_FAILED(rc))
    {
        LOG_MSG("ncmContentMetaDatabaseList failed! (0x%08X) (first entry).", rc);
        goto end;
    }
    
    /* Check if our application meta keys buffer was actually filled. */
    /* If it wasn't, odds are there are no titles in this storage. */
    if (!written || !total)
    {
        success = true;
        goto end;
    }
    
    /* Check if we need to resize our application meta keys buffer. */
    if (total > written)
    {
        /* Update application meta keys buffer size. */
        meta_keys_size *= total;
        
        /* Reallocate application meta keys buffer. */
        meta_keys_tmp = realloc(meta_keys, meta_keys_size);
        if (!meta_keys_tmp)
        {
            LOG_MSG("Unable to reallocate application meta keys buffer! (%u entries).", total);
            goto end;
        }
        
        meta_keys = meta_keys_tmp;
        meta_keys_tmp = NULL;
        
        /* Issue call again. */
        rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, (s32)total, 0, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
        if (R_FAILED(rc))
        {
            LOG_MSG("ncmContentMetaDatabaseList failed! (0x%08X) (%u %s).", rc, total, total > 1 ? "entries" : "entry");
            goto end;
        }
        
        /* Safety check. */
        if (written != total)
        {
            LOG_MSG("Application meta key count mismatch! (%u != %u).", written, total);
            goto end;
        }
    }
    
    /* Reallocate title info pointer array. */
    if (!titleReallocateTitleInfo(total, false))
    {
        LOG_MSG("Failed to reallocate title info pointer array for storage ID %u!", storage_id);
        goto end;
    }
    
    free_entries = true;
    
    /* Fill new title info entries. */
    for(u32 i = 0; i < total; i++)
    {
        u64 tmp_size = 0;
        NcmContentMetaKey *cur_meta_key = &(meta_keys[i]);
        
        TitleInfo *cur_title_info = g_titleInfo[g_titleInfoCount + extra_title_count];
        if (!cur_title_info)
        {
            /* Allocate memory for a new entry. */
            cur_title_info = calloc(1, sizeof(TitleInfo));
            if (!cur_title_info)
            {
                LOG_MSG("Failed to allocate memory for title info entry #%u! (%u / %u).", extra_title_count, i + 1, total);
                goto end;
            }
            
            /* Set title info entry pointer. */
            g_titleInfo[g_titleInfoCount + extra_title_count] = cur_title_info;
        }
        
        /* Get content infos. */
        if (!titleGetContentInfosFromTitle(storage_id, cur_meta_key, &(cur_title_info->content_infos), &(cur_title_info->content_count)))
        {
            LOG_MSG("Failed to get content infos for title ID %016lX!", cur_meta_key->id);
            continue;
        }
        
        /* Calculate title size. */
        for(u32 j = 0; j < cur_title_info->content_count; j++)
        {
            titleConvertNcmContentSizeToU64(cur_title_info->content_infos[j].size, &tmp_size);
            cur_title_info->size += tmp_size;
        }
        
        /* Fill information. */
        cur_title_info->storage_id = storage_id;
        memcpy(&(cur_title_info->meta_key), cur_meta_key, sizeof(NcmContentMetaKey));
        cur_title_info->version.value = cur_title_info->meta_key.version;
        utilsGenerateFormattedSizeString(cur_title_info->size, cur_title_info->size_str, sizeof(cur_title_info->size_str));
        
        /* Retrieve application metadata. */
        u64 app_id = (cur_title_info->meta_key.type <= NcmContentMetaType_Application ? cur_title_info->meta_key.id : \
                     (cur_title_info->meta_key.type == NcmContentMetaType_Patch ? titleGetApplicationIdByPatchId(cur_title_info->meta_key.id) : \
                     (cur_title_info->meta_key.type == NcmContentMetaType_AddOnContent ? titleGetApplicationIdByAddOnContentId(cur_title_info->meta_key.id) : \
                     titleGetApplicationIdByDeltaId(cur_title_info->meta_key.id))));
        
        cur_title_info->app_metadata = titleFindApplicationMetadataByTitleId(app_id);
        
        /* Increase extra title info counter. */
        extra_title_count++;
    }
    
    /* Check retrieved title info count. */
    if (!extra_title_count)
    {
        LOG_MSG("Unable to generate title info entries! (%u element[s]).", total);
        goto end;
    }
    
    /* Update title info count. */
    g_titleInfoCount += extra_title_count;
    
    /* Free extra allocated pointers if we didn't use them. */
    if (extra_title_count < total) titleReallocateTitleInfo(0, false);
    
    /* Update linked lists for user applications, patches and add-on contents. */
    /* This will also keep track of orphan titles - titles with no available application metadata. */
    titleUpdateTitleInfoLinkedLists();
    
    /* Update flag. */
    success = true;
    
end:
    if (meta_keys) free(meta_keys);
    
    /* Free previously allocated title info pointers. Ignore return value. */
    if (!success && free_entries) titleReallocateTitleInfo(extra_title_count, true);
    
    return success;
}

static bool titleGetContentInfosFromTitle(u8 storage_id, const NcmContentMetaKey *meta_key, NcmContentInfo **out_content_infos, u32 *out_content_count)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    if (!(ncm_db = titleGetNcmDatabaseByStorageId(storage_id)) || !serviceIsActive(&(ncm_db->s)) || !meta_key || !out_content_infos || !out_content_count)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    
    NcmContentMetaHeader content_meta_header = {0};
    u64 content_meta_header_read_size = 0;
    
    NcmContentInfo *content_infos = NULL;
    u32 content_count = 0, written = 0;
    
    bool success = false;
    
    /* Retrieve content meta header. */
    rc = ncmContentMetaDatabaseGet(ncm_db, meta_key, &content_meta_header_read_size, &content_meta_header, sizeof(NcmContentMetaHeader));
    if (R_FAILED(rc))
    {
        LOG_MSG("ncmContentMetaDatabaseGet failed! (0x%08X).", rc);
        goto end;
    }
    
    if (content_meta_header_read_size != sizeof(NcmContentMetaHeader))
    {
        LOG_MSG("Content meta header size mismatch! (0x%lX != 0x%lX).", content_meta_header_read_size, sizeof(NcmContentMetaHeader));
        goto end;
    }
    
    /* Get content count. */
    content_count = (u32)content_meta_header.content_count;
    if (!content_count)
    {
        LOG_MSG("Content count is zero!");
        goto end;
    }
    
    /* Allocate memory for the content infos. */
    content_infos = calloc(content_count, sizeof(NcmContentInfo));
    if (!content_infos)
    {
        LOG_MSG("Unable to allocate memory for the content infos buffer! (%u content[s]).", content_count);
        goto end;
    }
    
    /* Retrieve content infos. */
    rc = ncmContentMetaDatabaseListContentInfo(ncm_db, (s32*)&written, content_infos, (s32)content_count, meta_key, 0);
    if (R_FAILED(rc))
    {
        LOG_MSG("ncmContentMetaDatabaseListContentInfo failed! (0x%08X).", rc);
        goto end;
    }
    
    if (written != content_count)
    {
        LOG_MSG("Content count mismatch! (%u != %u).", written, content_count);
        goto end;
    }
    
    /* Update output. */
    *out_content_infos = content_infos;
    *out_content_count = content_count;
    
    success = true;
    
end:
    if (!success && content_infos) free(content_infos);
    
    return success;
}

static void titleUpdateTitleInfoLinkedLists(void)
{
    /* Free orphan title info. */
    titleFreeOrphanTitleInfo();
    
    /* Loop through all available user titles. */
    for(u32 i = g_titleInfoBuiltInUserStartIndex; i < g_titleInfoCount; i++)
    {
        /* Get pointer to the current title info and reset its linked list pointers. */
        TitleInfo *child_info = g_titleInfo[i];
        if (!child_info || child_info->meta_key.type < NcmContentMetaType_Application || child_info->meta_key.type > NcmContentMetaType_AddOnContent) continue;
        
        child_info->parent = child_info->previous = child_info->next = NULL;
        
        if (child_info->meta_key.type == NcmContentMetaType_Patch || child_info->meta_key.type == NcmContentMetaType_AddOnContent)
        {
            /* We're dealing with a patch or an add-on content. */
            /* Retrieve pointer to the first parent user application entry for patches and add-on contents. */
            /* Since gamecard title info entries are always appended to the end of the buffer, this guarantees we will first retrieve an eMMC / SD card entry (if available). */
            for(u32 j = g_titleInfoBuiltInUserStartIndex; j < g_titleInfoCount; j++)
            {
                TitleInfo *parent_info = g_titleInfo[j];
                if (!parent_info) continue;
                
                if (parent_info->meta_key.type == NcmContentMetaType_Application && \
                    ((child_info->meta_key.type == NcmContentMetaType_Patch && titleCheckIfPatchIdBelongsToApplicationId(parent_info->meta_key.id, child_info->meta_key.id)) || \
                    (child_info->meta_key.type == NcmContentMetaType_AddOnContent && titleCheckIfAddOnContentIdBelongsToApplicationId(parent_info->meta_key.id, child_info->meta_key.id))))
                {
                    child_info->parent = parent_info;
                    if (!child_info->app_metadata) child_info->app_metadata = parent_info->app_metadata;
                    break;
                }
            }
            
            /* If we have no application metadata, add orphan title info entry. */
            if (!child_info->app_metadata) titleAddOrphanTitleInfoEntry(child_info);
        }
        
        /* Locate previous user application, patch or add-on content entry. */
        /* If it's found, we will update both its next pointer and the previous pointer from the current entry. */
        for(u32 j = i; j > g_titleInfoBuiltInUserStartIndex; j--)
        {
            TitleInfo *previous_info = g_titleInfo[j - 1];
            if (!previous_info) continue;
            
            if (previous_info->meta_key.type == child_info->meta_key.type && (((child_info->meta_key.type == NcmContentMetaType_Application || child_info->meta_key.type == NcmContentMetaType_Patch) && \
                previous_info->meta_key.id == child_info->meta_key.id) || (child_info->meta_key.type == NcmContentMetaType_AddOnContent && \
                titleCheckIfAddOnContentIdsAreSiblings(previous_info->meta_key.id, child_info->meta_key.id))))
            {
                previous_info->next = child_info;
                child_info->previous = previous_info;
                break;
            }
        }
    }
}

static bool titleCreateGameCardInfoThread(void)
{
    if (!utilsCreateThread(&g_titleGameCardInfoThread, titleGameCardInfoThreadFunc, NULL, 1))
    {
        LOG_MSG("Failed to create gamecard title info thread!");
        return false;
    }
    
    return true;
}

static void titleDestroyGameCardInfoThread(void)
{
    /* Signal the exit event to terminate the gamecard title info thread. */
    ueventSignal(&g_titleGameCardInfoThreadExitEvent);
    
    /* Wait for the gamecard title info thread to exit. */
    utilsJoinThread(&g_titleGameCardInfoThread);
}

static void titleGameCardInfoThreadFunc(void *arg)
{
    (void)arg;
    
    Result rc = 0;
    int idx = 0;
    bool first_run = true;
    
    Waiter gamecard_status_event_waiter = waiterForUEvent(g_titleGameCardStatusChangeUserEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_titleGameCardInfoThreadExitEvent);
    Waiter update_info_waiter = waiterForUEvent(&g_titleGameCardUpdateInfoUserEvent);
    
    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, gamecard_status_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;
        
        /* Exit event triggered. */
        if (idx == 1) break;
        
        if (!first_run)
        {
            /* Update gamecard info updated flag. */
            mutexLock(&g_titleMutex);
            g_titleGameCardInfoUpdated = true;
            mutexUnlock(&g_titleMutex);
            
            /* Wait until another function signals us (titleIsGameCardInfoUpdated() or titleExit()). */
            rc = waitMulti(&idx, -1, update_info_waiter, exit_event_waiter);
            if (R_FAILED(rc))
            {
                mutexLock(&g_titleMutex);
                g_titleGameCardInfoUpdated = false;
                mutexUnlock(&g_titleMutex);
                continue;
            }
            
            /* Exit event triggered. */
            if (idx == 1) break;
        }
        
        /* Update gamecard title info. */
        mutexLock(&g_titleMutex);
        g_titleGameCardInfoUpdated = (titleRefreshGameCardTitleInfo() && !first_run);
        mutexUnlock(&g_titleMutex);
        
        if (first_run)
        {
            /* Disable first run flag. */
            first_run = false;
        } else {
            /* Wake up titleIsGameCardInfoUpdated(). */
            condvarWakeAll(&g_gameCardCondVar);
        }
    }
    
    /* Update gamecard flags. */
    g_titleGameCardAvailable = g_titleGameCardInfoUpdated = false;
    
    threadExit();
}

static bool titleRefreshGameCardTitleInfo(void)
{
    u32 gamecard_app_count = 0, extra_app_count = 0;
    bool status = false, success = false, cleanup = true, free_entries = false;
    
    /* Retrieve current gamecard status. */
    status = (gamecardGetStatus() == GameCardStatus_InsertedAndInfoLoaded);
    if (status == g_titleGameCardAvailable || !status)
    {
        success = cleanup = (status != g_titleGameCardAvailable);
        goto end;
    }
    
    /* Open gamecard ncm database and storage handles. */
    if (!titleOpenNcmDatabaseAndStorageFromGameCard())
    {
        LOG_MSG("Failed to open gamecard ncm database and storage handles.");
        goto end;
    }
    
    /* Update start index for the gamecard title info entries. */
    g_titleInfoGameCardStartIndex = g_titleInfoCount;
    
    /* Generate gamecard title info. */
    if (!titleGenerateTitleInfoFromStorage(NcmStorageId_GameCard))
    {
        LOG_MSG("Failed to generate gamecard title info!");
        goto end;
    }
    
    /* Update gamecard title info count. */
    g_titleInfoGameCardCount = (g_titleInfoCount - g_titleInfoGameCardStartIndex);
    if (!g_titleInfoGameCardCount)
    {
        LOG_MSG("Empty content meta key count from gamecard!");
        goto end;
    }
    
    /* Get gamecard user application count. */
    for(u32 i = g_titleInfoGameCardStartIndex; i < g_titleInfoCount; i++)
    {
        TitleInfo *cur_title_info = g_titleInfo[i];
        if (cur_title_info && cur_title_info->meta_key.type == NcmContentMetaType_Application) gamecard_app_count++;
    }
    
    /* Return immediately if, for some reason, there are no user applications. */
    if (!gamecard_app_count)
    {
        success = true;
        cleanup = false;
        goto end;
    }
    
    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(gamecard_app_count, false))
    {
        LOG_MSG("Failed to reallocate application metadata pointer array for gamecard user applications!");
        goto end;
    }
    
    free_entries = true;
    
    /* Retrieve application metadata for gamecard user applications. */
    for(u32 i = g_titleInfoGameCardStartIndex; i < g_titleInfoCount; i++)
    {
        TitleInfo *cur_title_info = g_titleInfo[i];
        if (!cur_title_info) continue;
        
        u64 app_id = (cur_title_info->meta_key.type <= NcmContentMetaType_Application ? cur_title_info->meta_key.id : \
                     (cur_title_info->meta_key.type == NcmContentMetaType_Patch ? titleGetApplicationIdByPatchId(cur_title_info->meta_key.id) : \
                     (cur_title_info->meta_key.type == NcmContentMetaType_AddOnContent ? titleGetApplicationIdByAddOnContentId(cur_title_info->meta_key.id) : \
                     titleGetApplicationIdByDeltaId(cur_title_info->meta_key.id))));
        
        /* Do not proceed if application metadata has already been retrieved, or if we can successfully retrieve it. */
        if (cur_title_info->app_metadata != NULL || (cur_title_info->app_metadata = titleFindApplicationMetadataByTitleId(app_id)) != NULL) continue;
        
        /* Retrieve application metadata pointer. */
        TitleApplicationMetadata *cur_app_metadata = g_appMetadata[g_appMetadataCount + extra_app_count];
        if (!cur_app_metadata)
        {
            /* Allocate memory for a new application metadata entry. */
            cur_app_metadata = calloc(1, sizeof(TitleApplicationMetadata));
            if (!cur_app_metadata)
            {
                LOG_MSG("Failed to allocate memory for application metadata entry #%u!", extra_app_count);
                goto end;
            }
            
            /* Set application metadata entry pointer. */
            g_appMetadata[g_appMetadataCount + extra_app_count] = cur_app_metadata;
        }
        
        /* Retrieve application metadata. */
        if (!titleRetrieveApplicationMetadataByTitleId(app_id, cur_app_metadata)) continue;
        
        /* Update application metadata pointer in title info. */
        cur_title_info->app_metadata = cur_app_metadata;
        
        /* Increase extra application metadata counter. */
        extra_app_count++;
    }
    
    if (extra_app_count)
    {
        /* Update application metadata count. */
        g_appMetadataCount += extra_app_count;
        
        /* Sort application metadata entries by name. */
        u32 user_app_metadata_count = (g_appMetadataCount - g_systemTitlesCount);
        if (user_app_metadata_count > 1) qsort(g_appMetadata + g_systemTitlesCount, user_app_metadata_count, sizeof(TitleApplicationMetadata*), &titleUserApplicationMetadataEntrySortFunction);
        
        /* Update linked lists for user applications, patches and add-on contents. */
        /* This will take care of orphan titles we might now have application metadata for. */
        titleUpdateTitleInfoLinkedLists();
    } else {
        /* Free leftover application metadata entry (if needed). */
        if (g_appMetadata[g_appMetadataCount]) free(g_appMetadata[g_appMetadataCount]);
    }
    
    /* Free extra allocated pointers if we didn't use them. */
    if (extra_app_count < gamecard_app_count) titleReallocateApplicationMetadata(0, false);
    
    /* Update flags. */
    success = true;
    cleanup = false;
    
end:
    /* Update gamecard status. */
    g_titleGameCardAvailable = status;
    
    /* Free previously allocated application metadata pointers. Ignore return value. */
    if (!success && free_entries) titleReallocateApplicationMetadata(extra_app_count, true);
    
    /* Remove gamecard title info entries and close its ncm database and storage handles (if needed). */
    if (cleanup)
    {
        titleRemoveGameCardTitleInfoEntries();
        titleCloseNcmDatabaseAndStorageFromGameCard();
    }
    
    return success;
}

static void titleRemoveGameCardTitleInfoEntries(void)
{
    if (!g_titleInfo || !*g_titleInfo || !g_titleInfoCount || !g_titleInfoGameCardCount) return;
    
    /* Update title info count. */
    g_titleInfoCount = g_titleInfoGameCardStartIndex;
    
    /* Reallocate title info pointer array. */
    titleReallocateTitleInfo(g_titleInfoGameCardCount - 1, true);
    
    /* Update gamecard variables. */
    g_titleInfoGameCardStartIndex = g_titleInfoGameCardCount = 0;
    
    /* Update linked lists for user applications, patches and add-on contents. */
    /* Won't do anything if g_titleInfoCount is already zero. */
    titleUpdateTitleInfoLinkedLists();
}

static bool titleIsUserApplicationContentAvailable(u64 app_id)
{
    if (!g_titleInfo || !*g_titleInfo || !g_titleInfoCount || !app_id) return false;
    
    for(u32 i = g_titleInfoBuiltInUserStartIndex; i < g_titleInfoCount; i++)
    {
        TitleInfo *cur_title_info = g_titleInfo[i];
        if (!cur_title_info) continue;
        
        if ((cur_title_info->meta_key.type == NcmContentMetaType_Application && cur_title_info->meta_key.id == app_id) || \
            (cur_title_info->meta_key.type == NcmContentMetaType_Patch && titleCheckIfPatchIdBelongsToApplicationId(app_id, cur_title_info->meta_key.id)) || \
            (cur_title_info->meta_key.type == NcmContentMetaType_AddOnContent && titleCheckIfAddOnContentIdBelongsToApplicationId(app_id, cur_title_info->meta_key.id))) return true;
    }
    
    return false;
}

static TitleInfo *_titleGetInfoFromStorageByTitleId(u8 storage_id, u64 title_id, bool lock)
{
    if (lock) mutexLock(&g_titleMutex);
    
    TitleInfo *info = NULL;
    
    u32 start_idx = ((storage_id == NcmStorageId_BuiltInSystem || storage_id == NcmStorageId_Any) ? 0 : (storage_id == NcmStorageId_BuiltInUser ? g_titleInfoBuiltInUserStartIndex : \
                     (storage_id == NcmStorageId_SdCard ? g_titleInfoSdCardStartIndex : g_titleInfoGameCardStartIndex)));
    
    u32 max_val = (storage_id == NcmStorageId_BuiltInSystem ? g_titleInfoBuiltInSystemCount : (storage_id == NcmStorageId_BuiltInUser ? g_titleInfoBuiltInUserCount : \
                   (storage_id == NcmStorageId_SdCard ? g_titleInfoSdCardCount : (storage_id == NcmStorageId_GameCard ? g_titleInfoGameCardCount : g_titleInfoCount))));
    
    max_val += start_idx;
    
    if (!g_titleInterfaceInit || !g_titleInfo || !*g_titleInfo || !g_titleInfoCount || storage_id < NcmStorageId_GameCard || storage_id > NcmStorageId_Any || max_val == start_idx || \
        max_val > g_titleInfoCount || start_idx > g_titleInfoCount || !title_id)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    for(u32 i = start_idx; i < max_val; i++)
    {
        TitleInfo *title_info = g_titleInfo[i];
        if (title_info && title_info->meta_key.id == title_id)
        {
            info = title_info;
            break;
        }
    }
    
    if (!info && lock) LOG_MSG("Unable to find title info entry with ID \"%016lX\"! (storage ID %u).", title_id, storage_id);
    
end:
    if (lock) mutexUnlock(&g_titleMutex);
    
    return info;
}

static int titleSystemTitleMetadataEntrySortFunction(const void *a, const void *b)
{
    const TitleApplicationMetadata *app_metadata_1 = *((const TitleApplicationMetadata**)a);
    const TitleApplicationMetadata *app_metadata_2 = *((const TitleApplicationMetadata**)b);
    
    if (app_metadata_1->title_id < app_metadata_2->title_id)
    {
        return -1;
    } else
    if (app_metadata_1->title_id > app_metadata_2->title_id)
    {
        return 1;
    }
    
    return 0;
}

static int titleUserApplicationMetadataEntrySortFunction(const void *a, const void *b)
{
    const TitleApplicationMetadata *app_metadata_1 = *((const TitleApplicationMetadata**)a);
    const TitleApplicationMetadata *app_metadata_2 = *((const TitleApplicationMetadata**)b);
    
    return strcasecmp(app_metadata_1->lang_entry.name, app_metadata_2->lang_entry.name);
}

static int titleOrphanTitleInfoSortFunction(const void *a, const void *b)
{
    const TitleInfo *title_info_1 = *((const TitleInfo**)a);
    const TitleInfo *title_info_2 = *((const TitleInfo**)b);
    
    if (title_info_1->meta_key.id < title_info_2->meta_key.id)
    {
        return -1;
    } else
    if (title_info_1->meta_key.id > title_info_2->meta_key.id)
    {
        return 1;
    }
    
    return 0;
}
