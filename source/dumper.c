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
#include "keys.h"

/* Extern variables */

extern bool runningSxOs;

extern FsDeviceOperator fsOperatorInstance;

extern nca_keyset_t nca_keyset;

extern u64 freeSpace;

extern int breaks;
extern int font_height;

extern u64 gameCardSize, trimmedCardSize;
extern char trimmedCardSizeStr[32];

extern u8 *hfs0_header;
extern u64 hfs0_offset, hfs0_size;
extern u32 hfs0_partition_cnt;

extern u8 *partitionHfs0Header;
extern u64 partitionHfs0HeaderOffset, partitionHfs0HeaderSize;
extern u32 partitionHfs0FileCount, partitionHfs0StrTableSize;

extern u32 titleAppCount;
extern FsStorageId *titleAppStorageId;

extern u32 titlePatchCount;
extern FsStorageId *titlePatchStorageId;

extern u32 titleAddOnCount;
extern FsStorageId *titleAddOnStorageId;

extern u32 sdCardTitleAppCount;
extern u32 sdCardTitlePatchCount;
extern u32 sdCardTitleAddOnCount;

extern u32 nandUserTitleAppCount;
extern u32 nandUserTitlePatchCount;
extern u32 nandUserTitleAddOnCount;

extern AppletType programAppletType;

extern exefs_ctx_t exeFsContext;
extern romfs_ctx_t romFsContext;
extern bktr_ctx_t bktrContext;

extern char curRomFsPath[NAME_BUF_LEN];
extern u32 curRomFsDirOffset;

extern char strbuf[NAME_BUF_LEN * 4];

extern char *filenameBuffer;
extern char *filenames[FILENAME_MAX_CNT];
extern int filenamesCount;

extern u8 *fileNormalIconBuf;

void workaroundPartitionZeroAccess()
{
    FsGameCardHandle handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle))) return;
    
    FsStorage gameCardStorage;
    if (R_FAILED(fsOpenGameCardStorage(&gameCardStorage, &handle, 0))) return;
    
    fsStorageClose(&gameCardStorage);
}

bool dumpCartridgeImage(bool isFat32, bool setXciArchiveBit, bool dumpCert, bool trimDump, bool calcCrc)
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
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        // We're probably dealing with a forced XCI dump
        dumpName = calloc(16, sizeof(char));
        if (!dumpName)
        {
            uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            breaks += 2;
            return false;
        }
        
        sprintf(dumpName, "gamecard");
    }
    
    for(partition = 0; partition < ISTORAGE_PARTITION_CNT; partition++)
    {
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Getting partition #%u size...", partition);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks++;*/
        
        if (partition == (ISTORAGE_PARTITION_CNT - 1) && runningSxOs)
        {
            // Total size for IStorage instances is maxed out under SX OS, so let's manually reduce the size for the last instance
            
            u64 partitionSizesSum = 0;
            for(int i = 0; i < (ISTORAGE_PARTITION_CNT - 1); i++) partitionSizesSum += partitionSizes[i];
            
            // Substract the total ECC block size as well as the size for previous IStorage instances
            partitionSizes[partition] = ((gameCardSize - ((gameCardSize / GAMECARD_ECC_BLOCK_SIZE) * GAMECARD_ECC_DATA_SIZE)) - partitionSizesSum);
            
            xciDataSize += partitionSizes[partition];
            convertSize(partitionSizes[partition], partitionSizesStr[partition], sizeof(partitionSizesStr[partition]) / sizeof(partitionSizesStr[partition][0]));
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u size: %s (%lu bytes).", partition, partitionSizesStr[partition], partitionSizes[partition]);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            breaks += 2;*/
        } else {
            workaroundPartitionZeroAccess();
            
            if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
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
                    // Temporary, we'll use this to check if the dump already exists (it should have the archive bit set if so)
                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xci", XCI_DUMP_PATH, dumpName);
                } else {
                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, splitIndex);
                }
            } else {
                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xci", XCI_DUMP_PATH, dumpName);
            }
            
            // Check if the dump already exists
            if (checkIfFileExists(filename))
            {
                // Ask the user if they want to proceed anyway
                int cur_breaks = breaks;
                breaks++;
                
                proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
                if (!proceed)
                {
                    uiDrawString("Process canceled.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                } else {
                    // Remove the prompt from the screen
                    breaks = cur_breaks;
                    uiFill(0, 8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                }
            }
            
            if (proceed)
            {
                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && setXciArchiveBit)
                {
                    // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
                    // Better safe than sorry
                    unlink(filename);
                    removeDirectory(filename);
                    
                    mkdir(filename, 0744);
                    
                    sprintf(tmp_idx, "/%02u", splitIndex);
                    strcat(filename, tmp_idx);
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
                            
                            workaroundPartitionZeroAccess();
                            
                            if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
                            {
                                if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, partition)))
                                {
                                    for(partitionOffset = 0; partitionOffset < partitionSizes[partition]; partitionOffset += n, progressCtx.curOffset += n)
                                    {
                                        uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                        
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
                                        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                        
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping IStorage partition #%u...", partition);
                                        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                        
                                        if (DUMP_BUFFER_SIZE > (partitionSizes[partition] - partitionOffset)) n = (partitionSizes[partition] - partitionOffset);
                                        
                                        if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset, buf, n)))
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX for partition #%u", result, partitionOffset, partition);
                                            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            proceed = false;
                                            break;
                                        }
                                        
                                        // Remove gamecard certificate
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
                                                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xci/%02u", XCI_DUMP_PATH, dumpName, splitIndex);
                                                } else {
                                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, splitIndex);
                                                }
                                                
                                                outFile = fopen(filename, "wb");
                                                if (!outFile)
                                                {
                                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                    proceed = false;
                                                    break;
                                                }
                                                
                                                if (new_file_chunk_size > 0)
                                                {
                                                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                                    if (write_res != new_file_chunk_size)
                                                    {
                                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                                                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                                                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                
                                                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                                                {
                                                    uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 8, ((progressCtx.line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                                                uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                proceed = false;
                                                break;
                                            }
                                        }
                                    }
                                    
                                    if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
                                    
                                    // Support empty files
                                    if (!partitionSizes[partition])
                                    {
                                        uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                        
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
                                        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                        
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping IStorage partition #%u...", partition);
                                        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                        
                                        printProgressBar(&progressCtx, false, 0);
                                    }
                                    
                                    fsStorageClose(&gameCardStorage);
                                } else {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed for partition #%u! (0x%08X)", partition, result);
                                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                    proceed = false;
                                }
                            } else {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed for partition #%u! (0x%08X)", partition, result);
                                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                proceed = false;
                            }
                            
                            if (!proceed)
                            {
                                setProgressBarError(&progressCtx);
                                break;
                            }
                        }
                        
                        free(buf);
                        
                        breaks = (progressCtx.line_offset + 2);
                        if (fat32_error) breaks += 2;
                    } else {
                        uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    }
                    
                    if (outFile) fclose(outFile);
                    
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
                            snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xci", XCI_DUMP_PATH, dumpName);
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
                                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xci", XCI_DUMP_PATH, dumpName);
                                removeDirectory(filename);
                            } else {
                                for(u8 i = 0; i <= splitIndex; i++)
                                {
                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, i);
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
            }
        } else {
            uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    }
    
    breaks += 2;
    
    free(dumpName);
    
    return success;
}

bool dumpNintendoSubmissionPackage(nspDumpType selectedNspDumpType, u32 titleIndex, bool isFat32, bool calcCrc, bool removeConsoleData, bool tiklessDump, bool batch)
{
    Result result;
    u32 i = 0, j = 0;
    u32 written = 0;
    u32 total = 0;
    u32 titleCount = 0;
    u32 ncmTitleIndex = 0;
    u32 titleNcaCount = 0;
    u32 partition = 0;
    
    FsStorageId curStorageId;
    
    FsGameCardHandle handle;
    
    FsStorage gameCardStorage;
    memset(&gameCardStorage, 0, sizeof(FsStorage));
    
    NcmContentMetaDatabase ncmDb;
    memset(&ncmDb, 0, sizeof(NcmContentMetaDatabase));
    
    NcmContentMetaRecordsHeader contentRecordsHeader;
    memset(&contentRecordsHeader, 0, sizeof(NcmContentMetaRecordsHeader));
    
    u64 contentRecordsHeaderReadSize = 0;
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    NcmApplicationContentMetaKey *titleList = NULL;
    NcmContentRecord *titleContentRecords = NULL;
    size_t titleListSize = sizeof(NcmApplicationContentMetaKey);
    
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
    
    u32 cnmtNcaIndex = 0;
    u8 *cnmtNcaBuf = NULL;
    bool cnmtFound = false;
    char *cnmtXml = NULL;
    
    u32 programNcaIndex = 0;
    u64 programInfoXmlSize = 0;
    char *programInfoXml = NULL;
    
    u32 nacpNcaIndex = 0;
    u64 nacpXmlSize = 0;
    char *nacpXml = NULL;
    
    u8 nacpIconCnt = 0;
    nacp_icons_ctx *nacpIcons = NULL;
    
    u32 legalInfoNcaIndex = 0;
    u64 legalInfoXmlSize = 0;
    char *legalInfoXml = NULL;
    
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
    bool proceed = true, success = false, dumping = false, fat32_error = false, removeFile = true;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char tmp_idx[5];
    
    size_t write_res;
    
    int initial_breaks = breaks;
    
    if ((selectedNspDumpType == DUMP_APP_NSP && !titleAppStorageId) || (selectedNspDumpType == DUMP_PATCH_NSP && !titlePatchStorageId) || (selectedNspDumpType == DUMP_ADDON_NSP && !titleAddOnStorageId))
    {
        uiDrawString("Error: title storage ID unavailable!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    curStorageId = (selectedNspDumpType == DUMP_APP_NSP ? titleAppStorageId[titleIndex] : (selectedNspDumpType == DUMP_PATCH_NSP ? titlePatchStorageId[titleIndex] : titleAddOnStorageId[titleIndex]));
    
    if (curStorageId != FsStorageId_GameCard && curStorageId != FsStorageId_SdCard && curStorageId != FsStorageId_NandUser)
    {
        uiDrawString("Error: invalid title storage ID!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    switch(curStorageId)
    {
        case FsStorageId_GameCard:
            titleCount = (selectedNspDumpType == DUMP_APP_NSP ? titleAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? titlePatchCount : titleAddOnCount));
            ncmTitleIndex = titleIndex;
            break;
        case FsStorageId_SdCard:
            titleCount = (selectedNspDumpType == DUMP_APP_NSP ? sdCardTitleAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? sdCardTitlePatchCount : sdCardTitleAddOnCount));
            ncmTitleIndex = titleIndex;
            break;
        case FsStorageId_NandUser:
            titleCount = (selectedNspDumpType == DUMP_APP_NSP ? nandUserTitleAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? nandUserTitlePatchCount : nandUserTitleAddOnCount));
            ncmTitleIndex = (titleIndex - (selectedNspDumpType == DUMP_APP_NSP ? sdCardTitleAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? sdCardTitlePatchCount : sdCardTitleAddOnCount)));
            break;
        default:
            break;
    }
    
    if (!titleCount)
    {
        uiDrawString("Error: invalid title type count!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (ncmTitleIndex > (titleCount - 1))
    {
        uiDrawString("Error: invalid title index!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    titleListSize *= titleCount;
    
    char *dumpName = generateNSPDumpName(selectedNspDumpType, titleIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    // If we're dealing with a gamecard, call workaroundPartitionZeroAccess() and read the secure partition header. Otherwise, ncmContentStorageReadContentIdFile() will fail with error 0x00171002
    // Also open an IStorage instance for the HFS0 Secure partition, since we may also need it if we're dealing with a Patch with titlekey crypto, in order to retrieve the tik file
    if (curStorageId == FsStorageId_GameCard)
    {
        partition = (hfs0_partition_cnt - 1); // Select the secure partition
        
        workaroundPartitionZeroAccess();
        
        if (!getPartitionHfs0Header(partition)) goto out;
        
        if (!partitionHfs0FileCount)
        {
            uiDrawString("The Secure HFS0 partition is empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
        
        if (R_FAILED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
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
    }
    
    if (!batch)
    {
        uiDrawString("Retrieving information from encrypted NCA content files...", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        uiRefreshDisplay();
        breaks++;
    }
    
    titleList = calloc(1, titleListSize);
    if (!titleList)
    {
        uiDrawString("Error: unable to allocate memory for the ApplicationContentMetaKey struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentMetaDatabase(curStorageId, &ncmDb)))
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
    
    if (written != total)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: title count mismatch in ncmContentMetaDatabaseListApplication (%u != %u)", written, total);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseGet(&ncmDb, &(titleList[ncmTitleIndex].metaRecord), sizeof(NcmContentMetaRecordsHeader), &contentRecordsHeader, &contentRecordsHeaderReadSize)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseGet failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    titleNcaCount = (u32)(contentRecordsHeader.numContentRecords);
    
    titleContentRecords = calloc(titleNcaCount, sizeof(NcmContentRecord));
    if (!titleContentRecords)
    {
        uiDrawString("Error: unable to allocate memory for the ContentRecord struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseListContentInfo(&ncmDb, &(titleList[ncmTitleIndex].metaRecord), 0, titleContentRecords, titleNcaCount * sizeof(NcmContentRecord), &written)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseListContentInfo failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Fill information for our CNMT XML
    memset(&xml_program_info, 0, sizeof(cnmt_xml_program_info));
    xml_program_info.type = titleList[ncmTitleIndex].metaRecord.type;
    xml_program_info.title_id = titleList[ncmTitleIndex].metaRecord.titleId;
    xml_program_info.version = titleList[ncmTitleIndex].metaRecord.version;
    xml_program_info.nca_cnt = titleNcaCount;
    
    xml_content_info = calloc(titleNcaCount, sizeof(cnmt_xml_content_info));
    if (!xml_content_info)
    {
        uiDrawString("Error: unable to allocate memory for the CNMT XML content info struct!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentStorage(curStorageId, &ncmStorage)))
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
        
        // Skip Delta Fragments or any other unknown content types if we're not dealing with Patch titles dumped from installed SD/eMMC (with tiklessDump disabled)
        if (titleContentRecords[titleRecordIndex].type >= NCA_CONTENT_TYPE_DELTA && (curStorageId == FsStorageId_GameCard || selectedNspDumpType != DUMP_PATCH_NSP || tiklessDump))
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
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read header from NCA \"%s\"! (0x%08X)", xml_content_info[i].nca_id_str, result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            proceed = false;
            break;
        }
        
        // Decrypt the NCA header
        // Don't retrieve the ticket and/or titlekey if we're dealing with a Patch with titlekey crypto bundled with the inserted gamecard
        if (!decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header, &rights_info, xml_content_info[i].decrypted_nca_keys, (curStorageId != FsStorageId_GameCard)))
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
        
        // Fill information for our CNMT XML
        xml_content_info[i].keyblob = (dec_nca_header.crypto_type2 > dec_nca_header.crypto_type ? dec_nca_header.crypto_type2 : dec_nca_header.crypto_type);
        
        if (curStorageId == FsStorageId_GameCard)
        {
            if (selectedNspDumpType == DUMP_APP_NSP || selectedNspDumpType == DUMP_ADDON_NSP) 
            {
                if (has_rights_id)
                {
                    uiDrawString("Error: Rights ID field in NCA header not empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    proceed = false;
                    break;
                }
                
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
                if (!has_rights_id)
                {
                    // We could be dealing with a custom XCI mounted through SX OS, so let's change back the content distribution method
                    dec_nca_header.distribution = 0;
                } else {
                    if (!rights_info.retrieved_tik)
                    {
                        // Retrieve tik file
                        if (!getPartitionHfs0FileByName(&gameCardStorage, rights_info.tik_filename, (u8*)(&(rights_info.tik_data)), ETICKET_TIK_FILE_SIZE))
                        {
                            proceed = false;
                            break;
                        }
                        
                        memcpy(rights_info.enc_titlekey, rights_info.tik_data.titlekey_block, 0x10);
                        
                        // Load external keys
                        if (!loadExternalKeys())
                        {
                            proceed = false;
                            break;
                        }
                        
                        // Decrypt titlekey
                        u8 crypto_type = xml_content_info[i].keyblob;
                        if (crypto_type) crypto_type--;
                        
                        Aes128Context titlekey_aes_ctx;
                        aes128ContextCreate(&titlekey_aes_ctx, nca_keyset.titlekeks[crypto_type], false);
                        aes128DecryptBlock(&titlekey_aes_ctx, rights_info.dec_titlekey, rights_info.enc_titlekey);
                        
                        rights_info.retrieved_tik = true;
                    }
                    
                    memset(xml_content_info[i].decrypted_nca_keys, 0, NCA_KEY_AREA_SIZE);
                    memcpy(xml_content_info[i].decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), rights_info.dec_titlekey, 0x10);
                    
                    // Mess with the NCA header if we're dealing with a content with a populated rights ID field and if tiklessDump is true (removeConsoleData is ignored)
                    if (tiklessDump)
                    {
                        // Generate new encrypted NCA key area using titlekey
                        if (!generateEncryptedNcaKeyAreaWithTitlekey(&dec_nca_header, xml_content_info[i].decrypted_nca_keys))
                        {
                            proceed = false;
                            break;
                        }
                        
                        // Remove rights ID from NCA
                        memset(dec_nca_header.rights_id, 0, 0x10);
                        
                        // Patch ACID pubkey and recreate NCA NPDM signature if we're dealing with the Program NCA
                        if (xml_content_info[i].type == NcmContentType_Program)
                        {
                            if (!processProgramNca(&ncmStorage, &ncaId, &dec_nca_header, &(xml_content_info[i]), &ncaProgramMod))
                            {
                                proceed = false;
                                break;
                            }
                        }
                    }
                }
            }
        } else
        if (curStorageId == FsStorageId_SdCard || curStorageId == FsStorageId_NandUser)
        {
            // Only mess with the NCA header if we're dealing with a content with a populated rights ID field, and if both removeConsoleData and tiklessDump are true
            if (has_rights_id && removeConsoleData && tiklessDump)
            {
                // Generate new encrypted NCA key area using titlekey
                if (!generateEncryptedNcaKeyAreaWithTitlekey(&dec_nca_header, xml_content_info[i].decrypted_nca_keys))
                {
                    proceed = false;
                    break;
                }
                
                // Remove rights ID from NCA
                memset(dec_nca_header.rights_id, 0, 0x10);
                
                // Patch ACID pubkey and recreate NCA NPDM signature if we're dealing with the Program NCA
                if (xml_content_info[i].type == NcmContentType_Program)
                {
                    if (!processProgramNca(&ncmStorage, &ncaId, &dec_nca_header, &(xml_content_info[i]), &ncaProgramMod))
                    {
                        proceed = false;
                        break;
                    }
                }
            }
        }
        
        // Generate programinfo.xml
        if (!programInfoXml && xml_content_info[i].type == NcmContentType_Program)
        {
            programNcaIndex = i;
            
            if (!generateProgramInfoXml(&ncmStorage, &ncaId, &dec_nca_header, xml_content_info[i].decrypted_nca_keys, &ncaProgramMod, &programInfoXml, &programInfoXmlSize))
            {
                proceed = false;
                break;
            }
        }
        
        // Retrieve NACP data (XML and icons)
        if (!nacpXml && xml_content_info[i].type == NcmContentType_Icon)
        {
            nacpNcaIndex = i;
            
            if (!retrieveNacpDataFromNca(&ncmStorage, &ncaId, &dec_nca_header, xml_content_info[i].decrypted_nca_keys, &nacpXml, &nacpXmlSize, &nacpIcons, &nacpIconCnt))
            {
                proceed = false;
                break;
            }
        }
        
        // Retrieve legalinfo.xml
        if (!legalInfoXml && xml_content_info[i].type == NcmContentType_Info)
        {
            legalInfoNcaIndex = i;
            
            if (!retrieveLegalInfoXmlFromNca(&ncmStorage, &ncaId, &dec_nca_header, xml_content_info[i].decrypted_nca_keys, &legalInfoXml, &legalInfoXmlSize))
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
    
    if (!proceed) goto out;
    
    if (proceed && !cnmtFound)
    {
        uiDrawString("Error: unable to find CNMT NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
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
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read CNMT NCA \"%s\"! (0x%08X)", xml_content_info[cnmtNcaIndex].nca_id_str, result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Retrieve CNMT NCA data
    if (!retrieveCnmtNcaData(curStorageId, selectedNspDumpType, cnmtNcaBuf, &xml_program_info, &(xml_content_info[cnmtNcaIndex]), &ncaCnmtMod, &rights_info, (removeConsoleData && tiklessDump))) goto out;
    
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
    
    bool includeTikAndCert = (rights_info.has_rights_id && !tiklessDump);
    
    if (includeTikAndCert)
    {
        if (curStorageId == FsStorageId_GameCard)
        {
            // Ticket files from Patch titles bundled with gamecards have a different layout
            // Let's convert it to a normal "common" ticket
            memset(rights_info.tik_data.signature, 0xFF, 0x100);
            
            memset((u8*)(&(rights_info.tik_data)) + 0x190, 0, 0x130);
            
            rights_info.tik_data.unk1 = 0x02; // Always 0x02 ?
            
            rights_info.tik_data.master_key_rev = rights_info.rights_id[15];
            
            memcpy(rights_info.tik_data.rights_id, rights_info.rights_id, 0x10);
            
            rights_info.tik_data.unk4[4] = 0xC0; // Always 0xC0 ?
            rights_info.tik_data.unk4[5] = 0x02; // Always 0x02 ?
        } else
        if (curStorageId == FsStorageId_SdCard || curStorageId == FsStorageId_NandUser)
        {
            // Only mess with the ticket data if removeConsoleData is true, if tiklessDump is false and if we're dealing with a personalized ticket
            if (removeConsoleData && rights_info.tik_data.titlekey_type == ETICKET_TITLEKEY_PERSONALIZED)
            {
                memset(rights_info.tik_data.signature, 0xFF, 0x100);
                
                memset(rights_info.tik_data.sig_issuer, 0, 0x40);
                sprintf(rights_info.tik_data.sig_issuer, "Root-CA00000003-XS00000020");
                
                memset(rights_info.tik_data.titlekey_block, 0, 0x100);
                memcpy(rights_info.tik_data.titlekey_block, rights_info.enc_titlekey, 0x10);
                
                rights_info.tik_data.titlekey_type = ETICKET_TITLEKEY_COMMON;
                rights_info.tik_data.ticket_id = 0;
                rights_info.tik_data.device_id = 0;
                rights_info.tik_data.account_id = 0;
            }
        }
        
        // Retrieve cert file
        if (!retrieveCertData(rights_info.cert_data, (rights_info.tik_data.titlekey_type == ETICKET_TITLEKEY_PERSONALIZED))) goto out;
        
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
    
    // Add our programinfo.xml if we created it
    if (programInfoXml)
    {
        nspFileCount++;
        nspPfs0StrTableSize += NSP_PROGRAM_XML_FILENAME_LENGTH;
    }
    
    // Add our NACP XML if we created it
    if (nacpXml)
    {
        // Add icons if we retrieved them
        if (nacpIcons && nacpIconCnt)
        {
            for(i = 0; i < nacpIconCnt; i++)
            {
                nspFileCount++;
                nspPfs0StrTableSize += (strlen(nacpIcons[i].filename) + 1);
            }
        }
        
        nspFileCount++;
        nspPfs0StrTableSize += NSP_NACP_XML_FILENAME_LENGTH;
    }
    
    // Add our legalinfo.xml if we retrieved it
    if (legalInfoXml)
    {
        nspFileCount++;
        nspPfs0StrTableSize += NSP_LEGAL_XML_FILENAME_LENGTH;
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
    full_nsp_header_size = (sizeof(pfs0_header) + ((u64)nspFileCount * sizeof(pfs0_entry_table)) + nspPfs0StrTableSize);
    
    // Round up our full NSP header size to a 0x10-byte boundary
    if (!(full_nsp_header_size % 0x10)) full_nsp_header_size++; // If it's already rounded, add more padding
    full_nsp_header_size = round_up(full_nsp_header_size, 0x10);
    
    // Determine our String Table size
    nspPfs0Header.str_table_size = (full_nsp_header_size - (sizeof(pfs0_header) + ((u64)nspFileCount * sizeof(pfs0_entry_table))));
    
    // Calculate total dump size
    progressCtx.totalSize = full_nsp_header_size;
    
    for(i = 0; i < titleNcaCount; i++) progressCtx.totalSize += xml_content_info[i].size;
    
    progressCtx.totalSize += strlen(cnmtXml);
    
    if (programInfoXml) progressCtx.totalSize += programInfoXmlSize;
    
    if (nacpXml)
    {
        if (nacpIcons && nacpIconCnt)
        {
            for(i = 0; i < nacpIconCnt; i++) progressCtx.totalSize += nacpIcons[i].icon_size;
        }
        
        progressCtx.totalSize += nacpXmlSize;
    }
    
    if (legalInfoXml) progressCtx.totalSize += legalInfoXmlSize;
    
    if (includeTikAndCert) progressCtx.totalSize += (ETICKET_TIK_FILE_SIZE + ETICKET_CERT_FILE_SIZE);
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    
    if (!batch)
    {
        breaks++;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Total NSP dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        uiRefreshDisplay();
        breaks++;
    }
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Temporary, we'll use this to check if the dump already exists (it should have the archive bit set if so)
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
    
    // Check if the dump already exists
    if (!batch && checkIfFileExists(dumpPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        breaks++;
        
        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
        if (!proceed)
        {
            uiDrawString("Process canceled.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            removeFile = false;
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        }
    }
    
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
    
    if (!batch)
    {
        breaks++;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        uiRefreshDisplay();
        breaks += 2;
    }
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
    }
    
    // Write placeholder zeroes
    write_res = fwrite(buf, 1, full_nsp_header_size, outFile);
    if (write_res != full_nsp_header_size)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes placeholder data to file offset 0x%016lX! (wrote %lu bytes)", full_nsp_header_size, (u64)0, write_res);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    progressCtx.curOffset = full_nsp_header_size;
    
    // Calculate DUMP_BUFFER_SIZE block numbers for the modified Program NCA data blocks
    if (ncaProgramMod.block_mod_cnt > 0)
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
            uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping NCA content \"%s\"...", xml_content_info[i].nca_id_str);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            if (DUMP_BUFFER_SIZE > (xml_content_info[i].size - nca_offset)) n = (xml_content_info[i].size - nca_offset);
            
            if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, nca_offset, buf, n)))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read %lu bytes chunk at offset 0x%016lX from NCA \"%s\"! (0x%08X)", n, nca_offset, xml_content_info[i].nca_id_str, result);
                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                proceed = false;
                break;
            }
            
            // Replace NCA header with our modified one
            if (nca_offset == 0) memcpy(buf, xml_content_info[i].encrypted_header_mod, NCA_FULL_HEADER_LENGTH);
            
            // Replace modified Program NCA data blocks
            if (ncaProgramMod.block_mod_cnt > 0 && xml_content_info[i].type == NcmContentType_Program)
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
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                {
                    splitIndex++;
                    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, splitIndex);
                    
                    outFile = fopen(dumpPath, "wb");
                    if (!outFile)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    
                    if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 8, ((progressCtx.line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                    uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
            uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), strrchr(dumpPath, '/' ) + 1);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping NCA content \"%s\"...", xml_content_info[i].nca_id_str);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            printProgressBar(&progressCtx, false, 0);
        }
        
        // Update content info
        sha256ContextGetHash(&nca_hash_ctx, xml_content_info[i].hash);
        convertDataToHexString(xml_content_info[i].hash, 32, xml_content_info[i].hash_str, 65);
        memcpy(xml_content_info[i].nca_id, xml_content_info[i].hash, 16);
        convertDataToHexString(xml_content_info[i].nca_id, 16, xml_content_info[i].nca_id_str, 33);
    }
    
    if (!proceed) goto out;
    
    uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
    uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    uiDrawString("Writing PFS0 header...", 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
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
        char ncaFileName[100] = {'\0'};
        u64 cur_file_size = 0;
        
        if (i < titleNcaCount)
        {
            // Always reserve the first titleNcaCount entries for our NCA contents
            sprintf(ncaFileName, "%s.%s", xml_content_info[i].nca_id_str, (i == cnmtNcaIndex ? "cnmt.nca" : "nca"));
            cur_file_size = xml_content_info[i].size;
        } else
        if (i == titleNcaCount)
        {
            // Reserve the entry right after our NCA contents for the CNMT XML
            sprintf(ncaFileName, "%s.cnmt.xml", xml_content_info[cnmtNcaIndex].nca_id_str);
            cur_file_size = strlen(cnmtXml);
        } else {
            // Deal with additional files packed into the PFS0, in the following order:
            // programinfo.xml (if available)
            // NACP icons (if available)
            // NACP XML (if available)
            // legalinfo.xml (if available)
            // Ticket (if available)
            // Certificate chain (if available)
            
            if (programInfoXml && i == (titleNcaCount + 1))
            {
                // programinfo.xml entry
                sprintf(ncaFileName, "%s.programinfo.xml", xml_content_info[programNcaIndex].nca_id_str);
                cur_file_size = programInfoXmlSize;
            } else
            if (nacpIcons && nacpIconCnt && ((!programInfoXml && i <= (titleNcaCount + nacpIconCnt)) || (programInfoXml && i <= (titleNcaCount + 1 + nacpIconCnt))))
            {
                // NACP icon entry
                u32 icon_idx = (!programInfoXml ? (i - (titleNcaCount + 1)) : (i - (titleNcaCount + 2)));
                sprintf(ncaFileName, nacpIcons[icon_idx].filename);
                cur_file_size = nacpIcons[icon_idx].icon_size;
            } else
            if (nacpXml && ((!programInfoXml && i == (titleNcaCount + nacpIconCnt + 1)) || (programInfoXml && i == (titleNcaCount + 1 + nacpIconCnt + 1))))
            {
                // NACP XML entry
                // If there are no icons, this will effectively make it the next entry after the CNMT XML
                sprintf(ncaFileName, "%s.nacp.xml", xml_content_info[nacpNcaIndex].nca_id_str);
                cur_file_size = nacpXmlSize;
            } else
            if (legalInfoXml && ((!includeTikAndCert && i == (nspFileCount - 1)) || (includeTikAndCert && i == (nspFileCount - 3))))
            {
                // legalinfo.xml entry
                // If none of the previous conditions are met, assume we're dealing with a legalinfo.xml depending on the includeTikAndCert and counter values
                sprintf(ncaFileName, "%s.legalinfo.xml", xml_content_info[legalInfoNcaIndex].nca_id_str);
                cur_file_size = legalInfoXmlSize;
            } else {
                // tik/cert entry
                sprintf(ncaFileName, "%s", (i == (nspFileCount - 2) ? rights_info.tik_filename : rights_info.cert_filename));
                cur_file_size = (i == (nspFileCount - 2) ? ETICKET_TIK_FILE_SIZE : ETICKET_CERT_FILE_SIZE);
            }
        }
        
        nspPfs0EntryTable[i].file_size = cur_file_size;
        nspPfs0EntryTable[i].file_offset = file_offset;
        nspPfs0EntryTable[i].filename_offset = filename_offset;
        
        strcpy(nspPfs0StrTable + filename_offset, ncaFileName);
        
        file_offset += nspPfs0EntryTable[i].file_size;
        filename_offset += (strlen(ncaFileName) + 1);
    }
    
    // Write our full PFS0 header
    memcpy(buf, &nspPfs0Header, sizeof(pfs0_header));
    memcpy(buf + sizeof(pfs0_header), nspPfs0EntryTable, (u64)nspFileCount * sizeof(pfs0_entry_table));
    memcpy(buf + sizeof(pfs0_header) + ((u64)nspFileCount * sizeof(pfs0_entry_table)), nspPfs0StrTable, nspPfs0Header.str_table_size);
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, 0);
        
        outFile = fopen(dumpPath, "rb+");
        if (!outFile)
        {
            setProgressBarError(&progressCtx);
            uiDrawString("Failed to re-open output file for part #0!", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
    } else {
        rewind(outFile);
    }
    
    write_res = fwrite(buf, 1, full_nsp_header_size, outFile);
    if (write_res != full_nsp_header_size)
    {
        setProgressBarError(&progressCtx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes PFS0 header to file offset 0x%016lX! (wrote %lu bytes)", full_nsp_header_size, (u64)0, write_res);
        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, splitIndex);
        
        outFile = fopen(dumpPath, "rb+");
        if (!outFile)
        {
            setProgressBarError(&progressCtx);
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to re-open output file for part #%u!", splitIndex);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            goto out;
        }
    }
    
    fseek(outFile, 0, SEEK_END);
    
    // Now let's write the rest of the data, including our modified CNMT NCA
    for(i = (titleNcaCount - 1); i < nspFileCount; i++)
    {
        n = DUMP_BUFFER_SIZE;
        
        char ncaFileName[100] = {'\0'};
        u64 cur_file_size = 0;
        
        if (i == (titleNcaCount - 1))
        {
            // CNMT NCA
            sprintf(ncaFileName, "%s.cnmt.nca", xml_content_info[i].nca_id_str);
            cur_file_size = xml_content_info[cnmtNcaIndex].size;
        } else
        if (i == titleNcaCount)
        {
            // CNMT XML
            sprintf(ncaFileName, "%s.cnmt.xml", xml_content_info[cnmtNcaIndex].nca_id_str);
            cur_file_size = strlen(cnmtXml);
        } else {
            if (programInfoXml && i == (titleNcaCount + 1))
            {
                // programinfo.xml entry
                sprintf(ncaFileName, "%s.programinfo.xml", xml_content_info[programNcaIndex].nca_id_str);
                cur_file_size = programInfoXmlSize;
            } else
            if (nacpIcons && nacpIconCnt && ((!programInfoXml && i <= (titleNcaCount + nacpIconCnt)) || (programInfoXml && i <= (titleNcaCount + 1 + nacpIconCnt))))
            {
                // NACP icon entry
                u32 icon_idx = (!programInfoXml ? (i - (titleNcaCount + 1)) : (i - (titleNcaCount + 2)));
                sprintf(ncaFileName, nacpIcons[icon_idx].filename);
                cur_file_size = nacpIcons[icon_idx].icon_size;
            } else
            if (nacpXml && ((!programInfoXml && i == (titleNcaCount + nacpIconCnt + 1)) || (programInfoXml && i == (titleNcaCount + 1 + nacpIconCnt + 1))))
            {
                // NACP XML entry
                sprintf(ncaFileName, "%s.nacp.xml", xml_content_info[nacpNcaIndex].nca_id_str);
                cur_file_size = nacpXmlSize;
            } else
            if (legalInfoXml && ((!includeTikAndCert && i == (nspFileCount - 1)) || (includeTikAndCert && i == (nspFileCount - 3))))
            {
                // legalinfo.xml entry
                sprintf(ncaFileName, "%s.legalinfo.xml", xml_content_info[legalInfoNcaIndex].nca_id_str);
                cur_file_size = legalInfoXmlSize;
            } else {
                // tik/cert entry
                sprintf(ncaFileName, "%s", (i == (nspFileCount - 2) ? rights_info.tik_filename : rights_info.cert_filename));
                cur_file_size = (i == (nspFileCount - 2) ? ETICKET_TIK_FILE_SIZE : ETICKET_CERT_FILE_SIZE);
            }
        }
        
        for(nca_offset = 0; nca_offset < cur_file_size; nca_offset += n, progressCtx.curOffset += n)
        {
            uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Writing \"%s\"...", ncaFileName);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            uiRefreshDisplay();
            
            if (DUMP_BUFFER_SIZE > (cur_file_size - nca_offset)) n = (cur_file_size - nca_offset);
            
            // Retrieve data from its respective source
            if (i == (titleNcaCount - 1))
            {
                // CNMT NCA
                memcpy(buf, cnmtNcaBuf + nca_offset, n);
            } else
            if (i == titleNcaCount)
            {
                // CNMT XML
                memcpy(buf, cnmtXml + nca_offset, n);
            } else {
                if (programInfoXml && i == (titleNcaCount + 1))
                {
                    // programinfo.xml entry
                    memcpy(buf, programInfoXml + nca_offset, n);
                } else
                if (nacpIcons && nacpIconCnt && ((!programInfoXml && i <= (titleNcaCount + nacpIconCnt)) || (programInfoXml && i <= (titleNcaCount + 1 + nacpIconCnt))))
                {
                    // NACP icon entry
                    u32 icon_idx = (!programInfoXml ? (i - (titleNcaCount + 1)) : (i - (titleNcaCount + 2)));
                    memcpy(buf, nacpIcons[icon_idx].icon_data + nca_offset, n);
                } else
                if (nacpXml && ((!programInfoXml && i == (titleNcaCount + nacpIconCnt + 1)) || (programInfoXml && i == (titleNcaCount + 1 + nacpIconCnt + 1))))
                {
                    // NACP XML entry
                    memcpy(buf, nacpXml + nca_offset, n);
                } else
                if (legalInfoXml && ((!includeTikAndCert && i == (nspFileCount - 1)) || (includeTikAndCert && i == (nspFileCount - 3))))
                {
                    // legalinfo.xml entry
                    memcpy(buf, legalInfoXml + nca_offset, n);
                } else {
                    // tik/cert entry
                    if (i == (nspFileCount - 2))
                    {
                        memcpy(buf, (u8*)(&(rights_info.tik_data)) + nca_offset, n);
                    } else {
                        memcpy(buf, rights_info.cert_data + nca_offset, n);
                    }
                }
            }
            
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
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                {
                    splitIndex++;
                    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, splitIndex);
                    
                    outFile = fopen(dumpPath, "wb");
                    if (!outFile)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    
                    if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 8, ((progressCtx.line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                    uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
        if (!cur_file_size)
        {
            uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), strrchr(dumpPath, '/' ) + 1);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Writing \"%s\"...", ncaFileName);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            printProgressBar(&progressCtx, false, 0);
        }
    }
    
    if (!proceed) goto out;
    
    dumping = false;
    
    breaks = (progressCtx.line_offset + 2);
    
    if (progressCtx.curOffset < progressCtx.totalSize)
    {
        setProgressBarError(&progressCtx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Unexpected underdump error! Wrote %lu bytes, expected %lu bytes.", progressCtx.curOffset, progressCtx.totalSize);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    success = true;
    
    // Finalize dump
    if (!batch)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        progressCtx.progress = 100;
        progressCtx.remainingTime = 0;
        
        printProgressBar(&progressCtx, false, 0);
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
        
        uiRefreshDisplay();
    }
    
    if (!batch && calcCrc)
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
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
        
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
                uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "File: \"%s\".", strrchr(dumpPath, '/' ) + 1);
                uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                
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
                            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            proceed = false;
                            break;
                        }
                    }
                    
                    fclose(outFile);
                    outFile = NULL;
                    
                    if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                    {
                        splitIndex++;
                        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, splitIndex);
                        
                        outFile = fopen(dumpPath, "rb");
                        if (!outFile)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to re-open output file for part #%u!", splitIndex);
                            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            proceed = false;
                            break;
                        }
                        
                        if (new_file_chunk_size > 0)
                        {
                            read_res = fread(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                            if (read_res != new_file_chunk_size)
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read %lu bytes chunk from offset 0x%016lX from part #%02u! (read %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, read_res);
                                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                        uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
            }
            
            breaks = (progressCtx.line_offset + 2);
            
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
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
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
        
        if (removeFile)
        {
            snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
            {
                removeDirectory(dumpPath);
            } else {
                unlink(dumpPath);
            }
        }
    }
    
    if (nspPfs0StrTable) free(nspPfs0StrTable);
    
    if (nspPfs0EntryTable) free(nspPfs0EntryTable);
    
    if (cnmtXml) free(cnmtXml);
    
    if (cnmtNcaBuf) free(cnmtNcaBuf);
    
    if (ncaProgramMod.block_data[1]) free(ncaProgramMod.block_data[1]);
    
    if (ncaProgramMod.block_data[0]) free(ncaProgramMod.block_data[0]);
    
    if (ncaProgramMod.hash_table) free(ncaProgramMod.hash_table);
    
    if (legalInfoXml) free(legalInfoXml);
    
    if (nacpIcons) free(nacpIcons);
    
    if (nacpXml) free(nacpXml);
    
    if (programInfoXml) free(programInfoXml);
    
    serviceClose(&(ncmStorage.s));
    
    if (xml_content_info) free(xml_content_info);
    
    if (titleContentRecords) free(titleContentRecords);
    
    serviceClose(&(ncmDb.s));
    
    if (titleList) free(titleList);
    
    if (curStorageId == FsStorageId_GameCard)
    {
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
    }
    
    free(dumpName);
    
    if (!batch) breaks += 2;
    
    return success;
}

bool dumpNintendoSubmissionPackageBatch(bool dumpAppTitles, bool dumpPatchTitles, bool dumpAddOnTitles, bool isFat32, bool removeConsoleData, bool tiklessDump, bool skipDumpedTitles, batchModeSourceStorage batchModeSrc)
{
    if ((!dumpAppTitles && !dumpPatchTitles && !dumpAddOnTitles) || (batchModeSrc == BATCH_SOURCE_ALL && ((dumpAppTitles && !titleAppCount) || (dumpPatchTitles && !titlePatchCount) || (dumpAddOnTitles && !titleAddOnCount))) || (batchModeSrc == BATCH_SOURCE_SDCARD && ((dumpAppTitles && !sdCardTitleAppCount) || (dumpPatchTitles && !sdCardTitlePatchCount) || (dumpAddOnTitles && !sdCardTitleAddOnCount))) || (batchModeSrc == BATCH_SOURCE_EMMC && ((dumpAppTitles && !nandUserTitleAppCount) || (dumpPatchTitles && !nandUserTitlePatchCount) || (dumpAddOnTitles && !nandUserTitleAddOnCount))))
    {
        uiDrawString("Error: invalid parameters to perform batch NSP dump!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    u32 i, j;
    
    u32 totalTitleCount = 0, totalAppCount = 0, totalPatchCount = 0, totalAddOnCount = 0;
    
    u32 titleCount, titleIndex;
    
    char *dumpName = NULL;
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    char curName[NAME_BUF_LEN * 2] = {'\0'};
    
    int initial_breaks = breaks, cur_breaks;
    
    const u32 maxSummaryFileCount = 6;
    u32 summaryPage = 0;
    
    memset(filenameBuffer, 0, FILENAME_BUFFER_SIZE);
    filenamesCount = 0;
    
    char *nextFilename = filenameBuffer;
    
    bool proceed = true;
    
    if (dumpAppTitles)
    {
        titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titleAppCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAppCount : nandUserTitleAppCount));
        
        for(i = 0; i < titleCount; i++)
        {
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? i : (i + sdCardTitleAppCount));
            
            dumpName = generateNSPDumpName(DUMP_APP_NSP, titleIndex);
            if (!dumpName)
            {
                uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                breaks += 2;
                return false;
            }
            
            snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            // Check if this title has already been dumped
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            snprintf(curName, sizeof(curName) / sizeof(curName[0]), strrchr(dumpPath, '/') + 1);
            
            // Fix entry name length
            u32 strWidth = uiGetStrWidth(curName);
            
            if ((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
            {
                while((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    curName[strlen(curName) - 1] = '\0';
                    strWidth = uiGetStrWidth(curName);
                }
                
                strcat(curName, "...");
            }
            
            addStringToFilenameBuffer(curName, &nextFilename);
            
            totalAppCount++;
        }
        
        totalTitleCount += totalAppCount;
    }
    
    if (dumpPatchTitles)
    {
        titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titlePatchCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitlePatchCount : nandUserTitlePatchCount));
        
        for(i = 0; i < titleCount; i++)
        {
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? i : (i + sdCardTitlePatchCount));
            
            dumpName = generateNSPDumpName(DUMP_PATCH_NSP, titleIndex);
            if (!dumpName)
            {
                uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                breaks += 2;
                return false;
            }
            
            snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            // Check if this title has already been dumped
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            snprintf(curName, sizeof(curName) / sizeof(curName[0]), strrchr(dumpPath, '/') + 1);
            
            // Fix entry name length
            u32 strWidth = uiGetStrWidth(curName);
            
            if ((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
            {
                while((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    curName[strlen(curName) - 1] = '\0';
                    strWidth = uiGetStrWidth(curName);
                }
                
                strcat(curName, "...");
            }
            
            addStringToFilenameBuffer(curName, &nextFilename);
            
            totalPatchCount++;
        }
        
        totalTitleCount += totalPatchCount;
    }
    
    if (dumpAddOnTitles)
    {
        titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titleAddOnCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAddOnCount : nandUserTitleAddOnCount));
        
        for(i = 0; i < titleCount; i++)
        {
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? i : (i + sdCardTitleAddOnCount));
            
            dumpName = generateNSPDumpName(DUMP_ADDON_NSP, titleIndex);
            if (!dumpName)
            {
                uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                breaks += 2;
                return false;
            }
            
            snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            // Check if this title has already been dumped
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            snprintf(curName, sizeof(curName) / sizeof(curName[0]), strrchr(dumpPath, '/') + 1);
            
            // Fix entry name length
            u32 strWidth = uiGetStrWidth(curName);
            
            if ((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
            {
                while((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    curName[strlen(curName) - 1] = '\0';
                    strWidth = uiGetStrWidth(curName);
                }
                
                strcat(curName, "...");
            }
            
            addStringToFilenameBuffer(curName, &nextFilename);
            
            totalAddOnCount++;
        }
        
        totalTitleCount += totalAddOnCount;
    }
    
    if (!totalTitleCount)
    {
        uiDrawString("You have already dumped all titles matching the selected settings!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        breaks += 2;
        return false;
    }
    
    // Display summary
    uiDrawString("Summary:", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    strbuf[0] = '\0';
    
    if (totalAppCount)
    {
        snprintf(curName, sizeof(curName) / sizeof(curName[0]), "BASE: %u", totalAppCount);
        strcat(strbuf, curName);
    }
    
    if (totalPatchCount)
    {
        if (totalAppCount) strcat(strbuf, " | ");
        snprintf(curName, sizeof(curName) / sizeof(curName[0]), "UPD: %u", totalPatchCount);
        strcat(strbuf, curName);
    }
    
    if (totalAddOnCount)
    {
        if (totalAppCount || totalPatchCount) strcat(strbuf, " | ");
        snprintf(curName, sizeof(curName) / sizeof(curName[0]), "DLC: %u", totalAddOnCount);
        strcat(strbuf, curName);
    }
    
    strcat(strbuf, " | ");
    snprintf(curName, sizeof(curName) / sizeof(curName[0]), "Total: %u", totalTitleCount);
    strcat(strbuf, curName);
    
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks++;
    
    while(true)
    {
        cur_breaks = breaks;
        
        uiFill(0, 8 + (cur_breaks * (font_height + (font_height / 4))), FB_WIDTH, FB_HEIGHT - (8 + (cur_breaks * (font_height + (font_height / 4)))), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        if (totalTitleCount > maxSummaryFileCount)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Current page: %u", summaryPage + 1);
            uiDrawString(strbuf, 8, (cur_breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            cur_breaks++;
        }
        
        cur_breaks++;
        
        for(i = (summaryPage * maxSummaryFileCount); i < ((summaryPage * maxSummaryFileCount) + maxSummaryFileCount); i++)
        {
            if (i >= totalTitleCount) break;
            uiDrawIcon(fileNormalIconBuf, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, 8, 8 + (cur_breaks * (font_height + (font_height / 4))) + (font_height / 8));
            uiDrawString(filenames[i], BROWSER_ICON_DIMENSION + 8, (cur_breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            cur_breaks++;
        }
        
        cur_breaks++;
        
        if (totalTitleCount > maxSummaryFileCount)
        {
            uiDrawString("[ " NINTENDO_FONT_L " / " NINTENDO_FONT_R " / " NINTENDO_FONT_ZL " / " NINTENDO_FONT_ZR " ] Change page | [ " NINTENDO_FONT_A " ] Proceed | [ " NINTENDO_FONT_B " ] Cancel", 8, (cur_breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        } else {
            uiDrawString("[ " NINTENDO_FONT_A " ] Proceed | [ " NINTENDO_FONT_B " ] Cancel", 8, (cur_breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        }
        
        uiRefreshDisplay();
        
        hidScanInput();
        
        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        
        if (keysDown & KEY_A)
        {
            proceed = true;
            break;
        } else
        if (keysDown & KEY_B)
        {
            proceed = false;
            break;
        } else
        if (((keysDown & KEY_L) || (keysDown & KEY_ZL)) && totalTitleCount > maxSummaryFileCount)
        {
            if (summaryPage > 0) summaryPage--;
        } else
        if (((keysDown & KEY_R) || (keysDown & KEY_ZR)) && totalTitleCount > maxSummaryFileCount)
        {
            if (((summaryPage * maxSummaryFileCount) + maxSummaryFileCount) < totalTitleCount) summaryPage++;
        }
    }
    
    if (!proceed)
    {
        breaks = (cur_breaks + 2);
        uiDrawString("Process canceled", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    breaks = initial_breaks;
    uiFill(0, 8 + (breaks * (font_height + (font_height / 4))), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4)))), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    initial_breaks = breaks;
    
    j = 0;
    
    if (totalAppCount)
    {
        titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titleAppCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAppCount : nandUserTitleAppCount));
        
        for(i = 0; i < titleCount; i++)
        {
            breaks = initial_breaks;
            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4)))), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? i : (i + sdCardTitleAppCount));
            
            dumpName = generateNSPDumpName(DUMP_APP_NSP, titleIndex);
            if (!dumpName)
            {
                uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                breaks += 2;
                return false;
            }
            
            snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            // Check if this title has already been dumped
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Title: %u / %u.", j + 1, totalTitleCount);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            uiRefreshDisplay();
            breaks += 2;
            
            // Dump title
            if (!dumpNintendoSubmissionPackage(DUMP_APP_NSP, titleIndex, isFat32, false, removeConsoleData, tiklessDump, true)) return false;
            
            j++;
        }
    }
    
    if (totalPatchCount)
    {
        titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titlePatchCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitlePatchCount : nandUserTitlePatchCount));
        
        for(i = 0; i < titleCount; i++)
        {
            breaks = initial_breaks;
            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4)))), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? i : (i + sdCardTitlePatchCount));
            
            dumpName = generateNSPDumpName(DUMP_PATCH_NSP, titleIndex);
            if (!dumpName)
            {
                uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                breaks += 2;
                return false;
            }
            
            snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            // Check if this title has already been dumped
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Title: %u / %u.", j + 1, totalTitleCount);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            uiRefreshDisplay();
            breaks += 2;
            
            // Dump title
            if (!dumpNintendoSubmissionPackage(DUMP_PATCH_NSP, titleIndex, isFat32, false, removeConsoleData, tiklessDump, true)) return false;
            
            j++;
        }
    }
    
    if (totalAddOnCount)
    {
        titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titleAddOnCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAddOnCount : nandUserTitleAddOnCount));
        
        for(i = 0; i < titleCount; i++)
        {
            breaks = initial_breaks;
            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4)))), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? i : (i + sdCardTitleAddOnCount));
            
            dumpName = generateNSPDumpName(DUMP_ADDON_NSP, titleIndex);
            if (!dumpName)
            {
                uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                breaks += 2;
                return false;
            }
            
            snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            // Check if this title has already been dumped
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Title: %u / %u.", j + 1, totalTitleCount);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            uiRefreshDisplay();
            breaks += 2;
            
            // Dump title
            if (!dumpNintendoSubmissionPackage(DUMP_ADDON_NSP, titleIndex, isFat32, false, removeConsoleData, tiklessDump, true)) return false;
            
            j++;
        }
    }
    
    uiDrawString("Process successfully completed!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
    
    breaks += 2;
    
    return true;
}

bool dumpRawHfs0Partition(u32 partition, bool doSplitting)
{
    Result result;
    u64 partitionOffset;
    bool proceed = true, success = false, fat32_error = false;
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
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess();
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
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
        // Oddly enough, IFileSystem instances actually point to the specified partition ID filesystem. I don't understand why it doesn't work like that for IStorage, but whatever
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
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                    } else {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s - Partition %u (%s).hfs0", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    }
                    
                    // Check if the dump already exists
                    if (checkIfFileExists(filename))
                    {
                        // Ask the user if they want to proceed anyway
                        int cur_breaks = breaks;
                        
                        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
                        if (!proceed)
                        {
                            uiDrawString("Process canceled.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        } else {
                            // Remove the prompt from the screen
                            breaks = cur_breaks;
                            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                        }
                    }
                    
                    if (proceed)
                    {
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
                                    uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                    
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
                                    uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                    
                                    if (DUMP_BUFFER_SIZE > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
                                    
                                    if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset + progressCtx.curOffset, buf, n)))
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionOffset + progressCtx.curOffset);
                                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                                                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                break;
                                            }
                                        }
                                        
                                        fclose(outFile);
                                        outFile = NULL;
                                        
                                        if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                                        {
                                            splitIndex++;
                                            snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                                            
                                            outFile = fopen(filename, "wb");
                                            if (!outFile)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                break;
                                            }
                                            
                                            if (new_file_chunk_size > 0)
                                            {
                                                write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                                if (write_res != new_file_chunk_size)
                                                {
                                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                                                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                                    break;
                                                }
                                            }
                                        }
                                    } else {
                                        write_res = fwrite(buf, 1, n, outFile);
                                        if (write_res != n)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                                            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            
                                            if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                                            {
                                                uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((progressCtx.line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                                            uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            break;
                                        }
                                    }
                                }
                                
                                if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
                                
                                // Support empty files
                                if (!progressCtx.totalSize)
                                {
                                    uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                                    
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
                                    uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                                    
                                    progressCtx.progress = 100;
                                    
                                    printProgressBar(&progressCtx, false, 0);
                                }
                                
                                breaks = (progressCtx.line_offset + 2);
                                
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
                                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), i);
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

bool copyFileFromHfs0(u32 partition, const char* source, const char* dest, const u64 file_offset, const u64 size, progress_ctx_t *progressCtx, bool doSplitting)
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
    
    uiFill(0, ((progressCtx->line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"%s\"...", source);
    uiDrawString(strbuf, 8, ((progressCtx->line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    if ((destLen + 1) < NAME_BUF_LEN)
    {
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
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
                            uiFill(0, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", ((size > FAT32_FILESIZE_LIMIT && doSplitting) ? (strrchr(splitFilename, '/') + 1) : (strrchr(dest, '/') + 1)));
                            uiDrawString(strbuf, 8, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                            
                            uiRefreshDisplay();
                            
                            if (DUMP_BUFFER_SIZE > (size - off)) n = (size - off);
                            
                            if (R_FAILED(result = fsStorageRead(&gameCardStorage, file_offset + off, buf, n)))
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, file_offset + off);
                                uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                                        uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                                        uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                        break;
                                    }
                                    
                                    if (new_file_chunk_size > 0)
                                    {
                                        write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                        if (write_res != new_file_chunk_size)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                                            uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                            break;
                                        }
                                    }
                                }
                            } else {
                                write_res = fwrite(buf, 1, n, outFile);
                                if (write_res != n)
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                                    uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                    
                                    if ((off + n) > FAT32_FILESIZE_LIMIT)
                                    {
                                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((progressCtx->line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                                    uiDrawString("Process canceled.", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                                    break;
                                }
                            }
                        }
                        
                        if (off >= size) success = true;
                        
                        // Support empty files
                        if (!size)
                        {
                            uiFill(0, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                            
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", ((size > FAT32_FILESIZE_LIMIT && doSplitting) ? (strrchr(splitFilename, '/') + 1) : (strrchr(dest, '/') + 1)));
                            uiDrawString(strbuf, 8, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                            
                            if (progressCtx->totalSize == size) progressCtx->progress = 100;
                            
                            printProgressBar(progressCtx, false, 0);
                        }
                        
                        if (!success)
                        {
                            setProgressBarError(progressCtx);
                            breaks = (progressCtx->line_offset + 2);
                            if (fat32_error) breaks += 2;
                        }
                        
                        free(buf);
                    } else {
                        uiDrawString("Failed to allocate memory for the dump process!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                    uiDrawString("Failed to open output file!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                }
                
                fsStorageClose(&gameCardStorage);
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
                uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            }
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination path is too long! (%lu bytes)", destLen);
        uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    return success;
}

bool copyHfs0Contents(u32 partition, hfs0_entry_table *partitionEntryTable, progress_ctx_t *progressCtx, const char *dest, bool splitting)
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
    
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx->start));
    
    for(i = 0; i < partitionHfs0FileCount; i++)
    {
        u32 filename_offset = (HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * partitionHfs0FileCount) + partitionEntryTable[i].filename_offset);
        char *filename = ((char*)partitionHfs0Header + filename_offset);
        strcpy(dbuf + dest_len, filename);
        
        removeIllegalCharacters(dbuf + dest_len);
        
        u64 file_offset = (partitionHfs0HeaderSize + partitionEntryTable[i].file_offset);
        if (HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition) == 0) file_offset += partitionHfs0HeaderOffset;
        
        success = copyFileFromHfs0(partition, filename, dbuf, file_offset, partitionEntryTable[i].file_size, progressCtx, splitting);
        if (!success) break;
    }
    
    return success;
}

bool dumpHfs0PartitionData(u32 partition, bool doSplitting)
{
    bool success = false;
    u32 i;
    hfs0_entry_table *entryTable = NULL;
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess(); 
    
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
                    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s - Partition %u (%s)", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying partition #%u data to \"%s/\". Hold %s to cancel.", partition, dumpPath, NINTENDO_FONT_B);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
                    breaks += 2;
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        breaks += 2;
                    }
                    
                    uiRefreshDisplay();
                    
                    progressCtx.line_offset = (breaks + 4);
                    
                    success = copyHfs0Contents(partition, entryTable, &progressCtx, dumpPath, doSplitting);
                    
                    if (success)
                    {
                        breaks = (progressCtx.line_offset + 2);
                        
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

bool dumpFileFromHfs0Partition(u32 partition, u32 file, char *filename, bool doSplitting)
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
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    u64 file_offset = 0;
    u64 file_size = 0;
    bool proceed = true, success = false;
    
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
            
            snprintf(destCopyPath, sizeof(destCopyPath) / sizeof(destCopyPath[0]), "%s%s - Partition %u (%s)", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
            
            if ((strlen(destCopyPath) + 1 + strlen(filename)) < NAME_BUF_LEN)
            {
                mkdir(destCopyPath, 0744);
                
                strcat(destCopyPath, "/");
                strcat(destCopyPath, fixedFilename);
                
                breaks++;
                
                // Check if the dump already exists
                if (checkIfFileExists(destCopyPath))
                {
                    // Ask the user if they want to proceed anyway
                    int cur_breaks = breaks;
                    
                    proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
                    if (!proceed)
                    {
                        uiDrawString("Process canceled.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    } else {
                        // Remove the prompt from the screen
                        breaks = cur_breaks;
                        uiFill(0, 8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                    }
                }
                
                if (proceed)
                {
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
                    
                    success = copyFileFromHfs0(partition, filename, destCopyPath, file_offset, file_size, &progressCtx, doSplitting);
                    
                    if (success)
                    {
                        breaks = (progressCtx.line_offset + 2);
                        
                        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
                        progressCtx.now -= progressCtx.start;
                        
                        formatETAString(progressCtx.now, progressCtx.etaInfo, sizeof(progressCtx.etaInfo) / sizeof(progressCtx.etaInfo[0]));
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", progressCtx.etaInfo);
                        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 0, 255, 0);
                    }
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

bool dumpExeFsSectionData(u32 titleIndex, bool usePatch, bool doSplitting)
{
    u64 n;
    FILE *outFile;
    u8 *buf = NULL;
    u8 splitIndex;
    bool proceed = true, success = false, fat32_error = false;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'}, curDumpPath[NAME_BUF_LEN * 4] = {'\0'};
    char tmp_idx[5];
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    u32 i;
    u64 offset;
    
    if ((!usePatch && !titleAppCount) || (usePatch && !titlePatchCount))
    {
        uiDrawString("Error: invalid title count!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if ((!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString("Error: invalid title index!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (!usePatch)
    {
        // Remove " (BASE)"
        dumpName[strlen(dumpName) - 7] = '\0';
    } else {
        // Remove " (UPD)"
        dumpName[strlen(dumpName) - 6] = '\0';
    }
    
    // Retrieve ExeFS from Program NCA
    if (!readProgramNcaExeFsOrRomFs(titleIndex, usePatch, false))
    {
        free(dumpName);
        breaks += 2;
        return false;
    }
    
    // Calculate total dump size
    if (!calculateExeFsExtractedDataSize(&(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Extracted ExeFS dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Prepare output dump path
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s", EXEFS_DUMP_PATH, dumpName);
    mkdir(dumpPath, 0744);
    
    buf = malloc(DUMP_BUFFER_SIZE);
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
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
    
    for(i = 0; i < exeFsContext.exefs_header.file_cnt; i++)
    {
        n = DUMP_BUFFER_SIZE;
        outFile = NULL;
        splitIndex = 0;
        
        char *exeFsFilename = (exeFsContext.exefs_str_table + exeFsContext.exefs_entries[i].filename_offset);
        
        // Check if we're dealing with a nameless file
        if (!strlen(exeFsFilename))
        {
            uiDrawString("Error: file entry without name in ExeFS section!", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            break;
        }
        
        snprintf(curDumpPath, sizeof(curDumpPath) / sizeof(curDumpPath[0]), "%s/%s", dumpPath, exeFsFilename);
        removeIllegalCharacters(curDumpPath + strlen(dumpPath) + 1);
        
        if (exeFsContext.exefs_entries[i].file_size > FAT32_FILESIZE_LIMIT && doSplitting)
        {
            sprintf(tmp_idx, ".%02u", splitIndex);
            strcat(curDumpPath, tmp_idx);
        }
        
        outFile = fopen(curDumpPath, "wb");
        if (!outFile)
        {
            uiDrawString("Failed to open output file!", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            break;
        }
        
        uiFill(0, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"%s\"...", exeFsFilename);
        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        for(offset = 0; offset < exeFsContext.exefs_entries[i].file_size; offset += n, progressCtx.curOffset += n)
        {
            uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(curDumpPath, '/') + 1);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            uiRefreshDisplay();
            
            if (DUMP_BUFFER_SIZE > (exeFsContext.exefs_entries[i].file_size - offset)) n = (exeFsContext.exefs_entries[i].file_size - offset);
            
            breaks = (progressCtx.line_offset + 2);
            proceed = processNcaCtrSectionBlock(&(exeFsContext.ncmStorage), &(exeFsContext.ncaId), &(exeFsContext.aes_ctx), exeFsContext.exefs_data_offset + exeFsContext.exefs_entries[i].file_offset + offset, buf, n, false);
            breaks = (progressCtx.line_offset - 4);
            
            if (!proceed) break;
            
            if (exeFsContext.exefs_entries[i].file_size > FAT32_FILESIZE_LIMIT && doSplitting && (offset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
            {
                u64 new_file_chunk_size = ((offset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, offset, splitIndex, write_res);
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (new_file_chunk_size > 0 || (offset + n) < exeFsContext.exefs_entries[i].file_size)
                {
                    char *tmp = strrchr(curDumpPath, '.');
                    if (tmp != NULL) *tmp = '\0';
                    
                    splitIndex++;
                    sprintf(tmp_idx, ".%02u", splitIndex);
                    strcat(curDumpPath, tmp_idx);
                    
                    outFile = fopen(curDumpPath, "wb");
                    if (!outFile)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, offset + old_file_chunk_size, splitIndex, write_res);
                            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(buf, 1, n, outFile);
                if (write_res != n)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, offset, write_res);
                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    
                    if ((offset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((progressCtx.line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                    uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    proceed = false;
                    break;
                }
            }
        }
        
        if (outFile) fclose(outFile);
        
        if (!proceed) break;
        
        // Support empty files
        if (!exeFsContext.exefs_entries[i].file_size)
        {
            uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(curDumpPath, '/') + 1);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
            
            uiRefreshDisplay();
            
            if (progressCtx.totalSize == exeFsContext.exefs_entries[i].file_size) progressCtx.progress = 100;
            
            printProgressBar(&progressCtx, true, 0);
        }
    }
    
    if (proceed)
    {
        if (progressCtx.curOffset >= progressCtx.totalSize)
        {
            success = true;
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Unexpected underdump error! Wrote %lu bytes, expected %lu bytes.", progressCtx.curOffset, progressCtx.totalSize);
            uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        }
    }
    
    breaks = (progressCtx.line_offset + 2);
    
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
        if (fat32_error) breaks += 2;
    }
    
out:
    if (buf) free(buf);
    
    freeExeFsContext();
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpFileFromExeFsSection(u32 titleIndex, u32 fileIndex, bool usePatch, bool doSplitting)
{
    if (!exeFsContext.exefs_header.file_cnt || fileIndex > (exeFsContext.exefs_header.file_cnt - 1) || !exeFsContext.exefs_entries || !exeFsContext.exefs_str_table || exeFsContext.exefs_data_offset <= exeFsContext.exefs_offset || (!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString("Error: invalid parameters to parse file entry from ExeFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 *buf = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false, removeFile = true;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    char tmp_idx[5];
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    char *exeFsFilename = (exeFsContext.exefs_str_table + exeFsContext.exefs_entries[fileIndex].filename_offset);
    
    // Check if we're dealing with a nameless file
    if (!strlen(exeFsFilename))
    {
        uiDrawString("Error: file entry without name in ExeFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (!usePatch)
    {
        // Remove " (BASE)"
        dumpName[strlen(dumpName) - 7] = '\0';
    } else {
        // Remove " (UPD)"
        dumpName[strlen(dumpName) - 6] = '\0';
    }
    
    // Generate output path
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s", EXEFS_DUMP_PATH, dumpName);
    mkdir(dumpPath, 0744);
    
    strcat(dumpPath, "/");
    size_t cur_len = strlen(dumpPath);
    strcat(dumpPath, exeFsFilename);
    removeIllegalCharacters(dumpPath + cur_len);
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
    {
        sprintf(tmp_idx, ".%02u", splitIndex);
        strcat(dumpPath, tmp_idx);
    }
    
    progressCtx.totalSize = exeFsContext.exefs_entries[fileIndex].file_size;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    breaks++;
    
    // Check if the dump already exists
    if (checkIfFileExists(dumpPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        
        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
        if (!proceed)
        {
            uiDrawString("Process canceled.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            removeFile = false;
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        }
    }
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Hold %s to cancel.", NINTENDO_FONT_B);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString("Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
    }
    
    // Start dump process
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"%s\"...", exeFsFilename);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks += 2;
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString("Failed to open output file!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    buf = malloc(DUMP_BUFFER_SIZE);
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    progressCtx.line_offset = (breaks + 2);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    for(progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        uiRefreshDisplay();
        
        if (DUMP_BUFFER_SIZE > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
        
        breaks = (progressCtx.line_offset + 2);
        proceed = processNcaCtrSectionBlock(&(exeFsContext.ncmStorage), &(exeFsContext.ncaId), &(exeFsContext.aes_ctx), exeFsContext.exefs_data_offset + exeFsContext.exefs_entries[fileIndex].file_offset + progressCtx.curOffset, buf, n, false);
        breaks = (progressCtx.line_offset - 2);
        
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
                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(buf, 1, n, outFile);
            if (write_res != n)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((progressCtx.line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                break;
            }
        }
    }
    
    if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
    
    // Support empty files
    if (!progressCtx.totalSize)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        progressCtx.progress = 100;
        
        printProgressBar(&progressCtx, false, 0);
    }
    
    breaks = (progressCtx.line_offset + 2);
    
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
        
        if (removeFile)
        {
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
    }
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool recursiveDumpRomFsFile(u32 file_offset, char *romfs_path, char *output_path, progress_ctx_t *progressCtx, bool usePatch, bool doSplitting)
{
    if ((!usePatch && (!romFsContext.romfs_filetable_size || file_offset > romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_filetable_size || file_offset > bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || !romfs_path || !output_path || !progressCtx)
    {
        uiDrawString("Error: invalid parameters to parse file entry from RomFS section!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
    
    romfs_file *entry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + file_offset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + file_offset));
    
    // Check if we're dealing with a nameless file
    if (!entry->nameLen)
    {
        uiDrawString("Error: file entry without name in RomFS section!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
    {
        uiDrawString("Error: RomFS section file path is too long!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Generate current path
    strcat(romfs_path, "/");
    strncat(romfs_path, (char*)entry->name, entry->nameLen);
    
    strcat(output_path, "/");
    strncat(output_path, (char*)entry->name, entry->nameLen);
    removeIllegalCharacters(output_path + orig_output_path_len + 1);
    
    // Start dump process
    uiFill(0, ((progressCtx->line_offset - 4) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"romfs:%s\"...", romfs_path);
    uiDrawString(strbuf, 8, ((progressCtx->line_offset - 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    
    if (entry->dataSize > FAT32_FILESIZE_LIMIT && doSplitting)
    {
        sprintf(tmp_idx, ".%02u", splitIndex);
        strcat(output_path, tmp_idx);
    }
    
    outFile = fopen(output_path, "wb");
    if (!outFile)
    {
        uiDrawString("Failed to open output file!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        goto out;
    }
    
    buf = malloc(DUMP_BUFFER_SIZE);
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        goto out;
    }
    
    for(off = 0; off < entry->dataSize; off += n, progressCtx->curOffset += n)
    {
        uiFill(0, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(output_path, '/') + 1);
        uiDrawString(strbuf, 8, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        uiRefreshDisplay();
        
        if (DUMP_BUFFER_SIZE > (entry->dataSize - off)) n = (entry->dataSize - off);
        
        breaks = (progressCtx->line_offset + 2);
        
        if (!usePatch)
        {
            proceed = processNcaCtrSectionBlock(&(romFsContext.ncmStorage), &(romFsContext.ncaId), &(romFsContext.aes_ctx), romFsContext.romfs_filedata_offset + entry->dataOff + off, buf, n, false);
        } else {
            proceed = readBktrSectionBlock(bktrContext.romfs_filedata_offset + entry->dataOff + off, buf, n);
        }
        
        breaks = (progressCtx->line_offset - 4);
        
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
                    uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                    uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                        uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(buf, 1, n, outFile);
            if (write_res != n)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                uiDrawString(strbuf, 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                
                if ((off + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((progressCtx->line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                uiDrawString("Process canceled.", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                break;
            }
        }
    }
    
    if (off >= entry->dataSize) success = true;
    
    // Support empty files
    if (!entry->dataSize)
    {
        uiFill(0, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(output_path, '/') + 1);
        uiDrawString(strbuf, 8, ((progressCtx->line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        if (progressCtx->totalSize == entry->dataSize) progressCtx->progress = 100;
        
        printProgressBar(progressCtx, false, 0);
    }
    
out:
    if (buf) free(buf);
    
    if (outFile) fclose(outFile);
    
    if (!success)
    {
        breaks = (progressCtx->line_offset + 2);
        if (fat32_error) breaks += 2;
    }
    
    romfs_path[orig_romfs_path_len] = '\0';
    output_path[orig_output_path_len] = '\0';
    
    if (success)
    {
        if (entry->sibling != ROMFS_ENTRY_EMPTY) success = recursiveDumpRomFsFile(entry->sibling, romfs_path, output_path, progressCtx, usePatch, doSplitting);
    }
    
    return success;
}

bool recursiveDumpRomFsDir(u32 dir_offset, char *romfs_path, char *output_path, progress_ctx_t *progressCtx, bool usePatch, bool dumpSiblingDir, bool doSplitting)
{
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries || !bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || !romfs_path || !output_path || !progressCtx)
    {
        uiDrawString("Error: invalid parameters to parse directory entry from RomFS section!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    size_t orig_romfs_path_len = strlen(romfs_path);
    size_t orig_output_path_len = strlen(output_path);
    
    romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
    
    // Check if we're dealing with a nameless directory that's not the root directory
    if (!entry->nameLen && dir_offset > 0)
    {
        uiDrawString("Error: directory entry without name in RomFS section!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
    {
        uiDrawString("Error: RomFS section directory path is too long!", 8, ((progressCtx->line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
        if (!recursiveDumpRomFsFile(entry->childFile, romfs_path, output_path, progressCtx, usePatch, doSplitting))
        {
            romfs_path[orig_romfs_path_len] = '\0';
            output_path[orig_output_path_len] = '\0';
            return false;
        }
    }
    
    if (entry->childDir != ROMFS_ENTRY_EMPTY)
    {
        if (!recursiveDumpRomFsDir(entry->childDir, romfs_path, output_path, progressCtx, usePatch, true, doSplitting))
        {
            romfs_path[orig_romfs_path_len] = '\0';
            output_path[orig_output_path_len] = '\0';
            return false;
        }
    }
    
    romfs_path[orig_romfs_path_len] = '\0';
    output_path[orig_output_path_len] = '\0';
    
    if (dumpSiblingDir && entry->sibling != ROMFS_ENTRY_EMPTY)
    {
        if (!recursiveDumpRomFsDir(entry->sibling, romfs_path, output_path, progressCtx, usePatch, true, doSplitting)) return false;
    }
    
    return true;
}

bool dumpRomFsSectionData(u32 titleIndex, bool usePatch, bool doSplitting)
{
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char romFsPath[NAME_BUF_LEN * 2] = {'\0'}, dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    if ((!usePatch && !titleAppCount) || (usePatch && !titlePatchCount))
    {
        uiDrawString("Error: invalid title count!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if ((!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString("Error: invalid title index!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (!usePatch)
    {
        // Remove " (BASE)"
        dumpName[strlen(dumpName) - 7] = '\0';
    } else {
        // Remove " (UPD)"
        dumpName[strlen(dumpName) - 6] = '\0';
    }
    
    // Retrieve RomFS from Program NCA
    if (!readProgramNcaExeFsOrRomFs(titleIndex, usePatch, true))
    {
        free(dumpName);
        breaks += 2;
        return false;
    }
    
    // Calculate total dump size
    if (!calculateRomFsFullExtractedSize(usePatch, &(progressCtx.totalSize))) goto out;
    
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
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s", ROMFS_DUMP_PATH, dumpName);
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
    
    success = recursiveDumpRomFsDir(0, romFsPath, dumpPath, &progressCtx, usePatch, true, doSplitting);
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
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
    if (usePatch) freeBktrContext();
    
    freeRomFsContext();
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpFileFromRomFsSection(u32 titleIndex, u32 file_offset, bool usePatch, bool doSplitting)
{
    if (!romFsContext.romfs_filetable_size || file_offset > romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries || (!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString("Error: invalid parameters to parse file entry from RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 *buf = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false, removeFile = true;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    char tmp_idx[5];
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    romfs_file *entry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + file_offset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + file_offset));
    
    // Check if we're dealing with a nameless file
    if (!entry->nameLen)
    {
        uiDrawString("Error: file entry without name in RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (!usePatch)
    {
        // Remove " (BASE)"
        dumpName[strlen(dumpName) - 7] = '\0';
    } else {
        // Remove " (UPD)"
        dumpName[strlen(dumpName) - 6] = '\0';
    }
    
    // Generate output path
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s", ROMFS_DUMP_PATH, dumpName);
    mkdir(dumpPath, 0744);
    
    // Create subdirectories
    char *tmp1 = NULL;
    char *tmp2 = NULL;
    size_t cur_len;
    
    tmp1 = strchr(curRomFsPath, '/');
    
    while(tmp1 != NULL)
    {
        tmp1++;
        
        if (!strlen(tmp1)) break;
        
        strcat(dumpPath, "/");
        
        cur_len = strlen(dumpPath);
        
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
    cur_len = strlen(dumpPath);
    strncat(dumpPath, (char*)entry->name, entry->nameLen);
    removeIllegalCharacters(dumpPath + cur_len);
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
    {
        sprintf(tmp_idx, ".%02u", splitIndex);
        strcat(dumpPath, tmp_idx);
    }
    
    progressCtx.totalSize = entry->dataSize;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    breaks++;
    
    // Check if the dump already exists
    if (checkIfFileExists(dumpPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        
        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
        if (!proceed)
        {
            uiDrawString("Process canceled.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            removeFile = false;
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        }
    }
    
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
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString("Failed to open output file!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    buf = malloc(DUMP_BUFFER_SIZE);
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    progressCtx.line_offset = (breaks + 2);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    for(progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        uiRefreshDisplay();
        
        if (DUMP_BUFFER_SIZE > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
        
        breaks = (progressCtx.line_offset + 2);
        
        if (!usePatch)
        {
            proceed = processNcaCtrSectionBlock(&(romFsContext.ncmStorage), &(romFsContext.ncaId), &(romFsContext.aes_ctx), romFsContext.romfs_filedata_offset + entry->dataOff + progressCtx.curOffset, buf, n, false);
        } else {
            proceed = readBktrSectionBlock(bktrContext.romfs_filedata_offset + entry->dataOff + progressCtx.curOffset, buf, n);
        }
        
        breaks = (progressCtx.line_offset - 2);
        
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
                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
                    uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(buf, 1, n, outFile);
            if (write_res != n)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                uiDrawString(strbuf, 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 8, ((progressCtx.line_offset + 4) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
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
                uiDrawString("Process canceled.", 8, ((progressCtx.line_offset + 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                break;
            }
        }
    }
    
    if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
    
    // Support empty files
    if (!progressCtx.totalSize)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + 8, FB_WIDTH, (font_height + (font_height / 4)) * 2, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        uiDrawString(strbuf, 8, ((progressCtx.line_offset - 2) * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
        
        progressCtx.progress = 100;
        
        printProgressBar(&progressCtx, false, 0);
    }
    
    breaks = (progressCtx.line_offset + 2);
    
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
        
        if (removeFile)
        {
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
    }
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpCurrentDirFromRomFsSection(u32 titleIndex, bool usePatch, bool doSplitting)
{
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char romFsPath[NAME_BUF_LEN * 2] = {'\0'}, dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    if ((!usePatch && !titleAppCount) || (usePatch && !titlePatchCount))
    {
        uiDrawString("Error: invalid title count!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if ((!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString("Error: invalid title index!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex);
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    if (!usePatch)
    {
        // Remove " (BASE)"
        dumpName[strlen(dumpName) - 7] = '\0';
    } else {
        // Remove " (UPD)"
        dumpName[strlen(dumpName) - 6] = '\0';
    }
    
    // Calculate total dump size
    if (!calculateRomFsExtractedDirSize(curRomFsDirOffset, usePatch, &(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, sizeof(progressCtx.totalSizeStr) / sizeof(progressCtx.totalSizeStr[0]));
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Extracted RomFS directory size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (strlen(curRomFsPath) > 1) snprintf(romFsPath, sizeof(romFsPath) / sizeof(romFsPath[0]), curRomFsPath);
    
    // Prepare output dump path
    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "%s%s", ROMFS_DUMP_PATH, dumpName);
    mkdir(dumpPath, 0744);
    
    // Create subdirectories
    char *tmp1 = NULL;
    char *tmp2 = NULL;
    size_t cur_len;
    
    tmp1 = strchr(curRomFsPath, '/');
    
    while(tmp1 != NULL)
    {
        tmp1++;
        
        if (!strlen(tmp1)) break;
        
        tmp2 = strchr(tmp1, '/');
        if (tmp2 != NULL)
        {
            strcat(dumpPath, "/");
            
            cur_len = strlen(dumpPath);
            
            strncat(dumpPath, tmp1, tmp2 - tmp1);
            
            removeIllegalCharacters(dumpPath + cur_len);
            
            mkdir(dumpPath, 0744);
            
            tmp1 = tmp2;
        } else {
            // Skip last entry
            tmp1 = NULL;
        }
    }
    
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
    
    success = recursiveDumpRomFsDir(curRomFsDirOffset, romFsPath, dumpPath, &progressCtx, usePatch, false, doSplitting);
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
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
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpGameCardCertificate()
{
    u32 crc = 0;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    bool proceed = true, success = false;
    FILE *outFile = NULL;
    char filename[NAME_BUF_LEN * 2] = {'\0'};
    u8 buf[CERT_SIZE];
    size_t write_res;
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess();
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
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
                    
                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "%s%s - Certificate (%08X).bin", CERT_DUMP_PATH, dumpName, crc);
                    
                    // Check if the dump already exists
                    if (checkIfFileExists(filename))
                    {
                        // Ask the user if they want to proceed anyway
                        int cur_breaks = breaks;
                        
                        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
                        if (!proceed)
                        {
                            uiDrawString("Process canceled.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                        } else {
                            // Remove the prompt from the screen
                            breaks = cur_breaks;
                            uiFill(0, 8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8), FB_WIDTH, FB_HEIGHT - (8 + (breaks * (font_height + (font_height / 4))) + (font_height / 8)), BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                        }
                    }
                    
                    if (proceed)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping gamecard certificate to \"%s\"...", strrchr(filename, '/' ) + 1);
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
