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

#define TITLE_PATCH_TYPE_VALUE              (u64)0x800

#define TITLE_ADDONCONTENT_TYPE_VALUE       (u64)0x1000
#define TITLE_ADDONCONTENT_CONVERSION_MASK  (u64)0xFFFFFFFFFFFFF000
#define TITLE_ADDONCONTENT_MAX_ENTRIES      2000

#define TITLE_DELTA_TYPE_VALUE              (u64)0xC00

/// Used to display version numbers in dot notation (major.minor.micro-major_relstep.minor_relstep).
typedef struct {
    u32 TitleVersion_MinorRelstep : 8;
    u32 TitleVersion_MajorRelstep : 8;
    u32 TitleVersion_Micro        : 4;
    u32 TitleVersion_Minor        : 6;
    u32 TitleVersion_Major        : 6;
} TitleVersion;

/// Retrieved using ns application records and/or ncm content meta keys.
/// Used by the UI to display title lists.
typedef struct {
    u64 title_id;                   ///< Title ID from the application / system title this data belongs to.
    NacpLanguageEntry lang_entry;   ///< UTF-8 strings in the console language.
    u32 icon_size;                  ///< JPEG icon size.
    u8 *icon;                       ///< JPEG icon data.
} TitleApplicationMetadata;

/// Retrieved using ncm databases.
typedef struct _TitleInfo {
    u8 storage_id;                                  ///< NcmStorageId.
    TitleVersion dot_version;                       ///< Holds the same value from meta_key.version.
    NcmContentMetaKey meta_key;                     ///< Used with ncm calls.
    u32 content_count;                              ///< Content info count.
    NcmContentInfo *content_infos;                  ///< Content info entries from this title.
    u64 title_size;                                 ///< Total title size.
    char title_size_str[32];                        ///< Total title size string.
    TitleApplicationMetadata *app_metadata;         ///< Only available for system titles and applications.
    struct _TitleInfo *parent, *previous, *next;    ///< Used with TitleInfo entries from patches and add-on contents.
} TitleInfo;

/// Used to deal with user applications stored in the eMMC, SD card and/or gamecard.
typedef struct {
    bool gamecard_available;    ///< Set to true if one or more titles matching this user application are stored in the inserted gamecard.
    TitleInfo *app_info;        ///< Pointer to a TitleInfo element for this application.
    TitleInfo *patch_info;      ///< Pointer to a TitleInfo element for the first detected patch.
    TitleInfo *aoc_info;        ///< Pointer to a TitleInfo element for the first detected add-on content.
} TitleUserApplicationData;

/// Initializes the title interface.
bool titleInitialize(void);

/// Closes the title interface.
void titleExit(void);

/// Returns a pointer to a ncm database handle using a NcmStorageId value.
NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id);

/// Returns a pointer to a ncm storage handle using a NcmStorageId value.
NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id);

/// Returns a pointer to a dynamically allocated buffer of pointers to TitleApplicationMetadata entries, as well as their count. The allocated buffer must be freed by the calling function.
/// If 'is_system' is true, TitleApplicationMetadata entries from available system titles (NcmStorageId_BuiltInSystem) will be returned.
/// Otherwise, TitleApplicationMetadata entries from user applications with available content data (NcmStorageId_Any) will be returned.
/// Returns NULL if an error occurs.
TitleApplicationMetadata **titleGetApplicationMetadataEntries(bool is_system, u32 *out_count);

/// Returns a pointer to a TitleInfo entry with a matching storage ID and title ID.
/// If NcmStorageId_Any is used, the first entry with a matching title ID is returned.
/// Returns NULL if an error occurs.
TitleInfo *titleGetInfoFromStorageByTitleId(u8 storage_id, u64 title_id);

/// Populates a TitleUserApplicationData element using an user application ID.
bool titleGetUserApplicationData(u64 app_id, TitleUserApplicationData *out);

/// Returns true if orphan titles are available.
/// Orphan titles are patches or add-on contents with no NsApplicationControlData available for its parent user application ID.
bool titleAreOrphanTitlesAvailable(void);

/// Returns a pointer to a dynamically allocated buffer of pointers to TitleInfo entries from orphan titles, as well as their count. The allocated buffer must be freed by the calling function.
/// Returns NULL if an error occurs.
TitleInfo **titleGetInfoFromOrphanTitles(u32 *out_count);

/// Returns true if the gamecard title info entries have been updated (e.g. after a new gamecard has been inserted, of after the current one has been taken out).
/// If titleGetApplicationMetadataEntries() has been previously called, its returned buffer should be freed and a new titleGetApplicationMetadataEntries() call should be issued.
bool titleIsGameCardInfoUpdated(void);

/// Returns a pointer to a string holding the name of the provided ncm content type.
const char *titleGetNcmContentTypeName(u8 content_type);

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
    return (app_id + TITLE_PATCH_TYPE_VALUE);
}

NX_INLINE u64 titleGetApplicationIdByPatchId(u64 patch_id)
{
    return (patch_id - TITLE_PATCH_TYPE_VALUE);
}

NX_INLINE bool titleCheckIfPatchIdBelongsToApplicationId(u64 app_id, u64 patch_id)
{
    return (patch_id == titleGetPatchIdByApplicationId(app_id));
}

NX_INLINE u64 titleGetAddOnContentBaseIdByApplicationId(u64 app_id)
{
    return ((app_id & TITLE_ADDONCONTENT_CONVERSION_MASK) + TITLE_ADDONCONTENT_TYPE_VALUE);
}

NX_INLINE u64 titleGetAddOnContentIdWithIndexByApplicationId(u64 app_id, u16 idx)
{
    return (titleGetAddOnContentBaseIdByApplicationId(app_id) + idx + 1);
}

NX_INLINE u64 titleGetApplicationIdByAddOnContentId(u64 aoc_id)
{
    return ((aoc_id - TITLE_ADDONCONTENT_TYPE_VALUE) & TITLE_ADDONCONTENT_CONVERSION_MASK);
}

NX_INLINE u64 titleGetAddOnContentMaxIdByBaseId(u64 aoc_base_id)
{
    return (aoc_base_id + TITLE_ADDONCONTENT_MAX_ENTRIES + 1);
}

NX_INLINE bool titleIsAddOnContentIdValid(u64 aoc_id, u64 aoc_base_id, u64 aoc_max_id)
{
    return (aoc_id > aoc_base_id && aoc_id < aoc_max_id);
}

NX_INLINE bool titleCheckIfAddOnContentIdBelongsToApplicationId(u64 app_id, u64 aoc_id)
{
    u64 aoc_base_id = titleGetAddOnContentBaseIdByApplicationId(app_id);
    u64 aoc_max_id = titleGetAddOnContentMaxIdByBaseId(aoc_base_id);
    return titleIsAddOnContentIdValid(aoc_id, aoc_base_id, aoc_max_id);
}

NX_INLINE bool titleCheckIfAddOnContentIdsAreSiblings(u64 aoc_id_1, u64 aoc_id_2)
{
    u64 app_id_1 = titleGetApplicationIdByAddOnContentId(aoc_id_1);
    u64 app_id_2 = titleGetApplicationIdByAddOnContentId(aoc_id_2);
    return (app_id_1 == app_id_2 && titleCheckIfAddOnContentIdBelongsToApplicationId(app_id_1, aoc_id_1) && titleCheckIfAddOnContentIdBelongsToApplicationId(app_id_2, aoc_id_2));
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
