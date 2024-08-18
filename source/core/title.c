/*
 * title.c
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
#include <core/title.h>
#include <core/gamecard.h>
#include <core/nacp.h>
#include <core/cnmt.h>

#define NS_APPLICATION_RECORD_BLOCK_SIZE    1024

#define TITLE_STORAGE_COUNT                 4                                       /* GameCard, BuiltInSystem, BuiltInUser, SdCard. */
#define TITLE_STORAGE_INDEX(storage_id)     ((storage_id) - NcmStorageId_GameCard)

#define NCM_CMT_APP_OFFSET                  0x7A

/* Type definitions. */

typedef struct {
    u64 title_id;
    char name[32];
} TitleSystemEntry;

typedef struct {
    u8 storage_id;                  ///< NcmStorageId.
    NcmContentMetaDatabase ncm_db;
    NcmContentStorage ncm_storage;
    TitleInfo **titles;
    u32 title_count;
} TitleStorage;

typedef struct {
    NcaContext nca_ctx;
    ContentMetaContext cnmt_ctx;
    NcmContentMetaKey meta_key;
} TitleGameCardContentMetaContext;

/* Global variables. */

static Mutex g_titleMutex = 0;

static Thread g_titleGameCardInfoThread = {0};
static UEvent g_titleGameCardInfoThreadExitEvent = {0}, *g_titleGameCardStatusChangeUserEvent = NULL;
static bool g_titleInterfaceInit = false, g_titleGameCardInfoThreadCreated = false, g_titleGameCardAvailable = false, g_titleGameCardInfoUpdated = false;

static NsApplicationControlData *g_nsAppControlData = NULL;

static TitleApplicationMetadata **g_systemMetadata = NULL, **g_userMetadata = NULL;
static u32 g_systemMetadataCount = 0, g_userMetadataCount = 0;

static TitleApplicationMetadata **g_filteredSystemMetadata = NULL, **g_filteredUserMetadata = NULL;
static u32 g_filteredSystemMetadataCount = 0, g_filteredUserMetadataCount = 0;

static TitleGameCardApplicationMetadata *g_titleGameCardApplicationMetadata = NULL;
static u32 g_titleGameCardApplicationMetadataCount = 0;

static char *g_titleGameCardFileNames[TitleNamingConvention_Count] = {NULL};

static TitleStorage g_titleStorage[TITLE_STORAGE_COUNT] = {0};

static TitleInfo **g_orphanTitleInfo = NULL;
static u32 g_orphanTitleInfoCount = 0;

static const char *g_titleNcmStorageIdNames[] = {
    [NcmStorageId_None]          = "None",
    [NcmStorageId_Host]          = "Host",
    [NcmStorageId_GameCard]      = "Gamecard",
    [NcmStorageId_BuiltInSystem] = "eMMC (system)",
    [NcmStorageId_BuiltInUser]   = "eMMC (user)",
    [NcmStorageId_SdCard]        = "SD card",
    [NcmStorageId_Any]           = "Any"
};

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
    [NcmContentMetaType_Unknown]                           = "Unknown",
    [NcmContentMetaType_SystemProgram]                     = "SystemProgram",
    [NcmContentMetaType_SystemData]                        = "SystemData",
    [NcmContentMetaType_SystemUpdate]                      = "SystemUpdate",
    [NcmContentMetaType_BootImagePackage]                  = "BootImagePackage",
    [NcmContentMetaType_BootImagePackageSafe]              = "BootImagePackageSafe",
    [NcmContentMetaType_Application - NCM_CMT_APP_OFFSET]  = "Application",
    [NcmContentMetaType_Patch - NCM_CMT_APP_OFFSET]        = "Patch",
    [NcmContentMetaType_AddOnContent - NCM_CMT_APP_OFFSET] = "AddOnContent",
    [NcmContentMetaType_Delta - NCM_CMT_APP_OFFSET]        = "Delta",
    [NcmContentMetaType_DataPatch - NCM_CMT_APP_OFFSET]    = "DataPatch"
};

static const char *g_filenameTypeStrings[] = {
    [NcmContentMetaType_Unknown]                           = "UNK",
    [NcmContentMetaType_SystemProgram]                     = "SYSPRG",
    [NcmContentMetaType_SystemData]                        = "SYSDAT",
    [NcmContentMetaType_SystemUpdate]                      = "SYSUPD",
    [NcmContentMetaType_BootImagePackage]                  = "BIP",
    [NcmContentMetaType_BootImagePackageSafe]              = "BIPS",
    [NcmContentMetaType_Application - NCM_CMT_APP_OFFSET]  = "BASE",
    [NcmContentMetaType_Patch - NCM_CMT_APP_OFFSET]        = "UPD",
    [NcmContentMetaType_AddOnContent - NCM_CMT_APP_OFFSET] = "DLC",
    [NcmContentMetaType_Delta - NCM_CMT_APP_OFFSET]        = "DELTA",
    [NcmContentMetaType_DataPatch - NCM_CMT_APP_OFFSET]    = "DLCUPD"
};

/* Info retrieved from https://switchbrew.org/wiki/Title_list. */
/* Titles bundled with the kernel are excluded. */
static const TitleSystemEntry g_systemTitles[] = {
    /* System modules. */
    /* Meta + Program NCAs. */
    { 0x0100000000000000, "fs" },                               ///< Unused, bundled with kernel.
    { 0x0100000000000001, "ldr" },                              ///< Unused, bundled with kernel.
    { 0x0100000000000002, "ncm" },                              ///< Unused, bundled with kernel.
    { 0x0100000000000003, "pm" },                               ///< Unused, bundled with kernel.
    { 0x0100000000000004, "sm" },                               ///< Unused, bundled with kernel.
    { 0x0100000000000005, "boot" },                             ///< Unused, bundled with kernel.
    { 0x0100000000000006, "usb" },
    { 0x0100000000000007, "htc" },
    { 0x0100000000000008, "boot2" },
    { 0x0100000000000009, "settings" },
    { 0x010000000000000A, "Bus" },
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
    { 0x0100000000000028, "spl" },                              ///< Unused, bundled with kernel.
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
    { 0x0100000000000043, "sys_applet_unknown_00" },            ///< Placeholder.
    { 0x0100000000000044, "sys_applet_unknown_01" },            ///< Placeholder.
    { 0x0100000000000045, "omm" },
    { 0x0100000000000046, "eth" },
    { 0x0100000000000047, "sys_applet_unknown_02" },            ///< Placeholder.
    { 0x0100000000000048, "sys_applet_unknown_03" },            ///< Placeholder.
    { 0x0100000000000049, "sys_applet_unknown_04" },            ///< Placeholder.
    { 0x010000000000004A, "sys_applet_unknown_05" },            ///< Placeholder.
    { 0x010000000000004B, "sys_applet_unknown_06" },            ///< Placeholder.
    { 0x010000000000004C, "netTc" },
    { 0x010000000000004D, "sys_applet_unknown_07" },            ///< Placeholder.
    { 0x010000000000004E, "sys_applet_unknown_08" },            ///< Placeholder.
    { 0x010000000000004F, "sys_applet_unknown_09" },            ///< Placeholder.
    { 0x0100000000000050, "ngc" },
    { 0x0100000000000051, "dmgr" },

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
    { 0x0100000000000832, "CradleFirmwareAula" },               ///< Placeholder.
    { 0x0100000000000835, "NewErrorMessage" },                  ///< Placeholder.

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
    { 0x010000000000100A, "LibAppletWeb" },
    { 0x010000000000100B, "LibAppletShop" },
    { 0x010000000000100C, "overlayDisp" },
    { 0x010000000000100D, "photoViewer" },
    { 0x010000000000100E, "set" },
    { 0x010000000000100F, "LibAppletOff" },
    { 0x0100000000001010, "LibAppletLns" },
    { 0x0100000000001011, "LibAppletAuth" },
    { 0x0100000000001012, "starter" },
    { 0x0100000000001013, "myPage" },
    { 0x0100000000001014, "PlayReport" },
    { 0x0100000000001015, "maintenance" },
    { 0x0100000000001016, "application_install" },              ///< Placeholder.
    { 0x0100000000001017, "nn.am.SystemReportTask" },           ///< Placeholder.
    { 0x0100000000001018, "systemupdate_dl_throughput" },       ///< Placeholder.
    { 0x0100000000001019, "volume_update"},                     ///< Placeholder.
    { 0x010000000000101A, "gift" },
    { 0x010000000000101B, "DummyECApplet" },
    { 0x010000000000101C, "userMigration" },
    { 0x010000000000101D, "EncounterSys" },
    { 0x010000000000101E, "nim_unknown_00" },                   ///< Placeholder.
    { 0x010000000000101F, "nim_glue_unknown_00" },              ///< Placeholder.
    { 0x0100000000001020, "story" },
    { 0x0100000000001021, "systemupdate_pass" },                ///< Placeholder.
    { 0x0100000000001023, "statistics" },                       ///< Placeholder.
    { 0x0100000000001024, "syslog" },                           ///< Placeholder.
    { 0x0100000000001025, "am_unknown_00" },                    ///< Placeholder.
    { 0x0100000000001026, "olsc_unknown_00" },                  ///< Placeholder.
    { 0x0100000000001027, "account_unknown_00" },               ///< Placeholder.
    { 0x0100000000001028, "ns_unknown_00" },                    ///< Placeholder.
    { 0x0100000000001029, "request_count" },                    ///< Placeholder.
    { 0x010000000000102A, "am_unknown_01" },                    ///< Placeholder.
    { 0x010000000000102B, "glue_unknown_00" },                  ///< Placeholder.
    { 0x010000000000102C, "am_unknown_02" },                    ///< Placeholder.
    { 0x010000000000102E, "blacklist" },                        ///< Placeholder.
    { 0x010000000000102F, "content_delivery" },                 ///< Placeholder.
    { 0x0100000000001030, "npns_create_token" },                ///< Placeholder.
    { 0x0100000000001031, "ns_unknown_01" },                    ///< Placeholder.
    { 0x0100000000001032, "glue_unknown_01" },                  ///< Placeholder.
    { 0x0100000000001033, "promotion" },                        ///< Placeholder.
    { 0x0100000000001034, "ngct_bcat_unknown_00" },             ///< Placeholder.
    { 0x0100000000001037, "nim_unknown_01" },                   ///< Placeholder.
    { 0x0100000000001038, "sample" },
    { 0x010000000000103C, "mnpp" },                             ///< Placeholder.
    { 0x010000000000103D, "bsdsocket_setting" },                ///< Placeholder.
    { 0x010000000000103E, "ntf_mission_completed" },            ///< Placeholder.
    { 0x0100000000001042, "am_unknown_03" },                    ///< Placeholder.
    { 0x0100000000001043, "am_unknown_04" },                    ///< Placeholder.
    { 0x0100000000001FFF, "EndOceanProgramId" },

    /* Development system applets. */
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
    { 0x010000000000211E, "C13Handling" },
    { 0x010000000000211F, "HidTest" },
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
    { 0x010000000000216A, "ButtonTest" },
    { 0x010000000000216D, "ExhibitionSaveDataSnapshot" },       ///< Placeholder.
    { 0x010000000000216E, "HandlingA" },
    { 0x0100000000002178, "SecureStartupSettings" },            ///< Placeholder.
    { 0x010000000000217A, "WirelessInterference" },
    { 0x010000000000217D, "CradleFirmwareUpdater" },
    { 0x0100000000002184, "HttpInstallSettings" },              ///< Placeholder.
    { 0x0100000000002187, "ExhibitionMovieAssetData" },         ///< Placeholder.
    { 0x0100000000002191, "ExhibitionPlayData" },               ///< Placeholder.

    /* Debug system modules. */
    { 0x0100000000003002, "DummyProcess" },
    { 0x0100000000003003, "DebugMonitor0" },
    { 0x0100000000003004, "SystemHelloWorld" },

    /* Development system modules. */
    { 0x010000000000B120, "nvdbgsvc" },
    { 0x010000000000B123, "acc:CORNX" },
    { 0x010000000000B14A, "manu" },
    { 0x010000000000B14B, "ManuUsbLoopBack" },
    { 0x010000000000B1B8, "DevFwdbgHbPackage" },
    { 0x010000000000B1B9, "DevFwdbgUsbPackage" },
    { 0x010000000000B1BA, "ProdFwdbgPackage" },
    { 0x010000000000B22A, "scs" },
    { 0x010000000000B22B, "ControllerFirmwareDebug" },
    { 0x010000000000B240, "htc" },

    /* Bdk system modules. */
    { 0x010000000000C600, "BdkSample01" },
    { 0x010000000000C601, "BdkSample02" },
    { 0x010000000000C602, "BdkSample03" },
    { 0x010000000000C603, "BdkSample04" },

    /* New development system modules. */
    { 0x010000000000D609, "dmnt.gen2" },
    { 0x010000000000D60A, "msm_unknown_00" },                   ///< Placeholder.
    { 0x010000000000D60B, "msm_unknown_01" },                   ///< Placeholder.
    { 0x010000000000D60C, "msm_unknown_02" },                   ///< Placeholder.
    { 0x010000000000D60D, "msm_unknown_03" },                   ///< Placeholder.
    { 0x010000000000D60E, "msm_unknown_04" },                   ///< Placeholder.
    { 0x010000000000D610, "msm_unknown_05" },                   ///< Placeholder.
    { 0x010000000000D611, "msm_unknown_06" },                   ///< Placeholder.
    { 0x010000000000D612, "msm_unknown_07" },                   ///< Placeholder.
    { 0x010000000000D613, "msm_unknown_08" },                   ///< Placeholder.
    { 0x010000000000D614, "msm_unknown_09" },                   ///< Placeholder.
    { 0x010000000000D615, "msm_unknown_0a" },                   ///< Placeholder.
    { 0x010000000000D616, "msm_unknown_0b" },                   ///< Placeholder.
    { 0x010000000000D617, "msm_unknown_0c" },                   ///< Placeholder.
    { 0x010000000000D619, "msm_unknown_0d" },                   ///< Placeholder.
    { 0x010000000000D621, "msm_unknown_0e" },                   ///< Placeholder.
    { 0x010000000000D623, "DevServer" },
    { 0x010000000000D633, "msm_unknown_0f" },                   ///< Placeholder.
    { 0x010000000000D640, "htcnet" },
    { 0x010000000000D65A, "netTcDev" },

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
    { 0x010023D002B98000, "DeltaStress" },
    { 0x010099F00D810000, "sysapp_unknown_00" },                ///< Placeholder.
    { 0x0100E6C01163C000, "sysapp_unknown_01" },                ///< Placeholder.

    /* Pre-release system applets. */
    { 0x1000000000000001, "SystemInitializer" },
    { 0x1000000000000004, "CalWriter" },
    { 0x1000000000000005, "DevMenuCommand" },
    { 0x1000000000000006, "SettingsManager" },
    { 0x1000000000000007, "DevMenu" },
    { 0x100000000000000B, "SnapShotDumper" },
    { 0x100000000000000C, "SystemUpdater" },
    { 0x100000000000000E, "ControllerFirmwareUpdater" },

    /* Pre-release system modules. */
    { 0x1000000000000201, "usb" },
    { 0x1000000000000202, "tma" },
    { 0x1000000000000203, "boot2" },
    { 0x1000000000000204, "settings" },
    { 0x1000000000000205, "bus" },
    { 0x1000000000000206, "bluetooth" },
    { 0x1000000000000208, "DebugMonitor0" },
    { 0x1000000000000209, "dmnt" },
    { 0x100000000000020B, "nifm" },
    { 0x100000000000020C, "ptm" },
    { 0x100000000000020D, "shell" },
    { 0x100000000000020E, "bsdsocket" },
    { 0x100000000000020F, "hid" },
    { 0x1000000000000210, "audio" },
    { 0x1000000000000212, "LogManager" },
    { 0x1000000000000213, "wlan" },
    { 0x1000000000000214, "cs" },
    { 0x1000000000000215, "ldn" },
    { 0x1000000000000216, "nvservices" },
    { 0x1000000000000217, "pcv" },
    { 0x1000000000000218, "ppc" },
    { 0x100000000000021A, "lbl0" },
    { 0x100000000000021B, "nvnflinger" },
    { 0x100000000000021C, "pcie" },
    { 0x100000000000021D, "account" },
    { 0x100000000000021E, "ns" },
    { 0x100000000000021F, "nfc" },
    { 0x1000000000000220, "psc" },
    { 0x1000000000000221, "capsrv" },
    { 0x1000000000000222, "am" },
    { 0x1000000000000223, "ssl" },
    { 0x1000000000000224, "nim" }
};

static const u32 g_systemTitlesCount = MAX_ELEMENTS(g_systemTitles);

/* Function prototypes. */

NX_INLINE void titleFreeApplicationMetadata(void);
static bool titleReallocateApplicationMetadata(u32 extra_app_count, bool is_system, bool free_entries);

NX_INLINE bool titleInitializePersistentTitleStorages(void);
NX_INLINE void titleCloseTitleStorages(void);

static bool titleInitializeTitleStorage(u8 storage_id);
static bool titleInitializeGameCardTitleStorageByHashFileSystem(u8 hfs_partition_type);
static void titleCloseTitleStorage(u8 storage_id);
static bool titleReallocateTitleInfoFromStorage(TitleStorage *title_storage, u32 extra_title_count, bool free_entries);

NX_INLINE void titleFreeOrphanTitleInfoEntries(void);
static void titleAddOrphanTitleInfoEntry(TitleInfo *orphan_title);

static bool titleGenerateMetadataEntriesFromSystemTitles(void);
static bool titleGenerateMetadataEntriesFromNsRecords(void);

static TitleApplicationMetadata *titleGetSystemMetadataEntry(u64 title_id);
static TitleApplicationMetadata *titleGenerateUserMetadataEntryFromNs(u64 title_id);
static TitleApplicationMetadata *titleGenerateUserMetadataEntryFromControlNca(TitleInfo *title_info);

static bool titleGetApplicationControlDataFromNs(u64 title_id, NsApplicationControlData *out_control_data, u64 *out_control_data_size);
static bool titleGetApplicationControlDataFromControlNca(TitleInfo *title_info, NsApplicationControlData *out_control_data, u64 *out_control_data_size);

static TitleApplicationMetadata *titleInitializeUserMetadataEntryFromControlData(u64 title_id, const NsApplicationControlData *control_data, u64 control_data_size);

static void titleGenerateFilteredApplicationMetadataPointerArray(bool is_system);
static bool titleIsUserApplicationContentAvailable(u64 app_id);

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id, bool is_system, u32 extra_app_count);

NX_INLINE u64 titleGetApplicationIdByContentMetaKey(const NcmContentMetaKey *meta_key);

static bool titleGenerateTitleInfoEntriesForTitleStorage(TitleStorage *title_storage);
static bool titleGenerateTitleInfoEntriesByHashFileSystemForGameCardTitleStorage(TitleStorage *title_storage, HashFileSystemContext *hfs_ctx);

static TitleInfo *titleGenerateTitleInfoEntry(u8 storage_id, const NcmContentMetaKey *meta_key, NcmContentInfo *content_infos, u32 content_count, bool get_control_nca_metadata);
static bool titleInitializeTitleInfoApplicationMetadataFromControlNca(TitleInfo *title_info);

static bool titleGetMetaKeysFromContentDatabase(NcmContentMetaDatabase *ncm_db, NcmContentMetaKey **out_meta_keys, u32 *out_meta_key_count);
static bool titleGetContentInfosByMetaKey(NcmContentMetaDatabase *ncm_db, const NcmContentMetaKey *meta_key, NcmContentInfo **out_content_infos, u32 *out_content_count);

static bool titleGetGameCardContentMetaContexts(HashFileSystemContext *hfs_ctx, TitleGameCardContentMetaContext **out_gc_meta_ctxs, u32 *out_gc_meta_ctx_count);
static void titleFreeGameCardContentMetaContexts(TitleGameCardContentMetaContext **gc_meta_ctxs, u32 gc_meta_ctx_count);

static bool titleGetContentInfosByGameCardContentMetaContext(TitleGameCardContentMetaContext *gc_meta_ctx, HashFileSystemContext *hfs_ctx, NcmContentInfo **out_content_infos, u32 *out_content_count);

static void titleUpdateTitleInfoLinkedLists(void);

static TitleInfo *_titleGetTitleInfoEntryFromStorageByTitleId(u8 storage_id, u64 title_id);

static TitleInfo *titleDuplicateTitleInfoFull(TitleInfo *title_info, TitleInfo *previous, TitleInfo *next);
static TitleInfo *titleDuplicateTitleInfo(TitleInfo *title_info);

static char *titleGetDisplayVersionString(TitleInfo *title_info);

static bool titleCreateGameCardInfoThread(void);
static void titleDestroyGameCardInfoThread(void);
static void titleGameCardInfoThreadFunc(void *arg);

static bool titleRefreshGameCardTitleInfo(void);

static void titleGenerateGameCardApplicationMetadataArray(void);

NX_INLINE void titleGenerateGameCardFileNames(void);
NX_INLINE void titleFreeGameCardFileNames(void);
static char *_titleGenerateGameCardFileName(u8 naming_convention);

static int titleSystemMetadataSortFunction(const void *a, const void *b);
static int titleUserMetadataSortFunction(const void *a, const void *b);
static int titleInfoSortFunction(const void *a, const void *b);
static int titleGameCardApplicationMetadataSortFunction(const void *a, const void *b);
static int titleGameCardContentMetaContextSortFunction(const void *a, const void *b);

bool titleInitialize(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_titleMutex)
    {
        ret = g_titleInterfaceInit;
        if (ret) break;

        /* Allocate memory for the ns application control data. */
        /* This will be used each time we need to retrieve the metadata from an application. */
        g_nsAppControlData = calloc(1, sizeof(NsApplicationControlData));
        if (!g_nsAppControlData)
        {
            LOG_MSG_ERROR("Failed to allocate memory for the ns application control data!");
            break;
        }

        /* Generate application metadata entries from hardcoded system titles, since we can't retrieve their names via ns. */
        if (!titleGenerateMetadataEntriesFromSystemTitles())
        {
            LOG_MSG_ERROR("Failed to generate application metadata from hardcoded system titles!");
            break;
        }

        /* Generate application metadata entries from ns records. */
        /* Theoretically speaking, we should only need to do this once. */
        /* However, if any new gamecard is inserted while the application is running, we *will* have to retrieve the metadata from its application(s). */
        if (!titleGenerateMetadataEntriesFromNsRecords())
        {
            LOG_MSG_ERROR("Failed to generate application metadata from ns records!");
            break;
        }

        /* Initialize persistent title storages (BuiltInSystem, BuiltInUser, SdCard). */
        /* The background gamecard title thread will take care of initializing the gamecard title storage. */
        if (!titleInitializePersistentTitleStorages())
        {
            LOG_MSG_ERROR("Failed to initialize persistent title storages!");
            break;
        }

        /* Generate filtered system application metadata pointer array. */
        titleGenerateFilteredApplicationMetadataPointerArray(true);

        /* Generate filtered user application metadata pointer array. */
        titleGenerateFilteredApplicationMetadataPointerArray(false);

        /* Create user-mode exit event. */
        ueventCreate(&g_titleGameCardInfoThreadExitEvent, true);

        /* Retrieve gamecard status change user event. */
        g_titleGameCardStatusChangeUserEvent = gamecardGetStatusChangeUserEvent();
        if (!g_titleGameCardStatusChangeUserEvent)
        {
            LOG_MSG_ERROR("Failed to retrieve gamecard status change user event!");
            break;
        }

        /* Create gamecard title info thread. */
        if (!(g_titleGameCardInfoThreadCreated = titleCreateGameCardInfoThread())) break;

        /* Update flags. */
        ret = g_titleInterfaceInit = true;
    }

    return ret;
}

void titleExit(void)
{
    SCOPED_LOCK(&g_titleMutex)
    {
        /* Destroy gamecard detection thread. */
        if (g_titleGameCardInfoThreadCreated)
        {
            titleDestroyGameCardInfoThread();
            g_titleGameCardInfoThreadCreated = false;
        }

        /* Close title storages. */
        titleCloseTitleStorages();

        /* Free orphan title info entries. */
        titleFreeOrphanTitleInfoEntries();

        /* Free application metadata. */
        titleFreeApplicationMetadata();

        /* Free ns application control data. */
        if (g_nsAppControlData)
        {
            free(g_nsAppControlData);
            g_nsAppControlData = NULL;
        }

        g_titleInterfaceInit = false;
    }
}

NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id)
{
    u8 idx = TITLE_STORAGE_INDEX(storage_id);
    return (idx < TITLE_STORAGE_COUNT ? &(g_titleStorage[idx].ncm_db) : NULL);
}

NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id)
{
    u8 idx = TITLE_STORAGE_INDEX(storage_id);
    return (idx < TITLE_STORAGE_COUNT ? &(g_titleStorage[idx].ncm_storage) : NULL);
}

TitleApplicationMetadata **titleGetApplicationMetadataEntries(bool is_system, u32 *out_count)
{
    TitleApplicationMetadata **dup_filtered_app_metadata = NULL;

    SCOPED_LOCK(&g_titleMutex)
    {
        TitleApplicationMetadata **filtered_app_metadata = (is_system ? g_filteredSystemMetadata : g_filteredUserMetadata);
        u32 filtered_app_metadata_count = (is_system ? g_filteredSystemMetadataCount : g_filteredUserMetadataCount);

        if (!g_titleInterfaceInit || !filtered_app_metadata || !filtered_app_metadata_count || !out_count)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        /* Allocate memory for the pointer array. */
        dup_filtered_app_metadata = malloc(filtered_app_metadata_count * sizeof(TitleApplicationMetadata*));
        if (!dup_filtered_app_metadata)
        {
            LOG_MSG_ERROR("Failed to allocate memory for pointer array duplicate!");
            break;
        }

        /* Copy application metadata pointers. */
        memcpy(dup_filtered_app_metadata, filtered_app_metadata, filtered_app_metadata_count * sizeof(TitleApplicationMetadata*));

        /* Update output counter. */
        *out_count = filtered_app_metadata_count;
    }

    return dup_filtered_app_metadata;
}

TitleGameCardApplicationMetadata *titleGetGameCardApplicationMetadataEntries(u32 *out_count)
{
    TitleGameCardApplicationMetadata *dup_gc_app_metadata = NULL;

    SCOPED_LOCK(&g_titleMutex)
    {
        if (!g_titleInterfaceInit || !g_titleGameCardAvailable || !g_titleGameCardApplicationMetadata || !g_titleGameCardApplicationMetadataCount || !out_count)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        /* Allocate memory for the output array. */
        dup_gc_app_metadata = malloc(g_titleGameCardApplicationMetadataCount * sizeof(TitleGameCardApplicationMetadata));
        if (!dup_gc_app_metadata)
        {
            LOG_MSG_ERROR("Failed to allocate memory for output array!");
            break;
        }

        /* Copy array data. */
        memcpy(dup_gc_app_metadata, g_titleGameCardApplicationMetadata, g_titleGameCardApplicationMetadataCount * sizeof(TitleGameCardApplicationMetadata));

        /* Update output counter. */
        *out_count = g_titleGameCardApplicationMetadataCount;
    }

    return dup_gc_app_metadata;
}

TitleInfo *titleGetTitleInfoEntryFromStorageByTitleId(u8 storage_id, u64 title_id)
{
    TitleInfo *ret = NULL;

    SCOPED_LOCK(&g_titleMutex)
    {
        TitleInfo *title_info = (g_titleInterfaceInit ? _titleGetTitleInfoEntryFromStorageByTitleId(storage_id, title_id) : NULL);
        if (title_info)
        {
            ret = titleDuplicateTitleInfoFull(title_info, NULL, NULL);
            if (!ret) LOG_MSG_ERROR("Failed to duplicate title info for %016lX!", title_id);
        }
    }

    return ret;
}

void titleFreeTitleInfo(TitleInfo **info)
{
    TitleInfo *ptr = NULL, *tmp1 = NULL, *tmp2 = NULL;
    if (!info || !(ptr = *info)) return;

    /* Free content infos. */
    if (ptr->content_infos) free(ptr->content_infos);

    /* Free previous sibling(s). */
    tmp1 = ptr->previous;
    while(tmp1)
    {
        tmp2 = tmp1->previous;
        tmp1->previous = tmp1->next = NULL;
        titleFreeTitleInfo(&tmp1);
        tmp1 = tmp2;
    }

    /* Free next sibling(s). */
    tmp1 = ptr->next;
    while(tmp1)
    {
        tmp2 = tmp1->next;
        tmp1->previous = tmp1->next = NULL;
        titleFreeTitleInfo(&tmp1);
        tmp1 = tmp2;
    }

    free(ptr);
    *info = NULL;
}

bool titleGetUserApplicationData(u64 app_id, TitleUserApplicationData *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_titleMutex)
    {
        if (!g_titleInterfaceInit || !app_id || !out)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        bool error = false;
        TitleInfo *app_info = NULL, *patch_info = NULL, *aoc_info = NULL, *aoc_patch_info = NULL;

        /* Clear output. */
        titleFreeUserApplicationData(out);

#define TITLE_ALLOCATE_USER_APP_DATA(elem, msg, decl) \
    if (elem##_info && !out->elem##_info) { \
        out->elem##_info = titleDuplicateTitleInfoFull(elem##_info, NULL, NULL); \
        if (!out->elem##_info) { \
            LOG_MSG_ERROR("Failed to duplicate %s info for %016lX!", msg, app_id); \
            decl; \
        } \
    }

        /* Get info for the first user application title. */
        app_info = _titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_Any, app_id);
        TITLE_ALLOCATE_USER_APP_DATA(app, "user application", break);

        /* Get info for the first patch title. */
        patch_info = _titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_Any, titleGetPatchIdByApplicationId(app_id));
        TITLE_ALLOCATE_USER_APP_DATA(patch, "patch", break);

        /* Get info for the first add-on content and add-on content patch titles. */
        for(u8 i = NcmStorageId_GameCard; i <= NcmStorageId_SdCard; i++)
        {
            if (i == NcmStorageId_BuiltInSystem) continue;

            TitleStorage *title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(i)]);
            if (!title_storage->titles || !title_storage->title_count) continue;

            for(u32 j = 0; j < title_storage->title_count; j++)
            {
                TitleInfo *title_info = title_storage->titles[j];
                if (!title_info) continue;

                if (title_info->meta_key.type == NcmContentMetaType_AddOnContent && titleCheckIfAddOnContentIdBelongsToApplicationId(app_id, title_info->meta_key.id))
                {
                    aoc_info = title_info;
                    break;
                } else
                if (title_info->meta_key.type == NcmContentMetaType_DataPatch && titleCheckIfDataPatchIdBelongsToApplicationId(app_id, title_info->meta_key.id))
                {
                    aoc_patch_info = title_info;
                    break;
                }
            }

            TITLE_ALLOCATE_USER_APP_DATA(aoc, "add-on content", error = true; break);

            TITLE_ALLOCATE_USER_APP_DATA(aoc_patch, "add-on content patch", error = true; break);

            if (out->aoc_info && out->aoc_patch_info) break;
        }

        if (error) break;

#undef TITLE_ALLOCATE_USER_APP_DATA

        /* Check retrieved title info. */
        ret = (app_info || patch_info || aoc_info || aoc_patch_info);
        if (!ret) LOG_MSG_ERROR("Failed to retrieve user application data for ID \"%016lX\"!", app_id);
    }

    /* Clear output. */
    if (!ret) titleFreeUserApplicationData(out);

    return ret;
}

void titleFreeUserApplicationData(TitleUserApplicationData *user_app_data)
{
    if (!user_app_data) return;

    /* Free user application info. */
    titleFreeTitleInfo(&(user_app_data->app_info));

    /* Free patch info. */
    titleFreeTitleInfo(&(user_app_data->patch_info));

    /* Free add-on content info. */
    titleFreeTitleInfo(&(user_app_data->aoc_info));

    /* Free add-on content patch info. */
    titleFreeTitleInfo(&(user_app_data->aoc_patch_info));
}

TitleInfo *titleGetAddOnContentBaseOrPatchList(TitleInfo *title_info)
{
    TitleInfo *out = NULL;
    bool success = false;

    SCOPED_LOCK(&g_titleMutex)
    {
        if (!g_titleInterfaceInit || !titleIsValidInfoBlock(title_info) || (title_info->meta_key.type != NcmContentMetaType_AddOnContent && \
            title_info->meta_key.type != NcmContentMetaType_DataPatch))
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        TitleInfo *aoc_info = NULL, *tmp = NULL;
        u64 ref_tid = title_info->meta_key.id;
        u64 lookup_tid = (title_info->meta_key.type == NcmContentMetaType_AddOnContent ? titleGetDataPatchIdByAddOnContentId(ref_tid) : titleGetAddOnContentIdByDataPatchId(ref_tid));
        bool error = false;

        /* Get info for the first add-on content (patch) title matching the lookup title ID. */
        aoc_info = _titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_Any, lookup_tid);
        if (!aoc_info) break;

        /* Create our own custom linked list using entries that match our lookup title ID. */
        while(aoc_info)
        {
            /* Check if this entry's title ID matches our lookup title ID. */
            if (aoc_info->meta_key.id != lookup_tid)
            {
                aoc_info = aoc_info->next;
                continue;
            }

            /* Duplicate current entry. */
            tmp = titleDuplicateTitleInfo(aoc_info);
            if (!tmp)
            {
                LOG_MSG_ERROR("Failed to duplicate TitleInfo object!");
                error = true;
                break;
            }

            /* Update pointer. */
            if (out)
            {
                out->next = tmp;
            } else {
                out = tmp;
            }

            tmp = NULL;

            /* Proceed onto the next entry. */
            aoc_info = aoc_info->next;
        }

        if (error) break;

        /* Update flag. */
        success = true;
    }

    if (!success && out) titleFreeTitleInfo(&out);

    return out;
}

bool titleAreOrphanTitlesAvailable(void)
{
    bool ret = false;
    SCOPED_TRY_LOCK(&g_titleMutex) ret = (g_titleInterfaceInit && g_orphanTitleInfo && *g_orphanTitleInfo && g_orphanTitleInfoCount > 0);
    return ret;
}

TitleInfo **titleGetOrphanTitles(u32 *out_count)
{
    TitleInfo **orphan_info = NULL;

    SCOPED_LOCK(&g_titleMutex)
    {
        if (!g_titleInterfaceInit || !g_orphanTitleInfo || !*g_orphanTitleInfo || !g_orphanTitleInfoCount || !out_count)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        /* Allocate orphan title info pointer array. */
        /* titleFreeOrphanTitles() depends on the last NULL element. */
        orphan_info = calloc(g_orphanTitleInfoCount + 1, sizeof(TitleInfo*));
        if (!orphan_info)
        {
            LOG_MSG_ERROR("Failed to allocate memory for orphan title info pointer array!");
            break;
        }

        /* Duplicate orphan title info entries. */
        for(u32 i = 0; i < g_orphanTitleInfoCount; i++)
        {
            orphan_info[i] = titleDuplicateTitleInfoFull(g_orphanTitleInfo[i], NULL, NULL);
            if (!orphan_info[i])
            {
                LOG_MSG_ERROR("Failed to duplicate info for orphan title %016lX!", g_orphanTitleInfo[i]->meta_key.id);
                titleFreeOrphanTitles(&orphan_info);
                break;
            }
        }

        /* Update output counter. */
        if (orphan_info) *out_count = g_orphanTitleInfoCount;
    }

    return orphan_info;
}

void titleFreeOrphanTitles(TitleInfo ***orphan_info)
{
    TitleInfo **ptr = NULL, *tmp = NULL;
    if (!orphan_info || !(ptr = *orphan_info)) return;

    tmp = *ptr;
    while(tmp)
    {
        titleFreeTitleInfo(&tmp);
        tmp++;
    }

    free(ptr);
    *orphan_info = NULL;
}

bool titleIsGameCardInfoUpdated(void)
{
    bool ret = false;

    SCOPED_TRY_LOCK(&g_titleMutex)
    {
        /* Check if the gamecard thread detected a gamecard status change. */
        ret = (g_titleInterfaceInit && g_titleGameCardInfoUpdated);
        if (ret) g_titleGameCardInfoUpdated = false;
    }

    return ret;
}

char *titleGenerateFileName(TitleInfo *title_info, u8 naming_convention, u8 illegal_char_replace_type)
{
    if (!title_info || (title_info->meta_key.type > NcmContentMetaType_BootImagePackageSafe && title_info->meta_key.type < NcmContentMetaType_Application) || \
        title_info->meta_key.type > NcmContentMetaType_DataPatch || naming_convention >= TitleNamingConvention_Count || illegal_char_replace_type >= TitleFileNameIllegalCharReplaceType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    u8 type_idx = title_info->meta_key.type;
    if (type_idx >= NcmContentMetaType_Application) type_idx -= NCM_CMT_APP_OFFSET;

    char title_name[0x300] = {0}, *filename = NULL;
    size_t title_name_len = 0;

    /* Generate filename for this title. */
    if (naming_convention == TitleNamingConvention_Full)
    {
        if (title_info->app_metadata && *(title_info->app_metadata->lang_entry.name))
        {
            snprintf(title_name, MAX_ELEMENTS(title_name), "%s ", title_info->app_metadata->lang_entry.name);

            /* Retrieve display version string if we're dealing with a Patch. */
            char *version_str = (title_info->meta_key.type == NcmContentMetaType_Patch ? titleGetDisplayVersionString(title_info) : NULL);
            if (version_str)
            {
                title_name_len = strlen(title_name);
                snprintf(title_name + title_name_len, MAX_ELEMENTS(title_name) - title_name_len, "%s ", version_str);
                free(version_str);
            }

            if (illegal_char_replace_type) utilsReplaceIllegalCharacters(title_name, illegal_char_replace_type == TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly);
        }

        title_name_len = strlen(title_name);
        snprintf(title_name + title_name_len, MAX_ELEMENTS(title_name) - title_name_len, "[%016lX][v%u][%s]", title_info->meta_key.id, title_info->meta_key.version, \
                                                                                                              g_filenameTypeStrings[type_idx]);
    } else
    if (naming_convention == TitleNamingConvention_IdAndVersionOnly)
    {
        snprintf(title_name, MAX_ELEMENTS(title_name), "%016lX_v%u_%s", title_info->meta_key.id, title_info->meta_key.version, g_filenameTypeStrings[type_idx]);
    }

    /* Duplicate generated filename. */
    filename = strdup(title_name);
    if (!filename) LOG_MSG_ERROR("Failed to duplicate generated filename!");

    return filename;
}

char *titleGenerateGameCardFileName(u8 naming_convention, u8 illegal_char_replace_type)
{
    char *filename = NULL;

    SCOPED_LOCK(&g_titleMutex)
    {
        if (!g_titleInterfaceInit || !g_titleGameCardAvailable || naming_convention >= TitleNamingConvention_Count || \
            illegal_char_replace_type >= TitleFileNameIllegalCharReplaceType_Count || !g_titleGameCardFileNames[naming_convention])
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        /* Duplicate generated filename. */
        filename = strdup(g_titleGameCardFileNames[naming_convention]);
        if (!filename)
        {
            LOG_MSG_ERROR("Failed to duplicate generated filename!");
            break;
        }

        /* Replace illegal characters, if requested. */
        if (illegal_char_replace_type) utilsReplaceIllegalCharacters(filename, illegal_char_replace_type == TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly);
    }

    return filename;
}

const char *titleGetNcmStorageIdName(u8 storage_id)
{
    return (storage_id <= NcmStorageId_Any ? g_titleNcmStorageIdNames[storage_id] : NULL);
}

const char *titleGetNcmContentTypeName(u8 content_type)
{
    return (content_type <= NcmContentType_DeltaFragment ? g_titleNcmContentTypeNames[content_type] : NULL);
}

const char *titleGetNcmContentMetaTypeName(u8 content_meta_type)
{
    if ((content_meta_type > NcmContentMetaType_BootImagePackageSafe && content_meta_type < NcmContentMetaType_Application) || content_meta_type > NcmContentMetaType_DataPatch) return NULL;
    return (content_meta_type <= NcmContentMetaType_BootImagePackageSafe ? g_titleNcmContentMetaTypeNames[content_meta_type] : g_titleNcmContentMetaTypeNames[content_meta_type - NCM_CMT_APP_OFFSET]);
}

NX_INLINE void titleFreeApplicationMetadata(void)
{
    /* Free cached application metadata. */
    for(u8 i = 0; i < 2; i++)
    {
        TitleApplicationMetadata **cached_app_metadata = (i == 0 ? g_systemMetadata : g_userMetadata);
        u32 cached_app_metadata_count = (i == 0 ? g_systemMetadataCount : g_userMetadataCount);

        if (cached_app_metadata)
        {
            for(u32 j = 0; j < cached_app_metadata_count; j++)
            {
                TitleApplicationMetadata *cur_app_metadata = cached_app_metadata[j];
                if (cur_app_metadata)
                {
                    if (cur_app_metadata->icon) free(cur_app_metadata->icon);
                    free(cur_app_metadata);
                }
            }

            free(cached_app_metadata);
        }
    }

    g_systemMetadata = g_userMetadata = NULL;
    g_systemMetadataCount = g_userMetadataCount = 0;

    /* Free filtered application metadata. */
    if (g_filteredSystemMetadata) free(g_filteredSystemMetadata);

    if (g_filteredUserMetadata) free(g_filteredUserMetadata);

    g_filteredSystemMetadata = g_filteredUserMetadata = NULL;
    g_filteredSystemMetadataCount = g_filteredUserMetadataCount = 0;

    /* Free gamecard application metadata. */
    if (g_titleGameCardApplicationMetadata) free(g_titleGameCardApplicationMetadata);

    g_titleGameCardApplicationMetadata = NULL;
    g_titleGameCardApplicationMetadataCount = 0;
}

static bool titleReallocateApplicationMetadata(u32 extra_app_count, bool is_system, bool free_entries)
{
    TitleApplicationMetadata **cached_app_metadata = (is_system ? g_systemMetadata : g_userMetadata), **tmp_app_metadata = NULL;
    u32 cached_app_metadata_count = (is_system ? g_systemMetadataCount : g_userMetadataCount);
    u32 realloc_app_count = (!free_entries ? (cached_app_metadata_count + extra_app_count) : cached_app_metadata_count);
    bool success = false;

    if (free_entries)
    {
        if (!cached_app_metadata)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            goto end;
        }

        /* Free previously allocated application metadata entries. */
        for(u32 i = 0; i <= extra_app_count; i++)
        {
            TitleApplicationMetadata *cur_app_metadata = cached_app_metadata[cached_app_metadata_count + i];
            if (cur_app_metadata)
            {
                if (cur_app_metadata->icon) free(cur_app_metadata->icon);
                free(cur_app_metadata);
                cached_app_metadata[cached_app_metadata_count + i] = NULL;
            }
        }
    }

    if (realloc_app_count)
    {
        /* Reallocate application metadata pointer array. */
        tmp_app_metadata = realloc(cached_app_metadata, realloc_app_count * sizeof(TitleApplicationMetadata*));
        if (tmp_app_metadata)
        {
            /* Update application metadata pointer. */
            cached_app_metadata = tmp_app_metadata;
            tmp_app_metadata = NULL;

            /* Clear new application metadata pointer array area (if needed). */
            if (realloc_app_count > cached_app_metadata_count) memset(cached_app_metadata + cached_app_metadata_count, 0, extra_app_count * sizeof(TitleApplicationMetadata*));
        } else {
            LOG_MSG_ERROR("Failed to reallocate application metadata pointer array! (%u element[s]).", realloc_app_count);
            goto end;
        }
    } else
    if (cached_app_metadata)
    {
        /* Free application metadata pointer array. */
        free(cached_app_metadata);
        cached_app_metadata = NULL;
    }

    /* Update global pointer array. */
    if (is_system)
    {
        g_systemMetadata = cached_app_metadata;
    } else {
        g_userMetadata = cached_app_metadata;
    }

    /* Update flag. */
    success = true;

end:
    return success;
}

NX_INLINE bool titleInitializePersistentTitleStorages(void)
{
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        if (!titleInitializeTitleStorage(i))
        {
            LOG_MSG_ERROR("Failed to initialize title storage with ID %u!", i);
            return false;
        }
    }

#if LOG_LEVEL <= LOG_LEVEL_INFO
#define ORPHAN_INFO_LOG(fmt, ...) utilsAppendFormattedStringToBuffer(&orphan_info_buf, &orphan_info_buf_size, fmt, ##__VA_ARGS__)

    if (g_orphanTitleInfo && g_orphanTitleInfoCount)
    {
        char *orphan_info_buf = NULL;
        size_t orphan_info_buf_size = 0;

        ORPHAN_INFO_LOG("Identified %u orphan title(s) across all initialized title storages.\r\n", g_orphanTitleInfoCount);

        for(u32 i = 0; i < g_orphanTitleInfoCount; i++)
        {
            TitleInfo *orphan_info = g_orphanTitleInfo[i];
            ORPHAN_INFO_LOG("- %016lX v%u (%s, %s).%s", orphan_info->meta_key.id, orphan_info->version.value, titleGetNcmContentMetaTypeName(orphan_info->meta_key.type), \
                                                      titleGetNcmStorageIdName(orphan_info->storage_id), (i + 1) < g_orphanTitleInfoCount ? "\r\n" : "");
        }

        if (orphan_info_buf)
        {
            LOG_MSG_INFO("%s", orphan_info_buf);
            free(orphan_info_buf);
        }
    }

#undef ORPHAN_INFO_LOG
#endif  /* LOG_LEVEL <= LOG_LEVEL_INFO */

    return true;
}

NX_INLINE void titleCloseTitleStorages(void)
{
    for(u8 i = NcmStorageId_GameCard; i <= NcmStorageId_SdCard; i++) titleCloseTitleStorage(i);
}

static bool titleInitializeTitleStorage(u8 storage_id)
{
    if (storage_id < NcmStorageId_GameCard || storage_id > NcmStorageId_SdCard)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Close title storage before proceeding. */
    titleCloseTitleStorage(storage_id);

    TitleStorage *title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(storage_id)]);
    NcmContentMetaDatabase *ncm_db = &(title_storage->ncm_db);
    NcmContentStorage *ncm_storage = &(title_storage->ncm_storage);

    Result rc = 0;
    bool success = false;

    /* Set ncm storage ID. */
    title_storage->storage_id = storage_id;

    /* Open ncm database. */
    rc = ncmOpenContentMetaDatabase(ncm_db, storage_id);
    if (R_FAILED(rc))
    {
        /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
        /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
        LOG_MSG_ERROR("ncmOpenContentMetaDatabase failed for %s! (0x%X).", titleGetNcmStorageIdName(storage_id), rc);
        if (storage_id == NcmStorageId_SdCard && rc == 0x21005) success = true;
        goto end;
    }

    /* Open ncm storage. */
    rc = ncmOpenContentStorage(ncm_storage, storage_id);
    if (R_FAILED(rc))
    {
        /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
        /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
        LOG_MSG_ERROR("ncmOpenContentStorage failed for %s! (0x%X).", titleGetNcmStorageIdName(storage_id), rc);
        if (storage_id == NcmStorageId_SdCard && rc == 0x21005) success = true;
        goto end;
    }

    /* Generate title info entries for this storage. */
    if (!titleGenerateTitleInfoEntriesForTitleStorage(title_storage))
    {
        LOG_MSG_ERROR("Failed to generate title info entries for %s!", titleGetNcmStorageIdName(storage_id));
        goto end;
    }

    LOG_MSG_INFO("Loaded %u title info %s from %s.", title_storage->title_count, (title_storage->title_count == 1 ? "entry" : "entries"), titleGetNcmStorageIdName(storage_id));

    /* Update flag. */
    success = true;

end:
    return success;
}

static bool titleInitializeGameCardTitleStorageByHashFileSystem(u8 hfs_partition_type)
{
    if (hfs_partition_type < HashFileSystemPartitionType_Root || hfs_partition_type >= HashFileSystemPartitionType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Close title storage before proceeding. */
    titleCloseTitleStorage(NcmStorageId_GameCard);

    TitleStorage *title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(NcmStorageId_GameCard)]);
    HashFileSystemContext hfs_ctx = {0};
    bool success = false;

    /* Set ncm storage ID. */
    title_storage->storage_id = NcmStorageId_GameCard;

    /* Get Hash FS context for the desired partition on the gamecard. */
    if (!gamecardGetHashFileSystemContext(hfs_partition_type, &hfs_ctx))
    {
        LOG_MSG_ERROR("Failed to retrieve HFS context for the gamecard's %s partition.", hfsGetPartitionNameString(hfs_partition_type));
        goto end;
    }

    /* Generate title info entries for this storage. */
    if (!titleGenerateTitleInfoEntriesByHashFileSystemForGameCardTitleStorage(title_storage, &hfs_ctx))
    {
        LOG_MSG_ERROR("Failed to generate title info entries!");
        goto end;
    }

    LOG_MSG_INFO("Loaded %u title info %s.", title_storage->title_count, (title_storage->title_count == 1 ? "entry" : "entries"));

    /* Update flag. */
    success = true;

end:
    hfsFreeContext(&hfs_ctx);

    return success;
}

static void titleCloseTitleStorage(u8 storage_id)
{
    if (storage_id < NcmStorageId_GameCard || storage_id > NcmStorageId_SdCard) return;

    TitleStorage *title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(storage_id)]);
    NcmContentMetaDatabase *ncm_db = &(title_storage->ncm_db);
    NcmContentStorage *ncm_storage = &(title_storage->ncm_storage);

    /* Free title infos from this title storage. */
    if (title_storage->titles)
    {
        for(u32 i = 0; i < title_storage->title_count; i++)
        {
            TitleInfo *cur_title_info = title_storage->titles[i];
            if (cur_title_info)
            {
                if (cur_title_info->content_infos) free(cur_title_info->content_infos);
                free(cur_title_info);
            }
        }

        free(title_storage->titles);
        title_storage->titles = NULL;
    }

    /* Reset title count. */
    title_storage->title_count = 0;

    /* Check if the ncm storage handle for this title storage has already been retrieved. If so, close it. */
    if (serviceIsActive(&(ncm_storage->s))) ncmContentStorageClose(ncm_storage);

    /* Check if the ncm database handle for this title storage has already been retrieved. If so, close it. */
    if (serviceIsActive(&(ncm_db->s))) ncmContentMetaDatabaseClose(ncm_db);

    /* Reset ncm storage ID. */
    title_storage->storage_id = NcmStorageId_None;
}

static bool titleReallocateTitleInfoFromStorage(TitleStorage *title_storage, u32 extra_title_count, bool free_entries)
{
    if (!title_storage)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    TitleInfo **title_info = title_storage->titles, **tmp_title_info = NULL;
    u32 title_count = title_storage->title_count;
    u32 realloc_title_count = (!free_entries ? (title_count + extra_title_count) : title_count);
    bool success = false;

    if (free_entries)
    {
        if (!title_info)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            goto end;
        }

        /* Free previously allocated title info entries. */
        for(u32 i = 0; i <= extra_title_count; i++)
        {
            TitleInfo *cur_title_info = title_info[title_count + i];
            if (cur_title_info)
            {
                if (cur_title_info->content_infos) free(cur_title_info->content_infos);
                free(cur_title_info);
                title_info[title_count + i] = NULL;
            }
        }
    }

    if (realloc_title_count)
    {
        /* Reallocate title info pointer array. */
        tmp_title_info = realloc(title_info, realloc_title_count * sizeof(TitleInfo*));
        if (tmp_title_info)
        {
            /* Update title info pointer. */
            title_info = tmp_title_info;
            tmp_title_info = NULL;

            /* Clear new title info pointer array area (if needed). */
            if (realloc_title_count > title_count) memset(title_info + title_count, 0, extra_title_count * sizeof(TitleInfo*));
        } else {
            LOG_MSG_ERROR("Failed to reallocate title info pointer array! (%u element[s]).", realloc_title_count);
            goto end;
        }
    } else
    if (title_info)
    {
        /* Free title info pointer array. */
        free(title_info);
        title_info = NULL;
    }

    /* Update pointer array in global title storage. */
    title_storage->titles = title_info;

    /* Update flag. */
    success = true;

end:
    return success;
}

NX_INLINE void titleFreeOrphanTitleInfoEntries(void)
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
        LOG_MSG_ERROR("Failed to reallocate orphan title info pointer array!");
        return;
    }

    g_orphanTitleInfo = tmp_orphan_info;
    tmp_orphan_info = NULL;

    /* Set orphan title info entry pointer. */
    g_orphanTitleInfo[g_orphanTitleInfoCount++] = orphan_title;

    /* Sort orphan title info entries by title ID, version and storage ID. */
    if (g_orphanTitleInfoCount > 1) qsort(g_orphanTitleInfo, g_orphanTitleInfoCount, sizeof(TitleInfo*), &titleInfoSortFunction);
}

static bool titleGenerateMetadataEntriesFromSystemTitles(void)
{
    u32 extra_app_count = 0;
    bool success = false;

    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(g_systemTitlesCount, true, false))
    {
        LOG_MSG_ERROR("Failed to reallocate application metadata pointer array for system titles!");
        return false;
    }

    /* Fill new application metadata entries. */
    for(extra_app_count = 0; extra_app_count < g_systemTitlesCount; extra_app_count++)
    {
        /* Allocate memory for the current entry. */
        TitleApplicationMetadata *cur_app_metadata = calloc(1, sizeof(TitleApplicationMetadata));
        if (!cur_app_metadata)
        {
            LOG_MSG_ERROR("Failed to allocate memory for application metadata entry #%u!", extra_app_count);
            goto end;
        }

        /* Fill information. */
        const TitleSystemEntry *system_title = &(g_systemTitles[extra_app_count]);
        cur_app_metadata->title_id = system_title->title_id;
        sprintf(cur_app_metadata->lang_entry.name, "%s", system_title->name);

        /* Set application metadata entry pointer. */
        g_systemMetadata[g_systemMetadataCount + extra_app_count] = cur_app_metadata;
    }

    /* Update application metadata count. */
    g_systemMetadataCount += g_systemTitlesCount;

    /* Sort application metadata entries by title ID. */
    if (g_systemMetadataCount > 1) qsort(g_systemMetadata, g_systemMetadataCount, sizeof(TitleApplicationMetadata*), &titleSystemMetadataSortFunction);

    /* Update flag. */
    success = true;

end:
    /* Free previously allocated application metadata pointers. Ignore return value. */
    if (!success) titleReallocateApplicationMetadata(extra_app_count, true, true);

    return success;
}

static bool titleGenerateMetadataEntriesFromNsRecords(void)
{
    Result rc = 0;

    NsApplicationRecord *app_records = NULL, *tmp_app_records = NULL;
    u32 app_records_block_count = 0, app_records_count = 0, extra_app_count = 0;
    size_t app_records_size = 0, app_records_block_size = (NS_APPLICATION_RECORD_BLOCK_SIZE * sizeof(NsApplicationRecord));

    bool success = false, free_entries = false;

    /* Retrieve NS application records in a loop until we get them all. */
    do {
        /* Allocate memory for the NS application records. */
        tmp_app_records = realloc(app_records, app_records_size + app_records_block_size);
        if (!tmp_app_records)
        {
            LOG_MSG_ERROR("Failed to reallocate NS application records buffer! (%u)", app_records_count);
            goto end;
        }

        app_records = tmp_app_records;
        tmp_app_records = NULL;
        app_records_size += app_records_block_size;

        /* Clear newly allocated block. */
        NsApplicationRecord *app_records_block = &(app_records[app_records_count]);
        memset(app_records_block, 0, app_records_block_size);

        /* Retrieve NS application records. */
        rc = nsListApplicationRecord(app_records_block, NS_APPLICATION_RECORD_BLOCK_SIZE, (s32)app_records_count, (s32*)&app_records_block_count);
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("nsListApplicationRecord failed! (0x%X) (%u).", rc, app_records_count);
            if (!app_records_count) goto end;
            break; /* Gotta work with what we have. */
        }

        app_records_count += app_records_block_count;
    } while(app_records_block_count >= NS_APPLICATION_RECORD_BLOCK_SIZE);

    /* Return right away if no records are available. */
    if (!app_records_count)
    {
        success = true;
        goto end;
    }

    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(app_records_count, false, false))
    {
        LOG_MSG_ERROR("Failed to reallocate application metadata pointer array for NS records!");
        goto end;
    }

    free_entries = true;

    /* Retrieve application metadata for each NS application record. */
    for(u32 i = 0; i < app_records_count; i++)
    {
        /* Retrieve application metadata. */
        TitleApplicationMetadata *cur_app_metadata = titleGenerateUserMetadataEntryFromNs(app_records[i].application_id);
        if (!cur_app_metadata) continue;

        /* Set application metadata entry pointer. */
        g_userMetadata[g_userMetadataCount + extra_app_count] = cur_app_metadata;

        /* Increase extra application metadata counter. */
        extra_app_count++;
    }

    /* Check retrieved application metadata count. */
    if (!extra_app_count)
    {
        LOG_MSG_ERROR("Unable to retrieve application metadata from NS application records! (%u element[s]).", app_records_count);
        goto end;
    }

    /* Update application metadata count. */
    g_userMetadataCount += extra_app_count;

    /* Free extra allocated pointers if we didn't use them. */
    if (extra_app_count < app_records_count) titleReallocateApplicationMetadata(0, false, false);

    /* Sort application metadata entries by name. */
    if (g_userMetadataCount > 1) qsort(g_userMetadata, g_userMetadataCount, sizeof(TitleApplicationMetadata*), &titleUserMetadataSortFunction);

    /* Update flag. */
    success = true;

end:
    if (app_records) free(app_records);

    /* Free previously allocated application metadata pointers. Ignore return value. */
    if (!success && free_entries) titleReallocateApplicationMetadata(extra_app_count, false, true);

    return success;
}

static TitleApplicationMetadata *titleGetSystemMetadataEntry(u64 title_id)
{
    if (!title_id)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    TitleApplicationMetadata *app_metadata = NULL;
    bool success = false;

    /* Make sure we have no application metadata entry for the requested title ID. */
    /* If we do, we'll just return a pointer to the existing entry. */
    app_metadata = titleFindApplicationMetadataByTitleId(title_id, true, 0);
    if (app_metadata)
    {
        success = true;
        goto end;
    }

    /* Allocate memory for the current entry. */
    app_metadata = calloc(1, sizeof(TitleApplicationMetadata));
    if (!app_metadata)
    {
        LOG_MSG_ERROR("Failed to allocate memory for application metadata entry for %016lX!", title_id);
        goto end;
    }

    /* Fill information for our dummy entry. */
    app_metadata->title_id = title_id;
    sprintf(app_metadata->lang_entry.name, "Unknown");

    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(1, true, false))
    {
        LOG_MSG_ERROR("Failed to reallocate application metadata pointer array for %016lX!", title_id);
        goto end;
    }

    /* Set application metadata entry pointer. */
    g_systemMetadata[g_systemMetadataCount++] = app_metadata;

    /* Sort application metadata entries by title ID. */
    if (g_systemMetadataCount > 1) qsort(g_systemMetadata, g_systemMetadataCount, sizeof(TitleApplicationMetadata*), &titleSystemMetadataSortFunction);

    /* Update flag. */
    success = true;

end:
    if (!success && app_metadata)
    {
        free(app_metadata);
        app_metadata = NULL;
    }

    return app_metadata;
}

static TitleApplicationMetadata *titleGenerateUserMetadataEntryFromNs(u64 title_id)
{
    if (!g_nsAppControlData || !title_id)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    u64 control_data_size = 0;
    TitleApplicationMetadata *app_metadata = NULL;

    /* Retrieve application control data from ns. */
    if (!titleGetApplicationControlDataFromNs(title_id, g_nsAppControlData, &control_data_size))
    {
        LOG_MSG_ERROR("Failed to retrieve application control data for %016lX!", title_id);
        goto end;
    }

    /* Initialize application metadata entry using the control data we just retrieved. */
    app_metadata = titleInitializeUserMetadataEntryFromControlData(title_id, g_nsAppControlData, control_data_size);
    if (!app_metadata) LOG_MSG_ERROR("Failed to generate application metadata entry for %016lX!", title_id);

end:
    return app_metadata;
}

static TitleApplicationMetadata *titleGenerateUserMetadataEntryFromControlNca(TitleInfo *title_info)
{
    if (!g_nsAppControlData || !title_info || !title_info->meta_key.id || title_info->app_metadata || \
        (title_info->meta_key.type != NcmContentMetaType_Application && title_info->meta_key.type != NcmContentMetaType_Patch))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    u64 control_data_size = 0, title_id = title_info->meta_key.id, app_id = titleGetApplicationIdByContentMetaKey(&(title_info->meta_key));
    TitleApplicationMetadata *app_metadata = NULL;

    /* Retrieve application control data from Control NCA. */
    if (!titleGetApplicationControlDataFromControlNca(title_info, g_nsAppControlData, &control_data_size))
    {
        LOG_MSG_ERROR("Failed to retrieve application control data for %016lX!", title_id);
        goto end;
    }

    /* Initialize application metadata entry using the control data we just retrieved. */
    app_metadata = titleInitializeUserMetadataEntryFromControlData(app_id, g_nsAppControlData, control_data_size);
    if (!app_metadata) LOG_MSG_ERROR("Failed to generate application metadata entry for %016lX!", title_id);

end:
    return app_metadata;
}

static bool titleGetApplicationControlDataFromNs(u64 title_id, NsApplicationControlData *out_control_data, u64 *out_control_data_size)
{
    if (!title_id || !out_control_data || !out_control_data_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    Result rc = 0;
    u64 control_data_size = 0;

    /* Retrieve application control data from ns. */
    rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, out_control_data, sizeof(NsApplicationControlData), &control_data_size);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("nsGetApplicationControlData failed for title ID \"%016lX\"! (0x%X).", title_id, rc);
        return false;
    }

    /* Sanity check. */
    if (control_data_size < sizeof(NacpStruct))
    {
        LOG_MSG_ERROR("Retrieved application control data buffer for title ID \"%016lX\" is too small! (0x%lX).", title_id, control_data_size);
        return false;
    }

    /* Update output size. */
    *out_control_data_size = control_data_size;

    return true;
}

static bool titleGetApplicationControlDataFromControlNca(TitleInfo *title_info, NsApplicationControlData *out_control_data, u64 *out_control_data_size)
{
    NcmContentInfo *nacp_content = NULL;

    if (!title_info || !title_info->meta_key.id || (title_info->meta_key.type != NcmContentMetaType_Application && title_info->meta_key.type != NcmContentMetaType_Patch) || \
        !(nacp_content = titleGetContentInfoByTypeAndIdOffset(title_info, NcmContentType_Control, 0)) || !out_control_data || !out_control_data_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u64 title_id = title_info->meta_key.id;
    u8 storage_id = title_info->storage_id;
    u8 hfs_partition_type = (storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : HashFileSystemPartitionType_None);

    NcaContext *nca_ctx = NULL;
    NacpContext nacp_ctx = {0};

    Result rc = 0;
    NacpLanguageEntry *lang_entry = NULL;
    u8 lang_id = NacpLanguage_AmericanEnglish;  /* Fallback value. */

    NacpIconContext *icon_ctx = NULL;

    bool success = false;

    LOG_MSG_DEBUG("Retrieving application control data for %s %016lX in %s...", titleGetNcmContentMetaTypeName(title_info->meta_key.type), title_id, \
                                                                                titleGetNcmStorageIdName(title_info->storage_id));

    /* Allocate memory for the NCA context. */
    nca_ctx = calloc(1, sizeof(NcaContext));
    if (!nca_ctx)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NCA context!");
        goto end;
    }

    /* Initialize NCA context. */
    if (!ncaInitializeContext(nca_ctx, storage_id, hfs_partition_type, &(title_info->meta_key), nacp_content, NULL))
    {
        LOG_MSG_ERROR("Failed to initialize NCA context for Control NCA from %016lX!", title_id);
        goto end;
    }

    /* Initialize NACP context. */
    if (!nacpInitializeContext(&nacp_ctx, nca_ctx))
    {
        LOG_MSG_ERROR("Failed to initialize NACP context for %016lX!", title_id);
        goto end;
    }

    /* Get language entry. */
    rc = nacpGetLanguageEntry((NacpStruct*)nacp_ctx.data, &lang_entry);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("nacpGetLanguageEntry failed! (0x%X).", rc);
        goto end;
    }

    /* Determine language ID from the selected entry. */
    for(u8 i = NacpLanguage_AmericanEnglish; i < NacpLanguage_Count; i++)
    {
        /* Don't proceed any further if no language entry was retrieved. */
        if (!lang_entry) break;

        NacpTitle *cur_title = &(nacp_ctx.data->title[i]);
        if (cur_title == (NacpTitle*)lang_entry)
        {
            lang_id = i;
            LOG_MSG_DEBUG("Selected language ID for %016lX: %u (%s).", title_id, lang_id, nacpGetLanguageString(lang_id));
            break;
        }
    }

    /* Find the right NACP icon for the selected language entry. */
    for(u8 i = 0; i < nacp_ctx.icon_count; i++)
    {
        icon_ctx = &(nacp_ctx.icon_ctx[i]);
        if (icon_ctx->language == lang_id) break;
        icon_ctx = NULL;
    }

    /* Fallback to the first available icon if we couldn't find one for our language ID. */
    if (!icon_ctx && nacp_ctx.icon_ctx) icon_ctx = &(nacp_ctx.icon_ctx[0]);

    /* Sanity check. */
    if (icon_ctx && (!icon_ctx->icon_size || icon_ctx->icon_size > NACP_MAX_ICON_SIZE))
    {
        LOG_MSG_ERROR("Invalid icon size! (0x%lX)", icon_ctx->icon_size);
        goto end;
    }

    /* Fill output. */
    memcpy(&(out_control_data->nacp), nacp_ctx.data, sizeof(NacpStruct));

    if (icon_ctx)
    {
        size_t diff = (NACP_MAX_ICON_SIZE - icon_ctx->icon_size);
        memcpy(out_control_data->icon, icon_ctx->icon_data, icon_ctx->icon_size);
        if (diff) memset(out_control_data->icon + icon_ctx->icon_size, 0, diff);
    } else {
        memset(out_control_data->icon, 0, NACP_MAX_ICON_SIZE);
    }

    *out_control_data_size = (sizeof(NacpStruct) + (icon_ctx ? icon_ctx->icon_size : 0));

    /* Update flag. */
    success = true;

end:
    nacpFreeContext(&nacp_ctx);

    if (nca_ctx) free(nca_ctx);

    return success;
}

static TitleApplicationMetadata *titleInitializeUserMetadataEntryFromControlData(u64 title_id, const NsApplicationControlData *control_data, u64 control_data_size)
{
    if (!title_id || !control_data || control_data_size < sizeof(NacpStruct))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    Result rc = 0;
    NacpLanguageEntry *lang_entry = NULL;
    u32 icon_size = 0;
    TitleApplicationMetadata *out = NULL;
    bool success = false;

    /* Get language entry. */
    rc = nacpGetLanguageEntry((NacpStruct*)&(control_data->nacp), &lang_entry);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("nacpGetLanguageEntry failed! (0x%X).", rc);
        goto end;
    }

    /* Allocate memory for our application metadata entry. */
    out = calloc(1, sizeof(TitleApplicationMetadata));
    if (!out)
    {
        LOG_MSG_ERROR("Error allocating memory for application metadata entry for %016lX!", title_id);
        goto end;
    }

    /* Calculate icon size. */
    icon_size = (u32)(control_data_size - sizeof(NacpStruct));
    if (icon_size)
    {
        /* Allocate memory for our icon. */
        out->icon = malloc(icon_size);
        if (!out->icon)
        {
            LOG_MSG_ERROR("Error allocating memory for the icon buffer! (0x%X, %016lX).", icon_size, title_id);
            goto end;
        }

        /* Copy icon data. */
        memcpy(out->icon, control_data->icon, icon_size);

        /* Set icon size. */
        out->icon_size = icon_size;
    }

    /* Fill the rest of the information. */
    out->title_id = title_id;

    if (lang_entry)
    {
        memcpy(&(out->lang_entry), lang_entry, sizeof(NacpLanguageEntry));
        utilsTrimString(out->lang_entry.name);
        utilsTrimString(out->lang_entry.author);
    } else {
        /* Yes, this can happen -- NACPs with empty language entries are a thing, somehow. */
        sprintf(out->lang_entry.name, "Unknown");
        sprintf(out->lang_entry.author, "Unknown");
        LOG_DATA_DEBUG(&(control_data->nacp), sizeof(NacpStruct), "NACP dump (ID %016lX):", title_id);
    }

    /* Update flag. */
    success = true;

end:
    if (!success && out)
    {
        free(out);
        out = NULL;
    }

    return out;
}

static void titleGenerateFilteredApplicationMetadataPointerArray(bool is_system)
{
    TitleApplicationMetadata **filtered_app_metadata = NULL, **tmp_filtered_app_metadata = NULL;
    u32 filtered_app_metadata_count = 0;

    TitleApplicationMetadata **cached_app_metadata = (is_system ? g_systemMetadata : g_userMetadata);
    u32 cached_app_metadata_count = (is_system ? g_systemMetadataCount : g_userMetadataCount);

    /* Reset the right pointer and counter based on the input flag. */
    if (is_system)
    {
        if (g_filteredSystemMetadata)
        {
            free(g_filteredSystemMetadata);
            g_filteredSystemMetadata = NULL;
        }

        g_filteredSystemMetadataCount = 0;
    } else {
        if (g_filteredUserMetadata)
        {
            free(g_filteredUserMetadata);
            g_filteredUserMetadata = NULL;
        }

        g_filteredUserMetadataCount = 0;
    }

    /* Make sure we actually have cached application metadata entries we can work with. */
    if (!cached_app_metadata || !cached_app_metadata_count)
    {
        LOG_MSG_ERROR("Cached %s application metadata array is empty!", is_system ? "system" : "user");
        return;
    }

    /* Loop through our cached application metadata entries. */
    for(u32 i = 0; i < cached_app_metadata_count; i++)
    {
        TitleApplicationMetadata *cur_app_metadata = cached_app_metadata[i];
        if (!cur_app_metadata) continue;

        /* Skip current metadata entry if content data for this title isn't available. */
        if ((is_system && !_titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_BuiltInSystem, cur_app_metadata->title_id)) || \
            (!is_system && !titleIsUserApplicationContentAvailable(cur_app_metadata->title_id))) continue;

        /* Reallocate filtered application metadata pointer array. */
        tmp_filtered_app_metadata = realloc(filtered_app_metadata, (filtered_app_metadata_count + 1) * sizeof(TitleApplicationMetadata*));
        if (!tmp_filtered_app_metadata)
        {
            LOG_MSG_ERROR("Failed to reallocate filtered application metadata pointer array!");
            if (filtered_app_metadata) free(filtered_app_metadata);
            return;
        }

        filtered_app_metadata = tmp_filtered_app_metadata;
        tmp_filtered_app_metadata = NULL;

        /* Set current pointer and increase counter. */
        filtered_app_metadata[filtered_app_metadata_count++] = cur_app_metadata;
    }

    if (!filtered_app_metadata || !filtered_app_metadata_count)
    {
        LOG_MSG_ERROR("No content data found for %s!", is_system ? "system titles" : "user applications");
        return;
    }

    /* Update the right pointer and counter based on the input flag. */
    if (is_system)
    {
        g_filteredSystemMetadata = filtered_app_metadata;
        g_filteredSystemMetadataCount = filtered_app_metadata_count;
    } else {
        g_filteredUserMetadata = filtered_app_metadata;
        g_filteredUserMetadataCount = filtered_app_metadata_count;
    }
}

static bool titleIsUserApplicationContentAvailable(u64 app_id)
{
    if (!app_id) return false;

    for(u8 i = NcmStorageId_GameCard; i <= NcmStorageId_SdCard; i++)
    {
        if (i == NcmStorageId_BuiltInSystem) continue;

        TitleStorage *title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(i)]);
        if (!title_storage->titles || !*(title_storage->titles) || !title_storage->title_count) continue;

        for(u32 j = 0; j < title_storage->title_count; j++)
        {
            TitleInfo *title_info = title_storage->titles[j];
            if (!title_info) continue;

            if ((title_info->meta_key.type == NcmContentMetaType_Application && title_info->meta_key.id == app_id) || \
                (title_info->meta_key.type == NcmContentMetaType_Patch && titleCheckIfPatchIdBelongsToApplicationId(app_id, title_info->meta_key.id)) || \
                (title_info->meta_key.type == NcmContentMetaType_AddOnContent && titleCheckIfAddOnContentIdBelongsToApplicationId(app_id, title_info->meta_key.id)) || \
                (title_info->meta_key.type == NcmContentMetaType_DataPatch && titleCheckIfDataPatchIdBelongsToApplicationId(app_id, title_info->meta_key.id))) return true;
        }
    }

    return false;
}

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id, bool is_system, u32 extra_app_count)
{
    if (!title_id || (is_system && (!g_systemMetadata || !g_systemMetadataCount)) || (!is_system && (!g_userMetadata || !g_userMetadataCount))) return NULL;

    TitleApplicationMetadata **cached_app_metadata = (is_system ? g_systemMetadata : g_userMetadata);
    u32 cached_app_metadata_count = ((is_system ? g_systemMetadataCount : g_userMetadataCount) + extra_app_count);

    for(u32 i = 0; i < cached_app_metadata_count; i++)
    {
        TitleApplicationMetadata *cur_app_metadata = cached_app_metadata[i];
        if (cur_app_metadata && cur_app_metadata->title_id == title_id) return cur_app_metadata;
    }

    return NULL;
}

NX_INLINE u64 titleGetApplicationIdByContentMetaKey(const NcmContentMetaKey *meta_key)
{
    if (!meta_key) return 0;

    u64 app_id = meta_key->id;

    switch(meta_key->type)
    {
        case NcmContentMetaType_Patch:
            app_id = titleGetApplicationIdByPatchId(meta_key->id);
            break;
        case NcmContentMetaType_AddOnContent:
            app_id = titleGetApplicationIdByAddOnContentId(meta_key->id);
            break;
        case NcmContentMetaType_Delta:
            app_id = titleGetApplicationIdByDeltaId(meta_key->id);
            break;
        case NcmContentMetaType_DataPatch:
            app_id = titleGetApplicationIdByDataPatchId(meta_key->id);
            break;
        default:
            break;
    }

    return app_id;
}

static bool titleGenerateTitleInfoEntriesForTitleStorage(TitleStorage *title_storage)
{
    if (!title_storage || title_storage->storage_id < NcmStorageId_GameCard || title_storage->storage_id > NcmStorageId_SdCard || !serviceIsActive(&(title_storage->ncm_db.s)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u8 storage_id = title_storage->storage_id;
    NcmContentMetaDatabase *ncm_db = &(title_storage->ncm_db);

    u32 meta_key_count = 0, extra_title_count = 0;
    NcmContentMetaKey *meta_keys = NULL;

    bool success = false, free_entries = false;

    /* Get content meta keys for this storage. */
    if (!titleGetMetaKeysFromContentDatabase(ncm_db, &meta_keys, &meta_key_count)) goto end;

    /* Check if we're dealing with an empty storage. */
    if (!meta_key_count)
    {
        success = true;
        goto end;
    }

    /* Reallocate pointer array in title storage. */
    if (!titleReallocateTitleInfoFromStorage(title_storage, meta_key_count, false)) goto end;

    free_entries = true;

    /* Fill new title info entries. */
    for(u32 i = 0; i < meta_key_count; i++)
    {
        NcmContentMetaKey *cur_meta_key = &(meta_keys[i]);

        NcmContentInfo *content_infos = NULL;
        u32 content_count = 0;

        TitleInfo *title_info = NULL;

        /* Get content infos. */
        if (!titleGetContentInfosByMetaKey(ncm_db, cur_meta_key, &content_infos, &content_count))
        {
            LOG_MSG_ERROR("Failed to get content infos for %016lX!", cur_meta_key->id);
            continue;
        }

        /* Generate TitleInfo entry. */
        title_info = titleGenerateTitleInfoEntry(storage_id, cur_meta_key, content_infos, content_count, false);
        if (!title_info)
        {
            LOG_MSG_ERROR("Failed to generate TitleInfo entry for %016lX!", cur_meta_key->id);
            free(content_infos);
            continue;
        }

        /* Set title info entry pointer. */
        title_storage->titles[title_storage->title_count + extra_title_count] = title_info;

        /* Increase extra title info counter. */
        extra_title_count++;
    }

    /* Check retrieved title info count. */
    if (!extra_title_count)
    {
        LOG_MSG_ERROR("Unable to generate title info entries! (%u element[s]).", meta_key_count);
        goto end;
    }

    /* Update title info count. */
    title_storage->title_count += extra_title_count;

    /* Free extra allocated pointers if we didn't use them. */
    if (extra_title_count < meta_key_count) titleReallocateTitleInfoFromStorage(title_storage, 0, false);

    /* Sort title info entries by title ID, version and storage ID. */
    if (title_storage->title_count > 1) qsort(title_storage->titles, title_storage->title_count, sizeof(TitleInfo*), &titleInfoSortFunction);

    /* Update linked lists for user applications, patches and add-on contents. */
    /* This will also keep track of orphan titles - titles with no available application metadata. */
    titleUpdateTitleInfoLinkedLists();

    /* Update flag. */
    success = true;

end:
    /* Free previously allocated title info pointers. Ignore return value. */
    if (!success && free_entries) titleReallocateTitleInfoFromStorage(title_storage, extra_title_count, true);

    if (meta_keys) free(meta_keys);

    return success;
}

static bool titleGenerateTitleInfoEntriesByHashFileSystemForGameCardTitleStorage(TitleStorage *title_storage, HashFileSystemContext *hfs_ctx)
{
    if (!title_storage || title_storage->storage_id != NcmStorageId_GameCard || !hfsIsValidContext(hfs_ctx))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    TitleGameCardContentMetaContext *gc_meta_ctxs = NULL;
    u32 gc_meta_ctx_count = 0, extra_title_count = 0;
    bool success = false, free_entries = false;

    /* Get gamecard Content Meta contexts. */
    if (!titleGetGameCardContentMetaContexts(hfs_ctx, &gc_meta_ctxs, &gc_meta_ctx_count))
    {
        LOG_MSG_ERROR("Failed to retrieve gamecard Content Meta contexts! (%s partition).", hfsGetPartitionNameString(hfs_ctx->type));
        goto end;
    }

    /* Check if we're dealing with an empty storage. */
    if (!gc_meta_ctx_count)
    {
        success = true;
        goto end;
    }

    /* Reallocate pointer array in title storage. */
    if (!titleReallocateTitleInfoFromStorage(title_storage, gc_meta_ctx_count, false)) goto end;

    free_entries = true;

    /* Fill new title info entries. */
    for(u32 i = 0; i < gc_meta_ctx_count; i++)
    {
        TitleGameCardContentMetaContext *cur_gc_meta_ctx = &(gc_meta_ctxs[i]);
        NcmContentMetaKey *meta_key = &(cur_gc_meta_ctx->meta_key);

        NcmContentInfo *content_infos = NULL;
        u32 content_count = 0;

        TitleInfo *title_info = NULL;

        /* Get content infos. */
        if (!titleGetContentInfosByGameCardContentMetaContext(cur_gc_meta_ctx, hfs_ctx, &content_infos, &content_count))
        {
            LOG_MSG_ERROR("Failed to get content infos for %016lX! (%s partition).", meta_key->id, hfsGetPartitionNameString(hfs_ctx->type));
            continue;
        }

        /* Generate TitleInfo entry. */
        title_info = titleGenerateTitleInfoEntry(NcmStorageId_GameCard, meta_key, content_infos, content_count, true);
        if (!title_info)
        {
            LOG_MSG_ERROR("Failed to generate TitleInfo entry for %016lX! (%s partition).", meta_key->id, hfsGetPartitionNameString(hfs_ctx->type));
            free(content_infos);
            continue;
        }

        /* Set title info entry pointer. */
        title_storage->titles[title_storage->title_count + extra_title_count] = title_info;

        /* Increase extra title info counter. */
        extra_title_count++;
    }

    /* Check retrieved title info count. */
    if (!extra_title_count)
    {
        LOG_MSG_ERROR("Unable to generate title info entries! (%u element[s]).", gc_meta_ctx_count);
        goto end;
    }

    /* Update title info count. */
    title_storage->title_count += extra_title_count;

    /* Free extra allocated pointers if we didn't use them. */
    if (extra_title_count < gc_meta_ctx_count) titleReallocateTitleInfoFromStorage(title_storage, 0, false);

    /* Sort title info entries by title ID, version and storage ID. */
    if (title_storage->title_count > 1) qsort(title_storage->titles, title_storage->title_count, sizeof(TitleInfo*), &titleInfoSortFunction);

    /* Update linked lists for user applications, patches and add-on contents. */
    titleUpdateTitleInfoLinkedLists();

    /* Update flag. */
    success = true;

end:
    /* Free previously allocated title info pointers. Ignore return value. */
    if (!success && free_entries) titleReallocateTitleInfoFromStorage(title_storage, extra_title_count, true);

    titleFreeGameCardContentMetaContexts(&gc_meta_ctxs, gc_meta_ctx_count);

    return success;
}

static TitleInfo *titleGenerateTitleInfoEntry(u8 storage_id, const NcmContentMetaKey *meta_key, NcmContentInfo *content_infos, u32 content_count, bool get_control_nca_metadata)
{
    if (storage_id < NcmStorageId_GameCard || storage_id > NcmStorageId_SdCard || !meta_key || !meta_key->id || !content_infos || !content_count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    TitleInfo *title_info = NULL;
    u64 title_size = 0, content_size = 0;

    /* Allocate memory for a new entry. */
    title_info = calloc(1, sizeof(TitleInfo));
    if (!title_info)
    {
        LOG_MSG_ERROR("Failed to allocate memory for title info entry!");
        return NULL;
    }

    /* Calculate title size. */
    for(u32 i = 0; i < content_count; i++)
    {
        ncmContentInfoSizeToU64(&(content_infos[i]), &content_size);
        title_size += content_size;
    }

    /* Fill information. */
    title_info->storage_id = storage_id;
    memcpy(&(title_info->meta_key), meta_key, sizeof(NcmContentMetaKey));
    title_info->version.value = meta_key->version;
    title_info->content_count = content_count;
    title_info->content_infos = content_infos;
    title_info->size = title_size;
    utilsGenerateFormattedSizeString((double)title_size, title_info->size_str, sizeof(title_info->size_str));

    /* Retrieve application metadata. */
    if (storage_id == NcmStorageId_BuiltInSystem)
    {
        /* If no application metadata entry is found, titleGetSystemMetadataEntry() will take care of generating a dummy one. */
        title_info->app_metadata = titleGetSystemMetadataEntry(meta_key->id);
    } else {
        /* Dig through what we have. */
        u64 app_id = titleGetApplicationIdByContentMetaKey(meta_key);
        title_info->app_metadata = titleFindApplicationMetadataByTitleId(app_id, false, 0);
        if (!title_info->app_metadata && get_control_nca_metadata && (meta_key->type == NcmContentMetaType_Application || meta_key->type == NcmContentMetaType_Patch))
        {
            /* Manually retrieve application metadata from this title's Control NCA. */
            titleInitializeTitleInfoApplicationMetadataFromControlNca(title_info);
        }
    }

    return title_info;
}

static bool titleInitializeTitleInfoApplicationMetadataFromControlNca(TitleInfo *title_info)
{
    if (!title_info || !title_info->meta_key.id || \
        (title_info->meta_key.type != NcmContentMetaType_Application && title_info->meta_key.type != NcmContentMetaType_Patch))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    TitleApplicationMetadata *app_metadata = NULL;
    bool success = false;

    /* Return immediately if an application metadata pointer was already set. */
    if (title_info->app_metadata)
    {
        success = true;
        goto end;
    }

    /* Get application metadata. */
    app_metadata = titleGenerateUserMetadataEntryFromControlNca(title_info);
    if (!app_metadata)
    {
        LOG_MSG_ERROR("Failed to generate application metadata from Control NCA for %016lX!", title_info->meta_key.id);
        goto end;
    }

    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(1, false, false))
    {
        LOG_MSG_ERROR("Failed to reallocate application metadata pointer array for %016lX!", title_info->meta_key.id);
        goto end;
    }

    /* Set application metadata entry pointer. */
    g_userMetadata[g_userMetadataCount++] = app_metadata;

    /* Sort application metadata entries by name. */
    if (g_userMetadataCount > 1) qsort(g_userMetadata, g_userMetadataCount, sizeof(TitleApplicationMetadata*), &titleUserMetadataSortFunction);

    /* Update flag. */
    success = true;

end:
    if (app_metadata)
    {
        if (success)
        {
            title_info->app_metadata = app_metadata;
        } else {
            free(app_metadata);
        }
    }

    return success;
}

static bool titleGetMetaKeysFromContentDatabase(NcmContentMetaDatabase *ncm_db, NcmContentMetaKey **out_meta_keys, u32 *out_meta_key_count)
{
    if (!ncm_db || !serviceIsActive(&(ncm_db->s)) || !out_meta_keys || !out_meta_key_count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    Result rc = 0;
    u32 written = 0, total = 0;
    NcmContentMetaKey *meta_keys = NULL, *meta_keys_tmp = NULL;
    size_t meta_keys_size = sizeof(NcmContentMetaKey);
    bool success = false;

    /* Allocate memory for the ncm application content meta keys. */
    meta_keys = calloc(1, meta_keys_size);
    if (!meta_keys)
    {
        LOG_MSG_ERROR("Unable to allocate memory for the ncm application meta keys!");
        goto end;
    }

    /* Get a full list of all titles available in this storage. */
    /* Meta type '0' means all title types will be retrieved. */
    rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, 1, 0, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("ncmContentMetaDatabaseList failed! (0x%X) (first entry).", rc);
        goto end;
    }

    /* Check if our application meta keys buffer was actually filled. */
    /* If it wasn't, odds are there are no titles in this storage. */
    if (!written || !total)
    {
        *out_meta_key_count = 0;
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
            LOG_MSG_ERROR("Unable to reallocate application meta keys buffer! (%u entries).", total);
            goto end;
        }

        meta_keys = meta_keys_tmp;
        meta_keys_tmp = NULL;

        /* Issue call again. */
        rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, (s32)total, 0, 0, 0, UINT64_MAX, NcmContentInstallType_Full);
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("ncmContentMetaDatabaseList failed! (0x%X) (%u %s).", rc, total, total > 1 ? "entries" : "entry");
            goto end;
        }

        /* Safety check. */
        if (written != total)
        {
            LOG_MSG_ERROR("Application meta key count mismatch! (%u != %u).", written, total);
            goto end;
        }
    }

    /* Update output. */
    *out_meta_keys = meta_keys;
    *out_meta_key_count = total;

    success = true;

end:
    if (!success && meta_keys) free(meta_keys);

    return success;
}

static bool titleGetContentInfosByMetaKey(NcmContentMetaDatabase *ncm_db, const NcmContentMetaKey *meta_key, NcmContentInfo **out_content_infos, u32 *out_content_count)
{
    if (!ncm_db || !serviceIsActive(&(ncm_db->s)) || !meta_key || !out_content_infos || !out_content_count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
        LOG_MSG_ERROR("ncmContentMetaDatabaseGet failed! (0x%X).", rc);
        goto end;
    }

    if (content_meta_header_read_size != sizeof(NcmContentMetaHeader))
    {
        LOG_MSG_ERROR("Content meta header size mismatch! (0x%lX != 0x%lX).", content_meta_header_read_size, sizeof(NcmContentMetaHeader));
        goto end;
    }

    /* Get content count. */
    content_count = (u32)content_meta_header.content_count;
    if (!content_count)
    {
        LOG_MSG_ERROR("Content count is zero!");
        goto end;
    }

    /* Allocate memory for the content infos. */
    content_infos = calloc(content_count, sizeof(NcmContentInfo));
    if (!content_infos)
    {
        LOG_MSG_ERROR("Unable to allocate memory for the content infos buffer! (%u content[s]).", content_count);
        goto end;
    }

    /* Retrieve content infos. */
    rc = ncmContentMetaDatabaseListContentInfo(ncm_db, (s32*)&written, content_infos, (s32)content_count, meta_key, 0);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("ncmContentMetaDatabaseListContentInfo failed! (0x%X).", rc);
        goto end;
    }

    if (written != content_count)
    {
        LOG_MSG_ERROR("Content count mismatch! (%u != %u).", written, content_count);
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

static bool titleGetGameCardContentMetaContexts(HashFileSystemContext *hfs_ctx, TitleGameCardContentMetaContext **out_gc_meta_ctxs, u32 *out_gc_meta_ctx_count)
{
    u32 hfs_entry_count = 0;

    if (!hfsIsValidContext(hfs_ctx) || !(hfs_entry_count = hfsGetEntryCount(hfs_ctx)) || !out_gc_meta_ctxs || !out_gc_meta_ctx_count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    TitleGameCardContentMetaContext *gc_meta_ctxs = NULL, *gc_meta_ctxs_tmp = NULL;
    u32 gc_meta_ctx_count = 0;

    bool success = false;

    /* Loop through all Hash FS file entries. */
    for(u32 i = 0; i < hfs_entry_count; i++)
    {
        HashFileSystemEntry *hfs_entry = NULL;
        char *hfs_entry_name = NULL;
        size_t meta_nca_filename_len = 0;

        /* Retrieve Hash FS file entry information. */
        if (!(hfs_entry = hfsGetEntryByIndex(hfs_ctx, i)) || !(hfs_entry_name = hfsGetEntryName(hfs_ctx, hfs_entry)))
        {
            LOG_MSG_ERROR("Failed to retrieve Hash FS file entry information for index %u (%s partition).", i, hfsGetPartitionNameString(hfs_ctx->type));
            goto end;
        }

        /* Skip non-Meta NCAs. */
        if ((meta_nca_filename_len = strlen(hfs_entry_name)) < NCA_HFS_META_NAME_LENGTH || \
            strcasecmp(hfs_entry_name + meta_nca_filename_len - 9, ".cnmt.nca") != 0)
        {
            LOG_MSG_DEBUG("Skipping non-Meta NCA \"%s\" (index %u, %s partition).", hfs_entry_name, i, hfsGetPartitionNameString(hfs_ctx->type));
            continue;
        }

        /* Reallocate contexts buffer. */
        gc_meta_ctxs_tmp = realloc(gc_meta_ctxs, (gc_meta_ctx_count + 1) * sizeof(TitleGameCardContentMetaContext));
        if (!gc_meta_ctxs_tmp)
        {
            LOG_MSG_ERROR("Unable to reallocate gamecard Content Meta contexts buffer! (%u entries, index %u, %s partition).", \
                          gc_meta_ctx_count + 1, i, hfsGetPartitionNameString(hfs_ctx->type));
            goto end;
        }

        gc_meta_ctxs = gc_meta_ctxs_tmp;

        /* Clear current context and increase counter. */
        gc_meta_ctxs_tmp = &(gc_meta_ctxs[gc_meta_ctx_count++]);
        memset(gc_meta_ctxs_tmp, 0, sizeof(TitleGameCardContentMetaContext));

        /* Get pointers for NCA and Content Meta contexts. */
        NcaContext *nca_ctx = &(gc_meta_ctxs_tmp->nca_ctx);
        ContentMetaContext *cnmt_ctx = &(gc_meta_ctxs_tmp->cnmt_ctx);
        NcmContentMetaKey *meta_key = &(gc_meta_ctxs_tmp->meta_key);

        /* Initialize NCA context. */
        if (!ncaInitializeContextByHashFileSystemEntry(nca_ctx, hfs_ctx, hfs_entry, NULL))
        {
            LOG_MSG_ERROR("Failed to initialize NCA context for \"%s\" (index %u, %s partition).", hfs_entry_name, i, hfsGetPartitionNameString(hfs_ctx->type));
            goto end;
        }

        /* Initialize Content Meta context. */
        if (!cnmtInitializeContext(cnmt_ctx, nca_ctx))
        {
            LOG_MSG_ERROR("Failed to initialize Content Meta context for \"%s\" (index %u, %s partition).", hfs_entry_name, i, hfsGetPartitionNameString(hfs_ctx->type));
            goto end;
        }

        /* Manually fill content meta key using CNMT info. */
        meta_key->id = cnmt_ctx->packaged_header->title_id;
        meta_key->version = cnmt_ctx->packaged_header->version.value;
        meta_key->type = cnmt_ctx->packaged_header->content_meta_type;
        meta_key->install_type = cnmt_ctx->packaged_header->content_install_type;
    }

    if (gc_meta_ctx_count)
    {
        /* Sort gamecard Content Meta contexts in descendent order. */
        /* This is done to make sure control data from patches is processed first. */
        if (gc_meta_ctx_count > 1) qsort(gc_meta_ctxs, gc_meta_ctx_count, sizeof(TitleGameCardContentMetaContext), &titleGameCardContentMetaContextSortFunction);

        /* Update output. */
        *out_gc_meta_ctxs = gc_meta_ctxs;
        *out_gc_meta_ctx_count = gc_meta_ctx_count;
    } else {
        LOG_MSG_INFO("No Meta NCAs available in gamecard %s partition.", hfsGetPartitionNameString(hfs_ctx->type));
        *out_gc_meta_ctx_count = 0;
    }

    /* Update flag. */
    success = true;

end:
    if (!success && gc_meta_ctxs) titleFreeGameCardContentMetaContexts(&gc_meta_ctxs, gc_meta_ctx_count);

    return success;
}

static void titleFreeGameCardContentMetaContexts(TitleGameCardContentMetaContext **gc_meta_ctxs, u32 gc_meta_ctx_count)
{
    TitleGameCardContentMetaContext *ptr = NULL;
    if (!gc_meta_ctxs || !(ptr = *gc_meta_ctxs)) return;

    for(u32 i = 0; i < gc_meta_ctx_count; i++) cnmtFreeContext(&(ptr[i].cnmt_ctx));

    free(ptr);
    *gc_meta_ctxs = NULL;
}

static bool titleGetContentInfosByGameCardContentMetaContext(TitleGameCardContentMetaContext *gc_meta_ctx, HashFileSystemContext *hfs_ctx, NcmContentInfo **out_content_infos, u32 *out_content_count)
{
    if (!gc_meta_ctx || !cnmtIsValidContext(&(gc_meta_ctx->cnmt_ctx)) || !hfsIsValidContext(hfs_ctx) || !gc_meta_ctx->cnmt_ctx.packaged_header->content_count || !out_content_infos || !out_content_count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    NcmContentInfo *content_infos = NULL, *content_infos_tmp = NULL;
    u32 content_count = (gc_meta_ctx->cnmt_ctx.packaged_header->content_count + 1), available_count = 0;

    /* Allocate memory for the content infos. */
    content_infos = calloc(content_count, sizeof(NcmContentInfo));
    if (!content_infos)
    {
        LOG_MSG_ERROR("Unable to allocate memory for the content infos buffer! (%u content[s]).", content_count);
        return false;
    }

    /* Loop through our NcmPackagedContentInfo entries. */
    for(u32 i = 0; i < content_count; i++)
    {
        if (i == 0)
        {
            /* Reserve the very first content info entry for our Meta NCA. */
            NcmContentInfo *cur_content_info = &(content_infos[0]);

            memcpy(&(cur_content_info->content_id), &(gc_meta_ctx->nca_ctx.content_id), sizeof(NcmContentId));
            ncmU64ToContentInfoSize(gc_meta_ctx->nca_ctx.content_size, cur_content_info);
            cur_content_info->attr = 0;
            cur_content_info->content_type = NcmContentType_Meta;
            cur_content_info->id_offset = 0;

            //LOG_DATA_DEBUG(cur_content_info, sizeof(NcmContentInfo), "Forged Meta content record:");
        } else {
            char nca_filename[0x30] = {0};
            NcmContentInfo *cur_content_info = &(gc_meta_ctx->cnmt_ctx.packaged_content_info[i - 1].info);

            /* Make sure this content exists on the inserted gamecard. */
            utilsGenerateHexString(nca_filename, sizeof(nca_filename), cur_content_info->content_id.c, sizeof(cur_content_info->content_id.c), false);
            strcat(nca_filename, cur_content_info->content_type == NcmContentType_Meta ? ".cnmt.nca" : ".nca");

            if (!hfsGetEntryByName(hfs_ctx, nca_filename))
            {
                LOG_MSG_DEBUG("Unable to locate %s NCA \"%s\" in Hash FS by name (%s partition).", titleGetNcmContentTypeName(cur_content_info->content_type), nca_filename, \
                                                                                                   hfsGetPartitionNameString(hfs_ctx->type));
                continue;
            }

            /* Copy content info data. */
            memcpy(&(content_infos[available_count]), cur_content_info, sizeof(NcmContentInfo));
        }

        /* Update available content count. */
        available_count++;
    }

    if (available_count < content_count)
    {
        /* Reallocate output buffer, if needed. */
        content_infos_tmp = realloc(content_infos, available_count * sizeof(NcmContentInfo));
        if (content_infos_tmp) content_infos = content_infos_tmp;
        content_infos_tmp = NULL;
    }

    /* Update output. */
    *out_content_infos = content_infos;
    *out_content_count = available_count;

    return true;
}

static void titleUpdateTitleInfoLinkedLists(void)
{
    /* Free orphan title info entries. */
    titleFreeOrphanTitleInfoEntries();

    /* Loop through all available title storages. */
    for(u8 i = NcmStorageId_GameCard; i <= NcmStorageId_SdCard; i++)
    {
        /* Don't process system titles. */
        if (i == NcmStorageId_BuiltInSystem) continue;

        TitleStorage *title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(i)]);
        TitleInfo **titles = title_storage->titles;
        u32 title_count = title_storage->title_count;

        /* Don't proceed if the current storage holds no titles. */
        if (!titles || !title_count) continue;

        /* Process titles from the current storage. */
        for(u32 j = 0; j < title_count; j++)
        {
            /* Get pointer to the current title info and reset its linked list pointers. */
            TitleInfo *child_info = titles[j];
            if (!child_info) continue;

            child_info->previous = child_info->next = NULL;

            /* If we're dealing with a title that's not an user application, patch, add-on content or add-on content patch, flag it as orphan and proceed onto the next one. */
            if (child_info->meta_key.type < NcmContentMetaType_Application || (child_info->meta_key.type > NcmContentMetaType_AddOnContent && \
                child_info->meta_key.type != NcmContentMetaType_DataPatch))
            {
                titleAddOrphanTitleInfoEntry(child_info);
                continue;
            }

            if (child_info->meta_key.type != NcmContentMetaType_Application && !child_info->app_metadata)
            {
                /* We're dealing with a patch, an add-on content or an add-on content patch. */
                /* We'll just retrieve a pointer to the first matching user application entry and use it to set a pointer to an application metadata entry. */
                u64 app_id = titleGetApplicationIdByContentMetaKey(&(child_info->meta_key));
                TitleInfo *parent = _titleGetTitleInfoEntryFromStorageByTitleId(NcmStorageId_Any, app_id);
                if (parent)
                {
                    /* Set pointer to application metadata. */
                    child_info->app_metadata = parent->app_metadata;
                } else {
                    /* Add orphan title info entry since we have no application metadata. */
                    titleAddOrphanTitleInfoEntry(child_info);
                    continue;
                }
            }

            /* Locate previous user application, patch, add-on content or add-on content patch entry. */
            /* If it's found, we will update both its next pointer and the previous pointer from the current entry. */
            for(u8 k = i; k >= NcmStorageId_GameCard; k--)
            {
                /* Don't process system titles. And don't proceed if we're currently dealing with the first entry from a storage. */
                if (k == NcmStorageId_BuiltInSystem || (k == i && j == 0)) continue;

                TitleStorage *prev_title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(k)]);
                TitleInfo **prev_titles = prev_title_storage->titles;
                u32 prev_title_count = prev_title_storage->title_count;
                u32 start_idx = (k == i ? j : prev_title_count);

                /* Don't proceed if the current storage holds no titles. */
                if (!prev_titles || !prev_title_count) continue;

                for(u32 l = start_idx; l > 0; l--)
                {
                    TitleInfo *prev_info = prev_titles[l - 1];
                    if (!prev_info) continue;

                    if (prev_info->meta_key.type == child_info->meta_key.type && \
                        (((child_info->meta_key.type == NcmContentMetaType_Application || child_info->meta_key.type == NcmContentMetaType_Patch) && prev_info->meta_key.id == child_info->meta_key.id) || \
                        (child_info->meta_key.type == NcmContentMetaType_AddOnContent && titleCheckIfAddOnContentIdsAreSiblings(prev_info->meta_key.id, child_info->meta_key.id)) || \
                        (child_info->meta_key.type == NcmContentMetaType_DataPatch && titleCheckIfDataPatchIdsAreSiblings(prev_info->meta_key.id, child_info->meta_key.id))))
                    {
                        prev_info->next = child_info;
                        child_info->previous = prev_info;
                        break;
                    }
                }

                if (child_info->previous) break;
            }
        }
    }
}

static TitleInfo *_titleGetTitleInfoEntryFromStorageByTitleId(u8 storage_id, u64 title_id)
{
    if (storage_id < NcmStorageId_GameCard || storage_id > NcmStorageId_Any || !title_id)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    TitleInfo *out = NULL;

    u8 start_idx = (storage_id == NcmStorageId_Any ? TITLE_STORAGE_INDEX(NcmStorageId_GameCard) : TITLE_STORAGE_INDEX(storage_id));
    u8 max_val = (storage_id == NcmStorageId_Any ? TITLE_STORAGE_INDEX(NcmStorageId_SdCard) : start_idx);

    for(u8 i = start_idx; i <= max_val; i++)
    {
        TitleStorage *title_storage = &(g_titleStorage[i]);
        if (!title_storage->titles || !*(title_storage->titles) || !title_storage->title_count) continue;

        for(u32 j = 0; j < title_storage->title_count; j++)
        {
            TitleInfo *title_info = title_storage->titles[j];
            if (title_info && title_info->meta_key.id == title_id)
            {
                out = title_info;
                break;
            }
        }

        if (out) break;
    }

    if (!out && storage_id != NcmStorageId_BuiltInSystem) LOG_MSG_DEBUG("Unable to find title info entry with ID \"%016lX\" in %s.", title_id, titleGetNcmStorageIdName(storage_id));

    return out;
}

static TitleInfo *titleDuplicateTitleInfoFull(TitleInfo *title_info, TitleInfo *previous, TitleInfo *next)
{
    if (!titleIsValidInfoBlock(title_info))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    TitleInfo *title_info_dup = NULL, *tmp1 = NULL, *tmp2 = NULL;
    bool dup_previous = false, dup_next = false, success = false;

    /* Duplicate TitleInfo object. */
    title_info_dup = titleDuplicateTitleInfo(title_info);
    if (!title_info_dup)
    {
        LOG_MSG_ERROR("Failed to duplicate TitleInfo object!");
        return NULL;
    }

#define TITLE_DUPLICATE_LINKED_LIST(elem, prv, nxt) \
    if (title_info->elem) { \
        if (elem) { \
            title_info_dup->elem = elem; \
        } else { \
            title_info_dup->elem = titleDuplicateTitleInfoFull(title_info->elem, prv, nxt); \
            if (!title_info_dup->elem) goto end; \
            dup_##elem = true; \
        } \
    }

#define TITLE_FREE_DUPLICATED_LINKED_LIST(elem) \
    if (dup_##elem) { \
        tmp1 = title_info_dup->elem; \
        while(tmp1) { \
            tmp2 = tmp1->elem; \
            tmp1->previous = tmp1->next = NULL; \
            titleFreeTitleInfo(&tmp1); \
            tmp1 = tmp2; \
        } \
    }

    /* Duplicate linked lists based on two different principles: */
    /* 1) Linked list pointers will only be populated if their corresponding pointer is also populated in the TitleInfo element to duplicate. */
    /* 2) Pointers passed into this function take precedence before actual data duplication. */
    TITLE_DUPLICATE_LINKED_LIST(previous, NULL, title_info_dup);
    TITLE_DUPLICATE_LINKED_LIST(next, title_info_dup, NULL);

    /* Update flag. */
    success = true;

end:
    /* We can't directly use titleFreeTitleInfo() on title_info_dup because some or all of the linked list data may have been provided as function arguments. */
    /* So we'll take care of freeing data the old fashioned way. */
    if (!success && title_info_dup)
    {
        /* Free content infos pointer. */
        if (title_info_dup->content_infos) free(title_info_dup->content_infos);

        /* Free previous and next linked lists (if duplicated). */
        /* We need to take care of not freeing the linked lists right away, either because we may have already freed them, or because they may have been passed as arguments. */
        /* Furthermore, both the next pointer from the previous sibling and the previous pointer from the next sibling reference our current duplicated entry. */
        /* To avoid issues, we'll just clear all linked list pointers. */
        TITLE_FREE_DUPLICATED_LINKED_LIST(previous);
        TITLE_FREE_DUPLICATED_LINKED_LIST(next);

        /* Free allocated buffer and update return pointer. */
        free(title_info_dup);
        title_info_dup = NULL;
    }

#undef TITLE_DUPLICATE_LINKED_LIST

#undef TITLE_FREE_DUPLICATED_LINKED_LIST

    return title_info_dup;
}

static TitleInfo *titleDuplicateTitleInfo(TitleInfo *title_info)
{
    if (!titleIsValidInfoBlock(title_info))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    TitleInfo *title_info_dup = NULL;
    NcmContentInfo *content_infos_dup = NULL;
    bool success = false;

    /* Allocate memory for the new TitleInfo element. */
    title_info_dup = calloc(1, sizeof(TitleInfo));
    if (!title_info_dup)
    {
        LOG_MSG_ERROR("Failed to allocate memory for TitleInfo duplicate!");
        return NULL;
    }

    /* Copy TitleInfo data. */
    memcpy(title_info_dup, title_info, sizeof(TitleInfo));
    title_info_dup->previous = title_info_dup->next = NULL;

    /* Allocate memory for NcmContentInfo elements. */
    content_infos_dup = calloc(title_info->content_count, sizeof(NcmContentInfo));
    if (!content_infos_dup)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NcmContentInfo duplicates!");
        goto end;
    }

    /* Copy NcmContentInfo data. */
    memcpy(content_infos_dup, title_info->content_infos, title_info->content_count * sizeof(NcmContentInfo));

    /* Update content infos pointer. */
    title_info_dup->content_infos = content_infos_dup;

    /* Update flag. */
    success = true;

end:
    if (!success)
    {
        if (content_infos_dup) free(content_infos_dup);

        if (title_info_dup)
        {
            free(title_info_dup);
            title_info_dup = NULL;
        }
    }

    return title_info_dup;
}

static char *titleGetDisplayVersionString(TitleInfo *title_info)
{
    NcmContentInfo *nacp_content = NULL;

    if (!title_info || (title_info->meta_key.type != NcmContentMetaType_Application && title_info->meta_key.type != NcmContentMetaType_Patch) || \
        !(nacp_content = titleGetContentInfoByTypeAndIdOffset(title_info, NcmContentType_Control, 0)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u64 title_id = title_info->meta_key.id;
    u8 storage_id = title_info->storage_id, hfs_partition_type = (storage_id == NcmStorageId_GameCard ? HashFileSystemPartitionType_Secure : 0);
    NcaContext *nca_ctx = NULL;
    NacpContext nacp_ctx = {0};
    char display_version[0x11] = {0}, *str = NULL;

    LOG_MSG_DEBUG("Retrieving display version string for %s \"%s\" (%016lX) in %s...", titleGetNcmContentMetaTypeName(title_info->meta_key.type), \
                                                                                       title_info->app_metadata->lang_entry.name, title_id, \
                                                                                       titleGetNcmStorageIdName(title_info->storage_id));

    /* Allocate memory for the NCA context. */
    nca_ctx = calloc(1, sizeof(NcaContext));
    if (!nca_ctx)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NCA context!");
        goto end;
    }

    /* Initialize NCA context. */
    if (!ncaInitializeContext(nca_ctx, storage_id, hfs_partition_type, &(title_info->meta_key), nacp_content, NULL))
    {
        LOG_MSG_ERROR("Failed to initialize NCA context for Control NCA from %016lX!", title_id);
        goto end;
    }

    /* Initialize NACP context. */
    if (!nacpInitializeContext(&nacp_ctx, nca_ctx))
    {
        LOG_MSG_ERROR("Failed to initialize NACP context for %016lX!", title_id);
        goto end;
    }

    /* Get trimmed version string. */
    snprintf(display_version, sizeof(display_version), "%s", nacp_ctx.data->display_version);
    utilsTrimString(display_version);

    /* Check version string length. */
    if (!*display_version)
    {
        LOG_MSG_ERROR("Display version string from %016lX is empty!", title_id);
        goto end;
    }

    /* Duplicate version string. */
    str = strdup(display_version);
    if (!str) LOG_MSG_ERROR("Failed to duplicate version string from %016lX!", title_id);

end:
    nacpFreeContext(&nacp_ctx);

    if (nca_ctx) free(nca_ctx);

    return str;
}

static bool titleCreateGameCardInfoThread(void)
{
    if (!utilsCreateThread(&g_titleGameCardInfoThread, titleGameCardInfoThreadFunc, NULL, 1))
    {
        LOG_MSG_ERROR("Failed to create gamecard title info thread!");
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
    NX_IGNORE_ARG(arg);

    Result rc = 0;
    int idx = 0;

    Waiter gamecard_status_event_waiter = waiterForUEvent(g_titleGameCardStatusChangeUserEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_titleGameCardInfoThreadExitEvent);

    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, gamecard_status_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;

        /* Exit event triggered. */
        if (idx == 1) break;

        /* Update gamecard title info. */
        SCOPED_LOCK(&g_titleMutex)
        {
            g_titleGameCardInfoUpdated = titleRefreshGameCardTitleInfo();

            /* Generate gamecard application metadata array. */
            titleGenerateGameCardApplicationMetadataArray();

            /* Generate gamecard file names. */
            titleGenerateGameCardFileNames();

            /* Generate filtered user application metadata pointer array. */
            if (g_titleGameCardInfoUpdated) titleGenerateFilteredApplicationMetadataPointerArray(false);
        }
    }

    /* Update gamecard flags. */
    g_titleGameCardAvailable = g_titleGameCardInfoUpdated = false;

    /* Free gamecard file names. */
    titleFreeGameCardFileNames();

    threadExit();
}

static bool titleRefreshGameCardTitleInfo(void)
{
    TitleStorage *title_storage = NULL;
    TitleInfo **titles = NULL;
    u32 title_count = 0, gamecard_app_count = 0, extra_app_count = 0;
    bool status = false, success = false, cleanup = true, free_entries = false, hfs_init = false;

    /* Retrieve current gamecard status. */
    status = (gamecardGetStatus() == GameCardStatus_InsertedAndInfoLoaded);
    if (status == g_titleGameCardAvailable || !status)
    {
        success = cleanup = (status != g_titleGameCardAvailable);
        goto end;
    }

    /* Initialize gamecard title storage. */
    if (!titleInitializeTitleStorage(NcmStorageId_GameCard))
    {
        /* Try to initialize the gamecard title storage manually. */
        if (!(hfs_init = titleInitializeGameCardTitleStorageByHashFileSystem(HashFileSystemPartitionType_Secure)))
        {
            LOG_MSG_ERROR("Failed to initialize gamecard title storage!");
            goto end;
        }
    }

    /* Get gamecard title storage info. */
    title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(NcmStorageId_GameCard)]);
    titles = title_storage->titles;
    title_count = title_storage->title_count;

    /* Verify title count. */
    if (!title_count)
    {
        LOG_MSG_ERROR("Gamecard title count is zero!");
        goto end;
    }

    /* Get gamecard user application count. */
    for(u32 i = 0; i < title_count; i++)
    {
        TitleInfo *cur_title_info = titles[i];
        if (cur_title_info && cur_title_info->meta_key.type == NcmContentMetaType_Application) gamecard_app_count++;
    }

    /* Return immediately if there are no user applications or if we initialized the gamecard storage using a Hash FS. */
    if (!gamecard_app_count || hfs_init)
    {
        success = true;
        cleanup = false;
        goto end;
    }

    /* Reallocate application metadata pointer array. */
    if (!titleReallocateApplicationMetadata(gamecard_app_count, false, false))
    {
        LOG_MSG_ERROR("Failed to reallocate application metadata pointer array for gamecard user applications!");
        goto end;
    }

    free_entries = true;

    /* Retrieve application metadata via ns for any new gamecard user applications. */
    for(u32 i = 0; i < title_count; i++)
    {
        TitleInfo *cur_title_info = titles[i];
        if (!cur_title_info) continue;

        /* Do not proceed if application metadata has already been retrieved, or if we can successfully retrieve it. */
        u64 app_id = titleGetApplicationIdByContentMetaKey(&(cur_title_info->meta_key));
        if (cur_title_info->app_metadata != NULL || (cur_title_info->app_metadata = titleFindApplicationMetadataByTitleId(app_id, false, extra_app_count)) != NULL) continue;

        /* Retrieve application metadata. */
        TitleApplicationMetadata *cur_app_metadata = titleGenerateUserMetadataEntryFromNs(app_id);
        if (!cur_app_metadata) continue;

        /* Set application metadata entry pointer. */
        g_userMetadata[g_userMetadataCount + extra_app_count] = cur_app_metadata;

        /* Increase extra application metadata counter. */
        extra_app_count++;
    }

    if (extra_app_count)
    {
        /* Update application metadata count. */
        g_userMetadataCount += extra_app_count;

        /* Sort application metadata entries by name. */
        if (g_userMetadataCount > 1) qsort(g_userMetadata, g_userMetadataCount, sizeof(TitleApplicationMetadata*), &titleUserMetadataSortFunction);

        /* Update linked lists for user applications, patches and add-on contents. */
        /* This will take care of orphan titles we might now have application metadata for. */
        titleUpdateTitleInfoLinkedLists();
    }

    /* Free extra allocated pointers if we didn't use them. */
    if (extra_app_count < gamecard_app_count) titleReallocateApplicationMetadata(0, false, false);

    /* Update flags. */
    success = true;
    cleanup = false;

end:
    /* Update gamecard status. */
    g_titleGameCardAvailable = status;

    /* Free previously allocated application metadata pointers. Ignore return value. */
    if (!success && free_entries) titleReallocateApplicationMetadata(extra_app_count, false, true);

    if (cleanup)
    {
        /* Close gamecard title storage. */
        titleCloseTitleStorage(NcmStorageId_GameCard);

        /* Update linked lists for user applications, patches and add-on contents. */
        titleUpdateTitleInfoLinkedLists();
    }

    return success;
}

static void titleGenerateGameCardApplicationMetadataArray(void)
{
    TitleStorage *title_storage = &(g_titleStorage[TITLE_STORAGE_INDEX(NcmStorageId_GameCard)]);
    TitleInfo **titles = title_storage->titles;
    u32 title_count = title_storage->title_count;
    TitleGameCardApplicationMetadata *tmp_gc_app_metadata = NULL;

    /* Free gamecard application metadata array. */
    if (g_titleGameCardApplicationMetadata)
    {
        free(g_titleGameCardApplicationMetadata);
        g_titleGameCardApplicationMetadata = NULL;
    }

    g_titleGameCardApplicationMetadataCount = 0;

    /* Make sure we actually have gamecard TitleInfo entries we can work with. */
    if (!titles || !title_count)
    {
        LOG_MSG_ERROR("No gamecard TitleInfo entries available!");
        return;
    }

    /* Loop through our gamecard TitleInfo entries. */
    LOG_MSG_DEBUG("Retrieving gamecard application metadata (%u title[s])...", title_count);

    for(u32 i = 0; i < title_count; i++)
    {
        /* Skip current entry if it's not a user application. */
        TitleInfo *app_info = titles[i], *patch_info = NULL;
        if (!app_info || app_info->meta_key.type != NcmContentMetaType_Application) continue;

        u32 app_version = app_info->meta_key.version;
        u32 dlc_count = 0;

        /* Check if the inserted gamecard holds any bundled patches for the current user application. */
        /* If so, we'll use the highest patch version available as part of the filename. */
        for(u32 j = 0; j < title_count; j++)
        {
            if (j == i) continue;

            TitleInfo *cur_title_info = titles[j];
            if (!cur_title_info || cur_title_info->meta_key.type != NcmContentMetaType_Patch || \
                !titleCheckIfPatchIdBelongsToApplicationId(app_info->meta_key.id, cur_title_info->meta_key.id) || cur_title_info->meta_key.version < app_version) continue;

            patch_info = cur_title_info;
            app_version = cur_title_info->meta_key.version;
        }

        /* Count DLCs available for this application in the inserted gamecard. */
        for(u32 j = 0; j < title_count; j++)
        {
            if (j == i) continue;

            TitleInfo *cur_title_info = titles[j];
            if (!cur_title_info || cur_title_info->meta_key.type != NcmContentMetaType_AddOnContent || \
                !titleCheckIfAddOnContentIdBelongsToApplicationId(app_info->meta_key.id, cur_title_info->meta_key.id)) continue;

            dlc_count++;
        }

        /* Reallocate application metadata pointer array. */
        tmp_gc_app_metadata = realloc(g_titleGameCardApplicationMetadata, (g_titleGameCardApplicationMetadataCount + 1) * sizeof(TitleGameCardApplicationMetadata));
        if (!tmp_gc_app_metadata)
        {
            LOG_MSG_ERROR("Failed to reallocate gamecard application metadata array!");

            if (g_titleGameCardApplicationMetadata) free(g_titleGameCardApplicationMetadata);
            g_titleGameCardApplicationMetadata = NULL;
            g_titleGameCardApplicationMetadataCount = 0;

            return;
        }

        g_titleGameCardApplicationMetadata = tmp_gc_app_metadata;

        /* Fill current entry and increase counter. */
        tmp_gc_app_metadata = &(g_titleGameCardApplicationMetadata[g_titleGameCardApplicationMetadataCount++]);
        memset(tmp_gc_app_metadata, 0, sizeof(TitleGameCardApplicationMetadata));
        tmp_gc_app_metadata->app_metadata = app_info->app_metadata;
        tmp_gc_app_metadata->has_patch = (patch_info != NULL);
        tmp_gc_app_metadata->version.value = app_version;
        tmp_gc_app_metadata->dlc_count = dlc_count;

        /* Try to retrieve the display version. */
        char *version_str = titleGetDisplayVersionString(patch_info ? patch_info : app_info);
        if (version_str)
        {
            snprintf(tmp_gc_app_metadata->display_version, MAX_ELEMENTS(tmp_gc_app_metadata->display_version), "%s", version_str);
            free(version_str);
        }
    }

    if (g_titleGameCardApplicationMetadata && g_titleGameCardApplicationMetadataCount)
    {
        /* Sort title metadata entries by name. */
        if (g_titleGameCardApplicationMetadataCount > 1) qsort(g_titleGameCardApplicationMetadata, g_titleGameCardApplicationMetadataCount, sizeof(TitleGameCardApplicationMetadata),
                                                               &titleGameCardApplicationMetadataSortFunction);
    } else {
        LOG_MSG_ERROR("No gamecard content data found for user applications!");
    }
}

NX_INLINE void titleGenerateGameCardFileNames(void)
{
    titleFreeGameCardFileNames();

    if (g_titleGameCardAvailable)
    {
        for(u8 i = 0; i < TitleNamingConvention_Count; i++) g_titleGameCardFileNames[i] = _titleGenerateGameCardFileName(i);
    }
}

NX_INLINE void titleFreeGameCardFileNames(void)
{
    for(u8 i = 0; i < TitleNamingConvention_Count; i++)
    {
        if (g_titleGameCardFileNames[i])
        {
            free(g_titleGameCardFileNames[i]);
            g_titleGameCardFileNames[i] = NULL;
        }
    }
}

static char *_titleGenerateGameCardFileName(u8 naming_convention)
{
    if (naming_convention >= TitleNamingConvention_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    char *filename = NULL;
    GameCardHeader gc_header = {0};
    char app_name[0x300] = {0};
    size_t cur_filename_len = 0, app_name_len = 0;
    bool error = false;

    LOG_MSG_DEBUG("Generating %s gamecard filename...", naming_convention == TitleNamingConvention_Full ? "full" : "ID and version");

    /* Check if we don't have any gamecard application metadata records we can work with. */
    /* This is especially true for Kiosk / Quest gamecards. */
    if (!g_titleGameCardApplicationMetadata || !g_titleGameCardApplicationMetadataCount) goto fallback;

    /* Loop through our gamecard application metadata entries. */
    for(u32 i = 0; i < g_titleGameCardApplicationMetadataCount; i++)
    {
        const TitleGameCardApplicationMetadata *cur_gc_app_metadata = &(g_titleGameCardApplicationMetadata[i]);

        /* Generate current user application name. */
        *app_name = '\0';

        if (naming_convention == TitleNamingConvention_Full)
        {
            if (cur_filename_len) strcat(app_name, " + ");

            if (cur_gc_app_metadata->app_metadata && cur_gc_app_metadata->app_metadata->lang_entry.name[0])
            {
                app_name_len = strlen(app_name);
                snprintf(app_name + app_name_len, MAX_ELEMENTS(app_name) - app_name_len, "%s ", cur_gc_app_metadata->app_metadata->lang_entry.name);

                /* Append display version string if the inserted gamecard holds a patch for the current user application. */
                if (cur_gc_app_metadata->has_patch && cur_gc_app_metadata->display_version[0])
                {
                    app_name_len = strlen(app_name);
                    snprintf(app_name + app_name_len, MAX_ELEMENTS(app_name) - app_name_len, "%s ", cur_gc_app_metadata->display_version);
                }
            }

            app_name_len = strlen(app_name);
            snprintf(app_name + app_name_len, MAX_ELEMENTS(app_name) - app_name_len, "[%016lX][v%u]", cur_gc_app_metadata->app_metadata->title_id, cur_gc_app_metadata->version.value);
        } else
        if (naming_convention == TitleNamingConvention_IdAndVersionOnly)
        {
            if (cur_filename_len) strcat(app_name, "+");
            app_name_len = strlen(app_name);
            snprintf(app_name + app_name_len, MAX_ELEMENTS(app_name) - app_name_len, "%016lX_v%u", cur_gc_app_metadata->app_metadata->title_id, cur_gc_app_metadata->version.value);
        }

        /* Reallocate output buffer. */
        app_name_len = strlen(app_name);

        char *tmp_filename = realloc(filename, (cur_filename_len + app_name_len + 1) * sizeof(char));
        if (!tmp_filename)
        {
            LOG_MSG_ERROR("Failed to reallocate filename buffer!");
            if (filename) free(filename);
            filename = NULL;
            error = true;
            break;
        }

        filename = tmp_filename;
        tmp_filename = NULL;

        /* Concatenate current user application name. */
        filename[cur_filename_len] = '\0';
        strcat(filename, app_name);
        cur_filename_len += app_name_len;
    }

fallback:
    if (!filename && !error)
    {
        LOG_MSG_ERROR("Error: the inserted gamecard doesn't hold any user applications!");

        /* Fallback string if no applications can be found. */
        sprintf(app_name, "gamecard");

        if (gamecardGetHeader(&gc_header))
        {
            strcat(app_name, "_");
            cur_filename_len = strlen(app_name);
            utilsGenerateHexString(app_name + cur_filename_len, sizeof(app_name) - cur_filename_len, gc_header.package_id, sizeof(gc_header.package_id), false);
        }

        filename = strdup(app_name);
        if (!filename) LOG_MSG_ERROR("Failed to duplicate fallback filename!");
    }

    return filename;
}

static int titleSystemMetadataSortFunction(const void *a, const void *b)
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

static int titleUserMetadataSortFunction(const void *a, const void *b)
{
    const TitleApplicationMetadata *app_metadata_1 = *((const TitleApplicationMetadata**)a);
    const TitleApplicationMetadata *app_metadata_2 = *((const TitleApplicationMetadata**)b);

    return strcasecmp(app_metadata_1->lang_entry.name, app_metadata_2->lang_entry.name);
}

static int titleInfoSortFunction(const void *a, const void *b)
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

    if (title_info_1->version.value < title_info_2->version.value)
    {
        return -1;
    } else
    if (title_info_1->version.value > title_info_2->version.value)
    {
        return 1;
    }

    if (title_info_1->storage_id < title_info_2->storage_id)
    {
        return -1;
    } else
    if (title_info_1->storage_id > title_info_2->storage_id)
    {
        return 1;
    }

    return 0;
}

static int titleGameCardApplicationMetadataSortFunction(const void *a, const void *b)
{
    const TitleGameCardApplicationMetadata *gc_app_metadata_1 = (const TitleGameCardApplicationMetadata*)a;
    const TitleGameCardApplicationMetadata *gc_app_metadata_2 = (const TitleGameCardApplicationMetadata*)b;

    return strcasecmp(gc_app_metadata_1->app_metadata->lang_entry.name, gc_app_metadata_2->app_metadata->lang_entry.name);
}

static int titleGameCardContentMetaContextSortFunction(const void *a, const void *b)
{
    const TitleGameCardContentMetaContext *gc_meta_ctx_1 = (const TitleGameCardContentMetaContext*)a;
    const TitleGameCardContentMetaContext *gc_meta_ctx_2 = (const TitleGameCardContentMetaContext*)b;

    if (gc_meta_ctx_1->meta_key.type < gc_meta_ctx_2->meta_key.type)
    {
        return 1;
    } else
    if (gc_meta_ctx_1->meta_key.type > gc_meta_ctx_2->meta_key.type)
    {
        return -1;
    }

    if (gc_meta_ctx_1->meta_key.id < gc_meta_ctx_2->meta_key.id)
    {
        return 1;
    } else
    if (gc_meta_ctx_1->meta_key.id > gc_meta_ctx_2->meta_key.id)
    {
        return -1;
    }

    if (gc_meta_ctx_1->meta_key.version < gc_meta_ctx_2->meta_key.version)
    {
        return 1;
    } else
    if (gc_meta_ctx_1->meta_key.version > gc_meta_ctx_2->meta_key.version)
    {
        return -1;
    }

    return 0;
}
