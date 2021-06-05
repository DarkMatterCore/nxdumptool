/*
 * bfttf.c
 *
 * Copyright (c) 2018, simontime.
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include "nxdt_utils.h"
#include "bfttf.h"
#include "romfs.h"
#include "title.h"

/* Type definitions. */

typedef struct {
    u64 title_id;   ///< System title ID.
    char path[64];  ///< Path to BFTTF file inside the RomFS section from the system title.
    u32 size;
    u8 *data;
} BfttfFontInfo;

/* Global variables. */

static Mutex g_bfttfMutex = 0;
static bool g_bfttfInterfaceInit = false;

static BfttfFontInfo g_fontInfo[] = {
    { 0x0100000000000811, "/nintendo_udsg-r_std_003.bfttf", 0, NULL },          /* FontStandard. */
    { 0x0100000000000810, "/nintendo_ext_003.bfttf", 0, NULL },                 /* FontNintendoExtension. There's a secondary entry at "/nintendo_ext2_003.bfttf", but it's identical to this one. */
    { 0x0100000000000812, "/nintendo_udsg-r_ko_003.bfttf", 0, NULL },           /* FontKorean. */
    { 0x0100000000000814, "/nintendo_udsg-r_org_zh-cn_003.bfttf", 0, NULL },    /* FontChineseSimplified (1). */
    { 0x0100000000000814, "/nintendo_udsg-r_ext_zh-cn_003.bfttf", 0, NULL },    /* FontChineseSimplified (2). */
    { 0x0100000000000813, "/nintendo_udjxh-db_zh-tw_003.bfttf", 0, NULL }       /* FontChineseTraditional. */
};

static const u32 g_fontInfoCount = MAX_ELEMENTS(g_fontInfo);

static const u32 g_bfttfKey = 0x06186249;

/* Function prototypes. */

static bool bfttfDecodeFont(BfttfFontInfo *font_info);

bool bfttfInitialize(void)
{
    NcaContext *nca_ctx = NULL;
    RomFileSystemContext romfs_ctx = {0};
    bool ret = false;
    
    SCOPED_LOCK(&g_bfttfMutex)
    {
        ret = g_bfttfInterfaceInit;
        if (ret) break;
        
        u32 count = 0;
        u64 prev_title_id = 0;
        
        /* Allocate memory for a temporary NCA context. */
        nca_ctx = calloc(1, sizeof(NcaContext));
        if (!nca_ctx)
        {
            LOG_MSG("Failed to allocate memory for temporary NCA context!");
            break;
        }
        
        /* Retrieve BFTTF data. */
        for(u32 i = 0; i < g_fontInfoCount; i++)
        {
            BfttfFontInfo *font_info = &(g_fontInfo[i]);
            TitleInfo *title_info = NULL;
            RomFileSystemFileEntry *romfs_file_entry = NULL;
            
            /* Check if the title ID for the current font container matches the one from the previous font container. */
            /* We won't have to reinitialize both NCA and RomFS contexts if that's the case. */
            if (font_info->title_id != prev_title_id)
            {
                /* Get title info. */
                if (!(title_info = titleGetInfoFromStorageByTitleId(NcmStorageId_BuiltInSystem, font_info->title_id)))
                {
                    LOG_MSG("Failed to get title info for %016lX!", font_info->title_id);
                    continue;
                }
                
                /* Initialize NCA context. */
                /* NCA contexts don't need to be freed beforehand. */
                bool nca_ctx_init = ncaInitializeContext(nca_ctx, NcmStorageId_BuiltInSystem, 0, titleGetContentInfoByTypeAndIdOffset(title_info, NcmContentType_Data, 0), NULL);
                
                /* Free title info. */
                titleFreeTitleInfo(&title_info);
                
                /* Check if NCA context initialization succeeded. */
                if (!nca_ctx_init)
                {
                    LOG_MSG("Failed to initialize Data NCA context for %016lX!", font_info->title_id);
                    continue;
                }
                
                /* Initialize RomFS context. */
                /* This will also free a previous RomFS context, if available. */
                if (!romfsInitializeContext(&romfs_ctx, &(nca_ctx->fs_ctx[0])))
                {
                    LOG_MSG("Failed to initialize RomFS context for Data NCA from %016lX!", font_info->title_id);
                    continue;
                }
                
                /* Update previous title ID. */
                prev_title_id = font_info->title_id;
            }
            
            /* Get RomFS file entry. */
            if (!(romfs_file_entry = romfsGetFileEntryByPath(&romfs_ctx, font_info->path)))
            {
                LOG_MSG("Failed to retrieve RomFS file entry in %016lX!", font_info->title_id);
                continue;
            }
            
            /* Check file size. */
            if (!romfs_file_entry->size)
            {
                LOG_MSG("File size for \"%s\" in %016lX is zero!", font_info->path, font_info->title_id);
                continue;
            }
            
            /* Allocate memory for BFTTF data. */
            if (!(font_info->data = malloc(romfs_file_entry->size)))
            {
                LOG_MSG("Failed to allocate 0x%lX bytes for \"%s\" in %016lX!", romfs_file_entry->size, font_info->path, font_info->title_id);
                continue;
            }
            
            /* Read BFTFF data. */
            if (!romfsReadFileEntryData(&romfs_ctx, romfs_file_entry, font_info->data, romfs_file_entry->size, 0))
            {
                LOG_MSG("Failed to read 0x%lX bytes long \"%s\" in %016lX!", romfs_file_entry->size, font_info->path, font_info->title_id);
                free(font_info->data);
                font_info->data = NULL;
                continue;
            }
            
            /* Update BFTTF size. */
            font_info->size = (u32)romfs_file_entry->size;
            
            /* Decode BFTTF data. */
            if (!bfttfDecodeFont(font_info))
            {
                LOG_MSG("Failed to decode 0x%lX bytes long \"%s\" in %016lX!", romfs_file_entry->size, font_info->path, font_info->title_id);
                free(font_info->data);
                font_info->data = NULL;
                font_info->size = 0;
                continue;
            }
            
            /* Increase retrieved BFTTF count. */
            count++;
        }
        
        /* Update flags. */
        ret = g_bfttfInterfaceInit = (count > 0);
        if (!ret) LOG_MSG("No BFTTF fonts retrieved!");
    }
    
    romfsFreeContext(&romfs_ctx);
    
    if (nca_ctx) free(nca_ctx);
    
    return ret;
}

void bfttfExit(void)
{
    SCOPED_LOCK(&g_bfttfMutex)
    {
        /* Free BFTTF data. */
        for(u32 i = 0; i < g_fontInfoCount; i++)
        {
            BfttfFontInfo *font_info = &(g_fontInfo[i]);
            
            font_info->size = 0;
            
            if (font_info->data)
            {
                free(font_info->data);
                font_info->data = NULL;
            }
        }
        
        g_bfttfInterfaceInit = false;
    }
}

bool bfttfGetFontByType(BfttfFontData *font_data, u8 font_type)
{
    if (!font_data || font_type >= BfttfFontType_Total)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    bool ret = false;
    
    SCOPED_LOCK(&g_bfttfMutex)
    {
        BfttfFontInfo *font_info = &(g_fontInfo[font_type]);
        if (font_info->size <= 8 || !font_info->data)
        {
            LOG_MSG("BFTTF font data unavailable for type 0x%02X!", font_type);
            break;
        }
        
        font_data->type = font_type;
        font_data->size = (font_info->size - 8);
        font_data->address = (font_info->data + 8);
        
        ret = true;
    }
    
    return ret;
}

static bool bfttfDecodeFont(BfttfFontInfo *font_info)
{
    if (!font_info || font_info->size <= 8 || !IS_ALIGNED(font_info->size, 4) || !font_info->data)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    for(u32 i = 8; i < font_info->size; i += 4)
    {
        u32 *ptr = (u32*)(font_info->data + i);
        *ptr = (*ptr ^ g_bfttfKey);
    }
    
    return true;
}
