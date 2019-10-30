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

extern bool highlight;
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

extern char *filenameBuffer;
extern char *filenames[FILENAME_MAX_CNT];
extern int filenamesCount;

extern u8 *enabledNormalIconBuf;
extern u8 *enabledHighlightIconBuf;
extern u8 *disabledNormalIconBuf;
extern u8 *disabledHighlightIconBuf;

extern u8 *dumpBuf;

void workaroundPartitionZeroAccess()
{
    FsGameCardHandle handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle))) return;
    
    FsStorage gameCardStorage;
    if (R_FAILED(fsOpenGameCardStorage(&gameCardStorage, &handle, 0))) return;
    
    fsStorageClose(&gameCardStorage);
}

bool dumpCartridgeImage(xciOptions *xciDumpCfg)
{
    if (!xciDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid XCI configuration struct!");
        breaks += 2;
        return false;
    }
    
    bool isFat32 = xciDumpCfg->isFat32;
    bool setXciArchiveBit = xciDumpCfg->setXciArchiveBit;
    bool keepCert = xciDumpCfg->keepCert;
    bool trimDump = xciDumpCfg->trimDump;
    bool calcCrc = xciDumpCfg->calcCrc;
    
    u64 partitionOffset = 0, xciDataSize = 0, n;
    u64 partitionSizes[ISTORAGE_PARTITION_CNT];
    char partitionSizesStr[ISTORAGE_PARTITION_CNT][32] = {'\0'}, xciDataSizeStr[32] = {'\0'}, filename[NAME_BUF_LEN * 2] = {'\0'};
    u32 partition;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    bool proceed = true, success = false, fat32_error = false;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    u32 certCrc = 0, certlessCrc = 0;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    bool seqDumpMode = false, seqDumpFileRemove = false, seqDumpFinish = false;
    char seqDumpFilename[NAME_BUF_LEN * 2] = {'\0'};
    FILE *seqDumpFile = NULL;
    u64 seqDumpFileSize = 0, seqDumpSessionOffset = 0;
    
    sequentialXciCtx seqXciCtx;
    memset(&seqXciCtx, 0, sizeof(sequentialXciCtx));
    
    char tmp_idx[5];
    
    size_t read_res, write_res;
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        // We're probably dealing with a forced XCI dump
        dumpName = calloc(16, sizeof(char));
        if (!dumpName)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
            breaks += 2;
            return false;
        }
        
        sprintf(dumpName, "gamecard");
    }
    
    // Check if we're dealing with a sequential dump
    snprintf(seqDumpFilename, MAX_ELEMENTS(seqDumpFilename), "%s%s.xci.seq", XCI_DUMP_PATH, dumpName);
    seqDumpMode = checkIfFileExists(seqDumpFilename);
    if (seqDumpMode)
    {
        // Open sequence file
        seqDumpFile = fopen(seqDumpFilename, "rb+");
        if (seqDumpFile)
        {
            // Retrieve sequence file size
            fseek(seqDumpFile, 0, SEEK_END);
            seqDumpFileSize = ftell(seqDumpFile);
            rewind(seqDumpFile);
            
            // Check file size
            if (seqDumpFileSize == sizeof(sequentialXciCtx))
            {
                // Read file contents
                read_res = fread(&seqXciCtx, 1, seqDumpFileSize, seqDumpFile);
                rewind(seqDumpFile);
                
                if (read_res == seqDumpFileSize)
                {
                    // Check if the IStorage partition index is valid
                    if (seqXciCtx.partitionIndex <= (ISTORAGE_PARTITION_CNT - 1))
                    {
                        // Restore parameters from the sequence file
                        isFat32 = true;
                        setXciArchiveBit = false;
                        keepCert = seqXciCtx.keepCert;
                        trimDump = seqXciCtx.trimDump;
                        calcCrc = seqXciCtx.calcCrc;
                        splitIndex = seqXciCtx.partNumber;
                        certCrc = seqXciCtx.certCrc;
                        certlessCrc = seqXciCtx.certlessCrc;
                        progressCtx.curOffset = ((u64)seqXciCtx.partNumber * SPLIT_FILE_SEQUENTIAL_SIZE);
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid IStorage partition index in sequential dump reference file!");
                        proceed = false;
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to read %lu bytes long sequential dump reference file! (read %lu bytes)", seqDumpFileSize, read_res);
                    proceed = false;
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: sequential dump reference file size mismatch! (%lu != %lu)", seqDumpFileSize, sizeof(sequentialXciCtx));
                proceed = false;
                seqDumpFileRemove = true;
            }
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to open existing sequential dump reference file for reading! (\"%s\")", seqDumpFilename);
            proceed = false;
        }
        
        uiRefreshDisplay();
    }
    
    if (!proceed) goto out;
    
    u64 part_size = (seqDumpMode ? SPLIT_FILE_SEQUENTIAL_SIZE : (!setXciArchiveBit ? SPLIT_FILE_XCI_PART_SIZE : SPLIT_FILE_NSP_PART_SIZE));
    
    for(partition = 0; partition < ISTORAGE_PARTITION_CNT; partition++)
    {
        /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Getting partition #%u size...", partition);
        breaks++;*/
        
        if (partition == (ISTORAGE_PARTITION_CNT - 1) && runningSxOs)
        {
            // Total size for IStorage instances is maxed out under SX OS, so let's manually reduce the size for the last instance
            
            u64 partitionSizesSum = 0;
            for(int i = 0; i < (ISTORAGE_PARTITION_CNT - 1); i++) partitionSizesSum += partitionSizes[i];
            
            // Substract the total ECC block size as well as the size for previous IStorage instances
            partitionSizes[partition] = ((gameCardSize - ((gameCardSize / GAMECARD_ECC_BLOCK_SIZE) * GAMECARD_ECC_DATA_SIZE)) - partitionSizesSum);
            
            xciDataSize += partitionSizes[partition];
            convertSize(partitionSizes[partition], partitionSizesStr[partition], MAX_ELEMENTS(partitionSizesStr[partition]));
            /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Partition #%u size: %s (%lu bytes).", partition, partitionSizesStr[partition], partitionSizes[partition]);
            breaks += 2;*/
        } else {
            workaroundPartitionZeroAccess();
            
            if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
            {
                /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "GetGameCardHandle succeeded: 0x%08X", handle.value);
                breaks++;*/
                
                if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, partition)))
                {
                    /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "OpenGameCardStorage succeeded: 0x%08X", handle.value);
                    breaks++;*/
                    
                    if (R_SUCCEEDED(result = fsStorageGetSize(&gameCardStorage, &(partitionSizes[partition]))))
                    {
                        xciDataSize += partitionSizes[partition];
                        convertSize(partitionSizes[partition], partitionSizesStr[partition], MAX_ELEMENTS(partitionSizesStr[partition]));
                        /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Partition #%u size: %s (%lu bytes).", partition, partitionSizesStr[partition], partitionSizes[partition]);
                        breaks += 2;*/
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "StorageGetSize failed! (0x%08X)", result);
                        proceed = false;
                    }
                    
                    fsStorageClose(&gameCardStorage);
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "OpenGameCardStorage failed! (0x%08X)", result);
                    proceed = false;
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "GetGameCardHandle failed! (0x%08X)", result);
                proceed = false;
            }
        }
        
        uiRefreshDisplay();
    }
    
    if (!proceed) goto out;
    
    convertSize(xciDataSize, xciDataSizeStr, MAX_ELEMENTS(xciDataSizeStr));
    /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "XCI data size: %s (%lu bytes).", xciDataSizeStr, xciDataSize);
    breaks += 2;*/
    
    if (trimDump)
    {
        progressCtx.totalSize = trimmedCardSize;
        snprintf(progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr), "%s", trimmedCardSizeStr);
        
        // Change dump size for the last IStorage partition
        u64 partitionSizesSum = 0;
        for(int i = 0; i < (ISTORAGE_PARTITION_CNT - 1); i++) partitionSizesSum += partitionSizes[i];
        
        partitionSizes[ISTORAGE_PARTITION_CNT - 1] = (trimmedCardSize - partitionSizesSum);
    } else {
        progressCtx.totalSize = xciDataSize;
        snprintf(progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr), "%s", xciDataSizeStr);
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Output dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks++;
    
    if (seqDumpMode)
    {
        // Check if the current offset doesn't exceed the total XCI size
        if (progressCtx.curOffset < progressCtx.totalSize)
        {
            // Check if the current partition offset doesn't exceed the partition size
            if (seqXciCtx.partitionOffset < partitionSizes[seqXciCtx.partitionIndex])
            {
                // Check if we have at least SPLIT_FILE_SEQUENTIAL_SIZE of free space
                if (progressCtx.totalSize <= freeSpace || (progressCtx.totalSize > freeSpace && freeSpace >= SPLIT_FILE_SEQUENTIAL_SIZE))
                {
                    // Inform that we are resuming an already started sequential dump operation
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation. Configuration parameters overrided.");
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Keep certificate: %s | Trim output dump: %s | CRC32 checksum calculation + dump verification: %s.", (keepCert ? "Yes" : "No"), (trimDump ? "Yes" : "No"), (calcCrc ? "Yes" : "No"));
                    breaks++;
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
                    proceed = false;
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid IStorage partition offset in the sequential dump reference file!");
                proceed = false;
            }
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid XCI offset in the sequential dump reference file!");
            proceed = false;
        }
    } else {
        if (progressCtx.totalSize > freeSpace)
        {
            // Check if we have at least (SPLIT_FILE_SEQUENTIAL_SIZE + sizeof(sequentialXciCtx)) of free space
            if (freeSpace >= (SPLIT_FILE_SEQUENTIAL_SIZE + sizeof(sequentialXciCtx)))
            {
                // Ask the user if they want to use the sequential dump mode
                int cur_breaks = breaks;
                breaks++;
                
                if (yesNoPrompt("There's not enough space available to generate a whole dump in this session. Do you want to use sequential dumping?\nIn this mode, the selected content will be dumped in more than one session.\nYou'll have to transfer the generated part files to a PC before continuing the process in the next session."))
                {
                    // Remove the prompt from the screen
                    breaks = cur_breaks;
                    uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
                    uiRefreshDisplay();
                    
                    // Modify config parameters
                    isFat32 = true;
                    setXciArchiveBit = false;
                    
                    part_size = SPLIT_FILE_SEQUENTIAL_SIZE;
                    
                    seqDumpMode = true;
                    seqDumpFileSize = sizeof(sequentialXciCtx);
                    
                    // Fill information in our sequential context
                    seqXciCtx.keepCert = keepCert;
                    seqXciCtx.trimDump = trimDump;
                    seqXciCtx.calcCrc = calcCrc;
                    
                    // Create sequential reference file and keep the handle to it opened
                    seqDumpFile = fopen(seqDumpFilename, "wb+");
                    if (seqDumpFile)
                    {
                        write_res = fwrite(&seqXciCtx, 1, seqDumpFileSize, seqDumpFile);
                        rewind(seqDumpFile);
                        
                        if (write_res == seqDumpFileSize)
                        {
                            // Update free space
                            freeSpace -= seqDumpFileSize;
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", seqDumpFileSize, write_res);
                            proceed = false;
                            seqDumpFileRemove = true;
                        }
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to create sequential dump reference file! (\"%s\")", seqDumpFilename);
                        proceed = false;
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    proceed = false;
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
                proceed = false;
            }
        }
    }
    
    if (!proceed) goto out;
    
    breaks++;
    
    if (seqDumpMode)
    {
        snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci.%02u", XCI_DUMP_PATH, dumpName, splitIndex);
    } else {
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            if (setXciArchiveBit)
            {
                // Temporary, we'll use this to check if the dump already exists (it should have the archive bit set if so)
                snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci", XCI_DUMP_PATH, dumpName);
            } else {
                snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, splitIndex);
            }
        } else {
            snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci", XCI_DUMP_PATH, dumpName);
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
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                goto out;
            } else {
                // Remove the prompt from the screen
                breaks = cur_breaks;
                uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
            }
        }
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && setXciArchiveBit)
        {
            // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
            // Better safe than sorry
            unlink(filename);
            fsdevDeleteDirectoryRecursively(filename);
            
            mkdir(filename, 0744);
            
            sprintf(tmp_idx, "/%02u", splitIndex);
            strcat(filename, tmp_idx);
        }
    }
    
    outFile = fopen(filename, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to open output file \"%s\"!", filename);
        goto out;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
        breaks += 2;
    }
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    u32 startPartitionIndex = (seqDumpMode ? seqXciCtx.partitionIndex : 0);
    u64 startPartitionOffset;
    
    for(partition = startPartitionIndex; partition < ISTORAGE_PARTITION_CNT; partition++)
    {
        n = DUMP_BUFFER_SIZE;
        
        startPartitionOffset = ((seqDumpMode && partition == startPartitionIndex) ? seqXciCtx.partitionOffset : 0);
        
        workaroundPartitionZeroAccess();
        
        if (R_FAILED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "GetGameCardHandle failed for partition #%u! (0x%08X)", partition, result);
            proceed = false;
            break;
        }
        
        if (R_FAILED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, partition)))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "OpenGameCardStorage failed for partition #%u! (0x%08X)", partition, result);
            proceed = false;
            break;
        }
        
        for(partitionOffset = startPartitionOffset; partitionOffset < partitionSizes[partition]; partitionOffset += n, progressCtx.curOffset += n, seqDumpSessionOffset += n)
        {
            if (seqDumpMode && seqDumpFinish) break;
            
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Dumping IStorage partition #%u...", partition);
            
            if (n > (partitionSizes[partition] - partitionOffset)) n = (partitionSizes[partition] - partitionOffset);
            
            // Check if the next read chunk will exceed the size of the current part file
            if (seqDumpMode && (seqDumpSessionOffset + n) >= (((splitIndex - seqXciCtx.partNumber) + 1) * part_size))
            {
                u64 new_file_chunk_size = ((seqDumpSessionOffset + n) - (((splitIndex - seqXciCtx.partNumber) + 1) * part_size));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                u64 remainderDumpSize = (progressCtx.totalSize - (progressCtx.curOffset + old_file_chunk_size));
                u64 remainderFreeSize = (freeSpace - (seqDumpSessionOffset + old_file_chunk_size));
                
                // Check if we have enough space for the next part
                // If so, set the chunk size to old_file_chunk_size
                if ((remainderDumpSize <= part_size && remainderDumpSize > remainderFreeSize) || (remainderDumpSize > part_size && part_size > remainderFreeSize))
                {
                    n = old_file_chunk_size;
                    seqDumpFinish = true;
                }
            }
            
            if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset, dumpBuf, n)))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "StorageRead failed (0x%08X) at offset 0x%016lX for partition #%u", result, partitionOffset, partition);
                proceed = false;
                break;
            }
            
            // Remove gamecard certificate
            if (progressCtx.curOffset == 0 && !keepCert) memset(dumpBuf + CERT_OFFSET, 0xFF, CERT_SIZE);
            
            if (calcCrc)
            {
                if (!trimDump)
                {
                    if (keepCert)
                    {
                        if (progressCtx.curOffset == 0)
                        {
                            // Update CRC32 (with gamecard certificate)
                            crc32(dumpBuf, n, &certCrc);
                            
                            // Backup gamecard certificate to an array
                            char tmpCert[CERT_SIZE] = {'\0'};
                            memcpy(tmpCert, dumpBuf + CERT_OFFSET, CERT_SIZE);
                            
                            // Remove gamecard certificate from buffer
                            memset(dumpBuf + CERT_OFFSET, 0xFF, CERT_SIZE);
                            
                            // Update CRC32 (without gamecard certificate)
                            crc32(dumpBuf, n, &certlessCrc);
                            
                            // Restore gamecard certificate to buffer
                            memcpy(dumpBuf + CERT_OFFSET, tmpCert, CERT_SIZE);
                        } else {
                            // Update CRC32 (with gamecard certificate)
                            crc32(dumpBuf, n, &certCrc);
                            
                            // Update CRC32 (without gamecard certificate)
                            crc32(dumpBuf, n, &certlessCrc);
                        }
                    } else {
                        // Update CRC32
                        crc32(dumpBuf, n, &certlessCrc);
                    }
                } else {
                    // Update CRC32
                    crc32(dumpBuf, n, &certCrc);
                }
            }
            
            if ((seqDumpMode || (!seqDumpMode && progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)) && (progressCtx.curOffset + n) >= ((splitIndex + 1) * part_size))
            {
                u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * part_size));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (((seqDumpMode && !seqDumpFinish) || !seqDumpMode) && (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize))
                {
                    splitIndex++;
                    
                    if (seqDumpMode)
                    {
                        snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci.%02u", XCI_DUMP_PATH, dumpName, splitIndex);
                    } else {
                        if (setXciArchiveBit)
                        {
                            snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci/%02u", XCI_DUMP_PATH, dumpName, splitIndex);
                        } else {
                            snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, splitIndex);
                        }
                    }
                    
                    outFile = fopen(filename, "wb");
                    if (!outFile)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                    
                    if (!seqDumpMode && (progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.");
                        fat32_error = true;
                    }
                    
                    proceed = false;
                    break;
                }
            }
            
            if (seqDumpMode) progressCtx.seqDumpCurOffset = seqDumpSessionOffset;
            printProgressBar(&progressCtx, true, n);
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                if (cancelProcessCheck(&progressCtx))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    proceed = false;
                    if (seqDumpMode) seqDumpFileRemove = true;
                    break;
                }
            }
        }
        
        fsStorageClose(&gameCardStorage);
        
        if (!proceed) break;
        
        // Support empty files
        if (!partitionSizes[partition])
        {
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Dumping IStorage partition #%u...", partition);
            
            printProgressBar(&progressCtx, false, 0);
        }
        
        if (progressCtx.curOffset >= progressCtx.totalSize || (seqDumpMode && seqDumpFinish)) success = true;
        
        if (seqDumpMode && seqDumpFinish) break;
    }
    
    if (!proceed) setProgressBarError(&progressCtx);
    
    breaks = (progressCtx.line_offset + 2);
    if (fat32_error) breaks += 2;
    
    if (outFile) fclose(outFile);
    
    if (success)
    {
        if (seqDumpMode)
        {
            if (seqDumpFinish)
            {
                // Update the sequence reference file in the SD card
                seqXciCtx.partNumber = (splitIndex + 1);
                seqXciCtx.partitionIndex = partition;
                seqXciCtx.partitionOffset = partitionOffset;
                
                if (calcCrc)
                {
                    seqXciCtx.certCrc = certCrc;
                    seqXciCtx.certlessCrc = certlessCrc;
                }
                
                write_res = fwrite(&seqXciCtx, 1, seqDumpFileSize, seqDumpFile);
                if (write_res != seqDumpFileSize)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", seqDumpFileSize, write_res);
                    success = false;
                    seqDumpFileRemove = true;
                    goto out;
                }
            } else {
                // Mark the file for deletion
                seqDumpFileRemove = true;
                
                // Finally disable sequential dump mode flag
                seqDumpMode = false;
            }
        }
        
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
        
        if (seqDumpMode && seqDumpFinish)
        {
            breaks += 2;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Please remember to exit the application and transfer the generated part file(s) to a PC before continuing in the next session!");
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Do NOT move the \"%s\" file!", strrchr(seqDumpFilename, '/' ) + 1);
        }
        
        if (!seqDumpMode && calcCrc)
        {
            breaks++;
            
            if (!trimDump)
            {
                if (keepCert)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "XCI dump CRC32 checksum (with certificate): %08X", certCrc);
                    breaks++;
                    
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "XCI dump CRC32 checksum (without certificate): %08X", certlessCrc);
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "XCI dump CRC32 checksum: %08X", certlessCrc);
                }
                
                breaks += 2;
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Starting verification process using XML database from NSWDB.COM...");
                breaks++;
                
                uiRefreshDisplay();
                
                gameCardDumpNSWDBCheck(certlessCrc);
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "XCI dump CRC32 checksum: %08X", certCrc);
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump verification disabled (not compatible with trimmed dumps).");
            }
        }
        
        // Set archive bit (only for FAT32 and if the required option is enabled)
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && setXciArchiveBit)
        {
            snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci", XCI_DUMP_PATH, dumpName);
            if (R_FAILED(result = fsdevSetArchiveBit(filename)))
            {
                breaks += 2;
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Warning: failed to set archive bit on output directory! (0x%08X)", result);
            }
        }
    } else {
        if (seqDumpMode)
        {
            for(u8 i = 0; i <= splitIndex; i++)
            {
                snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci.%02u", XCI_DUMP_PATH, dumpName, i);
                unlink(filename);
            }
        } else {
            if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
            {
                if (setXciArchiveBit)
                {
                    snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xci", XCI_DUMP_PATH, dumpName);
                    fsdevDeleteDirectoryRecursively(filename);
                } else {
                    for(u8 i = 0; i <= splitIndex; i++)
                    {
                        snprintf(filename, MAX_ELEMENTS(filename), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, i);
                        unlink(filename);
                    }
                }
            } else {
                unlink(filename);
            }
        }
    }
    
out:
    breaks += 2;
    
    if (dumpName) free(dumpName);
    
    if (seqDumpFile) fclose(seqDumpFile);
    
    if (seqDumpFileRemove) unlink(seqDumpFilename);
    
    return success;
}

bool dumpNintendoSubmissionPackage(nspDumpType selectedNspDumpType, u32 titleIndex, nspOptions *nspDumpCfg, bool batch)
{
    if (!nspDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NSP configuration struct!");
        breaks += 2;
        return false;
    }
    
    bool isFat32 = nspDumpCfg->isFat32;
    bool calcCrc = nspDumpCfg->calcCrc;
    bool removeConsoleData = nspDumpCfg->removeConsoleData;
    bool tiklessDump = nspDumpCfg->tiklessDump;
    
    Result result;
    u32 i = 0, j = 0;
    s32 written = 0;
    s32 total = 0;
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
    
    NcmContentMetaHeader contentRecordsHeader;
    memset(&contentRecordsHeader, 0, sizeof(NcmContentMetaHeader));
    
    u64 contentRecordsHeaderReadSize = 0;
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    NcmApplicationContentMetaKey *titleList = NULL;
    NcmContentInfo *titleContentInfos = NULL;
    size_t titleListSize = sizeof(NcmApplicationContentMetaKey);
    
    cnmt_xml_program_info xml_program_info;
    cnmt_xml_content_info *xml_content_info = NULL;
    
    NcmContentId ncaId;
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
    
    Sha256Context nca_hash_ctx;
    sha256ContextCreate(&nca_hash_ctx);
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    u64 n, nca_offset;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    u32 crc = 0;
    bool proceed = true, success = false, dumping = false, fat32_error = false, removeFile = true;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    bool seqDumpMode = false, seqDumpFileRemove = false, seqDumpFinish = false;
    char seqDumpFilename[NAME_BUF_LEN * 2] = {'\0'};
    FILE *seqDumpFile = NULL;
    u64 seqDumpFileSize = 0, seqDumpSessionOffset = 0;
    u8 *seqDumpNcaHashes = NULL;
    
    sequentialNspCtx seqNspCtx;
    memset(&seqNspCtx, 0, sizeof(sequentialNspCtx));
    
    char pfs0HeaderFilename[NAME_BUF_LEN * 2] = {'\0'};
    FILE *pfs0HeaderFile = NULL;
    
    char tmp_idx[5];
    
    size_t read_res, write_res;
    
    int initial_breaks = breaks;
    
    if ((selectedNspDumpType == DUMP_APP_NSP && !titleAppStorageId) || (selectedNspDumpType == DUMP_PATCH_NSP && !titlePatchStorageId) || (selectedNspDumpType == DUMP_ADDON_NSP && !titleAddOnStorageId))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: title storage ID unavailable!");
        breaks += 2;
        return false;
    }
    
    curStorageId = (selectedNspDumpType == DUMP_APP_NSP ? titleAppStorageId[titleIndex] : (selectedNspDumpType == DUMP_PATCH_NSP ? titlePatchStorageId[titleIndex] : titleAddOnStorageId[titleIndex]));
    
    if (curStorageId != FsStorageId_GameCard && curStorageId != FsStorageId_SdCard && curStorageId != FsStorageId_NandUser)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title storage ID!");
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title type count!");
        breaks += 2;
        return false;
    }
    
    if (ncmTitleIndex > (titleCount - 1))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title index!");
        breaks += 2;
        return false;
    }
    
    titleListSize *= titleCount;
    
    char *dumpName = generateNSPDumpName(selectedNspDumpType, titleIndex);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    if (!batch)
    {
        snprintf(seqDumpFilename, MAX_ELEMENTS(seqDumpFilename), "%s%s.nsp.seq", NSP_DUMP_PATH, dumpName);
        snprintf(pfs0HeaderFilename, MAX_ELEMENTS(pfs0HeaderFilename), "%s%s.nsp.hdr", NSP_DUMP_PATH, dumpName);
        
        // Check if we're dealing with a sequential dump
        seqDumpMode = checkIfFileExists(seqDumpFilename);
        if (seqDumpMode)
        {
            // Open sequence file
            seqDumpFile = fopen(seqDumpFilename, "rb+");
            if (seqDumpFile)
            {
                // Retrieve sequence file size
                fseek(seqDumpFile, 0, SEEK_END);
                seqDumpFileSize = ftell(seqDumpFile);
                rewind(seqDumpFile);
                
                // Check file size
                if (seqDumpFileSize > sizeof(sequentialNspCtx) && ((seqDumpFileSize - sizeof(sequentialNspCtx)) % SHA256_HASH_SIZE) == 0)
                {
                    // Read sequentialNspCtx struct info
                    read_res = fread(&seqNspCtx, 1, sizeof(sequentialNspCtx), seqDumpFile);
                    if (read_res == sizeof(sequentialNspCtx))
                    {
                        // Check if the storage ID is right
                        if (seqNspCtx.storageId == curStorageId)
                        {
                            // Check if we have the right amount of NCA hashes
                            if ((seqNspCtx.ncaCount * SHA256_HASH_SIZE) == (seqDumpFileSize - sizeof(sequentialNspCtx)))
                            {
                                // Allocate memory for the NCA hashes
                                seqDumpNcaHashes = calloc(1, seqDumpFileSize - sizeof(sequentialNspCtx));
                                if (seqDumpNcaHashes)
                                {
                                    // Read NCA hashes
                                    read_res = fread(seqDumpNcaHashes, 1, seqDumpFileSize - sizeof(sequentialNspCtx), seqDumpFile);
                                    rewind(seqDumpFile);
                                    
                                    if (read_res == (seqDumpFileSize - sizeof(sequentialNspCtx)))
                                    {
                                        // Restore parameters from the sequence file
                                        isFat32 = true;
                                        calcCrc = false;
                                        removeConsoleData = seqNspCtx.removeConsoleData;
                                        tiklessDump = seqNspCtx.tiklessDump;
                                        splitIndex = seqNspCtx.partNumber;
                                        progressCtx.curOffset = ((u64)seqNspCtx.partNumber * SPLIT_FILE_SEQUENTIAL_SIZE);
                                    } else {
                                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to read %lu bytes chunk from the sequential dump reference file! (read %lu bytes)", seqNspCtx.ncaCount * SHA256_HASH_SIZE, read_res);
                                        proceed = false;
                                    }
                                } else {
                                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA hashes from the sequential dump reference file!");
                                    proceed = false;
                                }
                            } else {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NCA count and/or NCA hash count in sequential dump reference file!");
                                proceed = false;
                            }
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid source storage ID in sequential dump reference file!");
                            proceed = false;
                        }
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to read %lu bytes chunk from the sequential dump reference file! (read %lu bytes)", seqDumpFileSize, read_res);
                        proceed = false;
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid sequential dump reference file size!");
                    proceed = false;
                    seqDumpFileRemove = true;
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to open existing sequential dump reference file for reading! (\"%s\")", seqDumpFilename);
                proceed = false;
            }
            
            uiRefreshDisplay();
        }
        
        if (!proceed) goto out;
    }
    
    u64 part_size = (seqDumpMode ? SPLIT_FILE_SEQUENTIAL_SIZE : SPLIT_FILE_NSP_PART_SIZE);
    
    // If we're dealing with a gamecard, call workaroundPartitionZeroAccess() and read the secure partition header. Otherwise, ncmContentStorageReadContentIdFile() will fail with error 0x00171002
    // Also open an IStorage instance for the HFS0 Secure partition, since we may also need it if we're dealing with a Patch with titlekey crypto, in order to retrieve the tik file
    if (curStorageId == FsStorageId_GameCard)
    {
        partition = (hfs0_partition_cnt - 1); // Select the secure partition
        
        workaroundPartitionZeroAccess();
        
        if (!getPartitionHfs0Header(partition)) goto out;
        
        if (!partitionHfs0FileCount)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "The Secure HFS0 partition is empty!");
            goto out;
        }
        
        if (R_FAILED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "GetGameCardHandle failed! (0x%08X)", result);
            goto out;
        }
        
        if (R_FAILED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "OpenGameCardStorage failed! (0x%08X)", result);
            goto out;
        }
    }
    
    if (!batch)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Retrieving information from encrypted NCA content files...");
        uiRefreshDisplay();
        breaks++;
    }
    
    titleList = calloc(1, titleListSize);
    if (!titleList)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the ApplicationContentMetaKey struct!");
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentMetaDatabase(&ncmDb, curStorageId)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: ncmOpenContentMetaDatabase failed! (0x%08X)", result);
        goto out;
    }
    NcmContentMetaType filter = ((NcmContentMetaType)selectedNspDumpType + NcmContentMetaType_Application);


    if (R_FAILED(result = ncmContentMetaDatabaseListApplication(&ncmDb, &total, &written, titleList, titleCount, filter)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: ncmContentMetaDatabaseListApplication failed! (0x%08X)", result);
        goto out;
    }
    
    if (!written || !total)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: ncmContentMetaDatabaseListApplication wrote no entries to output buffer!");
        goto out;
    }
    
    if (written != total)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: title count mismatch in ncmContentMetaDatabaseListApplication (%u != %u)", written, total);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseGet(&ncmDb, &(titleList[ncmTitleIndex].key), &contentRecordsHeaderReadSize, &contentRecordsHeader, sizeof(NcmContentMetaHeader))))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: ncmContentMetaDatabaseGet failed! (0x%08X)", result);
        goto out;
    }
    
    titleNcaCount = (u32)(contentRecordsHeader.content_meta_count);
    
    titleContentInfos = calloc(titleNcaCount, sizeof(NcmContentInfo));
    if (!titleContentInfos)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the ContentRecord struct!");
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseListContentInfo(&ncmDb, &written, titleContentInfos, titleNcaCount, &(titleList[ncmTitleIndex].key), 0)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: ncmContentMetaDatabaseListContentInfo failed! (0x%08X)", result);
        goto out;
    }
    
    // Fill information for our CNMT XML
    memset(&xml_program_info, 0, sizeof(cnmt_xml_program_info));
    xml_program_info.type = titleList[ncmTitleIndex].key.type;
    xml_program_info.title_id = titleList[ncmTitleIndex].key.id;
    xml_program_info.version = titleList[ncmTitleIndex].key.version;
    xml_program_info.nca_cnt = titleNcaCount;
    
    xml_content_info = calloc(titleNcaCount, sizeof(cnmt_xml_content_info));
    if (!xml_content_info)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the CNMT XML content info struct!");
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentStorage(&ncmStorage, curStorageId)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: ncmOpenContentStorage failed! (0x%08X)", result);
        goto out;
    }
    
    // Fill our CNMT XML content records, leaving the CNMT NCA at the end
    u32 titleRecordIndex;
    for(i = 0, titleRecordIndex = 0; titleRecordIndex < titleNcaCount; i++, titleRecordIndex++)
    {
        if (!cnmtFound && titleContentInfos[titleRecordIndex].content_type == NcmContentType_Meta)
        {
            cnmtFound = true;
            cnmtNcaIndex = titleRecordIndex;
            i--;
            continue;
        }
        
        // Skip Delta Fragments or any other unknown content types if we're not dealing with Patch titles dumped from installed SD/eMMC (with tiklessDump disabled)
        if (titleContentInfos[titleRecordIndex].content_type >= NCA_CONTENT_TYPE_DELTA && (curStorageId == FsStorageId_GameCard || selectedNspDumpType != DUMP_PATCH_NSP || tiklessDump))
        {
            xml_program_info.nca_cnt--;
            i--;
            continue;
        }
        
        // Fill information for our CNMT XML
        xml_content_info[i].type = titleContentInfos[titleRecordIndex].content_type;
        memcpy(xml_content_info[i].nca_id, titleContentInfos[titleRecordIndex].content_id.c, SHA256_HASH_SIZE / 2); // Temporary
        convertDataToHexString(titleContentInfos[titleRecordIndex].content_id.c, SHA256_HASH_SIZE / 2, xml_content_info[i].nca_id_str, SHA256_HASH_SIZE + 1); // Temporary
        convertNcaSizeToU64(titleContentInfos[titleRecordIndex].size, &(xml_content_info[i].size));
        convertDataToHexString(xml_content_info[i].hash, SHA256_HASH_SIZE, xml_content_info[i].hash_str, (SHA256_HASH_SIZE * 2) + 1); // Temporary
        
        memcpy(&ncaId, &(titleContentInfos[titleRecordIndex].content_id), sizeof(NcmContentId));
        
        if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, ncaHeader, NCA_FULL_HEADER_LENGTH, &ncaId, 0)))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read header from NCA \"%s\"! (0x%08X)", xml_content_info[i].nca_id_str, result);
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: Rights ID field in NCA header not empty!");
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
        if (!nacpXml && xml_content_info[i].type == NcmContentType_Control)
        {
            nacpNcaIndex = i;
            
            if (!retrieveNacpDataFromNca(&ncmStorage, &ncaId, &dec_nca_header, xml_content_info[i].decrypted_nca_keys, &nacpXml, &nacpXmlSize, &nacpIcons, &nacpIconCnt))
            {
                proceed = false;
                break;
            }
        }
        
        // Retrieve legalinfo.xml
        if (!legalInfoXml && xml_content_info[i].type == NcmContentType_Control)
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to find CNMT NCA!");
        goto out;
    }
    
    // Update NCA counter just in case we found any delta fragments
    titleNcaCount = xml_program_info.nca_cnt;
    
    // Fill information for our CNMT XML
    xml_content_info[titleNcaCount - 1].type = titleContentInfos[cnmtNcaIndex].content_type;
    memcpy(xml_content_info[titleNcaCount - 1].nca_id, titleContentInfos[cnmtNcaIndex].content_id.c, SHA256_HASH_SIZE / 2); // Temporary
    convertDataToHexString(titleContentInfos[cnmtNcaIndex].content_id.c, SHA256_HASH_SIZE / 2, xml_content_info[titleNcaCount - 1].nca_id_str, SHA256_HASH_SIZE + 1); // Temporary
    convertNcaSizeToU64(titleContentInfos[cnmtNcaIndex].size, &(xml_content_info[titleNcaCount - 1].size));
    convertDataToHexString(xml_content_info[titleNcaCount - 1].hash, SHA256_HASH_SIZE, xml_content_info[titleNcaCount - 1].hash_str, (SHA256_HASH_SIZE * 2) + 1); // Temporary
    
    memcpy(&ncaId, &(titleContentInfos[cnmtNcaIndex].content_id), sizeof(NcmContentId));
    
    // Update CNMT index
    cnmtNcaIndex = (titleNcaCount - 1);
    
    cnmtNcaBuf = malloc(xml_content_info[cnmtNcaIndex].size);
    if (!cnmtNcaBuf)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for CNMT NCA data!");
        goto out;
    }
    
    if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, cnmtNcaBuf, xml_content_info[cnmtNcaIndex].size, &ncaId, 0)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read CNMT NCA \"%s\"! (0x%08X)", xml_content_info[cnmtNcaIndex].nca_id_str, result);
        goto out;
    }
    
    // Retrieve CNMT NCA data
    if (!retrieveCnmtNcaData(curStorageId, selectedNspDumpType, cnmtNcaBuf, &xml_program_info, &(xml_content_info[cnmtNcaIndex]), &ncaCnmtMod, &rights_info, (removeConsoleData && tiklessDump))) goto out;
    
    // Generate a placeholder CNMT XML. It's length will be used to calculate the final output dump size
    /*breaks++;
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Generating placeholder CNMT XML...");
    uiRefreshDisplay();
    breaks++;*/
    
    // Make sure that the output buffer for our CNMT XML is big enough
    cnmtXml = calloc(NSP_XML_BUFFER_SIZE, sizeof(char));
    if (!cnmtXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the CNMT XML!");
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
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Generating placeholder PFS0 header...");
    uiRefreshDisplay();
    breaks++;*/
    
    memset(&nspPfs0Header, 0, sizeof(pfs0_header));
    nspPfs0Header.magic = bswap_32(PFS0_MAGIC);
    nspPfs0Header.file_cnt = nspFileCount;
    
    nspPfs0EntryTable = calloc(nspFileCount, sizeof(pfs0_entry_table));
    if (!nspPfs0EntryTable)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Unable to allocate memory for the PFS0 file entries!");
        goto out;
    }
    
    // Make sure we have enough space
    nspPfs0StrTable = calloc(nspPfs0StrTableSize * 2, sizeof(char));
    if (!nspPfs0StrTable)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Unable to allocate memory for the PFS0 string table!");
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
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
    
    if (!batch)
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Total NSP dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
        uiRefreshDisplay();
        breaks++;
        
        if (seqDumpMode)
        {
            // Check if the current offset doesn't exceed the total NSP size
            if (progressCtx.curOffset < progressCtx.totalSize)
            {
                // Check if we have at least SPLIT_FILE_SEQUENTIAL_SIZE of free space
                if (progressCtx.totalSize <= freeSpace || (progressCtx.totalSize > freeSpace && freeSpace >= SPLIT_FILE_SEQUENTIAL_SIZE))
                {
                    // Check if the NCA count is valid
                    // The CNMT NCA is excluded from the hash list
                    if (seqNspCtx.ncaCount == (titleNcaCount - 1))
                    {
                        // Check if the PFS0 file count is valid
                        if (seqNspCtx.nspFileCount == nspFileCount)
                        {
                            // Check if the current PFS0 file index is valid
                            if (seqNspCtx.fileIndex < nspFileCount)
                            {
                                for(i = 0; i < nspFileCount; i++)
                                {
                                    if (i < seqNspCtx.fileIndex)
                                    {
                                        // Exclude the CNMT NCA
                                        if (i < (titleNcaCount - 1))
                                        {
                                            // Fill information for our CNMT XML
                                            memcpy(xml_content_info[i].nca_id, seqDumpNcaHashes + (i * SHA256_HASH_SIZE), SHA256_HASH_SIZE / 2);
                                            convertDataToHexString(xml_content_info[i].nca_id, SHA256_HASH_SIZE / 2, xml_content_info[i].nca_id_str, SHA256_HASH_SIZE + 1);
                                            memcpy(xml_content_info[i].hash, seqDumpNcaHashes + (i * SHA256_HASH_SIZE), SHA256_HASH_SIZE);
                                            convertDataToHexString(xml_content_info[i].hash, SHA256_HASH_SIZE, xml_content_info[i].hash_str, (SHA256_HASH_SIZE * 2) + 1);
                                        }
                                    } else {
                                        if (i < titleNcaCount)
                                        {
                                            // Check if the offset for the current NCA is valid
                                            if (seqNspCtx.fileOffset < xml_content_info[i].size)
                                            {
                                                // Copy the SHA-256 context data, but only if we're not dealing with the CNMT NCA
                                                // NCA ID/hash for the CNMT NCA is handled in patchCnmtNca()
                                                if (i != cnmtNcaIndex) memcpy(&nca_hash_ctx, &(seqNspCtx.hashCtx), sizeof(Sha256Context));
                                            } else {
                                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NCA offset in the sequential dump reference file!");
                                                proceed = false;
                                            }
                                        } else
                                        if (i == titleNcaCount)
                                        {
                                            // Check if the offset for the CNMT XML is valid
                                            if (seqNspCtx.fileOffset >= strlen(cnmtXml))
                                            {
                                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid CNMT XML offset in the sequential dump reference file!");
                                                proceed = false;
                                            }
                                        } else {
                                            if (programInfoXml && i == (titleNcaCount + 1))
                                            {
                                                // Check if the offset for the programinfo.xml is valid
                                                if (seqNspCtx.fileOffset >= programInfoXmlSize)
                                                {
                                                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid programinfo.xml offset in the sequential dump reference file!");
                                                    proceed = false;
                                                }
                                            } else
                                            if (nacpIcons && nacpIconCnt && ((!programInfoXml && i <= (titleNcaCount + nacpIconCnt)) || (programInfoXml && i <= (titleNcaCount + 1 + nacpIconCnt))))
                                            {
                                                // Check if the offset for the NACP icon is valid
                                                u32 icon_idx = (!programInfoXml ? (i - (titleNcaCount + 1)) : (i - (titleNcaCount + 2)));
                                                if (seqNspCtx.fileOffset >= nacpIcons[icon_idx].icon_size)
                                                {
                                                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NACP icon offset in the sequential dump reference file!");
                                                    proceed = false;
                                                }
                                            } else
                                            if (nacpXml && ((!programInfoXml && i == (titleNcaCount + nacpIconCnt + 1)) || (programInfoXml && i == (titleNcaCount + 1 + nacpIconCnt + 1))))
                                            {
                                                // Check if the offset for the NACP XML is valid
                                                if (seqNspCtx.fileOffset >= nacpXmlSize)
                                                {
                                                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NACP XML offset in the sequential dump reference file!");
                                                    proceed = false;
                                                }
                                            } else
                                            if (legalInfoXml && ((!includeTikAndCert && i == (nspFileCount - 1)) || (includeTikAndCert && i == (nspFileCount - 3))))
                                            {
                                                // Check if the offset for the legalinfo.xml is valid
                                                if (seqNspCtx.fileOffset >= legalInfoXmlSize)
                                                {
                                                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid legalinfo.xml offset in the sequential dump reference file!");
                                                    proceed = false;
                                                }
                                            } else {
                                                if (i == (nspFileCount - 2))
                                                {
                                                    // Check if the offset for the ticket is valid
                                                    if (seqNspCtx.fileOffset >= ETICKET_TIK_FILE_SIZE)
                                                    {
                                                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid ticket offset in the sequential dump reference file!");
                                                        proceed = false;
                                                    }
                                                } else {
                                                    // Check if the offset for the certificate chain is valid
                                                    if (seqNspCtx.fileOffset >= ETICKET_CERT_FILE_SIZE)
                                                    {
                                                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid certificate chain offset in the sequential dump reference file!");
                                                        proceed = false;
                                                    }
                                                }
                                            }
                                        }
                                        
                                        break;
                                    }
                                }
                                
                                if (proceed)
                                {
                                    // Restore the modified Program NCA header
                                    // The NPDM signature from the NCA header is generated using cryptographically secure random numbers, so the modified header is stored during the first sequential dump session
                                    // If needed, it must be restored in later sessions
                                    if (ncaProgramMod.block_mod_cnt) memcpy(xml_content_info[programNcaIndex].encrypted_header_mod, &(seqNspCtx.programNcaHeaderMod), NCA_FULL_HEADER_LENGTH);
                                    
                                    // Inform that we are resuming an already started sequential dump operation
                                    breaks++;
                                    
                                    if (curStorageId == FsStorageId_GameCard)
                                    {
                                        if (selectedNspDumpType == DUMP_APP_NSP || selectedNspDumpType == DUMP_ADDON_NSP)
                                        {
                                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation.");
                                        } else {
                                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation. Configuration parameters overrided.");
                                            breaks++;
                                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Generate ticket-less dump: %s.", (tiklessDump ? "Yes" : "No"));
                                        }
                                    } else {
                                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation. Configuration parameters overrided.");
                                        breaks++;
                                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Remove console specific data: %s | Generate ticket-less dump: %s.", (removeConsoleData ? "Yes" : "No"), (tiklessDump ? "Yes" : "No"));
                                    }
                                    
                                    breaks++;
                                }
                            } else {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid PFS0 file index in the sequential dump reference file!");
                                proceed = false;
                            }
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: PFS0 file count mismatch in the sequential dump reference file! (%u != %u)", seqNspCtx.nspFileCount, nspFileCount);
                            proceed = false;
                        }
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: NCA count mismatch in the sequential dump reference file! (%u != %u)", seqNspCtx.ncaCount, titleNcaCount - 1);
                        proceed = false;
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
                    proceed = false;
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NSP offset in the sequential dump reference file!");
                proceed = false;
            }
        } else {
            if (progressCtx.totalSize > freeSpace)
            {
                // Check if we have at least (SPLIT_FILE_SEQUENTIAL_SIZE + (sizeof(sequentialNspCtx) + ((titleNcaCount - 1) * SHA256_HASH_SIZE))) of free space
                // The CNMT NCA is excluded from the hash list
                if (freeSpace >= (SPLIT_FILE_SEQUENTIAL_SIZE + (sizeof(sequentialNspCtx) + ((titleNcaCount - 1) * SHA256_HASH_SIZE))))
                {
                    // Ask the user if they want to use the sequential dump mode
                    int cur_breaks = breaks;
                    breaks++;
                    
                    if (yesNoPrompt("There's not enough space available to generate a whole dump in this session. Do you want to use sequential dumping?\nIn this mode, the selected content will be dumped in more than one session.\nYou'll have to transfer the generated part files to a PC before continuing the process in the next session."))
                    {
                        // Remove the prompt from the screen
                        breaks = cur_breaks;
                        uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
                        uiRefreshDisplay();
                        
                        // Modify config parameters
                        isFat32 = true;
                        calcCrc = false;
                        
                        part_size = SPLIT_FILE_SEQUENTIAL_SIZE;
                        
                        seqDumpMode = true;
                        seqDumpFileSize = (sizeof(sequentialNspCtx) + ((titleNcaCount - 1) * SHA256_HASH_SIZE));
                        
                        // Fill information in our sequential context
                        seqNspCtx.storageId = curStorageId;
                        seqNspCtx.removeConsoleData = removeConsoleData;
                        seqNspCtx.tiklessDump = tiklessDump;
                        seqNspCtx.nspFileCount = nspFileCount;
                        seqNspCtx.ncaCount = (titleNcaCount - 1); // Exclude the CNMT NCA from the hash list
                        
                        // Store the modified Program NCA header
                        // The NPDM signature from the NCA header is generated using cryptographically secure random numbers, so we must store the modified header during the first sequential dump session
                        if (ncaProgramMod.block_mod_cnt) memcpy(&(seqNspCtx.programNcaHeaderMod), xml_content_info[programNcaIndex].encrypted_header_mod, NCA_FULL_HEADER_LENGTH);
                        
                        // Allocate memory for the NCA hashes
                        seqDumpNcaHashes = calloc(1, (titleNcaCount - 1) * SHA256_HASH_SIZE);
                        if (seqDumpNcaHashes)
                        {
                            // Create sequential reference file and keep the handle to it opened
                            seqDumpFile = fopen(seqDumpFilename, "wb+");
                            if (seqDumpFile)
                            {
                                // Write the sequential dump struct
                                write_res = fwrite(&seqNspCtx, 1, sizeof(sequentialNspCtx), seqDumpFile);
                                if (write_res == sizeof(sequentialNspCtx))
                                {
                                    // Write the NCA hashes block
                                    write_res = fwrite(seqDumpNcaHashes, 1, seqDumpFileSize - sizeof(sequentialNspCtx), seqDumpFile);
                                    rewind(seqDumpFile);
                                    
                                    if (write_res == (seqDumpFileSize - sizeof(sequentialNspCtx)))
                                    {
                                        // Update free space
                                        freeSpace -= seqDumpFileSize;
                                    } else {
                                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", titleNcaCount * SHA256_HASH_SIZE, write_res);
                                        proceed = false;
                                        seqDumpFileRemove = true;
                                    }
                                } else {
                                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", sizeof(sequentialNspCtx), write_res);
                                    proceed = false;
                                    seqDumpFileRemove = true;
                                }
                            } else {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to create sequential dump reference file! (\"%s\")", seqDumpFilename);
                                proceed = false;
                            }
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA hashes from the sequential dump reference file!");
                            proceed = false;
                        }
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                        proceed = false;
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
                    proceed = false;
                }
            }
        }
    } else {
        if (progressCtx.totalSize > freeSpace)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
            proceed = false;
        }
    }
    
    if (!proceed) goto out;
    
    if (seqDumpMode)
    {
        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp.%02u", NSP_DUMP_PATH, dumpName, splitIndex);
    } else {
        // Temporary, we'll use this to check if the dump already exists (it should have the archive bit set if so)
        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
        
        // Check if the dump already exists
        if (!batch && checkIfFileExists(dumpPath))
        {
            // Ask the user if they want to proceed anyway
            int cur_breaks = breaks;
            breaks++;
            
            proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
            if (!proceed)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                removeFile = false;
                goto out;
            } else {
                // Remove the prompt from the screen
                breaks = cur_breaks;
                uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
            }
        }
        
        // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
        // Better safe than sorry
        unlink(dumpPath);
        fsdevDeleteDirectoryRecursively(dumpPath);
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            mkdir(dumpPath, 0744);
            
            sprintf(tmp_idx, "/%02u", splitIndex);
            strcat(dumpPath, tmp_idx);
        }
    }
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to open output file \"%s\"!", dumpPath);
        goto out;
    }
    
    if (!batch)
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
        uiRefreshDisplay();
        breaks += 2;
    }
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
        breaks += 2;
    }
    
    if (seqDumpMode)
    {
        // Skip the PFS0 header in the first part file
        // It will be saved to an additional ".nsp.hdr" file
        if (!seqNspCtx.partNumber) progressCtx.curOffset = seqDumpSessionOffset = full_nsp_header_size;
    } else {
        // Write placeholder zeroes
        write_res = fwrite(dumpBuf, 1, full_nsp_header_size, outFile);
        if (write_res != full_nsp_header_size)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes placeholder data to file offset 0x%016lX! (wrote %lu bytes)", full_nsp_header_size, (u64)0, write_res);
            goto out;
        }
        
        // Advance our current offset
        progressCtx.curOffset = full_nsp_header_size;
    }
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    dumping = true;
    
    u32 startFileIndex = (seqDumpMode ? seqNspCtx.fileIndex : 0);
    u64 startFileOffset;
    
    // Dump all NCAs excluding the CNMT NCA
    for(i = startFileIndex; i < (titleNcaCount - 1); i++, startFileIndex++)
    {
        n = DUMP_BUFFER_SIZE;
        
        startFileOffset = ((seqDumpMode && i == seqNspCtx.fileIndex) ? seqNspCtx.fileOffset : 0);
        
        memcpy(ncaId.c, xml_content_info[i].nca_id, SHA256_HASH_SIZE / 2);
        
        if (!seqDumpMode || (seqDumpMode && i != seqNspCtx.fileIndex)) sha256ContextCreate(&nca_hash_ctx);
        
        for(nca_offset = startFileOffset; nca_offset < xml_content_info[i].size; nca_offset += n, progressCtx.curOffset += n, seqDumpSessionOffset += n)
        {
            if (seqDumpMode && seqDumpFinish) break;
            
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Dumping NCA content \"%s\"...", xml_content_info[i].nca_id_str);
            
            if (n > (xml_content_info[i].size - nca_offset)) n = (xml_content_info[i].size - nca_offset);
            
            // Check if the next read chunk will exceed the size of the current part file
            if (seqDumpMode && (seqDumpSessionOffset + n) >= (((splitIndex - seqNspCtx.partNumber) + 1) * part_size))
            {
                u64 new_file_chunk_size = ((seqDumpSessionOffset + n) - (((splitIndex - seqNspCtx.partNumber) + 1) * part_size));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                u64 remainderDumpSize = (progressCtx.totalSize - (progressCtx.curOffset + old_file_chunk_size));
                u64 remainderFreeSize = (freeSpace - (seqDumpSessionOffset + old_file_chunk_size));
                
                // Check if we have enough space for the next part
                // If so, set the chunk size to old_file_chunk_size
                if ((remainderDumpSize <= part_size && remainderDumpSize > remainderFreeSize) || (remainderDumpSize > part_size && part_size > remainderFreeSize))
                {
                    n = old_file_chunk_size;
                    seqDumpFinish = true;
                }
            }
            
            if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, dumpBuf, n, &ncaId, nca_offset)))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to read %lu bytes chunk at offset 0x%016lX from NCA \"%s\"! (0x%08X)", n, nca_offset, xml_content_info[i].nca_id_str, result);
                proceed = false;
                break;
            }
            
            // Replace NCA header with our modified one
            if (nca_offset < NCA_FULL_HEADER_LENGTH)
            {
                u64 write_size = (NCA_FULL_HEADER_LENGTH - nca_offset);
                if (write_size > n) write_size = n;
                
                memcpy(dumpBuf, xml_content_info[i].encrypted_header_mod + nca_offset, write_size);
            }
            
            // Replace modified Program NCA data blocks
            if (ncaProgramMod.block_mod_cnt > 0 && xml_content_info[i].type == NcmContentType_Program)
            {
                u64 internal_block_offset;
                u64 internal_block_chunk_size;
                
                u64 buffer_offset;
                u64 buffer_chunk_size;
                
                if ((nca_offset + n) > ncaProgramMod.hash_table_offset && (ncaProgramMod.hash_table_offset + ncaProgramMod.hash_table_size) > nca_offset)
                {
                    internal_block_offset = (nca_offset > ncaProgramMod.hash_table_offset ? (nca_offset - ncaProgramMod.hash_table_offset) : 0);
                    internal_block_chunk_size = (ncaProgramMod.hash_table_size - internal_block_offset);
                    
                    buffer_offset = (nca_offset > ncaProgramMod.hash_table_offset ? 0 : (ncaProgramMod.hash_table_offset - nca_offset));
                    buffer_chunk_size = ((n - buffer_offset) > internal_block_chunk_size ? internal_block_chunk_size : (n - buffer_offset));
                    
                    memcpy(dumpBuf + buffer_offset, ncaProgramMod.hash_table + internal_block_offset, buffer_chunk_size);
                }
                
                if ((nca_offset + n) > ncaProgramMod.block_offset[0] && (ncaProgramMod.block_offset[0] + ncaProgramMod.block_size[0]) > nca_offset)
                {
                    internal_block_offset = (nca_offset > ncaProgramMod.block_offset[0] ? (nca_offset - ncaProgramMod.block_offset[0]) : 0);
                    internal_block_chunk_size = (ncaProgramMod.block_size[0] - internal_block_offset);
                    
                    buffer_offset = (nca_offset > ncaProgramMod.block_offset[0] ? 0 : (ncaProgramMod.block_offset[0] - nca_offset));
                    buffer_chunk_size = ((n - buffer_offset) > internal_block_chunk_size ? internal_block_chunk_size : (n - buffer_offset));
                    
                    memcpy(dumpBuf + buffer_offset, ncaProgramMod.block_data[0] + internal_block_offset, buffer_chunk_size);
                }
                
                if (ncaProgramMod.block_mod_cnt == 2 && (nca_offset + n) > ncaProgramMod.block_offset[1] && (ncaProgramMod.block_offset[1] + ncaProgramMod.block_size[1]) > nca_offset)
                {
                    internal_block_offset = (nca_offset > ncaProgramMod.block_offset[1] ? (nca_offset - ncaProgramMod.block_offset[1]) : 0);
                    internal_block_chunk_size = (ncaProgramMod.block_size[1] - internal_block_offset);
                    
                    buffer_offset = (nca_offset > ncaProgramMod.block_offset[1] ? 0 : (ncaProgramMod.block_offset[1] - nca_offset));
                    buffer_chunk_size = ((n - buffer_offset) > internal_block_chunk_size ? internal_block_chunk_size : (n - buffer_offset));
                    
                    memcpy(dumpBuf + buffer_offset, ncaProgramMod.block_data[1] + internal_block_offset, buffer_chunk_size);
                }
            }
            
            // Update SHA-256 calculation
            sha256ContextUpdate(&nca_hash_ctx, dumpBuf, n);
            
            if ((seqDumpMode || (!seqDumpMode && progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)) && (progressCtx.curOffset + n) >= ((splitIndex + 1) * part_size))
            {
                u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * part_size));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (((seqDumpMode && !seqDumpFinish) || !seqDumpMode) && (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize))
                {
                    splitIndex++;
                    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp%c%02u", NSP_DUMP_PATH, dumpName, (seqDumpMode ? '.' : '/'), splitIndex);
                    
                    outFile = fopen(dumpPath, "wb");
                    if (!outFile)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                    
                    if (!seqDumpMode && (progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.");
                        fat32_error = true;
                    }
                    
                    proceed = false;
                    break;
                }
            }
            
            if (seqDumpMode) progressCtx.seqDumpCurOffset = seqDumpSessionOffset;
            printProgressBar(&progressCtx, true, n);
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                if (cancelProcessCheck(&progressCtx))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    proceed = false;
                    if (seqDumpMode) seqDumpFileRemove = true;
                    break;
                }
            }
        }
        
        if (!proceed)
        {
            setProgressBarError(&progressCtx);
            break;
        } else {
            if (seqDumpMode && seqDumpFinish)
            {
                success = true;
                break;
            }
        }
        
        // Support empty files
        if (!xml_content_info[i].size)
        {
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, strrchr(dumpPath, '/' ) + 1);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Dumping NCA content \"%s\"...", xml_content_info[i].nca_id_str);
            
            printProgressBar(&progressCtx, false, 0);
        }
        
        // Update content info
        sha256ContextGetHash(&nca_hash_ctx, xml_content_info[i].hash);
        convertDataToHexString(xml_content_info[i].hash, SHA256_HASH_SIZE, xml_content_info[i].hash_str, (SHA256_HASH_SIZE * 2) + 1);
        memcpy(xml_content_info[i].nca_id, xml_content_info[i].hash, SHA256_HASH_SIZE / 2);
        convertDataToHexString(xml_content_info[i].nca_id, SHA256_HASH_SIZE / 2, xml_content_info[i].nca_id_str, SHA256_HASH_SIZE + 1);
        
        // If we're doing a sequential dump, copy the hash from the NCA we just finished dumping
        if (seqDumpMode) memcpy(seqDumpNcaHashes + (i * SHA256_HASH_SIZE), xml_content_info[i].hash, SHA256_HASH_SIZE);
    }
    
    if (!proceed || success) goto out;
    
    uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Writing PFS0 header...");
    
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
                // Replace the NCA ID from its filename, since it could have changed
                u32 icon_idx = (!programInfoXml ? (i - (titleNcaCount + 1)) : (i - (titleNcaCount + 2)));
                sprintf(ncaFileName, "%s%s", xml_content_info[nacpNcaIndex].nca_id_str, strchr(nacpIcons[icon_idx].filename, '.'));
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
    memcpy(dumpBuf, &nspPfs0Header, sizeof(pfs0_header));
    memcpy(dumpBuf + sizeof(pfs0_header), nspPfs0EntryTable, (u64)nspFileCount * sizeof(pfs0_entry_table));
    memcpy(dumpBuf + sizeof(pfs0_header) + ((u64)nspFileCount * sizeof(pfs0_entry_table)), nspPfs0StrTable, nspPfs0Header.str_table_size);
    
    if (seqDumpMode)
    {
        // Check if the PFS0 header file already exists
        if (!checkIfFileExists(pfs0HeaderFilename))
        {
            // Check if we have enough space for the header file
            u64 curFreeSpace = (freeSpace - seqDumpSessionOffset);
            if (!seqNspCtx.partNumber) curFreeSpace += full_nsp_header_size; // The PFS0 header size is skipped during the first sequential dump session
            
            if (curFreeSpace >= full_nsp_header_size)
            {
                pfs0HeaderFile = fopen(pfs0HeaderFilename, "wb");
                if (pfs0HeaderFile)
                {
                    write_res = fwrite(dumpBuf, 1, full_nsp_header_size, pfs0HeaderFile);
                    fclose(pfs0HeaderFile);
                    
                    if (write_res == full_nsp_header_size)
                    {
                        // Update free space
                        freeSpace -= full_nsp_header_size;
                    } else {
                        setProgressBarError(&progressCtx);
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: failed to write %lu bytes PFS0 header file! (wrote %lu bytes)", full_nsp_header_size, write_res);
                        unlink(pfs0HeaderFilename);
                        goto out;
                    }
                } else {
                    setProgressBarError(&progressCtx);
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: failed to create PFS0 header file!");
                    goto out;
                }
            } else {
                // Finish current sequential dump session
                seqDumpFinish = true;
                success = true;
                goto out;
            }
        }
    } else {
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            if (outFile)
            {
                fclose(outFile);
                outFile = NULL;
            }
            
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, 0);
            
            outFile = fopen(dumpPath, "rb+");
            if (!outFile)
            {
                setProgressBarError(&progressCtx);
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to re-open output file for part #0!");
                goto out;
            }
        } else {
            rewind(outFile);
        }
        
        write_res = fwrite(dumpBuf, 1, full_nsp_header_size, outFile);
        if (write_res != full_nsp_header_size)
        {
            setProgressBarError(&progressCtx);
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes PFS0 header to file offset 0x%016lX! (wrote %lu bytes)", full_nsp_header_size, (u64)0, write_res);
            goto out;
        }
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            if (outFile)
            {
                fclose(outFile);
                outFile = NULL;
            }
            
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, splitIndex);
            
            outFile = fopen(dumpPath, "rb+");
            if (!outFile)
            {
                setProgressBarError(&progressCtx);
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to re-open output file for part #%u!", splitIndex);
                goto out;
            }
        }
        
        fseek(outFile, 0, SEEK_END);
    }
    
    startFileIndex = ((seqDumpMode && seqNspCtx.fileIndex > (titleNcaCount - 1)) ? seqNspCtx.fileIndex : (titleNcaCount - 1));
    
    // Now let's write the rest of the data, including our modified CNMT NCA
    for(i = startFileIndex; i < nspFileCount; i++, startFileIndex++)
    {
        n = DUMP_BUFFER_SIZE;
        
        startFileOffset = ((seqDumpMode && i == seqNspCtx.fileIndex) ? seqNspCtx.fileOffset : 0);
        
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
        
        for(nca_offset = startFileOffset; nca_offset < cur_file_size; nca_offset += n, progressCtx.curOffset += n, seqDumpSessionOffset += n)
        {
            if (seqDumpMode && seqDumpFinish) break;
            
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Writing \"%s\"...", ncaFileName);
            
            uiRefreshDisplay();
            
            if (n > (cur_file_size - nca_offset)) n = (cur_file_size - nca_offset);
            
            // Check if the next read chunk will exceed the size of the current part file
            if (seqDumpMode && (seqDumpSessionOffset + n) >= (((splitIndex - seqNspCtx.partNumber) + 1) * part_size))
            {
                u64 new_file_chunk_size = ((seqDumpSessionOffset + n) - (((splitIndex - seqNspCtx.partNumber) + 1) * part_size));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                u64 remainderDumpSize = (progressCtx.totalSize - (progressCtx.curOffset + old_file_chunk_size));
                u64 remainderFreeSize = (freeSpace - (seqDumpSessionOffset + old_file_chunk_size));
                
                // Check if we have enough space for the next part
                // If so, set the chunk size to old_file_chunk_size
                if ((remainderDumpSize <= part_size && remainderDumpSize > remainderFreeSize) || (remainderDumpSize > part_size && part_size > remainderFreeSize))
                {
                    n = old_file_chunk_size;
                    seqDumpFinish = true;
                }
            }
            
            // Retrieve data from its respective source
            if (i == (titleNcaCount - 1))
            {
                // CNMT NCA
                memcpy(dumpBuf, cnmtNcaBuf + nca_offset, n);
            } else
            if (i == titleNcaCount)
            {
                // CNMT XML
                memcpy(dumpBuf, cnmtXml + nca_offset, n);
            } else {
                if (programInfoXml && i == (titleNcaCount + 1))
                {
                    // programinfo.xml entry
                    memcpy(dumpBuf, programInfoXml + nca_offset, n);
                } else
                if (nacpIcons && nacpIconCnt && ((!programInfoXml && i <= (titleNcaCount + nacpIconCnt)) || (programInfoXml && i <= (titleNcaCount + 1 + nacpIconCnt))))
                {
                    // NACP icon entry
                    u32 icon_idx = (!programInfoXml ? (i - (titleNcaCount + 1)) : (i - (titleNcaCount + 2)));
                    memcpy(dumpBuf, nacpIcons[icon_idx].icon_data + nca_offset, n);
                } else
                if (nacpXml && ((!programInfoXml && i == (titleNcaCount + nacpIconCnt + 1)) || (programInfoXml && i == (titleNcaCount + 1 + nacpIconCnt + 1))))
                {
                    // NACP XML entry
                    memcpy(dumpBuf, nacpXml + nca_offset, n);
                } else
                if (legalInfoXml && ((!includeTikAndCert && i == (nspFileCount - 1)) || (includeTikAndCert && i == (nspFileCount - 3))))
                {
                    // legalinfo.xml entry
                    memcpy(dumpBuf, legalInfoXml + nca_offset, n);
                } else {
                    // tik/cert entry
                    if (i == (nspFileCount - 2))
                    {
                        memcpy(dumpBuf, (u8*)(&(rights_info.tik_data)) + nca_offset, n);
                    } else {
                        memcpy(dumpBuf, rights_info.cert_data + nca_offset, n);
                    }
                }
            }
            
            if ((seqDumpMode || (!seqDumpMode && progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)) && (progressCtx.curOffset + n) >= ((splitIndex + 1) * part_size))
            {
                u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * part_size));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (((seqDumpMode && !seqDumpFinish) || !seqDumpMode) && (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize))
                {
                    splitIndex++;
                    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp%c%02u", NSP_DUMP_PATH, dumpName, (seqDumpMode ? '.' : '/'), splitIndex);
                    
                    outFile = fopen(dumpPath, "wb");
                    if (!outFile)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                    
                    if (!seqDumpMode && (progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.");
                        fat32_error = true;
                    }
                    
                    proceed = false;
                    break;
                }
            }
            
            if (seqDumpMode) progressCtx.seqDumpCurOffset = seqDumpSessionOffset;
            printProgressBar(&progressCtx, true, n);
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                if (cancelProcessCheck(&progressCtx))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    proceed = false;
                    if (seqDumpMode) seqDumpFileRemove = true;
                    break;
                }
            }
        }
        
        if (!proceed)
        {
            setProgressBarError(&progressCtx);
            break;
        } else {
            if (seqDumpMode && seqDumpFinish) break;
        }
        
        // Support empty files
        if (!cur_file_size)
        {
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, strrchr(dumpPath, '/' ) + 1);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Writing \"%s\"...", ncaFileName);
            
            printProgressBar(&progressCtx, false, 0);
        }
    }
    
    if (!proceed) goto out;
    
    dumping = false;
    
    breaks = (progressCtx.line_offset + 2);
    
    if (progressCtx.curOffset >= progressCtx.totalSize || (seqDumpMode && seqDumpFinish)) success = true;
    
    if (!success)
    {
        setProgressBarError(&progressCtx);
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Unexpected underdump error! Wrote %lu bytes, expected %lu bytes.", progressCtx.curOffset, progressCtx.totalSize);
        if (seqDumpMode) seqDumpFileRemove = true;
        goto out;
    }
    
    // Calculate CRC32 checksum
    if (!batch && calcCrc)
    {
        // Finalize dump
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        progressCtx.progress = 100;
        progressCtx.remainingTime = 0;
        
        printProgressBar(&progressCtx, false, 0);
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
        
        uiRefreshDisplay();
        
        breaks += 2;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "CRC32 checksum calculation will begin in %u seconds...", DUMP_NSP_CRC_WAIT);
        uiRefreshDisplay();
        
        delay(DUMP_NSP_CRC_WAIT);
        
        breaks = initial_breaks;
        uiFill(0, (breaks * LINE_HEIGHT) + 8, FB_WIDTH, FB_HEIGHT - (breaks * LINE_HEIGHT), BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Calculating CRC32 checksum. Hold %s to cancel.", NINTENDO_FONT_B);
        breaks += 2;
        
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }
        
        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
        
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
                uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "File: \"%s\".", strrchr(dumpPath, '/' ) + 1);
                
                if (n > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
                
                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && (progressCtx.curOffset + n) >= ((splitIndex + 1) * part_size))
                {
                    u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * part_size));
                    u64 old_file_chunk_size = (n - new_file_chunk_size);
                    
                    if (old_file_chunk_size > 0)
                    {
                        read_res = fread(dumpBuf, 1, old_file_chunk_size, outFile);
                        if (read_res != old_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to read %lu bytes chunk from offset 0x%016lX from part #%02u! (read %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, read_res);
                            proceed = false;
                            break;
                        }
                    }
                    
                    fclose(outFile);
                    outFile = NULL;
                    
                    if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                    {
                        splitIndex++;
                        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, splitIndex);
                        
                        outFile = fopen(dumpPath, "rb");
                        if (!outFile)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to re-open output file for part #%u!", splitIndex);
                            proceed = false;
                            break;
                        }
                        
                        if (new_file_chunk_size > 0)
                        {
                            read_res = fread(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                            if (read_res != new_file_chunk_size)
                            {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to read %lu bytes chunk from offset 0x%016lX from part #%02u! (read %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, read_res);
                                proceed = false;
                                break;
                            }
                        }
                    }
                } else {
                    read_res = fread(dumpBuf, 1, n, outFile);
                    if (read_res != n)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to read %lu bytes chunk from offset 0x%016lX! (read %lu bytes)", n, progressCtx.curOffset, read_res);
                        proceed = false;
                        break;
                    }
                }
                
                // Update CRC32
                crc32(dumpBuf, n, &crc);
                
                printProgressBar(&progressCtx, true, n);
                
                if ((progressCtx.curOffset + n) < progressCtx.totalSize)
                {
                    if (cancelProcessCheck(&progressCtx))
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
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
                
                formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "NSP dump CRC32 checksum: %08X", crc);
                breaks += 2;
            } else {
                setProgressBarError(&progressCtx);
            }
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to re-open output file in read mode!");
            breaks += 2;
        }
    }
    
    // Set archive bit (only for FAT32)
    if (!seqDumpMode && progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
        if (R_FAILED(result = fsdevSetArchiveBit(dumpPath))) 
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Warning: failed to set archive bit on output directory! (0x%08X)", result);
            breaks += 2;
        }
    }
    
out:
    if (outFile) fclose(outFile);
    
    if (success)
    {
        if (seqDumpMode)
        {
            if (seqDumpFinish)
            {
                // Update line count
                breaks = (progressCtx.line_offset + 2);
                
                // Update the sequence reference file in the SD card
                seqNspCtx.partNumber = (splitIndex + 1);
                seqNspCtx.fileIndex = startFileIndex;
                seqNspCtx.fileOffset = nca_offset;
                
                // Copy the SHA-256 context data, but only if we're not dealing with the CNMT NCA
                // NCA ID/hash for the CNMT NCA is handled in patchCnmtNca()
                if (seqNspCtx.fileIndex < titleNcaCount && seqNspCtx.fileIndex != cnmtNcaIndex)
                {
                    memcpy(&(seqNspCtx.hashCtx), &nca_hash_ctx, sizeof(Sha256Context));
                } else {
                    memset(&(seqNspCtx.hashCtx), 0, sizeof(Sha256Context));
                }
                
                // Write the struct data
                write_res = fwrite(&seqNspCtx, 1, sizeof(sequentialNspCtx), seqDumpFile);
                if (write_res == sizeof(sequentialNspCtx))
                {
                    // Write the NCA hashes
                    write_res = fwrite(seqDumpNcaHashes, 1, seqDumpFileSize - sizeof(sequentialNspCtx), seqDumpFile);
                    if (write_res != (seqDumpFileSize - sizeof(sequentialNspCtx)))
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", seqDumpFileSize - sizeof(sequentialNspCtx), write_res);
                        success = false;
                        seqDumpFileRemove = true;
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", sizeof(sequentialNspCtx), write_res);
                    success = false;
                    seqDumpFileRemove = true;
                }
            } else {
                // Mark the file for deletion
                seqDumpFileRemove = true;
            }
        }
        
        if (success && !batch && !calcCrc)
        {
            timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
            progressCtx.now -= progressCtx.start;
            
            if (!seqDumpMode || (seqDumpMode && !seqDumpFinish))
            {
                progressCtx.progress = 100;
                progressCtx.remainingTime = 0;
            }
            
            printProgressBar(&progressCtx, false, 0);
            
            formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
            
            if (seqDumpMode)
            {
                breaks += 2;
                
                if (seqDumpFinish)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Please remember to exit the application and transfer the generated part file(s) to a PC before continuing in the next session!");
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Do NOT move the \"%s\" file!", strrchr(seqDumpFilename, '/' ) + 1);
                }
                
                if (checkIfFileExists(pfs0HeaderFilename))
                {
                    if (seqDumpFinish) breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "The \"%s\" file contains the PFS0 header.\nUse it as the first file when concatenating all parts!", strrchr(pfs0HeaderFilename, '/' ) + 1);
                }
            }
            
            breaks += 2;
            
            uiRefreshDisplay();
        }
    } else {
        if (dumping)
        {
            breaks += 6;
            if (fat32_error) breaks += 2;
        }
        
        breaks += 2;
        
        if (removeFile)
        {
            if (seqDumpMode)
            {
                for(u8 i = 0; i <= splitIndex; i++)
                {
                    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp.%02u", NSP_DUMP_PATH, dumpName, i);
                    unlink(dumpPath);
                }
            } else {
                snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                
                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
                {
                    fsdevDeleteDirectoryRecursively(dumpPath);
                } else {
                    unlink(dumpPath);
                }
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
    
    if (titleContentInfos) free(titleContentInfos);
    
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
    
    if (seqDumpNcaHashes) free(seqDumpNcaHashes);
    
    if (seqDumpFile) fclose(seqDumpFile);
    
    if (seqDumpFileRemove) unlink(seqDumpFilename);
    
    if (dumpName) free(dumpName);
    
    return success;
}

bool dumpNintendoSubmissionPackageBatch(batchOptions *batchDumpCfg)
{
    if (!batchDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid batch dump configuration struct!");
        breaks += 2;
        return false;
    }
    
    bool dumpAppTitles = batchDumpCfg->dumpAppTitles;
    bool dumpPatchTitles = batchDumpCfg->dumpPatchTitles;
    bool dumpAddOnTitles = batchDumpCfg->dumpAddOnTitles;
    bool isFat32 = batchDumpCfg->isFat32;
    bool removeConsoleData = batchDumpCfg->removeConsoleData;
    bool tiklessDump = batchDumpCfg->tiklessDump;
    bool skipDumpedTitles = batchDumpCfg->skipDumpedTitles;
    batchModeSourceStorage batchModeSrc = batchDumpCfg->batchModeSrc;
    bool rememberDumpedTitles = batchDumpCfg->rememberDumpedTitles;
    
    if ((!dumpAppTitles && !dumpPatchTitles && !dumpAddOnTitles) || (batchModeSrc == BATCH_SOURCE_ALL && ((dumpAppTitles && !titleAppCount) || (dumpPatchTitles && !titlePatchCount) || (dumpAddOnTitles && !titleAddOnCount))) || (batchModeSrc == BATCH_SOURCE_SDCARD && ((dumpAppTitles && !sdCardTitleAppCount) || (dumpPatchTitles && !sdCardTitlePatchCount) || (dumpAddOnTitles && !sdCardTitleAddOnCount))) || (batchModeSrc == BATCH_SOURCE_EMMC && ((dumpAppTitles && !nandUserTitleAppCount) || (dumpPatchTitles && !nandUserTitlePatchCount) || (dumpAddOnTitles && !nandUserTitleAddOnCount))) || batchModeSrc >= BATCH_SOURCE_CNT)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to perform batch NSP dump!");
        breaks += 2;
        return false;
    }
    
    u32 i, j;
    
    u32 totalTitleCount = 0, totalAppCount = 0, totalPatchCount = 0, totalAddOnCount = 0;
    
    u32 titleCount, titleIndex;
    
    char *dumpName = NULL;
    char dumpPath[4150] = {'\0'};
    char summary_str[256] = {'\0'};
    
    int initial_breaks = breaks, cur_breaks;
    
    const u32 maxSummaryFileCount = 8;
    u32 summaryPage = 0, selectedSummaryEntry = 0;
    u32 xpos = 0, ypos = 0;
    u32 keysDown = 0, keysHeld = 0;
    
    u32 maxEntryCount = 0, batchEntryIndex = 0, disabledEntryCount = 0;
    batchEntry *batchEntries = NULL;
    
    bool proceed = true, success = false;
    
    // Generate NSP configuration struct
    nspOptions nspDumpCfg;
    
    nspDumpCfg.isFat32 = isFat32;
    nspDumpCfg.calcCrc = false;
    nspDumpCfg.removeConsoleData = removeConsoleData;
    nspDumpCfg.tiklessDump = tiklessDump;
    
    // Allocate memory for the batch entries
    if (dumpAppTitles) maxEntryCount += (batchModeSrc == BATCH_SOURCE_ALL ? titleAppCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAppCount : nandUserTitleAppCount));
    if (dumpPatchTitles) maxEntryCount += (batchModeSrc == BATCH_SOURCE_ALL ? titlePatchCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitlePatchCount : nandUserTitlePatchCount));
    if (dumpAppTitles) maxEntryCount += (batchModeSrc == BATCH_SOURCE_ALL ? titleAddOnCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAddOnCount : nandUserTitleAddOnCount));
    
    batchEntries = calloc(maxEntryCount, sizeof(batchEntry));
    if (!batchEntries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for batch entries!");
        breaks += 2;
        return false;
    }
    
    if (dumpAppTitles)
    {
        titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titleAppCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAppCount : nandUserTitleAppCount));
        
        for(i = 0; i < titleCount; i++)
        {
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? i : (i + sdCardTitleAppCount));
            
            dumpName = generateNSPDumpName(DUMP_APP_NSP, titleIndex);
            if (!dumpName)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
                breaks += 2;
                goto out;
            }
            
            // Check if an override file already exists for this dump
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", BATCH_OVERRIDES_PATH, dumpName);
            
            if (checkIfFileExists(dumpPath))
            {
                free(dumpName);
                dumpName = NULL;
                continue;
            }
            
            // Check if this title has already been dumped
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            // Save title properties
            batchEntries[batchEntryIndex].enabled = true;
            batchEntries[batchEntryIndex].titleType = DUMP_APP_NSP;
            batchEntries[batchEntryIndex].titleIndex = titleIndex;
            snprintf(batchEntries[batchEntryIndex].nspFilename, MAX_ELEMENTS(batchEntries[batchEntryIndex].nspFilename), strrchr(dumpPath, '/') + 1);
            
            // Fix entry name length
            snprintf(batchEntries[batchEntryIndex].truncatedNspFilename, MAX_ELEMENTS(batchEntries[batchEntryIndex].truncatedNspFilename), batchEntries[batchEntryIndex].nspFilename);
            
            u32 strWidth = uiGetStrWidth(batchEntries[batchEntryIndex].truncatedNspFilename);
            
            if ((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
            {
                while((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    batchEntries[batchEntryIndex].truncatedNspFilename[strlen(batchEntries[batchEntryIndex].truncatedNspFilename) - 1] = '\0';
                    strWidth = uiGetStrWidth(batchEntries[batchEntryIndex].truncatedNspFilename);
                }
                
                strcat(batchEntries[batchEntryIndex].truncatedNspFilename, "...");
            }
            
            // Increase batch entry index
            batchEntryIndex++;
            
            // Increase total base application count
            totalAppCount++;
        }
        
        // Increase total title count
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
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
                breaks += 2;
                goto out;
            }
            
            // Check if an override file already exists for this dump
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", BATCH_OVERRIDES_PATH, dumpName);
            
            if (checkIfFileExists(dumpPath))
            {
                free(dumpName);
                dumpName = NULL;
                continue;
            }
            
            // Check if this title has already been dumped
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            // Save title properties
            batchEntries[batchEntryIndex].enabled = true;
            batchEntries[batchEntryIndex].titleType = DUMP_PATCH_NSP;
            batchEntries[batchEntryIndex].titleIndex = titleIndex;
            snprintf(batchEntries[batchEntryIndex].nspFilename, MAX_ELEMENTS(batchEntries[batchEntryIndex].nspFilename), strrchr(dumpPath, '/') + 1);
            
            // Fix entry name length
            snprintf(batchEntries[batchEntryIndex].truncatedNspFilename, MAX_ELEMENTS(batchEntries[batchEntryIndex].truncatedNspFilename), batchEntries[batchEntryIndex].nspFilename);
            
            u32 strWidth = uiGetStrWidth(batchEntries[batchEntryIndex].truncatedNspFilename);
            
            if ((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
            {
                while((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    batchEntries[batchEntryIndex].truncatedNspFilename[strlen(batchEntries[batchEntryIndex].truncatedNspFilename) - 1] = '\0';
                    strWidth = uiGetStrWidth(batchEntries[batchEntryIndex].truncatedNspFilename);
                }
                
                strcat(batchEntries[batchEntryIndex].truncatedNspFilename, "...");
            }
            
            // Increase batch entry index
            batchEntryIndex++;
            
            // Increase total patch count
            totalPatchCount++;
        }
        
        // Increase total title count
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
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
                breaks += 2;
                goto out;
            }
            
            // Check if an override file already exists for this dump
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", BATCH_OVERRIDES_PATH, dumpName);
            
            if (checkIfFileExists(dumpPath))
            {
                free(dumpName);
                dumpName = NULL;
                continue;
            }
            
            // Check if this title has already been dumped
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            if (skipDumpedTitles && checkIfFileExists(dumpPath)) continue;
            
            // Save title properties
            batchEntries[batchEntryIndex].enabled = true;
            batchEntries[batchEntryIndex].titleType = DUMP_ADDON_NSP;
            batchEntries[batchEntryIndex].titleIndex = titleIndex;
            snprintf(batchEntries[batchEntryIndex].nspFilename, MAX_ELEMENTS(batchEntries[batchEntryIndex].nspFilename), strrchr(dumpPath, '/') + 1);
            
            // Fix entry name length
            snprintf(batchEntries[batchEntryIndex].truncatedNspFilename, MAX_ELEMENTS(batchEntries[batchEntryIndex].truncatedNspFilename), batchEntries[batchEntryIndex].nspFilename);
            
            u32 strWidth = uiGetStrWidth(batchEntries[batchEntryIndex].truncatedNspFilename);
            
            if ((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
            {
                while((8 + strWidth) >= (FB_WIDTH - (font_height * 5)))
                {
                    batchEntries[batchEntryIndex].truncatedNspFilename[strlen(batchEntries[batchEntryIndex].truncatedNspFilename) - 1] = '\0';
                    strWidth = uiGetStrWidth(batchEntries[batchEntryIndex].truncatedNspFilename);
                }
                
                strcat(batchEntries[batchEntryIndex].truncatedNspFilename, "...");
            }
            
            // Increase batch entry index
            batchEntryIndex++;
            
            // Increase total addon count
            totalAddOnCount++;
        }
        
        // Increase total title count
        totalTitleCount += totalAddOnCount;
    }
    
    if (!totalTitleCount)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "You have already dumped all titles matching the selected settings!");
        breaks += 2;
        return false;
    }
    
    // Display summary controls
    if (totalTitleCount > maxSummaryFileCount)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_ZL " / " NINTENDO_FONT_ZR " ] Change page | [ " NINTENDO_FONT_A " ] Proceed | [ " NINTENDO_FONT_B " ] Cancel | [ " NINTENDO_FONT_Y " ] Toggle selected entry | [ "  NINTENDO_FONT_L " ] Disable all entries | [ " NINTENDO_FONT_R " ] Enable all entries");
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Proceed | [ " NINTENDO_FONT_B " ] Cancel | [ " NINTENDO_FONT_Y " ] Toggle selected entry | [ "  NINTENDO_FONT_L " ] Disable all entries | [ " NINTENDO_FONT_R " ] Enable all entries");
    }
    
    breaks += 2;
    
    // Display summary
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Summary:");
    breaks += 2;
    
    if (totalAppCount)
    {
        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "BASE: %u", totalAppCount);
        strcat(summary_str, dumpPath);
    }
    
    if (totalPatchCount)
    {
        if (totalAppCount) strcat(summary_str, " | ");
        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "UPD: %u", totalPatchCount);
        strcat(summary_str, dumpPath);
    }
    
    if (totalAddOnCount)
    {
        if (totalAppCount || totalPatchCount) strcat(summary_str, " | ");
        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "DLC: %u", totalAddOnCount);
        strcat(summary_str, dumpPath);
    }
    
    strcat(summary_str, " | ");
    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "Total: %u", totalTitleCount);
    strcat(summary_str, dumpPath);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, summary_str);
    breaks++;
    
    while(true)
    {
        cur_breaks = breaks;
        
        uiFill(0, 8 + (cur_breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (cur_breaks * LINE_HEIGHT)) - ((3 * LINE_HEIGHT) + 8), BG_COLOR_RGB);
        
        // Calculate the number of selected titles
        j = 0;
        for(i = 0; i < totalTitleCount; i++)
        {
            if (batchEntries[i].enabled) j++;
        }
        
        if (totalTitleCount > maxSummaryFileCount)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(cur_breaks), FONT_COLOR_RGB, "Current page: %u | Selected titles: %u", summaryPage + 1, j);
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(cur_breaks), FONT_COLOR_RGB, "Selected titles: %u", j);
        }
        
        cur_breaks += 2;
        
        j = 0;
        highlight = false;
        
        for(i = (summaryPage * maxSummaryFileCount); i < ((summaryPage + 1) * maxSummaryFileCount); i++, j++)
        {
            if (i >= totalTitleCount) break;
            
            xpos = STRING_X_POS;
            ypos = ((cur_breaks * LINE_HEIGHT) + (j * (font_height + 12)) + 6);
            
            if (i == selectedSummaryEntry)
            {
                highlight = true;
                uiFill(0, (ypos + 8) - 6, FB_WIDTH, font_height + 12, HIGHLIGHT_BG_COLOR_RGB);
            }
            
            uiDrawIcon((highlight ? (batchEntries[i].enabled ? enabledHighlightIconBuf : disabledHighlightIconBuf) : (batchEntries[i].enabled ? enabledNormalIconBuf : disabledNormalIconBuf)), BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, xpos, ypos + 8);
            xpos += BROWSER_ICON_DIMENSION;
            
            if (highlight)
            {
                uiDrawString(xpos, ypos, HIGHLIGHT_FONT_COLOR_RGB, batchEntries[i].truncatedNspFilename);
            } else {
                uiDrawString(xpos, ypos, FONT_COLOR_RGB, batchEntries[i].truncatedNspFilename);
            }
            
            if (i == selectedSummaryEntry) highlight = false;
        }
        
        while(true)
        {
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            hidScanInput();
            
            keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
            keysHeld = hidKeysHeld(CONTROLLER_P1_AUTO);
            
            if ((keysDown && !(keysDown & KEY_TOUCH)) || (keysHeld && !(keysHeld & KEY_TOUCH))) break;
        }
        
        // Start batch dump process
        if (keysDown & KEY_A)
        {
            // Check if we have at least a single enabled entry
            for(i = 0; i < totalTitleCount; i++)
            {
                if (batchEntries[i].enabled) break;
            }
            
            if (i < totalTitleCount)
            {
                proceed = true;
                break;
            } else {
                uiStatusMsg("Please enable at least one entry from the list.");
            }
        }
        
        // Cancel batch dump process
        if (keysDown & KEY_B)
        {
            proceed = false;
            break;
        }
        
        // Toggle selected entry
        if (keysDown & KEY_Y) batchEntries[selectedSummaryEntry].enabled ^= 0x01;
        
        // Disable all entries
        if (keysDown & KEY_L)
        {
            for(i = 0; i < totalTitleCount; i++) batchEntries[i].enabled = false;
        }
        
        // Enable all entries
        if (keysDown & KEY_R)
        {
            for(i = 0; i < totalTitleCount; i++) batchEntries[i].enabled = true;
        }
        
        // Change page (left)
        if ((keysDown & KEY_ZL) && totalTitleCount > maxSummaryFileCount)
        {
            if (summaryPage > 0)
            {
                summaryPage--;
                selectedSummaryEntry = (summaryPage * maxSummaryFileCount);
            }
        }
        
        // Change page (right)
        if ((keysDown & KEY_ZR) && totalTitleCount > maxSummaryFileCount)
        {
            if (((summaryPage + 1) * maxSummaryFileCount) < totalTitleCount)
            {
                summaryPage++;
                selectedSummaryEntry = (summaryPage * maxSummaryFileCount);
            }
        }
        
        // Go up
        if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
        {
            if (selectedSummaryEntry > (summaryPage * maxSummaryFileCount))
            {
                selectedSummaryEntry--;
            } else {
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP))
                {
                    if (((summaryPage + 1) * maxSummaryFileCount) < totalTitleCount)
                    {
                        selectedSummaryEntry = (((summaryPage + 1) * maxSummaryFileCount) - 1);
                    } else {
                        selectedSummaryEntry = (totalTitleCount - 1);
                    }
                }
            }
        }
        
        // Go down
        if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
        {
            if (((((summaryPage + 1) * maxSummaryFileCount) < totalTitleCount) && selectedSummaryEntry < (((summaryPage + 1) * maxSummaryFileCount) - 1)) || ((((summaryPage + 1) * maxSummaryFileCount) >= totalTitleCount) && selectedSummaryEntry < (totalTitleCount - 1)))
            {
                selectedSummaryEntry++;
            } else {
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN))
                {
                    selectedSummaryEntry = (summaryPage * maxSummaryFileCount);
                }
            }
        }
    }
    
    breaks = initial_breaks;
    uiFill(0, 8 + (breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (breaks * LINE_HEIGHT)), BG_COLOR_RGB);
    
    if (!proceed)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled");
        breaks += 2;
        goto out;
    }
    
    // Substract the disabled entries from the total title count
    for(i = 0; i < totalTitleCount; i++)
    {
        if (!batchEntries[i].enabled) disabledEntryCount++;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    breaks += 2;
    
    initial_breaks = breaks;
    
    j = 0;
    
    for(i = 0; i < totalTitleCount; i++)
    {
        if (!batchEntries[i].enabled) continue;
        
        breaks = initial_breaks;
        
        uiFill(0, 8 + (breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (breaks * LINE_HEIGHT)), BG_COLOR_RGB);
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Title: %u / %u.", j + 1, totalTitleCount - disabledEntryCount);
        uiRefreshDisplay();
        
        breaks += 2;
        
        // Dump title
        if (!dumpNintendoSubmissionPackage(batchEntries[i].titleType, batchEntries[i].titleIndex, &nspDumpCfg, true)) goto out;
        
        // Create override file if necessary
        if (rememberDumpedTitles)
        {
            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s", BATCH_OVERRIDES_PATH, batchEntries[i].nspFilename);
            FILE *overrideFile = fopen(dumpPath, "wb");
            if (overrideFile) fclose(overrideFile);
        }
        
        // Update free space
        uiUpdateFreeSpace();
        
        j++;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed!");
    breaks += 2;
    
out:
    free(batchEntries);
    return success;
}

bool dumpRawHfs0Partition(u32 partition, bool doSplitting)
{
    Result result;
    u64 partitionOffset;
    bool proceed = true, success = false, fat32_error = false;
    u64 n = DUMP_BUFFER_SIZE;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    char filename[NAME_BUF_LEN * 2] = {'\0'};
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess();
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
    {
        /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "GetGameCardHandle succeeded: 0x%08X", handle.value);
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
            /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "OpenGameCardStorage succeeded: 0x%08X", handle.value);
            breaks++;*/
            
            if (getHfs0EntryDetails(hfs0_header, hfs0_offset, hfs0_size, hfs0_partition_cnt, partition, true, 0, &partitionOffset, &(progressCtx.totalSize)))
            {
                convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "HFS0 partition size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
                breaks += 2;
                
                /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "HFS0 partition offset (relative to IStorage instance): 0x%016lX", partitionOffset);
                breaks += 2;*/
                
                if (progressCtx.totalSize <= freeSpace)
                {
                    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
                    {
                        snprintf(filename, MAX_ELEMENTS(filename), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                    } else {
                        snprintf(filename, MAX_ELEMENTS(filename), "%s%s - Partition %u (%s).hfs0", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    }
                    
                    // Check if the dump already exists
                    if (checkIfFileExists(filename))
                    {
                        // Ask the user if they want to proceed anyway
                        int cur_breaks = breaks;
                        
                        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
                        if (!proceed)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                        } else {
                            // Remove the prompt from the screen
                            breaks = cur_breaks;
                            uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
                        }
                    }
                    
                    if (proceed)
                    {
                        outFile = fopen(filename, "wb");
                        if (outFile)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dumping raw HFS0 partition #%u. Hold %s to cancel.", partition, NINTENDO_FONT_B);
                            breaks += 2;
                            
                            if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                            {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
                                breaks += 2;
                            }
                            
                            uiRefreshDisplay();
                            
                            progressCtx.line_offset = (breaks + 2);
                            timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
                            
                            for (progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
                            {
                                uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
                                
                                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
                                
                                if (n > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
                                
                                if (R_FAILED(result = fsStorageRead(&gameCardStorage, partitionOffset + progressCtx.curOffset, dumpBuf, n)))
                                {
                                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "StorageRead failed (0x%08X) at offset 0x%016lX", result, partitionOffset + progressCtx.curOffset);
                                    break;
                                }
                                
                                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
                                {
                                    u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                                    u64 old_file_chunk_size = (n - new_file_chunk_size);
                                    
                                    if (old_file_chunk_size > 0)
                                    {
                                        write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                                        if (write_res != old_file_chunk_size)
                                        {
                                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                                            break;
                                        }
                                    }
                                    
                                    fclose(outFile);
                                    outFile = NULL;
                                    
                                    if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
                                    {
                                        splitIndex++;
                                        snprintf(filename, MAX_ELEMENTS(filename), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                                        
                                        outFile = fopen(filename, "wb");
                                        if (!outFile)
                                        {
                                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                                            break;
                                        }
                                        
                                        if (new_file_chunk_size > 0)
                                        {
                                            write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                            if (write_res != new_file_chunk_size)
                                            {
                                                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                                                break;
                                            }
                                        }
                                    }
                                } else {
                                    write_res = fwrite(dumpBuf, 1, n, outFile);
                                    if (write_res != n)
                                    {
                                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                                        
                                        if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                                        {
                                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                                            fat32_error = true;
                                        }
                                        
                                        break;
                                    }
                                }
                                
                                printProgressBar(&progressCtx, true, n);
                                
                                if ((progressCtx.curOffset + n) < progressCtx.totalSize)
                                {
                                    if (cancelProcessCheck(&progressCtx))
                                    {
                                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                                        break;
                                    }
                                }
                            }
                            
                            if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
                            
                            // Support empty files
                            if (!progressCtx.totalSize)
                            {
                                uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
                                
                                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(filename, '/' ) + 1);
                                
                                progressCtx.progress = 100;
                                
                                printProgressBar(&progressCtx, false, 0);
                            }
                            
                            breaks = (progressCtx.line_offset + 2);
                            
                            if (success)
                            {
                                timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
                                progressCtx.now -= progressCtx.start;
                                
                                formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
                            } else {
                                setProgressBarError(&progressCtx);
                                if (fat32_error) breaks += 2;
                            }
                            
                            if (outFile) fclose(outFile);
                            
                            if (!success)
                            {
                                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
                                {
                                    for(u8 i = 0; i <= splitIndex; i++)
                                    {
                                        snprintf(filename, MAX_ELEMENTS(filename), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), i);
                                        unlink(filename);
                                    }
                                } else {
                                    unlink(filename);
                                }
                            }
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to open output file \"%s\"!", filename);
                        }
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to get partition details from the root HFS0 header!");
            }
            
            fsStorageClose(&gameCardStorage);
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "OpenGameCardStorage failed! (0x%08X)", result);
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "GetGameCardHandle failed! (0x%08X)", result);
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
    u64 off, n = DUMP_BUFFER_SIZE;
    u8 splitIndex = 0;
    
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    
    size_t write_res;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    uiFill(0, ((progressCtx->line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 4), FONT_COLOR_RGB, "Copying \"%s\"...", source);
    
    if ((destLen + 1) < NAME_BUF_LEN)
    {
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
        {
            /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "GetGameCardHandle succeeded: 0x%08X", handle.value);
            breaks++;*/
            
            // Same ugly hack from dumpRawHfs0Partition()
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
            {
                /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "OpenGameCardStorage succeeded: 0x%08X", handle.value);
                breaks++;*/
                
                if (size > FAT32_FILESIZE_LIMIT && doSplitting) snprintf(splitFilename, MAX_ELEMENTS(splitFilename), "%s.%02u", dest, splitIndex);
                
                outFile = fopen(((size > FAT32_FILESIZE_LIMIT && doSplitting) ? splitFilename : dest), "wb");
                if (outFile)
                {
                    for (off = 0; off < size; off += n, progressCtx->curOffset += n)
                    {
                        uiFill(0, ((progressCtx->line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
                        
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", ((size > FAT32_FILESIZE_LIMIT && doSplitting) ? (strrchr(splitFilename, '/') + 1) : (strrchr(dest, '/') + 1)));
                        
                        uiRefreshDisplay();
                        
                        if (n > (size - off)) n = (size - off);
                        
                        if (R_FAILED(result = fsStorageRead(&gameCardStorage, file_offset + off, dumpBuf, n)))
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "StorageRead failed (0x%08X) at offset 0x%016lX", result, file_offset + off);
                            break;
                        }
                        
                        if (size > FAT32_FILESIZE_LIMIT && doSplitting && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
                        {
                            u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                            u64 old_file_chunk_size = (n - new_file_chunk_size);
                            
                            if (old_file_chunk_size > 0)
                            {
                                write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                                if (write_res != old_file_chunk_size)
                                {
                                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, off, splitIndex, write_res);
                                    break;
                                }
                            }
                            
                            fclose(outFile);
                            outFile = NULL;
                            
                            if (new_file_chunk_size > 0 || (off + n) < size)
                            {
                                splitIndex++;
                                snprintf(splitFilename, MAX_ELEMENTS(splitFilename), "%s.%02u", dest, splitIndex);
                                
                                outFile = fopen(splitFilename, "wb");
                                if (!outFile)
                                {
                                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                                    break;
                                }
                                
                                if (new_file_chunk_size > 0)
                                {
                                    write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                    if (write_res != new_file_chunk_size)
                                    {
                                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                                        break;
                                    }
                                }
                            }
                        } else {
                            write_res = fwrite(dumpBuf, 1, n, outFile);
                            if (write_res != n)
                            {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                                
                                if ((off + n) > FAT32_FILESIZE_LIMIT)
                                {
                                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                                    fat32_error = true;
                                }
                                
                                break;
                            }
                        }
                        
                        printProgressBar(progressCtx, true, n);
                        
                        if ((off + n) < size || (progressCtx->curOffset + n) < progressCtx->totalSize)
                        {
                            if (cancelProcessCheck(progressCtx))
                            {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                                break;
                            }
                        }
                    }
                    
                    if (off >= size) success = true;
                    
                    // Support empty files
                    if (!size)
                    {
                        uiFill(0, ((progressCtx->line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
                        
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", ((size > FAT32_FILESIZE_LIMIT && doSplitting) ? (strrchr(splitFilename, '/') + 1) : (strrchr(dest, '/') + 1)));
                        
                        if (progressCtx->totalSize == size) progressCtx->progress = 100;
                        
                        printProgressBar(progressCtx, false, 0);
                    }
                    
                    if (!success)
                    {
                        setProgressBarError(progressCtx);
                        breaks = (progressCtx->line_offset + 2);
                        if (fat32_error) breaks += 2;
                    }
                    
                    if (outFile) fclose(outFile);
                    
                    if (!success)
                    {
                        if (size > FAT32_FILESIZE_LIMIT && doSplitting)
                        {
                            for(u8 i = 0; i <= splitIndex; i++)
                            {
                                snprintf(splitFilename, MAX_ELEMENTS(splitFilename), "%s.%02u", dest, i);
                                unlink(splitFilename);
                            }
                        } else {
                            unlink(dest);
                        }
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_RGB, "Failed to open output file!");
                }
                
                fsStorageClose(&gameCardStorage);
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "OpenGameCardStorage failed! (0x%08X)", result);
            }
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "GetGameCardHandle failed! (0x%08X)", result);
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Destination path is too long! (%lu bytes)", destLen);
    }
    
    return success;
}

bool copyHfs0Contents(u32 partition, hfs0_entry_table *partitionEntryTable, progress_ctx_t *progressCtx, const char *dest, bool splitting)
{
    if (!dest || !*dest)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: destination directory is empty.");
        return false;
    }
    
    if (!partitionHfs0Header || !partitionEntryTable)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "HFS0 partition header information unavailable!");
        return false;
    }
    
    if (!progressCtx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid progress context.");
        return false;
    }
    
    char dbuf[NAME_BUF_LEN] = {'\0'};
    size_t dest_len = strlen(dest);
    
    if ((dest_len + 1) >= NAME_BUF_LEN)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Destination directory name is too long! (%lu bytes)", dest_len);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
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
                
                convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Total partition data size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
                breaks += 2;
                
                if (progressCtx.totalSize <= freeSpace)
                {
                    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s - Partition %u (%s)", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Copying partition #%u data to \"%s/\". Hold %s to cancel.", partition, dumpPath, NINTENDO_FONT_B);
                    breaks += 2;
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
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
                        
                        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
                    } else {
                        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
                }
                
                free(entryTable);
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Unable to allocate memory for the HFS0 file entries!");
            }
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "The selected partition is empty!");
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "HFS0 partition header information unavailable!");
        breaks += 2;
        return false;
    }
    
    if (!filename || !*filename)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Filename unavailable!");
        breaks += 2;
        return false;
    }
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    u64 file_offset = 0;
    u64 file_size = 0;
    bool proceed = true, success = false;
    
    if (getHfs0EntryDetails(partitionHfs0Header, partitionHfs0HeaderOffset, partitionHfs0HeaderSize, partitionHfs0FileCount, file, false, partition, &file_offset, &file_size))
    {
        progressCtx.totalSize = file_size;
        convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
        breaks++;
        
        if (file_size <= freeSpace)
        {
            char destCopyPath[NAME_BUF_LEN * 2] = {'\0'};
            char fixedFilename[NAME_BUF_LEN] = {'\0'};
            
            sprintf(fixedFilename, filename);
            removeIllegalCharacters(fixedFilename);
            
            snprintf(destCopyPath, MAX_ELEMENTS(destCopyPath), "%s%s - Partition %u (%s)", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
            
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
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    } else {
                        // Remove the prompt from the screen
                        breaks = cur_breaks;
                        uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
                    }
                }
                
                if (proceed)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Hold %s to cancel.", NINTENDO_FONT_B);
                    breaks += 2;
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
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
                        
                        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
                    }
                }
            } else {
                breaks++;
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Destination path is too long! (%lu bytes)", strlen(destCopyPath) + 1 + strlen(filename));
            }
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to get file details from the partition HFS0 header!");
    }
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpExeFsSectionData(u32 titleIndex, bool usePatch, bool doSplitting)
{
    u64 n;
    FILE *outFile;
    u8 splitIndex;
    bool proceed = true, success = false, fat32_error = false;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'}, curDumpPath[NAME_BUF_LEN * 4] = {'\0'};
    char tmp_idx[5];
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    u32 i;
    u64 offset;
    
    if ((!usePatch && !titleAppCount) || (usePatch && !titlePatchCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title count!");
        breaks += 2;
        return false;
    }
    
    if ((!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title index!");
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    // Retrieve ExeFS from Program NCA
    if (!readNcaExeFsSection(titleIndex, usePatch))
    {
        free(dumpName);
        breaks += 2;
        return false;
    }
    
    // Calculate total dump size
    if (!calculateExeFsExtractedDataSize(&(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Extracted ExeFS dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
        goto out;
    }
    
    // Prepare output dump path
    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s", EXEFS_DUMP_PATH, dumpName);
    mkdir(dumpPath, 0744);
    
    // Start dump process
    breaks++;
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    uiRefreshDisplay();
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: file entry without name in ExeFS section!");
            break;
        }
        
        snprintf(curDumpPath, MAX_ELEMENTS(curDumpPath), "%s/%s", dumpPath, exeFsFilename);
        removeIllegalCharacters(curDumpPath + strlen(dumpPath) + 1);
        
        if (exeFsContext.exefs_entries[i].file_size > FAT32_FILESIZE_LIMIT && doSplitting)
        {
            sprintf(tmp_idx, ".%02u", splitIndex);
            strcat(curDumpPath, tmp_idx);
        }
        
        outFile = fopen(curDumpPath, "wb");
        if (!outFile)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file!");
            break;
        }
        
        uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Copying \"%s\"...", exeFsFilename);
        
        for(offset = 0; offset < exeFsContext.exefs_entries[i].file_size; offset += n, progressCtx.curOffset += n)
        {
            uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(curDumpPath, '/') + 1);
            
            uiRefreshDisplay();
            
            if (n > (exeFsContext.exefs_entries[i].file_size - offset)) n = (exeFsContext.exefs_entries[i].file_size - offset);
            
            breaks = (progressCtx.line_offset + 2);
            proceed = processNcaCtrSectionBlock(&(exeFsContext.ncmStorage), &(exeFsContext.ncaId), &(exeFsContext.aes_ctx), exeFsContext.exefs_data_offset + exeFsContext.exefs_entries[i].file_offset + offset, dumpBuf, n, false);
            breaks = (progressCtx.line_offset - 4);
            
            if (!proceed) break;
            
            if (exeFsContext.exefs_entries[i].file_size > FAT32_FILESIZE_LIMIT && doSplitting && (offset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
            {
                u64 new_file_chunk_size = ((offset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, offset, splitIndex, write_res);
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
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, offset + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, offset, write_res);
                    
                    if ((offset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                        fat32_error = true;
                    }
                    
                    proceed = false;
                    break;
                }
            }
            
            printProgressBar(&progressCtx, true, n);
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                if (cancelProcessCheck(&progressCtx))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
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
            uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(curDumpPath, '/') + 1);
            
            if (progressCtx.totalSize == exeFsContext.exefs_entries[i].file_size) progressCtx.progress = 100;
            
            printProgressBar(&progressCtx, false, 0);
        }
    }
    
    if (proceed)
    {
        if (progressCtx.curOffset >= progressCtx.totalSize)
        {
            success = true;
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Unexpected underdump error! Wrote %lu bytes, expected %lu bytes.", progressCtx.curOffset, progressCtx.totalSize);
        }
    }
    
    breaks = (progressCtx.line_offset + 2);
    
    if (success)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        if (fat32_error) breaks += 2;
        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
    }
    
out:
    freeExeFsContext();
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpFileFromExeFsSection(u32 titleIndex, u32 fileIndex, bool usePatch, bool doSplitting)
{
    if (!exeFsContext.exefs_header.file_cnt || fileIndex > (exeFsContext.exefs_header.file_cnt - 1) || !exeFsContext.exefs_entries || !exeFsContext.exefs_str_table || exeFsContext.exefs_data_offset <= exeFsContext.exefs_offset || (!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to parse file entry from ExeFS section!");
        breaks += 2;
        return false;
    }
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false, removeFile = true;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    char tmp_idx[5];
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    char *exeFsFilename = (exeFsContext.exefs_str_table + exeFsContext.exefs_entries[fileIndex].filename_offset);
    
    // Check if we're dealing with a nameless file
    if (!strlen(exeFsFilename))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: file entry without name in ExeFS section!");
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    // Generate output path
    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s", EXEFS_DUMP_PATH, dumpName);
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
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
            removeFile = false;
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Hold %s to cancel.", NINTENDO_FONT_B);
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
        breaks += 2;
    }
    
    // Start dump process
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Copying \"%s\"...", exeFsFilename);
    breaks += 2;
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to open output file!");
        goto out;
    }
    
    progressCtx.line_offset = (breaks + 2);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    for(progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        
        uiRefreshDisplay();
        
        if (n > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
        
        breaks = (progressCtx.line_offset + 2);
        proceed = processNcaCtrSectionBlock(&(exeFsContext.ncmStorage), &(exeFsContext.ncaId), &(exeFsContext.aes_ctx), exeFsContext.exefs_data_offset + exeFsContext.exefs_entries[fileIndex].file_offset + progressCtx.curOffset, dumpBuf, n, false);
        breaks = (progressCtx.line_offset - 2);
        
        if (!proceed) break;
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
        {
            u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
            u64 old_file_chunk_size = (n - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(dumpBuf, 1, n, outFile);
            if (write_res != n)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(&progressCtx, true, n);
        
        if ((progressCtx.curOffset + n) < progressCtx.totalSize)
        {
            if (cancelProcessCheck(&progressCtx))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                break;
            }
        }
    }
    
    if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
    
    // Support empty files
    if (!progressCtx.totalSize)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        
        progressCtx.progress = 100;
        
        printProgressBar(&progressCtx, false, 0);
    }
    
    breaks = (progressCtx.line_offset + 2);
    
    if (success)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
    }
    
out:
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to parse file entry from RomFS section!");
        return false;
    }
    
    size_t orig_romfs_path_len = strlen(romfs_path);
    size_t orig_output_path_len = strlen(output_path);
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false;
    
    u32 romfs_file_offset = file_offset;
    romfs_file *entry = NULL;
    
    u64 off = 0;
    
    size_t write_res;
    
    char tmp_idx[5];
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    while(romfs_file_offset != ROMFS_ENTRY_EMPTY)
    {
        romfs_path[orig_romfs_path_len] = '\0';
        output_path[orig_output_path_len] = '\0';
        
        entry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + romfs_file_offset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + romfs_file_offset));
        
        // Check if we're dealing with a nameless file
        if (!entry->nameLen)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: file entry without name in RomFS section!");
            break;
        }
        
        if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: RomFS section file path is too long!");
            break;
        }
        
        // Generate current path
        strcat(romfs_path, "/");
        strncat(romfs_path, (char*)entry->name, entry->nameLen);
        
        strcat(output_path, "/");
        strncat(output_path, (char*)entry->name, entry->nameLen);
        removeIllegalCharacters(output_path + orig_output_path_len + 1);
        
        // Start dump process
        uiFill(0, ((progressCtx->line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 4), FONT_COLOR_RGB, "Copying \"romfs:%s\"...", romfs_path);
        
        if (entry->dataSize > FAT32_FILESIZE_LIMIT && doSplitting)
        {
            sprintf(tmp_idx, ".%02u", splitIndex);
            strcat(output_path, tmp_idx);
        }
        
        outFile = fopen(output_path, "wb");
        if (!outFile)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_RGB, "Failed to open output file!");
            break;
        }
        
        n = DUMP_BUFFER_SIZE;
        splitIndex = 0;
        
        for(off = 0; off < entry->dataSize; off += n, progressCtx->curOffset += n)
        {
            uiFill(0, ((progressCtx->line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(output_path, '/') + 1);
            
            uiRefreshDisplay();
            
            if (n > (entry->dataSize - off)) n = (entry->dataSize - off);
            
            breaks = (progressCtx->line_offset + 2);
            
            if (!usePatch)
            {
                proceed = processNcaCtrSectionBlock(&(romFsContext.ncmStorage), &(romFsContext.ncaId), &(romFsContext.aes_ctx), romFsContext.romfs_filedata_offset + entry->dataOff + off, dumpBuf, n, false);
            } else {
                proceed = readBktrSectionBlock(bktrContext.romfs_filedata_offset + entry->dataOff + off, dumpBuf, n);
            }
            
            breaks = (progressCtx->line_offset - 4);
            
            if (!proceed) break;
            
            if (entry->dataSize > FAT32_FILESIZE_LIMIT && doSplitting && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
            {
                u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, off, splitIndex, write_res);
                        proceed = false;
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
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                    
                    if ((off + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                        fat32_error = true;
                    }
                    
                    proceed = false;
                    break;
                }
            }
            
            printProgressBar(progressCtx, true, n);
            
            if ((off + n) < entry->dataSize || (progressCtx->curOffset + n) < progressCtx->totalSize)
            {
                if (cancelProcessCheck(progressCtx))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    proceed = false;
                    break;
                }
            }
        }
        
        if (outFile)
        {
            fclose(outFile);
            outFile = NULL;
        }
        
        if (!proceed || off < entry->dataSize) break;
        
        // Support empty files
        if (!entry->dataSize)
        {
            uiFill(0, ((progressCtx->line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(output_path, '/') + 1);
            
            if (progressCtx->totalSize == entry->dataSize) progressCtx->progress = 100;
            
            printProgressBar(progressCtx, false, 0);
        }
        
        romfs_file_offset = entry->sibling;
        if (romfs_file_offset == ROMFS_ENTRY_EMPTY) success = true;
    }
    
    if (!success)
    {
        breaks = (progressCtx->line_offset + 2);
        if (fat32_error) breaks += 2;
    }
    
    romfs_path[orig_romfs_path_len] = '\0';
    output_path[orig_output_path_len] = '\0';
    
    return success;
}

bool recursiveDumpRomFsDir(u32 dir_offset, char *romfs_path, char *output_path, progress_ctx_t *progressCtx, bool usePatch, bool dumpSiblingDir, bool doSplitting)
{
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries || !bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || !romfs_path || !output_path || !progressCtx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to parse directory entry from RomFS section!");
        return false;
    }
    
    size_t orig_romfs_path_len = strlen(romfs_path);
    size_t orig_output_path_len = strlen(output_path);
    
    romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
    
    // Check if we're dealing with a nameless directory that's not the root directory
    if (!entry->nameLen && dir_offset > 0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: directory entry without name in RomFS section!");
        return false;
    }
    
    if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Error: RomFS section directory path is too long!");
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

bool dumpRomFsSectionData(u32 titleIndex, selectedRomFsType curRomFsType, bool doSplitting)
{
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char romFsPath[NAME_BUF_LEN * 2] = {'\0'}, dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    if ((curRomFsType == ROMFS_TYPE_APP && !titleAppCount) || (curRomFsType == ROMFS_TYPE_PATCH && !titlePatchCount) || (curRomFsType == ROMFS_TYPE_ADDON && !titleAddOnCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title count!");
        breaks += 2;
        return false;
    }
    
    if ((curRomFsType == ROMFS_TYPE_APP && titleIndex > (titleAppCount - 1)) || (curRomFsType == ROMFS_TYPE_PATCH && titleIndex > (titlePatchCount - 1)) || (curRomFsType == ROMFS_TYPE_ADDON && titleIndex > (titleAddOnCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title index!");
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((curRomFsType == ROMFS_TYPE_APP ? DUMP_APP_NSP : (curRomFsType == ROMFS_TYPE_PATCH ? DUMP_PATCH_NSP : DUMP_ADDON_NSP)), titleIndex);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    // Retrieve RomFS from Program NCA
    if (!readNcaRomFsSection(titleIndex, curRomFsType))
    {
        free(dumpName);
        breaks += 2;
        return false;
    }
    
    // Calculate total dump size
    if (!calculateRomFsFullExtractedSize((curRomFsType == ROMFS_TYPE_PATCH), &(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Extracted RomFS dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
        goto out;
    }
    
    // Prepare output dump path
    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s", ROMFS_DUMP_PATH, dumpName);
    mkdir(dumpPath, 0744);
    
    // Start dump process
    breaks++;
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    uiRefreshDisplay();
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
        breaks += 2;
    }
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    success = recursiveDumpRomFsDir(0, romFsPath, dumpPath, &progressCtx, (curRomFsType == ROMFS_TYPE_PATCH), true, doSplitting);
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
    }
    
out:
    if (curRomFsType == ROMFS_TYPE_PATCH) freeBktrContext();
    
    freeRomFsContext();
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpFileFromRomFsSection(u32 titleIndex, u32 file_offset, selectedRomFsType curRomFsType, bool doSplitting)
{
    if ((curRomFsType != ROMFS_TYPE_PATCH && (!romFsContext.romfs_filetable_size || file_offset > romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (curRomFsType == ROMFS_TYPE_PATCH && (!bktrContext.romfs_filetable_size || file_offset > bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || (curRomFsType == ROMFS_TYPE_APP && titleIndex > (titleAppCount - 1)) || (curRomFsType == ROMFS_TYPE_PATCH && titleIndex > (titlePatchCount - 1)) || (curRomFsType == ROMFS_TYPE_ADDON && titleIndex > (titleAddOnCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to parse file entry from RomFS section!");
        breaks += 2;
        return false;
    }
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false, removeFile = true;
    
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    char tmp_idx[5];
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    romfs_file *entry = (curRomFsType != ROMFS_TYPE_PATCH ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + file_offset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + file_offset));
    
    // Check if we're dealing with a nameless file
    if (!entry->nameLen)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: file entry without name in RomFS section!");
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((curRomFsType == ROMFS_TYPE_APP ? DUMP_APP_NSP : (curRomFsType == ROMFS_TYPE_PATCH ? DUMP_PATCH_NSP : DUMP_ADDON_NSP)), titleIndex);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    // Generate output path
    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s", ROMFS_DUMP_PATH, dumpName);
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
    
    progressCtx.totalSize = entry->dataSize;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
        goto out;
    }
    
    breaks++;
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
    {
        sprintf(tmp_idx, ".%02u", splitIndex);
        strcat(dumpPath, tmp_idx);
    }
    
    // Check if the dump already exists
    if (checkIfFileExists(dumpPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        
        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
        if (!proceed)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
            removeFile = false;
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Hold %s to cancel.", NINTENDO_FONT_B);
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
        breaks += 2;
    }
    
    // Start dump process
    if (strlen(curRomFsPath) > 1)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Copying \"romfs:%s/%.*s\"...", curRomFsPath, entry->nameLen, entry->name);
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Copying \"romfs:/%.*s\"...", entry->nameLen, entry->name);
    }
    
    breaks += 2;
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to open output file!");
        goto out;
    }
    
    progressCtx.line_offset = (breaks + 2);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    for(progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        
        uiRefreshDisplay();
        
        if (n > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
        
        breaks = (progressCtx.line_offset + 2);
        
        if (curRomFsType != ROMFS_TYPE_PATCH)
        {
            proceed = processNcaCtrSectionBlock(&(romFsContext.ncmStorage), &(romFsContext.ncaId), &(romFsContext.aes_ctx), romFsContext.romfs_filedata_offset + entry->dataOff + progressCtx.curOffset, dumpBuf, n, false);
        } else {
            proceed = readBktrSectionBlock(bktrContext.romfs_filedata_offset + entry->dataOff + progressCtx.curOffset, dumpBuf, n);
        }
        
        breaks = (progressCtx.line_offset - 2);
        
        if (!proceed) break;
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
        {
            u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
            u64 old_file_chunk_size = (n - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to open output file for part #%u!", splitIndex);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(dumpBuf, 1, n, outFile);
            if (write_res != n)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, progressCtx.curOffset, write_res);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(&progressCtx, true, n);
        
        if ((progressCtx.curOffset + n) < progressCtx.totalSize)
        {
            if (cancelProcessCheck(&progressCtx))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                break;
            }
        }
    }
    
    if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
    
    // Support empty files
    if (!progressCtx.totalSize)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/') + 1);
        
        progressCtx.progress = 100;
        
        printProgressBar(&progressCtx, false, 0);
    }
    
    breaks = (progressCtx.line_offset + 2);
    
    if (success)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
    }
    
out:
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

bool dumpCurrentDirFromRomFsSection(u32 titleIndex, selectedRomFsType curRomFsType, bool doSplitting)
{
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char romFsPath[NAME_BUF_LEN * 2] = {'\0'}, dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    if ((curRomFsType == ROMFS_TYPE_APP && !titleAppCount) || (curRomFsType == ROMFS_TYPE_PATCH && !titlePatchCount) || (curRomFsType == ROMFS_TYPE_ADDON && !titleAddOnCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title count!");
        breaks += 2;
        return false;
    }
    
    if ((curRomFsType == ROMFS_TYPE_APP && titleIndex > (titleAppCount - 1)) || (curRomFsType == ROMFS_TYPE_PATCH && titleIndex > (titlePatchCount - 1)) || (curRomFsType == ROMFS_TYPE_ADDON && titleIndex > (titleAddOnCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid title index!");
        breaks += 2;
        return false;
    }
    
    char *dumpName = generateNSPDumpName((curRomFsType == ROMFS_TYPE_APP ? DUMP_APP_NSP : (curRomFsType == ROMFS_TYPE_PATCH ? DUMP_PATCH_NSP : DUMP_ADDON_NSP)), titleIndex);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    // Calculate total dump size
    if (!calculateRomFsExtractedDirSize(curRomFsDirOffset, (curRomFsType == ROMFS_TYPE_PATCH), &(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_ELEMENTS(progressCtx.totalSizeStr));
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Extracted RomFS directory size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
        goto out;
    }
    
    if (strlen(curRomFsPath) > 1)
    {
        // Copy the whole current path and remove the last element (current directory) from it
        // It will be re-added later
        snprintf(romFsPath, MAX_ELEMENTS(romFsPath), curRomFsPath);
        char *slash = strrchr(romFsPath, '/');
        if (slash) *slash = '\0';
    }
    
    // Prepare output dump path
    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s", ROMFS_DUMP_PATH, dumpName);
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
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump procedure started. Hold %s to cancel.", NINTENDO_FONT_B);
    uiRefreshDisplay();
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
        breaks += 2;
    }
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    success = recursiveDumpRomFsDir(curRomFsDirOffset, romFsPath, dumpPath, &progressCtx, (curRomFsType == ROMFS_TYPE_PATCH), false, doSplitting);
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_ELEMENTS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
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
    size_t write_res;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    char *dumpName = generateFullDumpName();
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to generate output dump name!");
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess();
    
    if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(&fsOperatorInstance, &handle)))
    {
        /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "GetGameCardHandle succeeded: 0x%08X", handle.value);
        breaks++;*/
        
        if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, 0)))
        {
            /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "OpenGameCardStorage succeeded: 0x%08X", handle.value);
            breaks++;*/
            
            if (CERT_SIZE <= freeSpace)
            {
                if (R_SUCCEEDED(result = fsStorageRead(&gameCardStorage, CERT_OFFSET, dumpBuf, CERT_SIZE)))
                {
                    // Calculate CRC32
                    crc32(dumpBuf, CERT_SIZE, &crc);
                    
                    snprintf(filename, MAX_ELEMENTS(filename), "%s%s - Certificate (%08X).bin", CERT_DUMP_PATH, dumpName, crc);
                    
                    // Check if the dump already exists
                    if (checkIfFileExists(filename))
                    {
                        // Ask the user if they want to proceed anyway
                        int cur_breaks = breaks;
                        
                        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
                        if (!proceed)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                        } else {
                            // Remove the prompt from the screen
                            breaks = cur_breaks;
                            uiFill(0, 8 + STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - (8 + STRING_Y_POS(breaks)), BG_COLOR_RGB);
                        }
                    }
                    
                    if (proceed)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dumping gamecard certificate to \"%s\"...", strrchr(filename, '/' ) + 1);
                        breaks += 2;
                        
                        if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Do not press the " NINTENDO_FONT_HOME " button. Doing so could corrupt the SD card filesystem.");
                            breaks += 2;
                        }
                        
                        uiRefreshDisplay();
                        
                        outFile = fopen(filename, "wb");
                        if (outFile)
                        {
                            write_res = fwrite(dumpBuf, 1, CERT_SIZE, outFile);
                            if (write_res == CERT_SIZE)
                            {
                                success = true;
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed!");
                            } else {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to write %u bytes certificate data! (wrote %lu bytes)", CERT_SIZE, write_res);
                            }
                            
                            fclose(outFile);
                            if (!success) unlink(filename);
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to open output file \"%s\"!", filename);
                        }
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "StorageRead failed (0x%08X) at offset 0x%08X", result, CERT_OFFSET);
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: not enough free space available in the SD card.");
            }
            
            fsStorageClose(&gameCardStorage);
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "OpenGameCardStorage failed! (0x%08X)", result);
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "GetGameCardHandle failed! (0x%08X)", result);
    }
    
    breaks += 2;
    
    free(dumpName);
    
    return success;
}
