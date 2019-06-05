#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <memory.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>

#include "crc32_fast.h"
#include "dumper.h"
#include "fs_ext.h"
#include "ui.h"
#include "nca.h"

/* Extern variables */

extern u64 freeSpace;

extern int breaks;
extern int font_height;

extern u64 trimmedCardSize;
extern char trimmedCardSizeStr[32];

extern u8 *hfs0_header;
extern u64 hfs0_offset, hfs0_size;
extern u32 hfs0_partition_cnt;

extern u8 *partitionHfs0Header;
extern u64 partitionHfs0HeaderOffset, partitionHfs0HeaderSize;
extern u32 partitionHfs0FileCount, partitionHfs0StrTableSize;

extern u32 gameCardAppCount;
extern u64 *gameCardTitleID;
extern u32 *gameCardVersion;
extern char **fixedGameCardName;

extern u32 gameCardPatchCount;

extern u32 gameCardAddOnCount;

extern AppletType programAppletType;

extern romfs_ctx_t romFsContext;

extern char curRomFsPath[NAME_BUF_LEN];

extern char strbuf[NAME_BUF_LEN * 4];

/* Statically allocated variables */

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator)
{
    FsGameCardHandle handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle))) return;
    
    FsStorage gameCardStorage;
    if (R_FAILED(fsOpenGameCardStorage(&gameCardStorage, &handle, 0))) return;
    
    fsStorageClose(&gameCardStorage);
}

bool dumpCartridgeImage(FsDeviceOperator* fsOperator, bool isFat32, bool setXciArchiveBit, bool dumpCert, bool trimDump, bool calcCrc)
{
    u64 partitionOffset = 0, xciDataSize = 0, n;
    u64 partitionSizes[ISTORAGE_PARTITION_CNT];
    char partitionSizesStr[ISTORAGE_PARTITION_CNT][32] = {'\0'}, xciDataSizeStr[32] = {'\0'}, filename[NAME_BUF_LEN * 2] = {'\0'};
    u32 partition;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    bool proceed = true, success = false, fat32_error = false;
    FILE *outFile = NULL;
    u8 *buf = NULL;
    u8 splitIndex = 0;
    u32 crc1 = 0, crc2 = 0;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char tmp_idx[5];
    
    size_t write_res;
    
    char *dumpName = generateDumpFullName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    for(partition = 0; partition < ISTORAGE_PARTITION_CNT; partition++)
    {
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Getting partition #%u size...", partition);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;*/
        
        workaroundPartitionZeroAccess(fsOperator);
        
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            breaks++;*/
            
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, partition)))
            {
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks++;*/
                
                if (R_SUCCEEDED(result = fsStorageGetSize(&gameCardStorage, &(partitionSizes[partition]))))
                {
                    xciDataSize += partitionSizes[partition];
                    convertSize(partitionSizes[partition], partitionSizesStr[partition], sizeof(partitionSizesStr[partition]) / sizeof(partitionSizesStr[partition][0]));
                    /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u size: %s (%lu bytes).", partition, partitionSizesStr[partition], partitionSizes[partition]);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                    breaks += 2;*/
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageGetSize failed! (0x%08X)", result);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    proceed = false;
                }
                
                fsStorageClose(&gameCardStorage);
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                proceed = false;
            }
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            proceed = false;
        }
        
        uiRefreshDisplay();
    }
    
    if (proceed)
    {
        convertSize(xciDataSize, xciDataSizeStr, sizeof(xciDataSizeStr) / sizeof(xciDataSizeStr[0]));
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI data size: %s (%lu bytes).", xciDataSizeStr, xciDataSize);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks += 2;*/
        
        if (trimDump)
        {
            progressCtx.totalSize = trimmedCardSize;
            snprintf(progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]), "%s", trimmedCardSizeStr);
            
            // Change dump size for the last IStorage partition
            u64 partitionSizesSum = 0;
            for(int i = 0; i < (ISTORAGE_PARTITION_CNT - 1); i++) partitionSizesSum += partitionSizes[i];
            
            partitionSizes[ISTORAGE_PARTITION_CNT - 1] = (trimmedCardSize - partitionSizesSum);
        } else {
            progressCtx.totalSize = xciDataSize;
            snprintf(progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]), "%s", xciDataSizeStr);
        }
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;
        
        if (progressCtx.totalSize <= freeSpace)
        {
            breaks++;
            
            if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
            {
                if (setXciArchiveBit)
                {
                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xci", dumpName);
                    
                    // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
                    // Better safe than sorry
                    unlink(filename);
                    removeDirectory(filename);
                    
                    mkdir(filename, 0744);
                    
                    sprintf(tmp_idx, "/%02u", splitIndex);
                    strcat(filename, tmp_idx);
                } else {
                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xc%u", dumpName, splitIndex);
                }
            } else {
                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xci", dumpName);
            }
            
            outFile = fopen(filename, "wb");
            if (outFile)
            {
                buf = malloc(DUMP_BUFFER_SIZE);
                if (buf)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                    breaks += 2;
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        breaks += 2;
                    }
                    
                    progressCtx.line_offset = (breaks + 4);
                    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
                    
                    for(partition = 0; partition < ISTORAGE_PARTITION_CNT; partition++)
                    {
                        n = DUMP_BUFFER_SIZE;
                        
                        workaroundPartitionZeroAccess(fsOperator);
                        
                        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
                        {
                            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, partition)))
                            {
                                for(partitionOffset = 0; partitionOffset < partitionSizes[partition]; partitionOffset += n, progressCtx.curOffset += n)
                                {
                                    uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                    
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", filename);
                                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                    
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping IStorage partition #%u...", partition);
                                    uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                    
                                    if (DUMP_BUFFER_SIZE > (partitionSizes[partition] - partitionOffset)) n = (partitionSizes[partition] - partitionOffset);
                                    
                                    if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset, buf, n)))
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX for partition #%u", result, partitionOffset, partition);
                                        uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                        proceed = false;
                                        break;
                                    }
                                    
                                    // Remove game card certificate
                                    if (progressCtx.curOffset == 0 && !dumpCert) memset(buf + CERT_OFFSET, 0xFF, CERT_SIZE);
                                    
                                    if (calcCrc)
                                    {
                                        if (!trimDump)
                                        {
                                            if (dumpCert)
                                            {
                                                if (progressCtx.curOffset == 0)
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
                                    
                                    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_XCI_PART_SIZE))
                                    {
                                        u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_XCI_PART_SIZE));
                                        u64 old_file_chunk_size = (n - new_file_chunk_size);
                                        
                                        if (old_file_chunk_size > 0)
                                        {
                                            write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                                            if (write_res != old_file_chunk_size)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                                                uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                proceed = false;
                                                break;
                                            }
                                        }
                                        
                                        fclose(outFile);
                                        outFile = NULL;
                                        
                                        if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                                        {
                                            splitIndex++;
                                            
                                            if (setXciArchiveBit)
                                            {
                                                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xci/%02u", dumpName, splitIndex);
                                            } else {
                                                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xc%u", dumpName, splitIndex);
                                            }
                                            
                                            outFile = fopen(filename, "wb");
                                            if (!outFile)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                                uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                proceed = false;
                                                break;
                                            }
                                            
                                            if (new_file_chunk_size > 0)
                                            {
                                                write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                                if (write_res != new_file_chunk_size)
                                                {
                                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                                                    uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                    proceed = false;
                                                    break;
                                                }
                                            }
                                        }
                                    } else {
                                        write_res = fwrite(buf, 1, n, outFile);
                                        if (write_res != n)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                                            uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            
                                            if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                                            {
                                                uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 8, ((breaks + 8) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                                fat32_error = true;
                                            }
                                            
                                            proceed = false;
                                            break;
                                        }
                                    }
                                    
                                    printProgressBar(&progressCtx, true, n);
                                    
                                    if ((progressCtx.curOffset + n) < progressCtx.totalSize && ((progressCtx.curOffset / DUMP_BUFFER_SIZE) % 10) == 0)
                                    {
                                        hidScanInput();
                                        
                                        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                                        if (keysDown & KEY_B)
                                        {
                                            uiDrawString("Process canceled.", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            proceed = false;
                                            break;
                                        }
                                    }
                                }
                                
                                if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
                                
                                // Support empty files
                                if (!partitionSizes[partition])
                                {
                                    uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                    
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", filename);
                                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                    
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping IStorage partition #%u...", partition);
                                    uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                    
                                    printProgressBar(&progressCtx, false, 0);
                                }
                                
                                fsStorageClose(&gameCardStorage);
                            } else {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed for partition #%u! (0x%08X)", partition, result);
                                uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                proceed = false;
                            }
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed for partition #%u! (0x%08X)", partition, result);
                            uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            proceed = false;
                        }
                        
                        if (!proceed)
                        {
                            setProgressBarError(&progressCtx);
                            if (fat32_error) breaks += 2;
                            break;
                        }
                    }
                    
                    free(buf);
                } else {
                    uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                }
                
                if (outFile) fclose(outFile);
                
                breaks += 6;
                
                if (success)
                {
                    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
                    progressCtx.now -= progressCtx.start;
                    
                    formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                    
                    if (calcCrc)
                    {
                        breaks++;
                        
                        if (!trimDump)
                        {
                            if (dumpCert)
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum (with certificate): %08X", crc1);
                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                                breaks++;
                                
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum (without certificate): %08X", crc2);
                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                            } else {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum: %08X", crc2);
                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                            }
                            
                            breaks += 2;
                            uiDrawString("Starting verification process using XML database from NSWDB.COM...", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                            breaks++;
                            
                            uiRefreshDisplay();
                            
                            gameCardDumpNSWDBCheck(crc2);
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI dump CRC32 checksum: %08X", crc1);
                            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                            breaks++;
                            
                            uiDrawString("Dump verification disabled (not compatible with trimmed dumps).", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        }
                    }
                    
                    // Set archive bit (only for FAT32 and if the required option is enabled)
                    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && setXciArchiveBit)
                    {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xci", dumpName);
                        if (R_FAILED(result = fsdevSetArchiveBit(filename)))
                        {
                            breaks += 2;
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Warning: failed to set archive bit on output directory! (0x%08X)", result);
                            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        }
                    }
                } else {
                    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
                    {
                        if (setXciArchiveBit)
                        {
                            snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xci", dumpName);
                            removeDirectory(filename);
                        } else {
                            for(u8 i = 0; i <= splitIndex; i++)
                            {
                                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xc%u", dumpName, i);
                                unlink(filename);
                            }
                        }
                    } else {
                        unlink(filename);
                    }
                }
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", filename);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
        } else {
            uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    }
    
    breaks += 2;
    
    free(dumpName);
    
    return success;
}

bool dumpNintendoSubmissionPackage(FsDeviceOperator* fsOperator, nspDumpType selectedNspDumpType, u32 titleIndex, bool isFat32, bool calcCrc)
{
    Result result;
    u32 i = 0, j = 0;
    u32 written = 0;
    u32 total = 0;
    u32 titleNcaCount = 0;
    u32 partition = (hfs0_partition_cnt - 1); // Select the secure partition
    
    FsGameCardHandle handle;
    
    FsStorage gameCardStorage;
    memset(&gameCardStorage, 0, sizeof(FsStorage));
    
    NcmContentMetaDatabase ncmDb;
    memset(&ncmDb, 0, sizeof(NcmContentMetaDatabase));
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    NcmApplicationContentMetaKey *titleList = NULL;
    NcmContentRecord *titleContentRecords = NULL;
    size_t titleListSize = sizeof(NcmApplicationContentMetaKey);
    titleListSize *= (selectedNspDumpType == DUMP_APP_NSP ? gameCardAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? gameCardPatchCount : gameCardAddOnCount));
    
    cnmt_xml_program_info xml_program_info;
    cnmt_xml_content_info *xml_content_info = NULL;
    
    NcmNcaId ncaId;
    u8 ncaHeader[NCA_FULL_HEADER_LENGTH] = {0};
    nca_header_t dec_nca_header;
    
    nca_program_mod_data ncaProgramMod;
    memset(&ncaProgramMod, 0, sizeof(nca_program_mod_data));
    
    ncaProgramMod.hash_table = NULL;
    ncaProgramMod.block_data[0] = NULL;
    ncaProgramMod.block_data[1] = NULL;
    
    nca_cnmt_mod_data ncaCnmtMod;
    memset(&ncaCnmtMod, 0, sizeof(nca_cnmt_mod_data));
    
    title_rights_ctx rights_info;
    memset(&rights_info, 0, sizeof(title_rights_ctx));
    
    u8 *tikData = NULL;
    u64 tikSize = 0;
    
    u8 *certData = NULL;
    u64 certSize = 0;
    
    u32 cnmtNcaIndex = 0;
    u8 *cnmtNcaBuf = NULL;
    bool cnmtFound = false;
    char *cnmtXml = NULL;
    
    u32 nacpNcaIndex = 0;
    char *nacpXml = NULL;
    
    u32 nspFileCount = 0;
    pfs0_header nspPfs0Header;
    pfs0_entry_table *nspPfs0EntryTable = NULL;
    char *nspPfs0StrTable = NULL;
    u64 nspPfs0StrTableSize = 0;
    u64 full_nsp_header_size = 0;
    
    u64 hash_table_dump_buffer_start = 0;
    u64 hash_table_dump_buffer_end = 0;
    u64 block0_dump_buffer_start = 0;
    u64 block0_dump_buffer_end = 0;
    u64 block1_dump_buffer_start = 0;
    u64 block1_dump_buffer_end = 0;
    
    Sha256Context nca_hash_ctx;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    u64 n, nca_offset;
    FILE *outFile = NULL;
    u8 *buf = NULL;
    u8 splitIndex = 0;
    u32 crc = 0;
    bool proceed = true, success = false, dumping = false, fat32_error = false;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char tmp_idx[5];
    
    size_t write_res;
    
    int initial_breaks = breaks;
    
    if ((selectedNspDumpType == DUMP_APP_NSP && !gameCardAppCount) || (selectedNspDumpType == DUMP_PATCH_NSP && !gameCardPatchCount) || (selectedNspDumpType == DUMP_ADDON_NSP && !gameCardAddOnCount))
    {
        uiDrawString("Error: invalid title type count!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if ((selectedNspDumpType == DUMP_APP_NSP && titleIndex > (gameCardAppCount - 1)) || (selectedNspDumpType == DUMP_PATCH_NSP && titleIndex > (gameCardPatchCount - 1)) || (selectedNspDumpType == DUMP_ADDON_NSP && titleIndex > (gameCardAddOnCount - 1)))
    {
        uiDrawString("Error: invalid title index!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName(selectedNspDumpType, titleIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (!getPartitionHfs0Header(partition)) return false;
    
    if (!partitionHfs0FileCount)
    {
        uiDrawString("The Secure HFS0 partition is empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    uiDrawString("Retrieving information from encrypted NCA content files...", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    titleList = calloc(1, titleListSize);
    if (!titleList)
    {
        uiDrawString("Error: unable to allocate memory for the ApplicationContentMetaKey struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentMetaDatabase(FsStorageId_GameCard, &ncmDb)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmOpenContentMetaDatabase failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    u8 filter = ((u8)selectedNspDumpType + META_DB_REGULAR_APPLICATION);
    
    if (R_FAILED(result = ncmContentMetaDatabaseListApplication(&ncmDb, filter, titleList, titleListSize, &written, &total)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseListApplication failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (!written || !total)
    {
        uiDrawString("Error: ncmContentMetaDatabaseListApplication wrote no entries to output buffer!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (written != total || (selectedNspDumpType == DUMP_APP_NSP && written != gameCardAppCount) || (selectedNspDumpType == DUMP_PATCH_NSP && written != gameCardPatchCount) || (selectedNspDumpType == DUMP_ADDON_NSP && written != gameCardAddOnCount))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: title count mismatch in ncmContentMetaDatabaseListApplication (%u != %u)", written, (selectedNspDumpType == DUMP_APP_NSP ? gameCardAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? gameCardPatchCount : gameCardAddOnCount)));
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    titleContentRecords = calloc(partitionHfs0FileCount, sizeof(NcmContentRecord));
    if (!titleContentRecords)
    {
        uiDrawString("Error: unable to allocate memory for the ContentRecord struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseListContentInfo(&ncmDb, &(titleList[titleIndex].metaRecord), 0, titleContentRecords, partitionHfs0FileCount * sizeof(NcmContentRecord), &written)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseListContentInfo failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    titleNcaCount = written;
    
    // Fill information for our CNMT XML
    memset(&xml_program_info, 0, sizeof(cnmt_xml_program_info));
    xml_program_info.type = titleList[titleIndex].metaRecord.type;
    xml_program_info.title_id = titleList[titleIndex].metaRecord.titleId;
    xml_program_info.version = titleList[titleIndex].metaRecord.version;
    xml_program_info.nca_cnt = titleNcaCount;
    
    xml_content_info = calloc(titleNcaCount, sizeof(cnmt_xml_content_info));
    if (!xml_content_info)
    {
        uiDrawString("Error: unable to allocate memory for the CNMT XML content info struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentStorage(FsStorageId_GameCard, &ncmStorage)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmOpenContentStorage failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Fill our CNMT XML content records, leaving the CNMT NCA at the end
    u32 titleRecordIndex;
    for(i = 0, titleRecordIndex = 0; titleRecordIndex < titleNcaCount; i++, titleRecordIndex++)
    {
        if (!cnmtFound && titleContentRecords[titleRecordIndex].type == NcmContentType_CNMT)
        {
            cnmtFound = true;
            cnmtNcaIndex = titleRecordIndex;
            i--;
            continue;
        }
        
        // Skip Delta Fragments or any other unknown content types
        if (titleContentRecords[titleRecordIndex].type >= NCA_CONTENT_TYPE_DELTA)
        {
            xml_program_info.nca_cnt--;
            i--;
            continue;
        }
        
        // Fill information for our CNMT XML
        xml_content_info[i].type = titleContentRecords[titleRecordIndex].type;
        memcpy(xml_content_info[i].nca_id, titleContentRecords[titleRecordIndex].ncaId.c, 16); // Temporary
        convertDataToHexString(titleContentRecords[titleRecordIndex].ncaId.c, 16, xml_content_info[i].nca_id_str, 33); // Temporary
        convertNcaSizeToU64(titleContentRecords[titleRecordIndex].size, &(xml_content_info[i].size));
        convertDataToHexString(xml_content_info[i].hash, 32, xml_content_info[i].hash_str, 65); // Temporary
        
        memcpy(&ncaId, &(titleContentRecords[titleRecordIndex].ncaId), sizeof(NcmNcaId));
        
        if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, 0, ncaHeader, NCA_FULL_HEADER_LENGTH)))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentStorageReadContentIdFile failed for NCA \"%s\"! (0x%08X)", xml_content_info[i].nca_id_str, result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            proceed = false;
            break;
        }
        
        // Decrypt the NCA header
        if (!decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header, &rights_info, xml_content_info[i].decrypted_nca_keys))
        {
            proceed = false;
            break;
        }
        
        bool has_rights_id = false;
        
        for(j = 0; j < 0x10; j++)
        {
            if (dec_nca_header.rights_id[j] != 0)
            {
                has_rights_id = true;
                break;
            }
        }
        
        if (selectedNspDumpType == DUMP_APP_NSP || selectedNspDumpType == DUMP_ADDON_NSP) 
        {
            if (has_rights_id)
            {
                uiDrawString("Error: Rights ID field in NCA header not empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                proceed = false;
                break;
            }
            
            // Fill information for our CNMT XML
            xml_content_info[i].keyblob = (dec_nca_header.crypto_type2 > dec_nca_header.crypto_type ? dec_nca_header.crypto_type2 : dec_nca_header.crypto_type);
            
            // Modify distribution type
            dec_nca_header.distribution = 0;
            
            // Patch ACID pubkey and recreate NCA NPDM signature if we're dealing with the Program NCA
            if (xml_content_info[i].type == NcmContentType_Program)
            {
                if (!processProgramNca(&ncmStorage, &ncaId, &dec_nca_header, &(xml_content_info[i]), &ncaProgramMod))
                {
                    proceed = false;
                    break;
                }
            }
        } else
        if (selectedNspDumpType == DUMP_PATCH_NSP)
        {
            if (has_rights_id)
            {
                // Fill information for our CNMT XML
                xml_content_info[i].keyblob = dec_nca_header.rights_id[15];
            } else {
                // Fill information for our CNMT XML
                xml_content_info[i].keyblob = (dec_nca_header.crypto_type2 > dec_nca_header.crypto_type ? dec_nca_header.crypto_type2 : dec_nca_header.crypto_type);
            }
        }
        
        // Generate NACP XML
        if (!nacpXml && xml_content_info[i].type == NcmContentType_Icon)
        {
            nacpNcaIndex = i;
            
            if (!generateNacpXmlFromNca(&ncmStorage, &ncaId, &dec_nca_header, xml_content_info[i].decrypted_nca_keys, &nacpXml))
            {
                proceed = false;
                break;
            }
        }
        
        // Reencrypt header
        if (!encryptNcaHeader(&dec_nca_header, xml_content_info[i].encrypted_header_mod, NCA_FULL_HEADER_LENGTH))
        {
            proceed = false;
            break;
        }
    }
    
    if (proceed && !cnmtFound)
    {
        uiDrawString("Error: unable to find CNMT NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (!proceed) goto out;
    
    // Update NCA counter just in case we found any delta fragments
    titleNcaCount = xml_program_info.nca_cnt;
    
    // Fill information for our CNMT XML
    xml_content_info[titleNcaCount - 1].type = titleContentRecords[cnmtNcaIndex].type;
    memcpy(xml_content_info[titleNcaCount - 1].nca_id, titleContentRecords[cnmtNcaIndex].ncaId.c, 16); // Temporary
    convertDataToHexString(titleContentRecords[cnmtNcaIndex].ncaId.c, 16, xml_content_info[titleNcaCount - 1].nca_id_str, 33); // Temporary
    convertNcaSizeToU64(titleContentRecords[cnmtNcaIndex].size, &(xml_content_info[titleNcaCount - 1].size));
    convertDataToHexString(xml_content_info[titleNcaCount - 1].hash, 32, xml_content_info[titleNcaCount - 1].hash_str, 65); // Temporary
    
    memcpy(&ncaId, &(titleContentRecords[cnmtNcaIndex].ncaId), sizeof(NcmNcaId));
    
    // Update CNMT index
    cnmtNcaIndex = (titleNcaCount - 1);
    
    cnmtNcaBuf = malloc(xml_content_info[cnmtNcaIndex].size);
    if (!cnmtNcaBuf)
    {
        uiDrawString("Error: unable to allocate memory for CNMT NCA data!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, 0, cnmtNcaBuf, xml_content_info[cnmtNcaIndex].size)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentStorageReadContentIdFile failed for CNMT NCA \"%s\"! (0x%08X)", xml_content_info[cnmtNcaIndex].nca_id_str, result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Retrieve CNMT NCA data
    if (!retrieveCnmtNcaData(selectedNspDumpType, cnmtNcaBuf, &xml_program_info, &(xml_content_info[cnmtNcaIndex]), &ncaCnmtMod, &rights_info)) goto out;
    
    // Generate a placeholder CNMT XML. It's length will be used to calculate the final output dump size
    /*breaks++;
    uiDrawString("Generating placeholder CNMT XML...", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;*/
    
    // Make sure that the output buffer for our CNMT XML is big enough
    cnmtXml = calloc(NAME_BUF_LEN * 4, sizeof(char));
    if (!cnmtXml)
    {
        uiDrawString("Error: unable to allocate memory for the CNMT XML!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    generateCnmtXml(&xml_program_info, xml_content_info, cnmtXml);
    
    if (rights_info.has_rights_id)
    {
        // Retrieve tik file
        /*breaks++;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Retrieving %s file...", rights_info.tik_filename);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        uiRefreshDisplay();
        breaks++;*/
        
        tikData = getPartitionHfs0FileByName(&gameCardStorage, rights_info.tik_filename, &tikSize);
        if (!tikData) goto out;
        
        // Retrieve cert file
        /*breaks++;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Retrieving %s file...", rights_info.cert_filename);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        uiRefreshDisplay();
        breaks++;*/
        
        certData = getPartitionHfs0FileByName(&gameCardStorage, rights_info.cert_filename, &certSize);
        if (!certData) goto out;
        
        // File count = NCA count + CNMT XML + tik + cert
        nspFileCount = (titleNcaCount + 3);
        
        // Calculate PFS0 String Table size
        nspPfs0StrTableSize = (((nspFileCount - 4) * NSP_NCA_FILENAME_LENGTH) + (NSP_CNMT_FILENAME_LENGTH * 2) + NSP_TIK_FILENAME_LENGTH + NSP_CERT_FILENAME_LENGTH);
    } else {
        // File count = NCA count + CNMT XML
        nspFileCount = (titleNcaCount + 1);
        
        // Calculate PFS0 String Table size
        nspPfs0StrTableSize = (((nspFileCount - 2) * NSP_NCA_FILENAME_LENGTH) + (NSP_CNMT_FILENAME_LENGTH * 2));
    }
    
    // Add our NACP XML if we created it
    if (nacpXml)
    {
        nspFileCount++;
        nspPfs0StrTableSize += NSP_NACP_FILENAME_LENGTH;
    }
    
    // Start NSP creation
    /*breaks++;
    uiDrawString("Generating placeholder PFS0 header...", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;*/
    
    memset(&nspPfs0Header, 0, sizeof(pfs0_header));
    nspPfs0Header.magic = bswap_32(PFS0_MAGIC);
    nspPfs0Header.file_cnt = nspFileCount;
    
    nspPfs0EntryTable = calloc(nspFileCount, sizeof(pfs0_entry_table));
    if (!nspPfs0EntryTable)
    {
        uiDrawString("Unable to allocate memory for the PFS0 file entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Make sure we have enough space
    nspPfs0StrTable = calloc(nspPfs0StrTableSize * 2, sizeof(char));
    if (!nspPfs0StrTable)
    {
        uiDrawString("Unable to allocate memory for the PFS0 string table!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Determine our full NSP header size
    full_nsp_header_size = (sizeof(pfs0_header) + (nspFileCount * sizeof(pfs0_entry_table)) + nspPfs0StrTableSize);
    full_nsp_header_size = round_up(full_nsp_header_size, 0x10);
    
    // Determine our String Table size
    nspPfs0Header.str_table_size = (full_nsp_header_size - (sizeof(pfs0_header) + (nspFileCount * sizeof(pfs0_entry_table))));
    
    // Calculate total dump size
    progressCtx.totalSize = full_nsp_header_size;
    progressCtx.totalSize += strlen(cnmtXml);
    if (nacpXml) progressCtx.totalSize += strlen(nacpXml);
    if (rights_info.has_rights_id) progressCtx.totalSize += (tikSize + certSize);
    for(i = 0; i < titleNcaCount; i++) progressCtx.totalSize += xml_content_info[i].size;
    
    breaks++;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Total NSP dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp", dumpName);
    
    // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
    // Better safe than sorry
    unlink(dumpPath);
    removeDirectory(dumpPath);
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        mkdir(dumpPath, 0744);
        
        sprintf(tmp_idx, "/%02u", splitIndex);
        strcat(dumpPath, tmp_idx);
    }
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", dumpPath);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    buf = calloc(DUMP_BUFFER_SIZE, sizeof(u8));
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    breaks++;
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
    }
    
    // Write placeholder zeroes
    write_res = fwrite(buf, 1, full_nsp_header_size + strlen(cnmtXml), outFile);
    if (write_res != (full_nsp_header_size + strlen(cnmtXml)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes placeholder data to file offset 0x%016lX! (wrote %lu bytes)", full_nsp_header_size + strlen(cnmtXml), (u64)0, write_res);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    progressCtx.curOffset = (full_nsp_header_size + strlen(cnmtXml));
    
    // Write our NACP XML
    if (nacpXml)
    {
        write_res = fwrite(nacpXml, 1, strlen(nacpXml), outFile);
        if (write_res != strlen(nacpXml))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes NACP XML to file offset 0x%016lX! (wrote %lu bytes)", strlen(nacpXml), progressCtx.curOffset, write_res);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
        
        progressCtx.curOffset += strlen(nacpXml);
    }
    
    if (rights_info.has_rights_id)
    {
        memcpy(buf, tikData, tikSize);
        memcpy(buf + tikSize, certData, certSize);
        
        // Write tik / cert
        write_res = fwrite(buf, 1, tikSize + certSize, outFile);
        if (write_res != (tikSize + certSize))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes tik + cert to file offset 0x%016lX! (wrote %lu bytes)", tikSize + certSize, progressCtx.curOffset, write_res);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
        
        progressCtx.curOffset += (tikSize + certSize);
    }
    
    // Calculate DUMP_BUFFER_SIZE block numbers for the modified Program NCA data blocks
    if (selectedNspDumpType != DUMP_PATCH_NSP && ncaProgramMod.block_mod_cnt > 0)
    {
        hash_table_dump_buffer_start = ((ncaProgramMod.hash_table_offset / DUMP_BUFFER_SIZE) * DUMP_BUFFER_SIZE);
        hash_table_dump_buffer_end = (((ncaProgramMod.hash_table_offset + ncaProgramMod.hash_table_size) / DUMP_BUFFER_SIZE) * DUMP_BUFFER_SIZE);
        
        block0_dump_buffer_start = ((ncaProgramMod.block_offset[0] / DUMP_BUFFER_SIZE) * DUMP_BUFFER_SIZE);
        block0_dump_buffer_end = (((ncaProgramMod.block_offset[0] + ncaProgramMod.block_size[0]) / DUMP_BUFFER_SIZE) * DUMP_BUFFER_SIZE);
        
        if (ncaProgramMod.block_mod_cnt == 2)
        {
            block1_dump_buffer_start = ((ncaProgramMod.block_offset[1] / DUMP_BUFFER_SIZE) * DUMP_BUFFER_SIZE);
            block1_dump_buffer_end = (((ncaProgramMod.block_offset[1] + ncaProgramMod.block_size[1]) / DUMP_BUFFER_SIZE) * DUMP_BUFFER_SIZE);
        }
    }
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    dumping = true;
    
    // Dump all NCAs excluding the CNMT NCA
    for(i = 0; i < (titleNcaCount - 1); i++)
    {
        n = DUMP_BUFFER_SIZE;
        
        memcpy(ncaId.c, xml_content_info[i].nca_id, 16);
        
        sha256ContextCreate(&nca_hash_ctx);
        
        for(nca_offset = 0; nca_offset < xml_content_info[i].size; nca_offset += n, progressCtx.curOffset += n)
        {
            uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", dumpPath);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping NCA content \"%s\"...", xml_content_info[i].nca_id_str);
            uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            if (DUMP_BUFFER_SIZE > (xml_content_info[i].size - nca_offset)) n = (xml_content_info[i].size - nca_offset);
            
            if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, nca_offset, buf, n)))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentStorageReadContentIdFile failed (0x%08X) at offset 0x%016lX for NCA \"%s\".", result, nca_offset, xml_content_info[i].nca_id_str);
                uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                proceed = false;
                break;
            }
            
            // Replace NCA header with our modified one
            if (nca_offset == 0) memcpy(buf, xml_content_info[i].encrypted_header_mod, NCA_FULL_HEADER_LENGTH);
            
            // Replace modified Program NCA data blocks
            if (xml_content_info[i].type == NcmContentType_Program && selectedNspDumpType != DUMP_PATCH_NSP && ncaProgramMod.block_mod_cnt > 0)
            {
                u64 program_nca_prev_write;
                u64 program_nca_next_write;
                
                if (nca_offset == hash_table_dump_buffer_start || nca_offset == hash_table_dump_buffer_end)
                {
                    if (hash_table_dump_buffer_start == hash_table_dump_buffer_end)
                    {
                        memcpy(buf + (ncaProgramMod.hash_table_offset - hash_table_dump_buffer_start), ncaProgramMod.hash_table, ncaProgramMod.hash_table_size);
                    } else {
                        program_nca_prev_write = (DUMP_BUFFER_SIZE - (ncaProgramMod.hash_table_offset - hash_table_dump_buffer_start));
                        program_nca_next_write = (ncaProgramMod.hash_table_size - program_nca_prev_write);
                        
                        if (nca_offset == hash_table_dump_buffer_start)
                        {
                            memcpy(buf + (ncaProgramMod.hash_table_offset - hash_table_dump_buffer_start), ncaProgramMod.hash_table, program_nca_prev_write);
                        } else {
                            memcpy(buf, ncaProgramMod.hash_table + program_nca_prev_write, program_nca_next_write);
                        }
                    }
                }
                
                if (nca_offset == block0_dump_buffer_start || nca_offset == block0_dump_buffer_end)
                {
                    if (block0_dump_buffer_start == block0_dump_buffer_end)
                    {
                        memcpy(buf + (ncaProgramMod.block_offset[0] - block0_dump_buffer_start), ncaProgramMod.block_data[0], ncaProgramMod.block_size[0]);
                    } else {
                        program_nca_prev_write = (DUMP_BUFFER_SIZE - (ncaProgramMod.block_offset[0] - block0_dump_buffer_start));
                        program_nca_next_write = (ncaProgramMod.block_size[0] - program_nca_prev_write);
                        
                        if (nca_offset == block0_dump_buffer_start)
                        {
                            memcpy(buf + (ncaProgramMod.block_offset[0] - block0_dump_buffer_start), ncaProgramMod.block_data[0], program_nca_prev_write);
                        } else {
                            memcpy(buf, ncaProgramMod.block_data[0] + program_nca_prev_write, program_nca_next_write);
                        }
                    }
                }
                
                if (ncaProgramMod.block_mod_cnt == 2 && (nca_offset == block1_dump_buffer_start || nca_offset == block1_dump_buffer_end))
                {
                    if (block1_dump_buffer_start == block1_dump_buffer_end)
                    {
                        memcpy(buf + (ncaProgramMod.block_offset[1] - block1_dump_buffer_start), ncaProgramMod.block_data[1], ncaProgramMod.block_size[1]);
                    } else {
                        program_nca_prev_write = (DUMP_BUFFER_SIZE - (ncaProgramMod.block_offset[1] - block1_dump_buffer_start));
                        program_nca_next_write = (ncaProgramMod.block_size[1] - program_nca_prev_write);
                        
                        if (nca_offset == block1_dump_buffer_start)
                        {
                            memcpy(buf + (ncaProgramMod.block_offset[1] - block1_dump_buffer_start), ncaProgramMod.block_data[1], program_nca_prev_write);
                        } else {
                            memcpy(buf, ncaProgramMod.block_data[1] + program_nca_prev_write, program_nca_next_write);
                        }
                    }
                }
            }
            
            // Update SHA-256 calculation
            sha256ContextUpdate(&nca_hash_ctx, buf, n);
            
            if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_NSP_PART_SIZE))
            {
                u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_NSP_PART_SIZE));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                        uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                {
                    splitIndex++;
                    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp/%02u", dumpName, splitIndex);
                    
                    outFile = fopen(dumpPath, "wb");
                    if (!outFile)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                        uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(buf, 1, n, outFile);
                if (write_res != n)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                    uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    
                    if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 8, ((breaks + 8) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                        fat32_error = true;
                    }
                    
                    proceed = false;
                    break;
                }
            }
            
            printProgressBar(&progressCtx, true, n);
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize && ((progressCtx.curOffset / DUMP_BUFFER_SIZE) % 10) == 0)
            {
                hidScanInput();
                
                u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                if (keysDown & KEY_B)
                {
                    uiDrawString("Process canceled.", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    proceed = false;
                    break;
                }
            }
        }
        
        if (!proceed)
        {
            setProgressBarError(&progressCtx);
            break;
        }
        
        // Support empty files
        if (!xml_content_info[i].size)
        {
            uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", dumpPath);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping NCA content \"%s\"...", xml_content_info[i].nca_id_str);
            uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            printProgressBar(&progressCtx, false, 0);
        }
        
        // Update content info
        sha256ContextGetHash(&nca_hash_ctx, xml_content_info[i].hash);
        convertDataToHexString(xml_content_info[i].hash, 32, xml_content_info[i].hash_str, 65);
        memcpy(xml_content_info[i].nca_id, xml_content_info[i].hash, 16);
        convertDataToHexString(xml_content_info[i].nca_id, 16, xml_content_info[i].nca_id_str, 33);
    }
    
    if (!proceed) goto out;
    
    dumping = false;
    
    breaks += 6;
    
    uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", dumpPath);
    uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    uiDrawString("Writing PFS0 header, CNMT XML and CNMT NCA...", 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    uiRefreshDisplay();
    
    // Now we can patch our CNMT NCA and generate our proper CNMT XML
    if (!patchCnmtNca(cnmtNcaBuf, xml_content_info[cnmtNcaIndex].size, &xml_program_info, xml_content_info, &ncaCnmtMod))
    {
        setProgressBarError(&progressCtx);
        goto out;
    }
    
    generateCnmtXml(&xml_program_info, xml_content_info, cnmtXml);
    
    // Fill our Entry and String Tables
    u64 file_offset = 0;
    u32 filename_offset = 0;
    
    for(i = 0; i < nspFileCount; i++)
    {
        // If dealing with a title with rights ID, reserve the first four entries for the CNMT XML, NACP XML (if available), tik and cert
        // Otherwise, just reserve the first entry for the CNMT XML
        
        char ncaFileName[50] = {'\0'};
        u64 cur_file_size = 0;
        
        if (i == 0)
        {
            // CNMT XML
            sprintf(ncaFileName, "%s.cnmt.xml", xml_content_info[cnmtNcaIndex].nca_id_str);
            cur_file_size = strlen(cnmtXml);
        } else {
            if (nacpXml && i == 1)
            {
                // NACP XML
                sprintf(ncaFileName, "%s.nacp.xml", xml_content_info[nacpNcaIndex].nca_id_str);
                cur_file_size = strlen(nacpXml);
            } else {
                if (rights_info.has_rights_id && ((!nacpXml && (i == 1 || i == 2)) || (nacpXml && (i == 2 || i == 3))))
                {
                    // tik / cert
                    sprintf(ncaFileName, "%s", (((!nacpXml && i == 1) || (nacpXml && i == 2)) ? rights_info.tik_filename : rights_info.cert_filename));
                    cur_file_size = (((!nacpXml && i == 1) || (nacpXml && i == 2)) ? tikSize : certSize);
                } else {
                    u32 cnt_idx = (i - (rights_info.has_rights_id ? 3 : 1) - (nacpXml ? 1 : 0));
                    sprintf(ncaFileName, "%s.%s", xml_content_info[cnt_idx].nca_id_str, (cnt_idx == cnmtNcaIndex ? "cnmt.nca" : "nca"));
                    cur_file_size = xml_content_info[cnt_idx].size;
                }
            }
        }
        
        nspPfs0EntryTable[i].file_size = cur_file_size;
        nspPfs0EntryTable[i].file_offset = file_offset;
        nspPfs0EntryTable[i].filename_offset = filename_offset;
        
        strcpy(nspPfs0StrTable + filename_offset, ncaFileName);
        
        file_offset += nspPfs0EntryTable[i].file_size;
        filename_offset += (strlen(ncaFileName) + 1);
    }
    
    // Write our full PFS0 header + CNMT XML
    memcpy(buf, &nspPfs0Header, sizeof(pfs0_header));
    memcpy(buf + sizeof(pfs0_header), nspPfs0EntryTable, nspFileCount * sizeof(pfs0_entry_table));
    memcpy(buf + sizeof(pfs0_header) + (nspFileCount * sizeof(pfs0_entry_table)), nspPfs0StrTable, nspPfs0Header.str_table_size);
    memcpy(buf + full_nsp_header_size, cnmtXml, strlen(cnmtXml));
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp/%02u", dumpName, 0);
        
        outFile = fopen(dumpPath, "rb+");
        if (!outFile)
        {
            setProgressBarError(&progressCtx);
            uiDrawString("Failed to re-open output file for part #0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
    } else {
        rewind(outFile);
    }
    
    write_res = fwrite(buf, 1, full_nsp_header_size + strlen(cnmtXml), outFile);
    if (write_res != (full_nsp_header_size + strlen(cnmtXml)))
    {
        setProgressBarError(&progressCtx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes PFS0 header + CNMT XML to file offset 0x%016lX! (wrote %lu bytes)", full_nsp_header_size + strlen(cnmtXml), (u64)0, write_res);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Now let's write our modified CNMT NCA
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp/%02u", dumpName, splitIndex);
        
        outFile = fopen(dumpPath, "rb+");
        if (!outFile)
        {
            setProgressBarError(&progressCtx);
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to re-open output file for part #%u!", splitIndex);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
        
        fseek(outFile, 0, SEEK_END);
        
        // This is a pain
        u64 cur_file_size = (progressCtx.curOffset - (splitIndex * SPLIT_FILE_NSP_PART_SIZE));
        if ((cur_file_size + xml_content_info[cnmtNcaIndex].size) > SPLIT_FILE_NSP_PART_SIZE)
        {
            u64 new_file_chunk_size = ((progressCtx.curOffset + xml_content_info[cnmtNcaIndex].size) - ((splitIndex + 1) * SPLIT_FILE_NSP_PART_SIZE));
            u64 old_file_chunk_size = (xml_content_info[cnmtNcaIndex].size - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(cnmtNcaBuf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    setProgressBarError(&progressCtx);
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes CNMT NCA chunk #1 from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    goto out;
                }
            }
            
            fclose(outFile);
            outFile = NULL;
            
            if (new_file_chunk_size > 0)
            {
                splitIndex++;
                snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp/%02u", dumpName, splitIndex);
                
                outFile = fopen(dumpPath, "wb");
                if (!outFile)
                {
                    setProgressBarError(&progressCtx);
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    goto out;
                }
                
                uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", dumpPath);
                uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                
                uiRefreshDisplay();
                
                write_res = fwrite(cnmtNcaBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                if (write_res != new_file_chunk_size)
                {
                    setProgressBarError(&progressCtx);
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes CNMT NCA chunk #2 from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    goto out;
                }
            }
        } else {
            write_res = fwrite(cnmtNcaBuf, 1, xml_content_info[cnmtNcaIndex].size, outFile);
            if (write_res != xml_content_info[cnmtNcaIndex].size)
            {
                setProgressBarError(&progressCtx);
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes CNMT NCA to file offset 0x%016lX! (wrote %lu bytes)", xml_content_info[cnmtNcaIndex].size, progressCtx.curOffset, write_res);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                goto out;
            }
        }
    } else {
        fseek(outFile, 0, SEEK_END);
        
        write_res = fwrite(cnmtNcaBuf, 1, xml_content_info[cnmtNcaIndex].size, outFile);
        if (write_res != xml_content_info[cnmtNcaIndex].size)
        {
            setProgressBarError(&progressCtx);
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes CNMT NCA to file offset 0x%016lX! (wrote %lu bytes)", xml_content_info[cnmtNcaIndex].size, progressCtx.curOffset, write_res);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            
            if ((progressCtx.curOffset + xml_content_info[cnmtNcaIndex].size) > FAT32_FILESIZE_LIMIT)
            {
                breaks += 2;
                uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            }
            
            goto out;
        }
    }
    
    progressCtx.curOffset += xml_content_info[cnmtNcaIndex].size;
    
    if (progressCtx.curOffset < progressCtx.totalSize)
    {
        setProgressBarError(&progressCtx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Unexpected underdump error! Wrote %lu bytes, expected %lu bytes.", progressCtx.curOffset, progressCtx.totalSize);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    success = true;
    
    // Finalize dump
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
    progressCtx.now -= progressCtx.start;
    
    progressCtx.progress = 100;
    progressCtx.remainingTime = 0;
    
    printProgressBar(&progressCtx, false, 0);
    
    formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
    
    uiRefreshDisplay();
    
    if (calcCrc)
    {
        breaks += 2;
        uiDrawString("CRC32 checksum calculation will begin in 5 seconds...", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        uiRefreshDisplay();
        
        delay(5);
        
        breaks = initial_breaks;
        uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, FB_HEIGHT - (breaks * (font_height + (font_height / 4))), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Calculating CRC32 checksum. Hold %s to cancel.", NINTENDO_FONT_B);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks += 2;
        
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp", dumpName);
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            splitIndex = 0;
            sprintf(tmp_idx, "/%02u", splitIndex);
            strcat(dumpPath, tmp_idx);
        }
        
        outFile = fopen(dumpPath, "rb");
        if (outFile)
        {
            n = DUMP_BUFFER_SIZE;
            progressCtx.start = progressCtx.now = progressCtx.remainingTime = 0;
            progressCtx.lastSpeed = progressCtx.averageSpeed = 0.0;
            
            size_t read_res;
            
            progressCtx.line_offset = (breaks + 2);
            timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
            
            for(progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
            {
                uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "File: \"%s\".", dumpPath);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                
                if (DUMP_BUFFER_SIZE > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
                
                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_NSP_PART_SIZE))
                {
                    u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_NSP_PART_SIZE));
                    u64 old_file_chunk_size = (n - new_file_chunk_size);
                    
                    if (old_file_chunk_size > 0)
                    {
                        read_res = fread(buf, 1, old_file_chunk_size, outFile);
                        if (read_res != old_file_chunk_size)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read %lu bytes chunk from offset 0x%016lX from part #%02u! (read %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, read_res);
                            uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            proceed = false;
                            break;
                        }
                    }
                    
                    fclose(outFile);
                    outFile = NULL;
                    
                    if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                    {
                        splitIndex++;
                        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp/%02u", dumpName, splitIndex);
                        
                        outFile = fopen(dumpPath, "rb");
                        if (!outFile)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to re-open output file for part #%u!", splitIndex);
                            uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            proceed = false;
                            break;
                        }
                        
                        if (new_file_chunk_size > 0)
                        {
                            read_res = fread(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                            if (read_res != new_file_chunk_size)
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read %lu bytes chunk from offset 0x%016lX from part #%02u! (read %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, read_res);
                                uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                proceed = false;
                                break;
                            }
                        }
                    }
                } else {
                    read_res = fread(buf, 1, n, outFile);
                    if (read_res != n)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read %lu bytes chunk from offset 0x%016lX! (read %lu bytes)", n, progressCtx.curOffset, read_res);
                        uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
                
                // Update CRC32
                crc32(buf, n, &crc);
                
                printProgressBar(&progressCtx, true, n);
                
                if ((progressCtx.curOffset + n) < progressCtx.totalSize && ((progressCtx.curOffset / DUMP_BUFFER_SIZE) % 10) == 0)
                {
                    hidScanInput();
                    
                    u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                    if (keysDown & KEY_B)
                    {
                        uiDrawString("Process canceled.", 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
            }
            
            breaks += 4;
            
            if (proceed)
            {
                timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
                progressCtx.now -= progressCtx.start;
                
                formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                breaks++;
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "NSP dump CRC32 checksum: %08X", crc);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
            } else {
                setProgressBarError(&progressCtx);
            }
        } else {
            uiDrawString("Failed to re-open output file in read mode!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    }
    
    // Set archive bit (only for FAT32)
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp", dumpName);
        if (R_FAILED(result = fsdevSetArchiveBit(dumpPath)))
        {
            breaks += 2;
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Warning: failed to set archive bit on output directory! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    }
    
out:
    if (buf) free(buf);
    
    if (outFile) fclose(outFile);
    
    if (!success)
    {
        if (dumping)
        {
            breaks += 6;
            if (fat32_error) breaks += 2;
        }
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s.nsp", dumpName);
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            removeDirectory(dumpPath);
        } else {
            unlink(dumpPath);
        }
    }
    
    if (nspPfs0StrTable) free(nspPfs0StrTable);
    
    if (nspPfs0EntryTable) free(nspPfs0EntryTable);
    
    if (certData) free(certData);
    
    if (tikData) free(tikData);
    
    if (cnmtXml) free(cnmtXml);
    
    if (cnmtNcaBuf) free(cnmtNcaBuf);
    
    if (ncaProgramMod.block_mod_cnt == 2 && ncaProgramMod.block_data[1]) free(ncaProgramMod.block_data[1]);
    
    if (ncaProgramMod.block_data[0]) free(ncaProgramMod.block_data[0]);
    
    if (ncaProgramMod.hash_table) free(ncaProgramMod.hash_table);
    
    if (nacpXml) free(nacpXml);
    
    serviceClose(&(ncmStorage.s));
    
    if (xml_content_info) free(xml_content_info);
    
    if (titleContentRecords) free(titleContentRecords);
    
    serviceClose(&(ncmDb.s));
    
    if (titleList) free(titleList);
    
    fsStorageClose(&gameCardStorage);
    
    if (partitionHfs0Header)
    {
        free(partitionHfs0Header);
        partitionHfs0Header = NULL;
        partitionHfs0HeaderOffset = 0;
        partitionHfs0HeaderSize = 0;
        partitionHfs0FileCount = 0;
        partitionHfs0StrTableSize = 0;
    }
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpRawHfs0Partition(FsDeviceOperator* fsOperator, u32 partition, bool doSplitting)
{
    Result result;
    u64 partitionOffset;
    bool success = false, fat32_error = false;
    u8 *buf = NULL;
    u64 n = DUMP_BUFFER_SIZE;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    char filename[NAME_BUF_LEN * 2] = {'\0'};
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    char *dumpName = generateDumpFullName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
    {
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;*/
        
        // Ugly hack
        // The IStorage instance returned for partition == 0 contains the gamecard header, the gamecard certificate, the root HFS0 header and:
        // * The "update" (0) partition and the "normal" (1) partition (for gamecard type 0x01)
        // * The "update" (0) partition, the "logo" (1) partition and the "normal" (2) partition (for gamecard type 0x02)
        // The IStorage instance returned for partition == 1 contains the "secure" partition (which can either be 2 or 3 depending on the gamecard type)
        // This ugly hack makes sure we just dump the *actual* raw HFS0 partition, without preceding data, padding, etc.
        // Oddly enough, IFileSystem instances actually points to the specified partition ID filesystem. I don't understand why it doesn't work like that for IStorage, but whatever
        // NOTE: Using partition == 2 returns error 0x149002, and using higher values probably do so, too
        
        if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            breaks++;*/
            
            if (getHfs0EntryDetails(hfs0_header, hfs0_offset, hfs0_size, hfs0_partition_cnt, partition, true, 0, &partitionOffset, &(progressCtx.totalSize)))
            {
                convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "HFS0 partition size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks += 2;
                
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "HFS0 partition offset (relative to IStorage instance): 0x%016lX", partitionOffset);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks += 2;*/
                
                if (progressCtx.totalSize <= freeSpace)
                {
                    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
                    {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0.%02u", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                    } else {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    }
                    
                    outFile = fopen(filename, "wb");
                    if (outFile)
                    {
                        buf = malloc(DUMP_BUFFER_SIZE);
                        if (buf)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping raw HFS0 partition #%u. Hold %s to cancel.", partition, NINTENDO_FONT_B);
                            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                            breaks += 2;
                            
                            if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                            {
                                uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                breaks += 2;
                            }
                            
                            uiRefreshDisplay();
                            
                            progressCtx.line_offset = (breaks + 2);
                            timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
                            
                            for (progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
                            {
                                uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", filename);
                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                
                                if (DUMP_BUFFER_SIZE > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
                                
                                if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset + progressCtx.curOffset, buf, n)))
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionOffset + progressCtx.curOffset);
                                    uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                    break;
                                }
                                
                                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
                                {
                                    u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                                    u64 old_file_chunk_size = (n - new_file_chunk_size);
                                    
                                    if (old_file_chunk_size > 0)
                                    {
                                        write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                                        if (write_res != old_file_chunk_size)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                                            uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            break;
                                        }
                                    }
                                    
                                    fclose(outFile);
                                    outFile = NULL;
                                    
                                    if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                                    {
                                        splitIndex++;
                                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0.%02u", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                                        
                                        outFile = fopen(filename, "wb");
                                        if (!outFile)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                            uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            break;
                                        }
                                        
                                        if (new_file_chunk_size > 0)
                                        {
                                            write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                            if (write_res != new_file_chunk_size)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                                                uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                break;
                                            }
                                        }
                                    }
                                } else {
                                    write_res = fwrite(buf, 1, n, outFile);
                                    if (write_res != n)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                                        uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                        
                                        if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                                        {
                                            uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                            fat32_error = true;
                                        }
                                        
                                        break;
                                    }
                                }
                                
                                printProgressBar(&progressCtx, true, n);
                                
                                if ((progressCtx.curOffset + n) < progressCtx.totalSize && ((progressCtx.curOffset / DUMP_BUFFER_SIZE) % 10) == 0)
                                {
                                    hidScanInput();
                                    
                                    u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                                    if (keysDown & KEY_B)
                                    {
                                        uiDrawString("Process canceled.", 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                        break;
                                    }
                                }
                            }
                            
                            if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
                            
                            // Support empty files
                            if (!progressCtx.totalSize)
                            {
                                uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", filename);
                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                
                                progressCtx.progress = 100;
                                
                                printProgressBar(&progressCtx, false, 0);
                            }
                            
                            breaks += 4;
                            
                            if (success)
                            {
                                timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
                                progressCtx.now -= progressCtx.start;
                                
                                formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
                                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                            } else {
                                setProgressBarError(&progressCtx);
                                if (fat32_error) breaks += 2;
                            }
                            
                            free(buf);
                        } else {
                            uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        }
                        
                        if (outFile) fclose(outFile);
                        
                        if (!success)
                        {
                            if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
                            {
                                for(u8 i = 0; i <= splitIndex; i++)
                                {
                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0.%02u", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), i);
                                    unlink(filename);
                                }
                            } else {
                                unlink(filename);
                            }
                        }
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", filename);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    }
                } else {
                    uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                }
            } else {
                uiDrawString("Error: unable to get partition details from the root HFS0 header!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
            
            fsStorageClose(&gameCardStorage);
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    breaks += 2;
    
    free(dumpName);
    
    return success;
}

bool copyFileFromHfs0(FsDeviceOperator* fsOperator, u32 partition, const char* source, const char* dest, const u64 file_offset, const u64 size, progress_ctx_t *progressCtx, bool doSplitting)
{
    Result result;
    bool success = false, fat32_error = false;
    char splitFilename[NAME_BUF_LEN] = {'\0'};
    size_t destLen = strlen(dest);
    FILE *outFile = NULL;
    u8 *buf = NULL;
    u64 off, n = DUMP_BUFFER_SIZE;
    u8 splitIndex = 0;
    
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    
    size_t write_res;
    
    uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"%s\"...", source);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    if ((destLen + 1) < NAME_BUF_LEN)
    {
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            breaks++;*/
            
            // Same ugly hack from dumpRawHfs0Partition()
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
            {
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks++;*/
                
                if (size > FAT32_FILESIZE_LIMIT && doSplitting) snprintf(splitFilename, sizeof(splitFilename) / sizeof(splitFilename[0]), "%s.%02u", dest, splitIndex);
                
                outFile = fopen(((size > FAT32_FILESIZE_LIMIT && doSplitting) ? splitFilename : dest), "wb");
                if (outFile)
                {
                    buf = malloc(DUMP_BUFFER_SIZE);
                    if (buf)
                    {
                        for (off = 0; off < size; off += n, progressCtx->curOffset += n)
                        {
                            uiFill(0, ((breaks + 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", ((size > FAT32_FILESIZE_LIMIT && doSplitting) ? (strrchr(splitFilename, '/') + 1) : (strrchr(dest, '/') + 1)));
                            uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                            
                            uiRefreshDisplay();
                            
                            if (DUMP_BUFFER_SIZE > (size - off)) n = (size - off);
                            
                            if (R_FAILED(result = fsStorageRead(&gameCardStorage, file_offset + off, buf, n)))
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, file_offset + off);
                                uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                break;
                            }
                            
                            if (size > FAT32_FILESIZE_LIMIT && doSplitting && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
                            {
                                u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                                u64 old_file_chunk_size = (n - new_file_chunk_size);
                                
                                if (old_file_chunk_size > 0)
                                {
                                    write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                                    if (write_res != old_file_chunk_size)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, off, splitIndex, write_res);
                                        uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                        break;
                                    }
                                }
                                
                                fclose(outFile);
                                outFile = NULL;
                                
                                if (new_file_chunk_size > 0 || (off + n) < size)
                                {
                                    splitIndex++;
                                    snprintf(splitFilename, sizeof(splitFilename) / sizeof(splitFilename[0]), "%s.%02u", dest, splitIndex);
                                    
                                    outFile = fopen(splitFilename, "wb");
                                    if (!outFile)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                        uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                        break;
                                    }
                                    
                                    if (new_file_chunk_size > 0)
                                    {
                                        write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                        if (write_res != new_file_chunk_size)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                                            uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            break;
                                        }
                                    }
                                }
                            } else {
                                write_res = fwrite(buf, 1, n, outFile);
                                if (write_res != n)
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                                    uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                    
                                    if ((off + n) > FAT32_FILESIZE_LIMIT)
                                    {
                                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((breaks + 8) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                        fat32_error = true;
                                    }
                                    
                                    break;
                                }
                            }
                            
                            printProgressBar(progressCtx, true, n);
                            
                            if ((off + n) < size && ((off / DUMP_BUFFER_SIZE) % 10) == 0)
                            {
                                hidScanInput();
                                
                                u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
                                if (keysDown & KEY_B)
                                {
                                    uiDrawString("Process canceled.", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                    break;
                                }
                            }
                        }
                        
                        if (off >= size) success = true;
                        
                        // Support empty files
                        if (!size)
                        {
                            uiFill(0, ((breaks + 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", ((size > FAT32_FILESIZE_LIMIT && doSplitting) ? (strrchr(splitFilename, '/') + 1) : (strrchr(dest, '/') + 1)));
                            uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                            
                            if (progressCtx->totalSize == size) progressCtx->progress = 100;
                            
                            printProgressBar(progressCtx, false, 0);
                        }
                        
                        if (!success)
                        {
                            setProgressBarError(progressCtx);
                            if (fat32_error) breaks += 2;
                        }
                        
                        free(buf);
                    } else {
                        uiDrawString("Failed to allocate memory for the dump process!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    }
                    
                    if (outFile) fclose(outFile);
                    
                    if (!success)
                    {
                        if (size > FAT32_FILESIZE_LIMIT && doSplitting)
                        {
                            for(u8 i = 0; i <= splitIndex; i++)
                            {
                                snprintf(splitFilename, sizeof(splitFilename) / sizeof(splitFilename[0]), "%s.%02u", dest, i);
                                unlink(splitFilename);
                            }
                        } else {
                            unlink(dest);
                        }
                    }
                } else {
                    uiDrawString("Failed to open output file!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                }
                
                fsStorageClose(&gameCardStorage);
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
                uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination path is too long! (%lu bytes)", destLen);
        uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    return success;
}

bool copyHfs0Contents(FsDeviceOperator* fsOperator, u32 partition, hfs0_entry_table *partitionEntryTable, progress_ctx_t *progressCtx, const char *dest, bool splitting)
{
    if (!dest || !*dest)
    {
        uiDrawString("Error: destination directory is empty.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!partitionHfs0Header || !partitionEntryTable)
    {
        uiDrawString("HFS0 partition header information unavailable!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!progressCtx)
    {
        uiDrawString("Error: invalid progress context.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    char dbuf[NAME_BUF_LEN] = {'\0'};
    size_t dest_len = strlen(dest);
    
    if ((dest_len + 1) >= NAME_BUF_LEN)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination directory name is too long! (%lu bytes)", dest_len);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    strcpy(dbuf, dest);
    mkdir(dbuf, 0744);
    
    dbuf[dest_len] = '/';
    dest_len++;
    
    u32 i;
    bool success;
    
    progressCtx->line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx->start));
    
    for(i = 0; i < partitionHfs0FileCount; i++)
    {
        u32 filename_offset = (HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * partitionHfs0FileCount) + partitionEntryTable[i].filename_offset);
        char *filename = ((char*)partitionHfs0Header + filename_offset);
        strcpy(dbuf + dest_len, filename);
        
        removeIllegalCharacters(dbuf + dest_len);
        
        u64 file_offset = (partitionHfs0HeaderSize + partitionEntryTable[i].file_offset);
        if (HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition) == 0) file_offset += partitionHfs0HeaderOffset;
        
        success = copyFileFromHfs0(fsOperator, partition, filename, dbuf, file_offset, partitionEntryTable[i].file_size, progressCtx, splitting);
        if (!success) break;
    }
    
    return success;
}

bool dumpHfs0PartitionData(FsDeviceOperator* fsOperator, u32 partition)
{
    bool success = false;
    u32 i;
    hfs0_entry_table *entryTable = NULL;
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char *dumpName = generateDumpFullName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess(fsOperator); 
    
    if (getPartitionHfs0Header(partition))
    {
        if (partitionHfs0FileCount)
        {
            entryTable = calloc(partitionHfs0FileCount, sizeof(hfs0_entry_table));
            if (entryTable)
            {
                memcpy(entryTable, partitionHfs0Header + HFS0_ENTRY_TABLE_ADDR, sizeof(hfs0_entry_table) * partitionHfs0FileCount);
                
                // Calculate total size
                for(i = 0; i < partitionHfs0FileCount; i++) progressCtx.totalSize += entryTable[i].file_size;
                
                convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Total partition data size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks += 2;
                
                if (progressCtx.totalSize <= freeSpace)
                {
                    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s - Partition %u (%s)", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying partition #%u data to \"%s/\". Hold %s to cancel.", partition, dumpPath, NINTENDO_FONT_B);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                    breaks += 2;
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        breaks += 2;
                    }
                    
                    uiRefreshDisplay();
                    
                    success = copyHfs0Contents(fsOperator, partition, entryTable, &progressCtx, dumpPath, true);
                    
                    breaks += 6;
                    
                    if (success)
                    {
                        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
                        progressCtx.now -= progressCtx.start;
                        
                        formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                    } else {
                        removeDirectory(dumpPath);
                    }
                } else {
                    uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                }
                
                free(entryTable);
            } else {
                uiDrawString("Unable to allocate memory for the HFS0 file entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
        } else {
            uiDrawString("The selected partition is empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
        
        free(partitionHfs0Header);
        partitionHfs0Header = NULL;
        partitionHfs0HeaderOffset = 0;
        partitionHfs0HeaderSize = 0;
        partitionHfs0FileCount = 0;
        partitionHfs0StrTableSize = 0;
    }
    
    breaks += 2;
    
    free(dumpName);
    
    return success;
}

bool dumpFileFromHfs0Partition(FsDeviceOperator* fsOperator, u32 partition, u32 file, char *filename)
{
    if (!partitionHfs0Header)
    {
        uiDrawString("HFS0 partition header information unavailable!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (!filename || !*filename)
    {
        uiDrawString("Filename unavailable!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char *dumpName = generateDumpFullName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    u64 file_offset = 0;
    u64 file_size = 0;
    bool success = false;
    
    if (getHfs0EntryDetails(partitionHfs0Header, partitionHfs0HeaderOffset, partitionHfs0HeaderSize, partitionHfs0FileCount, file, false, partition, &file_offset, &file_size))
    {
        progressCtx.totalSize = file_size;
        convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;
        
        if (file_size <= freeSpace)
        {
            char destCopyPath[NAME_BUF_LEN * 2] = {'\0'};
            char fixedFilename[NAME_BUF_LEN] = {'\0'};
            
            sprintf(fixedFilename, filename);
            removeIllegalCharacters(fixedFilename);
            
            snprintf(destCopyPath, sizeof(destCopyPath) / sizeof(destCopyPath[0]), "sdmc:/%s - Partition %u (%s)", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
            
            if ((strlen(destCopyPath) + 1 + strlen(filename)) < NAME_BUF_LEN)
            {
                mkdir(destCopyPath, 0744);
                
                snprintf(destCopyPath, sizeof(destCopyPath) / sizeof(destCopyPath[0]), "sdmc:/%s - Partition %u (%s)/%s", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), fixedFilename);
                
                breaks++;
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Hold %s to cancel.", NINTENDO_FONT_B);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                breaks += 2;
                
                if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                {
                    uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    breaks += 2;
                }
                
                uiRefreshDisplay();
                
                progressCtx.line_offset = (breaks + 4);
                timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
                
                success = copyFileFromHfs0(fsOperator, partition, filename, destCopyPath, file_offset, file_size, &progressCtx, true);
                
                breaks += 6;
                
                if (success)
                {
                    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
                    progressCtx.now -= progressCtx.start;
                    
                    formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                }
            } else {
                breaks++;
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination path is too long! (%lu bytes)", strlen(destCopyPath) + 1 + strlen(filename));
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
        } else {
            uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    } else {
        uiDrawString("Error: unable to get file details from the partition HFS0 header!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool recursiveDumpRomFsFile(u32 file_offset, char *romfs_path, char *output_path, progress_ctx_t *progressCtx, bool doSplitting)
{
    if (!romFsContext.romfs_filetable_size || file_offset > romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries || !romfs_path || !output_path || !progressCtx)
    {
        uiDrawString("Error: invalid parameters to parse file entry from RomFS section!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    size_t orig_romfs_path_len = strlen(romfs_path);
    size_t orig_output_path_len = strlen(output_path);
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 *buf = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false;
    
    u64 off = 0;
    
    size_t write_res;
    
    char tmp_idx[5];
    
    romfs_file *entry = (romfs_file*)((u8*)romFsContext.romfs_file_entries + file_offset);
    
    // Check if we're dealing with a nameless file
    if (!entry->nameLen)
    {
        uiDrawString("Error: file entry without name in RomFS section!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
    {
        uiDrawString("Error: RomFS section file path is too long!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Generate current path
    strcat(romfs_path, "/");
    strncat(romfs_path, (char*)entry->name, entry->nameLen);
    
    strcat(output_path, "/");
    strncat(output_path, (char*)entry->name, entry->nameLen);
    removeIllegalCharacters(output_path + orig_output_path_len + 1);
    
    // Start dump process
    uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"romfs:%s\"...", romfs_path);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    if (entry->dataSize > FAT32_FILESIZE_LIMIT && doSplitting)
    {
        sprintf(tmp_idx, ".%02u", splitIndex);
        strcat(output_path, tmp_idx);
    }
    
    outFile = fopen(output_path, "wb");
    if (!outFile)
    {
        uiDrawString("Failed to open output file!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        goto out;
    }
    
    buf = malloc(DUMP_BUFFER_SIZE);
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        goto out;
    }
    
    for(off = 0; off < entry->dataSize; off += n, progressCtx->curOffset += n)
    {
        uiFill(0, ((breaks + 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(output_path, '/') + 1);
        uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        uiRefreshDisplay();
        
        if (DUMP_BUFFER_SIZE > (entry->dataSize - off)) n = (entry->dataSize - off);
        
        breaks += 6;
        proceed = processNcaCtrSectionBlock(&(romFsContext.ncmStorage), &(romFsContext.ncaId), romFsContext.romfs_filedata_offset + entry->dataOff + off, buf, n, &(romFsContext.aes_ctx), false);
        breaks -= 6;
        
        if (!proceed) break;
        
        if (entry->dataSize > FAT32_FILESIZE_LIMIT && doSplitting && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
        {
            u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
            u64 old_file_chunk_size = (n - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, off, splitIndex, write_res);
                    uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    break;
                }
            }
            
            fclose(outFile);
            outFile = NULL;
            
            if (new_file_chunk_size > 0 || (off + n) < entry->dataSize)
            {
                char *tmp = strrchr(output_path, '.');
                if (tmp != NULL) *tmp = '\0';
                
                splitIndex++;
                sprintf(tmp_idx, ".%02u", splitIndex);
                strcat(output_path, tmp_idx);
                
                outFile = fopen(output_path, "wb");
                if (!outFile)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                    uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                        uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(buf, 1, n, outFile);
            if (write_res != n)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                uiDrawString(strbuf, 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                
                if ((off + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((breaks + 8) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(progressCtx, true, n);
        
        if ((off + n) < entry->dataSize && ((off / DUMP_BUFFER_SIZE) % 10) == 0)
        {
            hidScanInput();
            
            u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
            if (keysDown & KEY_B)
            {
                uiDrawString("Process canceled.", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                break;
            }
        }
    }
    
    if (off >= entry->dataSize) success = true;
    
    // Support empty files
    if (!entry->dataSize)
    {
        uiFill(0, ((breaks + 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(output_path, '/') + 1);
        uiDrawString(strbuf, 8, ((breaks + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        if (progressCtx->totalSize == entry->dataSize) progressCtx->progress = 100;
        
        printProgressBar(progressCtx, false, 0);
    }
    
out:
    if (buf) free(buf);
    
    if (outFile) fclose(outFile);
    
    if (!success)
    {
        if (fat32_error) breaks += 2;
        
        if (entry->dataSize > FAT32_FILESIZE_LIMIT && doSplitting)
        {
            for(u8 i = 0; i <= splitIndex; i++)
            {
                char *tmp = strrchr(output_path, '.');
                if (tmp != NULL) *tmp = '\0';
                
                sprintf(tmp_idx, ".%02u", splitIndex);
                strcat(output_path, tmp_idx);
                unlink(output_path);
            }
        } else {
            unlink(output_path);
        }
    }
    
    romfs_path[orig_romfs_path_len] = '\0';
    output_path[orig_output_path_len] = '\0';
    
    if (success)
    {
        if (entry->sibling != ROMFS_ENTRY_EMPTY) success = recursiveDumpRomFsFile(entry->sibling, romfs_path, output_path, progressCtx, true);
    }
    
    return success;
}

bool recursiveDumpRomFsDir(u32 dir_offset, char *romfs_path, char *output_path, progress_ctx_t *progressCtx)
{
    if (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries || !romfs_path || !output_path || !progressCtx)
    {
        uiDrawString("Error: invalid parameters to parse directory entry from RomFS section!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    size_t orig_romfs_path_len = strlen(romfs_path);
    size_t orig_output_path_len = strlen(output_path);
    
    romfs_dir *entry = (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset);
    
    // Check if we're dealing with a nameless directory that's not the root directory
    if (!entry->nameLen && dir_offset > 0)
    {
        uiDrawString("Error: directory entry without name in RomFS section!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
    {
        uiDrawString("Error: RomFS section directory path is too long!", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Generate current path
    if (entry->nameLen)
    {
        strcat(romfs_path, "/");
        strncat(romfs_path, (char*)entry->name, entry->nameLen);
        
        strcat(output_path, "/");
        strncat(output_path, (char*)entry->name, entry->nameLen);
        removeIllegalCharacters(output_path + orig_output_path_len + 1);
        mkdir(output_path, 0744);
    }
    
    if (entry->childFile != ROMFS_ENTRY_EMPTY)
    {
        if (!recursiveDumpRomFsFile(entry->childFile, romfs_path, output_path, progressCtx, true))
        {
            romfs_path[orig_romfs_path_len] = '\0';
            output_path[orig_output_path_len] = '\0';
            return false;
        }
    }
    
    if (entry->childDir != ROMFS_ENTRY_EMPTY)
    {
        if (!recursiveDumpRomFsDir(entry->childDir, romfs_path, output_path, progressCtx))
        {
            romfs_path[orig_romfs_path_len] = '\0';
            output_path[orig_output_path_len] = '\0';
            return false;
        }
    }
    
    romfs_path[orig_romfs_path_len] = '\0';
    output_path[orig_output_path_len] = '\0';
    
    if (entry->sibling != ROMFS_ENTRY_EMPTY)
    {
        if (!recursiveDumpRomFsDir(entry->sibling, romfs_path, output_path, progressCtx)) return false;
    }
    
    return true;
}

bool dumpRomFsSectionData(FsDeviceOperator* fsOperator, u32 appIndex)
{
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char romFsPath[NAME_BUF_LEN * 2] = {'\0'}, dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    initRomFsContext();
    
    if (!gameCardAppCount)
    {
        uiDrawString("Error: invalid gamecard application count!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (appIndex > (gameCardAppCount - 1))
    {
        uiDrawString("Error: invalid gamecard application index!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName(DUMP_APP_NSP, appIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    // Replace "(BASE)" with "(RomFS)"
    dumpName[strlen(dumpName) - 6] = '\0';
    strcat(dumpName, "(RomFS)");
    
    // Retrieve RomFS from Program NCA
    if (!readProgramNcaRomFs(appIndex)) goto out;
    
    // Calculate total dump size
    if (!calculateRomFsExtractedDataSize(&(progressCtx.totalSize))) goto out;
    
    breaks++;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Extracted RomFS dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Prepare output dump path
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s", dumpName);
    mkdir(dumpPath, 0744);
    
    // Start dump process
    breaks++;
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
    }
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    success = recursiveDumpRomFsDir(0, romFsPath, dumpPath, &progressCtx);
    
    breaks += 6;
    
    if (success)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
    } else {
        setProgressBarError(&progressCtx);
        removeDirectory(dumpPath);
    }
    
out:
    freeRomFsContext();
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpFileFromRomFsSection(u32 appIndex, u32 file_offset, bool doSplitting)
{
    if (!romFsContext.romfs_filetable_size || file_offset > romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries || appIndex > (gameCardAppCount - 1))
    {
        uiDrawString("Error: invalid parameters to parse file entry from RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 *buf = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    char tmp_idx[5];
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    romfs_file *entry = (romfs_file*)((u8*)romFsContext.romfs_file_entries + file_offset);
    
    // Check if we're dealing with a nameless file
    if (!entry->nameLen)
    {
        uiDrawString("Error: file entry without name in RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName(DUMP_APP_NSP, appIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    // Replace "(BASE)" with "(RomFS)"
    dumpName[strlen(dumpName) - 6] = '\0';
    strcat(dumpName, "(RomFS)");
    
    // Generate output path
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s", dumpName);
    mkdir(dumpPath, 0744);
    
    // Create subdirectories
    char *tmp1 = NULL;
    char *tmp2 = NULL;
    
    tmp1 = strchr(curRomFsPath, '/');
    
    while(tmp1 != NULL)
    {
        tmp1++;
        
        if (!strlen(tmp1)) break;
        
        strcat(dumpPath, "/");
        
        size_t cur_len = strlen(dumpPath);
        
        tmp2 = strchr(tmp1, '/');
        if (tmp2 != NULL)
        {
            strncat(dumpPath, tmp1, tmp2 - tmp1);
            
            tmp1 = tmp2;
        } else {
            strcat(dumpPath, tmp1);
            
            tmp1 = NULL;
        }
        
        removeIllegalCharacters(dumpPath + cur_len);
        
        mkdir(dumpPath, 0744);
    }
    
    strcat(dumpPath, "/");
    strncat(dumpPath, (char*)entry->name, entry->nameLen);
    
    progressCtx.totalSize = entry->dataSize;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Hold %s to cancel.", NINTENDO_FONT_B);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
    }
    
    // Start dump process
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"romfs:%s/%.*s\"...", curRomFsPath, entry->nameLen, entry->name);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
    {
        sprintf(tmp_idx, ".%02u", splitIndex);
        strcat(dumpPath, tmp_idx);
    }
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString("Failed to open output file!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        goto out;
    }
    
    buf = malloc(DUMP_BUFFER_SIZE);
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        goto out;
    }
    
    progressCtx.line_offset = (breaks + 2);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    for(progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
    {
        uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        uiRefreshDisplay();
        
        if (DUMP_BUFFER_SIZE > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
        
        breaks += 4;
        proceed = processNcaCtrSectionBlock(&(romFsContext.ncmStorage), &(romFsContext.ncaId), romFsContext.romfs_filedata_offset + entry->dataOff + progressCtx.curOffset, buf, n, &(romFsContext.aes_ctx), false);
        breaks -= 4;
        
        if (!proceed) break;
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
        {
            u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
            u64 old_file_chunk_size = (n - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                    uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    break;
                }
            }
            
            fclose(outFile);
            outFile = NULL;
            
            if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                char *tmp = strrchr(dumpPath, '.');
                if (tmp != NULL) *tmp = '\0';
                
                splitIndex++;
                sprintf(tmp_idx, ".%02u", splitIndex);
                strcat(dumpPath, tmp_idx);
                
                outFile = fopen(dumpPath, "wb");
                if (!outFile)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                    uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(buf, 1, n, outFile);
            if (write_res != n)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                uiDrawString(strbuf, 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((breaks + 6) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(&progressCtx, true, n);
        
        if ((progressCtx.curOffset + n) < progressCtx.totalSize && ((progressCtx.curOffset / DUMP_BUFFER_SIZE) % 10) == 0)
        {
            hidScanInput();
            
            u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
            if (keysDown & KEY_B)
            {
                uiDrawString("Process canceled.", 8, ((breaks + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                break;
            }
        }
    }
    
    if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
    
    // Support empty files
    if (!progressCtx.totalSize)
    {
        uiFill(0, (breaks * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        progressCtx.progress = 100;
        
        printProgressBar(&progressCtx, false, 0);
    }
    
    breaks += 4;
    
    if (success)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
    } else {
        setProgressBarError(&progressCtx);
    }
    
out:
    if (buf) free(buf);
    
    if (outFile) fclose(outFile);
    
    if (!success)
    {
        if (fat32_error) breaks += 2;
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
        {
            for(u8 i = 0; i <= splitIndex; i++)
            {
                char *tmp = strrchr(dumpPath, '.');
                if (tmp != NULL) *tmp = '\0';
                
                sprintf(tmp_idx, ".%02u", splitIndex);
                strcat(dumpPath, tmp_idx);
                unlink(dumpPath);
            }
        } else {
            unlink(dumpPath);
        }
    }
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpGameCertificate(FsDeviceOperator* fsOperator)
{
    u32 crc = 0;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    bool success = false;
    FILE *outFile = NULL;
    char filename[NAME_BUF_LEN * 2] = {'\0'};
    u8 buf[CERT_SIZE];
    size_t write_res;
    
    char *dumpName = generateDumpFullName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
    {
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;*/
        
        if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, 0)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            breaks++;*/
            
            if (CERT_SIZE <= freeSpace)
            {
                if (R_SUCCEEDED(result = fsStorageRead(&gameCardStorage, CERT_OFFSET, buf, CERT_SIZE)))
                {
                    // Calculate CRC32
                    crc32(buf, CERT_SIZE, &crc);
                    
                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Certificate (%08X).bin", dumpName, crc);
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping game card certificate to \"%s\"...", filename);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                    breaks += 2;
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        breaks += 2;
                    }
                    
                    uiRefreshDisplay();
                    
                    outFile = fopen(filename, "wb");
                    if (outFile)
                    {
                        write_res = fwrite(buf, 1, CERT_SIZE, outFile);
                        if (write_res == CERT_SIZE)
                        {
                            success = true;
                            uiDrawString("Process successfully completed!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %u bytes certificate data! (wrote %lu bytes)", CERT_SIZE, write_res);
                            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        }
                        
                        fclose(outFile);
                        if (!success) unlink(filename);
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", filename);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    }
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%08X", result, CERT_OFFSET);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                }
            } else {
                uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
            
            fsStorageClose(&gameCardStorage);
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    breaks += 2;
    
    free(dumpName);
    
    return success;
}
