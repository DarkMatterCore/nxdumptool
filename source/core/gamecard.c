/*
 * gamecard.c
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include "mem.h"
#include "gamecard.h"
#include "keys.h"

#define GAMECARD_HFS0_MAGIC                     0x48465330              /* "HFS0". */

#define GAMECARD_READ_BUFFER_SIZE               0x800000                /* 8 MiB. */

#define GAMECARD_ACCESS_WAIT_TIME               3                       /* Seconds. */

#define GAMECARD_UNUSED_AREA_BLOCK_SIZE         0x24
#define GAMECARD_UNUSED_AREA_SIZE(x)            (((x) / GAMECARD_PAGE_SIZE) * GAMECARD_UNUSED_AREA_BLOCK_SIZE)

#define GAMECARD_STORAGE_AREA_NAME(x)           ((x) == GameCardStorageArea_Normal ? "normal" : ((x) == GameCardStorageArea_Secure ? "secure" : "none"))

#define GAMECARD_HFS_PARTITION_NAME_INDEX(x)    ((x) - 1)

#define LAFW_MAGIC                              0x4C414657              /* "LAFW". */

/* Type definitions. */

typedef enum {
    GameCardStorageArea_None   = 0,
    GameCardStorageArea_Normal = 1,
    GameCardStorageArea_Secure = 2
} GameCardStorageArea;

typedef enum {
    GameCardCapacity_1GiB  = BIT_LONG(30),
    GameCardCapacity_2GiB  = BIT_LONG(31),
    GameCardCapacity_4GiB  = BIT_LONG(32),
    GameCardCapacity_8GiB  = BIT_LONG(33),
    GameCardCapacity_16GiB = BIT_LONG(34),
    GameCardCapacity_32GiB = BIT_LONG(35)
} GameCardCapacity;

/* Global variables. */

static Mutex g_gameCardMutex = 0;
static bool g_gameCardInterfaceInit = false;

static FsDeviceOperator g_deviceOperator = {0};
static FsEventNotifier g_gameCardEventNotifier = {0};
static Event g_gameCardKernelEvent = {0};
static bool g_openDeviceOperator = false, g_openEventNotifier = false, g_loadKernelEvent = false;

static LotusAsicFirmwareBlob *g_lafwBlob = NULL;
static u64 g_lafwVersion = 0;

static Thread g_gameCardDetectionThread = {0};
static UEvent g_gameCardDetectionThreadExitEvent = {0}, g_gameCardStatusChangeEvent = {0};
static bool g_gameCardDetectionThreadCreated = false;

static GameCardStatus g_gameCardStatus = GameCardStatus_NotInserted;

static FsGameCardHandle g_gameCardHandle = {0};
static FsStorage g_gameCardStorage = {0};
static u8 g_gameCardCurrentStorageArea = GameCardStorageArea_None;
static u8 *g_gameCardReadBuf = NULL;

static GameCardHeader g_gameCardHeader = {0};
static GameCardInfo g_gameCardInfoArea = {0};
static u64 g_gameCardNormalAreaSize = 0, g_gameCardSecureAreaSize = 0, g_gameCardTotalSize = 0;
static u64 g_gameCardCapacity = 0;

static u32 g_gameCardHfsCount = 0;
static HashFileSystemContext **g_gameCardHfsCtx = NULL;

static MemoryLocation g_fsProgramMemory = {
    .program_id = FS_SYSMODULE_TID,
    .mask = 0,
    .data = NULL,
    .data_size = 0
};

static const char *g_gameCardHfsPartitionNames[] = {
    [GAMECARD_HFS_PARTITION_NAME_INDEX(GameCardHashFileSystemPartitionType_Root)]   = "root",
    [GAMECARD_HFS_PARTITION_NAME_INDEX(GameCardHashFileSystemPartitionType_Update)] = "update",
    [GAMECARD_HFS_PARTITION_NAME_INDEX(GameCardHashFileSystemPartitionType_Logo)]   = "logo",
    [GAMECARD_HFS_PARTITION_NAME_INDEX(GameCardHashFileSystemPartitionType_Normal)] = "normal",
    [GAMECARD_HFS_PARTITION_NAME_INDEX(GameCardHashFileSystemPartitionType_Secure)] = "secure"
};

static const char *g_gameCardHosVersionStrings[GameCardFwVersion_Count] = {
    [GameCardFwVersion_ForDev]       = "1.0.0",
    [GameCardFwVersion_Since100NUP]  = "1.0.0",
    [GameCardFwVersion_Since400NUP]  = "4.0.0",
    [GameCardFwVersion_Since900NUP]  = "9.0.0",
    [GameCardFwVersion_Since1100NUP] = "11.0.0",
    [GameCardFwVersion_Since1200NUP] = "12.0.0"
};

static const char *g_gameCardCompatibilityTypeStrings[GameCardCompatibilityType_Count] = {
    [GameCardCompatibilityType_Normal] = "Normal",
    [GameCardCompatibilityType_Terra]  = "Terra"
};

static const char *g_lafwDeviceTypeStrings[LotusAsicDeviceType_Count] = {
    [LotusAsicDeviceType_Test]     = "Test",
    [LotusAsicDeviceType_Dev]      = "Dev",
    [LotusAsicDeviceType_Prod]     = "Prod",
    [LotusAsicDeviceType_Prod2Dev] = "Prod2Dev"
};

/* Function prototypes. */

static bool gamecardReadLotusAsicFirmwareBlob(void);

static bool gamecardCreateDetectionThread(void);
static void gamecardDestroyDetectionThread(void);
static void gamecardDetectionThreadFunc(void *arg);

NX_INLINE bool gamecardIsInserted(void);

static void gamecardLoadInfo(void);
static void gamecardFreeInfo(bool clear_status);

static bool gamecardReadHeader(void);

static bool _gamecardGetDecryptedCardInfoArea(void);

static bool gamecardReadSecurityInformation(GameCardSecurityInformation *out);

static bool gamecardGetHandleAndStorage(u32 partition);
NX_INLINE void gamecardCloseHandle(void);

static bool gamecardOpenStorageArea(u8 area);
static bool gamecardReadStorageArea(void *out, u64 read_size, u64 offset);
static void gamecardCloseStorageArea(void);

static bool gamecardGetStorageAreasSizes(void);
NX_INLINE u64 gamecardGetCapacityFromRomSizeValue(u8 rom_size);

static HashFileSystemContext *gamecardInitializeHashFileSystemContext(const char *name, u64 offset, u64 size, u8 *hash, u64 hash_target_offset, u32 hash_target_size);
static HashFileSystemContext *_gamecardGetHashFileSystemContext(u8 hfs_partition_type);

bool gamecardInitialize(void)
{
    Result rc = 0;
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = g_gameCardInterfaceInit;
        if (ret) break;

        /* Allocate memory for the gamecard read buffer. */
        g_gameCardReadBuf = malloc(GAMECARD_READ_BUFFER_SIZE);
        if (!g_gameCardReadBuf)
        {
            LOG_MSG("Unable to allocate memory for the gamecard read buffer!");
            break;
        }

        /* Open device operator. */
        rc = fsOpenDeviceOperator(&g_deviceOperator);
        if (R_FAILED(rc))
        {
            LOG_MSG("fsOpenDeviceOperator failed! (0x%08X).", rc);
            break;
        }

        g_openDeviceOperator = true;

        /* Open gamecard detection event notifier. */
        rc = fsOpenGameCardDetectionEventNotifier(&g_gameCardEventNotifier);
        if (R_FAILED(rc))
        {
            LOG_MSG("fsOpenGameCardDetectionEventNotifier failed! (0x%08X)", rc);
            break;
        }

        g_openEventNotifier = true;

        /* Retrieve gamecard detection kernel event. */
        rc = fsEventNotifierGetEventHandle(&g_gameCardEventNotifier, &g_gameCardKernelEvent, true);
        if (R_FAILED(rc))
        {
            LOG_MSG("fsEventNotifierGetEventHandle failed! (0x%08X)", rc);
            break;
        }

        g_loadKernelEvent = true;

        /* Create user-mode exit event. */
        ueventCreate(&g_gameCardDetectionThreadExitEvent, true);

        /* Create user-mode gamecard status change event. */
        ueventCreate(&g_gameCardStatusChangeEvent, true);

        /* Retrieve LAFW blob. */
        if (!gamecardReadLotusAsicFirmwareBlob()) break;

        /* Create gamecard detection thread. */
        if (!(g_gameCardDetectionThreadCreated = gamecardCreateDetectionThread())) break;

        /* Update flags. */
        ret = g_gameCardInterfaceInit = true;
    }

    return ret;
}

void gamecardExit(void)
{
    SCOPED_LOCK(&g_gameCardMutex)
    {
        /* Destroy gamecard detection thread. */
        if (g_gameCardDetectionThreadCreated)
        {
            gamecardDestroyDetectionThread();
            g_gameCardDetectionThreadCreated = false;
        }

        /* Free LAFW blob buffer. */
        if (g_lafwBlob)
        {
            free(g_lafwBlob);
            g_lafwBlob = NULL;
        }

        /* Close gamecard detection kernel event. */
        if (g_loadKernelEvent)
        {
            eventClose(&g_gameCardKernelEvent);
            g_loadKernelEvent = false;
        }

        /* Close gamecard detection event notifier. */
        if (g_openEventNotifier)
        {
            fsEventNotifierClose(&g_gameCardEventNotifier);
            g_openEventNotifier = false;
        }

        /* Close device operator. */
        if (g_openDeviceOperator)
        {
            fsDeviceOperatorClose(&g_deviceOperator);
            g_openDeviceOperator = false;
        }

        /* Free gamecard read buffer. */
        if (g_gameCardReadBuf)
        {
            free(g_gameCardReadBuf);
            g_gameCardReadBuf = NULL;
        }

        g_gameCardInterfaceInit = false;
    }
}

UEvent *gamecardGetStatusChangeUserEvent(void)
{
    UEvent *event = NULL;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (g_gameCardInterfaceInit) event = &g_gameCardStatusChangeEvent;
    }

    return event;
}

u8 gamecardGetStatus(void)
{
    u8 status = GameCardStatus_Processing;

    SCOPED_TRY_LOCK(&g_gameCardMutex)
    {
        if (g_gameCardInterfaceInit) status = g_gameCardStatus;
    }

    return status;
}

/* Read full FS program memory to retrieve the GameCardSecurityInformation block. */
/* In FS program memory, this is returned by Lotus command "ChangeToSecureMode" (0xF). */
/* This means it is only available *after* the gamecard secure area has been mounted, which is taken care of in gamecardReadSecurityInformation(). */
bool gamecardGetSecurityInformation(GameCardSecurityInformation *out)
{
    bool ret = false;
    SCOPED_LOCK(&g_gameCardMutex) ret = gamecardReadSecurityInformation(out);
    return ret;
}

bool gamecardGetIdSet(FsGameCardIdSet *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (!g_gameCardInterfaceInit || g_gameCardStatus != GameCardStatus_InsertedAndInfoLoaded || !out) break;

        Result rc = fsDeviceOperatorGetGameCardIdSet(&g_deviceOperator, out);
        if (R_FAILED(rc)) LOG_MSG("fsDeviceOperatorGetGameCardIdSet failed! (0x%08X)", rc);

        ret = R_SUCCEEDED(rc);
    }

    return ret;
}

bool gamecardGetLotusAsicFirmwareBlob(LotusAsicFirmwareBlob *out_lafw_blob, u64 *out_lafw_version)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (!g_gameCardInterfaceInit || !g_lafwBlob || (!out_lafw_blob && !out_lafw_version)) break;

        /* Copy LAFW blob data. */
        if (out_lafw_blob) memcpy(out_lafw_blob, g_lafwBlob, sizeof(LotusAsicFirmwareBlob));

        /* Copy LAFW version. */
        if (out_lafw_version) *out_lafw_version = g_lafwVersion;

        ret = true;
    }

    return ret;
}

bool gamecardReadStorage(void *out, u64 read_size, u64 offset)
{
    bool ret = false;
    SCOPED_LOCK(&g_gameCardMutex) ret = gamecardReadStorageArea(out, read_size, offset);
    return ret;
}

bool gamecardGetHeader(GameCardHeader *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && g_gameCardStatus == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) memcpy(out, &g_gameCardHeader, sizeof(GameCardHeader));
    }

    return ret;
}

bool gamecardGetDecryptedCardInfoArea(GameCardInfo *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && g_gameCardStatus == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) memcpy(out, &g_gameCardInfoArea, sizeof(GameCardInfo));
    }

    return ret;
}

bool gamecardGetCertificate(FsGameCardCertificate *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (!g_gameCardInterfaceInit || g_gameCardStatus != GameCardStatus_InsertedAndInfoLoaded || !g_gameCardHandle.value || !out) break;

        /* Read the gamecard certificate using the official IPC call. */
        Result rc = fsDeviceOperatorGetGameCardDeviceCertificate(&g_deviceOperator, &g_gameCardHandle, out);
        if (R_FAILED(rc)) LOG_MSG("fsDeviceOperatorGetGameCardDeviceCertificate failed! (0x%08X)", rc);

        ret = R_SUCCEEDED(rc);
    }

    return ret;
}

bool gamecardGetTotalSize(u64 *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && g_gameCardStatus == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) *out = g_gameCardTotalSize;
    }

    return ret;
}

bool gamecardGetTrimmedSize(u64 *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && g_gameCardStatus == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) *out = (sizeof(GameCardHeader) + GAMECARD_PAGE_OFFSET(g_gameCardHeader.valid_data_end_address));
    }

    return ret;
}

bool gamecardGetRomCapacity(u64 *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && g_gameCardStatus == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) *out = g_gameCardCapacity;
    }

    return ret;
}

bool gamecardGetBundledFirmwareUpdateVersion(Version *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (!g_gameCardInterfaceInit || g_gameCardStatus != GameCardStatus_InsertedAndInfoLoaded || !g_gameCardHandle.value || !out) break;

        u64 update_id = 0;
        u32 update_version = 0;

        Result rc = fsDeviceOperatorUpdatePartitionInfo(&g_deviceOperator, &g_gameCardHandle, &update_version, &update_id);
        if (R_FAILED(rc)) LOG_MSG("fsDeviceOperatorUpdatePartitionInfo failed! (0x%08X)", rc);

        ret = (R_SUCCEEDED(rc) && update_id == GAMECARD_UPDATE_TID);
        if (ret) out->value = update_version;
    }

    return ret;
}

bool gamecardGetHashFileSystemContext(u8 hfs_partition_type, HashFileSystemContext *out)
{
    if (!hfs_partition_type || hfs_partition_type >= GameCardHashFileSystemPartitionType_Count || !out)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    bool ret = false;

    /* Free Hash FS context. */
    hfsFreeContext(out);

    SCOPED_LOCK(&g_gameCardMutex)
    {
        /* Get pointer to the Hash FS context for the requested partition. */
        HashFileSystemContext *fs_ctx = _gamecardGetHashFileSystemContext(hfs_partition_type);
        if (!fs_ctx) break;

        /* Fill Hash FS context. */
        out->name = strdup(fs_ctx->name);
        if (!out->name)
        {
            LOG_MSG("Failed to duplicate Hash FS partition name! (%s).", fs_ctx->name);
            break;
        }

        out->type = fs_ctx->type;
        out->offset = fs_ctx->offset;
        out->size = fs_ctx->size;
        out->header_size = fs_ctx->header_size;

        out->header = calloc(fs_ctx->header_size, sizeof(u8));
        if (!out->header)
        {
            LOG_MSG("Failed to duplicate Hash FS partition header! (%s).", fs_ctx->name);
            break;
        }

        memcpy(out->header, fs_ctx->header, fs_ctx->header_size);

        /* Update flag. */
        ret = true;
    }

    if (!ret) hfsFreeContext(out);

    return ret;
}

bool gamecardGetHashFileSystemEntryInfoByName(u8 hfs_partition_type, const char *entry_name, u64 *out_offset, u64 *out_size)
{
    if (!hfs_partition_type || hfs_partition_type >= GameCardHashFileSystemPartitionType_Count || !entry_name || !*entry_name || (!out_offset && !out_size))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        /* Get pointer to the Hash FS context for the requested partition. */
        HashFileSystemContext *fs_ctx = _gamecardGetHashFileSystemContext(hfs_partition_type);
        if (!fs_ctx) break;

        /* Get Hash FS entry by name. */
        HashFileSystemEntry *fs_entry = hfsGetEntryByName(fs_ctx, entry_name);
        if (!fs_entry) break;

        /* Update output variables. */
        if (out_offset) *out_offset = (fs_ctx->offset + fs_ctx->header_size + fs_entry->offset);
        if (out_size) *out_size = fs_entry->size;

        /* Update flag. */
        ret = true;
    }

    return ret;
}

const char *gamecardGetRequiredHosVersionString(u64 fw_version)
{
    return (fw_version < GameCardFwVersion_Count ? g_gameCardHosVersionStrings[fw_version] : NULL);
}

const char *gamecardGetCompatibilityTypeString(u8 compatibility_type)
{
    return (compatibility_type < GameCardCompatibilityType_Count ? g_gameCardCompatibilityTypeStrings[compatibility_type] : NULL);
}

const char *gamecardGetLafwTypeString(u32 fw_type)
{
    const char *type = NULL;

    switch(fw_type)
    {
        case LotusAsicFirmwareType_ReadFw:
            type = "ReadFw";
            break;
        case LotusAsicFirmwareType_ReadDevFw:
            type = "ReadDevFw";
            break;
        case LotusAsicFirmwareType_WriterFw:
            type = "WriterFw";
            break;
        case LotusAsicFirmwareType_RmaFw:
            type = "RmaFw";
            break;
        default:
            break;
    }

    return type;
}

const char *gamecardGetLafwDeviceTypeString(u64 device_type)
{
    return (device_type < LotusAsicDeviceType_Count ? g_lafwDeviceTypeStrings[device_type] : NULL);
}

static bool gamecardReadLotusAsicFirmwareBlob(void)
{
    u64 fw_version = 0;
    bool ret = false, found = false, dev_unit = utilsIsDevelopmentUnit();

    /* Allocate memory for the LAFW blob. */
    g_lafwBlob = calloc(1, sizeof(LotusAsicFirmwareBlob));
    if (!g_lafwBlob)
    {
        LOG_MSG("Failed to allocate memory for LAFW blob!");
        goto end;
    }

    /* Temporarily set the segment mask to .data. */
    g_fsProgramMemory.mask = MemoryProgramSegmentType_Data;

    /* Retrieve full FS program memory dump. */
    ret = memRetrieveProgramMemorySegment(&g_fsProgramMemory);

    /* Clear segment mask. */
    g_fsProgramMemory.mask = 0;

    if (!ret)
    {
        LOG_MSG("Failed to retrieve FS .data segment dump!");
        goto end;
    }

    /* Look for the LAFW ReadFw blob in the FS .data memory dump. */
    for(u64 offset = 0; offset < g_fsProgramMemory.data_size; offset++)
    {
        if ((g_fsProgramMemory.data_size - offset) < sizeof(LotusAsicFirmwareBlob)) break;

        LotusAsicFirmwareBlob *lafw_blob = (LotusAsicFirmwareBlob*)(g_fsProgramMemory.data + offset);
        u32 magic = __builtin_bswap32(lafw_blob->magic), fw_type = lafw_blob->fw_type;

        if (magic == LAFW_MAGIC && ((!dev_unit && fw_type == LotusAsicFirmwareType_ReadFw) || (dev_unit && fw_type == LotusAsicFirmwareType_ReadDevFw)))
        {
            /* Jackpot. */
            memcpy(g_lafwBlob, lafw_blob, sizeof(LotusAsicFirmwareBlob));
            fw_version = lafw_blob->fw_version;
            found = true;
            break;
        }
    }

    if (!found)
    {
        LOG_MSG("Unable to locate Lotus %s blob in FS .data segment!", dev_unit ? "ReadDevFw" : "ReadFw");
        goto end;
    }

    /* Convert LAFW version bitmask to an integer. */
    g_lafwVersion = 0;

    while(fw_version)
    {
        g_lafwVersion += (fw_version & 1);
        fw_version >>= 1;
    }

    LOG_MSG("LAFW version: %lu.", g_lafwVersion);

    /* Update flag. */
    ret = true;

end:
    memFreeMemoryLocation(&g_fsProgramMemory);

    return ret;
}

static bool gamecardCreateDetectionThread(void)
{
    if (!utilsCreateThread(&g_gameCardDetectionThread, gamecardDetectionThreadFunc, NULL, 1))
    {
        LOG_MSG("Failed to create gamecard detection thread!");
        return false;
    }

    return true;
}

static void gamecardDestroyDetectionThread(void)
{
    /* Signal the exit event to terminate the gamecard detection thread. */
    ueventSignal(&g_gameCardDetectionThreadExitEvent);

    /* Wait for the gamecard detection thread to exit. */
    utilsJoinThread(&g_gameCardDetectionThread);
}

static void gamecardDetectionThreadFunc(void *arg)
{
    (void)arg;

    Result rc = 0;
    int idx = 0;

    Waiter gamecard_event_waiter = waiterForEvent(&g_gameCardKernelEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_gameCardDetectionThreadExitEvent);

    /* Retrieve initial gamecard insertion status. */
    /* Load gamecard info right away if a gamecard is inserted, then signal the user mode gamecard status change event. */
    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (gamecardIsInserted()) gamecardLoadInfo();
        ueventSignal(&g_gameCardStatusChangeEvent);
    }

    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, gamecard_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;

        /* Exit event triggered. */
        if (idx == 1) break;

        SCOPED_LOCK(&g_gameCardMutex)
        {
            /* Free gamecard info before proceeding. */
            gamecardFreeInfo(true);

            /* Retrieve current gamecard insertion status. */
            /* Only proceed if we're dealing with a status change. */
            if (gamecardIsInserted())
            {
                /* Don't access the gamecard immediately to avoid conflicts with HOS / sysmodules. */
                utilsSleep(GAMECARD_ACCESS_WAIT_TIME);

                /* Load gamecard info. */
                gamecardLoadInfo();
            }

            /* Signal user mode gamecard status change event. */
            ueventSignal(&g_gameCardStatusChangeEvent);
        }
    }

    /* Free gamecard info and close gamecard handle. */
    gamecardFreeInfo(true);

    threadExit();
}

NX_INLINE bool gamecardIsInserted(void)
{
    bool inserted = false;
    Result rc = fsDeviceOperatorIsGameCardInserted(&g_deviceOperator, &inserted);
    if (R_FAILED(rc)) LOG_MSG("fsDeviceOperatorIsGameCardInserted failed! (0x%08X)", rc);
    return (R_SUCCEEDED(rc) && inserted);
}

static void gamecardLoadInfo(void)
{
    if (g_gameCardStatus == GameCardStatus_InsertedAndInfoLoaded) return;

    HashFileSystemContext *root_fs_ctx = NULL;
    u32 root_fs_entry_count = 0, root_fs_name_table_size = 0;
    char *root_fs_name_table = NULL;

    /* Set initial gamecard status. */
    g_gameCardStatus = GameCardStatus_InsertedAndInfoNotLoaded;

    /* Read gamecard header. */
    /* This step *will* fail if the running CFW enabled the "nogc" patch. */
    /* gamecardGetHandleAndStorage() takes care of updating the gamecard status accordingly if this happens. */
    if (!gamecardReadHeader()) goto end;

    /* Get decrypted CardInfo area from header. */
    if (!_gamecardGetDecryptedCardInfoArea()) goto end;

    /* Check if we meet the Lotus ASIC firmware (LAFW) version requirement. */
    if (g_lafwVersion < g_gameCardInfoArea.fw_version)
    {
        LOG_MSG("LAFW version doesn't meet gamecard requirement! (%lu < %lu).", g_lafwVersion, g_gameCardInfoArea.fw_version);
        g_gameCardStatus = GameCardStatus_LotusAsicFirmwareUpdateRequired;
        goto end;
    }

    /* Retrieve gamecard storage area sizes. */
    /* gamecardReadStorageArea() actually checks if the storage area sizes are greater than zero, so we must perform this step. */
    if (!gamecardGetStorageAreasSizes())
    {
        LOG_MSG("Failed to retrieve gamecard storage area sizes!");
        goto end;
    }

    /* Get gamecard capacity. */
    g_gameCardCapacity = gamecardGetCapacityFromRomSizeValue(g_gameCardHeader.rom_size);
    if (!g_gameCardCapacity)
    {
        LOG_MSG("Invalid gamecard capacity value! (0x%02X).", g_gameCardHeader.rom_size);
        goto end;
    }

    if (utilsGetCustomFirmwareType() == UtilsCustomFirmwareType_SXOS)
    {
        /* The total size for the secure storage area is maxed out under SX OS. */
        /* Let's try to calculate it manually. */
        g_gameCardSecureAreaSize = (g_gameCardCapacity - (g_gameCardNormalAreaSize + GAMECARD_UNUSED_AREA_SIZE(g_gameCardCapacity)));
    }

    /* Initialize Hash FS context for the root partition. */
    root_fs_ctx = gamecardInitializeHashFileSystemContext(NULL, g_gameCardHeader.partition_fs_header_address, 0, g_gameCardHeader.partition_fs_header_hash, 0, g_gameCardHeader.partition_fs_header_size);
    if (!root_fs_ctx) goto end;

    /* Calculate total Hash FS partition count. */
    root_fs_entry_count = hfsGetEntryCount(root_fs_ctx);
    g_gameCardHfsCount = (root_fs_entry_count + 1);

    /* Allocate Hash FS context pointer array. */
    g_gameCardHfsCtx = calloc(g_gameCardHfsCount, sizeof(HashFileSystemContext*));
    if (!g_gameCardHfsCtx)
    {
        LOG_MSG("Unable to allocate Hash FS context pointer array! (%u).", g_gameCardHfsCount);
        goto end;
    }

    /* Set root partition context as the first pointer. */
    g_gameCardHfsCtx[0] = root_fs_ctx;

    /* Get root partition name table. */
    root_fs_name_table_size = ((HashFileSystemHeader*)root_fs_ctx->header)->name_table_size;
    root_fs_name_table = hfsGetNameTable(root_fs_ctx);

    /* Initialize Hash FS contexts for the child partitions. */
    for(u32 i = 0; i < root_fs_entry_count; i++)
    {
        HashFileSystemEntry *fs_entry = hfsGetEntryByIndex(root_fs_ctx, i);
        char *fs_entry_name = (root_fs_name_table + fs_entry->name_offset);
        u64 fs_entry_offset = (root_fs_ctx->offset + root_fs_ctx->header_size + fs_entry->offset);

        if (fs_entry->name_offset >= root_fs_name_table_size || !*fs_entry_name)
        {
            LOG_MSG("Invalid name for root Hash FS partition entry #%u!", i);
            goto end;
        }

        g_gameCardHfsCtx[i + 1] = gamecardInitializeHashFileSystemContext(fs_entry_name, fs_entry_offset, fs_entry->size, fs_entry->hash, fs_entry->hash_target_offset, fs_entry->hash_target_size);
        if (!g_gameCardHfsCtx[i + 1]) goto end;
    }

    /* Update gamecard status. */
    g_gameCardStatus = GameCardStatus_InsertedAndInfoLoaded;

end:
    if (g_gameCardStatus != GameCardStatus_InsertedAndInfoLoaded)
    {
        if (!g_gameCardHfsCtx && root_fs_ctx)
        {
            hfsFreeContext(root_fs_ctx);
            free(root_fs_ctx);
        }

        gamecardFreeInfo(false);
    }
}

static void gamecardFreeInfo(bool clear_status)
{
    memset(&g_gameCardHeader, 0, sizeof(GameCardHeader));

    memset(&g_gameCardInfoArea, 0, sizeof(GameCardInfo));

    g_gameCardNormalAreaSize = g_gameCardSecureAreaSize = g_gameCardTotalSize = 0;

    g_gameCardCapacity = 0;

    if (g_gameCardHfsCtx)
    {
        for(u32 i = 0; i < g_gameCardHfsCount; i++)
        {
            HashFileSystemContext *cur_fs_ctx = g_gameCardHfsCtx[i];
            if (cur_fs_ctx)
            {
                hfsFreeContext(cur_fs_ctx);
                free(cur_fs_ctx);
            }
        }

        free(g_gameCardHfsCtx);
        g_gameCardHfsCtx = NULL;
    }

    g_gameCardHfsCount = 0;

    gamecardCloseStorageArea();

    if (clear_status) g_gameCardStatus = GameCardStatus_NotInserted;
}

static bool gamecardReadHeader(void)
{
    /* Open normal storage area. */
    if (!gamecardOpenStorageArea(GameCardStorageArea_Normal))
    {
        LOG_MSG("Failed to open normal storage area!");
        return false;
    }

    /* Read gamecard header. */
    /* This step doesn't rely on gamecardReadStorageArea() because of its dependence on storage area sizes (which we haven't retrieved). */
    Result rc = fsStorageRead(&g_gameCardStorage, 0, &g_gameCardHeader, sizeof(GameCardHeader));
    if (R_FAILED(rc))
    {
        LOG_MSG("fsStorageRead failed to read gamecard header! (0x%08X).", rc);
        return false;
    }

    //LOG_DATA(&g_gameCardHeader, sizeof(GameCardHeader), "Gamecard header dump:");

    /* Check magic word from gamecard header. */
    if (__builtin_bswap32(g_gameCardHeader.magic) != GAMECARD_HEAD_MAGIC)
    {
        LOG_MSG("Invalid gamecard header magic word! (0x%08X).", __builtin_bswap32(g_gameCardHeader.magic));
        return false;
    }

    return true;
}

static bool _gamecardGetDecryptedCardInfoArea(void)
{
    const u8 *card_info_key = NULL;
    u8 card_info_iv[AES_128_KEY_SIZE] = {0};
    Aes128CbcContext aes_ctx = {0};

    /* Retrieve CardInfo area key. */
    card_info_key = keysGetGameCardInfoKey();
    if (!card_info_key)
    {
        LOG_MSG("Failed to retrieve CardInfo area key!");
        return false;
    }

    /* Reverse CardInfo IV. */
    for(u8 i = 0; i < AES_128_KEY_SIZE; i++) card_info_iv[i] = g_gameCardHeader.card_info_iv[AES_128_KEY_SIZE - i - 1];

    /* Initialize AES-128-CBC context. */
    aes128CbcContextCreate(&aes_ctx, card_info_key, card_info_iv, false);

    /* Decrypt CardInfo area. */
    aes128CbcDecrypt(&aes_ctx, &g_gameCardInfoArea, &(g_gameCardHeader.card_info), sizeof(GameCardInfo));

    //LOG_DATA(&g_gameCardInfoArea, sizeof(GameCardInfo), "Gamecard CardInfo area dump:");

    return true;
}

static bool gamecardReadSecurityInformation(GameCardSecurityInformation *out)
{
    if (!g_gameCardInterfaceInit || g_gameCardStatus != GameCardStatus_InsertedAndInfoLoaded || !out)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    /* Clear output. */
    memset(out, 0, sizeof(GameCardSecurityInformation));

    /* Open secure storage area. */
    if (!gamecardOpenStorageArea(GameCardStorageArea_Secure))
    {
        LOG_MSG("Failed to open secure storage area!");
        return false;
    }

    bool found = false;
    u8 tmp_hash[SHA256_HASH_SIZE] = {0};

    /* Retrieve full FS program memory dump. */
    if (!memRetrieveFullProgramMemory(&g_fsProgramMemory))
    {
        LOG_MSG("Failed to retrieve full FS program memory dump!");
        return false;
    }

    /* Look for the initial data block in the FS memory dump using the package ID and the initial data hash from the gamecard header. */
    for(u64 offset = 0; offset < g_fsProgramMemory.data_size; offset++)
    {
        if ((g_fsProgramMemory.data_size - offset) < sizeof(GameCardInitialData)) break;

        if (memcmp(g_fsProgramMemory.data + offset, &(g_gameCardHeader.package_id), sizeof(g_gameCardHeader.package_id)) != 0) continue;

        sha256CalculateHash(tmp_hash, g_fsProgramMemory.data + offset, sizeof(GameCardInitialData));
        if (!memcmp(tmp_hash, g_gameCardHeader.initial_data_hash, SHA256_HASH_SIZE))
        {
            /* Jackpot. */
            memcpy(out, g_fsProgramMemory.data + offset + sizeof(GameCardInitialData) - sizeof(GameCardSecurityInformation), sizeof(GameCardSecurityInformation));

            /* Clear out the current ASIC session hash. */
            /* It's not actually part of the gamecard data, and this changes every time a gamecard (re)insertion takes place. */
            memset(out->specific_data.asic_session_hash, 0xFF, sizeof(out->specific_data.asic_session_hash));

            found = true;
            break;
        }
    }

    /* Free FS memory dump. */
    memFreeMemoryLocation(&g_fsProgramMemory);

    return found;
}

static bool gamecardGetHandleAndStorage(u32 partition)
{
    if (g_gameCardStatus < GameCardStatus_InsertedAndInfoNotLoaded || partition > 1)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    Result rc = 0;

    /* 10 tries. */
    for(u8 i = 0; i < 10; i++)
    {
        /* 100 ms wait in case there was an error in the previous loop. */
        if (R_FAILED(rc)) svcSleepThread(100000000);

        /* First, let's try to retrieve a gamecard handle. */
        /* This can return an error if the "nogc" patch is enabled by the running CFW (most commonly 0x140A02). */
        rc = fsDeviceOperatorGetGameCardHandle(&g_deviceOperator, &g_gameCardHandle);
        if (R_FAILED(rc))
        {
            //LOG_MSG("fsDeviceOperatorGetGameCardHandle failed on try #%u! (0x%08X).", i + 1, rc);
            continue;
        }

        /* If the previous call succeeded, let's try to open the desired gamecard storage area. */
        rc = fsOpenGameCardStorage(&g_gameCardStorage, &g_gameCardHandle, partition);
        if (R_FAILED(rc))
        {
            gamecardCloseHandle(); /* Close invalid gamecard handle. */
            //LOG_MSG("fsOpenGameCardStorage failed to open %s storage area on try #%u! (0x%08X).", GAMECARD_STORAGE_AREA_NAME(partition + 1), i + 1, rc);
            continue;
        }

        /* If we got up to this point, both a valid gamecard handle and a valid storage area handle are guaranteed. */
        break;
    }

    if (R_FAILED(rc))
    {
        LOG_MSG("fsDeviceOperatorGetGameCardHandle / fsOpenGameCardStorage failed! (0x%08X).", rc);
        if (g_gameCardStatus == GameCardStatus_InsertedAndInfoNotLoaded && partition == 0) g_gameCardStatus = GameCardStatus_NoGameCardPatchEnabled;
    }

    return R_SUCCEEDED(rc);
}

NX_INLINE void gamecardCloseHandle(void)
{
    g_gameCardHandle.value = 0;
}

static bool gamecardOpenStorageArea(u8 area)
{
    if (g_gameCardStatus < GameCardStatus_InsertedAndInfoNotLoaded || (area != GameCardStorageArea_Normal && area != GameCardStorageArea_Secure))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    /* Return right away if a valid handle has already been retrieved and the desired gamecard storage area is currently open. */
    if (g_gameCardHandle.value && serviceIsActive(&(g_gameCardStorage.s)) && g_gameCardCurrentStorageArea == area) return true;

    /* Close both gamecard handle and open storage area. */
    gamecardCloseStorageArea();

    /* Retrieve both a new gamecard handle and a storage area handle. */
    if (!gamecardGetHandleAndStorage(area - 1)) /* Zero-based index. */
    {
        LOG_MSG("Failed to retrieve gamecard handle and storage area handle! (%s).", GAMECARD_STORAGE_AREA_NAME(area));
        return false;
    }

    /* Update current gamecard storage area. */
    g_gameCardCurrentStorageArea = area;

    return true;
}

static bool gamecardReadStorageArea(void *out, u64 read_size, u64 offset)
{
    if (g_gameCardStatus < GameCardStatus_InsertedAndInfoNotLoaded || !g_gameCardNormalAreaSize || !g_gameCardSecureAreaSize || !out || !read_size || (offset + read_size) > g_gameCardTotalSize)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    Result rc = 0;
    u8 *out_u8 = (u8*)out;
    u8 area = (offset < g_gameCardNormalAreaSize ? GameCardStorageArea_Normal : GameCardStorageArea_Secure);
    bool success = false;

    /* Handle reads that span both the normal and secure gamecard storage areas. */
    if (area == GameCardStorageArea_Normal && (offset + read_size) > g_gameCardNormalAreaSize)
    {
        /* Calculate normal storage area size difference. */
        u64 diff_size = (g_gameCardNormalAreaSize - offset);

        /* Read normal storage area data. */
        if (!gamecardReadStorageArea(out_u8, diff_size, offset)) goto end;

        /* Adjust variables to read right from the start of the secure storage area. */
        read_size -= diff_size;
        offset = g_gameCardNormalAreaSize;
        out_u8 += diff_size;
        area = GameCardStorageArea_Secure;
    }

    /* Open a storage area if needed. */
    /* If the right storage area has already been opened, this will return true. */
    if (!gamecardOpenStorageArea(area))
    {
        LOG_MSG("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
        goto end;
    }

    /* Calculate proper storage area offset. */
    u64 base_offset = (area == GameCardStorageArea_Normal ? offset : (offset - g_gameCardNormalAreaSize));

    if (!(base_offset % GAMECARD_PAGE_SIZE) && !(read_size % GAMECARD_PAGE_SIZE))
    {
        /* Optimization for reads that are already aligned to a GAMECARD_PAGE_SIZE boundary. */
        rc = fsStorageRead(&g_gameCardStorage, base_offset, out_u8, read_size);
        if (R_FAILED(rc))
        {
            LOG_MSG("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (aligned).", read_size, base_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
            goto end;
        }

        success = true;
    } else {
        /* Fix offset and/or size to avoid unaligned reads. */
        u64 block_start_offset = ALIGN_DOWN(base_offset, GAMECARD_PAGE_SIZE);
        u64 block_end_offset = ALIGN_UP(base_offset + read_size, GAMECARD_PAGE_SIZE);
        u64 block_size = (block_end_offset - block_start_offset);

        u64 data_start_offset = (base_offset - block_start_offset);
        u64 chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? GAMECARD_READ_BUFFER_SIZE : block_size);
        u64 out_chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? (GAMECARD_READ_BUFFER_SIZE - data_start_offset) : read_size);

        rc = fsStorageRead(&g_gameCardStorage, block_start_offset, g_gameCardReadBuf, chunk_size);
        if (R_FAILED(rc))
        {
            LOG_MSG("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (unaligned).", chunk_size, block_start_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
            goto end;
        }

        memcpy(out_u8, g_gameCardReadBuf + data_start_offset, out_chunk_size);

        success = (block_size > GAMECARD_READ_BUFFER_SIZE ? gamecardReadStorageArea(out_u8 + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size) : true);
    }

end:
    return success;
}

static void gamecardCloseStorageArea(void)
{
    if (g_gameCardCurrentStorageArea == GameCardStorageArea_None) return;

    if (serviceIsActive(&(g_gameCardStorage.s)))
    {
        fsStorageClose(&g_gameCardStorage);
        memset(&g_gameCardStorage, 0, sizeof(FsStorage));
    }

    gamecardCloseHandle();

    g_gameCardCurrentStorageArea = GameCardStorageArea_None;
}

static bool gamecardGetStorageAreasSizes(void)
{
    for(u8 i = 0; i < 2; i++)
    {
        Result rc = 0;
        u64 area_size = 0;
        u8 area = (i == 0 ? GameCardStorageArea_Normal : GameCardStorageArea_Secure);

        if (!gamecardOpenStorageArea(area))
        {
            LOG_MSG("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
            return false;
        }

        rc = fsStorageGetSize(&g_gameCardStorage, (s64*)&area_size);

        gamecardCloseStorageArea();

        if (R_FAILED(rc) || !area_size)
        {
            LOG_MSG("fsStorageGetSize failed to retrieve %s storage area size! (0x%08X).", GAMECARD_STORAGE_AREA_NAME(area), rc);
            return false;
        }

        if (area == GameCardStorageArea_Normal)
        {
            g_gameCardNormalAreaSize = area_size;
        } else {
            g_gameCardSecureAreaSize = area_size;
        }
    }

    g_gameCardTotalSize = (g_gameCardNormalAreaSize + g_gameCardSecureAreaSize);

    return true;
}

NX_INLINE u64 gamecardGetCapacityFromRomSizeValue(u8 rom_size)
{
    u64 capacity = 0;

    switch(rom_size)
    {
        case GameCardRomSize_1GiB:
            capacity = GameCardCapacity_1GiB;
            break;
        case GameCardRomSize_2GiB:
            capacity = GameCardCapacity_2GiB;
            break;
        case GameCardRomSize_4GiB:
            capacity = GameCardCapacity_4GiB;
            break;
        case GameCardRomSize_8GiB:
            capacity = GameCardCapacity_8GiB;
            break;
        case GameCardRomSize_16GiB:
            capacity = GameCardCapacity_16GiB;
            break;
        case GameCardRomSize_32GiB:
            capacity = GameCardCapacity_32GiB;
            break;
        default:
            break;
    }

    return capacity;
}

static HashFileSystemContext *gamecardInitializeHashFileSystemContext(const char *name, u64 offset, u64 size, u8 *hash, u64 hash_target_offset, u32 hash_target_size)
{
    u32 i = 0, magic = 0;
    HashFileSystemContext *fs_ctx = NULL;
    HashFileSystemHeader fs_header = {0};
    u8 fs_header_hash[SHA256_HASH_SIZE] = {0};

    bool success = false, dump_fs_header = false;

    if ((name && !*name) || offset < (GAMECARD_CERTIFICATE_OFFSET + sizeof(FsGameCardCertificate)) || !IS_ALIGNED(offset, GAMECARD_PAGE_SIZE) || \
        (size && (!IS_ALIGNED(size, GAMECARD_PAGE_SIZE) || (offset + size) > g_gameCardTotalSize)))
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }

    /* Allocate memory for the output context. */
    fs_ctx = calloc(1, sizeof(HashFileSystemContext));
    if (!fs_ctx)
    {
        LOG_MSG("Unable to allocate memory for Hash FS context! (offset 0x%lX).", offset);
        goto end;
    }

    /* Duplicate partition name. */
    fs_ctx->name = (name ? strdup(name) : strdup(g_gameCardHfsPartitionNames[GAMECARD_HFS_PARTITION_NAME_INDEX(GameCardHashFileSystemPartitionType_Root)]));
    if (!fs_ctx->name)
    {
        LOG_MSG("Failed to duplicate Hash FS partition name! (offset 0x%lX).", offset);
        goto end;
    }

    /* Determine Hash FS partition type. */
    for(i = GameCardHashFileSystemPartitionType_Root; i < GameCardHashFileSystemPartitionType_Count; i++)
    {
        if (!strcmp(g_gameCardHfsPartitionNames[GAMECARD_HFS_PARTITION_NAME_INDEX(i)], fs_ctx->name)) break;
    }

    if (i >= GameCardHashFileSystemPartitionType_Count)
    {
        LOG_MSG("Failed to find a matching Hash FS partition type for \"%s\"! (offset 0x%lX).", fs_ctx->name, offset);
        goto end;
    }

    fs_ctx->type = i;

    /* Read partial Hash FS header. */
    if (!gamecardReadStorageArea(&fs_header, sizeof(HashFileSystemHeader), offset))
    {
        LOG_MSG("Failed to read partial Hash FS header! (\"%s\", offset 0x%lX).", fs_ctx->name, offset);
        goto end;
    }

    magic = __builtin_bswap32(fs_header.magic);
    if (magic != HFS0_MAGIC)
    {
        LOG_MSG("Invalid Hash FS magic word! (0x%08X) (\"%s\", offset 0x%lX).", magic, fs_ctx->name, offset);
        dump_fs_header = true;
        goto end;
    }

    /* Check Hash FS entry count and name table size. */
    /* Only allow a zero entry count if we're not dealing with the root partition. Never allow a zero-sized name table. */
    if ((!name && !fs_header.entry_count) || !fs_header.name_table_size)
    {
        LOG_MSG("Invalid Hash FS entry count / name table size! (\"%s\", offset 0x%lX).", fs_ctx->name, offset);
        dump_fs_header = true;
        goto end;
    }

    /* Calculate full Hash FS header size. */
    fs_ctx->header_size = (sizeof(HashFileSystemHeader) + (fs_header.entry_count * sizeof(HashFileSystemEntry)) + fs_header.name_table_size);
    fs_ctx->header_size = ALIGN_UP(fs_ctx->header_size, GAMECARD_PAGE_SIZE);

    /* Allocate memory for the full Hash FS header. */
    fs_ctx->header = calloc(fs_ctx->header_size, sizeof(u8));
    if (!fs_ctx->header)
    {
        LOG_MSG("Unable to allocate 0x%lX bytes buffer for the full Hash FS header! (\"%s\", offset 0x%lX).", fs_ctx->header_size, fs_ctx->name, offset);
        goto end;
    }

    /* Read full Hash FS header. */
    if (!gamecardReadStorageArea(fs_ctx->header, fs_ctx->header_size, offset))
    {
        LOG_MSG("Failed to read full Hash FS header! (\"%s\", offset 0x%lX).", fs_ctx->name, offset);
        goto end;
    }

    /* Verify Hash FS header (if possible). */
    if (hash && hash_target_size && (hash_target_offset + hash_target_size) <= fs_ctx->header_size)
    {
        sha256CalculateHash(fs_header_hash, fs_ctx->header + hash_target_offset, hash_target_size);
        if (memcmp(fs_header_hash, hash, SHA256_HASH_SIZE) != 0)
        {
            LOG_MSG("Hash FS header doesn't match expected SHA-256 hash! (\"%s\", offset 0x%lX).", fs_ctx->name, offset);
            goto end;
        }
    }

    /* Fill context. */
    fs_ctx->offset = offset;

    if (name)
    {
        /* Use provided partition size. */
        fs_ctx->size = size;
    } else {
        /* Calculate root partition size. */
        HashFileSystemEntry *fs_entry = hfsGetEntryByIndex(fs_ctx, fs_header.entry_count - 1);
        fs_ctx->size = (fs_ctx->header_size + fs_entry->offset + fs_entry->size);
    }

    /* Update flag. */
    success = true;

end:
    if (!success && fs_ctx)
    {
        if (dump_fs_header) LOG_DATA(&fs_header, sizeof(HashFileSystemHeader), "Partial Hash FS header dump (\"%s\", offset 0x%lX):", fs_ctx->name, offset);

        if (fs_ctx->header) free(fs_ctx->header);

        if (fs_ctx->name) free(fs_ctx->name);

        free(fs_ctx);
        fs_ctx = NULL;
    }

    return fs_ctx;
}

static HashFileSystemContext *_gamecardGetHashFileSystemContext(u8 hfs_partition_type)
{
    HashFileSystemContext *fs_ctx = NULL;

    if (!g_gameCardInterfaceInit || g_gameCardStatus != GameCardStatus_InsertedAndInfoLoaded || !g_gameCardHfsCount || !g_gameCardHfsCtx || !hfs_partition_type || \
        hfs_partition_type >= GameCardHashFileSystemPartitionType_Count)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }

    /* Return right away if the root partition was requested. */
    if (hfs_partition_type == GameCardHashFileSystemPartitionType_Root)
    {
        fs_ctx = g_gameCardHfsCtx[0];
        goto end;
    }

    /* Try to find the requested partition by looping through our Hash FS contexts. */
    for(u32 i = 1; i < g_gameCardHfsCount; i++)
    {
        fs_ctx = g_gameCardHfsCtx[i];
        if (fs_ctx->type == hfs_partition_type) break;
        fs_ctx = NULL;
    }

    if (!fs_ctx) LOG_MSG("Failed to locate Hash FS partition with type %u!", hfs_partition_type);

end:
    return fs_ctx;
}
