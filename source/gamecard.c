/*
 * gamecard.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include "mem.h"
#include "gamecard.h"

#define GAMECARD_HFS0_MAGIC                     0x48465330              /* "HFS0". */

#define GAMECARD_READ_BUFFER_SIZE               0x800000                /* 8 MiB. */

#define GAMECARD_ACCESS_WAIT_TIME               3                       /* Seconds. */

#define GAMECARD_UNUSED_AREA_BLOCK_SIZE         0x24
#define GAMECARD_UNUSED_AREA_SIZE(x)            (((x) / GAMECARD_MEDIA_UNIT_SIZE) * GAMECARD_UNUSED_AREA_BLOCK_SIZE)

#define GAMECARD_STORAGE_AREA_NAME(x)           ((x) == GameCardStorageArea_Normal ? "normal" : ((x) == GameCardStorageArea_Secure ? "secure" : "none"))

#define GAMECARD_CAPACITY_1GiB                  (u64)0x40000000
#define GAMECARD_CAPACITY_2GiB                  (u64)0x80000000
#define GAMECARD_CAPACITY_4GiB                  (u64)0x100000000
#define GAMECARD_CAPACITY_8GiB                  (u64)0x200000000
#define GAMECARD_CAPACITY_16GiB                 (u64)0x400000000
#define GAMECARD_CAPACITY_32GiB                 (u64)0x800000000

/* Type definitions. */

/// Only kept for documentation purposes, not really used.
/// A copy of the gamecard header without the RSA-2048 signature and a plaintext GameCardHeaderEncryptedArea precedes this struct in FS program memory.
typedef struct {
    u32 memory_interface_mode;
    u32 asic_status;
    u8 card_id_area[0x48];
    u8 reserved[0x1B0];
    FsGameCardCertificate certificate;
    GameCardInitialData initial_data;
} GameCardSecurityInformation;

typedef struct {
    u32 magic;              ///< "HFS0".
    u32 entry_count;
    u32 name_table_size;
    u8 reserved[0x4];
} GameCardHashFileSystemHeader;

typedef struct {
    u64 offset;
    u64 size;
    u32 name_offset;
    u32 hash_target_size;
    u64 hash_target_offset;
    u8 hash[SHA256_HASH_SIZE];
} GameCardHashFileSystemEntry;

typedef enum {
    GameCardStorageArea_None   = 0,
    GameCardStorageArea_Normal = 1,
    GameCardStorageArea_Secure = 2
} GameCardStorageArea;

typedef struct {
    u64 offset;         ///< Relative to the start of the gamecard header.
    u64 size;           ///< Whole partition size.
    u64 header_size;    ///< Full header size.
    u8 *header;         ///< GameCardHashFileSystemHeader + (GameCardHashFileSystemEntry * entry_count) + Name Table.
} GameCardHashFileSystemPartitionInfo;

/* Global variables. */

static Mutex g_gamecardMutex = 0;
static bool g_gamecardInterfaceInit = false;

static FsDeviceOperator g_deviceOperator = {0};
static FsEventNotifier g_gameCardEventNotifier = {0};
static Event g_gameCardKernelEvent = {0};
static bool g_openDeviceOperator = false, g_openEventNotifier = false, g_loadKernelEvent = false;

static Thread g_gameCardDetectionThread = {0};
static UEvent g_gameCardDetectionThreadExitEvent = {0}, g_gameCardStatusChangeEvent = {0};
static bool g_gameCardDetectionThreadCreated = false, g_gameCardInserted = false, g_gameCardInfoLoaded = false;

static FsGameCardHandle g_gameCardHandle = {0};
static FsStorage g_gameCardStorage = {0};
static u8 g_gameCardStorageCurrentArea = GameCardStorageArea_None;
static u8 *g_gameCardReadBuf = NULL;

static GameCardHeader g_gameCardHeader = {0};
static u64 g_gameCardStorageNormalAreaSize = 0, g_gameCardStorageSecureAreaSize = 0;
static u64 g_gameCardCapacity = 0;

static u8 *g_gameCardHfsRootHeader = NULL;   /// GameCardHashFileSystemHeader + (entry_count * GameCardHashFileSystemEntry) + Name Table.
static GameCardHashFileSystemPartitionInfo *g_gameCardHfsPartitions = NULL;

static MemoryLocation g_fsProgramMemory = {
    .program_id = FS_SYSMODULE_TID,
    .mask = 0,
    .data = NULL,
    .data_size = 0
};

static const char *g_gameCardHfsPartitionNames[] = {
    [GameCardHashFileSystemPartitionType_Root]     = "root",
    [GameCardHashFileSystemPartitionType_Update]   = "update",
    [GameCardHashFileSystemPartitionType_Logo]     = "logo",
    [GameCardHashFileSystemPartitionType_Normal]   = "normal",
    [GameCardHashFileSystemPartitionType_Secure]   = "secure",
    [GameCardHashFileSystemPartitionType_Boot]     = "boot"
};

/* Function prototypes. */

static bool gamecardCreateDetectionThread(void);
static void gamecardDestroyDetectionThread(void);
static void gamecardDetectionThreadFunc(void *arg);

NX_INLINE bool gamecardIsInserted(void);

static void gamecardLoadInfo(void);
static void gamecardFreeInfo(void);

static bool gamecardReadInitialData(GameCardKeyArea *out);

static bool gamecardGetHandleAndStorage(u32 partition);
NX_INLINE void gamecardCloseHandle(void);

static bool gamecardOpenStorageArea(u8 area);
static bool gamecardReadStorageArea(void *out, u64 read_size, u64 offset, bool lock);
static void gamecardCloseStorageArea(void);

static bool gamecardGetStorageAreasSizes(void);
NX_INLINE u64 gamecardGetCapacityFromRomSizeValue(u8 rom_size);

static GameCardHashFileSystemHeader *gamecardGetHashFileSystemPartitionHeader(u8 hfs_partition_type, u32 *out_hfs_partition_idx);

NX_INLINE GameCardHashFileSystemEntry *gamecardGetHashFileSystemEntryByIndex(void *header, u32 idx);
NX_INLINE char *gamecardGetHashFileSystemNameTable(void *header);
NX_INLINE char *gamecardGetHashFileSystemEntryNameByIndex(void *header, u32 idx);
static bool gamecardGetHashFileSystemEntryIndexByName(void *header, const char *name, u32 *out_idx);

bool gamecardInitialize(void)
{
    mutexLock(&g_gamecardMutex);
    
    Result rc = 0;
    
    bool ret = g_gamecardInterfaceInit;
    if (ret) goto end;
    
    /* Allocate memory for the gamecard read buffer. */
    g_gameCardReadBuf = malloc(GAMECARD_READ_BUFFER_SIZE);
    if (!g_gameCardReadBuf)
    {
        LOGFILE("Unable to allocate memory for the gamecard read buffer!");
        goto end;
    }
    
    /* Open device operator. */
    rc = fsOpenDeviceOperator(&g_deviceOperator);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenDeviceOperator failed! (0x%08X).", rc);
        goto end;
    }
    
    g_openDeviceOperator = true;
    
    /* Open gamecard detection event notifier. */
    rc = fsOpenGameCardDetectionEventNotifier(&g_gameCardEventNotifier);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenGameCardDetectionEventNotifier failed! (0x%08X)", rc);
        goto end;
    }
    
    g_openEventNotifier = true;
    
    /* Retrieve gamecard detection kernel event. */
    rc = fsEventNotifierGetEventHandle(&g_gameCardEventNotifier, &g_gameCardKernelEvent, true);
    if (R_FAILED(rc))
    {
        LOGFILE("fsEventNotifierGetEventHandle failed! (0x%08X)", rc);
        goto end;
    }
    
    g_loadKernelEvent = true;
    
    /* Create usermode exit event. */
    ueventCreate(&g_gameCardDetectionThreadExitEvent, true);
    
    /* Create usermode gamecard status change event. */
    ueventCreate(&g_gameCardStatusChangeEvent, true);
    
    /* Create gamecard detection thread. */
    if (!(g_gameCardDetectionThreadCreated = gamecardCreateDetectionThread())) goto end;
    
    ret = g_gamecardInterfaceInit = true;
    
end:
    mutexUnlock(&g_gamecardMutex);
    
    return ret;
}

void gamecardExit(void)
{
    mutexLock(&g_gamecardMutex);
    
    /* Destroy gamecard detection thread. */
    if (g_gameCardDetectionThreadCreated)
    {
        gamecardDestroyDetectionThread();
        g_gameCardDetectionThreadCreated = false;
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
    
    g_gamecardInterfaceInit = false;
    
    mutexUnlock(&g_gamecardMutex);
}

UEvent *gamecardGetStatusChangeUserEvent(void)
{
    mutexLock(&g_gamecardMutex);
    UEvent *event = (g_gamecardInterfaceInit ? &g_gameCardStatusChangeEvent : NULL);
    mutexUnlock(&g_gamecardMutex);
    return event;
}

u8 gamecardGetStatus(void)
{
    mutexLock(&g_gamecardMutex);
    u8 status = (g_gameCardInserted ? (g_gameCardInfoLoaded ? GameCardStatus_InsertedAndInfoLoaded : GameCardStatus_InsertedAndInfoNotLoaded) : GameCardStatus_NotInserted);
    mutexUnlock(&g_gamecardMutex);
    return status;
}

bool gamecardReadStorage(void *out, u64 read_size, u64 offset)
{
    return gamecardReadStorageArea(out, read_size, offset, true);
}

bool gamecardGetKeyArea(GameCardKeyArea *out)
{
    /* Read full FS program memory to retrieve the GameCardInitialData block, which is part of the GameCardKeyArea block. */
    /* In FS program memory, this is stored as part of the GameCardSecurityInformation struct, which is returned by Lotus command "ChangeToSecureMode" (0xF). */
    /* This means it is only available *after* the gamecard secure area has been mounted, which is taken care of in gamecardReadInitialData(). */
    /* The GameCardSecurityInformation struct is only kept for documentation purposes. It isn't used at all to retrieve the GameCardInitialData block. */
    mutexLock(&g_gamecardMutex);
    bool ret = gamecardReadInitialData(out);
    mutexUnlock(&g_gamecardMutex);
    return ret;
}

bool gamecardGetHeader(GameCardHeader *out)
{
    mutexLock(&g_gamecardMutex);
    bool ret = (g_gameCardInserted && g_gameCardInfoLoaded && out);
    if (ret) memcpy(out, &g_gameCardHeader, sizeof(GameCardHeader));
    mutexUnlock(&g_gamecardMutex);
    return ret;
}

bool gamecardGetCertificate(FsGameCardCertificate *out)
{
    Result rc = 0;
    bool ret = false;
    
    mutexLock(&g_gamecardMutex);
    if (g_gameCardInserted && g_gameCardHandle.value && out)
    {
        rc = fsDeviceOperatorGetGameCardDeviceCertificate(&g_deviceOperator, &g_gameCardHandle, out);
        if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorGetGameCardDeviceCertificate failed! (0x%08X)", rc);
        ret = R_SUCCEEDED(rc);
    }
    mutexUnlock(&g_gamecardMutex);
    
    return ret;
}

bool gamecardGetTotalSize(u64 *out)
{
    mutexLock(&g_gamecardMutex);
    bool ret = (g_gameCardInserted && g_gameCardInfoLoaded && out);
    if (ret) *out = (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize);
    mutexUnlock(&g_gamecardMutex);
    return ret;
}

bool gamecardGetTrimmedSize(u64 *out)
{
    mutexLock(&g_gamecardMutex);
    bool ret = (g_gameCardInserted && g_gameCardInfoLoaded && out);
    if (ret) *out = (sizeof(GameCardHeader) + GAMECARD_MEDIA_UNIT_OFFSET(g_gameCardHeader.valid_data_end_address));
    mutexUnlock(&g_gamecardMutex);
    return ret;
}

bool gamecardGetRomCapacity(u64 *out)
{
    mutexLock(&g_gamecardMutex);
    bool ret = (g_gameCardInserted && g_gameCardInfoLoaded && out);
    if (ret) *out = g_gameCardCapacity;
    mutexUnlock(&g_gamecardMutex);
    return ret;
}

bool gamecardGetBundledFirmwareUpdateVersion(u32 *out)
{
    Result rc = 0;
    u64 update_id = 0;
    u32 update_version = 0;
    bool ret = false;
    
    mutexLock(&g_gamecardMutex);
    if (g_gameCardInserted && g_gameCardHandle.value && out)
    {
        rc = fsDeviceOperatorUpdatePartitionInfo(&g_deviceOperator, &g_gameCardHandle, &update_version, &update_id);
        if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorUpdatePartitionInfo failed! (0x%08X)", rc);
        
        ret = (R_SUCCEEDED(rc) && update_id == GAMECARD_UPDATE_TID);
        if (ret) *out = update_version;
    }
    mutexUnlock(&g_gamecardMutex);
    
    return ret;
}

const char *gamecardGetHashFileSystemPartitionName(u8 hfs_partition_type)
{
    return (hfs_partition_type < GameCardHashFileSystemPartitionType_Count ? g_gameCardHfsPartitionNames[hfs_partition_type] : NULL);
}

bool gamecardGetEntryCountFromHashFileSystemPartition(u8 hfs_partition_type, u32 *out_count)
{
    bool ret = false;
    GameCardHashFileSystemHeader *fs_header = NULL;
    
    mutexLock(&g_gamecardMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out_count)
    {
        fs_header = gamecardGetHashFileSystemPartitionHeader(hfs_partition_type, NULL);
        if (fs_header)
        {
            *out_count = fs_header->entry_count;
            ret = true;
        } else {
            LOGFILE("Failed to retrieve hash FS partition header!");
        }
    }
    mutexUnlock(&g_gamecardMutex);
    
    return ret;
}

bool gamecardGetEntryInfoFromHashFileSystemPartitionByIndex(u8 hfs_partition_type, u32 idx, u64 *out_offset, u64 *out_size, char **out_name)
{
    bool ret = false;
    char *entry_name = NULL;
    u32 hfs_partition_idx = 0;
    GameCardHashFileSystemHeader *fs_header = NULL;
    GameCardHashFileSystemEntry *fs_entry = NULL;
    
    mutexLock(&g_gamecardMutex);
    
    if (g_gameCardInserted && g_gameCardInfoLoaded && (out_offset || out_size || out_name))
    {
        fs_header = gamecardGetHashFileSystemPartitionHeader(hfs_partition_type, &hfs_partition_idx);
        if (!fs_header)
        {
            LOGFILE("Failed to retrieve hash FS partition header!");
            goto end;
        }
        
        fs_entry = gamecardGetHashFileSystemEntryByIndex(fs_header, idx);
        if (!fs_entry)
        {
            LOGFILE("Failed to retrieve hash FS partition entry by index!");
            goto end;
        }
        
        if (out_offset)
        {
            if (hfs_partition_type == GameCardHashFileSystemPartitionType_Root)
            {
                *out_offset = g_gameCardHfsPartitions[idx].offset;  /* No need to recalculate what we already have. */
            } else {
                *out_offset = (g_gameCardHfsPartitions[hfs_partition_idx].offset + g_gameCardHfsPartitions[hfs_partition_idx].header_size + fs_entry->offset);
            }
        }
        
        if (out_size) *out_size = fs_entry->size;
        
        if (out_name)
        {
            entry_name = gamecardGetHashFileSystemEntryNameByIndex(fs_header, idx);
            if (!entry_name || !*entry_name)
            {
                LOGFILE("Invalid hash FS partition entry name!");
                goto end;
            }
            
            *out_name = strdup(entry_name);
            if (!*out_name)
            {
                LOGFILE("Failed to duplicate hash FS partition entry name!");
                goto end;
            }
        }
        
        ret = true;
    }
    
end:
    mutexUnlock(&g_gamecardMutex);
    
    return ret;
}

bool gamecardGetEntryInfoFromHashFileSystemPartitionByName(u8 hfs_partition_type, const char *name, u64 *out_offset, u64 *out_size)
{
    bool ret = false;
    u32 hfs_partition_idx = 0, fs_entry_idx = 0;
    GameCardHashFileSystemHeader *fs_header = NULL;
    GameCardHashFileSystemEntry *fs_entry = NULL;
    
    mutexLock(&g_gamecardMutex);
    
    if (g_gameCardInserted && g_gameCardInfoLoaded && (out_offset || out_size))
    {
        fs_header = gamecardGetHashFileSystemPartitionHeader(hfs_partition_type, &hfs_partition_idx);
        if (!fs_header)
        {
            LOGFILE("Failed to retrieve hash FS partition header!");
            goto end;
        }
        
        if (!gamecardGetHashFileSystemEntryIndexByName(fs_header, name, &fs_entry_idx))
        {
            LOGFILE("Failed to retrieve hash FS partition entry index by name!");
            goto end;
        }
        
        fs_entry = gamecardGetHashFileSystemEntryByIndex(fs_header, fs_entry_idx);
        if (!fs_entry)
        {
            LOGFILE("Failed to retrieve hash FS partition entry by index!");
            goto end;
        }
        
        if (out_offset)
        {
            if (hfs_partition_type == GameCardHashFileSystemPartitionType_Root)
            {
                *out_offset = g_gameCardHfsPartitions[fs_entry_idx].offset;  /* No need to recalculate what we already have. */
            } else {
                *out_offset = (g_gameCardHfsPartitions[hfs_partition_idx].offset + g_gameCardHfsPartitions[hfs_partition_idx].header_size + fs_entry->offset);
            }
        }
        
        if (out_size) *out_size = fs_entry->size;
        
        ret = true;
    }
    
end:
    mutexUnlock(&g_gamecardMutex);
    
    return ret;
}

static bool gamecardCreateDetectionThread(void)
{
    if (!utilsCreateThread(&g_gameCardDetectionThread, gamecardDetectionThreadFunc, NULL, 1))
    {
        LOGFILE("Failed to create gamecard detection thread!");
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
    mutexLock(&g_gamecardMutex);
    g_gameCardInserted = gamecardIsInserted();
    if (g_gameCardInserted) gamecardLoadInfo();
    mutexUnlock(&g_gamecardMutex);
    
    ueventSignal(&g_gameCardStatusChangeEvent);
    
    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, gamecard_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;
        
        /* Exit event triggered. */
        if (idx == 1) break;
        
        mutexLock(&g_gamecardMutex);
        
        /* Retrieve current gamecard insertion status. */
        /* Only proceed if we're dealing with a status change. */
        g_gameCardInserted = gamecardIsInserted();
        gamecardFreeInfo();
        
        if (g_gameCardInserted)
        {
            /* Don't access the gamecard immediately to avoid conflicts with HOS / sysmodules. */
            utilsSleep(GAMECARD_ACCESS_WAIT_TIME);
            
            /* Load gamecard info. */
            gamecardLoadInfo();
        }
        
        mutexUnlock(&g_gamecardMutex);
        
        /* Signal user mode gamecard status change event. */
        ueventSignal(&g_gameCardStatusChangeEvent);
    }
    
    /* Free gamecard info and close gamecard handle. */
    gamecardFreeInfo();
    g_gameCardInserted = false;
    
    threadExit();
}

NX_INLINE bool gamecardIsInserted(void)
{
    bool inserted = false;
    Result rc = fsDeviceOperatorIsGameCardInserted(&g_deviceOperator, &inserted);
    if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorIsGameCardInserted failed! (0x%08X)", rc);
    return (R_SUCCEEDED(rc) && inserted);
}

static void gamecardLoadInfo(void)
{
    if (g_gameCardInfoLoaded) return;
    
    GameCardHashFileSystemHeader *fs_header = NULL;
    GameCardHashFileSystemEntry *fs_entry = NULL;
    
    /* Retrieve gamecard storage area sizes. */
    /* gamecardReadStorageArea() actually checks if the storage area sizes are greater than zero, so we must first perform this step. */
    if (!gamecardGetStorageAreasSizes())
    {
        LOGFILE("Failed to retrieve gamecard storage area sizes!");
        goto end;
    }
    
    /* Read gamecard header. */
    if (!gamecardReadStorageArea(&g_gameCardHeader, sizeof(GameCardHeader), 0, false))
    {
        LOGFILE("Failed to read gamecard header!");
        goto end;
    }
    
    /* Check magic word from gamecard header. */
    if (__builtin_bswap32(g_gameCardHeader.magic) != GAMECARD_HEAD_MAGIC)
    {
        LOGFILE("Invalid gamecard header magic word! (0x%08X)", __builtin_bswap32(g_gameCardHeader.magic));
        goto end;
    }
    
    /* Get gamecard capacity. */
    g_gameCardCapacity = gamecardGetCapacityFromRomSizeValue(g_gameCardHeader.rom_size);
    if (!g_gameCardCapacity)
    {
        LOGFILE("Invalid gamecard capacity value! (0x%02X).", g_gameCardHeader.rom_size);
        goto end;
    }
    
    if (utilsGetCustomFirmwareType() == UtilsCustomFirmwareType_SXOS)
    {
        /* The total size for the secure storage area is maxed out under SX OS. */
        /* Let's try to calculate it manually. */
        g_gameCardStorageSecureAreaSize = (g_gameCardCapacity - (g_gameCardStorageNormalAreaSize + GAMECARD_UNUSED_AREA_SIZE(g_gameCardCapacity)));
    }
    
    /* Allocate memory for the root hash FS header. */
    g_gameCardHfsRootHeader = calloc(g_gameCardHeader.partition_fs_header_size, sizeof(u8));
    if (!g_gameCardHfsRootHeader)
    {
        LOGFILE("Unable to allocate memory for the root hash FS header!");
        goto end;
    }
    
    /* Read root hash FS header. */
    if (!gamecardReadStorageArea(g_gameCardHfsRootHeader, g_gameCardHeader.partition_fs_header_size, g_gameCardHeader.partition_fs_header_address, false))
    {
        LOGFILE("Failed to read root hash FS header from offset 0x%lX!", g_gameCardHeader.partition_fs_header_address);
        goto end;
    }
    
    fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsRootHeader;
    
    if (__builtin_bswap32(fs_header->magic) != GAMECARD_HFS0_MAGIC)
    {
        LOGFILE("Invalid magic word in root hash FS header! (0x%08X).", __builtin_bswap32(fs_header->magic));
        goto end;
    }
    
    if (!fs_header->entry_count || !fs_header->name_table_size || \
        (sizeof(GameCardHashFileSystemHeader) + (fs_header->entry_count * sizeof(GameCardHashFileSystemEntry)) + fs_header->name_table_size) > g_gameCardHeader.partition_fs_header_size)
    {
        LOGFILE("Invalid file count and/or name table size in root hash FS header!");
        goto end;
    }
    
    /* Allocate memory for the hash FS partitions info. */
    g_gameCardHfsPartitions = calloc(fs_header->entry_count, sizeof(GameCardHashFileSystemEntry));
    if (!g_gameCardHfsPartitions)
    {
        LOGFILE("Unable to allocate memory for the hash FS partitions info!");
        goto end;
    }
    
    /* Read hash FS partitions. */
    for(u32 i = 0; i < fs_header->entry_count; i++)
    {
        GameCardHashFileSystemPartitionInfo *hfs_partition = &(g_gameCardHfsPartitions[i]);
        
        fs_entry = gamecardGetHashFileSystemEntryByIndex(g_gameCardHfsRootHeader, i);
        if (!fs_entry || !fs_entry->size)
        {
            LOGFILE("Invalid hash FS partition entry!");
            goto end;
        }
        
        hfs_partition->offset = (g_gameCardHeader.partition_fs_header_address + g_gameCardHeader.partition_fs_header_size + fs_entry->offset);
        hfs_partition->size = fs_entry->size;
        
        /* Partially read the current hash FS partition header. */
        GameCardHashFileSystemHeader partition_header = {0};
        if (!gamecardReadStorageArea(&partition_header, sizeof(GameCardHashFileSystemHeader), hfs_partition->offset, false))
        {
            LOGFILE("Failed to partially read hash FS partition #%u header from offset 0x%lX!", i, hfs_partition->offset);
            goto end;
        }
        
        if (__builtin_bswap32(partition_header.magic) != GAMECARD_HFS0_MAGIC)
        {
            LOGFILE("Invalid magic word in hash FS partition #%u header! (0x%08X).", i, __builtin_bswap32(partition_header.magic));
            goto end;
        }
        
        if (!partition_header.name_table_size)
        {
            LOGFILE("Invalid name table size in hash FS partition #%u header!", i);
            goto end;
        }
        
        /* Calculate the full header size for the current hash FS partition and round it to a GAMECARD_MEDIA_UNIT_SIZE bytes boundary. */
        hfs_partition->header_size = (sizeof(GameCardHashFileSystemHeader) + (partition_header.entry_count * sizeof(GameCardHashFileSystemEntry)) + partition_header.name_table_size);
        hfs_partition->header_size = ALIGN_UP(hfs_partition->header_size, GAMECARD_MEDIA_UNIT_SIZE);
        
        /* Allocate memory for the hash FS partition header. */
        hfs_partition->header = calloc(hfs_partition->header_size, sizeof(u8));
        if (!hfs_partition->header)
        {
            LOGFILE("Unable to allocate memory for the hash FS partition #%u header!", i);
            goto end;
        }
        
        /* Finally, read the full hash FS partition header. */
        if (!gamecardReadStorageArea(hfs_partition->header, hfs_partition->header_size, hfs_partition->offset, false))
        {
            LOGFILE("Failed to read full hash FS partition #%u header from offset 0x%lX!", i, hfs_partition->offset);
            goto end;
        }
    }
    
    g_gameCardInfoLoaded = true;
    
end:
    if (!g_gameCardInfoLoaded) gamecardFreeInfo();
}

static void gamecardFreeInfo(void)
{
    memset(&g_gameCardHeader, 0, sizeof(GameCardHeader));
    
    g_gameCardStorageNormalAreaSize = 0;
    g_gameCardStorageSecureAreaSize = 0;
    
    g_gameCardCapacity = 0;
    
    if (g_gameCardHfsRootHeader)
    {
        if (g_gameCardHfsPartitions)
        {
            GameCardHashFileSystemHeader *fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsRootHeader;
            
            for(u32 i = 0; i < fs_header->entry_count; i++)
            {
                if (g_gameCardHfsPartitions[i].header) free(g_gameCardHfsPartitions[i].header);
            }
        }
        
        free(g_gameCardHfsRootHeader);
        g_gameCardHfsRootHeader = NULL;
    }
    
    if (g_gameCardHfsPartitions)
    {
        free(g_gameCardHfsPartitions);
        g_gameCardHfsPartitions = NULL;
    }
    
    gamecardCloseStorageArea();
    
    g_gameCardInfoLoaded = false;
}

static bool gamecardReadInitialData(GameCardKeyArea *out)
{
    if (!g_gameCardInserted || !g_gameCardInfoLoaded || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Clear output. */
    memset(out, 0, sizeof(GameCardKeyArea));
    
    /* Open secure storage area. */
    if (!gamecardOpenStorageArea(GameCardStorageArea_Secure))
    {
        LOGFILE("Failed to open secure storage area!");
        return false;
    }
    
    bool found = false;
    u8 tmp_hash[SHA256_HASH_SIZE] = {0};
    
    /* Retrieve full FS program memory dump. */
    if (!memRetrieveFullProgramMemory(&g_fsProgramMemory))
    {
        LOGFILE("Failed to retrieve full FS program memory dump!");
        return false;
    }
    
    /* Look for the initial data block in the FS memory dump using the package ID and the initial data hash from the gamecard header. */
    for(u64 offset = 0; offset < g_fsProgramMemory.data_size; offset++)
    {
        if (memcmp(g_fsProgramMemory.data + offset, &(g_gameCardHeader.package_id), sizeof(g_gameCardHeader.package_id)) != 0) continue;
        
        sha256CalculateHash(tmp_hash, g_fsProgramMemory.data + offset, sizeof(GameCardInitialData));
        
        if (!memcmp(tmp_hash, g_gameCardHeader.initial_data_hash, SHA256_HASH_SIZE))
        {
            /* Jackpot. */
            memcpy(&(out->initial_data), g_fsProgramMemory.data + offset, sizeof(GameCardInitialData));
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
    if (!g_gameCardInserted || partition > 1)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    
    /* 10 tries. */
    for(u8 i = 0; i < 10; i++)
    {
        /* 100 ms wait in case there was an error in the previous loop. */
        if (R_FAILED(rc)) svcSleepThread(100000000);
        
        /* First, let's try to retrieve a gamecard handle. */
        /* This can return 0x140A02 if the "nogc" patch is enabled by the running CFW. */
        rc = fsDeviceOperatorGetGameCardHandle(&g_deviceOperator, &g_gameCardHandle);
        if (R_FAILED(rc))
        {
            //LOGFILE("fsDeviceOperatorGetGameCardHandle failed on try #%u! (0x%08X).", i + 1, rc);
            continue;
        }
        
        /* If the previous call succeeded, let's try to open the desired gamecard storage area. */
        rc = fsOpenGameCardStorage(&g_gameCardStorage, &g_gameCardHandle, partition);
        if (R_FAILED(rc))
        {
            gamecardCloseHandle(); /* Close invalid gamecard handle. */
            //LOGFILE("fsOpenGameCardStorage failed to open %s storage area on try #%u! (0x%08X).", GAMECARD_STORAGE_AREA_NAME(partition + 1), i + 1, rc);
            continue;
        }
        
        /* If we got up to this point, both a valid gamecard handle and a valid storage area handle are guaranteed. */
        break;
    }
    
    if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorGetGameCardHandle / fsOpenGameCardStorage failed! (0x%08X).", rc);
    
    return R_SUCCEEDED(rc);
}

NX_INLINE void gamecardCloseHandle(void)
{
    /* I need to find a way to properly close a gamecard handle... */
    if (!g_gameCardHandle.value) return;
    svcCloseHandle(g_gameCardHandle.value);
    g_gameCardHandle.value = 0;
}

static bool gamecardOpenStorageArea(u8 area)
{
    if (!g_gameCardInserted || (area != GameCardStorageArea_Normal && area != GameCardStorageArea_Secure))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Return right away if a valid handle has already been retrieved and the desired gamecard storage area is currently open. */
    if (g_gameCardHandle.value && serviceIsActive(&(g_gameCardStorage.s)) && g_gameCardStorageCurrentArea == area) return true;
    
    /* Close both gamecard handle and open storage area. */
    gamecardCloseStorageArea();
    
    /* Retrieve both a new gamecard handle and a storage area handle. */
    if (!gamecardGetHandleAndStorage(area - 1)) /* Zero-based index. */
    {
        LOGFILE("Failed to retrieve gamecard handle and storage area handle!");
        return false;
    }
    
    /* Update current gamecard storage area. */
    g_gameCardStorageCurrentArea = area;
    
    return true;
}

static bool gamecardReadStorageArea(void *out, u64 read_size, u64 offset, bool lock)
{
    if (lock) mutexLock(&g_gamecardMutex);
    
    bool success = false;
    
    if (!g_gameCardInserted || !g_gameCardStorageNormalAreaSize || !g_gameCardStorageSecureAreaSize || !out || !read_size || \
        offset >= (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize) || (offset + read_size) > (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize))
    {
        LOGFILE("Invalid parameters!");
        goto end;
    }
    
    Result rc = 0;
    u8 *out_u8 = (u8*)out;
    u8 area = (offset < g_gameCardStorageNormalAreaSize ? GameCardStorageArea_Normal : GameCardStorageArea_Secure);
    
    /* Handle reads that span both the normal and secure gamecard storage areas. */
    if (area == GameCardStorageArea_Normal && (offset + read_size) > g_gameCardStorageNormalAreaSize)
    {
        /* Calculate normal storage area size difference. */
        u64 diff_size = (g_gameCardStorageNormalAreaSize - offset);
        
        if (!gamecardReadStorageArea(out_u8, diff_size, offset, false)) goto end;
        
        /* Adjust variables to read right from the start of the secure storage area. */
        read_size -= diff_size;
        offset = g_gameCardStorageNormalAreaSize;
        out_u8 += diff_size;
        area = GameCardStorageArea_Secure;
    }
    
    /* Open a storage area if needed. */
    /* If the right storage area has already been opened, this will return true. */
    if (!gamecardOpenStorageArea(area))
    {
        LOGFILE("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
        goto end;
    }
    
    /* Calculate appropiate storage area offset and retrieve the right storage area pointer. */
    u64 base_offset = (area == GameCardStorageArea_Normal ? offset : (offset - g_gameCardStorageNormalAreaSize));
    
    if (!(base_offset % GAMECARD_MEDIA_UNIT_SIZE) && !(read_size % GAMECARD_MEDIA_UNIT_SIZE))
    {
        /* Optimization for reads that are already aligned to a GAMECARD_MEDIA_UNIT_SIZE boundary. */
        rc = fsStorageRead(&g_gameCardStorage, base_offset, out_u8, read_size);
        if (R_FAILED(rc))
        {
            LOGFILE("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (aligned).", read_size, base_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
            goto end;
        }
        
        success = true;
    } else {
        /* Fix offset and/or size to avoid unaligned reads. */
        u64 block_start_offset = ALIGN_DOWN(base_offset, GAMECARD_MEDIA_UNIT_SIZE);
        u64 block_end_offset = ALIGN_UP(base_offset + read_size, GAMECARD_MEDIA_UNIT_SIZE);
        u64 block_size = (block_end_offset - block_start_offset);
        
        u64 data_start_offset = (base_offset - block_start_offset);
        u64 chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? GAMECARD_READ_BUFFER_SIZE : block_size);
        u64 out_chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? (GAMECARD_READ_BUFFER_SIZE - data_start_offset) : read_size);
        
        rc = fsStorageRead(&g_gameCardStorage, block_start_offset, g_gameCardReadBuf, chunk_size);
        if (R_FAILED(rc))
        {
            LOGFILE("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (unaligned).", chunk_size, block_start_offset, GAMECARD_STORAGE_AREA_NAME(area), rc);
            goto end;
        }
        
        memcpy(out_u8, g_gameCardReadBuf + data_start_offset, out_chunk_size);
        
        success = (block_size > GAMECARD_READ_BUFFER_SIZE ? gamecardReadStorageArea(out_u8 + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size, false) : true);
    }
    
end:
    if (lock) mutexUnlock(&g_gamecardMutex);
    
    return success;
}

static void gamecardCloseStorageArea(void)
{
    if (serviceIsActive(&(g_gameCardStorage.s)))
    {
        fsStorageClose(&g_gameCardStorage);
        memset(&g_gameCardStorage, 0, sizeof(FsStorage));
    }
    
    gamecardCloseHandle();
    
    g_gameCardStorageCurrentArea = GameCardStorageArea_None;
}

static bool gamecardGetStorageAreasSizes(void)
{
    if (!g_gameCardInserted)
    {
        LOGFILE("Gamecard not inserted!");
        return false;
    }
    
    for(u8 i = 0; i < 2; i++)
    {
        Result rc = 0;
        u64 area_size = 0;
        u8 area = (i == 0 ? GameCardStorageArea_Normal : GameCardStorageArea_Secure);
        
        if (!gamecardOpenStorageArea(area))
        {
            LOGFILE("Failed to open %s storage area!", GAMECARD_STORAGE_AREA_NAME(area));
            return false;
        }
        
        rc = fsStorageGetSize(&g_gameCardStorage, (s64*)&area_size);
        
        gamecardCloseStorageArea();
        
        if (R_FAILED(rc) || !area_size)
        {
            LOGFILE("fsStorageGetSize failed to retrieve %s storage area size! (0x%08X).", GAMECARD_STORAGE_AREA_NAME(area), rc);
            g_gameCardStorageNormalAreaSize = g_gameCardStorageSecureAreaSize = 0;
            return false;
        }
        
        if (area == GameCardStorageArea_Normal)
        {
            g_gameCardStorageNormalAreaSize = area_size;
        } else {
            g_gameCardStorageSecureAreaSize = area_size;
        }
    }
    
    return true;
}

NX_INLINE u64 gamecardGetCapacityFromRomSizeValue(u8 rom_size)
{
    u64 capacity = 0;
    
    switch(rom_size)
    {
        case GameCardRomSize_1GiB:
            capacity = GAMECARD_CAPACITY_1GiB;
            break;
        case GameCardRomSize_2GiB:
            capacity = GAMECARD_CAPACITY_2GiB;
            break;
        case GameCardRomSize_4GiB:
            capacity = GAMECARD_CAPACITY_4GiB;
            break;
        case GameCardRomSize_8GiB:
            capacity = GAMECARD_CAPACITY_8GiB;
            break;
        case GameCardRomSize_16GiB:
            capacity = GAMECARD_CAPACITY_16GiB;
            break;
        case GameCardRomSize_32GiB:
            capacity = GAMECARD_CAPACITY_32GiB;
            break;
        default:
            break;
    }
    
    return capacity;
}

static GameCardHashFileSystemHeader *gamecardGetHashFileSystemPartitionHeader(u8 hfs_partition_type, u32 *out_hfs_partition_idx)
{
    if (hfs_partition_type > GameCardHashFileSystemPartitionType_Secure) return NULL;
    
    u32 hfs_partition_idx = 0;
    GameCardHashFileSystemHeader *fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsRootHeader;
    
    if (hfs_partition_type != GameCardHashFileSystemPartitionType_Root)
    {
        if (gamecardGetHashFileSystemEntryIndexByName(fs_header, gamecardGetHashFileSystemPartitionName(hfs_partition_type), &hfs_partition_idx))
        {
            fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsPartitions[hfs_partition_idx].header;
            if (out_hfs_partition_idx) *out_hfs_partition_idx = hfs_partition_idx;
        } else {
            fs_header = NULL;
        }
    }
    
    return fs_header;
}

NX_INLINE GameCardHashFileSystemEntry *gamecardGetHashFileSystemEntryByIndex(void *header, u32 idx)
{
    if (!header || idx >= ((GameCardHashFileSystemHeader*)header)->entry_count) return NULL;
    return (GameCardHashFileSystemEntry*)((u8*)header + sizeof(GameCardHashFileSystemHeader) + (idx * sizeof(GameCardHashFileSystemEntry)));
}

NX_INLINE char *gamecardGetHashFileSystemNameTable(void *header)
{
    GameCardHashFileSystemHeader *fs_header = (GameCardHashFileSystemHeader*)header;
    if (!fs_header || !fs_header->entry_count) return NULL;
    return ((char*)header + sizeof(GameCardHashFileSystemHeader) + (fs_header->entry_count * sizeof(GameCardHashFileSystemEntry)));
}

NX_INLINE char *gamecardGetHashFileSystemEntryNameByIndex(void *header, u32 idx)
{
    GameCardHashFileSystemEntry *fs_entry = gamecardGetHashFileSystemEntryByIndex(header, idx);
    char *name_table = gamecardGetHashFileSystemNameTable(header);
    if (!fs_entry || !name_table) return NULL;
    return (name_table + fs_entry->name_offset);
}

static bool gamecardGetHashFileSystemEntryIndexByName(void *header, const char *name, u32 *out_idx)
{
    size_t name_len = 0;
    GameCardHashFileSystemEntry *fs_entry = NULL;
    GameCardHashFileSystemHeader *fs_header = (GameCardHashFileSystemHeader*)header;
    char *name_table = gamecardGetHashFileSystemNameTable(header);
    
    if (!fs_header || !fs_header->entry_count || !name_table || !name || !(name_len = strlen(name)) || !out_idx) return false;
    
    for(u32 i = 0; i < fs_header->entry_count; i++)
    {
        if (!(fs_entry = gamecardGetHashFileSystemEntryByIndex(header, i))) return false;
        
        if (strlen(name_table + fs_entry->name_offset) == name_len && !strcmp(name_table + fs_entry->name_offset, name))
        {
            *out_idx = i;
            return true;
        }
    }
    
    return false;
}
