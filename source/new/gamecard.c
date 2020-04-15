#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <threads.h>

#include "gamecard.h"
#include "service_guard.h"
#include "utils.h"

#define GAMECARD_ACCESS_WAIT_TIME               3                       /* Seconds */

#define GAMECARD_UPDATE_TID                     (u64)0x0100000000000816

#define GAMECARD_READ_BUFFER_SIZE               0x800000                /* 8 MiB */

#define GAMECARD_ECC_BLOCK_SIZE                 0x200
#define GAMECARD_ECC_DATA_SIZE                  0x24

typedef struct {
    u64 offset; ///< Relative to the start of the gamecard header.
    u64 size;   ///< Whole partition size.
    u8 *header; ///< GameCardHashFileSystemHeader + GameCardHashFileSystemEntry + Name Table.
} GameCardHashFileSystemPartitionInfo;

static FsDeviceOperator g_deviceOperator = {0};
static FsEventNotifier g_gameCardEventNotifier = {0};
static Event g_gameCardKernelEvent = {0};
static bool g_openDeviceOperator = false, g_openEventNotifier = false, g_loadKernelEvent = false;

static thrd_t g_gameCardDetectionThread;
static UEvent g_gameCardDetectionThreadExitEvent = {0};
static mtx_t g_gameCardSharedDataMutex;
static bool g_gameCardDetectionThreadCreated = false, g_gameCardInserted = false, g_gameCardInfoLoaded = false;

static FsGameCardHandle g_gameCardHandle = {0};
static FsStorage g_gameCardStorageNormal = {0}, g_gameCardStorageSecure = {0};
static u8 *g_gameCardReadBuf = NULL;

static GameCardHeader g_gameCardHeader = {0};
static u64 g_gameCardStorageNormalAreaSize = 0, g_gameCardStorageSecureAreaSize = 0;

static u8 *g_gameCardHfsRootHeader = NULL;   /* GameCardHashFileSystemHeader + GameCardHashFileSystemEntry + Name Table */
static GameCardHashFileSystemPartitionInfo *g_gameCardHfsPartitions = NULL;

static bool gamecardCreateDetectionThread(void);
static void gamecardDestroyDetectionThread(void);
static int gamecardDetectionThreadFunc(void *arg);

static inline bool gamecardCheckIfInserted(void);

static void gamecardLoadInfo(void);
static void gamecardFreeInfo(void);

static bool gamecardGetHandle(void);
static inline void gamecardCloseHandle(void);

static bool gamecardOpenStorageAreas(void);
static bool _gamecardStorageRead(void *out, u64 out_size, u64 offset, bool lock);
static void gamecardCloseStorageAreas(void);

static bool gamecardGetSizesFromStorageAreas(void);

/* Service guard used to generate thread-safe initialize + exit functions */
NX_GENERATE_SERVICE_GUARD(gamecard);

bool gamecardCheckReadyStatus(void)
{
    mtx_lock(&g_gameCardSharedDataMutex);
    bool status = (g_gameCardInserted && g_gameCardInfoLoaded);
    mtx_unlock(&g_gameCardSharedDataMutex);
    return status;
}

bool gamecardStorageRead(void *out, u64 out_size, u64 offset)
{
    return _gamecardStorageRead(out, out_size, offset, true);
}

bool gamecardGetHeader(GameCardHeader *out)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out)
    {
        memcpy(out, &g_gameCardHeader, sizeof(GameCardHeader));
        ret = true;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetTotalRomSize(u64 *out)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out)
    {
        *out = (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize);
        ret = true;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetTrimmedRomSize(u64 *out)
{
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardInfoLoaded && out)
    {
        *out = (sizeof(GameCardHeader) + ((u64)g_gameCardHeader.valid_data_end_address * GAMECARD_MEDIA_UNIT_SIZE));
        ret = true;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetCertificate(FsGameCardCertificate *out)
{
    Result rc = 0;
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardHandle.value && out)
    {
        rc = fsDeviceOperatorGetGameCardDeviceCertificate(&g_deviceOperator, &g_gameCardHandle, out);
        if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorGetGameCardDeviceCertificate failed! (0x%08X)", rc);
        ret = R_SUCCEEDED(rc);
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}

bool gamecardGetBundledFirmwareUpdateVersion(u32 *out)
{
    Result rc = 0;
    u64 update_id = 0;
    u32 update_version = 0;
    bool ret = false;
    
    mtx_lock(&g_gameCardSharedDataMutex);
    if (g_gameCardInserted && g_gameCardHandle.value && out)
    {
        rc = fsDeviceOperatorUpdatePartitionInfo(&g_deviceOperator, &g_gameCardHandle, &update_version, &update_id);
        if (R_FAILED(rc)) LOGFILE("fsDeviceOperatorUpdatePartitionInfo failed! (0x%08X)", rc);
        ret = (R_SUCCEEDED(rc) && update_id == GAMECARD_UPDATE_TID);
        if (ret) *out = update_version;
    }
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return ret;
}
























NX_INLINE Result _gamecardInitialize(void)
{
    Result rc = 0;
    
    /* Allocate memory for the gamecard read buffer */
    g_gameCardReadBuf = malloc(GAMECARD_READ_BUFFER_SIZE);
    if (!g_gameCardReadBuf)
    {
        LOGFILE("Unable to allocate memory for the gamecard read buffer!");
        rc = MAKERESULT(Module_Libnx, LibnxError_HeapAllocFailed);
        goto out;
    }
    
    /* Open device operator */
    rc = fsOpenDeviceOperator(&g_deviceOperator);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenDeviceOperator failed! (0x%08X)", rc);
        goto out;
    }
    
    g_openDeviceOperator = true;
    
    /* Open gamecard detection event notifier */
    rc = fsOpenGameCardDetectionEventNotifier(&g_gameCardEventNotifier);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenGameCardDetectionEventNotifier failed! (0x%08X)", rc);
        goto out;
    }
    
    g_openEventNotifier = true;
    
    /* Retrieve gamecard detection kernel event */
    rc = fsEventNotifierGetEventHandle(&g_gameCardEventNotifier, &g_gameCardKernelEvent, true);
    if (R_FAILED(rc))
    {
        LOGFILE("fsEventNotifierGetEventHandle failed! (0x%08X)", rc);
        goto out;
    }
    
    g_loadKernelEvent = true;
    
    /* Create usermode exit event */
    ueventCreate(&g_gameCardDetectionThreadExitEvent, false);
    
    /* Create gamecard detection thread */
    g_gameCardDetectionThreadCreated = gamecardCreateDetectionThread();
    if (!g_gameCardDetectionThreadCreated)
    {
        LOGFILE("Failed to create gamecard detection thread!");
        rc = MAKERESULT(Module_Libnx, LibnxError_IoError);
    }
    
out:
    return rc;
}

static void _gamecardCleanup(void)
{
    /* Destroy gamecard detection thread */
    if (g_gameCardDetectionThreadCreated)
    {
        gamecardDestroyDetectionThread();
        g_gameCardDetectionThreadCreated = false;
    }
    
    /* Close gamecard detection kernel event */
    if (g_loadKernelEvent)
    {
        eventClose(&g_gameCardKernelEvent);
        g_loadKernelEvent = false;
    }
    
    /* Close gamecard detection event notifier */
    if (g_openEventNotifier)
    {
        fsEventNotifierClose(&g_gameCardEventNotifier);
        g_openEventNotifier = false;
    }
    
    /* Close device operator */
    if (g_openDeviceOperator)
    {
        fsDeviceOperatorClose(&g_deviceOperator);
        g_openDeviceOperator = false;
    }
    
    /* Free gamecard read buffer */
    if (g_gameCardReadBuf)
    {
        free(g_gameCardReadBuf);
        g_gameCardReadBuf = NULL;
    }
}

static bool gamecardCreateDetectionThread(void)
{
    if (mtx_init(&g_gameCardSharedDataMutex, mtx_plain) != thrd_success)
    {
        LOGFILE("Failed to initialize gamecard shared data mutex!");
        return false;
    }
    
    if (thrd_create(&g_gameCardDetectionThread, gamecardDetectionThreadFunc, NULL) != thrd_success)
    {
        LOGFILE("Failed to create gamecard detection thread!");
        mtx_destroy(&g_gameCardSharedDataMutex);
        return false;
    }
    
    return true;
}

static void gamecardDestroyDetectionThread(void)
{
    /* Signal the exit event to terminate the gamecard detection thread */
    ueventSignal(&g_gameCardDetectionThreadExitEvent);
    
    /* Wait for the gamecard detection thread to exit */
    thrd_join(g_gameCardDetectionThread, NULL);
    
    /* Destroy mutex */
    mtx_destroy(&g_gameCardSharedDataMutex);
}

static int gamecardDetectionThreadFunc(void *arg)
{
    (void)arg;
    
    Result rc = 0;
    int idx = 0;
    bool prev_status = false;
    
    Waiter gamecard_event_waiter = waiterForEvent(&g_gameCardKernelEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_gameCardDetectionThreadExitEvent);
    
    mtx_lock(&g_gameCardSharedDataMutex);
    
    /* Retrieve initial gamecard insertion status */
    g_gameCardInserted = prev_status = gamecardCheckIfInserted();
    
    /* Load gamecard info right away if a gamecard is inserted and if a handle can be retrieved */
    if (g_gameCardInserted && gamecardGetHandle()) gamecardLoadInfo();
    
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    while(true)
    {
        /* Wait until an event is triggered */
        rc = waitMulti(&idx, -1, gamecard_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;
        
        /* Exit event triggered */
        if (idx == 1) break;
        
        /* Retrieve current gamecard insertion status */
        /* Only proceed if we're dealing with a status change */
        mtx_lock(&g_gameCardSharedDataMutex);
        
        g_gameCardInserted = gamecardCheckIfInserted();
        
        if (!prev_status && g_gameCardInserted)
        {
            /* Don't access the gamecard immediately to avoid conflicts with HOS / sysmodules */
            SLEEP(GAMECARD_ACCESS_WAIT_TIME);
            
            /* Load gamecard info if a gamecard is inserted and if a handle can be retrieved */
            if (gamecardGetHandle()) gamecardLoadInfo();
        } else {
            /* Free gamecard info and close gamecard handle */
            gamecardFreeInfo();
            gamecardCloseHandle();
        }
        
        prev_status = g_gameCardInserted;
        
        mtx_unlock(&g_gameCardSharedDataMutex);
    }
    
    /* Free gamecard info and close gamecard handle */
    mtx_lock(&g_gameCardSharedDataMutex);
    gamecardFreeInfo();
    gamecardCloseHandle();
    g_gameCardInserted = false;
    mtx_unlock(&g_gameCardSharedDataMutex);
    
    return 0;
}

static inline bool gamecardCheckIfInserted(void)
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
    
    /* Open gamecard storage areas */
    if (!gamecardOpenStorageAreas())
    {
        LOGFILE("Failed to open gamecard storage areas!");
        goto out;
    }
    
    /* Read gamecard header */
    if (!_gamecardStorageRead(&g_gameCardHeader, sizeof(GameCardHeader), 0, false))
    {
        LOGFILE("Failed to read gamecard header!");
        goto out;
    }
    
    /* Check magic word from gamecard header */
    if (__builtin_bswap32(g_gameCardHeader.magic) != GAMECARD_HEAD_MAGIC)
    {
        LOGFILE("Invalid gamecard header magic word! (0x%08X)", __builtin_bswap32(g_gameCardHeader.magic));
        goto out;
    }
    
    if (utilsGetCustomFirmwareType() == UtilsCustomFirmwareType_SXOS)
    {
        /* Total size for the secure storage area is maxed out under SX OS */
        /* Let's try to calculate it manually */
        u64 capacity = gamecardGetCapacity(&g_gameCardHeader);
        if (!capacity)
        {
            LOGFILE("Invalid gamecard capacity value! (0x%02X)", g_gameCardHeader.rom_size);
            goto out;
        }
        
        g_gameCardStorageSecureAreaSize = ((capacity - ((capacity / GAMECARD_ECC_BLOCK_SIZE) * GAMECARD_ECC_DATA_SIZE)) - g_gameCardStorageNormalAreaSize);
    }
    
    /* Allocate memory for the root hash FS header */
    g_gameCardHfsRootHeader = calloc(g_gameCardHeader.partition_fs_header_size, sizeof(u8));
    if (!g_gameCardHfsRootHeader)
    {
        LOGFILE("Unable to allocate memory for the root hash FS header!");
        goto out;
    }
    
    /* Read root hash FS header */
    if (!_gamecardStorageRead(g_gameCardHfsRootHeader, g_gameCardHeader.partition_fs_header_size, g_gameCardHeader.partition_fs_header_address, false))
    {
        LOGFILE("Failed to read root hash FS header from offset 0x%lX!", g_gameCardHeader.partition_fs_header_address);
        goto out;
    }
    
    fs_header = (GameCardHashFileSystemHeader*)g_gameCardHfsRootHeader;
    
    if (__builtin_bswap32(fs_header->magic) != GAMECARD_HFS0_MAGIC)
    {
        LOGFILE("Invalid magic word in root hash FS header! (0x%08X)", __builtin_bswap32(fs_header->magic));
        goto out;
    }
    
    if (!fs_header->entry_count || !fs_header->name_table_size || \
        (sizeof(GameCardHashFileSystemHeader) + (fs_header->entry_count * sizeof(GameCardHashFileSystemEntry)) + fs_header->name_table_size) > g_gameCardHeader.partition_fs_header_size)
    {
        LOGFILE("Invalid file count and/or name table size in root hash FS header!");
        goto out;
    }
    
    /* Allocate memory for the hash FS partitions info */
    g_gameCardHfsPartitions = calloc(fs_header->entry_count, sizeof(GameCardHashFileSystemEntry));
    if (!g_gameCardHfsPartitions)
    {
        LOGFILE("Unable to allocate memory for the hash FS partitions info!");
        goto out;
    }
    
    /* Read hash FS partitions */
    for(u32 i = 0; i < fs_header->entry_count; i++)
    {
        fs_entry = (GameCardHashFileSystemEntry*)(g_gameCardHfsRootHeader + sizeof(GameCardHashFileSystemHeader) + (i * sizeof(GameCardHashFileSystemEntry)));
        
        if (!fs_entry->size)
        {
            LOGFILE("Invalid size for hash FS partition #%u!", i);
            goto out;
        }
        
        g_gameCardHfsPartitions[i].offset = (g_gameCardHeader.partition_fs_header_address + g_gameCardHeader.partition_fs_header_size + fs_entry->offset);
        g_gameCardHfsPartitions[i].size = fs_entry->size;
        
        /* Partially read the current hash FS partition header */
        GameCardHashFileSystemHeader partition_header = {0};
        if (!_gamecardStorageRead(&partition_header, sizeof(GameCardHashFileSystemHeader), g_gameCardHfsPartitions[i].offset, false))
        {
            LOGFILE("Failed to partially read hash FS partition #%u header from offset 0x%lX!", i, g_gameCardHfsPartitions[i].offset);
            goto out;
        }
        
        if (__builtin_bswap32(partition_header.magic) != GAMECARD_HFS0_MAGIC)
        {
            LOGFILE("Invalid magic word in hash FS partition #%u header! (0x%08X)", i, __builtin_bswap32(partition_header.magic));
            goto out;
        }
        
        if (!partition_header.name_table_size)
        {
            LOGFILE("Invalid name table size in hash FS partition #%u header!", i);
            goto out;
        }
        
        /* Calculate the full header size for the current hash FS partition */
        u64 partition_header_size = (sizeof(GameCardHashFileSystemHeader) + (partition_header.entry_count * sizeof(GameCardHashFileSystemEntry)) + partition_header.name_table_size);
        
        /* Allocate memory for the hash FS partition header */
        g_gameCardHfsPartitions[i].header = calloc(partition_header_size, sizeof(u8));
        if (!g_gameCardHfsPartitions[i].header)
        {
            LOGFILE("Unable to allocate memory for the hash FS partition #%u header!", i);
            goto out;
        }
        
        /* Finally, read the full hash FS partition header */
        if (!_gamecardStorageRead(g_gameCardHfsPartitions[i].header, partition_header_size, g_gameCardHfsPartitions[i].offset, false))
        {
            LOGFILE("Failed to read full hash FS partition #%u header from offset 0x%lX!", i, g_gameCardHfsPartitions[i].offset);
            goto out;
        }
    }
    
    g_gameCardInfoLoaded = true;
    
out:
    if (!g_gameCardInfoLoaded) gamecardFreeInfo();
}

static void gamecardFreeInfo(void)
{
    memset(&g_gameCardHeader, 0, sizeof(GameCardHeader));
    
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
    
    gamecardCloseStorageAreas();
    
    g_gameCardInfoLoaded = false;
}

static bool gamecardGetHandle(void)
{
    if (!g_gameCardInserted)
    {
        LOGFILE("Gamecard not inserted!");
        return false;
    }
    
    if (g_gameCardInfoLoaded && g_gameCardHandle.value) return true;
    
    Result rc1 = 0, rc2 = 0;
    FsStorage tmp_storage = {0};
    
    /* 10 tries */
    for(u8 i = 0; i < 10; i++)
    {
        /* First try to open a gamecard storage area using the current gamecard handle */
        rc1 = fsOpenGameCardStorage(&tmp_storage, &g_gameCardHandle, 0);
        if (R_SUCCEEDED(rc1))
        {
            fsStorageClose(&tmp_storage);
            break;
        }
        
        /* If the previous call failed, we may have an invalid handle, so let's close the current one and try to retrieve a new one */
        gamecardCloseHandle();
        rc2 = fsDeviceOperatorGetGameCardHandle(&g_deviceOperator, &g_gameCardHandle);
    }
    
    if (R_FAILED(rc1) || R_FAILED(rc2))
    {
        /* Close leftover gamecard handle */
        gamecardCloseHandle();
        
        if (R_FAILED(rc1)) LOGFILE("fsOpenGameCardStorage failed! (0x%08X)", rc1);
        if (R_FAILED(rc2)) LOGFILE("fsDeviceOperatorGetGameCardHandle failed! (0x%08X)", rc2);
        
        return false;
    }
    
    return true;
}

static inline void gamecardCloseHandle(void)
{
    svcCloseHandle(g_gameCardHandle.value);
    g_gameCardHandle.value = 0;
}

static bool gamecardOpenStorageAreas(void)
{
    if (!g_gameCardInserted || !g_gameCardHandle.value)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    if (g_gamecardInfoLoaded && serviceIsActive(&(g_gameCardStorageNormal.s)) && serviceIsActive(&(g_gameCardStorageSecure.s))) return true;
    
    gamecardCloseStorageAreas();
    
    Result rc = 0;
    bool success = false;
    
    rc = fsOpenGameCardStorage(&g_gameCardStorageNormal, &g_gameCardHandle, 0);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenGameCardStorage failed! (0x%08X) (normal)", rc);
        goto out;
    }
    
    rc = fsOpenGameCardStorage(&g_gameCardStorageSecure, &g_gameCardHandle, 1);
    if (R_FAILED(rc))
    {
        LOGFILE("fsOpenGameCardStorage failed! (0x%08X) (secure)", rc);
        goto out;
    }
    
    if (!gamecardGetSizesFromStorageAreas())
    {
        LOGFILE("Failed to retrieve sizes from storage areas!");
        goto out;
    }
    
    success = true;
    
out:
    if (!success) gamecardCloseStorageAreas();
    
    return success;
}

static bool _gamecardStorageRead(void *out, u64 out_size, u64 offset, bool lock)
{
    if (lock) mtx_lock(&g_gameCardSharedDataMutex);
    
    bool success = false;
    
    if (!g_gameCardInserted || !serviceIsActive(&(g_gameCardStorageNormal.s)) || !g_gameCardStorageNormalAreaSize || !serviceIsActive(&(g_gameCardStorageSecure.s)) || \
        !g_gameCardStorageSecureAreaSize || !out || !out_size || offset >= (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize) || \
        (offset + out_size) > (g_gameCardStorageNormalAreaSize + g_gameCardStorageSecureAreaSize))
    {
        LOGFILE("Invalid parameters!");
        goto out;
    }
    
    Result rc = 0;
    u8 *out_u8 = (u8*)out;
    
    /* Handle reads between the end of the normal storage area and the start of the secure storage area */
    if (offset < g_gameCardStorageNormalAreaSize && (offset + out_size) > g_gameCardStorageNormalAreaSize)
    {
        /* Calculate normal storage area size difference */
        u64 diff_size = (g_gameCardStorageNormalAreaSize - offset);
        
        if (!_gamecardStorageRead(out_u8, diff_size, offset, false)) goto out;
        
        /* Adjust variables to start reading right from the start of the secure storage area */
        out_u8 += diff_size;
        offset = g_gameCardStorageNormalAreaSize;
        out_size -= diff_size;
    }
    
    /* Calculate appropiate storage area offset and retrieve the right storage area pointer */
    const char *area = (offset < g_gameCardStorageNormalAreaSize ? "normal" : "secure");
    u64 base_offset = (offset < g_gameCardStorageNormalAreaSize ? offset : (offset - g_gameCardStorageNormalAreaSize));
    FsStorage *storage = (offset < g_gameCardStorageNormalAreaSize ? &g_gameCardStorageNormal : &g_gameCardStorageSecure);
    
    if (!(base_offset % GAMECARD_MEDIA_UNIT_SIZE) && !(out_size % GAMECARD_MEDIA_UNIT_SIZE))
    {
        /* Optimization for reads that are already aligned to GAMECARD_MEDIA_UNIT_SIZE bytes */
        rc = fsStorageRead(storage, base_offset, out_u8, out_size);
        if (R_FAILED(rc))
        {
            LOGFILE("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (aligned)", out_size, base_offset, area, rc);
            goto out;
        }
        
        success = true;
    } else {
        /* Fix offset and/or size to avoid unaligned reads */
        u64 block_start_offset = (base_offset - (base_offset % GAMECARD_MEDIA_UNIT_SIZE));
        u64 block_end_offset = round_up(base_offset + out_size, GAMECARD_MEDIA_UNIT_SIZE);
        u64 block_size = (block_end_offset - block_start_offset);
        
        u64 chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? GAMECARD_READ_BUFFER_SIZE : block_size);
        u64 out_chunk_size = (block_size > GAMECARD_READ_BUFFER_SIZE ? (GAMECARD_READ_BUFFER_SIZE - (base_offset - block_start_offset)) : out_size);
        
        rc = fsStorageRead(storage, block_start_offset, g_gameCardReadBuf, chunk_size);
        if (!R_FAILED(rc))
        {
            LOGFILE("fsStorageRead failed to read 0x%lX bytes at offset 0x%lX from %s storage area! (0x%08X) (unaligned)", chunk_size, block_start_offset, area, rc);
            goto out;
        }
        
        memcpy(out_u8, g_gameCardReadBuf + (base_offset - block_start_offset), out_chunk_size);
        
        success = (block_size > GAMECARD_READ_BUFFER_SIZE ? _gamecardStorageRead(out_u8 + out_chunk_size, out_size - out_chunk_size, base_offset + out_chunk_size, false) : true);
    }
    
out:
    if (lock) mtx_unlock(&g_gameCardSharedDataMutex);
    
    return success;
}

static void gamecardCloseStorageAreas(void)
{
    if (serviceIsActive(&(g_gameCardStorageNormal.s)))
    {
        fsStorageClose(&g_gameCardStorageNormal);
        memset(&g_gameCardStorageNormal, 0, sizeof(FsStorage));
    }
    
    g_gameCardStorageNormalAreaSize = 0;
    
    if (serviceIsActive(&(g_gameCardStorageSecure.s)))
    {
        fsStorageClose(&g_gameCardStorageSecure);
        memset(&g_gameCardStorageSecure, 0, sizeof(FsStorage));
    }
    
    g_gameCardStorageSecureAreaSize = 0;
}

static bool gamecardGetSizesFromStorageAreas(void)
{
    if (!g_gameCardInserted || !serviceIsActive(&(g_gameCardStorageNormal.s)) || !serviceIsActive(&(g_gameCardStorageSecure.s)))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    
    rc = fsStorageGetSize(&g_gameCardStorageNormal, (s64*)&g_gameCardStorageNormalAreaSize);
    if (R_FAILED(rc))
    {
        LOGFILE("fsStorageGetSize failed! (0x%08X) (normal)", rc);
        return false;
    }
    
    rc = fsStorageGetSize(&g_gameCardStorageSecure, (s64*)&g_gameCardStorageSecureAreaSize);
    if (R_FAILED(rc))
    {
        LOGFILE("fsStorageGetSize failed! (0x%08X) (secure)", rc);
        g_gameCardStorageNormalAreaSize = 0;
        return false;
    }
    
    return true;
}
