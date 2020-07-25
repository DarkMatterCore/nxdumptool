/*
 * title.c
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
#include "title.h"
#include "gamecard.h"

#define NS_APPLICATION_RECORD_LIMIT 4096

/* Global variables. */

static Mutex g_titleMutex = 0;
static bool g_titleInterfaceInit = false, g_titleGameCardAvailable = false;

static NsApplicationControlData *g_nsAppControlData = NULL;

static TitleApplicationMetadata *g_appMetadata = NULL;
static u32 g_appMetadataCount = 0;

static NcmContentMetaDatabase g_ncmDbGameCard = {0}, g_ncmDbEmmcSystem = {0}, g_ncmDbEmmcUser = {0}, g_ncmDbSdCard = {0};
static NcmContentStorage g_ncmStorageGameCard = {0}, g_ncmStorageEmmcSystem = {0}, g_ncmStorageEmmcUser = {0}, g_ncmStorageSdCard = {0};

static TitleInfo *g_titleInfo = NULL;
static u32 g_titleInfoCount = 0;

/* Function prototypes. */

static bool titleRetrieveApplicationMetadataFromNsRecords(void);
static bool titleRetrieveApplicationMetadataByTitleId(u64 title_id, TitleApplicationMetadata *out);

static bool titleOpenNcmDatabases(void);
static void titleCloseNcmDatabases(void);

static bool titleOpenNcmStorages(void);
static void titleCloseNcmStorages(void);

static bool titleLoadTitleInfo(void);
static bool titleRetrieveContentMetaKeysFromDatabase(u8 storage_id, NcmContentMetaDatabase *ncm_db);

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id);

bool titleInitialize(void)
{
    mutexLock(&g_titleMutex);
    
    bool ret = g_titleInterfaceInit;
    if (ret) goto end;
    
    
    
    
    
    while(!gamecardGetStatusChangeUserEvent());
    
    
    
    
    /* Allocate memory for the ns application control data. */
    /* This will be used each time we need to retrieve application metadata. */
    g_nsAppControlData = calloc(1, sizeof(NsApplicationControlData));
    if (!g_nsAppControlData)
    {
        LOGFILE("Failed to allocate memory for the ns application control data!");
        goto end;
    }
    
    /* Retrieve application metadata from ns records. */
    /* Theoretically speaking, we should only need to do this once. */
    /* However, if any new gamecard is inserted while the application is running, we *will* have to retrieve the metadata from its application(s). */
    if (!titleRetrieveApplicationMetadataFromNsRecords())
    {
        LOGFILE("Failed to retrieve application metadata from ns records!");
        goto end;
    }
    
    /* Open eMMC System, eMMC User and SD card ncm databases. */
    if (!titleOpenNcmDatabases())
    {
        LOGFILE("Failed to open ncm databases!");
        goto end;
    }
    
    /* Open eMMC System, eMMC User and SD card ncm storages. */
    if (!titleOpenNcmStorages())
    {
        LOGFILE("Failed to open ncm storages!");
        goto end;
    }
    
    /* Load title info by retrieving content meta keys from available eMMC System, eMMC User and SD card titles. */
    if (!titleLoadTitleInfo())
    {
        LOGFILE("Failed to load title info!");
        goto end;
    }
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    if (g_titleInfo && g_titleInfoCount)
    {
        mkdir("sdmc:/records", 0777);
        
        FILE *title_infos_txt = NULL, *icon_jpg = NULL;
        char icon_path[FS_MAX_PATH] = {0};
        
        title_infos_txt = fopen("sdmc:/records/title_infos.txt", "wb");
        if (title_infos_txt)
        {
            for(u32 i = 0; i < g_titleInfoCount; i++)
            {
                TitleVersion version;
                memcpy(&version, &(g_titleInfo[i].meta_key.version), sizeof(u32));
                
                fprintf(title_infos_txt, "Storage ID: 0x%02X\r\n", g_titleInfo[i].storage_id);
                fprintf(title_infos_txt, "Title ID: %016lX\r\n", g_titleInfo[i].meta_key.id);
                fprintf(title_infos_txt, "Version: %u (%u.%u.%u-%u.%u)\r\n", g_titleInfo[i].meta_key.version, version.TitleVersion_Major, version.TitleVersion_Minor, version.TitleVersion_Micro, \
                        version.TitleVersion_MajorRelstep, version.TitleVersion_MinorRelstep);
                fprintf(title_infos_txt, "Type: 0x%02X\r\n", g_titleInfo[i].meta_key.type);
                fprintf(title_infos_txt, "Install Type: 0x%02X\r\n", g_titleInfo[i].meta_key.install_type);
                fprintf(title_infos_txt, "Title Size: 0x%lX\r\n", g_titleInfo[i].title_size);
                
                if (g_titleInfo[i].app_metadata)
                {
                    fprintf(title_infos_txt, "Application Name: %s\r\n", g_titleInfo[i].app_metadata->lang_entry.name);
                    fprintf(title_infos_txt, "Application Author: %s\r\n", g_titleInfo[i].app_metadata->lang_entry.author);
                    fprintf(title_infos_txt, "JPEG Icon Size: 0x%X\r\n", g_titleInfo[i].app_metadata->icon_size);
                    
                    if (g_titleInfo[i].app_metadata->icon_size)
                    {
                        sprintf(icon_path, "sdmc:/records/%016lX.jpg", g_titleInfo[i].app_metadata->title_id);
                        icon_jpg = fopen(icon_path, "wb");
                        if (icon_jpg)
                        {
                            fwrite(g_titleInfo[i].app_metadata->icon, 1, g_titleInfo[i].app_metadata->icon_size, icon_jpg);
                            fclose(icon_jpg);
                            icon_jpg = NULL;
                        }
                    }
                }
                
                fprintf(title_infos_txt, "\r\n");
                
                fflush(title_infos_txt);
            }
            
            fclose(title_infos_txt);
            title_infos_txt = NULL;
        }
    }
    
    
    
    
    
    
    
    
    
    
    
    
    ret = g_titleInterfaceInit = true;
    
end:
    mutexUnlock(&g_titleMutex);
    
    return ret;
}

void titleExit(void)
{
    mutexLock(&g_titleMutex);
    
    
    
    
    
    
    
    
    
    
    
    
    /* Free title info. */
    if (g_titleInfo) free(g_titleInfo);
    g_titleInfoCount = 0;
    
    /* Close eMMC System, eMMC User and SD card ncm storages. */
    titleCloseNcmStorages();
    
    /* Close eMMC System, eMMC User and SD card ncm databases. */
    titleCloseNcmDatabases();
    
    /* Free application metadata. */
    if (g_appMetadata) free(g_appMetadata);
    g_appMetadataCount = 0;
    
    /* Free ns application control data. */
    if (g_nsAppControlData) free(g_nsAppControlData);
    
    g_titleInterfaceInit = false;
    
    mutexUnlock(&g_titleMutex);
}

NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    switch(storage_id)
    {
        case NcmStorageId_GameCard:
            ncm_db = &g_ncmDbGameCard;
            break;
        case NcmStorageId_BuiltInSystem:
            ncm_db = &g_ncmDbEmmcSystem;
            break;
        case NcmStorageId_BuiltInUser:
            ncm_db = &g_ncmDbEmmcUser;
            break;
        case NcmStorageId_SdCard:
            ncm_db = &g_ncmDbSdCard;
            break;
        default:
            break;
    }
    
    return ncm_db;
}

NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id)
{
    NcmContentStorage *ncm_storage = NULL;
    
    switch(storage_id)
    {
        case NcmStorageId_GameCard:
            ncm_storage = &g_ncmStorageGameCard;
            break;
        case NcmStorageId_BuiltInSystem:
            ncm_storage = &g_ncmStorageEmmcSystem;
            break;
        case NcmStorageId_BuiltInUser:
            ncm_storage = &g_ncmStorageEmmcUser;
            break;
        case NcmStorageId_SdCard:
            ncm_storage = &g_ncmStorageSdCard;
            break;
        default:
            break;
    }
    
    return ncm_storage;
}






















static bool titleRetrieveApplicationMetadataFromNsRecords(void)
{
    /* Return right away if application metadata has already been retrieved. */
    if (g_appMetadata || g_appMetadataCount) return true;
    
    Result rc = 0;
    
    NsApplicationRecord *app_records = NULL;
    u32 app_records_count = 0;
    
    bool success = false;
    
    /* Allocate memory for the ns application records. */
    app_records = calloc(NS_APPLICATION_RECORD_LIMIT, sizeof(NsApplicationRecord));
    if (!app_records)
    {
        LOGFILE("Failed to allocate memory for ns application records!");
        goto end;
    }
    
    /* Retrieve ns application records. */
    rc = nsListApplicationRecord(app_records, NS_APPLICATION_RECORD_LIMIT, 0, (s32*)&app_records_count);
    if (R_FAILED(rc))
    {
        LOGFILE("nsListApplicationRecord failed! (0x%08X).", rc);
        goto end;
    }
    
    /* Return right away if no records were retrieved. */
    if (!app_records_count)
    {
        success = true;
        goto end;
    }
    
    /* Allocate memory for the application metadata. */
    g_appMetadata = calloc(app_records_count, sizeof(TitleApplicationMetadata));
    if (!g_appMetadata)
    {
        LOGFILE("Failed to allocate memory for application metadata! (%u %s).", app_records_count, app_records_count > 1 ? "entries" : "entry");
        goto end;
    }
    
    /* Retrieve application metadata for each ns application record. */
    g_appMetadataCount = 0;
    for(u32 i = 0; i < app_records_count; i++)
    {
        if (!titleRetrieveApplicationMetadataByTitleId(app_records[i].application_id, &(g_appMetadata[g_appMetadataCount]))) continue;
        g_appMetadataCount++;
    }
    
    /* Check retrieved application metadata count. */
    if (!g_appMetadataCount)
    {
        LOGFILE("Unable to retrieve application metadata from ns application records! (%u %s).", app_records_count, app_records_count > 1 ? "entries" : "entry");
        goto end;
    }
    
    /* Decrease buffer size if needed. */
    if (g_appMetadataCount < app_records_count)
    {
        TitleApplicationMetadata *tmp_app_metadata = realloc(g_appMetadata, g_appMetadataCount * sizeof(TitleApplicationMetadata));
        if (!tmp_app_metadata)
        {
            LOGFILE("Failed to reallocate application metadata buffer! (%u %s).", g_appMetadataCount, g_appMetadataCount > 1 ? "entries" : "entry");
            goto end;
        }
        
        g_appMetadata = tmp_app_metadata;
        tmp_app_metadata = NULL;
    }
    
    success = true;
    
end:
    if (!success)
    {
        if (g_appMetadata)
        {
            free(g_appMetadata);
            g_appMetadata = NULL;
        }
        
        g_appMetadataCount = 0;
    }
    
    if (app_records) free(app_records);
    
    return success;
}

static bool titleRetrieveApplicationMetadataByTitleId(u64 title_id, TitleApplicationMetadata *out)
{
    if (!g_nsAppControlData || !title_id || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 write_size = 0;
    NacpLanguageEntry *lang_entry = NULL;
    
    /* Retrieve ns application control data. */
    rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, title_id, g_nsAppControlData, sizeof(NsApplicationControlData), &write_size);
    if (R_FAILED(rc))
    {
        LOGFILE("nsGetApplicationControlData failed for title ID \"%016lX\"! (0x%08X).", rc, title_id);
        return false;
    }
    
    if (write_size < sizeof(NacpStruct))
    {
        LOGFILE("Retrieved application control data buffer is too small! (0x%lX).", write_size);
        return false;
    }
    
    /* Get language entry. */
    rc = nacpGetLanguageEntry(&(g_nsAppControlData->nacp), &lang_entry);
    if (R_FAILED(rc))
    {
        LOGFILE("nacpGetLanguageEntry failed! (0x%08X).", rc);
        return false;
    }
    
    /* Copy data. */
    out->title_id = title_id;
    
    memcpy(&(out->lang_entry), lang_entry, sizeof(NacpLanguageEntry));
    utilsTrimString(out->lang_entry.name);
    utilsTrimString(out->lang_entry.author);
    
    out->icon_size = (write_size - sizeof(NacpStruct));
    memcpy(out->icon, g_nsAppControlData->icon, sizeof(g_nsAppControlData->icon));
    
    return true;
}

static bool titleOpenNcmDatabases(void)
{
    Result rc = 0;
    NcmContentMetaDatabase *ncm_db = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm database pointer. */
        ncm_db = titleGetNcmDatabaseByStorageId(i);
        if (!ncm_db)
        {
            LOGFILE("Failed to retrieve ncm database pointer for storage ID %u!", i);
            return false;
        }
        
        /* Check if the ncm database handle has already been retrieved. */
        if (serviceIsActive(&(ncm_db->s))) continue;
        
        /* Open ncm database. */
        rc = ncmOpenContentMetaDatabase(ncm_db, i);
        if (R_FAILED(rc))
        {
            /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
            /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
            LOGFILE("ncmOpenContentMetaDatabase failed for storage ID %u! (0x%08X).", i, rc);
            if (i == NcmStorageId_SdCard && rc == 0x21005) continue;
            return false;
        }
    }
    
    return true;
}

static void titleCloseNcmDatabases(void)
{
    NcmContentMetaDatabase *ncm_db = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm database pointer. */
        ncm_db = titleGetNcmDatabaseByStorageId(i);
        if (!ncm_db) continue;
        
        /* Check if the ncm database handle has already been retrieved. */
        if (serviceIsActive(&(ncm_db->s))) ncmContentMetaDatabaseClose(ncm_db);
    }
}

static bool titleOpenNcmStorages(void)
{
    Result rc = 0;
    NcmContentStorage *ncm_storage = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm storage pointer. */
        ncm_storage = titleGetNcmStorageByStorageId(i);
        if (!ncm_storage)
        {
            LOGFILE("Failed to retrieve ncm storage pointer for storage ID %u!", i);
            return false;
        }
        
        /* Check if the ncm storage handle has already been retrieved. */
        if (serviceIsActive(&(ncm_storage->s))) continue;
        
        /* Open ncm storage. */
        rc = ncmOpenContentStorage(ncm_storage, i);
        if (R_FAILED(rc))
        {
            /* If the SD card is mounted, but it isn't currently being used by HOS, 0x21005 will be returned, so we'll just filter this particular error and continue. */
            /* This can occur when using the "Nintendo" directory from a different console, or when the "sdmc:/Nintendo/Contents/private" file is corrupted. */
            LOGFILE("ncmOpenContentStorage failed for storage ID %u! (0x%08X).", i, rc);
            if (i == NcmStorageId_SdCard && rc == 0x21005) continue;
            return false;
        }
    }
    
    return true;
}

static void titleCloseNcmStorages(void)
{
    NcmContentStorage *ncm_storage = NULL;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm storage pointer. */
        ncm_storage = titleGetNcmStorageByStorageId(i);
        if (!ncm_storage) continue;
        
        /* Check if the ncm storage handle has already been retrieved. */
        if (serviceIsActive(&(ncm_storage->s))) ncmContentStorageClose(ncm_storage);
    }
}

static bool titleLoadTitleInfo(void)
{
    /* Return right away if title info has already been retrieved. */
    if (g_titleInfo || g_titleInfoCount) return true;
    
    NcmContentMetaDatabase *ncm_db = NULL;
    
    g_titleInfoCount = 0;
    
    for(u8 i = NcmStorageId_BuiltInSystem; i <= NcmStorageId_SdCard; i++)
    {
        /* Retrieve ncm database pointer. */
        ncm_db = titleGetNcmDatabaseByStorageId(i);
        if (!ncm_db) continue;
        
        /* Check if the ncm database handle has already been retrieved. */
        if (!serviceIsActive(&(ncm_db->s))) continue;
        
        /* Retrieve content meta keys from this ncm database. */
        if (!titleRetrieveContentMetaKeysFromDatabase(i, ncm_db))
        {
            LOGFILE("Failed to retrieve content meta keys from storage ID %u!", i);
            return false;
        }
    }
    
    return true;
}

static bool titleRetrieveContentMetaKeysFromDatabase(u8 storage_id, NcmContentMetaDatabase *ncm_db)
{
    if (storage_id < NcmStorageId_GameCard || storage_id > NcmStorageId_SdCard || !ncm_db || !serviceIsActive(&(ncm_db->s)))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    
    u32 written = 0, total = 0;
    NcmContentMetaKey *meta_keys = NULL, *meta_keys_tmp = NULL;
    size_t meta_keys_size = sizeof(NcmContentMetaKey);
    
    TitleInfo *tmp_title_info = NULL;
    
    bool success = false;
    
    /* Allocate memory for the ncm application content meta keys. */
    meta_keys = calloc(1, meta_keys_size);
    if (!meta_keys)
    {
        LOGFILE("Unable to allocate memory for the ncm application meta keys!");
        goto end;
    }
    
    /* Get a full list of all titles available in this storage. */
    /* Meta type '0' means all title types will be retrieved. */
    rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, 1, 0, 0, 0, -1, NcmContentInstallType_Full);
    if (R_FAILED(rc))
    {
        LOGFILE("ncmContentMetaDatabaseList failed! (0x%08X) (first entry).", rc);
        goto end;
    }
    
    /* Check if our application meta keys buffer was actually filled. */
    /* If it wasn't, odds are there are no titles in this storage. */
    if (!written || !total)
    {
        success = true;
        goto end;
    }
    
    /* Check if we need to resize our application meta keys buffer. */
    if (total > written)
    {
        /* Update application meta keys buffer size. */
        meta_keys_size *= total;
        
        /* Reallocate application meta keys buffer. */
        meta_keys_tmp = realloc(meta_keys, meta_keys_size);
        if (!meta_keys_tmp)
        {
            LOGFILE("Unable to reallocate application meta keys buffer! (%u entries).", total);
            goto end;
        }
        
        meta_keys = meta_keys_tmp;
        meta_keys_tmp = NULL;
        
        /* Issue call again. */
        rc = ncmContentMetaDatabaseList(ncm_db, (s32*)&total, (s32*)&written, meta_keys, (s32)total, 0, 0, 0, -1, NcmContentInstallType_Full);
        if (R_FAILED(rc))
        {
            LOGFILE("ncmContentMetaDatabaseList failed! (0x%08X) (%u %s).", rc, total, total > 1 ? "entries" : "entry");
            goto end;
        }
        
        /* Safety check. */
        if (written != total)
        {
            LOGFILE("Application meta key count mismatch! (%u != %u).", written, total);
            goto end;
        }
    }
    
    /* Reallocate title info buffer. */
    /* If g_titleInfo == NULL, realloc() will essentially act as a malloc(). */
    tmp_title_info = realloc(g_titleInfo, (g_titleInfoCount + total) * sizeof(TitleInfo));
    if (!tmp_title_info)
    {
        LOGFILE("Unable to reallocate title info buffer! (%u %s).", g_titleInfoCount + total, (g_titleInfoCount + total) > 1 ? "entries" : "entry");
        goto end;
    }
    
    g_titleInfo = tmp_title_info;
    tmp_title_info = NULL;
    
    /* Clear new title info buffer area. */
    memset(g_titleInfo + g_titleInfoCount, 0, total * sizeof(TitleInfo));
    
    /* Fill new title info entries. */
    for(u32 i = 0; i < total; i++)
    {
        TitleInfo *cur_title_info = &(g_titleInfo[g_titleInfoCount + i]);
        
        cur_title_info->storage_id = storage_id;
        memcpy(&(cur_title_info->meta_key), &(meta_keys[i]), sizeof(NcmContentMetaKey));
        /* TO DO: RETRIEVE TITLE SIZE HERE. */
        cur_title_info->app_metadata = titleFindApplicationMetadataByTitleId(meta_keys[i].id);
    }
    
    /* Update title info count. */
    g_titleInfoCount += total;
    
    success = true;
    
end:
    if (meta_keys) free(meta_keys);
    
    return success;
}

NX_INLINE TitleApplicationMetadata *titleFindApplicationMetadataByTitleId(u64 title_id)
{
    if (!g_appMetadata || !g_appMetadataCount || !title_id) return NULL;
    
    for(u32 i = 0; i < g_appMetadataCount; i++)
    {
        if (g_appMetadata[i].title_id == title_id) return &(g_appMetadata[i]);
    }
    
    return NULL;
}
