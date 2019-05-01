#include <stdio.h>
#include <malloc.h>
#include <dirent.h>
#include <memory.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>
#include <ctype.h>

#include "crc32_fast.h"
#include "dumper.h"
#include "fsext.h"
#include "ui.h"
#include "util.h"

/* Extern variables */

extern u64 freeSpace;

extern int breaks;
extern int font_height;

extern u64 trimmedCardSize;
extern char trimmedCardSizeStr[32];

extern char *hfs0_header;
extern u64 hfs0_offset, hfs0_size;
extern u32 hfs0_partition_cnt;

extern char *partitionHfs0Header;
extern u64 partitionHfs0HeaderOffset, partitionHfs0HeaderSize;
extern u32 partitionHfs0FileCount, partitionHfs0StrTableSize;

extern u32 gameCardAppCount;
extern u64 *gameCardTitleID;
extern u32 *gameCardVersion;
extern char **fixedGameCardName;

extern AppletType programAppletType;

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

bool dumpCartridgeImage(FsDeviceOperator* fsOperator, bool isFat32, bool dumpCert, bool trimDump, bool calcCrc)
{
    u64 partitionOffset = 0, fileOffset = 0, xciDataSize = 0, totalSize = 0, n;
    u64 partitionSizes[ISTORAGE_PARTITION_CNT];
    char partitionSizesStr[ISTORAGE_PARTITION_CNT][32] = {'\0'}, xciDataSizeStr[32] = {'\0'}, curSizeStr[32] = {'\0'}, totalSizeStr[32] = {'\0'}, filename[NAME_BUF_LEN * 2] = {'\0'};
    u32 partition;
    Result result;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    bool proceed = true, success = false, fat32_error = false;
    FILE *outFile = NULL;
    char *buf = NULL;
    u8 splitIndex = 0;
    u8 progress = 0;
    u32 crc1 = 0, crc2 = 0;
    
    u64 start, now, remainingTime;
    struct tm *timeinfo;
    char etaInfo[32] = {'\0'};
    double lastSpeed = 0.0, averageSpeed = 0.0;
    
    size_t write_res;
    
    char *dumpName = generateDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 0, breaks * font_height, 255, 0, 0);
        breaks += 2;
        return false;
    }
    
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
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition #%u size: %s (%lu bytes).", partition, partitionSizesStr[partition], partitionSizes[partition]);
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
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "XCI data size: %s (%lu bytes).", xciDataSizeStr, xciDataSize);
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
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Output dump size: %s (%lu bytes).", totalSizeStr, totalSize);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        if (totalSize <= freeSpace)
        {
            breaks++;
            
            if (totalSize > FAT32_FILESIZE_LIMIT && isFat32)
            {
                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xc%u", dumpName, splitIndex);
            } else {
                snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xci", dumpName);
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
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                        breaks += 2;
                    }
                    
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
                                    
                                    if (totalSize > FAT32_FILESIZE_LIMIT && isFat32 && (fileOffset + n) < totalSize && (fileOffset + n) >= ((splitIndex + 1) * SPLIT_FILE_XCI_PART_SIZE))
                                    {
                                        u64 new_file_chunk_size = ((fileOffset + n) - ((splitIndex + 1) * SPLIT_FILE_XCI_PART_SIZE));
                                        u64 old_file_chunk_size = (n - new_file_chunk_size);
                                        
                                        if (old_file_chunk_size > 0)
                                        {
                                            write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                                            if (write_res != old_file_chunk_size)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, fileOffset, splitIndex, write_res);
                                                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                                proceed = false;
                                                break;
                                            }
                                        }
                                        
                                        fclose(outFile);
                                        
                                        splitIndex++;
                                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xc%u", dumpName, splitIndex);
                                        
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
                                            write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                            if (write_res != new_file_chunk_size)
                                            {
                                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, fileOffset + old_file_chunk_size, splitIndex, write_res);
                                                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                                proceed = false;
                                                break;
                                            }
                                        }
                                    } else {
                                        write_res = fwrite(buf, 1, n, outFile);
                                        if (write_res != n)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, fileOffset, write_res);
                                            uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                            
                                            if ((fileOffset + n) > FAT32_FILESIZE_LIMIT)
                                            {
                                                uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 0, (breaks + 7) * font_height, 255, 255, 255);
                                                fat32_error = true;
                                            }
                                            
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
                                    if (fat32_error) breaks += 2;
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
                    
                    if (totalSize > FAT32_FILESIZE_LIMIT && isFat32)
                    {
                        for(u8 i = 0; i <= splitIndex; i++)
                        {
                            snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s.xc%u", dumpName, i);
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
    
    free(dumpName);
    
    return success;
}

bool dumpApplicationNSP(FsDeviceOperator* fsOperator, bool isFat32, bool calcCrc, u32 appIndex)
{
    Result result;
    u32 i = 0, j = 0;
    u32 written = 0;
    u32 total = 0;
    u32 appNcaCount = 0;
    u32 partition = (hfs0_partition_cnt - 1); // Select the secure partition
    
    NcmContentMetaDatabase ncmDb;
    NcmContentStorage ncmStorage;
    NcmApplicationContentMetaKey *appList = NULL;
    NcmContentRecord *appContentRecords = NULL;
    size_t appListSize = (gameCardAppCount * sizeof(NcmApplicationContentMetaKey));
    
    cnmt_xml_program_info xml_program_info;
    cnmt_xml_content_info *xml_content_info = NULL;
    
    NcmNcaId ncaId;
    char ncaHeader[NCA_FULL_HEADER_LENGTH] = {'\0'};
    nca_header_t dec_nca_header;
    
    u32 cnmtNcaIndex = 0;
    char *cnmtNcaBuf = NULL;
    bool cnmtFound = false;
    
    u64 cnmt_pfs0_offset;
    u64 cnmt_pfs0_size;
    pfs0_header cnmt_pfs0_header;
    pfs0_entry_table *cnmt_pfs0_entries = NULL;
    
    u64 appCnmtOffset;
    cnmt_header appCnmtHeader;
    cnmt_application_header appCnmtAppHeader;
    cnmt_content_record *appCnmtContentRecords = NULL;
    
    char *metadataXml = NULL;
    
    pfs0_header nspPfs0Header;
    pfs0_entry_table *nspPfs0EntryTable = NULL;
    char *nspPfs0StrTable = NULL;
    u32 full_nsp_header_size = 0;
    
    u64 total_size = 0;
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'}, curSizeStr[32] = {'\0'}, totalSizeStr[32] = {'\0'};
    
    u64 n, nca_offset, nsp_file_offset = 0;
    FILE *outFile = NULL;
    char *buf = NULL;
    u8 splitIndex = 0;
    u8 progress = 0;
    u32 crc = 0;
    bool proceed = true, success = false, fat32_error = false;
    
    u64 start, now, remainingTime;
    struct tm *timeinfo;
    char etaInfo[32] = {'\0'};
    double lastSpeed = 0.0, averageSpeed = 0.0;
    
    size_t write_res;
    
    // Generate filename for our required CNMT file
    char cnmtFileName[40] = {'\0'};
    snprintf(cnmtFileName, sizeof(cnmtFileName) / sizeof(cnmtFileName[0]), "Application_%016lx.cnmt", gameCardTitleID[appIndex]);
    
    if (appIndex > (gameCardAppCount - 1))
    {
        uiDrawString("Error: invalid application index!", 0, breaks * font_height, 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (!getPartitionHfs0Header(partition)) return false;
    
    if (!partitionHfs0FileCount)
    {
        uiDrawString("The Secure HFS0 partition is empty!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    uiDrawString("Retrieving information from encrypted NCA content files...", 0, breaks * font_height, 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    appList = (NcmApplicationContentMetaKey*)calloc(1, appListSize);
    if (!appList)
    {
        uiDrawString("Error: unable to allocate memory for the ApplicationContentMetaKey struct!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentMetaDatabase(FsStorageId_GameCard, &ncmDb)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmOpenContentMetaDatabase failed! (0x%08X)", result);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseListApplication(&ncmDb, META_DB_REGULAR_APPLICATION, appList, appListSize, &written, &total)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseListApplication failed! (0x%08X)", result);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (!written || !total)
    {
        uiDrawString("Error: ncmContentMetaDatabaseListApplication wrote no entries to output buffer!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (written != total || written != gameCardAppCount)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: application count mismatch in ncmContentMetaDatabaseListApplication (%u != %u)", written, gameCardAppCount);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    appContentRecords = (NcmContentRecord*)calloc(partitionHfs0FileCount, sizeof(NcmContentRecord));
    if (!appContentRecords)
    {
        uiDrawString("Error: unable to allocate memory for the ContentRecord struct!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmContentMetaDatabaseListContentInfo(&ncmDb, &(appList[appIndex].metaRecord), 0, appContentRecords, partitionHfs0FileCount * sizeof(NcmContentRecord), &written)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentMetaDatabaseListContentInfo failed! (0x%08X)", result);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    appNcaCount = written;
    
    // Fill information for our XML
    memset(&xml_program_info, 0, sizeof(cnmt_xml_program_info));
    xml_program_info.type = appList[appIndex].metaRecord.type;
    xml_program_info.title_id = appList[appIndex].metaRecord.titleId;
    xml_program_info.version = appList[appIndex].metaRecord.version;
    xml_program_info.nca_cnt = appNcaCount;
    
    xml_content_info = (cnmt_xml_content_info*)calloc(appNcaCount, sizeof(cnmt_xml_content_info));
    if (!xml_content_info)
    {
        uiDrawString("Error: unable to allocate memory for the CNMT XML content info struct!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (R_FAILED(result = ncmOpenContentStorage(FsStorageId_GameCard, &ncmStorage)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmOpenContentStorage failed! (0x%08X)", result);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    for(i = 0; i < appNcaCount; i++)
    {
        // Fill information for our XML
        xml_content_info[i].type = appContentRecords[i].type;
        memcpy(xml_content_info[i].nca_id, appContentRecords[i].ncaId.c, 16);
        convertDataToHexString(appContentRecords[i].ncaId.c, 16, xml_content_info[i].nca_id_str, 33);
        convertNcaSizeToU64(appContentRecords[i].size, &(xml_content_info[i].size));
        
        memcpy(&ncaId, &(appContentRecords[i].ncaId), sizeof(NcmNcaId));
        
        if (!cnmtFound && appContentRecords[i].type == NcmContentType_CNMT)
        {
            cnmtFound = true;
            cnmtNcaIndex = i;
            
            cnmtNcaBuf = (char*)calloc(xml_content_info[i].size, sizeof(char));
            if (!cnmtNcaBuf)
            {
                uiDrawString("Error: unable to allocate memory for CNMT NCA data!", 0, breaks * font_height, 255, 0, 0);
                proceed = false;
                break;
            }
            
            if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, 0, cnmtNcaBuf, xml_content_info[i].size)))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentStorageReadContentIdFile failed for NCA \"%s\"! (0x%08X)", xml_content_info[i].nca_id_str, result);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                proceed = false;
                break;
            }
            
            // Calculate SHA-256 checksum for the CNMT NCA
            if (!calculateSHA256((u8*)cnmtNcaBuf, (u32)xml_content_info[i].size, xml_content_info[i].hash))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "SHA-256 checksum calculation for CNMT NCA \"%s\" failed!", xml_content_info[i].nca_id_str);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                proceed = false;
                break;
            }
            
            // Fill information for our XML
            convertDataToHexString(xml_content_info[i].hash, NCA_CNMT_DIGEST_SIZE, xml_content_info[i].hash_str, 65);
            
            // Decrypt the CNMT NCA buffer in-place
            if (!decryptCnmtNca(cnmtNcaBuf, xml_content_info[i].size))
            {
                proceed = false;
                break;
            }
            
            memcpy(&dec_nca_header, cnmtNcaBuf, sizeof(nca_header_t));
        } else {
            if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, 0, ncaHeader, NCA_FULL_HEADER_LENGTH)))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentStorageReadContentIdFile failed for NCA \"%s\"! (0x%08X)", xml_content_info[i].nca_id_str, result);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                proceed = false;
                break;
            }
            
            // Decrypt the NCA header in-place
            if (!decryptNcaHeader(ncaHeader, NCA_FULL_HEADER_LENGTH, &dec_nca_header))
            {
                proceed = false;
                break;
            }
        }
        
        // Fill information for our XML
        xml_content_info[i].keyblob = (dec_nca_header.crypto_type2 > dec_nca_header.crypto_type ? dec_nca_header.crypto_type2 : dec_nca_header.crypto_type);
        
        // Modify distribution type
        dec_nca_header.distribution = 0;
        
        // Reencrypt header
        if (!encryptNcaHeader(&dec_nca_header, xml_content_info[i].encrypted_header_mod, NCA_FULL_HEADER_LENGTH))
        {
            proceed = false;
            break;
        }
        
        /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "sdmc:/%s_header.nca", xml_content_info[i].nca_id_str);
        FILE *mod_header = fopen(strbuf, "wb");
        if (mod_header)
        {
            fwrite(xml_content_info[i].encrypted_header_mod, 1, NCA_FULL_HEADER_LENGTH, mod_header);
            fclose(mod_header);
        }*/
    }
    
    if (proceed && !cnmtFound)
    {
        uiDrawString("Error: unable to find the NCA ID for the application's CNMT!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (!proceed) goto out;
    
    // Fill information for our XML
    xml_program_info.min_keyblob = xml_content_info[cnmtNcaIndex].keyblob;
    
    memcpy(&dec_nca_header, cnmtNcaBuf, sizeof(nca_header_t));
    
    cnmt_pfs0_offset = ((dec_nca_header.section_entries[0].media_start_offset * MEDIA_UNIT_SIZE) + dec_nca_header.fs_headers[0].pfs0_superblock.hash_table_offset + dec_nca_header.fs_headers[0].pfs0_superblock.pfs0_offset);
    cnmt_pfs0_size = dec_nca_header.fs_headers[0].pfs0_superblock.pfs0_size;
    
    // Fill information for our XML
    memcpy(xml_program_info.digest, cnmtNcaBuf + cnmt_pfs0_offset + cnmt_pfs0_size - NCA_CNMT_DIGEST_SIZE, NCA_CNMT_DIGEST_SIZE);
    convertDataToHexString(xml_program_info.digest, NCA_CNMT_DIGEST_SIZE, xml_program_info.digest_str, 65);
    
    memcpy(&cnmt_pfs0_header, cnmtNcaBuf + cnmt_pfs0_offset, sizeof(pfs0_header));
    
    cnmt_pfs0_entries = (pfs0_entry_table*)calloc(cnmt_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
    if (!cnmt_pfs0_entries)
    {
        uiDrawString("Error: unable to allocate memory for the PFS0 File Entry Table from CNMT NCA section #0!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    cnmtFound = false;
    
    // Extract the decrypted CNMT in order to retrieve the remaining information for our XML
    // It's filename in the PFS0 partition must match the "Application_{lower-case hex titleID}.cnmt" format
    for(i = 0; i < cnmt_pfs0_header.file_cnt; i++)
    {
        u32 filename_offset = (cnmt_pfs0_offset + sizeof(pfs0_header) + (cnmt_pfs0_header.file_cnt * sizeof(pfs0_entry_table)) + cnmt_pfs0_entries[i].filename_offset);
        if (!strncasecmp(cnmtNcaBuf + filename_offset, cnmtFileName, strlen(cnmtFileName)))
        {
            cnmtFound = true;
            appCnmtOffset = (cnmt_pfs0_offset + sizeof(pfs0_header) + (cnmt_pfs0_header.file_cnt * sizeof(pfs0_entry_table)) + cnmt_pfs0_header.str_table_size + cnmt_pfs0_entries[i].file_offset);
            break;
        }
    }
    
    if (!cnmtFound)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to find file \"%s\" in PFS0 partition from CNMT NCA section #0!", cnmtFileName);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    memcpy(&appCnmtHeader, cnmtNcaBuf + appCnmtOffset, sizeof(cnmt_header));
    memcpy(&appCnmtAppHeader, cnmtNcaBuf + appCnmtOffset + sizeof(cnmt_header), sizeof(cnmt_application_header));
    
    // Fill information for our XML
    xml_program_info.patch_tid = appCnmtAppHeader.patch_tid;
    xml_program_info.min_sysver = (u32)appCnmtAppHeader.min_sysver;
    
    appCnmtContentRecords = (cnmt_content_record*)calloc(appCnmtHeader.content_records_cnt, sizeof(cnmt_content_record));
    if (!appCnmtContentRecords)
    {
        uiDrawString("Error: unable to allocate memory for the PFS0 File Entry Table from CNMT NCA section #0!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    memcpy(appCnmtContentRecords, cnmtNcaBuf + appCnmtOffset + sizeof(cnmt_header) + appCnmtHeader.table_offset, appCnmtHeader.content_records_cnt * sizeof(cnmt_content_record));
    
    for(i = 0; i < appCnmtHeader.content_records_cnt; i++)
    {
        for(j = 0; j < appNcaCount; j++)
        {
            if (!memcmp(appCnmtContentRecords[i].nca_id, xml_content_info[j].nca_id, 16))
            {
                // Fill information for our XML
                memcpy(xml_content_info[j].hash, appCnmtContentRecords[i].hash, NCA_CNMT_DIGEST_SIZE);
                convertDataToHexString(xml_content_info[j].hash, NCA_CNMT_DIGEST_SIZE, xml_content_info[j].hash_str, 65);
                break;
            }
        }
    }
    
    /*for(i = 0; i < appNcaCount; i++)
    {
        breaks++;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Content Record #%u:", i);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "NCA ID: %s", xml_content_info[i].nca_id_str);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Size: 0x%016lX", xml_content_info[i].size);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Type: 0x%02X", xml_content_info[i].type);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Hash: %s", xml_content_info[i].hash_str);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Keyblob: 0x%02X", xml_content_info[i].keyblob);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        breaks++;
        
        uiRefreshDisplay();
    }*/
    
    breaks++;
    uiDrawString("Generating metadata XML...", 0, breaks * font_height, 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    // Generate our metadata XML, making sure that the output buffer is big enough
    metadataXml = (char*)calloc(NAME_BUF_LEN * 4, sizeof(char));
    if (!metadataXml)
    {
        uiDrawString("Error: unable to allocate memory for the metadata XML!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    generateCnmtMetadataXml(&xml_program_info, xml_content_info, metadataXml);
    
    /*char cnmtXmlFileName[50] = {'\0'};
    sprintf(cnmtXmlFileName, "%s.cnmt.xml", xml_content_info[cnmtNcaIndex].nca_id_str);
    FILE *metaxml = fopen(cnmtXmlFileName, "wb");
    if (metaxml)
    {
        fwrite(metadataXml, 1, strlen(metadataXml), metaxml);
        fclose(metaxml);
    }*/
    
    // Start NSP creation
    breaks++;
    uiDrawString("Generating PFS0 header...", 0, breaks * font_height, 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    memset(&nspPfs0Header, 0, sizeof(pfs0_header));
    nspPfs0Header.magic = bswap_32((u32)PFS0_MAGIC);
    nspPfs0Header.file_cnt = (appNcaCount + 1); // Make sure to consider the metadata XML
    
    nspPfs0EntryTable = (pfs0_entry_table*)calloc(appNcaCount + 1, sizeof(pfs0_entry_table));
    if (!nspPfs0EntryTable)
    {
        uiDrawString("Unable to allocate memory for the PFS0 file entries!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    // Make sure we have enough memory for the PFS0 String Table
    nspPfs0StrTable = (char*)calloc(NAME_BUF_LEN * 4, sizeof(char));
    if (!nspPfs0StrTable)
    {
        uiDrawString("Unable to allocate memory for the PFS0 string table!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    // Fill our Entry and String Tables
    u64 file_offset = 0;
    u32 filename_offset = 0;
    
    for(i = 0; i < (appNcaCount + 1); i++)
    {
        char ncaFileName[50] = {'\0'};
        
        if (i == appNcaCount)
        {
            sprintf(ncaFileName, "%s.cnmt.xml", xml_content_info[cnmtNcaIndex].nca_id_str);
            nspPfs0EntryTable[i].file_size = strlen(metadataXml);
        } else {
            sprintf(ncaFileName, "%s.%s", xml_content_info[i].nca_id_str, (i == cnmtNcaIndex ? "cnmt.nca" : "nca"));
            nspPfs0EntryTable[i].file_size = xml_content_info[i].size;
        }
        
        nspPfs0EntryTable[i].file_offset = file_offset;
        nspPfs0EntryTable[i].filename_offset = filename_offset;
        
        strcpy(nspPfs0StrTable + filename_offset, ncaFileName);
        
        file_offset += nspPfs0EntryTable[i].file_size;
        filename_offset += (strlen(ncaFileName) + 1);
    }
    
    filename_offset--;
    
    // Determine our full NSP header size
    full_nsp_header_size = (sizeof(pfs0_header) + ((appNcaCount + 1) * sizeof(pfs0_entry_table)) + filename_offset);
    full_nsp_header_size = round_up(full_nsp_header_size, 16);
    
    // Determine our String Table size
    nspPfs0Header.str_table_size = (full_nsp_header_size - (sizeof(pfs0_header) + ((appNcaCount + 1) * sizeof(pfs0_entry_table))));
    
    // Calculate total dump size
    total_size = full_nsp_header_size;
    for(i = 0; i < (appNcaCount + 1); i++) total_size += nspPfs0EntryTable[i].file_size;
    
    breaks++;
    convertSize(total_size, totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]));
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Total NSP dump size: %s (%lu bytes).", totalSizeStr, total_size);
    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
    uiRefreshDisplay();
    breaks++;
    
    if (total_size > freeSpace)
    {
        uiDrawString("Error: not enough free space available in the SD card.", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    if (total_size > FAT32_FILESIZE_LIMIT && isFat32)
    {
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s v%u (%016lX).nsp", fixedGameCardName[appIndex], gameCardVersion[appIndex], gameCardTitleID[appIndex]);
        
        mkdir(dumpPath, 0744);
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s v%u (%016lX).nsp/%02u", fixedGameCardName[appIndex], gameCardVersion[appIndex], gameCardTitleID[appIndex], splitIndex);
    } else {
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s v%u (%016lX).nsp", fixedGameCardName[appIndex], gameCardVersion[appIndex], gameCardTitleID[appIndex]);
    }
    
    outFile = fopen(dumpPath, "wb");
    if (!outFile)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file \"%s\"!", dumpPath);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    buf = (char*)malloc(DUMP_BUFFER_SIZE);
    if (!buf)
    {
        uiDrawString("Failed to allocate memory for the dump process!", 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    breaks++;
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dump procedure started. Writing output to \"%.*s\". Hold B to cancel.", (int)((total_size > FAT32_FILESIZE_LIMIT && isFat32) ? (strlen(dumpPath) - 3) : strlen(dumpPath)), dumpPath);
    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
    uiRefreshDisplay();
    breaks += 2;
    
    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
    {
        uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
        breaks += 2;
    }
    
    // Write our full PFS0 header
    memcpy(buf, &nspPfs0Header, sizeof(pfs0_header));
    memcpy(buf + sizeof(pfs0_header), nspPfs0EntryTable, sizeof(pfs0_entry_table) * (appNcaCount + 1));
    memcpy(buf + sizeof(pfs0_header) + (sizeof(pfs0_entry_table) * (appNcaCount + 1)), nspPfs0StrTable, nspPfs0Header.str_table_size);
    
    write_res = fwrite(buf, 1, full_nsp_header_size, outFile);
    if (write_res != full_nsp_header_size)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %u bytes full PFS0 header to file offset 0x%016lX! (wrote %lu bytes)", full_nsp_header_size, (u64)0, write_res);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        goto out;
    }
    
    nsp_file_offset = full_nsp_header_size;
    
    // Update CRC32
    if (calcCrc) crc32(buf, full_nsp_header_size, &crc);
    
    timeGetCurrentTime(TimeType_LocalSystemClock, &start);
    
    for(i = 0; i < appNcaCount; i++)
    {
        uiFill(0, breaks * font_height, FB_WIDTH, font_height * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping NCA content #%u...", i);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
        
        n = DUMP_BUFFER_SIZE;
        memcpy(ncaId.c, xml_content_info[i].nca_id, 16);
        
        for(nca_offset = 0; nca_offset < xml_content_info[i].size; nca_offset += n, nsp_file_offset += n)
        {
            if (DUMP_BUFFER_SIZE > (xml_content_info[i].size - nca_offset)) n = (xml_content_info[i].size - nca_offset);
            
            if (R_FAILED(result = ncmContentStorageReadContentIdFile(&ncmStorage, &ncaId, nca_offset, buf, n)))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentStorageReadContentIdFile failed (0x%08X) at offset 0x%016lX for NCA \"%s\".", result, nca_offset, xml_content_info[i].nca_id_str);
                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                proceed = false;
                break;
            }
            
            // Replace NCA header with our modified one
            if (nca_offset == 0) memcpy(buf, xml_content_info[i].encrypted_header_mod, NCA_FULL_HEADER_LENGTH);
            
            // Update CRC32
            if (calcCrc) crc32(buf, n, &crc);
            
            if (total_size > FAT32_FILESIZE_LIMIT && isFat32 && (nsp_file_offset + n) < total_size && (nsp_file_offset + n) >= ((splitIndex + 1) * SPLIT_FILE_NSP_PART_SIZE))
            {
                u64 new_file_chunk_size = ((nsp_file_offset + n) - ((splitIndex + 1) * SPLIT_FILE_NSP_PART_SIZE));
                u64 old_file_chunk_size = (n - new_file_chunk_size);
                
                if (old_file_chunk_size > 0)
                {
                    write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                    if (write_res != old_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, nsp_file_offset, splitIndex, write_res);
                        uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
                
                fclose(outFile);
                
                splitIndex++;
                snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s v%u (%016lX).nsp/%02u", fixedGameCardName[appIndex], gameCardVersion[appIndex], gameCardTitleID[appIndex], splitIndex);
                
                outFile = fopen(dumpPath, "wb");
                if (!outFile)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                    uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                    proceed = false;
                    break;
                }
                
                if (new_file_chunk_size > 0)
                {
                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                    if (write_res != new_file_chunk_size)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, nsp_file_offset + old_file_chunk_size, splitIndex, write_res);
                        uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                        proceed = false;
                        break;
                    }
                }
            } else {
                write_res = fwrite(buf, 1, n, outFile);
                if (write_res != n)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, nsp_file_offset, write_res);
                    uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                    
                    if ((nsp_file_offset + n) > FAT32_FILESIZE_LIMIT)
                    {
                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable the \"Split output dump\" option.", 0, (breaks + 7) * font_height, 255, 255, 255);
                        fat32_error = true;
                    }
                    
                    proceed = false;
                    break;
                }
            }
            
            timeGetCurrentTime(TimeType_LocalSystemClock, &now);
            
            lastSpeed = (((double)(nsp_file_offset + n) / (double)DUMP_BUFFER_SIZE) / difftime((time_t)now, (time_t)start));
            averageSpeed = ((SMOOTHING_FACTOR * lastSpeed) + ((1 - SMOOTHING_FACTOR) * averageSpeed));
            if (!isnormal(averageSpeed)) averageSpeed = 0.00; // Very low values
            
            remainingTime = (u64)(((double)(total_size - (nsp_file_offset + n)) / (double)DUMP_BUFFER_SIZE) / averageSpeed);
            timeinfo = localtime((time_t*)&remainingTime);
            strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
            
            progress = (u8)(((nsp_file_offset + n) * 100) / total_size);
            
            uiFill(0, ((breaks + 2) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%.2lf MiB/s [ETA: %s]", averageSpeed, etaInfo);
            uiDrawString(strbuf, font_height * 2, (breaks + 2) * font_height, 255, 255, 255);
            
            uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, FB_WIDTH / 2, font_height, 0, 0, 0);
            uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, (((nsp_file_offset + n) * (FB_WIDTH / 2)) / total_size), font_height, 0, 255, 0);
            
            uiFill(FB_WIDTH - (FB_WIDTH / 4), ((breaks + 2) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            convertSize(nsp_file_offset + n, curSizeStr, sizeof(curSizeStr) / sizeof(curSizeStr[0]));
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progress, curSizeStr, totalSizeStr);
            uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (breaks + 2) * font_height, 255, 255, 255);
            
            uiRefreshDisplay();
            
            if ((nsp_file_offset + n) < total_size && ((nsp_file_offset / DUMP_BUFFER_SIZE) % 10) == 0)
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
        
        if (!proceed)
        {
            uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, (((nsp_file_offset + n) * (FB_WIDTH / 2)) / total_size), font_height, 255, 0, 0);
            break;
        }
        
        // Support empty files
        if (!xml_content_info[i].size)
        {
            uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, ((nsp_file_offset * (FB_WIDTH / 2)) / total_size), font_height, 0, 255, 0);
            
            uiFill(FB_WIDTH - (FB_WIDTH / 4), ((breaks + 2) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
            convertSize(nsp_file_offset, curSizeStr, sizeof(curSizeStr) / sizeof(curSizeStr[0]));
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progress, curSizeStr, totalSizeStr);
            uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (breaks + 2) * font_height, 255, 255, 255);
            
            uiRefreshDisplay();
        }
    }
    
    if (!proceed) goto out;
    
    // Write our metadata XML
    uiFill(0, breaks * font_height, FB_WIDTH, font_height * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Writing metadata XML \"%s.cnmt.xml\"...", xml_content_info[cnmtNcaIndex].nca_id_str);
    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
    
    write_res = fwrite(metadataXml, 1, strlen(metadataXml), outFile);
    if (write_res != strlen(metadataXml))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes metadata XML to file offset 0x%016lX! (wrote %lu bytes)", strlen(metadataXml), nsp_file_offset, write_res);
        uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
        goto out;
    }
    
    nsp_file_offset += strlen(metadataXml);
    
    if (nsp_file_offset < total_size)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Unexpected underdump error! Wrote %lu bytes, expected %lu bytes.", nsp_file_offset, total_size);
        uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
        goto out;
    }
    
    success = true;
    
    // Update CRC32
    if (calcCrc) crc32(metadataXml, strlen(metadataXml), &crc);
    
    // Update progress
    remainingTime = 0;
    timeinfo = localtime((time_t*)&remainingTime);
    strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
    
    progress = 100;
    
    uiFill(FB_WIDTH / 4, ((breaks + 2) * font_height) + 2, FB_WIDTH / 2, font_height, 0, 255, 0);
    
    uiFill(FB_WIDTH - (FB_WIDTH / 4), ((breaks + 2) * font_height) - 4, FB_WIDTH / 4, font_height + 8, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    convertSize(nsp_file_offset, curSizeStr, sizeof(curSizeStr) / sizeof(curSizeStr[0]));
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%u%% [%s / %s]", progress, curSizeStr, totalSizeStr);
    uiDrawString(strbuf, FB_WIDTH - (FB_WIDTH / 4) + (font_height * 2), (breaks + 2) * font_height, 255, 255, 255);
    uiRefreshDisplay();
    
    breaks += 5;
    
    // Finalize dump
    now -= start;
    timeinfo = localtime((time_t*)&now);
    strftime(etaInfo, sizeof(etaInfo) / sizeof(etaInfo[0]), "%HH%MM%SS", timeinfo);
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Process successfully completed after %s!", etaInfo);
    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
    
    if (calcCrc)
    {
        breaks++;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "NSP dump CRC32 checksum: %08X", crc);
        uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
        breaks++;
    }
    
    // Set archive bit (only for FAT32)
    if (total_size > FAT32_FILESIZE_LIMIT && isFat32)
    {
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s v%u (%016lX).nsp", fixedGameCardName[appIndex], gameCardVersion[appIndex], gameCardTitleID[appIndex]);
        if (R_FAILED(result = fsdevSetArchiveBit(dumpPath)))
        {
            breaks++;
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Warning: failed to set archive bit on output directory! (0x%08X)", result);
            uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
            breaks++;
        }
    }
    
out:
    if (buf) free(buf);
    
    if (outFile) fclose(outFile);
    
    if (!success)
    {
        breaks += 5;
        if (fat32_error) breaks += 2;
        
        snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s v%u (%016lX).nsp", fixedGameCardName[appIndex], gameCardVersion[appIndex], gameCardTitleID[appIndex]);
        
        if (total_size > FAT32_FILESIZE_LIMIT && isFat32)
        {
            removeDirectory(dumpPath);
        } else {
            remove(dumpPath);
        }
    }
    
    if (nspPfs0StrTable) free(nspPfs0StrTable);
    
    if (nspPfs0EntryTable) free(nspPfs0EntryTable);
    
    if (metadataXml) free(metadataXml);
    
    if (appCnmtContentRecords) free(appCnmtContentRecords);
    
    if (cnmt_pfs0_entries) free(cnmt_pfs0_entries);
    
    if (cnmtNcaBuf) free(cnmtNcaBuf);
    
    if (xml_content_info) free(xml_content_info);
    
    if (appContentRecords) free(appContentRecords);
    
    if (appList) free(appList);
    
    if (partitionHfs0Header)
    {
        free(partitionHfs0Header);
        partitionHfs0Header = NULL;
        partitionHfs0HeaderSize = 0;
        partitionHfs0FileCount = 0;
        partitionHfs0StrTableSize = 0;
    }
    
    breaks += 2;
    
    return success;
}

bool dumpRawPartition(FsDeviceOperator* fsOperator, u32 partition, bool doSplitting)
{
    Result result;
    u64 size, partitionOffset;
    bool success = false, fat32_error = false;
    char *buf;
    u64 off, n = DUMP_BUFFER_SIZE;
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    char totalSizeStr[32] = {'\0'}, curSizeStr[32] = {'\0'}, filename[NAME_BUF_LEN * 2] = {'\0'};
    u8 progress = 0;
    FILE *outFile = NULL;
    u8 splitIndex = 0;
    
    u64 start, now, remainingTime;
    struct tm *timeinfo;
    char etaInfo[32] = {'\0'};
    double lastSpeed = 0.0, averageSpeed = 0.0;
    
    size_t write_res;
    
    char *dumpName = generateDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 0, breaks * font_height, 255, 0, 0);
        breaks += 2;
        return false;
    }
    
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
        // Oddly enough, IFileSystem instances actually points to the specified partition ID filesystem. I don't understand why it doesn't work like that for IStorage, but whatever
        // NOTE: Using partition == 2 returns error 0x149002, and using higher values probably do so, too
        
        if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            if (getHfs0EntryDetails(hfs0_header, hfs0_offset, hfs0_size, hfs0_partition_cnt, partition, true, 0, &partitionOffset, &size))
            {
                convertSize(size, totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition size: %s (%lu bytes).", totalSizeStr, size);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition offset (relative to IStorage instance): 0x%016lX", partitionOffset);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                if (size <= freeSpace)
                {
                    if (size > FAT32_FILESIZE_LIMIT && doSplitting)
                    {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0.%02u", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                    } else {
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    }
                    
                    outFile = fopen(filename, "wb");
                    if (outFile)
                    {
                        buf = (char*)malloc(DUMP_BUFFER_SIZE);
                        if (buf)
                        {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping raw HFS0 partition #%u to \"%.*s\". Hold B to cancel.", partition, (int)((size > FAT32_FILESIZE_LIMIT && doSplitting) ? (strlen(filename) - 3) : strlen(filename)), filename);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                            breaks += 2;
                            
                            if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                            {
                                uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                                breaks += 2;
                            }
                            
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
                                
                                if (size > FAT32_FILESIZE_LIMIT && doSplitting && (off + n) < size && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
                                {
                                    u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                                    u64 old_file_chunk_size = (n - new_file_chunk_size);
                                    
                                    if (old_file_chunk_size > 0)
                                    {
                                        write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                                        if (write_res != old_file_chunk_size)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, off, splitIndex, write_res);
                                            uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                            break;
                                        }
                                    }
                                    
                                    fclose(outFile);
                                    
                                    splitIndex++;
                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0.%02u", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), splitIndex);
                                    
                                    outFile = fopen(filename, "wb");
                                    if (!outFile)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open output file for part #%u!", splitIndex);
                                        uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                        break;
                                    }
                                    
                                    if (new_file_chunk_size > 0)
                                    {
                                        write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                        if (write_res != new_file_chunk_size)
                                        {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                                            uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                            break;
                                        }
                                    }
                                } else {
                                    write_res = fwrite(buf, 1, n, outFile);
                                    if (write_res != n)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                                        uiDrawString(strbuf, 0, (breaks + 3) * font_height, 255, 0, 0);
                                        
                                        if ((off + n) > FAT32_FILESIZE_LIMIT)
                                        {
                                            uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 0, (breaks + 5) * font_height, 255, 255, 255);
                                            fat32_error = true;
                                        }
                                        
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
                            if (fat32_error) breaks += 2;
                            
                            free(buf);
                        } else {
                            uiDrawString("Failed to allocate memory for the dump process!", 0, breaks * font_height, 255, 0, 0);
                        }
                        
                        if (outFile) fclose(outFile);
                        
                        if (!success)
                        {
                            if (size > FAT32_FILESIZE_LIMIT && doSplitting)
                            {
                                for(u8 i = 0; i <= splitIndex; i++)
                                {
                                    snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Partition %u (%s).hfs0.%02u", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), i);
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
    
    free(dumpName);
    
    return success;
}

bool copyFileFromHfs0(FsDeviceOperator* fsOperator, u32 partition, const char* source, const char* dest, const u64 file_offset, const u64 size, bool doSplitting, bool calcEta)
{
    Result result;
    bool success = false, fat32_error = false;
    char splitFilename[NAME_BUF_LEN] = {'\0'};
    size_t destLen = strlen(dest);
    FILE *outFile = NULL;
    char *buf = NULL;
    u64 off, n = DUMP_BUFFER_SIZE;
    u8 splitIndex = 0;
    u8 progress = 0;
    char totalSizeStr[32] = {'\0'}, curSizeStr[32] = {'\0'};
    
    FsGameCardHandle handle;
    FsStorage gameCardStorage;
    
    u64 start, now, remainingTime;
    struct tm *timeinfo;
    char etaInfo[32] = {'\0'};
    double lastSpeed = 0.0, averageSpeed = 0.0;
    
    size_t write_res;
    
    uiFill(0, breaks * font_height, FB_WIDTH, font_height * 4, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
    
    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying \"%s\"...", source);
    uiDrawString(strbuf, 0, (breaks + 1) * font_height, 255, 255, 255);
    uiRefreshDisplay();
    
    if ((destLen + 1) < NAME_BUF_LEN)
    {
        if (R_SUCCEEDED(result = fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
        {
            /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle succeeded: 0x%08X", handle.value);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
            breaks++;*/
            
            // Same ugly hack from dumpRawPartition()
            if (R_SUCCEEDED(result = fsOpenGameCardStorage(&gameCardStorage, &handle, HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition))))
            {
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage succeeded: 0x%08X", handle.value);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;*/
                
                convertSize(size, totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]));
                
                if (size > FAT32_FILESIZE_LIMIT && doSplitting) snprintf(splitFilename, sizeof(splitFilename) / sizeof(splitFilename[0]), "%s.%02u", dest, splitIndex);
                
                outFile = fopen(((size > FAT32_FILESIZE_LIMIT && doSplitting) ? splitFilename : dest), "wb");
                if (outFile)
                {
                    buf = (char*)malloc(DUMP_BUFFER_SIZE);
                    if (buf)
                    {
                        if (calcEta) timeGetCurrentTime(TimeType_LocalSystemClock, &start);
                        
                        for (off = 0; off < size; off += n)
                        {
                            if (DUMP_BUFFER_SIZE > (size - off)) n = (size - off);
                            
                            if (R_FAILED(result = fsStorageRead(&gameCardStorage, file_offset + off, buf, n)))
                            {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "StorageRead failed (0x%08X) at offset 0x%016lX", result, file_offset + off);
                                uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                break;
                            }
                            
                            if (size > FAT32_FILESIZE_LIMIT && doSplitting && (off + n) < size && (off + n) >= ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE))
                            {
                                u64 new_file_chunk_size = ((off + n) - ((splitIndex + 1) * SPLIT_FILE_GENERIC_PART_SIZE));
                                u64 old_file_chunk_size = (n - new_file_chunk_size);
                                
                                if (old_file_chunk_size > 0)
                                {
                                    write_res = fwrite(buf, 1, old_file_chunk_size, outFile);
                                    if (write_res != old_file_chunk_size)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", old_file_chunk_size, off, splitIndex, write_res);
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
                                    write_res = fwrite(buf + old_file_chunk_size, 1, new_file_chunk_size, outFile);
                                    if (write_res != new_file_chunk_size)
                                    {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX to part #%02u! (wrote %lu bytes)", new_file_chunk_size, off + old_file_chunk_size, splitIndex, write_res);
                                        uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                        break;
                                    }
                                }
                            } else {
                                write_res = fwrite(buf, 1, n, outFile);
                                if (write_res != n)
                                {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %lu bytes chunk from offset 0x%016lX! (wrote %lu bytes)", n, off, write_res);
                                    uiDrawString(strbuf, 0, (breaks + 5) * font_height, 255, 0, 0);
                                    
                                    if ((off + n) > FAT32_FILESIZE_LIMIT)
                                    {
                                        uiDrawString("You're probably using a FAT32 partition. Make sure to enable file splitting.", 0, (breaks + 7) * font_height, 255, 255, 255);
                                        fat32_error = true;
                                    }
                                    
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
                            if (fat32_error) breaks += 2;
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
                        if (size > FAT32_FILESIZE_LIMIT && doSplitting)
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
                
                fsStorageClose(&gameCardStorage);
            } else {
                breaks += 3;
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "OpenGameCardStorage failed! (0x%08X)", result);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            }
        } else {
            breaks += 3;
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "GetGameCardHandle failed! (0x%08X)", result);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        }
    } else {
        breaks += 3;
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination path is too long! (%lu bytes)", destLen);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
    }
    
    return success;
}

bool copyHfs0Contents(FsDeviceOperator* fsOperator, u32 partition, hfs0_entry_table *partitionEntryTable, const char *dest, bool splitting)
{
    if (!dest || !*dest)
    {
        uiDrawString("Error: destination directory is empty.", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (!partitionHfs0Header || !partitionEntryTable)
    {
        uiDrawString("HFS0 partition header information unavailable!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    char dbuf[NAME_BUF_LEN] = {'\0'};
    size_t dest_len = strlen(dest);
    
    if ((dest_len + 1) >= NAME_BUF_LEN)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination directory name is too long! (%lu bytes)", dest_len);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    strcpy(dbuf, dest);
    mkdir(dbuf, 0744);
    
    dbuf[dest_len] = '/';
    dest_len++;
    
    u32 i;
    bool success;
    
    for(i = 0; i < partitionHfs0FileCount; i++)
    {
        u32 filename_offset = (HFS0_ENTRY_TABLE_ADDR + (sizeof(hfs0_entry_table) * partitionHfs0FileCount) + partitionEntryTable[i].filename_offset);
        char *filename = (partitionHfs0Header + filename_offset);
        strcpy(dbuf + dest_len, filename);
        
        u64 file_offset = (partitionHfs0HeaderSize + partitionEntryTable[i].file_offset);
        if (HFS0_TO_ISTORAGE_IDX(hfs0_partition_cnt, partition) == 0) file_offset += partitionHfs0HeaderOffset;
        
        success = copyFileFromHfs0(fsOperator, partition, filename, dbuf, file_offset, partitionEntryTable[i].file_size, splitting, false);
        if (!success) break;
    }
    
    return success;
}

bool dumpPartitionData(FsDeviceOperator* fsOperator, u32 partition)
{
    bool success = false;
    u64 total_size = 0;
    u32 i;
    hfs0_entry_table *entryTable = NULL;
    char dumpPath[NAME_BUF_LEN * 2] = {'\0'}, totalSizeStr[32] = {'\0'};
    
    char *dumpName = generateDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 0, breaks * font_height, 255, 0, 0);
        breaks += 2;
        return false;
    }
    
    workaroundPartitionZeroAccess(fsOperator);
    
    if (getPartitionHfs0Header(partition))
    {
        if (partitionHfs0FileCount)
        {
            entryTable = (hfs0_entry_table*)malloc(sizeof(hfs0_entry_table) * partitionHfs0FileCount);
            if (entryTable)
            {
                memcpy(entryTable, partitionHfs0Header + HFS0_ENTRY_TABLE_ADDR, sizeof(hfs0_entry_table) * partitionHfs0FileCount);
                
                // Calculate total size
                for(i = 0; i < partitionHfs0FileCount; i++) total_size += entryTable[i].file_size;
                
                convertSize(total_size, totalSizeStr, sizeof(totalSizeStr) / sizeof(totalSizeStr[0]));
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Total partition data size: %s (%lu bytes).", totalSizeStr, total_size);
                uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                breaks++;
                
                if (total_size <= freeSpace)
                {
                    snprintf(dumpPath, sizeof(dumpPath) / sizeof(dumpPath[0]), "sdmc:/%s - Partition %u (%s)", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Copying partition #%u data to \"%s/\". Hold B to cancel.", partition, dumpPath);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                    breaks += 2;
                    
                    if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                    {
                        uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                        breaks += 2;
                    }
                    
                    uiRefreshDisplay();
                    
                    success = copyHfs0Contents(fsOperator, partition, entryTable, dumpPath, true);
                    if (success)
                    {
                        breaks += 5;
                        uiDrawString("Process successfully completed!", 0, breaks * font_height, 0, 255, 0);
                    } else {
                        removeDirectory(dumpPath);
                    }
                } else {
                    uiDrawString("Error: not enough free space available in the SD card.", 0, breaks * font_height, 255, 0, 0);
                }
                
                free(entryTable);
            } else {
                uiDrawString("Unable to allocate memory for the HFS0 file entries!", 0, breaks * font_height, 255, 0, 0);
            }
        } else {
            uiDrawString("The selected partition is empty!", 0, breaks * font_height, 255, 0, 0);
        }
        
        free(partitionHfs0Header);
        partitionHfs0Header = NULL;
        partitionHfs0HeaderSize = 0;
        partitionHfs0FileCount = 0;
        partitionHfs0StrTableSize = 0;
    }
    
    breaks += 2;
    
    free(dumpName);
    
    return success;
}

bool dumpFileFromPartition(FsDeviceOperator* fsOperator, u32 partition, u32 file, char *filename)
{
    if (!partitionHfs0Header)
    {
        uiDrawString("HFS0 partition header information unavailable!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    if (!filename || !*filename)
    {
        uiDrawString("Filename unavailable!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    char *dumpName = generateDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 0, breaks * font_height, 255, 0, 0);
        return false;
    }
    
    u64 file_offset = 0;
    u64 file_size = 0;
    bool success = false;
    
    if (getHfs0EntryDetails(partitionHfs0Header, partitionHfs0HeaderOffset, partitionHfs0HeaderSize, partitionHfs0FileCount, file, false, partition, &file_offset, &file_size))
    {
        if (file_size <= freeSpace)
        {
            char destCopyPath[NAME_BUF_LEN * 2] = {'\0'};
            snprintf(destCopyPath, sizeof(destCopyPath) / sizeof(destCopyPath[0]), "sdmc:/%s - Partition %u (%s)", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition));
            
            if ((strlen(destCopyPath) + 1 + strlen(filename)) < NAME_BUF_LEN)
            {
                mkdir(destCopyPath, 0744);
                snprintf(destCopyPath, sizeof(destCopyPath) / sizeof(destCopyPath[0]), "sdmc:/%s - Partition %u (%s)/%s", dumpName, partition, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, partition), filename);
                
                uiDrawString("Hold B to cancel.", 0, breaks * font_height, 255, 255, 255);
                breaks += 2;
                
                if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                {
                    uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
                    breaks += 2;
                }
                
                uiRefreshDisplay();
                
                success = copyFileFromHfs0(fsOperator, partition, filename, destCopyPath, file_offset, file_size, true, true);
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Destination path is too long! (%lu bytes)", strlen(destCopyPath) + 1 + strlen(filename));
                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
            }
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: not enough free space available in the SD card (%lu bytes required).", file_size);
            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        }
    } else {
        uiDrawString("Error: unable to get file details from the partition HFS0 header!", 0, breaks * font_height, 255, 0, 0);
    }
    
    free(dumpName);
    
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
    char filename[NAME_BUF_LEN * 2] = {'\0'};
    char *buf = NULL;
    size_t write_res;
    
    char *dumpName = generateDumpName();
    if (!dumpName)
    {
        uiDrawString("Error: unable to generate output dump name!", 0, breaks * font_height, 255, 0, 0);
        breaks += 2;
        return false;
    }
    
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
                        
                        snprintf(filename, sizeof(filename) / sizeof(filename[0]), "sdmc:/%s - Certificate (%08X).bin", dumpName, crc);
                        
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Dumping game card certificate to \"%s\"...", filename);
                        uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                        breaks += 2;
                        
                        if (programAppletType != AppletType_Application && programAppletType != AppletType_SystemApplication)
                        {
                            uiDrawString("Do not press the HOME button. Doing so could corrupt the SD card filesystem.", 0, breaks * font_height, 255, 0, 0);
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
                                uiDrawString("Process successfully completed!", 0, breaks * font_height, 0, 255, 0);
                            } else {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to write %u bytes certificate data! (wrote %lu bytes)", CERT_SIZE, write_res);
                                uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
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
    
    free(dumpName);
    
    return success;
}
