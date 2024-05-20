/*
 * gamecard.c
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
#include <core/mem.h>
#include <core/gamecard.h>
#include <core/keys.h>
#include <core/rsa.h>

#define GAMECARD_READ_BUFFER_SIZE               0x800000                /* 8 MiB. */

#define GAMECARD_ACCESS_DELAY                   3                       /* Seconds. */

#define GAMECARD_UNUSED_AREA_BLOCK_SIZE         0x24
#define GAMECARD_UNUSED_AREA_SIZE(x)            (((x) / GAMECARD_PAGE_SIZE) * GAMECARD_UNUSED_AREA_BLOCK_SIZE)

#define GAMECARD_STORAGE_AREA_NAME(x)           ((x) == GameCardStorageArea_Normal ? "normal" : ((x) == GameCardStorageArea_Secure ? "secure" : "none"))

#define LAFW_MAGIC                              0x4C414657              /* "LAFW". */

/* Type definitions. */

typedef enum {
    GameCardStorageArea_None   = 0,
    GameCardStorageArea_Normal = 1,
    GameCardStorageArea_Secure = 2
} GameCardStorageArea;

typedef enum {
    GameCardCapacity_1GiB  = BITL(30),
    GameCardCapacity_2GiB  = BITL(31),
    GameCardCapacity_4GiB  = BITL(32),
    GameCardCapacity_8GiB  = BITL(33),
    GameCardCapacity_16GiB = BITL(34),
    GameCardCapacity_32GiB = BITL(35)
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

static atomic_uchar g_gameCardStatus = GameCardStatus_NotInserted;

static FsGameCardHandle g_gameCardHandle = {0};
static FsStorage g_gameCardStorage = {0};
static u8 g_gameCardCurrentStorageArea = GameCardStorageArea_None;
static u8 *g_gameCardReadBuf = NULL;

static GameCardHeader g_gameCardHeader = {0};
static GameCardInfo g_gameCardInfoArea = {0};

static GameCardHeader2 g_gameCardHeader2 = {0};
static GameCardHeader2Certificate g_gameCardHeader2Cert = {0};

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

static bool _gamecardGetPlaintextCardInfoArea(void);

static bool gamecardReadSecurityInformation(GameCardSecurityInformation *out);

static bool gamecardGetHandleAndStorage(u32 partition);

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
            LOG_MSG_ERROR("Unable to allocate memory for the gamecard read buffer!");
            break;
        }

        /* Open device operator. */
        rc = fsOpenDeviceOperator(&g_deviceOperator);
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("fsOpenDeviceOperator failed! (0x%X).", rc);
            break;
        }

        g_openDeviceOperator = true;

        /* Open gamecard detection event notifier. */
        rc = fsOpenGameCardDetectionEventNotifier(&g_gameCardEventNotifier);
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("fsOpenGameCardDetectionEventNotifier failed! (0x%X)", rc);
            break;
        }

        g_openEventNotifier = true;

        /* Retrieve gamecard detection kernel event. */
        rc = fsEventNotifierGetEventHandle(&g_gameCardEventNotifier, &g_gameCardKernelEvent, true);
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("fsEventNotifierGetEventHandle failed! (0x%X)", rc);
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

        /* Make sure NS can access the gamecard. */
        /* Fixes gamecard launch errors after exiting the application. */
        /* TODO: find out why this doesn't work. */
        //Result rc = nsEnsureGameCardAccess();
        //if (R_FAILED(rc)) LOG_MSG_ERROR("nsEnsureGameCardAccess failed! (0x%X).", rc);

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
    return atomic_load(&g_gameCardStatus);
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

bool gamecardGetCardIdSet(FsGameCardIdSet *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (!g_gameCardInterfaceInit || atomic_load(&g_gameCardStatus) != GameCardStatus_InsertedAndInfoLoaded || !out) break;

        Result rc = fsDeviceOperatorGetGameCardIdSet(&g_deviceOperator, out, sizeof(FsGameCardIdSet), (s64)sizeof(FsGameCardIdSet));
        if (R_FAILED(rc)) LOG_MSG_ERROR("fsDeviceOperatorGetGameCardIdSet failed! (0x%X)", rc);

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
        ret = (g_gameCardInterfaceInit && atomic_load(&g_gameCardStatus) == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) memcpy(out, &g_gameCardHeader, sizeof(GameCardHeader));
    }

    return ret;
}

bool gamecardGetPlaintextCardInfoArea(GameCardInfo *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && atomic_load(&g_gameCardStatus) == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) memcpy(out, &g_gameCardInfoArea, sizeof(GameCardInfo));
    }

    return ret;
}

bool gamecardGetCertificate(FsGameCardCertificate *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (!g_gameCardInterfaceInit || atomic_load(&g_gameCardStatus) != GameCardStatus_InsertedAndInfoLoaded || !g_gameCardHandle.value || !out) break;

        /* Read the gamecard certificate using the official IPC call. */
        Result rc = fsDeviceOperatorGetGameCardDeviceCertificate(&g_deviceOperator, &g_gameCardHandle, out, sizeof(FsGameCardCertificate), (s64)sizeof(FsGameCardCertificate));
        if (R_FAILED(rc)) LOG_MSG_ERROR("fsDeviceOperatorGetGameCardDeviceCertificate failed! (0x%X)", rc);

        ret = R_SUCCEEDED(rc);
    }

    return ret;
}

bool gamecardGetTotalSize(u64 *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && atomic_load(&g_gameCardStatus) == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) *out = g_gameCardTotalSize;
    }

    return ret;
}

bool gamecardGetTrimmedSize(u64 *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && atomic_load(&g_gameCardStatus) == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) *out = (sizeof(GameCardHeader) + GAMECARD_PAGE_OFFSET(g_gameCardHeader.valid_data_end_page));
    }

    return ret;
}

bool gamecardGetRomCapacity(u64 *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        ret = (g_gameCardInterfaceInit && atomic_load(&g_gameCardStatus) == GameCardStatus_InsertedAndInfoLoaded && out);
        if (ret) *out = g_gameCardCapacity;
    }

    return ret;
}

bool gamecardGetBundledFirmwareUpdateVersion(Version *out)
{
    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (!g_gameCardInterfaceInit || atomic_load(&g_gameCardStatus) != GameCardStatus_InsertedAndInfoLoaded || !g_gameCardHandle.value || !out) break;

        u64 update_id = 0;
        u32 update_version = 0;

        Result rc = fsDeviceOperatorUpdatePartitionInfo(&g_deviceOperator, &g_gameCardHandle, &update_version, &update_id);
        if (R_FAILED(rc)) LOG_MSG_ERROR("fsDeviceOperatorUpdatePartitionInfo failed! (0x%X)", rc);

        ret = (R_SUCCEEDED(rc) && update_id == GAMECARD_UPDATE_TID);
        if (ret) out->value = update_version;
    }

    return ret;
}

bool gamecardGetHashFileSystemContext(u8 hfs_partition_type, HashFileSystemContext *out)
{
    if (hfs_partition_type < HashFileSystemPartitionType_Root || hfs_partition_type >= HashFileSystemPartitionType_Count || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;

    /* Free Hash FS context. */
    hfsFreeContext(out);

    SCOPED_LOCK(&g_gameCardMutex)
    {
        /* Get pointer to the Hash FS context for the requested partition. */
        HashFileSystemContext *hfs_ctx = _gamecardGetHashFileSystemContext(hfs_partition_type);
        if (!hfs_ctx) break;

        /* Fill Hash FS context. */
        out->name = strdup(hfs_ctx->name);
        if (!out->name)
        {
            LOG_MSG_ERROR("Failed to duplicate Hash FS partition name! (%s).", hfs_ctx->name);
            break;
        }

        out->type = hfs_ctx->type;
        out->offset = hfs_ctx->offset;
        out->size = hfs_ctx->size;
        out->header_size = hfs_ctx->header_size;

        out->header = calloc(hfs_ctx->header_size, sizeof(u8));
        if (!out->header)
        {
            LOG_MSG_ERROR("Failed to duplicate Hash FS partition header! (%s).", hfs_ctx->name);
            break;
        }

        memcpy(out->header, hfs_ctx->header, hfs_ctx->header_size);

        /* Update flag. */
        ret = true;
    }

    if (!ret) hfsFreeContext(out);

    return ret;
}

bool gamecardGetHashFileSystemEntryInfoByName(u8 hfs_partition_type, const char *entry_name, u64 *out_offset, u64 *out_size)
{
    if (hfs_partition_type < HashFileSystemPartitionType_Root || hfs_partition_type >= HashFileSystemPartitionType_Count || !entry_name || !*entry_name || (!out_offset && !out_size))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_gameCardMutex)
    {
        /* Get pointer to the Hash FS context for the requested partition. */
        HashFileSystemContext *hfs_ctx = _gamecardGetHashFileSystemContext(hfs_partition_type);
        if (!hfs_ctx) break;

        /* Get Hash FS entry by name. */
        HashFileSystemEntry *hfs_entry = hfsGetEntryByName(hfs_ctx, entry_name);
        if (!hfs_entry) break;

        /* Update output variables. */
        if (out_offset) *out_offset = (hfs_ctx->offset + hfs_ctx->header_size + hfs_entry->offset);
        if (out_size) *out_size = hfs_entry->size;

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
        LOG_MSG_ERROR("Failed to allocate memory for LAFW blob!");
        goto end;
    }

    /* Temporarily set the segment mask to .data. */
    g_fsProgramMemory.mask = MemoryProgramSegmentType_Data;

    /* Retrieve FS .data segment memory dump. */
    if (!memRetrieveProgramMemorySegment(&g_fsProgramMemory))
    {
        LOG_MSG_ERROR("Failed to retrieve FS .data segment dump!");
        goto end;
    }

    /* Look for the LAFW ReadFw blob in the FS .data segment memory dump. */
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
        LOG_MSG_ERROR("Unable to locate Lotus %s blob in FS .data segment!", dev_unit ? "ReadDevFw" : "ReadFw");
        goto end;
    }

    /* Convert LAFW version bitmask to an integer. */
    g_lafwVersion = 0;

    while(fw_version)
    {
        g_lafwVersion += (fw_version & 1);
        fw_version >>= 1;
    }

    LOG_MSG_INFO("LAFW version: %lu.", g_lafwVersion);

    /* Update flag. */
    ret = true;

end:
    memFreeMemoryLocation(&g_fsProgramMemory);

    g_fsProgramMemory.mask = MemoryProgramSegmentType_None;

    return ret;
}

static bool gamecardCreateDetectionThread(void)
{
    if (!utilsCreateThread(&g_gameCardDetectionThread, gamecardDetectionThreadFunc, NULL, 1))
    {
        LOG_MSG_ERROR("Failed to create gamecard detection thread!");
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
    NX_IGNORE_ARG(arg);

    Result rc = 0;
    int idx = 0;

    Waiter gamecard_event_waiter = waiterForEvent(&g_gameCardKernelEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_gameCardDetectionThreadExitEvent);

    /* Retrieve initial gamecard insertion status. */
    /* Load gamecard info right away if a gamecard is inserted, then signal the user mode gamecard status change event. */
    SCOPED_LOCK(&g_gameCardMutex)
    {
        if (gamecardIsInserted())
        {
            atomic_store(&g_gameCardStatus, GameCardStatus_Processing);
            gamecardLoadInfo();
        }

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

            /* Set initial gamecard status. */
            bool gc_inserted = gamecardIsInserted();
            atomic_store(&g_gameCardStatus, gc_inserted ? GameCardStatus_Processing : GameCardStatus_NotInserted);

            /* Delay gamecard access by GAMECARD_ACCESS_DELAY full seconds. This is done to to avoid conflicts with HOS / sysmodules. */
            /* We will periodically check if the gamecard is still inserted during this period. */
            /* If the gamecard is taken out before reaching the length of the delay, we won't try to access it. */
            time_t start = time(NULL);
            bool gc_delay_passed = false;

            while(gc_inserted)
            {
                time_t now = time(NULL);
                time_t diff = (now - start);

                if (diff >= GAMECARD_ACCESS_DELAY)
                {
                    gc_delay_passed = true;
                    break;
                }

                utilsAppletLoopDelay();

                gc_inserted = gamecardIsInserted();
            }

            /* Load gamecard info (if applicable). */
            if (gc_delay_passed) gamecardLoadInfo();

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
    if (R_FAILED(rc)) LOG_MSG_ERROR("fsDeviceOperatorIsGameCardInserted failed! (0x%X)", rc);
    return (R_SUCCEEDED(rc) && inserted);
}

static void gamecardLoadInfo(void)
{
    if (atomic_load(&g_gameCardStatus) == GameCardStatus_InsertedAndInfoLoaded) return;

    HashFileSystemContext *root_hfs_ctx = NULL;
    u32 root_hfs_entry_count = 0, root_hfs_name_table_size = 0;
    char *root_hfs_name_table = NULL;

    /* Read gamecard header. */
    /* This step *will* fail if the running CFW enabled the "nogc" patch. */
    /* gamecardGetHandleAndStorage() takes care of updating the gamecard status accordingly if this happens. */
    if (!gamecardReadHeader()) goto end;

    /* Get decrypted CardInfo area from header. */
    if (!_gamecardGetPlaintextCardInfoArea()) goto end;

    /* Check if we meet the Lotus ASIC firmware (LAFW) version requirement. */
    if (g_lafwVersion < g_gameCardInfoArea.fw_version)
    {
        LOG_MSG_ERROR("LAFW version doesn't meet gamecard requirement! (%lu < %lu).", g_lafwVersion, g_gameCardInfoArea.fw_version);
        atomic_store(&g_gameCardStatus, GameCardStatus_LotusAsicFirmwareUpdateRequired);
        goto end;
    }

    /* Retrieve gamecard storage area sizes. */
    /* gamecardReadStorageArea() actually checks if the storage area sizes are greater than zero, so we must perform this step. */
    if (!gamecardGetStorageAreasSizes())
    {
        LOG_MSG_ERROR("Failed to retrieve gamecard storage area sizes!");
        goto end;
    }

    /* Get gamecard capacity. */
    g_gameCardCapacity = gamecardGetCapacityFromRomSizeValue(g_gameCardHeader.rom_size);
    if (!g_gameCardCapacity)
    {
        LOG_MSG_ERROR("Invalid gamecard capacity value! (0x%02X).", g_gameCardHeader.rom_size);
        goto end;
    }

    if (utilsGetCustomFirmwareType() == UtilsCustomFirmwareType_SXOS)
    {
        /* The total size for the secure storage area is maxed out under SX OS. */
        /* Let's try to calculate it manually. */
        g_gameCardSecureAreaSize = (g_gameCardCapacity - (g_gameCardNormalAreaSize + GAMECARD_UNUSED_AREA_SIZE(g_gameCardCapacity)));
    }

    /* Initialize Hash FS context for the root partition. */
    root_hfs_ctx = gamecardInitializeHashFileSystemContext(NULL, g_gameCardHeader.partition_fs_header_address, 0, g_gameCardHeader.partition_fs_header_hash, 0, g_gameCardHeader.partition_fs_header_size);
    if (!root_hfs_ctx) goto end;

    /* Calculate total Hash FS partition count. */
    root_hfs_entry_count = hfsGetEntryCount(root_hfs_ctx);
    g_gameCardHfsCount = (root_hfs_entry_count + 1);

    /* Allocate Hash FS context pointer array. */
    g_gameCardHfsCtx = calloc(g_gameCardHfsCount, sizeof(HashFileSystemContext*));
    if (!g_gameCardHfsCtx)
    {
        LOG_MSG_ERROR("Unable to allocate Hash FS context pointer array! (%u).", g_gameCardHfsCount);
        goto end;
    }

    /* Set root partition context as the first pointer. */
    g_gameCardHfsCtx[0] = root_hfs_ctx;

    /* Get root partition name table. */
    root_hfs_name_table_size = ((HashFileSystemHeader*)root_hfs_ctx->header)->name_table_size;
    root_hfs_name_table = hfsGetNameTable(root_hfs_ctx);

    /* Initialize Hash FS contexts for the child partitions. */
    for(u32 i = 0; i < root_hfs_entry_count; i++)
    {
        HashFileSystemEntry *hfs_entry = hfsGetEntryByIndex(root_hfs_ctx, i);
        char *hfs_entry_name = (root_hfs_name_table + hfs_entry->name_offset);
        u64 hfs_entry_offset = (root_hfs_ctx->offset + root_hfs_ctx->header_size + hfs_entry->offset);

        if (hfs_entry->name_offset >= root_hfs_name_table_size || !*hfs_entry_name)
        {
            LOG_MSG_ERROR("Invalid name for root Hash FS partition entry #%u!", i);
            goto end;
        }

        g_gameCardHfsCtx[i + 1] = gamecardInitializeHashFileSystemContext(hfs_entry_name, hfs_entry_offset, hfs_entry->size, hfs_entry->hash, hfs_entry->hash_target_offset, hfs_entry->hash_target_size);
        if (!g_gameCardHfsCtx[i + 1]) goto end;
    }

    /* Update gamecard status. */
    atomic_store(&g_gameCardStatus, GameCardStatus_InsertedAndInfoLoaded);

end:
    u8 status = atomic_load(&g_gameCardStatus);
    if (status != GameCardStatus_InsertedAndInfoLoaded)
    {
        if (status == GameCardStatus_Processing) atomic_store(&g_gameCardStatus, GameCardStatus_InsertedAndInfoNotLoaded);

        if (!g_gameCardHfsCtx && root_hfs_ctx)
        {
            hfsFreeContext(root_hfs_ctx);
            free(root_hfs_ctx);
        }

        gamecardFreeInfo(false);
    }
}

static void gamecardFreeInfo(bool clear_status)
{
    memset(&g_gameCardHeader, 0, sizeof(GameCardHeader));
    memset(&g_gameCardInfoArea, 0, sizeof(GameCardInfo));

    memset(&g_gameCardHeader2, 0, sizeof(GameCardHeader2));
    memset(&g_gameCardHeader2Cert, 0, sizeof(GameCardHeader2Certificate));

    g_gameCardNormalAreaSize = g_gameCardSecureAreaSize = g_gameCardTotalSize = 0;

    g_gameCardCapacity = 0;

    if (g_gameCardHfsCtx)
    {
        for(u32 i = 0; i < g_gameCardHfsCount; i++)
        {
            HashFileSystemContext *cur_hfs_ctx = g_gameCardHfsCtx[i];
            if (cur_hfs_ctx)
            {
                hfsFreeContext(cur_hfs_ctx);
                free(cur_hfs_ctx);
            }
        }

        free(g_gameCardHfsCtx);
        g_gameCardHfsCtx = NULL;
    }

    g_gameCardHfsCount = 0;

    gamecardCloseStorageArea();

    if (clear_status) atomic_store(&g_gameCardStatus, GameCardStatus_NotInserted);
}

static bool gamecardReadHeader(void)
{
    Result rc = 0;

    /* Open normal storage area. */
    if (!gamecardOpenStorageArea(GameCardStorageArea_Normal))
    {
        LOG_MSG_ERROR("Failed to open normal storage area!");
        return false;
    }

    /* Read gamecard header. */
    /* We don't use gamecardReadStorageArea() here because of its dependence on storage area sizes (which we haven't yet retrieved). */
    rc = fsStorageRead(&g_gameCardStorage, 0, &g_gameCardHeader, sizeof(GameCardHeader));
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("fsStorageRead failed to read gamecard header! (0x%X).", rc);
        return false;
    }

    LOG_DATA_DEBUG(&g_gameCardHeader, sizeof(GameCardHeader), "Gamecard header dump:");

    /* Check magic word from gamecard header. */
    if (__builtin_bswap32(g_gameCardHeader.magic) != GAMECARD_HEAD_MAGIC)
    {
        LOG_MSG_ERROR("Invalid gamecard header magic word! (0x%08X).", __builtin_bswap32(g_gameCardHeader.magic));
        return false;
    }

    /* Check if a Header2 area is available. */
    if (g_gameCardHeader.flags & GameCardFlags_HasCa10Certificate)
    {
        /* Read the Header2 area. */
        rc = fsStorageRead(&g_gameCardStorage, GAMECARD_HEADER2_OFFSET, &g_gameCardHeader2, sizeof(GameCardHeader2));
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("fsStorageRead failed to read gamecard Header2 area! (0x%X).", rc);
            return false;
        }

        LOG_DATA_DEBUG(&g_gameCardHeader2, sizeof(GameCardHeader2), "Gamecard Header2 dump:");

        /* Read the Header2Certificate area. */
        rc = fsStorageRead(&g_gameCardStorage, GAMECARD_HEADER2_CERT_OFFSET, &g_gameCardHeader2Cert, sizeof(GameCardHeader2Certificate));
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("fsStorageRead failed to read gamecard Header2Certificate area! (0x%X).", rc);
            return false;
        }

        LOG_DATA_DEBUG(&g_gameCardHeader2Cert, sizeof(GameCardHeader2Certificate), "Gamecard Header2Certificate dump:");

        /* Verify the signature from the Header2 area. */
        if (!rsa2048VerifySha256BasedPkcs1v15Signature(&(g_gameCardHeader2.unknown), sizeof(GameCardHeader2) - MEMBER_SIZE(GameCardHeader2, signature), g_gameCardHeader2.signature, \
                                                       g_gameCardHeader2Cert.modulus, g_gameCardHeader2Cert.exponent, sizeof(g_gameCardHeader2Cert.exponent)))
        {
            LOG_MSG_ERROR("Gamecard Header2 signature verification failed!");
            return false;
        }

        // TODO: remove this once anyone comes across a gamecard with an actual Header2 area.
        // Public non-static functions to retrieve both the Header2 and the Header2Certificate areas will be implemented afterwards.
        // For the time being, we will force an error.
        return false;
    }

    return true;
}

static bool _gamecardGetPlaintextCardInfoArea(void)
{
    const u8 *card_info_key = NULL;
    u8 card_info_iv[AES_128_KEY_SIZE] = {0};
    Aes128CbcContext aes_ctx = {0};

    /* Retrieve CardInfo area key. */
    card_info_key = keysGetGameCardInfoKey();
    if (!card_info_key)
    {
        LOG_MSG_ERROR("Failed to retrieve CardInfo area key!");
        return false;
    }

    /* Reverse CardInfo IV. */
    for(u8 i = 0; i < AES_128_KEY_SIZE; i++) card_info_iv[i] = g_gameCardHeader.card_info_iv[AES_128_KEY_SIZE - i - 1];

    /* Initialize AES-128-CBC context. */
    aes128CbcContextCreate(&aes_ctx, card_info_key, card_info_iv, false);

    /* Decrypt CardInfo area. */
    aes128CbcDecrypt(&aes_ctx, &g_gameCardInfoArea, &(g_gameCardHeader.card_info), sizeof(GameCardInfo));

    LOG_DATA_DEBUG(&g_gameCardInfoArea, sizeof(GameCardInfo), "Gamecard CardInfo area dump:");

    return true;
}

static bool gamecardReadSecurityInformation(GameCardSecurityInformation *out)
{
    if (!g_gameCardInterfaceInit || atomic_load(&g_gameCardStatus) != GameCardStatus_InsertedAndInfoLoaded || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Clear output. */
    memset(out, 0, sizeof(GameCardSecurityInformation));

    /* Open secure storage area. */
    if (!gamecardOpenStorageArea(GameCardStorageArea_Secure))
    {
        LOG_MSG_ERROR("Failed to open secure storage area!");
        return false;
    }

    bool found = false;
    u8 tmp_hash[SHA256_HASH_SIZE] = {0};

    /* Retrieve full FS program memory dump. */
    if (!memRetrieveFullProgramMemory(&g_fsProgramMemory))
    {
        LOG_MSG_ERROR("Failed to retrieve full FS program memory dump!");
        return false;
    }

    /* Look for the initial data block in the FS memory dump using the package ID and the initial data hash from the gamecard header. */
    for(u64 offset = 0; offset < g_fsProgramMemory.data_size; offset++)
    {
        if ((g_fsProgramMemory.data_size - offset) < sizeof(GameCardInitialData)) break;

        if (memcmp(g_fsProgramMemory.data + offset, g_gameCardHeader.package_id, sizeof(g_gameCardHeader.package_id)) != 0) continue;

        sha256CalculateHash(tmp_hash, g_fsProgramMemory.data + offset, sizeof(GameCardInitialData));

        if (!memcmp(tmp_hash, g_gameCardHeader.initial_data_hash, SHA256_HASH_SIZE))
        {
            /* Jackpot. */
            memcpy(out, g_fsProgramMemory.data + offset + sizeof(GameCardInitialData) - sizeof(GameCardSecurityInformation), sizeof(GameCardSecurityInformation));

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
    u8 status = atomic_load(&g_gameCardStatus);

    if (partition > 1 || (status < GameCardStatus_LotusAsicFirmwareUpdateRequired && status != GameCardStatus_Processing) || \
        (status == GameCardStatus_LotusAsicFirmwareUpdateRequired && partition == 1))
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
            LOG_MSG_DEBUG("fsDeviceOperatorGetGameCardHandle failed on try #%u! (0x%X).", i + 1, rc);
            continue;
        }

        /* If the previous call succeeded, let's try to open the desired gamecard storage area. */
        rc = fsOpenGameCardStorage(&g_gameCardStorage, &g_gameCardHandle, partition);
        if (R_FAILED(rc))
        {
            LOG_MSG_DEBUG("fsOpenGameCardStorage failed to open %s storage area on try #%u! (0x%X).", GAMECARD_STORAGE_AREA_NAME(partition + 1), i + 1, rc);
            continue;
        }

        /* If we got up to this point, both a valid gamecard handle and a valid storage area handle are guaranteed. */
        break;
    }

    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("fsDeviceOperatorGetGameCardHandle / fsOpenGameCardStorage failed! (0x%X).", rc);
        if (status == GameCardStatus_Processing && partition == 0) atomic_store(&g_gameCardStatus, GameCardStatus_NoGameCardPatchEnabled);
    }

    return R_SUCCEEDED(rc);
}

static bool gamecardOpenStorageArea(u8 area)
{
    u8 status = atomic_load(&g_gameCardStatus);

    if ((area != GameCardStorageArea_Normal && area != GameCardStorageArea_Secure) || (status < GameCardStatus_LotusAsicFirmwareUpdateRequired && \
        status != GameCardStatus_Processing) || (status == GameCardStatus_LotusAsicFirmwareUpdateRequired && area == GameCardStorageArea_Secure))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Return right away if a valid handle has already been retrieved and the desired gamecard storage area is currently open. */
    if (g_gameCardHandle.value && serviceIsActive(&(g_gameCardStorage.s)) && g_gameCardCurrentStorageArea == area) return true;

    /* Close both the gamecard handle and the open storage area. */
    gamecardCloseStorageArea();

    /* Retrieve both a new gamecard handle and a storage area handle. */
    if (!gamecardGetHandleAndStorage(area - 1)) /* Zero-based index. */
    {
        LOG_MSG_ERROR("Failed to retrieve gamecard handle and storage area handle! (%s).", GAMECARD_STORAGE_AREA_NAME(area));
        return false;
    }

    /* Update current gamecard storage area. */
    g_gameCardCurrentStorageArea = area;

    return true;
}

static bool gamecardReadStorageArea(void *out, u64 read_size, u64 offset)
{
    u8 status = atomic_load(&g_gameCardStatus);

    if ((status < GameCardStatus_LotusAsicFirmwareUpdateRequired && status != GameCardStatus_Processing) || !g_gameCardNormalAreaSize || !g_gameCardSecureAreaSize || \
        !out || !read_size || (offset + read_size) > g_gameCardTotalSize)
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
        LOG_MSG_ERROR("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
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
            LOG_MSG_ERROR("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%X) (aligned).", read_size, base_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
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
            LOG_MSG_ERROR("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%X) (unaligned).", chunk_size, block_start_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
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

    g_gameCardHandle.value = 0;

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
            LOG_MSG_ERROR("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
            return false;
        }

        rc = fsStorageGetSize(&g_gameCardStorage, (s64*)&area_size);

        gamecardCloseStorageArea();

        if (R_FAILED(rc) || !area_size)
        {
            LOG_MSG_ERROR("fsStorageGetSize failed to retrieve %s storage area size! (0x%X).", GAMECARD_STORAGE_AREA_NAME(area), rc);
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
    HashFileSystemContext *hfs_ctx = NULL;
    HashFileSystemHeader hfs_header = {0};
    u8 hfs_header_hash[SHA256_HASH_SIZE] = {0};

    bool success = false, dump_fs_header = false;

    if ((name && !*name) || offset < (GAMECARD_CERT_OFFSET + sizeof(FsGameCardCertificate)) || !IS_ALIGNED(offset, GAMECARD_PAGE_SIZE) || \
        (size && (!IS_ALIGNED(size, GAMECARD_PAGE_SIZE) || (offset + size) > g_gameCardTotalSize)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        goto end;
    }

    /* Allocate memory for the output context. */
    hfs_ctx = calloc(1, sizeof(HashFileSystemContext));
    if (!hfs_ctx)
    {
        LOG_MSG_ERROR("Unable to allocate memory for Hash FS context! (offset 0x%lX).", offset);
        goto end;
    }

    /* Duplicate partition name. */
    hfs_ctx->name = (name ? strdup(name) : strdup(hfsGetPartitionNameString(HashFileSystemPartitionType_Root)));
    if (!hfs_ctx->name)
    {
        LOG_MSG_ERROR("Failed to duplicate Hash FS partition name! (offset 0x%lX).", offset);
        goto end;
    }

    /* Determine Hash FS partition type. */
    for(i = HashFileSystemPartitionType_Root; i < HashFileSystemPartitionType_Count; i++)
    {
        const char *hfs_partition_name = hfsGetPartitionNameString((u8)i);
        if (hfs_partition_name && !strcmp(hfs_partition_name, hfs_ctx->name)) break;
    }

    if (i >= HashFileSystemPartitionType_Count)
    {
        LOG_MSG_ERROR("Failed to find a matching Hash FS partition type for \"%s\"! (offset 0x%lX).", hfs_ctx->name, offset);
        goto end;
    }

    hfs_ctx->type = i;

    /* Read partial Hash FS header. */
    if (!gamecardReadStorageArea(&hfs_header, sizeof(HashFileSystemHeader), offset))
    {
        LOG_MSG_ERROR("Failed to read partial Hash FS header! (\"%s\", offset 0x%lX).", hfs_ctx->name, offset);
        goto end;
    }

    magic = __builtin_bswap32(hfs_header.magic);
    if (magic != HFS0_MAGIC)
    {
        LOG_MSG_ERROR("Invalid Hash FS magic word! (0x%08X) (\"%s\", offset 0x%lX).", magic, hfs_ctx->name, offset);
        dump_fs_header = true;
        goto end;
    }

    /* Check Hash FS entry count and name table size. */
    /* Only allow a zero entry count if we're not dealing with the root partition. Never allow a zero-sized name table. */
    if ((!name && !hfs_header.entry_count) || !hfs_header.name_table_size)
    {
        LOG_MSG_ERROR("Invalid Hash FS entry count / name table size! (\"%s\", offset 0x%lX).", hfs_ctx->name, offset);
        dump_fs_header = true;
        goto end;
    }

    /* Calculate full Hash FS header size. */
    hfs_ctx->header_size = (sizeof(HashFileSystemHeader) + (hfs_header.entry_count * sizeof(HashFileSystemEntry)) + hfs_header.name_table_size);
    hfs_ctx->header_size = ALIGN_UP(hfs_ctx->header_size, GAMECARD_PAGE_SIZE);

    /* Allocate memory for the full Hash FS header. */
    /* If we're dealing with the root partition, we'll reserve an extra byte to accomodate for the compatibility type value. */
    /* This value is used as a salt when calculating the SHA-256 checksum. */
    u64 hfs_header_alloc_size = (name ? hfs_ctx->header_size : (hfs_ctx->header_size + 1));
    hfs_ctx->header = calloc(hfs_header_alloc_size, sizeof(u8));
    if (!hfs_ctx->header)
    {
        LOG_MSG_ERROR("Unable to allocate 0x%lX bytes buffer for the full Hash FS header! (\"%s\", offset 0x%lX).", hfs_header_alloc_size, hfs_ctx->name, offset);
        goto end;
    }

    /* Read full Hash FS header. */
    if (!gamecardReadStorageArea(hfs_ctx->header, hfs_ctx->header_size, offset))
    {
        LOG_MSG_ERROR("Failed to read full Hash FS header! (\"%s\", offset 0x%lX).", hfs_ctx->name, offset);
        goto end;
    }

    /* Verify Hash FS header (if possible). */
    if (hash && hash_target_size && (hash_target_offset + hash_target_size) <= hfs_ctx->header_size)
    {
        /* Add salt to header and update hash target size if we're dealing with the root partition + a compatibility type other than Normal. */
        u8 compatibility_type = g_gameCardInfoArea.compatibility_type;
        bool use_salt = (!name && compatibility_type != GameCardCompatibilityType_Normal);
        if (use_salt) hfs_ctx->header[hash_target_size++] = compatibility_type;

        sha256CalculateHash(hfs_header_hash, hfs_ctx->header + hash_target_offset, hash_target_size);

        if (memcmp(hfs_header_hash, hash, SHA256_HASH_SIZE) != 0)
        {
            LOG_MSG_ERROR("Hash FS header doesn't match expected SHA-256 hash! (\"%s\", offset 0x%lX).", hfs_ctx->name, offset);
            dump_fs_header = true;
            goto end;
        }

        /* Remove salt from header. */
        if (use_salt) hfs_ctx->header[--hash_target_size] = '\0';
    }

    /* Fill context. */
    hfs_ctx->offset = offset;

    if (name)
    {
        /* Use provided partition size. */
        hfs_ctx->size = size;
    } else {
        /* Calculate root partition size. */
        hfs_ctx->size = 1; // Prevents hfsGetEntryByIndex() from returning NULL.
        HashFileSystemEntry *hfs_entry = hfsGetEntryByIndex(hfs_ctx, hfs_header.entry_count - 1);
        hfs_ctx->size = (hfs_ctx->header_size + hfs_entry->offset + hfs_entry->size);
    }

    /* Update flag. */
    success = true;

end:
    if (!success && hfs_ctx)
    {
        if (dump_fs_header)
        {
            if (hfs_ctx->header)
            {
                LOG_DATA_DEBUG(hfs_ctx->header, hfs_ctx->header_size, "Hash FS header dump (\"%s\", offset 0x%lX):", hfs_ctx->name, offset);
            } else {
                LOG_DATA_DEBUG(&hfs_header, sizeof(HashFileSystemHeader), "Partial Hash FS header dump (\"%s\", offset 0x%lX):", hfs_ctx->name, offset);
            }
        }

        if (hfs_ctx->header) free(hfs_ctx->header);

        if (hfs_ctx->name) free(hfs_ctx->name);

        free(hfs_ctx);
        hfs_ctx = NULL;
    }

    return hfs_ctx;
}

static HashFileSystemContext *_gamecardGetHashFileSystemContext(u8 hfs_partition_type)
{
    HashFileSystemContext *hfs_ctx = NULL;

    if (!g_gameCardInterfaceInit || atomic_load(&g_gameCardStatus) != GameCardStatus_InsertedAndInfoLoaded || !g_gameCardHfsCount || !g_gameCardHfsCtx || \
        hfs_partition_type < HashFileSystemPartitionType_Root || hfs_partition_type >= HashFileSystemPartitionType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        goto end;
    }

    /* Return right away if the root partition was requested. */
    if (hfs_partition_type == HashFileSystemPartitionType_Root)
    {
        hfs_ctx = g_gameCardHfsCtx[0];
        goto end;
    }

    /* Try to find the requested partition by looping through our Hash FS contexts. */
    for(u32 i = 1; i < g_gameCardHfsCount; i++)
    {
        hfs_ctx = g_gameCardHfsCtx[i];
        if (hfs_ctx->type == hfs_partition_type) break;
        hfs_ctx = NULL;
    }

    if (!hfs_ctx) LOG_MSG_ERROR("Failed to locate Hash FS partition with type %u!", hfs_partition_type);

end:
    return hfs_ctx;
}
