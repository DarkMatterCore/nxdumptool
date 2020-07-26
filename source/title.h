/*
 * title.h
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

#pragma once

#ifndef __TITLE_H__
#define __TITLE_H__

#define TITLE_PATCH_ID_MASK         (u64)0x800
#define TITLE_ADDONCONTENT_ID_MASK  (u64)0xFFFFFFFFFFFF0000

typedef struct {
    u32 TitleVersion_MinorRelstep : 8;
    u32 TitleVersion_MajorRelstep : 8;
    u32 TitleVersion_Micro        : 4;
    u32 TitleVersion_Minor        : 6;
    u32 TitleVersion_Major        : 6;
} TitleVersion;

/// Retrieved using ns application records and/or ncm content meta keys.
typedef struct {
    u64 title_id;                   ///< Title ID from the application this data belongs to.
    NacpLanguageEntry lang_entry;   ///< UTF-8 strings in the console language.
    u32 icon_size;                  ///< JPEG icon size.
    u8 icon[0x20000];               ///< JPEG icon data.
} TitleApplicationMetadata;

/// Retrieved using ncm databases.
typedef struct {
    u8 storage_id;                          ///< NcmStorageId.
    TitleVersion dot_version;               ///< Holds the same value from meta_key.version. Used to display version numbers in dot notation (major.minor.micro-major_relstep.minor_relstep).
    NcmContentMetaKey meta_key;             ///< Used with ncm calls.
    u32 content_count;                      ///< Content info count.
    NcmContentInfo *content_infos;          ///< Content info entries from this title.
    u64 title_size;                         ///< Total title size.
    char title_size_str[32];                ///< Total title size string.
    TitleApplicationMetadata *app_metadata; ///< Only available for applications.
    
    
    /* Pointers to patches / AOC? */
    
    
} TitleInfo;

/// Initializes the title interface.
bool titleInitialize(void);

/// Closes the title interface.
void titleExit(void);

/// Returns a pointer to a ncm database handle using a NcmStorageId value.
NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id);

/// Returns a pointer to a ncm storage handle using a NcmStorageId value.
NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id);

/// Returns true if gamecard title info could be loaded.
/// Suitable for being called between UI updates.
bool titleRefreshGameCardTitleInfo(void);

/// Retrieves a pointer to a TitleInfo entry with a matching storage ID and title ID.
/// If NcmStorageId_Any is used, the first entry with a matching title ID is returned.
/// Returns NULL if an error occurs.
TitleInfo *titleGetInfoFromStorageByTitleId(u8 storage_id, u64 title_id);











/// Miscellaneous functions.

NX_INLINE void titleConvertNcmContentSizeToU64(const u8 *size, u64 *out)
{
    if (!size || !out) return;
    *out = 0;
    memcpy(out, size, 6);
}

NX_INLINE void titleConvertU64ToNcmContentSize(const u64 *size, u8 *out)
{
    if (size && out) memcpy(out, size, 6);
}

NX_INLINE u64 titleGetPatchIdByApplicationId(u64 app_id)
{
    return (app_id | TITLE_PATCH_ID_MASK);
}

NX_INLINE u64 titleGetApplicationIdByPatchId(u64 patch_id)
{
    return (patch_id & ~TITLE_PATCH_ID_MASK);
}

NX_INLINE bool titleCheckIfPatchIdBelongsToApplicationId(u64 app_id, u64 patch_id)
{
    return (titleGetPatchIdByApplicationId(app_id) == patch_id);
}

NX_INLINE bool titleCheckIfAddOnContentIdBelongsToApplicationId(u64 app_id, u64 aoc_id)
{
    return ((app_id & TITLE_ADDONCONTENT_ID_MASK) == (aoc_id & TITLE_ADDONCONTENT_ID_MASK));
}

NX_INLINE NcmContentInfo *titleGetContentInfoByTypeAndIdOffset(TitleInfo *info, u8 content_type, u8 id_offset)
{
    if (!info || !info->content_count || !info->content_infos || content_type > NcmContentType_DeltaFragment) return NULL;
    
    for(u32 i = 0; i < info->content_count; i++)
    {
        if (info->content_infos[i].content_type == content_type && info->content_infos[i].id_offset == id_offset) return &(info->content_infos[i]);
    }
    
    return NULL;
}

#endif /* __TITLE_H__ */
