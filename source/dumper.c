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
#include "save.h"

/* Extern variables */

extern nca_keyset_t nca_keyset;

extern u64 freeSpace;

extern bool highlight;
extern int breaks;
extern int font_height;

extern gamecard_ctx_t gameCardInfo;

extern u32 titleAppCount, titlePatchCount, titleAddOnCount;
extern u32 sdCardTitleAppCount, sdCardTitlePatchCount, sdCardTitleAddOnCount;
extern u32 emmcTitleAppCount, emmcTitlePatchCount, emmcTitleAddOnCount;

extern base_app_ctx_t *baseAppEntries;
extern patch_addon_ctx_t *patchEntries, *addOnEntries;

extern exefs_ctx_t exeFsContext;
extern romfs_ctx_t romFsContext;
extern bktr_ctx_t bktrContext;

extern char curRomFsPath[NAME_BUF_LEN];
extern u32 curRomFsDirOffset;

extern u8 *enabledNormalIconBuf;
extern u8 *enabledHighlightIconBuf;
extern u8 *disabledNormalIconBuf;
extern u8 *disabledHighlightIconBuf;

extern u8 *dumpBuf;

extern char strbuf[NAME_BUF_LEN];

extern u64 freeSpace;
extern char freeSpaceStr[32];

extern char cfwDirStr[32];

static void dumpStartMsg()
{
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump procedure started. Hold " NINTENDO_FONT_B " to cancel.");
    breaks++;
}

bool dumpNXCardImage(xciOptions *xciDumpCfg)
{
    if (!xciDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid XCI configuration struct!", __func__);
        breaks += 2;
        return false;
    }
    
    bool isFat32 = xciDumpCfg->isFat32;
    bool setXciArchiveBit = xciDumpCfg->setXciArchiveBit;
    bool keepCert = xciDumpCfg->keepCert;
    bool trimDump = xciDumpCfg->trimDump;
    bool calcCrc = xciDumpCfg->calcCrc;
    bool useNoIntroLookup = xciDumpCfg->useNoIntroLookup;
    bool useBrackets = xciDumpCfg->useBrackets;
    
    u64 partitionOffset = 0, xciDataSize = 0, n;
    u64 partitionSizes[ISTORAGE_PARTITION_CNT];
    char dumpPath[NAME_BUF_LEN] = {'\0'};
    u32 partition;
    Result result;
    bool proceed = true, success = false, fat32_error = false;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    u32 certCrc = 0, certlessCrc = 0;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    bool seqDumpMode = false, seqDumpFileRemove = false, seqDumpFinish = false;
    char seqDumpFilename[NAME_BUF_LEN] = {'\0'};
    FILE *seqDumpFile = NULL;
    u64 seqDumpFileSize = 0, seqDumpSessionOffset = 0;
    
    sequentialXciCtx seqXciCtx;
    memset(&seqXciCtx, 0, sizeof(sequentialXciCtx));
    
    char tmp_idx[5];
    
    size_t read_res, write_res;
    
    char *dumpName = generateGameCardDumpName(useBrackets);
    if (!dumpName)
    {
        // We're probably dealing with a forced XCI dump
        dumpName = calloc(16, sizeof(char));
        if (!dumpName)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
            breaks += 2;
            return false;
        }
        
        sprintf(dumpName, "gamecard");
    }
    
    // Check if we're dealing with a sequential dump
    snprintf(seqDumpFilename, MAX_CHARACTERS(seqDumpFilename), "%s%s.xci.seq", XCI_DUMP_PATH, dumpName);
    seqDumpMode = checkIfFileExists(seqDumpFilename);
    if (seqDumpMode)
    {
        // Open sequence file
        seqDumpFile = fopen(seqDumpFilename, "rb+");
        if (!seqDumpFile)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to open existing sequential dump reference file for reading! (\"%s\")", __func__, seqDumpFilename);
            goto out;
        }
        
        // Retrieve sequence file size
        fseek(seqDumpFile, 0, SEEK_END);
        seqDumpFileSize = ftell(seqDumpFile);
        rewind(seqDumpFile);
        
        // Check file size
        if (seqDumpFileSize != sizeof(sequentialXciCtx))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: sequential dump reference file size mismatch! (%lu != %lu)", __func__, seqDumpFileSize, sizeof(sequentialXciCtx));
            seqDumpFileRemove = true;
            goto out;
        }
        
        // Read file contents
        read_res = fread(&seqXciCtx, 1, seqDumpFileSize, seqDumpFile);
        rewind(seqDumpFile);
        
        if (read_res != seqDumpFileSize)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes long sequential dump reference file! (read %lu bytes)", __func__, seqDumpFileSize, read_res);
            goto out;
        }
        
        // Check if the IStorage partition index is valid
        if (seqXciCtx.partitionIndex > (ISTORAGE_PARTITION_CNT - 1))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid IStorage partition index in sequential dump reference file!", __func__);
            seqDumpFileRemove = true;
            goto out;
        }
        
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
    }
    
    u64 partSize = (seqDumpMode ? SPLIT_FILE_SEQUENTIAL_SIZE : (!setXciArchiveBit ? SPLIT_FILE_XCI_PART_SIZE : SPLIT_FILE_NSP_PART_SIZE));
    
    // Retrieve dump sizes for each IStorage partition
    for(partition = 0; partition < ISTORAGE_PARTITION_CNT; partition++)
    {
        partitionSizes[partition] = gameCardInfo.IStoragePartitionSizes[partition];
        xciDataSize += partitionSizes[partition];
    }
    
    if (trimDump)
    {
        // Change dump size for the secure IStorage partition
        u64 partitionSizesSum = 0;
        for(partition = 0; partition < (ISTORAGE_PARTITION_CNT - 1); partition++) partitionSizesSum += partitionSizes[partition];
        
        partitionSizes[ISTORAGE_PARTITION_CNT - 1] = (gameCardInfo.trimmedSize - partitionSizesSum);
        
        progressCtx.totalSize = gameCardInfo.trimmedSize;
    } else {
        progressCtx.totalSize = xciDataSize;
    }
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Output dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks += 2;
    
    if (seqDumpMode)
    {
        // Check if the current offset doesn't exceed the total XCI size
        if (progressCtx.curOffset >= progressCtx.totalSize)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid XCI offset in the sequential dump reference file!", __func__);
            seqDumpFileRemove = true;
            goto out;
        }
        
        // Check if the current partition offset doesn't exceed the partition size
        if (seqXciCtx.partitionOffset >= partitionSizes[seqXciCtx.partitionIndex])
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid IStorage partition offset in the sequential dump reference file!", __func__);
            seqDumpFileRemove = true;
            goto out;
        }
        
        u64 curXciOffset = 0, restSize = 0;
        
        for(u32 i = 0; i < seqXciCtx.partitionIndex; i++) curXciOffset += partitionSizes[i];
        curXciOffset += seqXciCtx.partitionOffset;
        
        restSize = (progressCtx.totalSize - curXciOffset);
        
        // Check if our previously calculated XCI offset is aligned to SPLIT_FILE_SEQUENTIAL_SIZE
        if (curXciOffset != progressCtx.curOffset)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: overall XCI dump offset isn't aligned to 0x%08X in the sequential dump reference file!", __func__, (u32)SPLIT_FILE_SEQUENTIAL_SIZE);
            seqDumpFileRemove = true;
            goto out;
        }
        
        // Check if there's enough free space to continue the sequential dump process
        if (progressCtx.totalSize > freeSpace && ((restSize > SPLIT_FILE_SEQUENTIAL_SIZE && freeSpace < SPLIT_FILE_SEQUENTIAL_SIZE) || (restSize <= SPLIT_FILE_SEQUENTIAL_SIZE && freeSpace < restSize)))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
            goto out;
        }
        
        // Inform that we are resuming an already started sequential dump operation
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation. Configuration parameters overrided.");
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Keep certificate: %s | Trim output dump: %s | CRC32 checksum calculation + dump verification: %s.", (keepCert ? "Yes" : "No"), (trimDump ? "Yes" : "No"), (calcCrc ? "Yes" : "No"));
        breaks += 2;
        
        uiRefreshDisplay();
    } else {
        if (progressCtx.totalSize > freeSpace)
        {
            // Check if we have at least (SPLIT_FILE_SEQUENTIAL_SIZE + sizeof(sequentialXciCtx)) of free space
            if (freeSpace < (SPLIT_FILE_SEQUENTIAL_SIZE + sizeof(sequentialXciCtx)))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
                goto out;
            }
            
            // Ask the user if they want to use the sequential dump mode
            int cur_breaks = breaks;
            
            if (!yesNoPrompt("There's not enough space available to generate a whole dump in this session. Do you want to use sequential dumping?\nIn this mode, the selected content will be dumped in more than one session.\nYou'll have to transfer the generated part files to a PC before continuing the process in the next session."))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                goto out;
            }
            
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
            uiRefreshDisplay();
            
            // Modify config parameters
            isFat32 = true;
            setXciArchiveBit = false;
            
            partSize = SPLIT_FILE_SEQUENTIAL_SIZE;
            
            seqDumpMode = true;
            seqDumpFileSize = sizeof(sequentialXciCtx);
            
            // Fill information in our sequential context
            seqXciCtx.keepCert = keepCert;
            seqXciCtx.trimDump = trimDump;
            seqXciCtx.calcCrc = calcCrc;
            
            // Create sequential reference file and keep the handle to it opened
            seqDumpFile = fopen(seqDumpFilename, "wb+");
            if (!seqDumpFile)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to create sequential dump reference file! (\"%s\")", __func__, seqDumpFilename);
                goto out;
            }
            
            write_res = fwrite(&seqXciCtx, 1, seqDumpFileSize, seqDumpFile);
            rewind(seqDumpFile);
            
            if (write_res != seqDumpFileSize)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", __func__, seqDumpFileSize, write_res);
                seqDumpFileRemove = true;
                goto out;
            }
            
            // Update free space
            freeSpace -= seqDumpFileSize;
        }
    }
    
    if (seqDumpMode)
    {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci.%02u", XCI_DUMP_PATH, dumpName, splitIndex);
    } else {
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            if (setXciArchiveBit)
            {
                // Temporary, we'll use this to check if the dump already exists (it should have the archive bit set if so)
                snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci", XCI_DUMP_PATH, dumpName);
            } else {
                snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, splitIndex);
            }
        } else {
            snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci", XCI_DUMP_PATH, dumpName);
        }
        
        // Check if the dump already exists
        if (checkIfFileExists(dumpPath))
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
                uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
            }
        }
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && setXciArchiveBit)
        {
            // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
            // Better safe than sorry
            remove(dumpPath);
            fsdevDeleteDirectoryRecursively(dumpPath);
            
            mkdir(dumpPath, 0744);
            
            sprintf(tmp_idx, "/%02u", splitIndex);
            strcat(dumpPath, tmp_idx);
        }
    }
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, dumpPath);
        goto out;
    }
    
    // Start dump process
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    u32 startPartitionIndex = (seqDumpMode ? seqXciCtx.partitionIndex : 0);
    u64 startPartitionOffset;
    
    for(partition = startPartitionIndex; partition < ISTORAGE_PARTITION_CNT; partition++)
    {
        n = DUMP_BUFFER_SIZE;
        
        startPartitionOffset = ((seqDumpMode && partition == startPartitionIndex) ? seqXciCtx.partitionOffset : 0);
        
        openIStoragePartition idx = (openIStoragePartition)(partition + 1);
        
        result = openGameCardStoragePartition(idx);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open IStorage partition #%u! (0x%08X)", __func__, partition, result);
            proceed = false;
            break;
        }
        
        for(partitionOffset = startPartitionOffset; partitionOffset < partitionSizes[partition]; partitionOffset += n, progressCtx.curOffset += n, seqDumpSessionOffset += n)
        {
            if (seqDumpMode && seqDumpFinish) break;
            
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Dumping IStorage partition #%u...", partition);
            
            if (n > (partitionSizes[partition] - partitionOffset)) n = (partitionSizes[partition] - partitionOffset);
            
            // Check if the next read chunk will exceed the size of the current part file
            if (seqDumpMode && (seqDumpSessionOffset + n) >= (((splitIndex - seqXciCtx.partNumber) + 1) * partSize))
            {
                u64 new_file_chunk_size = ((seqDumpSessionOffset + n) - (((splitIndex - seqXciCtx.partNumber) + 1) * partSize));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                u64 remainderDumpSize = (progressCtx.totalSize - (progressCtx.curOffset + old_file_chunk_size));
                u64 remainderFreeSize = (freeSpace - (seqDumpSessionOffset + old_file_chunk_size));
                
                // Check if we have enough space for the next part
                // If so, set the chunk size to old_file_chunk_size
                if ((remainderDumpSize <= partSize && remainderDumpSize > remainderFreeSize) || (remainderDumpSize > partSize && partSize > remainderFreeSize))
                {
                    n = old_file_chunk_size;
                    seqDumpFinish = true;
                }
            }
            
            result = readGameCardStoragePartition(partitionOffset, dumpBuf, n);
            if (R_FAILED(result))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes chunk at offset 0x%016lX from IStorage partition #%u! (0x%08X)", __func__, n, partitionOffset, partition, result);
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
            
            if ((seqDumpMode || (!seqDumpMode && progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)) && (progressCtx.curOffset + n) >= ((splitIndex + 1) * partSize))
            {
                u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * partSize));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
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
                        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci.%02u", XCI_DUMP_PATH, dumpName, splitIndex);
                    } else {
                        if (setXciArchiveBit)
                        {
                            snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci/%02u", XCI_DUMP_PATH, dumpName, splitIndex);
                        } else {
                            snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, splitIndex);
                        }
                    }
                    
                    outFile = fopen(dumpPath, "wb");
                    if (!outFile)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, progressCtx.curOffset, write_res);
                    
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
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize && cancelProcessCheck(&progressCtx))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                proceed = false;
                break;
            }
        }
        
        closeGameCardStoragePartition();
        
        if (!proceed)
        {
            if (seqDumpMode) seqDumpFileRemove = true;
            break;
        }
        
        // Support empty files
        if (!partitionSizes[partition])
        {
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", __func__, seqDumpFileSize, write_res);
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
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "XCI dump CRC32 checksum (with certificate): %08X | XCI dump CRC32 checksum (without certificate): %08X", certCrc, certlessCrc);
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "XCI dump CRC32 checksum: %08X", certlessCrc);
                }
                
                breaks++;
                
                uiRefreshDisplay();
                
                if (useNoIntroLookup)
                {
                    noIntroDumpCheck(false, certlessCrc);
                } else {
                    gameCardDumpNSWDBCheck(certlessCrc);
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "XCI dump CRC32 checksum: %08X", certCrc);
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump verification disabled (not compatible with trimmed dumps).");
            }
        }
        
        // Set archive bit (only for FAT32 and if the required option is enabled)
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && setXciArchiveBit)
        {
            snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci", XCI_DUMP_PATH, dumpName);
            result = fsdevSetConcatenationFileAttribute(dumpPath);
            if (R_FAILED(result))
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
                snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci.%02u", XCI_DUMP_PATH, dumpName, i);
                remove(dumpPath);
            }
        } else {
            if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
            {
                if (setXciArchiveBit)
                {
                    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci", XCI_DUMP_PATH, dumpName);
                    fsdevDeleteDirectoryRecursively(dumpPath);
                } else {
                    for(u8 i = 0; i <= splitIndex; i++)
                    {
                        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xc%u", XCI_DUMP_PATH, dumpName, i);
                        remove(dumpPath);
                    }
                }
            } else {
                remove(dumpPath);
            }
        }
    }
    
out:
    if (dumpName) free(dumpName);
    
    if (seqDumpFile) fclose(seqDumpFile);
    
    if (seqDumpFileRemove) remove(seqDumpFilename);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

int dumpNintendoSubmissionPackage(nspDumpType selectedNspDumpType, u32 titleIndex, nspOptions *nspDumpCfg, bool batch)
{
    int ret = -1;
    
    if (!nspDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NSP configuration struct!", __func__);
        breaks += 2;
        return ret;
    }
    
    bool isFat32 = nspDumpCfg->isFat32;
    bool useNoIntroLookup = nspDumpCfg->useNoIntroLookup;
    bool removeConsoleData = nspDumpCfg->removeConsoleData;
    bool tiklessDump = nspDumpCfg->tiklessDump;
    bool npdmAcidRsaPatch = nspDumpCfg->npdmAcidRsaPatch;
    bool dumpDeltaFragments = nspDumpCfg->dumpDeltaFragments;
    bool useBrackets = nspDumpCfg->useBrackets;
    bool preInstall = false;
    
    Result result;
    u32 i = 0, j = 0;
    
    NcmStorageId curStorageId;
    NcmContentMetaType metaType;
    u32 titleCount = 0, ncmTitleIndex = 0;
    
    char dumpPath[NAME_BUF_LEN] = {'\0'};
    
    NcmContentInfo *titleContentInfos = NULL;
    u32 titleContentInfoCnt = 0;
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    cnmt_xml_program_info xml_program_info;
    cnmt_xml_content_info *xml_content_info = NULL;
    
    NcmContentId ncaId;
    u8 ncaHeader[NCA_FULL_HEADER_LENGTH] = {0};
    nca_header_t dec_nca_header;
    
    nca_cnmt_mod_data ncaCnmtMod;
    memset(&ncaCnmtMod, 0, sizeof(nca_cnmt_mod_data));
    
    u32 ncaProgramModCnt = 0;
    nca_program_mod_data *ncaProgramMod = NULL;
    
    title_rights_ctx rights_info;
    memset(&rights_info, 0, sizeof(title_rights_ctx));
    
    u32 cnmtNcaIndex = 0;
    u8 *cnmtNcaBuf = NULL;
    bool cnmtFound = false;
    
    pfs0_header nspPfs0Header;
    memset(&nspPfs0Header, 0, sizeof(pfs0_header));
    nspPfs0Header.magic = __builtin_bswap32(PFS0_MAGIC);
    
    pfs0_file_entry *nspPfs0EntryTable = NULL;
    
    char *nspPfs0StrTable = NULL;
    u64 nspPfs0StrTableSize = 0;
    
    u64 fullPfs0HeaderSize = 0;
    
    u8 **nspPfs0FilePtrs = NULL;
    
    Sha256Context nca_hash_ctx;
    sha256ContextCreate(&nca_hash_ctx);
    
    u64 n, fileOffset;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    u32 crc = 0;
    bool proceed = true, dumping = false, fat32_error = false, removeFile = true;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    bool seqDumpMode = false, seqDumpFileRemove = false, seqDumpFinish = false;
    char seqDumpFilename[NAME_BUF_LEN] = {'\0'};
    FILE *seqDumpFile = NULL;
    u64 seqDumpFileSize = 0, seqDumpSessionOffset = 0;
    u8 *seqDumpNcaHashes = NULL;
    
    sequentialNspCtx seqNspCtx;
    memset(&seqNspCtx, 0, sizeof(sequentialNspCtx));
    
    char pfs0HeaderFilename[NAME_BUF_LEN] = {'\0'};
    FILE *pfs0HeaderFile = NULL;
    
    char tmp_idx[5];
    
    size_t read_res, write_res;
    
    if ((selectedNspDumpType == DUMP_APP_NSP && !baseAppEntries) || (selectedNspDumpType == DUMP_PATCH_NSP && !patchEntries) || (selectedNspDumpType == DUMP_ADDON_NSP && !addOnEntries))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: title storage ID unavailable!", __func__);
        breaks += 2;
        return ret;
    }
    
    if ((selectedNspDumpType == DUMP_APP_NSP && titleIndex >= titleAppCount) || (selectedNspDumpType == DUMP_PATCH_NSP && titleIndex >= titlePatchCount) || (selectedNspDumpType == DUMP_ADDON_NSP && titleIndex >= titleAddOnCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title index!", __func__);
        breaks += 2;
        return ret;
    }
    
    curStorageId = (selectedNspDumpType == DUMP_APP_NSP ? baseAppEntries[titleIndex].storageId : (selectedNspDumpType == DUMP_PATCH_NSP ? patchEntries[titleIndex].storageId : addOnEntries[titleIndex].storageId));
    
    ncmTitleIndex = (selectedNspDumpType == DUMP_APP_NSP ? baseAppEntries[titleIndex].ncmIndex : (selectedNspDumpType == DUMP_PATCH_NSP ? patchEntries[titleIndex].ncmIndex : addOnEntries[titleIndex].ncmIndex));
    
    metaType = (selectedNspDumpType == DUMP_APP_NSP ? NcmContentMetaType_Application : (selectedNspDumpType == DUMP_PATCH_NSP ? NcmContentMetaType_Patch : NcmContentMetaType_AddOnContent));
    
    switch(curStorageId)
    {
        case NcmStorageId_GameCard:
            titleCount = (selectedNspDumpType == DUMP_APP_NSP ? titleAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? titlePatchCount : titleAddOnCount));
            break;
        case NcmStorageId_SdCard:
            titleCount = (selectedNspDumpType == DUMP_APP_NSP ? sdCardTitleAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? sdCardTitlePatchCount : sdCardTitleAddOnCount));
            break;
        case NcmStorageId_BuiltInUser:
            titleCount = (selectedNspDumpType == DUMP_APP_NSP ? emmcTitleAppCount : (selectedNspDumpType == DUMP_PATCH_NSP ? emmcTitlePatchCount : emmcTitleAddOnCount));
            break;
        default:
            break;
    }
    
    char *dumpName = generateNSPDumpName(selectedNspDumpType, titleIndex, useBrackets);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
        breaks += 2;
        return ret;
    }
    
    if (!batch)
    {
        snprintf(seqDumpFilename, MAX_CHARACTERS(seqDumpFilename), "%s%s.nsp.seq", NSP_DUMP_PATH, dumpName);
        snprintf(pfs0HeaderFilename, MAX_CHARACTERS(pfs0HeaderFilename), "%s%s.nsp.hdr", NSP_DUMP_PATH, dumpName);
        
        // Check if we're dealing with a sequential dump
        seqDumpMode = checkIfFileExists(seqDumpFilename);
        if (seqDumpMode)
        {
            // Open sequence file
            seqDumpFile = fopen(seqDumpFilename, "rb+");
            if (!seqDumpFile)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to open existing sequential dump reference file for reading! (\"%s\")", __func__, seqDumpFilename);
                goto out;
            }
            
            // Retrieve sequence file size
            fseek(seqDumpFile, 0, SEEK_END);
            seqDumpFileSize = ftell(seqDumpFile);
            rewind(seqDumpFile);
            
            // Read sequentialNspCtx struct info
            read_res = fread(&seqNspCtx, 1, sizeof(sequentialNspCtx), seqDumpFile);
            if (read_res != sizeof(sequentialNspCtx))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes chunk from the sequential dump reference file! (read %lu bytes)", __func__, sizeof(sequentialNspCtx), read_res);
                goto out;
            }
            
            // Check if the storage ID is right
            if (seqNspCtx.storageId != curStorageId)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid source storage ID in sequential dump reference file!", __func__);
                goto out;
            }
            
            // Check if the Program NCA mod count field is valid
            if (seqNspCtx.programNcaModCount > 0 && !seqNspCtx.npdmAcidRsaPatch)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid Program NCA mod count sequential dump reference file!", __func__);
                seqDumpFileRemove = true;
                goto out;
            }
            
            // Check file size
            if (seqDumpFileSize != (sizeof(sequentialNspCtx) + (seqNspCtx.ncaCount * SHA256_HASH_SIZE) + (seqNspCtx.programNcaModCount * NCA_FULL_HEADER_LENGTH)))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid sequential dump reference file size!", __func__);
                seqDumpFileRemove = true;
                goto out;
            }
            
            // Allocate memory for the NCA hashes
            seqDumpNcaHashes = calloc(1, seqNspCtx.ncaCount * SHA256_HASH_SIZE);
            if (!seqDumpNcaHashes)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA hashes from the sequential dump reference file!", __func__);
                goto out;
            }
            
            // Read NCA hashes
            read_res = fread(seqDumpNcaHashes, 1, seqNspCtx.ncaCount * SHA256_HASH_SIZE, seqDumpFile);
            if (read_res != (seqNspCtx.ncaCount * SHA256_HASH_SIZE))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes chunk from the sequential dump reference file! (read %lu bytes)", __func__, seqNspCtx.ncaCount * SHA256_HASH_SIZE, read_res);
                goto out;
            }
            
            // Restore parameters from the sequence file
            isFat32 = true;
            removeConsoleData = seqNspCtx.removeConsoleData;
            tiklessDump = seqNspCtx.tiklessDump;
            npdmAcidRsaPatch = seqNspCtx.npdmAcidRsaPatch;
            preInstall = seqNspCtx.preInstall;
            splitIndex = seqNspCtx.partNumber;
            progressCtx.curOffset = ((u64)seqNspCtx.partNumber * SPLIT_FILE_SEQUENTIAL_SIZE);
        }
    }
    
    u64 partSize = (seqDumpMode ? SPLIT_FILE_SEQUENTIAL_SIZE : SPLIT_FILE_NSP_PART_SIZE);
    
    if (!batch)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Retrieving information from encrypted NCA content files...");
        uiRefreshDisplay();
        breaks += 2;
    }
    
    if (!retrieveContentInfosFromTitle(curStorageId, metaType, titleCount, ncmTitleIndex, &titleContentInfos, &titleContentInfoCnt))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
        goto out;
    }
    
    // If we're dealing with a gamecard, open the Secure HFS0 partition (IStorage partition #1) to read NCA data
    // We may also need to retrieve a ticket if we're dealing with a Patch with titlekey crypto
    if (curStorageId == NcmStorageId_GameCard)
    {
        result = openGameCardStoragePartition(ISTORAGE_PARTITION_SECURE);
        if (R_FAILED(result))
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: failed to open IStorage partition #1! (0x%08X)", __func__, result);
            goto out;
        }
    }
    
    result = ncmOpenContentStorage(&ncmStorage, curStorageId);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: ncmOpenContentStorage failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    // Fill information for our CNMT XML
    memset(&xml_program_info, 0, sizeof(cnmt_xml_program_info));
    xml_program_info.type = (u8)metaType;
    xml_program_info.title_id = (selectedNspDumpType == DUMP_APP_NSP ? baseAppEntries[titleIndex].titleId : (selectedNspDumpType == DUMP_PATCH_NSP ? patchEntries[titleIndex].titleId : addOnEntries[titleIndex].titleId));
    xml_program_info.version = (selectedNspDumpType == DUMP_APP_NSP ? baseAppEntries[titleIndex].version : (selectedNspDumpType == DUMP_PATCH_NSP ? patchEntries[titleIndex].version : addOnEntries[titleIndex].version));
    xml_program_info.nca_cnt = titleContentInfoCnt;
    
    xml_content_info = calloc(titleContentInfoCnt, sizeof(cnmt_xml_content_info));
    if (!xml_content_info)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for the CNMT XML content info struct!", __func__);
        goto out;
    }
    
    // Fill our CNMT XML content records, leaving the CNMT NCA at the end
    u32 titleContentInfoIndex;
    for(i = 0, titleContentInfoIndex = 0; titleContentInfoIndex < titleContentInfoCnt; i++, titleContentInfoIndex++)
    {
        if (!cnmtFound && titleContentInfos[titleContentInfoIndex].content_type == NcmContentType_Meta)
        {
            cnmtFound = true;
            cnmtNcaIndex = titleContentInfoIndex;
            i--;
            continue;
        }
        
        // Skip Delta Fragments and/or any other unknown content types (only if the related option is disabled)
        // Delta Fragments are used to update from a certain version to another version without needing to install the whole update
        // For any dumping purposes, they're useless, because they just increase the size of the output dump. The more updates come out for a title, the more Delta Fragments there will be available for that title
        // Also, since they're basically an eShop thing, they're not available in gamecards (so in this particular case, we need to skip them anyway)
        // However, their content records must be kept intact in the CNMT NCA
        if (titleContentInfos[titleContentInfoIndex].content_type >= NcmContentType_DeltaFragment && !dumpDeltaFragments)
        {
            xml_program_info.nca_cnt--;
            i--;
            continue;
        }
        
        // Fill information for our CNMT XML
        xml_content_info[i].type = titleContentInfos[titleContentInfoIndex].content_type;
        memcpy(xml_content_info[i].nca_id, titleContentInfos[titleContentInfoIndex].content_id.c, SHA256_HASH_SIZE / 2); // Temporary
        convertDataToHexString(titleContentInfos[titleContentInfoIndex].content_id.c, SHA256_HASH_SIZE / 2, xml_content_info[i].nca_id_str, SHA256_HASH_SIZE + 1); // Temporary
        convertNcaSizeToU64(titleContentInfos[titleContentInfoIndex].size, &(xml_content_info[i].size));
        xml_content_info[i].id_offset = titleContentInfos[titleContentInfoIndex].id_offset;
        convertDataToHexString(xml_content_info[i].hash, SHA256_HASH_SIZE, xml_content_info[i].hash_str, (SHA256_HASH_SIZE * 2) + 1); // Temporary
        
        memcpy(&ncaId, &(titleContentInfos[titleContentInfoIndex].content_id), sizeof(NcmContentId));
        
        if (!readNcaDataByContentId(&ncmStorage, &ncaId, 0, ncaHeader, NCA_FULL_HEADER_LENGTH))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read header from NCA \"%s\"!", __func__, xml_content_info[i].nca_id_str);
            proceed = false;
            break;
        }
        
        // Decrypt the NCA header
        // Don't retrieve the ticket and/or titlekey if we're dealing with a Patch with titlekey crypto bundled with the inserted gamecard
        if (!decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header, &rights_info, xml_content_info[i].decrypted_nca_keys, (curStorageId != NcmStorageId_GameCard)))
        {
            proceed = false;
            break;
        }
        
        // Check if this particular content has a populated Rights ID field
        bool has_rights_id = false;
        
        for(j = 0; j < 0x10; j++)
        {
            if (dec_nca_header.rights_id[j] != 0)
            {
                has_rights_id = true;
                break;
            }
        }
        
        // Check if the missing ticket flag is enabled
        // If so, we may be dealing with a preinstalled title
        if (curStorageId != NcmStorageId_GameCard && has_rights_id && rights_info.missing_tik && !preInstall)
        {
            // Only display the pre-install prompt if we're not running a batch / sequential dump operation (excluding the first run of the latter)
            if (!batch && !seqDumpMode)
            {
                int cur_breaks = breaks;
                breaks += 2;
                
                proceed = yesNoPrompt("This is probably a pre-installed title, which explains why a ticket for it couldn't be found (even though its Rights ID field isn't empty).\nDo you want to proceed with the dump procedure anyway?\nBear in mind that no content decryption will be possible for this title in its current status.");
                if (!proceed)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    break;
                } else {
                    breaks = cur_breaks;
                    preInstall = true;
                }
            }
            
            // Remove the prompt / error from the screen
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        }
        
        // Fill information for our CNMT XML
        xml_content_info[i].keyblob = (dec_nca_header.crypto_type2 > dec_nca_header.crypto_type ? dec_nca_header.crypto_type2 : dec_nca_header.crypto_type);
        
        if (curStorageId == NcmStorageId_GameCard)
        {
            // Modify content distribution type
            // It's always set to 1 (gamecard) in Applications and Add-Ons bundled in gamecards
            // It's always set to 0 (download) in Patches bundled in gamecards. But if we're dealing with a custom XCI mounted through SX OS, we may need to change that
            dec_nca_header.distribution = 0;
            
            if (selectedNspDumpType == DUMP_APP_NSP || selectedNspDumpType == DUMP_ADDON_NSP) 
            {
                // Application and AddOn titles don't have a populated Rights ID field when bundled in gamecards
                if (has_rights_id)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: Rights ID field in NCA header not empty!", __func__);
                    proceed = false;
                    break;
                }
                
                // Patch ACID public RSA key and recreate the NCA NPDM signature if we're dealing with the Program NCA
                if (xml_content_info[i].type == NcmContentType_Program && npdmAcidRsaPatch)
                {
                    if (!processProgramNca(&ncmStorage, &ncaId, &dec_nca_header, &(xml_content_info[i]), &ncaProgramMod, &ncaProgramModCnt, i))
                    {
                        proceed = false;
                        break;
                    }
                }
            } else
            if (selectedNspDumpType == DUMP_PATCH_NSP)
            {
                // Patch titles *do* have a populated Rights ID field and a ticket + certificate chain combination when bundled in gamecards
                // Depending on the dump settings, we may need to change or remove that
                // If no Rights ID is available, we may be dealing with a custom XCI mounted through SX OS. In this particular case, no further modifications should be needed
                if (has_rights_id)
                {
                    // Retrieve the ticket from the HFS0 partition in the gamecard
                    if (!retrieveTitleKeyFromGameCardTicket(&rights_info, xml_content_info[i].decrypted_nca_keys))
                    {
                        proceed = false;
                        break;
                    }
                    
                    // Mess with the NCA header if we're dealing with a NCA with a populated Rights ID field and if tiklessDump is true (removeConsoleData is ignored)
                    if (tiklessDump)
                    {
                        // Generate new encrypted NCA key area using titlekey
                        if (!generateEncryptedNcaKeyAreaWithTitlekey(&dec_nca_header, xml_content_info[i].decrypted_nca_keys))
                        {
                            proceed = false;
                            break;
                        }
                        
                        // Remove Rights ID from NCA
                        memset(dec_nca_header.rights_id, 0, 0x10);
                        
                        // Patch ACID pubkey and recreate NCA NPDM signature if we're dealing with the Program NCA
                        if (xml_content_info[i].type == NcmContentType_Program && npdmAcidRsaPatch)
                        {
                            if (!processProgramNca(&ncmStorage, &ncaId, &dec_nca_header, &(xml_content_info[i]), &ncaProgramMod, &ncaProgramModCnt, i))
                            {
                                proceed = false;
                                break;
                            }
                        }
                    }
                }
            }
        } else
        if (curStorageId == NcmStorageId_SdCard || curStorageId == NcmStorageId_BuiltInUser)
        {
            // Only mess with the NCA header if we're dealing with a content with a populated Rights ID field, and if both removeConsoleData and tiklessDump are true
            // This will only be done if we were able to retrieve the ticket for this title
            if (has_rights_id && rights_info.retrieved_tik && removeConsoleData && tiklessDump)
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
                if (xml_content_info[i].type == NcmContentType_Program && npdmAcidRsaPatch)
                {
                    if (!processProgramNca(&ncmStorage, &ncaId, &dec_nca_header, &(xml_content_info[i]), &ncaProgramMod, &ncaProgramModCnt, i))
                    {
                        proceed = false;
                        break;
                    }
                }
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find CNMT NCA!", __func__);
        goto out;
    }
    
    // Update NCA counter just in case we found any delta fragments and excluded them
    titleContentInfoCnt = xml_program_info.nca_cnt;
    
    // Fill information for our CNMT XML
    xml_content_info[titleContentInfoCnt - 1].type = titleContentInfos[cnmtNcaIndex].content_type;
    memcpy(xml_content_info[titleContentInfoCnt - 1].nca_id, titleContentInfos[cnmtNcaIndex].content_id.c, SHA256_HASH_SIZE / 2); // Temporary
    convertDataToHexString(titleContentInfos[cnmtNcaIndex].content_id.c, SHA256_HASH_SIZE / 2, xml_content_info[titleContentInfoCnt - 1].nca_id_str, SHA256_HASH_SIZE + 1); // Temporary
    convertNcaSizeToU64(titleContentInfos[cnmtNcaIndex].size, &(xml_content_info[titleContentInfoCnt - 1].size));
    xml_content_info[titleContentInfoCnt - 1].id_offset = titleContentInfos[cnmtNcaIndex].id_offset;
    convertDataToHexString(xml_content_info[titleContentInfoCnt - 1].hash, SHA256_HASH_SIZE, xml_content_info[titleContentInfoCnt - 1].hash_str, (SHA256_HASH_SIZE * 2) + 1); // Temporary
    
    memcpy(&ncaId, &(titleContentInfos[cnmtNcaIndex].content_id), sizeof(NcmContentId));
    
    // Update CNMT index
    cnmtNcaIndex = (titleContentInfoCnt - 1);
    
    cnmtNcaBuf = malloc(xml_content_info[cnmtNcaIndex].size);
    if (!cnmtNcaBuf)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for CNMT NCA data!", __func__);
        goto out;
    }
    
    if (!readNcaDataByContentId(&ncmStorage, &ncaId, 0, cnmtNcaBuf, xml_content_info[cnmtNcaIndex].size))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read CNMT NCA \"%s\"!", __func__, xml_content_info[cnmtNcaIndex].nca_id_str);
        goto out;
    }
    
    // Retrieve CNMT NCA data
    if (!retrieveCnmtNcaData(curStorageId, cnmtNcaBuf, &xml_program_info, xml_content_info, cnmtNcaIndex, &ncaCnmtMod, &rights_info)) goto out;
    
    bool includeTikAndCert = (rights_info.retrieved_tik && !tiklessDump);
    
    if (includeTikAndCert)
    {
        // Only mess with the ticket data if removeConsoleData is true, if tiklessDump is false and if we're dealing with a personalized ticket (checked in removeConsoleDataFromTicket())
        // Ticket files from Patch titles bundled with gamecards always use common titlekey crypto
        if ((curStorageId == NcmStorageId_SdCard || curStorageId == NcmStorageId_BuiltInUser) && removeConsoleData) removeConsoleDataFromTicket(&rights_info);
        
        // Retrieve cert file
        if (!retrieveCertData(rights_info.cert_data, (rights_info.tik_data.titlekey_type == ETICKET_TITLEKEY_PERSONALIZED)))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
            goto out;
        }
        
        // File count = NCA count + tik + cert
        nspPfs0Header.file_cnt = (titleContentInfoCnt + 2);
        
        // Calculate PFS0 String Table size
        nspPfs0StrTableSize = (((titleContentInfoCnt - 1) * NSP_NCA_FILENAME_LENGTH) + NSP_CNMT_FILENAME_LENGTH + NSP_TIK_FILENAME_LENGTH + NSP_CERT_FILENAME_LENGTH);
    } else {
        // File count = NCA count
        nspPfs0Header.file_cnt = titleContentInfoCnt;
        
        // Calculate PFS0 String Table size
        nspPfs0StrTableSize = (((titleContentInfoCnt - 1) * NSP_NCA_FILENAME_LENGTH) + NSP_CNMT_FILENAME_LENGTH);
    }
    
    // Start NSP creation
    nspPfs0EntryTable = calloc(nspPfs0Header.file_cnt, sizeof(pfs0_file_entry));
    if (!nspPfs0EntryTable)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for the PFS0 file entries!", __func__);
        goto out;
    }
    
    // Make sure we have enough space
    nspPfs0StrTable = calloc(nspPfs0StrTableSize * 2, sizeof(char));
    if (!nspPfs0StrTable)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for the PFS0 string table!", __func__);
        goto out;
    }
    
    // Determine our full NSP header size
    fullPfs0HeaderSize = (sizeof(pfs0_header) + ((u64)nspPfs0Header.file_cnt * sizeof(pfs0_file_entry)) + nspPfs0StrTableSize);
    
    // Round up our full NSP header size to a 0x10-byte boundary
    if (!(fullPfs0HeaderSize % 0x10)) fullPfs0HeaderSize++; // If it's already rounded, add more padding
    fullPfs0HeaderSize = round_up(fullPfs0HeaderSize, 0x10);
    
    // Determine our String Table size
    nspPfs0Header.str_table_size = (fullPfs0HeaderSize - (sizeof(pfs0_header) + ((u64)nspPfs0Header.file_cnt * sizeof(pfs0_file_entry))));
    
    // Allocate memory for PFS0 file data pointer array. Exclude all NCAs but the CNMT NCA
    nspPfs0FilePtrs = calloc(nspPfs0Header.file_cnt - (titleContentInfoCnt - 1), sizeof(u8*));
    if (!nspPfs0FilePtrs)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for the PFS0 file data pointer array!", __func__);
        goto out;
    }
    
    // Fill PFS0 entry table
    // PFS0 string table will be filled at a later time
    u64 curFileOffset = 0;
    u32 curFilenameOffset = 0;
    
    u64 entrySize = 0;
    u32 entryFilenameSize = 0;
    
    u32 entryIdx = 0, ptrIdx = 0;
    
    for(i = 0; i < titleContentInfoCnt; i++, entryIdx++)
    {
        // Always reserve the first titleContentInfoCnt entries for our NCAs
        // Only save the CNMT NCA buffer pointer to the PFS0 file data pointer array. We don't have any other pointers to raw NCA data, so we leave the rest untouched
        entrySize = xml_content_info[i].size;
        entryFilenameSize = (i == cnmtNcaIndex ? NSP_CNMT_FILENAME_LENGTH : NSP_NCA_FILENAME_LENGTH);
        if (i == cnmtNcaIndex) nspPfs0FilePtrs[ptrIdx++] = cnmtNcaBuf;
        
        nspPfs0EntryTable[i].file_size = entrySize;
        nspPfs0EntryTable[i].file_offset = curFileOffset;
        nspPfs0EntryTable[i].filename_offset = curFilenameOffset;
        
        curFileOffset += entrySize;
        curFilenameOffset += entryFilenameSize;
    }
    
    if (includeTikAndCert)
    {
        for(i = 0; i < 2; i++, entryIdx++)
        {
            entrySize = (i == 0 ? ETICKET_TIK_FILE_SIZE : ETICKET_CERT_FILE_SIZE);
            entryFilenameSize = (i == 0 ? NSP_TIK_FILENAME_LENGTH : NSP_CERT_FILENAME_LENGTH);
            nspPfs0FilePtrs[ptrIdx++] = (i == 0 ? (u8*)(&(rights_info.tik_data)) : rights_info.cert_data);
            
            nspPfs0EntryTable[entryIdx].file_size = entrySize;
            nspPfs0EntryTable[entryIdx].file_offset = curFileOffset;
            nspPfs0EntryTable[entryIdx].filename_offset = curFilenameOffset;
            
            curFileOffset += entrySize;
            curFilenameOffset += entryFilenameSize;
        }
    }
    
    // Calculate total dump size
    progressCtx.totalSize += fullPfs0HeaderSize;
    for(i = 0; i < titleContentInfoCnt; i++) progressCtx.totalSize += xml_content_info[i].size;
    if (includeTikAndCert) progressCtx.totalSize += (ETICKET_TIK_FILE_SIZE + ETICKET_CERT_FILE_SIZE);
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Total NSP dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiRefreshDisplay();
    breaks += 2;
    
    if (!batch)
    {
        if (seqDumpMode)
        {
            // Check if the current offset doesn't exceed the total NSP size
            if (progressCtx.curOffset >= progressCtx.totalSize)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NSP offset in the sequential dump reference file!", __func__);
                goto out;
            }
            
            // Check if the NCA count is valid
            // The CNMT NCA is excluded from the hash list
            if (seqNspCtx.ncaCount != (titleContentInfoCnt - 1))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: NCA count mismatch in the sequential dump reference file! (%u != %u)", __func__, seqNspCtx.ncaCount, titleContentInfoCnt - 1);
                goto out;
            }
            
            // Check if the Program NCA mod count is valid
            if (seqNspCtx.programNcaModCount != ncaProgramModCnt)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: Program NCA mod count mismatch in the sequential dump reference file! (%u != %u)", __func__, seqNspCtx.programNcaModCount, ncaProgramModCnt);
                goto out;
            }
            
            // Check if the PFS0 file count is valid
            if (seqNspCtx.pfs0FileCount != nspPfs0Header.file_cnt)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: PFS0 file count mismatch in the sequential dump reference file! (%u != %u)", __func__, seqNspCtx.pfs0FileCount, nspPfs0Header.file_cnt);
                goto out;
            }
            
            // Check if the current PFS0 file index is valid
            if (seqNspCtx.fileIndex >= nspPfs0Header.file_cnt)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid PFS0 file index in the sequential dump reference file!", __func__);
                goto out;
            }
            
            // Check if we're really dealing with a title with a missing ticket if preInstall == true
            if (seqNspCtx.preInstall && !rights_info.missing_tik)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title preinstall status in the sequential dump reference file!", __func__);
                goto out;
            }
            
            // Check if the current overall offset is aligned to SPLIT_FILE_SEQUENTIAL_SIZE
            u64 curNspOffset = fullPfs0HeaderSize;
            for(i = 0; i < seqNspCtx.fileIndex; i++) curNspOffset += nspPfs0EntryTable[i].file_size;
            curNspOffset += seqNspCtx.fileOffset;
            
            if (curNspOffset != progressCtx.curOffset)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: overall NSP dump offset isn't aligned to 0x%08X in the sequential dump reference file!", __func__, (u32)SPLIT_FILE_SEQUENTIAL_SIZE);
                goto out;
            }
            
            // Check if there's enough free space to continue the sequential dump process
            u64 restSize = (progressCtx.totalSize - curNspOffset);
            if (progressCtx.totalSize > freeSpace && ((restSize > SPLIT_FILE_SEQUENTIAL_SIZE && freeSpace < SPLIT_FILE_SEQUENTIAL_SIZE) || (restSize <= SPLIT_FILE_SEQUENTIAL_SIZE && freeSpace < restSize)))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
                goto out;
            }
            
            // Now check if the current PFS0 file entry offset is correct
            if (seqNspCtx.fileOffset >= nspPfs0EntryTable[seqNspCtx.fileIndex].file_size)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid offset for current PFS0 file entry in the sequential dump reference file!", __func__);
                goto out;
            }
            
            // Copy previously calculated NCA IDs and hashes
            for(i = 0; i < seqNspCtx.fileIndex; i++)
            {
                // Exit loop if we reach the CNMT NCA
                // Its ID/hash calculation is always handled by patchCnmtNca()
                if (i >= (titleContentInfoCnt - 1)) break;
                
                // Fill information for our CNMT XML
                memcpy(xml_content_info[i].nca_id, seqDumpNcaHashes + (i * SHA256_HASH_SIZE), SHA256_HASH_SIZE / 2);
                convertDataToHexString(xml_content_info[i].nca_id, SHA256_HASH_SIZE / 2, xml_content_info[i].nca_id_str, SHA256_HASH_SIZE + 1);
                memcpy(xml_content_info[i].hash, seqDumpNcaHashes + (i * SHA256_HASH_SIZE), SHA256_HASH_SIZE);
                convertDataToHexString(xml_content_info[i].hash, SHA256_HASH_SIZE, xml_content_info[i].hash_str, (SHA256_HASH_SIZE * 2) + 1);
            }
            
            // Copy the NCA SHA-256 context data, but only if we're not dealing with the CNMT NCA
            if (seqNspCtx.fileIndex < (titleContentInfoCnt - 1)) memcpy(&nca_hash_ctx, &(seqNspCtx.hashCtx), sizeof(Sha256Context));
            
            // Restore the modified Program NCA headers
            // The NPDM signature from the NCA headers is generated using cryptographically secure random numbers, so the modified header is stored during the first sequential dump session
            // If needed, it must be restored in later sessions
            for(i = 0; i < ncaProgramModCnt; i++)
            {
                read_res = fread(xml_content_info[ncaProgramMod[i].nca_index].encrypted_header_mod, 1, NCA_FULL_HEADER_LENGTH, seqDumpFile);
                if (read_res != NCA_FULL_HEADER_LENGTH)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes chunk from the sequential dump reference file! (read %lu bytes)", __func__, NCA_FULL_HEADER_LENGTH, read_res);
                    goto out;
                }
            }
            
            rewind(seqDumpFile);
            
            // Inform that we are resuming an already started sequential dump operation
            if (curStorageId == NcmStorageId_GameCard)
            {
                if (selectedNspDumpType == DUMP_APP_NSP)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation. Configuration parameters overrided.");
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Change NPDM RSA key/sig in Program NCA: %s.", (npdmAcidRsaPatch ? "Yes" : "No"));
                } else
                if (selectedNspDumpType == DUMP_PATCH_NSP)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation. Configuration parameters overrided.");
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Generate ticket-less dump: %s | Change NPDM RSA key/sig in Program NCA: %s.", (tiklessDump ? "Yes" : "No"), (npdmAcidRsaPatch ? "Yes" : "No"));
                } else
                if (selectedNspDumpType == DUMP_ADDON_NSP)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation.");
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Resuming previous sequential dump operation. Configuration parameters overrided.");
                breaks++;
                
                if (selectedNspDumpType == DUMP_APP_NSP || selectedNspDumpType == DUMP_PATCH_NSP)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Remove console specific data: %s | Generate ticket-less dump: %s | Change NPDM RSA key/sig in Program NCA: %s.", (removeConsoleData ? "Yes" : "No"), (tiklessDump ? "Yes" : "No"), (npdmAcidRsaPatch ? "Yes" : "No"));
                } else
                if (selectedNspDumpType == DUMP_ADDON_NSP)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Remove console specific data: %s | Generate ticket-less dump: %s.", (removeConsoleData ? "Yes" : "No"), (tiklessDump ? "Yes" : "No"));
                }
            }
            
            breaks++;
        } else {
            if (progressCtx.totalSize > freeSpace)
            {
                // Check if we have enough free space
                // The CNMT NCA is excluded from the hash list
                seqDumpFileSize = (sizeof(sequentialNspCtx) + ((titleContentInfoCnt - 1) * SHA256_HASH_SIZE) + (ncaProgramModCnt * NCA_FULL_HEADER_LENGTH));
                if (freeSpace < (SPLIT_FILE_SEQUENTIAL_SIZE + seqDumpFileSize))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
                    goto out;
                }
                
                // Ask the user if they want to use the sequential dump mode
                int cur_breaks = breaks;
                
                if (!yesNoPrompt("There's not enough space available to generate a whole dump in this session. Do you want to use sequential dumping?\nIn this mode, the selected content will be dumped in more than one session.\nYou'll have to transfer the generated part files to a PC before continuing the process in the next session."))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
                    goto out;
                }
                
                // Remove the prompt from the screen
                breaks = cur_breaks;
                uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
                uiRefreshDisplay();
                
                // Modify config parameters
                isFat32 = true;
                partSize = SPLIT_FILE_SEQUENTIAL_SIZE;
                seqDumpMode = true;
                
                // Fill information in our sequential context
                seqNspCtx.storageId = curStorageId;
                seqNspCtx.removeConsoleData = removeConsoleData;
                seqNspCtx.tiklessDump = tiklessDump;
                seqNspCtx.npdmAcidRsaPatch = npdmAcidRsaPatch;
                seqNspCtx.preInstall = preInstall;
                seqNspCtx.pfs0FileCount = nspPfs0Header.file_cnt;
                seqNspCtx.ncaCount = (titleContentInfoCnt - 1); // Exclude the CNMT NCA from the hash list
                seqNspCtx.programNcaModCount = ncaProgramModCnt;
                
                // Allocate memory for the NCA hashes
                seqDumpNcaHashes = calloc(1, (titleContentInfoCnt - 1) * SHA256_HASH_SIZE);
                if (!seqDumpNcaHashes)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA hashes from the sequential dump reference file!", __func__);
                    goto out;
                }
                
                // Create sequential reference file and keep the handle to it opened
                seqDumpFile = fopen(seqDumpFilename, "wb+");
                if (!seqDumpFile)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to create sequential dump reference file! (\"%s\")", __func__, seqDumpFilename);
                    goto out;
                }
                
                // Write the sequential dump struct
                write_res = fwrite(&seqNspCtx, 1, sizeof(sequentialNspCtx), seqDumpFile);
                if (write_res != sizeof(sequentialNspCtx))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", __func__, sizeof(sequentialNspCtx), write_res);
                    seqDumpFileRemove = true;
                    goto out;
                }
                
                // Write the NCA hashes block
                write_res = fwrite(seqDumpNcaHashes, 1, (titleContentInfoCnt - 1) * SHA256_HASH_SIZE, seqDumpFile);
                if (write_res != ((titleContentInfoCnt - 1) * SHA256_HASH_SIZE))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", __func__, (titleContentInfoCnt - 1) * SHA256_HASH_SIZE, write_res);
                    seqDumpFileRemove = true;
                    goto out;
                }
                
                // Write the modified Program NCA headers
                // The NPDM signature from the NCA headers is generated using cryptographically secure random numbers, so we must store the modified header during the first sequential dump session
                for(i = 0; i < ncaProgramModCnt; i++)
                {
                    write_res = fwrite(xml_content_info[ncaProgramMod[i].nca_index].encrypted_header_mod, 1, NCA_FULL_HEADER_LENGTH, seqDumpFile);
                    if (write_res != NCA_FULL_HEADER_LENGTH)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", __func__, NCA_FULL_HEADER_LENGTH, write_res);
                        seqDumpFileRemove = true;
                        goto out;
                    }
                }
                
                rewind(seqDumpFile);
                
                // Update free space
                freeSpace -= seqDumpFileSize;
            }
        }
    } else {
        if (progressCtx.totalSize > freeSpace)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
            goto out;
        }
    }
    
    if (seqDumpMode)
    {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp.%02u", NSP_DUMP_PATH, dumpName, splitIndex);
    } else {
        // Temporary, we'll use this to check if the dump already exists (it should have the archive bit set if so)
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
        
        // Check if the dump already exists
        if (!batch && checkIfFileExists(dumpPath))
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
                uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
            }
        }
        
        // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
        // Better safe than sorry
        remove(dumpPath);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, dumpPath);
        goto out;
    }
    
    // Start dump process
    if (!batch) dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    
    if (!batch)
    {
        breaks++;
        changeHomeButtonBlockStatus(true);
    }
    
    if (seqDumpMode)
    {
        // Skip the PFS0 header in the first part file
        // It will be saved to an additional ".nsp.hdr" file
        if (!seqNspCtx.partNumber) progressCtx.curOffset = seqDumpSessionOffset = fullPfs0HeaderSize;
    } else {
        // Write placeholder zeroes
        write_res = fwrite(dumpBuf, 1, fullPfs0HeaderSize, outFile);
        if (write_res != fullPfs0HeaderSize)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes placeholder data to file offset 0x%016lX! (wrote %lu bytes)", __func__, fullPfs0HeaderSize, (u64)0, write_res);
            goto out;
        }
        
        // Advance our current offset
        progressCtx.curOffset = fullPfs0HeaderSize;
    }
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    dumping = true;
    
    u32 startFileIndex = (seqDumpMode ? seqNspCtx.fileIndex : 0);
    u64 startFileOffset;
    
    // Write all PFS0 entries
    for(i = startFileIndex; i < nspPfs0Header.file_cnt; i++, startFileIndex++)
    {
        char *entryFilename = NULL;
        
        n = DUMP_BUFFER_SIZE;
        
        startFileOffset = ((seqDumpMode && i == seqNspCtx.fileIndex) ? seqNspCtx.fileOffset : 0);
        
        int programModIdx = -1;
        
        // Check if we're dealing with a NCA
        if (i < titleContentInfoCnt)
        {
            // Check if we're not dealing with the CNMT NCA
            if (i < (titleContentInfoCnt - 1))
            {
                // Copy NCA ID
                memcpy(ncaId.c, xml_content_info[i].nca_id, SHA256_HASH_SIZE / 2);
                
                // Reset SHA-256 context if necessary
                if (!seqDumpMode || (seqDumpMode && i != seqNspCtx.fileIndex)) sha256ContextCreate(&nca_hash_ctx);
                
                // Retrieve Program NCA mod data index
                if (xml_content_info[i].type == NcmContentType_Program && ncaProgramModCnt > 0)
                {
                    for(j = 0; j < ncaProgramModCnt; j++)
                    {
                        if (ncaProgramMod[j].nca_index == i)
                        {
                            programModIdx = (int)j;
                            break;
                        }
                    }
                }
            } else {
                // Patch CNMT NCA
                breaks = (progressCtx.line_offset + 2);
                
                proceed = patchCnmtNca(cnmtNcaBuf, xml_content_info[cnmtNcaIndex].size, &xml_program_info, xml_content_info, &ncaCnmtMod);
                if (!proceed)
                {
                    dumping = false;
                    break;
                }
                
                breaks = (progressCtx.line_offset - 4);
                
                // Fill PFS0 string table
                // This is done here because we'll need to display filenames for the rest of the PFS0 entries starting with the next loop iteration
                entryIdx = 0;
                
                for(j = 0; j < titleContentInfoCnt; j++, entryIdx++)
                {
                    char *curFilename = (nspPfs0StrTable + nspPfs0EntryTable[entryIdx].filename_offset);
                    sprintf(curFilename, "%s.%s", xml_content_info[j].nca_id_str, (j == cnmtNcaIndex ? "cnmt.nca" : "nca"));
                }
                
                if (includeTikAndCert)
                {
                    for(j = 0; j < 2; j++, entryIdx++)
                    {
                        char *curFilename = (nspPfs0StrTable + nspPfs0EntryTable[entryIdx].filename_offset);
                        sprintf(curFilename, (j == 0 ? rights_info.tik_filename : rights_info.cert_filename));
                    }
                }
            }
        } else {
            // Copy current filename
            entryFilename = (nspPfs0StrTable + nspPfs0EntryTable[i].filename_offset);
        }
        
        for(fileOffset = startFileOffset; fileOffset < nspPfs0EntryTable[i].file_size; fileOffset += n, progressCtx.curOffset += n, seqDumpSessionOffset += n)
        {
            if (seqDumpMode && seqDumpFinish)
            {
                ret = 0;
                break;
            }
            
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            
            if (i < titleContentInfoCnt)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Dumping NCA \"%s\" (%s)...", xml_content_info[i].nca_id_str, getContentType(xml_content_info[i].type));
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Writing \"%s\"...", entryFilename);
            }
            
            if (n > (nspPfs0EntryTable[i].file_size - fileOffset)) n = (nspPfs0EntryTable[i].file_size - fileOffset);
            
            // Check if the next read chunk will exceed the size of the current part file
            if (seqDumpMode && (seqDumpSessionOffset + n) >= (((splitIndex - seqNspCtx.partNumber) + 1) * partSize))
            {
                u64 new_file_chunk_size = ((seqDumpSessionOffset + n) - (((splitIndex - seqNspCtx.partNumber) + 1) * partSize));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                u64 remainderDumpSize = (progressCtx.totalSize - (progressCtx.curOffset + old_file_chunk_size));
                u64 remainderFreeSize = (freeSpace - (seqDumpSessionOffset + old_file_chunk_size));
                
                // Check if we have enough space for the next part
                // If so, set the chunk size to old_file_chunk_size
                if ((remainderDumpSize <= partSize && remainderDumpSize > remainderFreeSize) || (remainderDumpSize > partSize && partSize > remainderFreeSize))
                {
                    n = old_file_chunk_size;
                    seqDumpFinish = true;
                }
            }
            
            if (i < (titleContentInfoCnt - 1))
            {
                breaks = (progressCtx.line_offset + 2);
                
                proceed = readNcaDataByContentId(&ncmStorage, &ncaId, fileOffset, dumpBuf, n);
                if (!proceed)
                {
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes chunk at offset 0x%016lX from NCA \"%s\"!", __func__, n, fileOffset, xml_content_info[i].nca_id_str);
                    dumping = false;
                    break;
                }
                
                breaks = (progressCtx.line_offset - 4);
                
                // Replace NCA header with our modified one
                if (fileOffset < NCA_FULL_HEADER_LENGTH)
                {
                    u64 write_size = (NCA_FULL_HEADER_LENGTH - fileOffset);
                    if (write_size > n) write_size = n;
                    
                    memcpy(dumpBuf, xml_content_info[i].encrypted_header_mod + fileOffset, write_size);
                }
                
                // Replace modified Program NCA data blocks
                if (programModIdx != -1)
                {
                    u64 internal_block_offset;
                    u64 internal_block_chunk_size;
                    
                    u64 buffer_offset;
                    u64 buffer_chunk_size;
                    
                    if ((fileOffset + n) > ncaProgramMod[programModIdx].hash_table_offset && (ncaProgramMod[programModIdx].hash_table_offset + ncaProgramMod[programModIdx].hash_table_size) > fileOffset)
                    {
                        internal_block_offset = (fileOffset > ncaProgramMod[programModIdx].hash_table_offset ? (fileOffset - ncaProgramMod[programModIdx].hash_table_offset) : 0);
                        internal_block_chunk_size = (ncaProgramMod[programModIdx].hash_table_size - internal_block_offset);
                        
                        buffer_offset = (fileOffset > ncaProgramMod[programModIdx].hash_table_offset ? 0 : (ncaProgramMod[programModIdx].hash_table_offset - fileOffset));
                        buffer_chunk_size = ((n - buffer_offset) > internal_block_chunk_size ? internal_block_chunk_size : (n - buffer_offset));
                        
                        memcpy(dumpBuf + buffer_offset, ncaProgramMod[programModIdx].hash_table + internal_block_offset, buffer_chunk_size);
                    }
                    
                    if ((fileOffset + n) > ncaProgramMod[programModIdx].block_offset[0] && (ncaProgramMod[programModIdx].block_offset[0] + ncaProgramMod[programModIdx].block_size[0]) > fileOffset)
                    {
                        internal_block_offset = (fileOffset > ncaProgramMod[programModIdx].block_offset[0] ? (fileOffset - ncaProgramMod[programModIdx].block_offset[0]) : 0);
                        internal_block_chunk_size = (ncaProgramMod[programModIdx].block_size[0] - internal_block_offset);
                        
                        buffer_offset = (fileOffset > ncaProgramMod[programModIdx].block_offset[0] ? 0 : (ncaProgramMod[programModIdx].block_offset[0] - fileOffset));
                        buffer_chunk_size = ((n - buffer_offset) > internal_block_chunk_size ? internal_block_chunk_size : (n - buffer_offset));
                        
                        memcpy(dumpBuf + buffer_offset, ncaProgramMod[programModIdx].block_data[0] + internal_block_offset, buffer_chunk_size);
                    }
                    
                    if (ncaProgramMod[programModIdx].block_mod_cnt == 2 && (fileOffset + n) > ncaProgramMod[programModIdx].block_offset[1] && (ncaProgramMod[programModIdx].block_offset[1] + ncaProgramMod[programModIdx].block_size[1]) > fileOffset)
                    {
                        internal_block_offset = (fileOffset > ncaProgramMod[programModIdx].block_offset[1] ? (fileOffset - ncaProgramMod[programModIdx].block_offset[1]) : 0);
                        internal_block_chunk_size = (ncaProgramMod[programModIdx].block_size[1] - internal_block_offset);
                        
                        buffer_offset = (fileOffset > ncaProgramMod[programModIdx].block_offset[1] ? 0 : (ncaProgramMod[programModIdx].block_offset[1] - fileOffset));
                        buffer_chunk_size = ((n - buffer_offset) > internal_block_chunk_size ? internal_block_chunk_size : (n - buffer_offset));
                        
                        memcpy(dumpBuf + buffer_offset, ncaProgramMod[programModIdx].block_data[1] + internal_block_offset, buffer_chunk_size);
                    }
                }
                
                // Update SHA-256 calculation
                sha256ContextUpdate(&nca_hash_ctx, dumpBuf, n);
            } else {
                // Copy data using pointer array
                u32 ptrIdx = (i - (titleContentInfoCnt - 1));
                memcpy(dumpBuf, nspPfs0FilePtrs[ptrIdx] + fileOffset, n);
            }
            
            if ((seqDumpMode || (!seqDumpMode && progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)) && (progressCtx.curOffset + n) >= ((splitIndex + 1) * partSize))
            {
                u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * partSize));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (((seqDumpMode && !seqDumpFinish) || !seqDumpMode) && (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize))
                {
                    splitIndex++;
                    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp%c%02u", NSP_DUMP_PATH, dumpName, (seqDumpMode ? '.' : '/'), splitIndex);
                    
                    outFile = fopen(dumpPath, "wb");
                    if (!outFile)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, progressCtx.curOffset, write_res);
                    
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
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize && cancelProcessCheck(&progressCtx))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                ret = -2;
                proceed = false;
                break;
            }
        }
        
        if (!proceed || ret >= 0) break;
        
        // Support empty files
        if (!nspPfs0EntryTable[i].file_size)
        {
            uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
            
            if (i < titleContentInfoCnt)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Dumping NCA \"%s\" (%s)...", xml_content_info[i].nca_id_str, getContentType(xml_content_info[i].type));
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Writing \"%s\"...", entryFilename);
            }
            
            printProgressBar(&progressCtx, false, 0);
        }
        
        // Check if we're not dealing with the CNMT NCA
        if (i < (titleContentInfoCnt - 1))
        {
            // Update content info
            sha256ContextGetHash(&nca_hash_ctx, xml_content_info[i].hash);
            convertDataToHexString(xml_content_info[i].hash, SHA256_HASH_SIZE, xml_content_info[i].hash_str, (SHA256_HASH_SIZE * 2) + 1);
            memcpy(xml_content_info[i].nca_id, xml_content_info[i].hash, SHA256_HASH_SIZE / 2);
            convertDataToHexString(xml_content_info[i].nca_id, SHA256_HASH_SIZE / 2, xml_content_info[i].nca_id_str, SHA256_HASH_SIZE + 1);
            
            // If we're doing a sequential dump and we just finished dumping a NCA, copy its calculated hash
            if (seqDumpMode) memcpy(seqDumpNcaHashes + (i * SHA256_HASH_SIZE), xml_content_info[i].hash, SHA256_HASH_SIZE);
        }
    }
    
    if (!proceed || ret >= 0)
    {
        if (!proceed)
        {
            setProgressBarError(&progressCtx);
            if (seqDumpMode) seqDumpFileRemove = true;
        }
        
        goto out;
    }
    
    uiFill(0, ((progressCtx.line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 4), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Writing PFS0 header...");
    
    uiRefreshDisplay();
    
    // Write our full PFS0 header
    memcpy(dumpBuf, &nspPfs0Header, sizeof(pfs0_header));
    memcpy(dumpBuf + sizeof(pfs0_header), nspPfs0EntryTable, (u64)nspPfs0Header.file_cnt * sizeof(pfs0_file_entry));
    memcpy(dumpBuf + sizeof(pfs0_header) + ((u64)nspPfs0Header.file_cnt * sizeof(pfs0_file_entry)), nspPfs0StrTable, nspPfs0Header.str_table_size);
    
    if (seqDumpMode)
    {
        // Just in case
        remove(pfs0HeaderFilename);
        
        // Check if we have enough space for the header file
        u64 curFreeSpace = (freeSpace - seqDumpSessionOffset);
        if (!seqNspCtx.partNumber) curFreeSpace += fullPfs0HeaderSize; // The PFS0 header size is skipped during the first sequential dump session
        
        if (curFreeSpace < fullPfs0HeaderSize)
        {
            // Finish current sequential dump session
            seqDumpFinish = true;
            ret = 0;
            goto out;
        }
        
        pfs0HeaderFile = fopen(pfs0HeaderFilename, "wb");
        if (!pfs0HeaderFile)
        {
            setProgressBarError(&progressCtx);
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to create PFS0 header file!", __func__);
            seqDumpFileRemove = true;
            goto out;
        }
        
        write_res = fwrite(dumpBuf, 1, fullPfs0HeaderSize, pfs0HeaderFile);
        fclose(pfs0HeaderFile);
        
        if (write_res != fullPfs0HeaderSize)
        {
            setProgressBarError(&progressCtx);
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes PFS0 header file! (wrote %lu bytes)", __func__, fullPfs0HeaderSize, write_res);
            remove(pfs0HeaderFilename);
            seqDumpFileRemove = true;
            goto out;
        }
        
        // Update free space
        freeSpace -= fullPfs0HeaderSize;
    } else {
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            if (outFile)
            {
                fclose(outFile);
                outFile = NULL;
            }
            
            snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp/%02u", NSP_DUMP_PATH, dumpName, 0);
            
            outFile = fopen(dumpPath, "rb+");
            if (!outFile)
            {
                setProgressBarError(&progressCtx);
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to re-open output file for part #0!", __func__);
                goto out;
            }
        } else {
            rewind(outFile);
        }
        
        write_res = fwrite(dumpBuf, 1, fullPfs0HeaderSize, outFile);
        if (write_res != fullPfs0HeaderSize)
        {
            setProgressBarError(&progressCtx);
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes PFS0 header to file offset 0x%016lX! (wrote %lu bytes)", __func__, fullPfs0HeaderSize, (u64)0, write_res);
            goto out;
        }
    }
    
    dumping = false;
    
    breaks = (progressCtx.line_offset + 2);
    
    if (progressCtx.curOffset >= progressCtx.totalSize || (seqDumpMode && seqDumpFinish)) ret = 0;
    
    if (ret < 0)
    {
        setProgressBarError(&progressCtx);
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: underdump error! Wrote %lu bytes, expected %lu bytes.", __func__, progressCtx.curOffset, progressCtx.totalSize);
        if (seqDumpMode) seqDumpFileRemove = true;
        goto out;
    }
    
    // Set archive bit (only for FAT32)
    if (!seqDumpMode && progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
        result = fsdevSetConcatenationFileAttribute(dumpPath);
        if (R_FAILED(result)) 
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Warning: failed to set archive bit on output directory! (0x%08X)", result);
            breaks += 2;
        }
    }
    
out:
    if (outFile) fclose(outFile);
    
    if (ret >= 0)
    {
        if (seqDumpMode)
        {
            if (seqDumpFinish)
            {
                // Update line count
                breaks = (progressCtx.line_offset + 2);
                
                // Update the sequence reference file
                seqNspCtx.partNumber = (splitIndex + 1);
                seqNspCtx.fileIndex = startFileIndex;
                seqNspCtx.fileOffset = fileOffset;
                
                // Copy the SHA-256 context data, but only if we're not dealing with the CNMT NCA
                // NCA ID/hash for the CNMT NCA is handled in patchCnmtNca()
                if (seqNspCtx.fileIndex < titleContentInfoCnt && seqNspCtx.fileIndex != cnmtNcaIndex)
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
                    write_res = fwrite(seqDumpNcaHashes, 1, seqNspCtx.ncaCount * SHA256_HASH_SIZE, seqDumpFile);
                    if (write_res != (seqNspCtx.ncaCount * SHA256_HASH_SIZE))
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", __func__, seqNspCtx.ncaCount * SHA256_HASH_SIZE, write_res);
                        ret = -1;
                        seqDumpFileRemove = true;
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk to the sequential dump reference file! (wrote %lu bytes)", __func__, sizeof(sequentialNspCtx), write_res);
                    ret = -1;
                    seqDumpFileRemove = true;
                }
            } else {
                // Mark the file for deletion
                seqDumpFileRemove = true;
            }
        }
        
        if (ret >= 0 && !batch)
        {
            timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
            progressCtx.now -= progressCtx.start;
            
            if (!seqDumpMode || (seqDumpMode && !seqDumpFinish))
            {
                progressCtx.progress = 100;
                progressCtx.remainingTime = 0;
            }
            
            printProgressBar(&progressCtx, false, 0);
            
            formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
            uiRefreshDisplay();
            
            // Only perform the checksum lookup if we have finished the dump process
            if (useNoIntroLookup && (!seqDumpMode || (seqDumpMode && !seqDumpFinish)))
            {
                if (curStorageId != NcmStorageId_GameCard && !tiklessDump)
                {
                    // Calculate CRC32 checksum for the CNMT NCA
                    crc32(cnmtNcaBuf, xml_content_info[cnmtNcaIndex].size, &crc);
                    
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "CNMT NCA CRC32 checksum: %08X.", crc);
                    uiRefreshDisplay();
                    breaks++;
                    
                    // Perform checksum lookup
                    noIntroDumpCheck(true, crc);
                } else {
                    if (curStorageId != NcmStorageId_GameCard && tiklessDump)
                    {
                        breaks++;
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dump verification disabled (not compatible with NSP dumps with modified NCAs).");
                    }
                }
            }
            
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
                    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp.%02u", NSP_DUMP_PATH, dumpName, i);
                    remove(dumpPath);
                }
            } else {
                snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                
                if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
                {
                    fsdevDeleteDirectoryRecursively(dumpPath);
                } else {
                    remove(dumpPath);
                }
            }
        }
    }
    
    if (nspPfs0FilePtrs) free(nspPfs0FilePtrs);
    
    if (nspPfs0StrTable) free(nspPfs0StrTable);
    
    if (nspPfs0EntryTable) free(nspPfs0EntryTable);
    
    if (cnmtNcaBuf) free(cnmtNcaBuf);
    
    if (ncaProgramMod)
    {
        for(i = 0; i < ncaProgramModCnt; i++)
        {
            if (ncaProgramMod[i].hash_table) free(ncaProgramMod[i].hash_table);
            if (ncaProgramMod[i].block_data[0]) free(ncaProgramMod[i].block_data[0]);
            if (ncaProgramMod[i].block_data[1]) free(ncaProgramMod[i].block_data[1]);
        }
        
        free(ncaProgramMod);
    }
    
    if (xml_content_info) free(xml_content_info);
    
    ncmContentStorageClose(&ncmStorage);
    
    if (curStorageId == NcmStorageId_GameCard) closeGameCardStoragePartition();
    
    if (titleContentInfos) free(titleContentInfos);
    
    if (seqDumpNcaHashes) free(seqDumpNcaHashes);
    
    if (seqDumpFile) fclose(seqDumpFile);
    
    if (seqDumpFileRemove) remove(seqDumpFilename);
    
    if (dumpName) free(dumpName);
    
    if (!batch) changeHomeButtonBlockStatus(false);
    
    return ret;
}

int batchEntryCmp(const void *a, const void *b)
{
	batchEntry *batchEntry1 = (batchEntry*)a;
	batchEntry *batchEntry2 = (batchEntry*)b;
	
	return strcasecmp(batchEntry1->nspFilename, batchEntry2->nspFilename);
}

int dumpNintendoSubmissionPackageBatch(batchOptions *batchDumpCfg)
{
    int ret = -1;
    
    if (!batchDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid batch dump configuration struct!", __func__);
        breaks += 2;
        return ret;
    }
    
    bool dumpAppTitles = batchDumpCfg->dumpAppTitles;
    bool dumpPatchTitles = batchDumpCfg->dumpPatchTitles;
    bool dumpAddOnTitles = batchDumpCfg->dumpAddOnTitles;
    bool isFat32 = batchDumpCfg->isFat32;
    bool removeConsoleData = batchDumpCfg->removeConsoleData;
    bool tiklessDump = batchDumpCfg->tiklessDump;
    bool npdmAcidRsaPatch = batchDumpCfg->npdmAcidRsaPatch;
    bool dumpDeltaFragments = batchDumpCfg->dumpDeltaFragments;
    bool skipDumpedTitles = batchDumpCfg->skipDumpedTitles;
    bool rememberDumpedTitles = batchDumpCfg->rememberDumpedTitles;
    bool haltOnErrors = batchDumpCfg->haltOnErrors;
    bool useBrackets = batchDumpCfg->useBrackets;
    batchModeSourceStorage batchModeSrc = batchDumpCfg->batchModeSrc;
    
    if ((!dumpAppTitles && !dumpPatchTitles && !dumpAddOnTitles) || (batchModeSrc == BATCH_SOURCE_ALL && ((dumpAppTitles && !titleAppCount) || (dumpPatchTitles && !titlePatchCount) || (dumpAddOnTitles && !titleAddOnCount))) || (batchModeSrc == BATCH_SOURCE_SDCARD && ((dumpAppTitles && !sdCardTitleAppCount) || (dumpPatchTitles && !sdCardTitlePatchCount) || (dumpAddOnTitles && !sdCardTitleAddOnCount))) || (batchModeSrc == BATCH_SOURCE_EMMC && ((dumpAppTitles && !emmcTitleAppCount) || (dumpPatchTitles && !emmcTitlePatchCount) || (dumpAddOnTitles && !emmcTitleAddOnCount))) || batchModeSrc >= BATCH_SOURCE_CNT)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to perform batch NSP dump!", __func__);
        breaks += 2;
        return ret;
    }
    
    u32 i, j;
    
    u32 totalTitleCount = 0, totalAppCount = 0, totalPatchCount = 0, totalAddOnCount = 0;
    
    u32 titleCount = 0, titleIndex = 0;
    
    char *dumpName = NULL;
    char summary_str[128] = {'\0'};
    
    int initial_breaks = breaks, cur_breaks;
    
    const u32 maxSummaryFileCount = 8;
    u32 summaryPage = 0, selectedSummaryEntry = 0;
    u32 xpos = 0, ypos = 0;
    u64 keysDown = 0, keysHeld = 0;
    
    u32 maxEntryCount = 0, batchEntryIndex = 0, disabledEntryCount = 0;
    batchEntry *batchEntries = NULL, *tmpBatchEntries = NULL;
    
    bool proceed = true;
    
    // Generate NSP configuration struct
    nspOptions nspDumpCfg;
    
    nspDumpCfg.isFat32 = isFat32;
    nspDumpCfg.useNoIntroLookup = false;
    nspDumpCfg.removeConsoleData = removeConsoleData;
    nspDumpCfg.tiklessDump = tiklessDump;
    nspDumpCfg.npdmAcidRsaPatch = npdmAcidRsaPatch;
    nspDumpCfg.dumpDeltaFragments = dumpDeltaFragments;
    nspDumpCfg.useBrackets = useBrackets;
    
    // Allocate memory for the batch entries
    if (dumpAppTitles) maxEntryCount += (batchModeSrc == BATCH_SOURCE_ALL ? titleAppCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAppCount : emmcTitleAppCount));
    if (dumpPatchTitles) maxEntryCount += (batchModeSrc == BATCH_SOURCE_ALL ? titlePatchCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitlePatchCount : emmcTitlePatchCount));
    if (dumpAddOnTitles) maxEntryCount += (batchModeSrc == BATCH_SOURCE_ALL ? titleAddOnCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAddOnCount : emmcTitleAddOnCount));
    
    batchEntries = calloc(maxEntryCount, sizeof(batchEntry));
    if (!batchEntries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for batch entries!", __func__);
        breaks += 2;
        return ret;
    }
    
    for(i = 0; i < 3; i++)
    {
        if ((i == 0 && !dumpAppTitles) || (i == 1 && !dumpPatchTitles) || (i == 2 && !dumpAddOnTitles)) continue;
        
        u32 emmcRefTitleCount = 0;
        nspDumpType curNspDumpType = DUMP_APP_NSP;
        
        switch(i)
        {
            case 0:
                titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titleAppCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAppCount : emmcTitleAppCount));
                emmcRefTitleCount = sdCardTitleAppCount;
                curNspDumpType = DUMP_APP_NSP;
                break;
            case 1:
                titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titlePatchCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitlePatchCount : emmcTitlePatchCount));
                emmcRefTitleCount = sdCardTitlePatchCount;
                curNspDumpType = DUMP_PATCH_NSP;
                break;
            case 2:
                titleCount = (batchModeSrc == BATCH_SOURCE_ALL ? titleAddOnCount : (batchModeSrc == BATCH_SOURCE_SDCARD ? sdCardTitleAddOnCount : emmcTitleAddOnCount));
                emmcRefTitleCount = sdCardTitleAddOnCount;
                curNspDumpType = DUMP_ADDON_NSP;
                break;
            default:
                break;
        }
        
        for(j = 0; j < titleCount; j++)
        {
            titleIndex = ((batchModeSrc == BATCH_SOURCE_ALL || batchModeSrc == BATCH_SOURCE_SDCARD) ? j : (j + emmcRefTitleCount));
            
            dumpName = generateNSPDumpName(curNspDumpType, titleIndex, false);
            if (!dumpName)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
                breaks += 2;
                goto out;
            }
            
            // Check if an override file already exists for this dump
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s.nsp", BATCH_OVERRIDES_PATH, dumpName);
            
            if (checkIfFileExists(strbuf))
            {
                free(dumpName);
                dumpName = NULL;
                continue;
            }
            
            snprintf(batchEntries[batchEntryIndex].nspFilename, MAX_CHARACTERS(batchEntries[batchEntryIndex].nspFilename), strrchr(strbuf, '/') + 1);
            snprintf(batchEntries[batchEntryIndex].truncatedNspFilename, MAX_CHARACTERS(batchEntries[batchEntryIndex].truncatedNspFilename), batchEntries[batchEntryIndex].nspFilename);
            
            if (useBrackets)
            {
                // Generate output name with brackets
                free(dumpName);
                dumpName = NULL;
                
                dumpName = generateNSPDumpName(curNspDumpType, titleIndex, true);
                if (!dumpName)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name with brackets!", __func__);
                    breaks += 2;
                    goto out;
                }
            }
            
            // Check if this title has already been dumped
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
            
            free(dumpName);
            dumpName = NULL;
            
            if (skipDumpedTitles && checkIfFileExists(strbuf)) continue;
            
            // Save title properties
            batchEntries[batchEntryIndex].enabled = true;
            batchEntries[batchEntryIndex].titleType = curNspDumpType;
            batchEntries[batchEntryIndex].titleIndex = titleIndex;
            batchEntries[batchEntryIndex].contentSize = (i == 0 ? baseAppEntries[titleIndex].contentSize : (i == 1 ? patchEntries[titleIndex].contentSize : addOnEntries[titleIndex].contentSize));
            batchEntries[batchEntryIndex].contentSizeStr = (i == 0 ? baseAppEntries[titleIndex].contentSizeStr : (i == 1 ? patchEntries[titleIndex].contentSizeStr : addOnEntries[titleIndex].contentSizeStr));
            
            // Fix entry name length
            truncateBrowserEntryName(batchEntries[batchEntryIndex].truncatedNspFilename);
            
            // Increase batch entry index
            batchEntryIndex++;
            
            // Increase total base application count
            if (i == 0) totalAppCount++;
            
            // Increase total patch count
            if (i == 1) totalPatchCount++;
            
            // Increase total addon count
            if (i == 2) totalAddOnCount++;
        }
    }
    
    // Calculate total title count
    totalTitleCount = (totalAppCount + totalPatchCount + totalAddOnCount);
    if (!totalTitleCount)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "You have already dumped all titles matching the selected settings!");
        breaks += 2;
        goto out;
    }
    
    // Sort batch entries by name
    qsort(batchEntries, totalTitleCount, sizeof(batchEntry), batchEntryCmp);
    
    if (totalTitleCount < maxEntryCount)
    {
        tmpBatchEntries = realloc(batchEntries, totalTitleCount * sizeof(batchEntry));
        if (!tmpBatchEntries)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "%s: failed to reallocate batch entries!", __func__);
            breaks += 2;
            goto out;
        }
        
        batchEntries = tmpBatchEntries;
        tmpBatchEntries = NULL;
    }
    
    // Display summary controls
    if (totalTitleCount > maxSummaryFileCount)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_ZL " / " NINTENDO_FONT_ZR " ] Change page | [ " NINTENDO_FONT_A " ] Proceed | [ " NINTENDO_FONT_B " ] Cancel | [ " NINTENDO_FONT_Y " ] Toggle selected entry | [ "  NINTENDO_FONT_L " ] Disable all entries | [ " NINTENDO_FONT_R " ] Enable all entries\n[ " NINTENDO_FONT_PLUS " ] Exit");
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Proceed | [ " NINTENDO_FONT_B " ] Cancel | [ " NINTENDO_FONT_Y " ] Toggle selected entry | [ "  NINTENDO_FONT_L " ] Disable all entries | [ " NINTENDO_FONT_R " ] Enable all entries | [ " NINTENDO_FONT_PLUS " ] Exit");
    }
    
    breaks += 2;
    
    // Display free space
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Free SD card space: %s (%lu bytes).", freeSpaceStr, freeSpace);
    breaks += 2;
    
    // Display summary
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Summary:");
    breaks += 2;
    
    if (totalAppCount)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "BASE: %u", totalAppCount);
        strcat(summary_str, strbuf);
    }
    
    if (totalPatchCount)
    {
        if (totalAppCount) strcat(summary_str, " | ");
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "UPD: %u", totalPatchCount);
        strcat(summary_str, strbuf);
    }
    
    if (totalAddOnCount)
    {
        if (totalAppCount || totalPatchCount) strcat(summary_str, " | ");
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "DLC: %u", totalAddOnCount);
        strcat(summary_str, strbuf);
    }
    
    strcat(summary_str, " | ");
    snprintf(strbuf, MAX_CHARACTERS(strbuf), "Total: %u", totalTitleCount);
    strcat(summary_str, strbuf);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, summary_str);
    breaks++;
    
    while(true)
    {
        cur_breaks = breaks;
        
        uiFill(0, 8 + (cur_breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (cur_breaks * LINE_HEIGHT)), BG_COLOR_RGB);
        
        // Calculate the number of selected titles
        j = 0;
        u64 totalOutSize = 0;
        char totalOutSizeStr[32] = {'\0'};
        
        for(i = 0; i < totalTitleCount; i++)
        {
            if (batchEntries[i].enabled)
            {
                j++;
                totalOutSize += batchEntries[i].contentSize;
            }
        }
        
        convertSize(totalOutSize, totalOutSizeStr, MAX_CHARACTERS(totalOutSizeStr));
        
        if (totalTitleCount > maxSummaryFileCount)
        {
            if (j && totalOutSize)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(cur_breaks), FONT_COLOR_RGB, "Current page: %u | Selected titles: %u | Approximate total dump size: %s (%lu bytes)", summaryPage + 1, j, totalOutSizeStr, totalOutSize);
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(cur_breaks), FONT_COLOR_RGB, "Current page: %u | Selected titles: %u", summaryPage + 1, j);
            }
        } else {
            if (j && totalOutSize)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(cur_breaks), FONT_COLOR_RGB, "Selected titles: %u | Approximate total dump size: %s (%lu bytes)", j, totalOutSizeStr, totalOutSize);
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(cur_breaks), FONT_COLOR_RGB, "Selected titles: %u", j);
            }
        }
        
        cur_breaks += 2;
        
        j = 0;
        highlight = false;
        
        for(i = (summaryPage * maxSummaryFileCount); i < ((summaryPage + 1) * maxSummaryFileCount); i++, j++)
        {
            if (i >= totalTitleCount) break;
            
            xpos = STRING_X_POS;
            ypos = (8 + (cur_breaks * LINE_HEIGHT) + (j * (font_height + 12)) + 6);
            
            if (i == selectedSummaryEntry)
            {
                highlight = true;
                uiFill(0, ypos - 6, FB_WIDTH, font_height + 12, HIGHLIGHT_BG_COLOR_RGB);
            }
            
            uiDrawIcon((highlight ? (batchEntries[i].enabled ? enabledHighlightIconBuf : disabledHighlightIconBuf) : (batchEntries[i].enabled ? enabledNormalIconBuf : disabledNormalIconBuf)), BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, xpos, ypos);
            xpos += (BROWSER_ICON_DIMENSION + 8);
            
            if (highlight)
            {
                uiDrawString(xpos, ypos, HIGHLIGHT_FONT_COLOR_RGB, batchEntries[i].truncatedNspFilename);
                
                if (batchEntries[i].contentSize)
                {
                    snprintf(strbuf, MAX_CHARACTERS(strbuf), "(%s)", batchEntries[i].contentSizeStr);
                    uiDrawString(FB_WIDTH - (8 + uiGetStrWidth(strbuf)), ypos, HIGHLIGHT_FONT_COLOR_RGB, strbuf);
                }
            } else {
                uiDrawString(xpos, ypos, FONT_COLOR_RGB, batchEntries[i].truncatedNspFilename);
                
                if (batchEntries[i].contentSize)
                {
                    snprintf(strbuf, MAX_CHARACTERS(strbuf), "(%s)", batchEntries[i].contentSizeStr);
                    uiDrawString(FB_WIDTH - (8 + uiGetStrWidth(strbuf)), ypos, FONT_COLOR_RGB, strbuf);
                }
            }
            
            if (i == selectedSummaryEntry) highlight = false;
        }
        
        while(true)
        {
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            scanPads();
            
            keysDown = getButtonsDown();
            keysHeld = getButtonsHeld();
            
            if (keysDown || keysHeld) break;
        }
        
        // Exit
        if (keysDown & HidNpadButton_Plus)
        {
            ret = -2;
            proceed = false;
            break;
        }
        
        // Start batch dump process
        if (keysDown & HidNpadButton_A)
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
        if (keysDown & HidNpadButton_B)
        {
            proceed = false;
            break;
        }
        
        // Toggle selected entry
        if (keysDown & HidNpadButton_Y) batchEntries[selectedSummaryEntry].enabled ^= 0x01;
        
        // Disable all entries
        if (keysDown & HidNpadButton_L)
        {
            for(i = 0; i < totalTitleCount; i++) batchEntries[i].enabled = false;
        }
        
        // Enable all entries
        if (keysDown & HidNpadButton_R)
        {
            for(i = 0; i < totalTitleCount; i++) batchEntries[i].enabled = true;
        }
        
        // Change page (left)
        if ((keysDown & HidNpadButton_ZL) && totalTitleCount > maxSummaryFileCount)
        {
            if (summaryPage > 0)
            {
                summaryPage--;
                selectedSummaryEntry = (summaryPage * maxSummaryFileCount);
            }
        }
        
        // Change page (right)
        if ((keysDown & HidNpadButton_ZR) && totalTitleCount > maxSummaryFileCount)
        {
            if (((summaryPage + 1) * maxSummaryFileCount) < totalTitleCount)
            {
                summaryPage++;
                selectedSummaryEntry = (summaryPage * maxSummaryFileCount);
            }
        }
        
        // Go up
        if ((keysDown & HidNpadButton_Up) || (keysDown & HidNpadButton_StickLUp) || (keysHeld & HidNpadButton_StickRUp))
        {
            if (selectedSummaryEntry > (summaryPage * maxSummaryFileCount))
            {
                selectedSummaryEntry--;
            } else {
                if ((keysDown & HidNpadButton_Up) || (keysDown & HidNpadButton_StickLUp))
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
        if ((keysDown & HidNpadButton_Down) || (keysDown & HidNpadButton_StickLDown) || (keysHeld & HidNpadButton_StickRDown))
        {
            if (((((summaryPage + 1) * maxSummaryFileCount) < totalTitleCount) && selectedSummaryEntry < (((summaryPage + 1) * maxSummaryFileCount) - 1)) || ((((summaryPage + 1) * maxSummaryFileCount) >= totalTitleCount) && selectedSummaryEntry < (totalTitleCount - 1)))
            {
                selectedSummaryEntry++;
            } else {
                if ((keysDown & HidNpadButton_Down) || (keysDown & HidNpadButton_StickLDown))
                {
                    selectedSummaryEntry = (summaryPage * maxSummaryFileCount);
                }
            }
        }
    }
    
    uiClearStatusMsg();
    
    breaks = initial_breaks;
    uiFill(0, 8 + (breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (breaks * LINE_HEIGHT)), BG_COLOR_RGB);
    
    if (!proceed)
    {
        if (ret != -2)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled");
            breaks += 2;
        }
        
        goto out;
    }
    
    // Calculate the disabled entry count
    for(i = 0; i < totalTitleCount; i++)
    {
        if (!batchEntries[i].enabled) disabledEntryCount++;
    }
    
    // Start dump process
    dumpStartMsg();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    initial_breaks = breaks;
    
    j = 0;
    
    for(i = 0; i < totalTitleCount; i++)
    {
        if (!batchEntries[i].enabled) continue;
        
        breaks = initial_breaks;
        
        uiFill(0, 8 + (breaks * LINE_HEIGHT), FB_WIDTH, FB_HEIGHT - (8 + (breaks * LINE_HEIGHT)), BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Title: %.*s [%u / %u].", strlen(batchEntries[i].nspFilename) - 4, batchEntries[i].nspFilename, j + 1, totalTitleCount - disabledEntryCount);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Free SD card space: %s (%lu bytes).", freeSpaceStr, freeSpace);
        breaks++;
        
        uiRefreshDisplay();
        
        // Dump title
        int nspRet = dumpNintendoSubmissionPackage(batchEntries[i].titleType, batchEntries[i].titleIndex, &nspDumpCfg, true);
        if (nspRet >= 0)
        {
            // Create override file if necessary
            if (rememberDumpedTitles)
            {
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s", BATCH_OVERRIDES_PATH, batchEntries[i].nspFilename);
                FILE *overrideFile = fopen(strbuf, "wb");
                if (overrideFile) fclose(overrideFile);
            }
        } else {
            // If "Halt dump process on errors" is disabled, just wait a little bit and keep going (unless the process was truly canceled by the user)
            if (!haltOnErrors && nspRet != -2)
            {
                delay(5);
            } else {
                goto out;
            }
        }
        
        // Update free space
        updateFreeSpace();
        
        j++;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed!");
    breaks += 2;
    
    ret = 0;
    
out:
    if (batchEntries) free(batchEntries);
    
    changeHomeButtonBlockStatus(false);
    
    return ret;
}

bool dumpRawHfs0Partition(u32 partition, bool doSplitting)
{
    if (!gameCardInfo.rootHfs0Header || !gameCardInfo.hfs0PartitionCnt || partition >= gameCardInfo.hfs0PartitionCnt || !gameCardInfo.hfs0Partitions || !gameCardInfo.hfs0Partitions[partition].size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to dump raw HFS0 partition!", __func__);
        breaks += 2;
        return false;
    }
    
    Result result;
    u64 partitionOffset;
    bool success = false, fat32_error = false;
    u64 n = DUMP_BUFFER_SIZE;
    char dumpPath[NAME_BUF_LEN] = {'\0'};
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    openIStoragePartition storageIndex;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    size_t write_res;
    
    char *dumpName = generateGameCardDumpName(false);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
        breaks += 2;
        return false;
    }
    
    // The IStorage instance returned for partition == 0 contains the gamecard header, the gamecard certificate, the root HFS0 header and:
    // * The "update" (0) partition and the "normal" (1) partition (for gamecard type 0x01)
    // * The "update" (0) partition, the "logo" (1) partition and the "normal" (2) partition (for gamecard type 0x02)
    // The IStorage instance returned for partition == 1 contains the "secure" partition (which can either be 2 or 3 depending on the gamecard type)
    // This makes sure we just dump the *actual* raw HFS0 partition, without preceding data, padding, etc.
    // Oddly enough, IFileSystem instances actually point to the specified partition ID filesystem. I don't understand why it doesn't work like that for IStorage, but whatever
    // NOTE: Using partition == 2 returns error 0x149002, and using higher values probably do so, too
    partitionOffset = gameCardInfo.hfs0Partitions[partition].offset; // Relative to IStorage instance
    progressCtx.totalSize = gameCardInfo.hfs0Partitions[partition].size;
    storageIndex = (openIStoragePartition)(HFS0_TO_ISTORAGE_IDX(gameCardInfo.hfs0PartitionCnt, partition) + 1);
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "HFS0 partition size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks += 2;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
    {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, partition), splitIndex);
    } else {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s - Partition %u (%s).hfs0", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, partition));
    }
    
    // Check if the dump already exists
    if (checkIfFileExists(dumpPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        
        if (!yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?"))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        }
    }
    
    result = openGameCardStoragePartition(storageIndex);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open IStorage partition #%u! (0x%08X)", __func__, storageIndex - 1, result);
        goto out;
    }
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, dumpPath);
        goto out;
    }
    
    // Start dump process
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    progressCtx.line_offset = (breaks + 2);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    for (progressCtx.curOffset = 0; progressCtx.curOffset < progressCtx.totalSize; progressCtx.curOffset += n)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
        
        if (n > (progressCtx.totalSize - progressCtx.curOffset)) n = (progressCtx.totalSize - progressCtx.curOffset);
        
        result = readGameCardStoragePartition(partitionOffset + progressCtx.curOffset, dumpBuf, n);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes chunk at offset 0x%016lX from IStorage partition #%u! (0x%08X)", __func__, n, partitionOffset + progressCtx.curOffset, storageIndex - 1, result);
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                    break;
                }
            }
            
            fclose(outFile);
            outFile = NULL;
            
            if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                splitIndex++;
                snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, partition), splitIndex);
                
                outFile = fopen(dumpPath, "wb");
                if (!outFile)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(dumpBuf, 1, n, outFile);
            if (write_res != n)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, progressCtx.curOffset, write_res);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(&progressCtx, true, n);
        
        if ((progressCtx.curOffset + n) < progressCtx.totalSize && cancelProcessCheck(&progressCtx))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
            break;
        }
    }
    
    if (progressCtx.curOffset >= progressCtx.totalSize) success = true;
    
    // Support empty files
    if (!progressCtx.totalSize)
    {
        uiFill(0, ((progressCtx.line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dumpPath, '/' ) + 1);
        
        progressCtx.progress = 100;
        
        printProgressBar(&progressCtx, false, 0);
    }
    
    breaks = (progressCtx.line_offset + 2);
    
    if (success)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        if (fat32_error) breaks += 2;
    }
    
out:
    if (outFile) fclose(outFile);
    
    if (!success)
    {
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && doSplitting)
        {
            for(u8 i = 0; i <= splitIndex; i++)
            {
                snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s - Partition %u (%s).hfs0.%02u", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, partition), i);
                remove(dumpPath);
            }
        } else {
            remove(dumpPath);
        }
    }
    
    closeGameCardStoragePartition();
    
    if (dumpName) free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool copyFileFromHfs0Partition(u32 partition, const char *dest, const char *source, const u64 fileOffset, const u64 fileSize, progress_ctx_t *progressCtx, bool doSplitting)
{
    if (!gameCardInfo.rootHfs0Header || !gameCardInfo.hfs0PartitionCnt || partition >= gameCardInfo.hfs0PartitionCnt || !gameCardInfo.hfs0Partitions || !gameCardInfo.hfs0Partitions[partition].header || !gameCardInfo.hfs0Partitions[partition].header_size || !dest || !strlen(dest) || !source || !strlen(source) || !progressCtx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to copy file from HFS0 partition!", __func__);
        return false;
    }
    
    if (!gameCardInfo.hfs0Partitions[partition].file_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: the selected HFS0 partition is empty!", __func__);
        return false;
    }
    
    // IStorage handle must have been retrieved beforehand by the caller function
    
    Result result;
    bool success = false, fat32_error = false;
    char splitFilename[NAME_BUF_LEN * 3] = {'\0'};
    size_t destLen = strlen(dest);
    FILE *outFile = NULL;
    u64 off, n = DUMP_BUFFER_SIZE;
    u8 splitIndex = 0;
    openIStoragePartition storageIndex = (openIStoragePartition)(HFS0_TO_ISTORAGE_IDX(gameCardInfo.hfs0PartitionCnt, partition) + 1);
    
    size_t write_res;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    uiFill(0, ((progressCtx->line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 4), FONT_COLOR_RGB, "Copying \"%s\"...", source);
    
    if ((destLen + 1) >= MAX_CHARACTERS(splitFilename))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: destination path is too long! (%lu bytes)", __func__, destLen);
        return false;
    }
    
    if (fileSize > FAT32_FILESIZE_LIMIT && doSplitting) snprintf(splitFilename, MAX_CHARACTERS(splitFilename), "%s.%02u", dest, splitIndex);
    
    outFile = fopen(((fileSize > FAT32_FILESIZE_LIMIT && doSplitting) ? splitFilename : dest), "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file!", __func__);
        goto out;
    }
    
    for (off = 0; off < fileSize; off += n, progressCtx->curOffset += n)
    {
        uiFill(0, ((progressCtx->line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", ((fileSize > FAT32_FILESIZE_LIMIT && doSplitting) ? (strrchr(splitFilename, '/') + 1) : (strrchr(dest, '/') + 1)));
        
        uiRefreshDisplay();
        
        if (n > (fileSize - off)) n = (fileSize - off);
        
        result = readGameCardStoragePartition(fileOffset + off, dumpBuf, n);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes chunk at offset 0x%016lX from IStorage partition #%u! (0x%08X)", __func__, n, fileOffset + off, storageIndex - 1, result);
            break;
        }
        
        if (fileSize > FAT32_FILESIZE_LIMIT && doSplitting && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
        {
            u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
            u64 old_file_chunk_size = (n - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, off, splitIndex, write_res);
                    break;
                }
            }
            
            fclose(outFile);
            outFile = NULL;
            
            if (new_file_chunk_size > 0 || (off + n) < fileSize)
            {
                splitIndex++;
                snprintf(splitFilename, MAX_CHARACTERS(splitFilename), "%s.%02u", dest, splitIndex);
                
                outFile = fopen(splitFilename, "wb");
                if (!outFile)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(dumpBuf, 1, n, outFile);
            if (write_res != n)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, off, write_res);
                
                if ((off + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(progressCtx, true, n);
        
        if (((off + n) < fileSize || (progressCtx->curOffset + n) < progressCtx->totalSize) && cancelProcessCheck(progressCtx))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
            break;
        }
    }
    
    if (off >= fileSize) success = true;
    
    // Support empty files
    if (!fileSize)
    {
        uiFill(0, ((progressCtx->line_offset - 2) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 2, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 2), FONT_COLOR_RGB, "Output file: \"%s\".", strrchr(dest, '/') + 1);
        
        if (progressCtx->totalSize == fileSize) progressCtx->progress = 100;
        
        printProgressBar(progressCtx, false, 0);
    }
    
    if (!success)
    {
        setProgressBarError(progressCtx);
        breaks = (progressCtx->line_offset + 2);
        if (fat32_error) breaks += 2;
    }
    
out:
    if (outFile) fclose(outFile);
    
    if (!success)
    {
        if (fileSize > FAT32_FILESIZE_LIMIT && doSplitting)
        {
            for(u8 i = 0; i <= splitIndex; i++)
            {
                snprintf(splitFilename, MAX_CHARACTERS(splitFilename), "%s.%02u", dest, i);
                remove(splitFilename);
            }
        } else {
            remove(dest);
        }
    }
    
    breaks += 2;
    
    return success;
}

bool copyHfs0PartitionContents(u32 partition, progress_ctx_t *progressCtx, const char *dest, bool splitting)
{
    if (!gameCardInfo.rootHfs0Header || !gameCardInfo.hfs0PartitionCnt || partition >= gameCardInfo.hfs0PartitionCnt || !gameCardInfo.hfs0Partitions || !gameCardInfo.hfs0Partitions[partition].header || !gameCardInfo.hfs0Partitions[partition].header_size || !progressCtx || !dest || !strlen(dest))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to copy HFS0 partition contents!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!gameCardInfo.hfs0Partitions[partition].file_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: the selected HFS0 partition is empty!", __func__);
        breaks += 2;
        return false;
    }
    
    Result result;
    char dbuf[NAME_BUF_LEN * 2] = {'\0'};
    hfs0_file_entry entry;
    size_t dest_len = strlen(dest);
    openIStoragePartition storageIndex = (openIStoragePartition)(HFS0_TO_ISTORAGE_IDX(gameCardInfo.hfs0PartitionCnt, partition) + 1);
    
    u32 i;
    bool success = false;
    
    if ((dest_len + 1) >= MAX_CHARACTERS(dbuf))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: destination directory name is too long! (%lu bytes)", __func__, dest_len);
        breaks += 2;
        return false;
    }
    
    snprintf(dbuf, MAX_CHARACTERS(dbuf), dest);
    mkdir(dbuf, 0744);
    
    dbuf[dest_len] = '/';
    dest_len++;
    
    result = openGameCardStoragePartition(storageIndex);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open IStorage partition #%u! (0x%08X)", __func__, storageIndex - 1, result);
        breaks += 2;
        return false;
    }
    
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx->start));
    
    for(i = 0; i < gameCardInfo.hfs0Partitions[partition].file_cnt; i++)
    {
        memcpy(&entry, gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (i * sizeof(hfs0_file_entry)), sizeof(hfs0_file_entry));
        
        char *filename = (char*)(gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (gameCardInfo.hfs0Partitions[partition].file_cnt * sizeof(hfs0_file_entry)) + entry.filename_offset);
        
        dbuf[dest_len] = '\0';
        strcat(dbuf, filename);
        removeIllegalCharacters(dbuf + dest_len);
        
        u64 fileOffset = (gameCardInfo.hfs0Partitions[partition].offset + gameCardInfo.hfs0Partitions[partition].header_size + entry.file_offset);
        
        success = copyFileFromHfs0Partition(partition, dbuf, filename, fileOffset, entry.file_size, progressCtx, splitting);
        if (!success) break;
    }
    
    closeGameCardStoragePartition();
    
    return success;
}

bool dumpHfs0PartitionData(u32 partition, bool doSplitting)
{
    if (!gameCardInfo.rootHfs0Header || !gameCardInfo.hfs0PartitionCnt || partition >= gameCardInfo.hfs0PartitionCnt || !gameCardInfo.hfs0Partitions || !gameCardInfo.hfs0Partitions[partition].header)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to dump HFS0 partition data!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!gameCardInfo.hfs0Partitions[partition].file_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: the selected HFS0 partition is empty!", __func__);
        breaks += 2;
        return false;
    }
    
    u32 i;
    hfs0_file_entry entry;
    char dumpPath[NAME_BUF_LEN] = {'\0'};
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    bool success = false;
    
    char *dumpName = generateGameCardDumpName(false);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
        breaks += 2;
        return false;
    }
    
    // Calculate total size
    for(i = 0; i < gameCardInfo.hfs0Partitions[partition].file_cnt; i++)
    {
        memcpy(&entry, gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (i * sizeof(hfs0_file_entry)), sizeof(hfs0_file_entry));
        progressCtx.totalSize += entry.file_size;
    }
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Total partition data size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks += 2;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s - Partition %u (%s)", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, partition));
    
    // Start dump process
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    progressCtx.line_offset = (breaks + 4);
    
    success = copyHfs0PartitionContents(partition, &progressCtx, dumpPath, doSplitting);
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
    }
    
out:
    free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool dumpFileFromHfs0Partition(u32 partition, u32 fileIndex, char *filename, bool doSplitting)
{
    if (!gameCardInfo.rootHfs0Header || !gameCardInfo.hfs0PartitionCnt || partition >= gameCardInfo.hfs0PartitionCnt || !gameCardInfo.hfs0Partitions || !gameCardInfo.hfs0Partitions[partition].header || !gameCardInfo.hfs0Partitions[partition].header_size || !filename || !strlen(filename))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to dump file from HFS0 partition!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!gameCardInfo.hfs0Partitions[partition].file_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: the selected HFS0 partition is empty!", __func__);
        breaks += 2;
        return false;
    }
    
    if (fileIndex >= gameCardInfo.hfs0Partitions[partition].file_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid file index!", __func__);
        breaks += 2;
        return false;
    }
    
    Result result;
    hfs0_file_entry entry;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    u64 fileOffset = 0;
    openIStoragePartition storageIndex = (openIStoragePartition)(HFS0_TO_ISTORAGE_IDX(gameCardInfo.hfs0PartitionCnt, partition) + 1);
    
    char destCopyPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    char *dumpName = generateGameCardDumpName(false);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
        breaks += 2;
        return false;
    }
    
    memcpy(&entry, gameCardInfo.hfs0Partitions[partition].header + sizeof(hfs0_header) + (fileIndex * sizeof(hfs0_file_entry)), sizeof(hfs0_file_entry));
    
    fileOffset = (gameCardInfo.hfs0Partitions[partition].offset + gameCardInfo.hfs0Partitions[partition].header_size + entry.file_offset);
    
    progressCtx.totalSize = entry.file_size;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks += 2;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    snprintf(destCopyPath, MAX_CHARACTERS(destCopyPath), "%s%s - Partition %u (%s)", HFS0_DUMP_PATH, dumpName, partition, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, partition));
    mkdir(destCopyPath, 0744);
    
    strcat(destCopyPath, "/");
    size_t cur_len = strlen(destCopyPath);
    strcat(destCopyPath, filename);
    removeIllegalCharacters(destCopyPath + cur_len);
    
    // Check if the dump already exists
    if (checkIfFileExists(destCopyPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        
        if (!yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?"))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        }
    }
    
    result = openGameCardStoragePartition(storageIndex);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open IStorage partition #%u! (0x%08X)", __func__, storageIndex - 1, result);
        goto out;
    }
    
    // Start dump process
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    success = copyFileFromHfs0Partition(partition, destCopyPath, filename, fileOffset, progressCtx.totalSize, &progressCtx, doSplitting);
    
    closeGameCardStoragePartition();
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        breaks -= 2;
    }
    
out:
    free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool dumpExeFsSectionData(u32 titleIndex, bool usePatch, ncaFsOptions *exeFsDumpCfg)
{
    if (!exeFsDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid ExeFS configuration struct!", __func__);
        breaks += 2;
        return false;
    }
    
    bool isFat32 = exeFsDumpCfg->isFat32;
    bool useLayeredFSDir = exeFsDumpCfg->useLayeredFSDir;
    
    u32 i;
    u64 n = 0, offset = 0;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    size_t write_res;
    bool proceed = true, success = false, fat32_error = false;
    
    char tmp_idx[5] = {'\0'};
    char *dumpName = NULL;
    char dumpPath[NAME_BUF_LEN] = {'\0'}, curDumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    if ((!usePatch && !titleAppCount) || (usePatch && !titlePatchCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title count!", __func__);
        breaks += 2;
        return false;
    }
    
    if ((!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title index!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!useLayeredFSDir)
    {
        dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex, false);
        if (!dumpName)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
            breaks += 2;
            return false;
        }
    }
    
    // Retrieve ExeFS from Program NCA
    if (!readNcaExeFsSection(titleIndex, usePatch))
    {
        if (dumpName) free(dumpName);
        breaks += 2;
        return false;
    }
    
    // Calculate total dump size
    if (!calculateExeFsExtractedDataSize(&(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Extracted ExeFS dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    // Generate output path
    if (!useLayeredFSDir)
    {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s", EXEFS_DUMP_PATH, dumpName);
        
        if (exeFsContext.idOffset > 0)
        {
            sprintf(strbuf, " (ID offset #%u)", exeFsContext.idOffset);
            strcat(dumpPath, strbuf);
        }
    } else {
        mkdir(cfwDirStr, 0744);
        
        // Always use the base application title ID
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%016lX", cfwDirStr, (usePatch ? (patchEntries[titleIndex].titleId & ~APPLICATION_PATCH_BITMASK) : baseAppEntries[titleIndex].titleId) + exeFsContext.idOffset);
        mkdir(dumpPath, 0744);
        
        strcat(dumpPath, "/exefs");
    }
    
    mkdir(dumpPath, 0744);
    
    // Start dump process
    breaks++;
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: file entry without name in ExeFS section!", __func__);
            break;
        }
        
        snprintf(curDumpPath, MAX_CHARACTERS(curDumpPath), "%s/%s", dumpPath, exeFsFilename);
        removeIllegalCharacters(curDumpPath + strlen(dumpPath) + 1);
        
        if (exeFsContext.exefs_entries[i].file_size > FAT32_FILESIZE_LIMIT && isFat32)
        {
            mkdir(curDumpPath, 0744);
            sprintf(tmp_idx, "/%02u", splitIndex);
            strcat(curDumpPath, tmp_idx);
        }
        
        outFile = fopen(curDumpPath, "wb");
        if (!outFile)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, curDumpPath);
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
            
            if (exeFsContext.exefs_entries[i].file_size > FAT32_FILESIZE_LIMIT && isFat32 && (offset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
            {
                u64 new_file_chunk_size = ((offset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, offset, splitIndex, write_res);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (new_file_chunk_size > 0 || (offset + n) < exeFsContext.exefs_entries[i].file_size)
                {
                    char *tmp = strrchr(curDumpPath, '/');
                    if (tmp != NULL) *tmp = '\0';
                    
                    splitIndex++;
                    sprintf(tmp_idx, "/%02u", splitIndex);
                    strcat(curDumpPath, tmp_idx);
                    
                    outFile = fopen(curDumpPath, "wb");
                    if (!outFile)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, offset + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, offset, write_res);
                    
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
            
            if ((progressCtx.curOffset + n) < progressCtx.totalSize && cancelProcessCheck(&progressCtx))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                proceed = false;
                break;
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
        
        // Set archive bit (only for FAT32)
        if (exeFsContext.exefs_entries[i].file_size > FAT32_FILESIZE_LIMIT && isFat32)
        {
            char *tmp = strrchr(curDumpPath, '/');
            if (tmp != NULL) *tmp = '\0';
            fsdevSetConcatenationFileAttribute(curDumpPath);
        }
    }
    
    if (proceed)
    {
        if (progressCtx.curOffset >= progressCtx.totalSize)
        {
            success = true;
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: underdump error! Wrote %lu bytes, expected %lu bytes.", __func__, progressCtx.curOffset, progressCtx.totalSize);
        }
    }
    
    breaks = (progressCtx.line_offset + 2);
    
    if (success)
    {
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        if (fat32_error) breaks += 2;
        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
    }
    
out:
    freeExeFsContext();
    
    if (dumpName) free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool dumpFileFromExeFsSection(u32 titleIndex, u32 fileIndex, bool usePatch, ncaFsOptions *exeFsDumpCfg)
{
    if (!exeFsDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid ExeFS configuration struct!", __func__);
        breaks += 2;
        return false;
    }
    
    bool isFat32 = exeFsDumpCfg->isFat32;
    bool useLayeredFSDir = exeFsDumpCfg->useLayeredFSDir;
    
    if (!exeFsContext.exefs_header.file_cnt || fileIndex > (exeFsContext.exefs_header.file_cnt - 1) || !exeFsContext.exefs_entries || !exeFsContext.exefs_str_table || exeFsContext.exefs_data_offset <= exeFsContext.exefs_offset || (!usePatch && titleIndex > (titleAppCount - 1)) || (usePatch && titleIndex > (titlePatchCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to parse file entry from ExeFS section!", __func__);
        breaks += 2;
        return false;
    }
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    size_t write_res;
    bool proceed = true, success = false, fat32_error = false, removeFile = true;
    
    char tmp_idx[5];
    char *dumpName = NULL;
    char dumpPath[NAME_BUF_LEN] = {'\0'};
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    char *exeFsFilename = (exeFsContext.exefs_str_table + exeFsContext.exefs_entries[fileIndex].filename_offset);
    
    // Check if we're dealing with a nameless file
    if (!strlen(exeFsFilename))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: file entry without name in ExeFS section!", __func__);
        breaks += 2;
        return false;
    }
    
    // Generate output path
    if (!useLayeredFSDir)
    {
        dumpName = generateNSPDumpName((!usePatch ? DUMP_APP_NSP : DUMP_PATCH_NSP), titleIndex, false);
        if (!dumpName)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
            breaks += 2;
            return false;
        }
        
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s", EXEFS_DUMP_PATH, dumpName);
        
        if (exeFsContext.idOffset > 0)
        {
            sprintf(strbuf, " (ID offset #%u)", exeFsContext.idOffset);
            strcat(dumpPath, strbuf);
        }
    } else {
        mkdir(cfwDirStr, 0744);
        
        // Always use the base application title ID
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%016lX", cfwDirStr, (usePatch ? (patchEntries[titleIndex].titleId & ~APPLICATION_PATCH_BITMASK) : baseAppEntries[titleIndex].titleId) + + exeFsContext.idOffset);
        mkdir(dumpPath, 0744);
        
        strcat(dumpPath, "/exefs");
    }
    
    mkdir(dumpPath, 0744);
    
    strcat(dumpPath, "/");
    size_t cur_len = strlen(dumpPath);
    strcat(dumpPath, exeFsFilename);
    removeIllegalCharacters(dumpPath + cur_len);
    
    progressCtx.totalSize = exeFsContext.exefs_entries[fileIndex].file_size;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
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
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        }
    }
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
        // Better safe than sorry
        remove(dumpPath);
        fsdevDeleteDirectoryRecursively(dumpPath);
        
        mkdir(dumpPath, 0744);
        sprintf(tmp_idx, "/%02u", splitIndex);
        strcat(dumpPath, tmp_idx);
    }
    
    // Start dump process
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Copying \"%s\"...", exeFsFilename);
    breaks += 2;
    
    uiRefreshDisplay();
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open output file!", __func__);
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
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
        {
            u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
            u64 old_file_chunk_size = (n - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                    break;
                }
            }
            
            fclose(outFile);
            outFile = NULL;
            
            if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                char *tmp = strrchr(dumpPath, '/');
                if (tmp != NULL) *tmp = '\0';
                
                splitIndex++;
                sprintf(tmp_idx, "/%02u", splitIndex);
                strcat(dumpPath, tmp_idx);
                
                outFile = fopen(dumpPath, "wb");
                if (!outFile)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(dumpBuf, 1, n, outFile);
            if (write_res != n)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, progressCtx.curOffset, write_res);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(&progressCtx, true, n);
        
        if ((progressCtx.curOffset + n) < progressCtx.totalSize && cancelProcessCheck(&progressCtx))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
            break;
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
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        if (fat32_error) breaks += 2;
    }
    
out:
    if (outFile) fclose(outFile);
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        char *tmp = strrchr(dumpPath, '/');
        if (tmp != NULL) *tmp = '\0';
        
        if (success)
        {
            // Set archive bit (only for FAT32)
            fsdevSetConcatenationFileAttribute(dumpPath);
        } else {
            if (removeFile) fsdevDeleteDirectoryRecursively(dumpPath);
        }
    } else {
        if (!success && removeFile) remove(dumpPath);
    }
    
    if (dumpName) free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool recursiveDumpRomFsFile(u32 file_offset, char *romfs_path, char *output_path, progress_ctx_t *progressCtx, bool usePatch, bool isFat32)
{
    if ((!usePatch && (!romFsContext.romfs_filetable_size || file_offset > romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_filetable_size || file_offset > bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || !romfs_path || !output_path || !progressCtx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to parse file entry from RomFS section!", __func__);
        return false;
    }
    
    size_t orig_romfs_path_len = strlen(romfs_path);
    size_t orig_output_path_len = strlen(output_path);
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    bool proceed = true, success = false, fat32_error = false;
    
    // Used to overcome issues related to the max entry count per directory in FAT32
    int dir_limit_counter = -1;
    
    u32 romfs_file_offset = file_offset;
    romfs_file *entry = NULL;
    
    u64 off = 0;
    
    size_t write_res;
    
    char tmp_idx[16];
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    while(romfs_file_offset != ROMFS_ENTRY_EMPTY)
    {
        romfs_path[orig_romfs_path_len] = '\0';
        output_path[orig_output_path_len] = '\0';
        
        n = DUMP_BUFFER_SIZE;
        splitIndex = 0;
        
        entry = (!usePatch ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + romfs_file_offset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + romfs_file_offset));
        
        // Check if we're dealing with a nameless file
        if (!entry->nameLen)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: file entry without name in RomFS section!", __func__);
            break;
        }
        
        if (dir_limit_counter >= 0)
        {
            sprintf(tmp_idx, "_%d", dir_limit_counter);
        } else {
            tmp_idx[0] = '\0';
        }
        
        if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + strlen(tmp_idx) + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: RomFS section file path is too long!", __func__);
            break;
        }
        
        // Generate current path
        strcat(romfs_path, "/");
        strncat(romfs_path, (char*)entry->name, entry->nameLen);
        
        if (dir_limit_counter >= 0) strcat(output_path, tmp_idx);
        strcat(output_path, "/");
        strncat(output_path, (char*)entry->name, entry->nameLen);
        removeIllegalCharacters(output_path + orig_output_path_len + strlen(tmp_idx) + 1);
        
        if (entry->dataSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            mkdir(output_path, 0744);
            sprintf(tmp_idx, "/%02u", splitIndex);
            strcat(output_path, tmp_idx);
        }
        
        // Start dump process
        uiFill(0, ((progressCtx->line_offset - 4) * LINE_HEIGHT) + 8, FB_WIDTH, LINE_HEIGHT * 4, BG_COLOR_RGB);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset - 4), FONT_COLOR_RGB, "Copying \"romfs:%s\"...", romfs_path);
        
        outFile = fopen(output_path, "wb");
        if (!outFile)
        {
            if (entry->dataSize <= FAT32_FILESIZE_LIMIT || !isFat32)
            {
                output_path[orig_output_path_len] = '\0';
                
                dir_limit_counter++;
                sprintf(tmp_idx, "_%d", dir_limit_counter);
                strcat(output_path, tmp_idx);
                mkdir(output_path, 0744);
                
                strcat(output_path, "/");
                strncat(output_path, (char*)entry->name, entry->nameLen);
                removeIllegalCharacters(output_path + orig_output_path_len + strlen(tmp_idx) + 1);
                
                outFile = fopen(output_path, "wb");
            }
            
            if (!outFile)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, output_path);
                break;
            }
        }
        
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
            
            if (entry->dataSize > FAT32_FILESIZE_LIMIT && isFat32 && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
            {
                u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, off, splitIndex, write_res);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                outFile = NULL;
                
                if (new_file_chunk_size > 0 || (off + n) < entry->dataSize)
                {
                    char *tmp = strrchr(output_path, '/');
                    if (tmp != NULL) *tmp = '\0';
                    
                    splitIndex++;
                    sprintf(tmp_idx, "/%02u", splitIndex);
                    strcat(output_path, tmp_idx);
                    
                    outFile = fopen(output_path, "wb");
                    if (!outFile)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                        proceed = false;
                        break;
                    }
                    
                    if (new_file_chunk_size > 0)
                    {
                        write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                        if (write_res != new_file_chunk_size)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                            proceed = false;
                            break;
                        }
                    }
                }
            } else {
                write_res = fwrite(dumpBuf, 1, n, outFile);
                if (write_res != n)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, off, write_res);
                    
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
            
            if (((off + n) < entry->dataSize || (progressCtx->curOffset + n) < progressCtx->totalSize) && cancelProcessCheck(progressCtx))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
                proceed = false;
                break;
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
        
        // Set archive bit (only for FAT32)
        if (entry->dataSize > FAT32_FILESIZE_LIMIT && isFat32)
        {
            char *tmp = strrchr(output_path, '/');
            if (tmp != NULL) *tmp = '\0';
            fsdevSetConcatenationFileAttribute(output_path);
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

bool recursiveDumpRomFsDir(u32 dir_offset, char *romfs_path, char *output_path, progress_ctx_t *progressCtx, bool usePatch, bool dumpSiblingDir, bool isFat32)
{
    if ((!usePatch && (!romFsContext.romfs_dirtable_size || dir_offset > romFsContext.romfs_dirtable_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (usePatch && (!bktrContext.romfs_dirtable_size || dir_offset > bktrContext.romfs_dirtable_size || !bktrContext.romfs_dir_entries || !bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || !romfs_path || !output_path || !progressCtx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to parse directory entry from RomFS section!", __func__);
        return false;
    }
    
    size_t orig_romfs_path_len = strlen(romfs_path);
    size_t orig_output_path_len = strlen(output_path);
    
    romfs_dir *entry = (!usePatch ? (romfs_dir*)((u8*)romFsContext.romfs_dir_entries + dir_offset) : (romfs_dir*)((u8*)bktrContext.romfs_dir_entries + dir_offset));
    
    // Check if we're dealing with a nameless directory that's not the root directory
    if (!entry->nameLen && dir_offset > 0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: directory entry without name in RomFS section!", __func__);
        return false;
    }
    
    if ((orig_romfs_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2) || (orig_output_path_len + 1 + entry->nameLen) >= (NAME_BUF_LEN * 2))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx->line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: RomFS section directory path is too long!", __func__);
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
        if (!recursiveDumpRomFsFile(entry->childFile, romfs_path, output_path, progressCtx, usePatch, isFat32))
        {
            romfs_path[orig_romfs_path_len] = '\0';
            output_path[orig_output_path_len] = '\0';
            return false;
        }
    }
    
    if (entry->childDir != ROMFS_ENTRY_EMPTY)
    {
        if (!recursiveDumpRomFsDir(entry->childDir, romfs_path, output_path, progressCtx, usePatch, true, isFat32))
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
        if (!recursiveDumpRomFsDir(entry->sibling, romfs_path, output_path, progressCtx, usePatch, true, isFat32)) return false;
    }
    
    return true;
}

bool dumpRomFsSectionData(u32 titleIndex, selectedRomFsType curRomFsType, ncaFsOptions *romFsDumpCfg)
{
    if (!romFsDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid RomFS configuration struct!", __func__);
        breaks += 2;
        return false;
    }
    
    bool isFat32 = romFsDumpCfg->isFat32;
    bool useLayeredFSDir = romFsDumpCfg->useLayeredFSDir;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char *dumpName = NULL;
    char romFsPath[NAME_BUF_LEN * 2] = {'\0'}, dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    if ((curRomFsType == ROMFS_TYPE_APP && !titleAppCount) || (curRomFsType == ROMFS_TYPE_PATCH && !titlePatchCount) || (curRomFsType == ROMFS_TYPE_ADDON && !titleAddOnCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title count!", __func__);
        breaks += 2;
        return false;
    }
    
    if ((curRomFsType == ROMFS_TYPE_APP && titleIndex > (titleAppCount - 1)) || (curRomFsType == ROMFS_TYPE_PATCH && titleIndex > (titlePatchCount - 1)) || (curRomFsType == ROMFS_TYPE_ADDON && titleIndex > (titleAddOnCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title index!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!useLayeredFSDir)
    {
        dumpName = generateNSPDumpName((curRomFsType == ROMFS_TYPE_APP ? DUMP_APP_NSP : (curRomFsType == ROMFS_TYPE_PATCH ? DUMP_PATCH_NSP : DUMP_ADDON_NSP)), titleIndex, false);
        if (!dumpName)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
            breaks += 2;
            return false;
        }
    }
    
    // Retrieve RomFS from Program NCA
    if (readNcaRomFsSection(titleIndex, curRomFsType, -1) != 0)
    {
        free(dumpName);
        breaks += 2;
        return false;
    }
    
    // Calculate total dump size
    if (!calculateRomFsFullExtractedSize((curRomFsType == ROMFS_TYPE_PATCH), &(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Extracted RomFS dump size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    // Generate output path
    if (!useLayeredFSDir)
    {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s", ROMFS_DUMP_PATH, dumpName);
        
        if ((curRomFsType != ROMFS_TYPE_PATCH && romFsContext.idOffset > 0) || (curRomFsType == ROMFS_TYPE_PATCH && bktrContext.idOffset > 0))
        {
            sprintf(strbuf, " (ID offset #%u)", (curRomFsType != ROMFS_TYPE_PATCH ? romFsContext.idOffset : bktrContext.idOffset));
            strcat(dumpPath, strbuf);
        }
    } else {
        mkdir(cfwDirStr, 0744);
        
        // Base applications and updates: always use the base application title ID
        // DLCs: use DLC title ID
        u64 titleId = (curRomFsType == ROMFS_TYPE_APP ? baseAppEntries[titleIndex].titleId : (curRomFsType == ROMFS_TYPE_PATCH ? (patchEntries[titleIndex].titleId & ~APPLICATION_PATCH_BITMASK) : addOnEntries[titleIndex].titleId));
        titleId += (curRomFsType != ROMFS_TYPE_PATCH ? romFsContext.idOffset : bktrContext.idOffset);
        
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%016lX", cfwDirStr, titleId);
        mkdir(dumpPath, 0744);
        
        strcat(dumpPath, "/romfs");
    }
    
    mkdir(dumpPath, 0744);
    
    // Start dump process
    breaks++;
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    success = recursiveDumpRomFsDir(0, romFsPath, dumpPath, &progressCtx, (curRomFsType == ROMFS_TYPE_PATCH), true, isFat32);
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
    }
    
out:
    if (curRomFsType == ROMFS_TYPE_PATCH) freeBktrContext();
    
    freeRomFsContext();
    
    if (dumpName) free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool dumpFileFromRomFsSection(u32 titleIndex, u32 file_offset, selectedRomFsType curRomFsType, ncaFsOptions *romFsDumpCfg)
{
    if (!romFsDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid RomFS configuration struct!", __func__);
        breaks += 2;
        return false;
    }
    
    bool isFat32 = romFsDumpCfg->isFat32;
    bool useLayeredFSDir = romFsDumpCfg->useLayeredFSDir;
    
    if ((curRomFsType != ROMFS_TYPE_PATCH && (!romFsContext.romfs_filetable_size || file_offset > romFsContext.romfs_filetable_size || !romFsContext.romfs_file_entries)) || (curRomFsType == ROMFS_TYPE_PATCH && (!bktrContext.romfs_filetable_size || file_offset > bktrContext.romfs_filetable_size || !bktrContext.romfs_file_entries)) || (curRomFsType == ROMFS_TYPE_APP && titleIndex > (titleAppCount - 1)) || (curRomFsType == ROMFS_TYPE_PATCH && titleIndex > (titlePatchCount - 1)) || (curRomFsType == ROMFS_TYPE_ADDON && titleIndex > (titleAddOnCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to parse file entry from RomFS section!", __func__);
        breaks += 2;
        return false;
    }
    
    u64 n = DUMP_BUFFER_SIZE;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    size_t write_res;
    bool proceed = true, success = false, fat32_error = false, removeFile = true;
    
    char tmp_idx[5];
    char *dumpName = NULL;
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    romfs_file *entry = (curRomFsType != ROMFS_TYPE_PATCH ? (romfs_file*)((u8*)romFsContext.romfs_file_entries + file_offset) : (romfs_file*)((u8*)bktrContext.romfs_file_entries + file_offset));
    
    // Check if we're dealing with a nameless file
    if (!entry->nameLen)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: file entry without name in RomFS section!", __func__);
        breaks += 2;
        return false;
    }
    
    progressCtx.totalSize = entry->dataSize;
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "File size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        breaks += 2;
        return false;
    }
    
    breaks++;
    
    // Generate output path
    if (!useLayeredFSDir)
    {
        dumpName = generateNSPDumpName((curRomFsType == ROMFS_TYPE_APP ? DUMP_APP_NSP : (curRomFsType == ROMFS_TYPE_PATCH ? DUMP_PATCH_NSP : DUMP_ADDON_NSP)), titleIndex, false);
        if (!dumpName)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
            breaks += 2;
            return false;
        }
        
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s", ROMFS_DUMP_PATH, dumpName);
        
        if ((curRomFsType != ROMFS_TYPE_PATCH && romFsContext.idOffset > 0) || (curRomFsType == ROMFS_TYPE_PATCH && bktrContext.idOffset > 0))
        {
            sprintf(strbuf, " (ID offset #%u)", (curRomFsType != ROMFS_TYPE_PATCH ? romFsContext.idOffset : bktrContext.idOffset));
            strcat(dumpPath, strbuf);
        }
    } else {
        mkdir(cfwDirStr, 0744);
        
        // Base applications and updates: always use the base application title ID
        // DLCs: use DLC title ID
        u64 titleId = (curRomFsType == ROMFS_TYPE_APP ? baseAppEntries[titleIndex].titleId : (curRomFsType == ROMFS_TYPE_PATCH ? (patchEntries[titleIndex].titleId & ~APPLICATION_PATCH_BITMASK) : addOnEntries[titleIndex].titleId));
        titleId += (curRomFsType != ROMFS_TYPE_PATCH ? romFsContext.idOffset : bktrContext.idOffset);
        
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%016lX", cfwDirStr, titleId);
        mkdir(dumpPath, 0744);
        
        strcat(dumpPath, "/romfs");
    }
    
    mkdir(dumpPath, 0744);
    
    // Create subdirectories
    char *tmp1 = NULL, *tmp2 = NULL;
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
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        }
    }
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        // Since we may actually be dealing with an existing directory with the archive bit set or unset, let's try both
        // Better safe than sorry
        remove(dumpPath);
        fsdevDeleteDirectoryRecursively(dumpPath);
        
        mkdir(dumpPath, 0744);
        sprintf(tmp_idx, "/%02u", splitIndex);
        strcat(dumpPath, tmp_idx);
    }
    
    // Start dump process
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, dumpPath);
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
        
        if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32 && (progressCtx.curOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
        {
            u64 new_file_chunk_size = ((progressCtx.curOffset + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
            u64 old_file_chunk_size = (n - new_file_chunk_size);
            
            if (old_file_chunk_size > 0)
            {
                write_res = fwrite(dumpBuf, 1, old_file_chunk_size, outFile);
                if (write_res != old_file_chunk_size)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, old_file_chunk_size, progressCtx.curOffset, splitIndex, write_res);
                    break;
                }
            }
            
            fclose(outFile);
            outFile = NULL;
            
            if (new_file_chunk_size > 0 || (progressCtx.curOffset + n) < progressCtx.totalSize)
            {
                char *tmp = strrchr(dumpPath, '/');
                if (tmp != NULL) *tmp = '\0';
                
                splitIndex++;
                sprintf(tmp_idx, "/%02u", splitIndex);
                strcat(dumpPath, tmp_idx);
                
                outFile = fopen(dumpPath, "wb");
                if (!outFile)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to open output file for part #%u!", __func__, splitIndex);
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(dumpBuf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", __func__, new_file_chunk_size, progressCtx.curOffset + old_file_chunk_size, splitIndex, write_res);
                        break;
                    }
                }
            }
        } else {
            write_res = fwrite(dumpBuf, 1, n, outFile);
            if (write_res != n)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "%s: failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", __func__, n, progressCtx.curOffset, write_res);
                
                if ((progressCtx.curOffset + n) > FAT32_FILESIZE_LIMIT)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 4), FONT_COLOR_RGB, "You're probably using a FAT32 partition. Make sure to enable file splitting.");
                    fat32_error = true;
                }
                
                break;
            }
        }
        
        printProgressBar(&progressCtx, true, n);
        
        if ((progressCtx.curOffset + n) < progressCtx.totalSize && cancelProcessCheck(&progressCtx))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(progressCtx.line_offset + 2), FONT_COLOR_ERROR_RGB, "Process canceled.");
            break;
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
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        if (fat32_error) breaks += 2;
    }
    
out:
    if (outFile) fclose(outFile);
    
    if (progressCtx.totalSize > FAT32_FILESIZE_LIMIT && isFat32)
    {
        char *tmp = strrchr(dumpPath, '/');
        if (tmp != NULL) *tmp = '\0';
        
        if (success)
        {
            // Set archive bit (only for FAT32)
            fsdevSetConcatenationFileAttribute(dumpPath);
        } else {
            if (removeFile) fsdevDeleteDirectoryRecursively(dumpPath);
        }
    } else {
        if (!success && removeFile) remove(dumpPath);
    }
    
    if (dumpName) free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool dumpCurrentDirFromRomFsSection(u32 titleIndex, selectedRomFsType curRomFsType, ncaFsOptions *romFsDumpCfg)
{
    if (!romFsDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid RomFS configuration struct!", __func__);
        breaks += 2;
        return false;
    }
    
    bool isFat32 = romFsDumpCfg->isFat32;
    bool useLayeredFSDir = romFsDumpCfg->useLayeredFSDir;
    
    progress_ctx_t progressCtx;
    memset(&progressCtx, 0, sizeof(progress_ctx_t));
    
    char *dumpName = NULL;
    char romFsPath[NAME_BUF_LEN * 2] = {'\0'}, dumpPath[NAME_BUF_LEN * 2] = {'\0'};
    
    bool success = false;
    
    if ((curRomFsType == ROMFS_TYPE_APP && !titleAppCount) || (curRomFsType == ROMFS_TYPE_PATCH && !titlePatchCount) || (curRomFsType == ROMFS_TYPE_ADDON && !titleAddOnCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title count!", __func__);
        breaks += 2;
        return false;
    }
    
    if ((curRomFsType == ROMFS_TYPE_APP && titleIndex > (titleAppCount - 1)) || (curRomFsType == ROMFS_TYPE_PATCH && titleIndex > (titlePatchCount - 1)) || (curRomFsType == ROMFS_TYPE_ADDON && titleIndex > (titleAddOnCount - 1)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title index!", __func__);
        breaks += 2;
        return false;
    }
    
    if (!useLayeredFSDir)
    {
        dumpName = generateNSPDumpName((curRomFsType == ROMFS_TYPE_APP ? DUMP_APP_NSP : (curRomFsType == ROMFS_TYPE_PATCH ? DUMP_PATCH_NSP : DUMP_ADDON_NSP)), titleIndex, false);
        if (!dumpName)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
            breaks += 2;
            return false;
        }
    }
    
    // Calculate total dump size
    if (!calculateRomFsExtractedDirSize(curRomFsDirOffset, (curRomFsType == ROMFS_TYPE_PATCH), &(progressCtx.totalSize))) goto out;
    
    convertSize(progressCtx.totalSize, progressCtx.totalSizeStr, MAX_CHARACTERS(progressCtx.totalSizeStr));
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Extracted RomFS directory size: %s (%lu bytes).", progressCtx.totalSizeStr, progressCtx.totalSize);
    uiRefreshDisplay();
    breaks++;
    
    if (progressCtx.totalSize > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    if (strlen(curRomFsPath) > 1)
    {
        // Copy the whole current path and remove the last element (current directory) from it
        // It will be re-added later
        snprintf(romFsPath, MAX_CHARACTERS(romFsPath), curRomFsPath);
        char *slash = strrchr(romFsPath, '/');
        if (slash) *slash = '\0';
    }
    
    // Generate output path
    if (!useLayeredFSDir)
    {
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s", ROMFS_DUMP_PATH, dumpName);
        
        if ((curRomFsType != ROMFS_TYPE_PATCH && romFsContext.idOffset > 0) || (curRomFsType == ROMFS_TYPE_PATCH && bktrContext.idOffset > 0))
        {
            sprintf(strbuf, " (ID offset #%u)", (curRomFsType != ROMFS_TYPE_PATCH ? romFsContext.idOffset : bktrContext.idOffset));
            strcat(dumpPath, strbuf);
        }
    } else {
        mkdir(cfwDirStr, 0744);
        
        // Base applications and updates: always use the base application title ID
        // DLCs: use DLC title ID
        u64 titleId = (curRomFsType == ROMFS_TYPE_APP ? baseAppEntries[titleIndex].titleId : (curRomFsType == ROMFS_TYPE_PATCH ? (patchEntries[titleIndex].titleId & ~APPLICATION_PATCH_BITMASK) : addOnEntries[titleIndex].titleId));
        titleId += (curRomFsType != ROMFS_TYPE_PATCH ? romFsContext.idOffset : bktrContext.idOffset);
        
        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%016lX", cfwDirStr, titleId);
        mkdir(dumpPath, 0744);
        
        strcat(dumpPath, "/romfs");
    }
    
    mkdir(dumpPath, 0744);
    
    // Create subdirectories
    char *tmp1 = NULL, *tmp2 = NULL;
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
    dumpStartMsg();
    appletModeOperationWarning();
    uiRefreshDisplay();
    breaks++;
    
    changeHomeButtonBlockStatus(true);
    
    progressCtx.line_offset = (breaks + 4);
    timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.start));
    
    success = recursiveDumpRomFsDir(curRomFsDirOffset, romFsPath, dumpPath, &progressCtx, (curRomFsType == ROMFS_TYPE_PATCH), false, isFat32);
    
    if (success)
    {
        breaks = (progressCtx.line_offset + 2);
        
        timeGetCurrentTime(TimeType_LocalSystemClock, &(progressCtx.now));
        progressCtx.now -= progressCtx.start;
        
        formatETAString(progressCtx.now, progressCtx.etaInfo, MAX_CHARACTERS(progressCtx.etaInfo));
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed after %s!", progressCtx.etaInfo);
    } else {
        setProgressBarError(&progressCtx);
        removeDirectoryWithVerbose(dumpPath, "Deleting output directory. Please wait...");
    }
    
out:
    if (dumpName) free(dumpName);
    
    breaks += 2;
    
    changeHomeButtonBlockStatus(false);
    
    return success;
}

bool dumpGameCardCertificate()
{
    u32 crc = 0;
    Result result;
    bool success = false;
    FILE *outFile = NULL;
    char dumpPath[NAME_BUF_LEN] = {'\0'};
    size_t write_res;
    
    memset(dumpBuf, 0, DUMP_BUFFER_SIZE);
    
    char *dumpName = generateGameCardDumpName(false);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
        breaks += 2;
        return false;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Dumping gamecard certificate. Please wait.");
    breaks++;
    
    appletModeOperationWarning();
    breaks++;
    
    uiRefreshDisplay();
    
    result = openGameCardStoragePartition(ISTORAGE_PARTITION_NORMAL);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open IStorage partition #0! (0x%08X)", __func__, result);
        goto out;
    }
    
    if (CERT_SIZE > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    result = readGameCardStoragePartition(CERT_OFFSET, dumpBuf, CERT_SIZE);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read %u bytes long certificate at offset 0x%016lX from IStorage partition #0! (0x%08X)", __func__, CERT_SIZE, CERT_OFFSET, result);
        goto out;
    }
    
    // Calculate CRC32
    crc32(dumpBuf, CERT_SIZE, &crc);
    
    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s - Certificate (%08X).bin", CERT_DUMP_PATH, dumpName, crc);
    
    // Check if the dump already exists
    if (checkIfFileExists(dumpPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        
        if (!yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?"))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        }
    }
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, dumpPath);
        goto out;
    }
    
    write_res = fwrite(dumpBuf, 1, CERT_SIZE, outFile);
    if (write_res != CERT_SIZE)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %u bytes certificate data! (wrote %lu bytes)", __func__, CERT_SIZE, write_res);
        goto out;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully completed!");
    breaks++;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Certificate dumped to: \"%s\".", strrchr(dumpPath, '/' ) + 1);
    
    success = true;
    
out:
    if (outFile) fclose(outFile);
    
    if (!success && strlen(dumpPath)) remove(dumpPath);
    
    closeGameCardStoragePartition();
    
    free(dumpName);
    
    breaks += 2;
    
    return success;
}

bool dumpTicketFromTitle(u32 titleIndex, selectedTicketType curTikType, ticketOptions *tikDumpCfg)
{
    if (!tikDumpCfg)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid ticket dump configuration struct!", __func__);
        breaks += 2;
        return false;
    }
    
    bool removeConsoleData = tikDumpCfg->removeConsoleData;
    
    u32 i = 0;
    Result result;
    
    NcmStorageId curStorageId;
    NcmContentMetaType metaType;
    u32 titleCount = 0, ncmTitleIndex = 0;
    
    char *dumpName = NULL;
    char dumpPath[NAME_BUF_LEN] = {'\0'};
    
    NcmContentInfo *titleContentInfos = NULL;
    u32 titleContentInfoCnt = 0;
    
    NcmContentId ncaId;
    char ncaIdStr[SHA256_HASH_SIZE + 1] = {'\0'};
    
    NcmContentStorage ncmStorage;
    memset(&ncmStorage, 0, sizeof(NcmContentStorage));
    
    u8 ncaHeader[NCA_FULL_HEADER_LENGTH] = {0};
    nca_header_t dec_nca_header;
    
    u8 decrypted_nca_keys[NCA_KEY_AREA_SIZE];
    
    title_rights_ctx rights_info;
    memset(&rights_info, 0, sizeof(title_rights_ctx));
    
    char encTitleKeyStr[0x21] = {'\0'};
    char decTitleKeyStr[0x21] = {'\0'};
    
    FILE *outFile = NULL;
    
    bool success = false, proceed = true, foundRightsIdAndTik = false, removeFile = false;
    
    if (curTikType != TICKET_TYPE_APP && curTikType != TICKET_TYPE_PATCH && curTikType != TICKET_TYPE_ADDON)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid ticket title type!", __func__);
        goto out;
    }
    
    if ((curTikType == TICKET_TYPE_APP && !baseAppEntries) || (curTikType == TICKET_TYPE_PATCH && !patchEntries) || (curTikType == TICKET_TYPE_ADDON && !addOnEntries))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: title storage ID unavailable!", __func__);
        goto out;
    }
    
    if ((curTikType == TICKET_TYPE_APP && titleIndex >= titleAppCount) || (curTikType == TICKET_TYPE_PATCH && titleIndex >= titlePatchCount) || (curTikType == TICKET_TYPE_ADDON && titleIndex >= titleAddOnCount))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title index!", __func__);
        goto out;
    }
    
    curStorageId = (curTikType == TICKET_TYPE_APP ? baseAppEntries[titleIndex].storageId : (curTikType == TICKET_TYPE_PATCH ? patchEntries[titleIndex].storageId : addOnEntries[titleIndex].storageId));
    
    ncmTitleIndex = (curTikType == TICKET_TYPE_APP ? baseAppEntries[titleIndex].ncmIndex : (curTikType == TICKET_TYPE_PATCH ? patchEntries[titleIndex].ncmIndex : addOnEntries[titleIndex].ncmIndex));
    
    metaType = (curTikType == TICKET_TYPE_APP ? NcmContentMetaType_Application : (curTikType == TICKET_TYPE_PATCH ? NcmContentMetaType_Patch : NcmContentMetaType_AddOnContent));
    
    if (curStorageId == NcmStorageId_GameCard)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid title storage ID!", __func__);
        goto out;
    }
    
    if (sizeof(rsa2048_sha256_ticket) > freeSpace)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: not enough free space available in the SD card!", __func__);
        goto out;
    }
    
    switch(curStorageId)
    {
        case NcmStorageId_SdCard:
            titleCount = (curTikType == TICKET_TYPE_APP ? sdCardTitleAppCount : (curTikType == TICKET_TYPE_PATCH ? sdCardTitlePatchCount : sdCardTitleAddOnCount));
            break;
        case NcmStorageId_BuiltInUser:
            titleCount = (curTikType == TICKET_TYPE_APP ? emmcTitleAppCount : (curTikType == TICKET_TYPE_PATCH ? emmcTitlePatchCount : emmcTitleAddOnCount));
            break;
        default:
            break;
    }
    
    dumpName = generateNSPDumpName((nspDumpType)curTikType, titleIndex, false);
    if (!dumpName)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to generate output dump name!", __func__);
        goto out;
    }
    
    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.tik", TICKET_PATH, dumpName);
    
    // Check if the dump already exists
    if (checkIfFileExists(dumpPath))
    {
        // Ask the user if they want to proceed anyway
        int cur_breaks = breaks;
        
        proceed = yesNoPrompt("You have already dumped this content. Do you wish to proceed anyway?");
        if (!proceed)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Process canceled.");
            goto out;
        } else {
            // Remove the prompt from the screen
            breaks = cur_breaks;
            uiFill(0, STRING_Y_POS(breaks), FB_WIDTH, FB_HEIGHT - STRING_Y_POS(breaks), BG_COLOR_RGB);
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Retrieving Rights ID and Ticket for the selected %s...", (curTikType == TICKET_TYPE_APP ? "base application" : (curTikType == TICKET_TYPE_PATCH ? "update" : "DLC")));
    breaks++;
    
    appletModeOperationWarning();
    breaks++;
    
    uiRefreshDisplay();
    
    if (!retrieveContentInfosFromTitle(curStorageId, metaType, titleCount, ncmTitleIndex, &titleContentInfos, &titleContentInfoCnt))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
        goto out;
    }
    
    result = ncmOpenContentStorage(&ncmStorage, curStorageId);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: ncmOpenContentStorage failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    for(i = 0; i < titleContentInfoCnt; i++)
    {
        memcpy(&ncaId, &(titleContentInfos[i].content_id), sizeof(NcmContentId));
        convertDataToHexString(titleContentInfos[i].content_id.c, SHA256_HASH_SIZE / 2, ncaIdStr, SHA256_HASH_SIZE + 1);
        
        if (!readNcaDataByContentId(&ncmStorage, &ncaId, 0, ncaHeader, NCA_FULL_HEADER_LENGTH))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read header from NCA \"%s\"!", __func__, ncaIdStr);
            proceed = false;
            break;
        }
        
        // Decrypt the NCA header
        proceed = decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header, &rights_info, decrypted_nca_keys, true);
        if (!proceed) break;
        
        // Check if we hit the right spot
        if (rights_info.has_rights_id && rights_info.retrieved_tik)
        {
            convertDataToHexString(rights_info.enc_titlekey, 0x10, encTitleKeyStr, 0x21);
            convertDataToHexString(rights_info.dec_titlekey, 0x10, decTitleKeyStr, 0x21);
            foundRightsIdAndTik = true;
            break;
        }
    }
    
    if (!proceed) goto out;
    
    if (!foundRightsIdAndTik)
    {
        if (!rights_info.has_rights_id)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: the selected %s doesn't use titlekey crypto! Rights ID field is empty in all the NCAs!", __func__, (curTikType == TICKET_TYPE_APP ? "base application" : (curTikType == TICKET_TYPE_PATCH ? "update" : "DLC")));
            goto out;
        }
        
        if (rights_info.missing_tik)
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: the selected %s uses titlekey crypto, but no ticket for it is available! This is probably a pre-install.", __func__, (curTikType == TICKET_TYPE_APP ? "base application" : (curTikType == TICKET_TYPE_PATCH ? "update" : "DLC")));
            goto out;
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Rights ID: \"%s\".", rights_info.rights_id_str);
    breaks++;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Ticket type: %s (0x%02X).", (rights_info.tik_data.titlekey_type == ETICKET_TITLEKEY_COMMON ? "common" : (rights_info.tik_data.titlekey_type == ETICKET_TITLEKEY_PERSONALIZED ? "personalized" : "unknown")), rights_info.tik_data.titlekey_type);
    breaks++;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Encrypted title key: \"%s\".", encTitleKeyStr);
    breaks++;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Decrypted title key: \"%s\".", decTitleKeyStr);
    breaks += 2;
    
    uiRefreshDisplay();
    
    // Only mess with the ticket data if removeConsoleData is true and if we're dealing with a personalized ticket (checked in removeConsoleDataFromTicket())
    if (removeConsoleData) removeConsoleDataFromTicket(&rights_info);
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open output file \"%s\"!", __func__, dumpPath);
        goto out;
    }
    
    size_t wr = fwrite(&(rights_info.tik_data), 1, sizeof(rsa2048_sha256_ticket), outFile);
    if (wr != sizeof(rsa2048_sha256_ticket))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to write %u bytes long ticket data to \"%s\"! Wrote %lu bytes.", __func__, sizeof(rsa2048_sha256_ticket), dumpPath, wr);
        removeFile = true;
        goto out;
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Process successfully finished!");
    breaks++;
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Ticket saved to \"%s\".", dumpPath);
    
    success = true;
    
out:
    breaks += 2;
    
    if (outFile) fclose(outFile);
    
    if (!success && removeFile) remove(dumpPath);
    
    ncmContentStorageClose(&ncmStorage);
    
    if (titleContentInfos) free(titleContentInfos);
    
    if (dumpName) free(dumpName);
    
    return success;
}
