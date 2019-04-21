#include <stdio.h>
#include <malloc.h>
#include <dirent.h>
#include <memory.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include "crc32_fast.h"
#include "dumper.h"
#include "fsext.h"
#include "ui.h"
#include "util.h"

extern u64 freeSpace;

extern int breaks;
extern int font_height;

extern u64 gameCardSize, trimmedCardSize;
extern char gameCardSizeStr[32], trimmedCardSizeStr[32];

extern char *hfs0_header;
extern u64 hfs0_offset, hfs0_size;
extern u32 hfs0_partition_cnt;

extern char *partitionHfs0Header;
extern u64 partitionHfs0HeaderSize;

extern u64 gameCardTitleID;
extern u32 gameCardVersion;
extern char fixedGameCardName[0x201];

extern u64 gameCardUpdateTitleID;
extern u32 gameCardUpdateVersion;
extern char gameCardUpdateVersionStr[128];

static char strbuf[NAME_BUF_LEN * 2] = {'\0'};

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator)
{
    FsGameCardHandle handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle))) return;
    
    FsStorage gameCardStorage;
    if (R_FAILED(fsOpenGameCardStorage(&gameCardStorage, &handle, 0))) return;
    
    fsStorageClose(&gameCardStorage);
}

bool getRootHfs0Header(FsDeviceOperator* fsOperator)
{
    u32 magic;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    
    hfs0_partition_cnt = 0;
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (R_FAILED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
    {
        uiStatusMsg("getRootHfs0Header: GetGameCardHandle failed! (0x%08X)", result);
        return false;
    }
    
    // Get bundled FW version update
    if (R_SUCCEEDED(fsDeviceOperatorUpdatePartitionInfo(fsOperator, &handle, &gameCardUpdateVersion, &gameCardUpdateTitleID)))
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
            gameCardUpdateTitleID = 0;
            gameCardUpdateVersion = 0;
        }
    }
    
    if (R_FAILED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, 0)))
    {
        uiStatusMsg("getRootHfs0Header: OpenGameCardStorage failed! (0x%08X)", result);
        return false;
    }
    
    char *gamecard_header = (char*)malloc(GAMECARD_HEADER_SIZE);
    if (!gamecard_header)
    {
        uiStatusMsg("getRootHfs0Header: Unable to allocate memory for the gamecard header!");
        fsStorageClose(&gameCardStorage);
        return false;
    }
    
    if (R_FAILED(result = fsStorageRead(&gameCardStorage, 0, gamecard_header, GAMECARD_HEADER_SIZE)))
    {
        uiStatusMsg("getRootHfs0Header: StorageRead failed to read %u-byte chunk from offset 0x%016lX! (0x%08X)", GAMECARD_HEADER_SIZE, 0, result);
        free(gamecard_header);
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
            free(gamecard_header);
            fsStorageClose(&gameCardStorage);
            return false;
    }
    
    convertSize(gameCardSize, gameCardSizeStr, sizeof(gameCardSizeStr) / sizeof(gameCardSizeStr[0]));
    
    memcpy(&trimmedCardSize, gamecard_header + GAMECARD_DATAEND_ADDR, sizeof(u64));
    trimmedCardSize = (GAMECARD_HEADER_SIZE + (trimmedCardSize * MEDIA_UNIT_SIZE));
    convertSize(trimmedCardSize, trimmedCardSizeStr, sizeof(trimmedCardSizeStr) / sizeof(trimmedCardSizeStr[0]));
    
    memcpy(&hfs0_offset, gamecard_header + HFS0_OFFSET_ADDR, sizeof(u64));
    memcpy(&hfs0_size, gamecard_header + HFS0_SIZE_ADDR, sizeof(u64));
    
    free(gamecard_header);
    
    hfs0_header = (char*)malloc(hfs0_size);
    if (!hfs0_header)
    {
        uiStatusMsg("getRootHfs0Header: Unable to allocate memory for the root HFS0 header!");
        
        gameCardSize = 0;
        memset(gameCardSizeStr, 0, sizeof(gameCardSizeStr));
        
        trimmedCardSize = 0;
        memset(trimmedCardSizeStr, 0, sizeof(trimmedCardSizeStr));
        
        hfs0_offset = hfs0_size = 0;
        
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
        hfs0_offset = hfs0_size = 0;
        
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
        hfs0_offset = hfs0_size = 0;
        
        fsStorageClose(&gameCardStorage);
        
        return false;
    }
    
    memcpy(&hfs0_partition_cnt, hfs0_header + HFS0_FILE_COUNT_ADDR, sizeof(u32));
    
    fsStorageClose(&gameCardStorage);
    
    return true;
}

bool getHsf0PartitionDetails(u32 partition, u64 *out_offset, u64 *out_size)
{
    if (hfs0_header == NULL) return false;
    
    if (partition > (hfs0_partition_cnt - 1)) return false;
    
    hfs0_entry_table *entryTable = (hfs0_entry_table*)malloc(sizeof(hfs0_entry_table) * hfs0_partition_cnt);
    if (!entryTable) return false;
    
    memcpy(entryTable, hfs0_header + HFS0_ENTRY_TABLE_ADDR, sizeof(hfs0_entry_table) * hfs0_partition_cnt);
    
    switch(partition)
    {
        case 0: // update (contained within IStorage instance with partition ID 0)
        case 1: // normal or logo (depending on the gamecard type) (contained within IStorage instance with partition ID 0)
            *out_offset = (hfs0_offset + hfs0_size + entryTable[partition].file_offset);
            break;
        case 2:
            if (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT)
            {
                // secure (contained within IStorage instance with partition ID 1)
                *out_offset = 0;
            } else {
                // normal (contained within IStorage instance with partition ID 0)
                *out_offset = (hfs0_offset + hfs0_size + entryTable[partition].file_offset);
            }
            break;
        case 3: // secure (gamecard type 0x02) (contained within IStorage instance with partition ID 1)
            *out_offset = 0;
            break;
        default:
            break;
    }
    
    *out_size = entryTable[partition].file_size;
    
    free(entryTable);
    
    return true;
}

bool getPartitionHfs0Header(FsDeviceOperator* fsOperator, u32 partition)
{
    if (hfs0_header == NULL) return false;
    
    if (partitionHfs0Header != NULL)
    {
        free(partitionHfs0Header);
        partitionHfs0Header = NULL;
        partitionHfs0HeaderSize = 0;
    }
    
    char *buf = NULL;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    u64 partitionSize = 0, partitionOffset = 0, roundedHfs0HeaderSize = 0;
    u32 hfs0FileCount = 0, hfs0StrTableSize = 0, magic = 0;
    bool success = false;
    
    if (getHsf0PartitionDetails(partition, &partitionOffset, &partitionSize))
    {
        workaroundPartitionZeroAccess(fsOperator);
        
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            // Same ugly hack from dumpRawPartition()
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? (partition < 2 ? 0 : 1) : (partition < 3 ? 0 : 1)))))
            {
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;*/
                
                buf = (char*)malloc(MEDIA_UNIT_SIZE);
                if (buf)
                {
                    // First read MEDIA_UNIT_SIZE bytes
                    if (R_SUCCEEDED(result = fsStorageRead(&gameCardStorage, partitionOffset, buf, MEDIA_UNIT_SIZE)))
                    {
                        // Check the HFS0 magic word
                        memcpy(&magic, buf, sizeof(u32));
                        magic = bswap_32(magic);
                        if (magic == HFS0_MAGIC)
                        {
                            // Calculate the size for the partition HFS0 header
                            memcpy(&hfs0FileCount, buf + HFS0_FILE_COUNT_ADDR, sizeof(u32));
                            memcpy(&hfs0StrTableSize, buf + HFS0_STR_TABLE_SIZE_ADDR, sizeof(u32));
                            partitionHfs0HeaderSize = (HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * hfs0FileCount) + hfs0StrTableSize);
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u file count: %u", partition, hfs0FileCount);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u string table size: %u bytes", partition, hfs0StrTableSize);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u HFS0 header size: %lu bytes", partition, partitionHfs0HeaderSize);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            // Round up the partition HFS0 header size to a MEDIA_UNIT_SIZE bytes boundary
                            roundedHfs0HeaderSize = round_up(partitionHfs0HeaderSize, MEDIA_UNIT_SIZE);
                            
                            partitionHfs0Header = (char*)malloc(roundedHfs0HeaderSize);
                            if (partitionHfs0Header)
                            {
                                // Check if we were dealing with the correct header size all along
                                if (roundedHfs0HeaderSize == MEDIA_UNIT_SIZE)
                                {
                                    // Just copy what we already have
                                    memcpy(partitionHfs0Header, buf, MEDIA_UNIT_SIZE);
                                    success = true;
                                } else {
                                    // Read the whole HFS0 header
                                    if (R_SUCCEEDED(result = fsStorageRead(&gameCardStorage, partitionOffset, partitionHfs0Header, roundedHfs0HeaderSize)))
                                    {
                                        success = true;
                                    } else {
                                        free(partitionHfs0Header);
                                        partitionHfs0Header = NULL;
                                        partitionHfs0HeaderSize = 0;
                                        
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionOffset);
                                        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                                    }
                                }
                                
                                if (success)
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u HFS0 header successfully retrieved!", partition);
                                    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                                }
                            } else {
                                partitionHfs0HeaderSize = 0;
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to allocate memory for the HFS0 header from partition #%u!", partition);
                                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                            }
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Magic word mismatch! 0x%08X != 0x%08X", magic, HFS0_MAGIC);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                        }
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionOffset);
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
    
    breaks += 2;
    
    return success;
}

bool dumpGameCartridge(FsDeviceOperator* fsOperator, bool isFat32, bool dumpCert, bool trimDump, bool calcCrc)
{
    u64 partitionOffset = 0, fileOffset = 0, xciDataSize = 0, totalSize = 0, n;
    u64 partitionSizes[ISTORAGE_PARTITION_CNT];
    char partitionSizesStr[ISTORAGE_PARTITION_CNT][32] = {'\0'}, xciDataSizeStr[32] = {'\0'}, curSizeStr[32] = {'\0'}, totalSizeStr[32] = {'\0'}, filename[NAME_BUF_LEN] = {'\0'};
    u32 partition;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    bool proceed = true, success = false;
    FILE *outFile = NULL;
    char *buf = NULL;
    u8 splitIndex = 0;
    u8 progress = 0;
    u32 crc1 = 0, crc2 = 0;
    
    u64 start, now, remainingTime;
    struct tm *timeinfo;
    char etaInfo[32] = {'\0'};
    double lastSpeed = 0.0, averageSpeed = 0.0;
    
    for(partition = 0; partition < ISTORAGE_PARTITION_CNT; partition++)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Getting partition #%u size...", partition);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        workaroundPartitionZeroAccess(fsOperator);
        
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, partition)))
            {
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;*/
                
                if (R_SUCCEEDED(result = fsStorageGetSize(&gameCardStorage, &(partitionSizes[partition]))))
                {
                    xciDataSize += partitionSizes[partition];
                    convertSize(partitionSizes[partition], partitionSizesStr[partition], sizeof(partitionSizesStr[partition]) / sizeof(partitionSizesStr[partition][0]));
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u size: %s (%lu bytes)", partition, partitionSizesStr[partition], partitionSizes[partition]);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                    breaks += 2;
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageGetSize failed! (0x%08X)", result);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                    proceed = false;
                }
                
                fsStorageClose(&gameCardStorage);
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                proceed = false;
            }
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            proceed = false;
        }
        
        uiRefreshDisplay();
    }
    
    if (proceed)
    {
        convertSize(xciDataSize, xciDataSizeStr, sizeof(xciDataSizeStr) / sizeof(xciDataSizeStr[0]));
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI data size: %s (%lu bytes)", xciDataSizeStr, xciDataSize);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks += 2;
        
        if (trimDump)
        {
            totalSize = trimmedCardSize;
            snprintf(totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]), "%s", trimmedCardSizeStr);
            
            // Change dump size for the last IStorage partition
            u64 partitionSizesSum = 0;
            for(int i = 0; i < (ISTORAGE_PARTITION_CNT - 1); i++) partitionSizesSum += partitionSizes[i];
            
            partitionSizes[ISTORAGE_PARTITION_CNT - 1] = (trimmedCardSize - partitionSizesSum);
        } else {
            totalSize = xciDataSize;
            snprintf(totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]), "%s", xciDataSizeStr);
        }
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output dump size: %s (%lu bytes)", totalSizeStr, totalSize);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        if (totalSize <= freeSpace)
        {
            breaks++;
            
            if (totalSize > SPLIT_FILE_MIN && isFat32)
            {
                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX).xc%u", fixedGameCardName, gameCardVersion, gameCardTitleID, splitIndex);
            } else {
                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX).xci", fixedGameCardName, gameCardVersion, gameCardTitleID);
            }
            
            outFile = fopen(filename, "wb");
            if (outFile)
            {
                buf = (char*)malloc(DUMP_BUFFER_SIZE);
                if (buf)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump procedure started. Writing output to \"%s\". Hold B to cancel.", filename);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                    breaks += 2;
                    
                    uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                    breaks += 2;
                    
                    timeGetCurrentTime(TimeType_LocalSystemClock, &start);
                    
                    for(partition = 0; partition < ISTORAGE_PARTITION_CNT; partition++)
                    {
                        n = DUMP_BUFFER_SIZE;
                        
                        uiFill(0, breaks * font_height, FB_WIDTH, font_height * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping partition #%u...", partition);
                        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                        
                        workaroundPartitionZeroAccess(fsOperator);
                        
                        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
                        {
                            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, partition)))
                            {
                                for(partitionOffset = 0; partitionOffset < partitionSizes[partition]; partitionOffset += n, fileOffset += n)
                                {
                                    if (DUMP_BUFFER_SIZE > (partitionSizes[partition] - partitionOffset)) n = (partitionSizes[partition] - partitionOffset);
                                    
                                    if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset, buf, n)))
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX for partition #%u", result, partitionOffset, partition);
                                        uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                        proceed = false;
                                        break;
                                    }
                                    
                                    // Remove game card certificate
                                    if (fileOffset == 0 && !dumpCert) memset(buf + CERT_OFFSET, 0xFF, CERT_SIZE);
                                    
                                    if (calcCrc)
                                    {
                                        if (!trimDump)
                                        {
                                            if (dumpCert)
                                            {
                                                if (fileOffset == 0)
                                                {
                                                    // Update CRC32 (with gamecard certificate)
                                                    crc32(buf, n, &crc1);
                                                    
                                                    // Backup gamecard certificate to an array
                                                    char tmpCert[CERT_SIZE] = {'\0'};
                                                    memcpy(tmpCert, buf + CERT_OFFSET, CERT_SIZE);
                                                    
                                                    // Remove gamecard certificate from buffer
                                                    memset(buf + CERT_OFFSET, 0xFF, CERT_SIZE);
                                                    
                                                    // Update CRC32 (without gamecard certificate)
                                                    crc32(buf, n, &crc2);
                                                    
                                                    // Restore gamecard certificate to buffer
                                                    memcpy(buf + CERT_OFFSET, tmpCert, CERT_SIZE);
                                                } else {
                                                    // Update CRC32 (with gamecard certificate)
                                                    crc32(buf, n, &crc1);
                                                    
                                                    // Update CRC32 (without gamecard certificate)
                                                    crc32(buf, n, &crc2);
                                                }
                                            } else {
                                                // Update CRC32
                                                crc32(buf, n, &crc2);
                                            }
                                        } else {
                                            // Update CRC32
                                            crc32(buf, n, &crc1);
                                        }
                                    }
                                    
                                    if (totalSize > SPLIT_FILE_MIN && isFat32 && (fileOffset + n) < totalSize && (fileOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_2GiB))
                                    {
                                        u64 new_file_chunk_size = ((fileOffset + n) - ((splitIndex + 1) * SPLIT_FILE_2GiB));
                                        u64 old_file_chunk_size = (n - new_file_chunk_size);
                                        
                                        if (old_file_chunk_size > 0)
                                        {
                                            if (fwrite(buf, 1, old_file_chunk_size, outFile) != old_file_chunk_size)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", fileOffset);
                                                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                                proceed = false;
                                                break;
                                            }
                                        }
                                        
                                        fclose(outFile);
                                        
                                        splitIndex++;
                                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX).xc%u", fixedGameCardName, gameCardVersion, gameCardTitleID, splitIndex);
                                        
                                        outFile = fopen(filename, "wb");
                                        if (!outFile)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                            uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                            proceed = false;
                                            break;
                                        }
                                        
                                        if (new_file_chunk_size > 0)
                                        {
                                            if (fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile) != new_file_chunk_size)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", fileOffset + old_file_chunk_size);
                                                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                                proceed = false;
                                                break;
                                            }
                                        }
                                    } else {
                                        if (fwrite(buf, 1, n, outFile) != n)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", fileOffset);
                                            uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                            proceed = false;
                                            break;
                                        }
                                    }
                                    
                                    timeGetCurrentTime(TimeType_LocalSystemClock, &now);
                                    
                                    lastSpeed = (((double)(fileOffset + n) / (double)DUMP_BUFFER_SIZE) / difftime((time_t)now, (time_t)start));
                                    averageSpeed = ((SMOOTHING_FACTOR * lastSpeed) + ((1 - SMOOTHING_FACTOR) * averageSpeed));
                                    if (!isnormal(averageSpeed)) averageSpeed = 0.00; // Very low values
                                    
                                    remainingTime = (u64)(((double)(totalSize - (fileOffset + n)) / (double)DUMP_BUFFER_SIZE) / averageSpeed);
                                    timeinfo = localtime((time_t*)&remainingTime);
                                    strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
                                    
                                    progress = (u8)(((fileOffset + n) * 100) / totalSize);
                                    
                                    uiFill(0, ((breaks + 2) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%.2lf MiB/s [ETA: %s]", averageSpeed, etaInfo);
                                    uiDrawString(strbuf, font_height * 2, (breaks + 2) * font_height, 255, 255, 255);
                                    
                                    uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, FB_WIDTH / 2, font_height, 0, 0, 0);
                                    uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, (((fileOffset + n) * (FB_WIDTH / 2)) / totalSize), font_height, 0, 255, 0);
                                    
                                    uiFill(FB_WIDTH - (FB_WIDTH / 4), ((breaks + 2) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                    convertSize(fileOffset + n, curSizeStr, sizeof(curSizeStr) / sizeof(curSizeStr[0]));
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progress, curSizeStr, totalSizeStr);
                                    uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (breaks + 2) * font_height, 255, 255, 255);
                                    
                                    uiRefreshDisplay();
                                    
                                    if ((fileOffset + n) < totalSize && ((fileOffset / DUMP_BUFFER_SIZE) % 10) == 0)
                                    {
                                        hidScanInput();
                                        
                                        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                                        if (keysDown & KEY_B)
                                        {
                                            uiDrawString("Process canceled", 0, (breaks + 5) * font_height, 255, 0, 0);
                                            proceed = false;
                                            break;
                                        }
                                    }
                                }
                                
                                if (fileOffset >= totalSize) success = true;
                                
                                // Support empty files
                                if (!partitionSizes[partition])
                                {
                                    uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, ((fileOffset * (FB_WIDTH / 2)) / totalSize), font_height, 0, 255, 0);
                                    
                                    uiFill(FB_WIDTH - (FB_WIDTH / 4), ((breaks + 2) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                    convertSize(fileOffset, curSizeStr, sizeof(curSizeStr) / sizeof(curSizeStr[0]));
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progress, curSizeStr, totalSizeStr);
                                    uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (breaks + 2) * font_height, 255, 255, 255);
                                    
                                    uiRefreshDisplay();
                                }
                                
                                if (!proceed)
                                {
                                    uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, (((fileOffset + n) * (FB_WIDTH / 2)) / totalSize), font_height, 255, 0, 0);
                                    breaks += 5;
                                }
                                
                                fsStorageClose(&gameCardStorage);
                            } else {
                                breaks++;
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed for partition #%u! (0x%08X)", partition, result);
                                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                                proceed = false;
                            }
                        } else {
                            breaks++;
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed for partition #%u! (0x%08X)", partition, result);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                            proceed = false;
                        }
                        
                        if (!proceed) break;
                    }
                    
                    free(buf);
                } else {
                    uiDrawString("Failed to allocate memory for the dump process!", 0, breaks * font_height, 255, 0, 0);
                }
                
                if (success)
                {
                    fclose(outFile);
                    
                    breaks += 5;
                    
                    now -= start;
                    timeinfo = localtime((time_t*)&now);
                    strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", etaInfo);
                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                    
                    if (calcCrc)
                    {
                        breaks++;
                        
                        if (!trimDump)
                        {
                            if (dumpCert)
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum (with certificate): %08X", crc1);
                                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                                breaks++;
                                
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum (without certificate): %08X", crc2);
                                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                            } else {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum: %08X", crc2);
                                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                            }
                            
                            breaks += 2;
                            uiDrawString("Starting verification process using XML database from NSWDB.COM...", 0, breaks * font_height, 255, 255, 255);
                            breaks++;
                            
                            uiRefreshDisplay();
                            
                            gameCardDumpNSWDBCheck(crc2);
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum: %08X", crc1);
                            uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                            breaks++;
                            
                            uiDrawString("Dump verification disabled (not compatible with trimmed dumps).", 0, breaks * font_height, 255, 255, 255);
                        }
                    }
                } else {
                    if (outFile) fclose(outFile);
                    
                    if (totalSize > SPLIT_FILE_MIN && isFat32)
                    {
                        for(u8 i = 0; i <= splitIndex; i++)
                        {
                            snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX).xc%u", fixedGameCardName, gameCardVersion, gameCardTitleID, i);
                            remove(filename);
                        }
                    } else {
                        remove(filename);
                    }
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", filename);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            }
        } else {
            uiDrawString("Error: not enough free space available in the SD card.", 0, breaks * font_height, 255, 0, 0);
        }
    }
    
    breaks += 2;
    
    return success;
}

bool dumpRawPartition(FsDeviceOperator* fsOperator, u32 partition, bool doSplitting)
{
    Result result;
    u64 size, partitionOffset;
    bool success = false;
    char *buf;
    u64 off, n = DUMP_BUFFER_SIZE;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    char totalSizeStr[32] = {'\0'}, curSizeStr[32] = {'\0'}, filename[NAME_BUF_LEN] = {'\0'};
    u8 progress = 0;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    
    u64 start, now, remainingTime;
    struct tm *timeinfo;
    char etaInfo[32] = {'\0'};
    double lastSpeed = 0.0, averageSpeed = 0.0;
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
    {
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;*/
        
        // Ugly hack
        // The IStorage instance returned for partition == 0 contains the gamecard header, the gamecard certificate, the root HFS0 header and:
        // * The "update" (0) partition and the "normal" (1) partition (for gamecard type 0x01)
        // * The "update" (0) partition, the "logo" (1) partition and the "normal" (2) partition (for gamecard type 0x02)
        // The IStorage instance returned for partition == 1 contains the "secure" partition (which can either be 2 or 3 depending on the gamecard type)
        // This ugly hack makes sure we just dump the *actual* raw HFS0 partition, without preceding data, padding, etc.
        // Oddly enough, IFileSystem instances actually point to the specified partition ID filesystem. I don't understand why it doesn't work like that for IStorage, but whatever
        // NOTE: Using partition == 2 returns error 0x149002, and using higher values probably do so, too
        
        if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? (partition < 2 ? 0 : 1) : (partition < 3 ? 0 : 1)))))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            if (getHsf0PartitionDetails(partition, &partitionOffset, &size))
            {
                convertSize(size, totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition size: %s (%lu bytes)", totalSizeStr, size);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition offset (relative to IStorage instance): 0x%016lX", partitionOffset);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                if (size <= freeSpace)
                {
                    if (size > SPLIT_FILE_MIN && doSplitting)
                    {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX) - Partition %u (%s).hfs0.%02u", fixedGameCardName, gameCardVersion, gameCardTitleID, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                    } else {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX) - Partition %u (%s).hfs0", fixedGameCardName, gameCardVersion, gameCardTitleID, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    }
                    
                    outFile = fopen(filename, "wb");
                    if (outFile)
                    {
                        buf = (char*)malloc(DUMP_BUFFER_SIZE);
                        if (buf)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping raw HFS0 partition #%u to \"%.*s\". Hold B to cancel.", partition, (int)((size > SPLIT_FILE_MIN && doSplitting) ? (strlen(filename) - 3) : strlen(filename)), filename);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks += 2;
                            
                            uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                            breaks += 2;
                            
                            uiRefreshDisplay();
                            
                            timeGetCurrentTime(TimeType_LocalSystemClock, &start);
                            
                            for (off = 0; off < size; off += n)
                            {
                                if (DUMP_BUFFER_SIZE > (size - off)) n = (size - off);
                                
                                if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset + off, buf, n)))
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionOffset + off);
                                    uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                    break;
                                }
                                
                                if (size > SPLIT_FILE_MIN && doSplitting && (off + n) < size && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_2GiB))
                                {
                                    u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_2GiB));
                                    u64 old_file_chunk_size = (n - new_file_chunk_size);
                                    
                                    if (old_file_chunk_size > 0)
                                    {
                                        if (fwrite(buf, 1, old_file_chunk_size, outFile) != old_file_chunk_size)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", off);
                                            uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                            break;
                                        }
                                    }
                                    
                                    fclose(outFile);
                                    
                                    splitIndex++;
                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX) - Partition %u (%s).hfs0.%02u", fixedGameCardName, gameCardVersion, gameCardTitleID, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                                    
                                    outFile = fopen(filename, "wb");
                                    if (!outFile)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                        uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                        break;
                                    }
                                    
                                    if (new_file_chunk_size > 0)
                                    {
                                        if (fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile) != new_file_chunk_size)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", off + old_file_chunk_size);
                                            uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                            break;
                                        }
                                    }
                                } else {
                                    if (fwrite(buf, 1, n, outFile) != n)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", off);
                                        uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                        break;
                                    }
                                }
                                
                                timeGetCurrentTime(TimeType_LocalSystemClock, &now);
                                
                                lastSpeed = (((double)(off + n) / (double)DUMP_BUFFER_SIZE) / difftime((time_t)now, (time_t)start));
                                averageSpeed = ((SMOOTHING_FACTOR * lastSpeed) + ((1 - SMOOTHING_FACTOR) * averageSpeed));
                                if (!isnormal(averageSpeed)) averageSpeed = 0.00; // Very low values
                                
                                remainingTime = (u64)(((double)(size - (off + n)) / (double)DUMP_BUFFER_SIZE) / averageSpeed);
                                timeinfo = localtime((time_t*)&remainingTime);
                                strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
                                
                                progress = (u8)(((off + n) * 100) / size);
                                
                                uiFill(0, (breaks * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%.2lf MiB/s [ETA: %s]", averageSpeed, etaInfo);
                                uiDrawString(strbuf, font_height * 2, breaks * font_height, 255, 255, 255);
                                
                                uiFill(FB_WIDTH / 4, (breaks * font_height) + 2, FB_WIDTH / 2, font_height, 0, 0, 0);
                                uiFill(FB_WIDTH / 4, (breaks * font_height) + 2, (((off + n) * (FB_WIDTH / 2)) / size), font_height, 0, 255, 0);
                                
                                uiFill(FB_WIDTH - (FB_WIDTH / 4), (breaks * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                convertSize(off + n, curSizeStr, sizeof(curSizeStr) / sizeof(curSizeStr[0]));
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progress, curSizeStr, totalSizeStr);
                                uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), breaks * font_height, 255, 255, 255);
                                
                                uiRefreshDisplay();
                                
                                if ((off + n) < size && ((off / DUMP_BUFFER_SIZE) % 10) == 0)
                                {
                                    hidScanInput();
                                    
                                    u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                                    if (keysDown & KEY_B)
                                    {
                                        uiDrawString("Process canceled", 0, (breaks + 3) * font_height, 255, 0, 0);
                                        break;
                                    }
                                }
                            }
                            
                            if (off >= size) success = true;
                            
                            // Support empty files
                            if (!size)
                            {
                                uiFill(FB_WIDTH / 4, (breaks * font_height) + 2, FB_WIDTH / 2, font_height, 0, 255, 0);
                                
                                uiFill(FB_WIDTH - (FB_WIDTH / 4), (breaks * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                uiDrawString("100%% [0 B / 0 B]", FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), breaks * font_height, 255, 255, 255);
                                
                                uiRefreshDisplay();
                            }
                            
                            if (success)
                            {
                                now -= start;
                                timeinfo = localtime((time_t*)&now);
                                strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", etaInfo);
                                uiDrawString(strbuf, 0, (breaks + 3) * font_height, 0, 255, 0);
                            } else {
                                uiFill(FB_WIDTH / 4, (breaks * font_height) + 2, (((off + n) * (FB_WIDTH / 2)) / size), font_height, 255, 0, 0);
                            }
                            
                            breaks += 3;
                            
                            free(buf);
                        } else {
                            uiDrawString("Failed to allocate memory for the dump process!", 0, breaks * font_height, 255, 0, 0);
                        }
                        
                        if (outFile) fclose(outFile);
                        
                        if (!success)
                        {
                            if (size > SPLIT_FILE_MIN && doSplitting)
                            {
                                for(u8 i = 0; i <= splitIndex; i++)
                                {
                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX) - Partition %u (%s).hfs0.%02u", fixedGameCardName, gameCardVersion, gameCardTitleID, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), i);
                                    remove(filename);
                                }
                            } else {
                                remove(filename);
                            }
                        }
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", filename);
                        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                    }
                } else {
                    uiDrawString("Error: not enough free space available in the SD card.", 0, breaks * font_height, 255, 0, 0);
                }
            } else {
                uiDrawString("Error: unable to get partition details from the root HFS0 header!", 0, breaks * font_height, 255, 0, 0);
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
    
    breaks += 2;
    
    return success;
}

bool openPartitionFs(FsFileSystem* ret, FsDeviceOperator* fsOperator, u32 partition)
{
    FsGameCardHandle handle;
    Result result;
    bool success = false;
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
    {
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;*/
        
        if (R_SUCCEEDED(result = fsOpenGameCardFileSystem(ret, &handle, partition)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardFileSystem succeeded: 0x%08X", result);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            success = true;
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardFileSystem failed! (0x%08X)", result);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            breaks += 2;
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        breaks += 2;
    }
    
    return success;
}

bool copyFile(const char* source, const char* dest, bool doSplitting, bool calcEta)
{
    bool success = false;
    char splitFilename[NAME_BUF_LEN] = {'\0'};
    size_t destLen = strlen(dest);
    FILE *inFile, *outFile;
    char *buf = NULL;
    u64 size, off, n = DUMP_BUFFER_SIZE;
    u8 splitIndex = 0;
    u8 progress = 0;
    char totalSizeStr[32] = {'\0'}, curSizeStr[32] = {'\0'};
    
    u64 start, now, remainingTime;
    struct tm *timeinfo;
    char etaInfo[32] = {'\0'};
    double lastSpeed = 0.0, averageSpeed = 0.0;
    
    uiFill(0, breaks * font_height, FB_WIDTH, font_height * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"%s\"...", source);
    uiDrawString(strbuf, 0, (breaks + 1) * font_height, 255, 255, 255);
    
    if ((destLen + 1) < NAME_BUF_LEN)
    {
        inFile = fopen(source, "rb");
        if (inFile)
        {
            fseek(inFile, 0L, SEEK_END);
            size = ftell(inFile);
            rewind(inFile);
            
            convertSize(size, totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]));
            
            if (size > SPLIT_FILE_MIN && doSplitting) snprintf(splitFilename, sizeof(splitFilename) / sizeof(splitFilename[0]), "%s.%02u", dest, splitIndex);
            
            outFile = fopen(((size > SPLIT_FILE_MIN && doSplitting) ? splitFilename : dest), "wb");
            if (outFile)
            {
                buf = (char*)malloc(DUMP_BUFFER_SIZE);
                if (buf)
                {
                    if (calcEta) timeGetCurrentTime(TimeType_LocalSystemClock, &start);
                    
                    for (off = 0; off < size; off += n)
                    {
                        if (DUMP_BUFFER_SIZE > (size - off)) n = (size - off);
                        
                        if (fread(buf, 1, n, inFile) != n)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read chunk from offset 0x%016lX", off);
                            uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                            break;
                        }
                        
                        if (size > SPLIT_FILE_MIN && doSplitting && (off + n) < size && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_2GiB))
                        {
                            u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_2GiB));
                            u64 old_file_chunk_size = (n - new_file_chunk_size);
                            
                            if (old_file_chunk_size > 0)
                            {
                                if (fwrite(buf, 1, old_file_chunk_size, outFile) != old_file_chunk_size)
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", off);
                                    uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                    break;
                                }
                            }
                            
                            fclose(outFile);
                            
                            splitIndex++;
                            snprintf(splitFilename, sizeof(splitFilename) / sizeof(splitFilename[0]), "%s.%02u", dest, splitIndex);
                            
                            outFile = fopen(splitFilename, "wb");
                            if (!outFile)
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                break;
                            }
                            
                            if (new_file_chunk_size > 0)
                            {
                                if (fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile) != new_file_chunk_size)
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", off + old_file_chunk_size);
                                    uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                    break;
                                }
                            }
                        } else {
                            if (fwrite(buf, 1, n, outFile) != n)
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write chunk to offset 0x%016lX", off);
                                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                break;
                            }
                        }
                        
                        if (calcEta)
                        {
                            timeGetCurrentTime(TimeType_LocalSystemClock, &now);
                            
                            lastSpeed = (((double)(off + n) / (double)DUMP_BUFFER_SIZE) / difftime((time_t)now, (time_t)start));
                            averageSpeed = ((SMOOTHING_FACTOR * lastSpeed) + ((1 - SMOOTHING_FACTOR) * averageSpeed));
                            if (!isnormal(averageSpeed)) averageSpeed = 0.00; // Very low values
                            
                            remainingTime = (u64)(((double)(size - (off + n)) / (double)DUMP_BUFFER_SIZE) / averageSpeed);
                            timeinfo = localtime((time_t*)&remainingTime);
                            strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
                            
                            uiFill(0, ((breaks + 3) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%.2lf MiB/s [ETA: %s]", averageSpeed, etaInfo);
                            uiDrawString(strbuf, font_height * 2, (breaks + 3) * font_height, 255, 255, 255);
                        }
                        
                        progress = (u8)(((off + n) * 100) / size);
                        
                        uiFill(FB_WIDTH / 4, ((breaks + 3) * font_height) + 2, FB_WIDTH / 2, font_height, 0, 0, 0);
                        uiFill(FB_WIDTH / 4, ((breaks + 3) * font_height) + 2, (((off + n) * (FB_WIDTH / 2)) / size), font_height, 0, 255, 0);
                        
                        uiFill(FB_WIDTH - (FB_WIDTH / 4), ((breaks + 3) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                        convertSize(off + n, curSizeStr, sizeof(curSizeStr) / sizeof(curSizeStr[0]));
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progress, curSizeStr, totalSizeStr);
                        uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (breaks + 3) * font_height, 255, 255, 255);
                        
                        uiRefreshDisplay();
                        
                        if ((off + n) < size && ((off / DUMP_BUFFER_SIZE) % 10) == 0)
                        {
                            hidScanInput();
                            
                            u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                            if (keysDown & KEY_B)
                            {
                                uiDrawString("Process canceled", 0, (breaks + 5) * font_height, 255, 0, 0);
                                break;
                            }
                        }
                    }
                    
                    if (off >= size) success = true;
                    
                    // Support empty files
                    if (!size)
                    {
                        uiFill(FB_WIDTH / 4, ((breaks + 3) * font_height) + 2, FB_WIDTH / 2, font_height, 0, 255, 0);
                        
                        uiFill(FB_WIDTH - (FB_WIDTH / 4), ((breaks + 3) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                        uiDrawString("100%% [0 B / 0 B]", FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (breaks + 3) * font_height, 255, 255, 255);
                        
                        uiRefreshDisplay();
                    }
                    
                    if (!success)
                    {
                        uiFill(FB_WIDTH / 4, ((breaks + 3) * font_height) + 2, (((off + n) * (FB_WIDTH / 2)) / size), font_height, 255, 0, 0);
                        breaks += 5;
                    }
                    
                    free(buf);
                } else {
                    breaks += 3;
                    uiDrawString("Failed to allocate memory for the dump process!", 0, breaks * font_height, 255, 0, 0);
                }
                
                if (outFile) fclose(outFile);
                
                if (success)
                {
                    if (calcEta)
                    {
                        breaks += 5;
                        
                        now -= start;
                        timeinfo = localtime((time_t*)&now);
                        strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", etaInfo);
                        uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                    }
                } else {
                    if (size > SPLIT_FILE_MIN && doSplitting)
                    {
                        for(u8 i = 0; i <= splitIndex; i++)
                        {
                            snprintf(splitFilename, sizeof(splitFilename) / sizeof(splitFilename[0]), "%s.%02u", dest, i);
                            remove(splitFilename);
                        }
                    } else {
                        remove(dest);
                    }
                }
            } else {
                breaks += 3;
                uiDrawString("Failed to open output file!", 0, breaks * font_height, 255, 255, 255);
            }
            
            fclose(inFile);
        } else {
            breaks += 3;
            uiDrawString("Failed to open input file!", 0, breaks * font_height, 255, 255, 255);
        }
    } else {
        breaks += 3;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination path is too long! \"%s\" (%lu)", dest, destLen);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
    }
    
    return success;
}

bool _copyDirectory(char* sbuf, size_t source_len, char* dbuf, size_t dest_len, bool splitting)
{
    struct dirent* ent;
    bool success = true;
    
    DIR *dir = opendir(sbuf);
    if (dir)
    {
        sbuf[source_len] = '/';
        dbuf[dest_len] = '/';
        
        while ((ent = readdir(dir)) != NULL)
        {
            if ((strlen(ent->d_name) == 1 && !strcmp(ent->d_name, ".")) || (strlen(ent->d_name) == 2 && !strcmp(ent->d_name, ".."))) continue;
            
            size_t d_name_len = strlen(ent->d_name);
            
            if ((source_len + 1 + d_name_len + 1) >= NAME_BUF_LEN || (dest_len + 1 + d_name_len + 1) >= NAME_BUF_LEN)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Filename too long! \"%s\" (%lu)", ent->d_name, d_name_len);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                breaks++;
                success = false;
                break;
            }
            
            strcpy(sbuf + source_len + 1, ent->d_name);
            strcpy(dbuf + dest_len + 1, ent->d_name);
            
            if (ent->d_type == DT_DIR)
            {
                mkdir(dbuf, 0744);
                if (!_copyDirectory(sbuf, source_len + 1 + d_name_len, dbuf, dest_len + 1 + d_name_len, splitting))
                {
                    success = false;
                    break;
                }
            } else {
                if (!copyFile(sbuf, dbuf, splitting, false))
                {
                    success = false;
                    break;
                }
            }
        }
        
        closedir(dir);
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error opening directory \"%s\"", dbuf);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        breaks++;
        success = false;
    }
    
    return success;
}

bool copyDirectory(const char* source, const char* dest, bool splitting)
{
    char sbuf[NAME_BUF_LEN], dbuf[NAME_BUF_LEN];
    size_t source_len = strlen(source), dest_len = strlen(dest);
    
    if (source_len + 1 >= NAME_BUF_LEN)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Source directory name too long! \"%s\" (%lu)", source, source_len);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        breaks++;
        return false;
    }
    
    if (dest_len + 1 >= NAME_BUF_LEN)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination directory name too long! \"%s\" (%lu)", dest, dest_len);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        breaks++;
        return false;
    }
    
    strcpy(sbuf, source);
    strcpy(dbuf, dest);
    
    mkdir(dbuf, 0744);
    
    return _copyDirectory(sbuf, source_len, dbuf, dest_len, splitting);
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

bool getDirectorySize(const char *path, u64 *out_size)
{
    struct dirent* ent;
    char cur_path[NAME_BUF_LEN] = {'\0'};
    bool success = true;
    u64 total_size = 0, dir_size = 0;
    FILE *file = NULL;
    
    DIR *dir = opendir(path);
    if (dir)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            if ((strlen(ent->d_name) == 1 && !strcmp(ent->d_name, ".")) || (strlen(ent->d_name) == 2 && !strcmp(ent->d_name, ".."))) continue;
            
            snprintf(cur_path, sizeof(cur_path) / sizeof(cur_path[0]), "%s/%s", path, ent->d_name);
            
            if (ent->d_type == DT_DIR)
            {
                if (getDirectorySize(cur_path, &dir_size))
                {
                    total_size += dir_size;
                    dir_size = 0;
                } else {
                    success = false;
                    break;
                }
            } else {
                file = fopen(cur_path, "rb");
                if (file)
                {
                    fseek(file, 0L, SEEK_END);
                    total_size += ftell(file);
                    
                    fclose(file);
                    file = NULL;
                } else {
                    success = false;
                    break;
                }
            }
        }
        
        closedir(dir);
    } else {
        success = false;
    }
    
    if (success) *out_size = total_size;
    
    return success;
}

bool dumpPartitionData(FsDeviceOperator* fsOperator, u32 partition)
{
    FsFileSystem fs;
    int ret;
    bool success = false;
    u64 total_size;
    char dumpPath[NAME_BUF_LEN] = {'\0'}, totalSizeStr[32] = {'\0'};
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (openPartitionFs(&fs, fsOperator, partition))
    {
        ret = fsdevMountDevice("gamecard", fs);
        if (ret != -1)
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "fsdevMountDevice succeeded: %d", ret);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            if (getDirectorySize("gamecard:/", &total_size))
            {
                convertSize(total_size, totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Total partition data size: %s (%lu bytes)", totalSizeStr, total_size);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                if (total_size <= freeSpace)
                {
                    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s v%u (%016lX) - Partition %u (%s)", fixedGameCardName, gameCardVersion, gameCardTitleID, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying partition #%u data to \"%s/\". Hold B to cancel.", partition, dumpPath);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                    breaks += 2;
                    
                    uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                    breaks += 2;
                    
                    if (copyDirectory("gamecard:/", dumpPath, true))
                    {
                        success = true;
                        breaks += 5;
                        uiDrawString("Process successfully completed!", 0, breaks * font_height, 0, 255, 0);
                    } else {
                        removeDirectory(dumpPath);
                    }
                } else {
                    uiDrawString("Error: not enough free space available in the SD card.", 0, breaks * font_height, 255, 0, 0);
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to get total partition data size!");
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            }
            
            fsdevUnmountDevice("gamecard");
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "fsdevMountDevice failed! (%d)", ret);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        }
        
        fsFsClose(&fs);
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open partition #%u filesystem!", partition);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
    }
    
    breaks += 2;
    
    return success;
}

bool mountViewPartition(FsDeviceOperator *fsOperator, FsFileSystem *out, u32 partition)
{
    int ret;
    bool success = false;
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (openPartitionFs(out, fsOperator, partition))
    {
        ret = fsdevMountDevice("view", *out);
        if (ret != -1)
        {
            //snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "fsdevMountDevice succeeded: %d", ret);
            //uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            //breaks++;
            
            success = true;
        } else {
            fsFsClose(out);
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "fsdevMountDevice failed! (%d)", ret);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open partition #%u filesystem!", partition);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
    }
    
    return success;
}

bool dumpGameCertificate(FsDeviceOperator* fsOperator)
{
    u32 crc;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    bool success = false;
    FILE *outFile = NULL;
    char filename[NAME_BUF_LEN] = {'\0'};
    char *buf = NULL;
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
    {
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;*/
        
        if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, 0)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            if (CERT_SIZE <= freeSpace)
            {
                buf = (char*)malloc(DUMP_BUFFER_SIZE);
                if (buf)
                {
                    if (R_SUCCEEDED(result = fsStorageRead(&gameCardStorage, CERT_OFFSET, buf, CERT_SIZE)))
                    {
                        // Calculate CRC32
                        crc32(buf, CERT_SIZE, &crc);
                        
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s v%u (%016lX) - Certificate (%08X).bin", fixedGameCardName, gameCardVersion, gameCardTitleID, crc);
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping game card certificate to file \"%s\"...", filename);
                        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                        breaks++;
                        
                        uiRefreshDisplay();
                        
                        outFile = fopen(filename, "wb");
                        if (outFile)
                        {
                            if (fwrite(buf, 1, CERT_SIZE, outFile) == CERT_SIZE)
                            {
                                success = true;
                                uiDrawString("Process successfully completed!", 0, breaks * font_height, 0, 255, 0);
                            } else {
                                uiDrawString("Failed to write certificate data!", 0, breaks * font_height, 255, 0, 0);
                            }
                            
                            fclose(outFile);
                            if (!success) remove(filename);
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", filename);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                        }
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%08X", result, CERT_OFFSET);
                        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                    }
                    
                    free(buf);
                } else {
                    uiDrawString("Failed to allocate memory for the dump process!", 0, breaks * font_height, 255, 0, 0);
                }
            } else {
                uiDrawString("Error: not enough free space available in the SD card.", 0, breaks * font_height, 255, 0, 0);
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
    
    breaks += 2;
    
    return success;
}
