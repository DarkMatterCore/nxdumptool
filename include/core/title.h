/*
 * title.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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

#pragma once

#ifndef __TITLE_H__
#define __TITLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define TITLE_PATCH_TYPE_VALUE              (u64)0x800

#define TITLE_ADDONCONTENT_TYPE_VALUE       (u64)0x1000
#define TITLE_ADDONCONTENT_CONVERSION_MASK  (u64)0xFFFFFFFFFFFFF000
#define TITLE_ADDONCONTENT_MAX_ENTRIES      2000

#define TITLE_DELTA_TYPE_VALUE              (u64)0xC00

/// Generated using ns application records and/or ncm content meta keys.
/// Used by the UI to display title lists.
typedef struct {
    u64 title_id;                   ///< Title ID from the application / system title this data belongs to.
    NacpLanguageEntry lang_entry;   ///< UTF-8 strings in the console language.
    u32 icon_size;                  ///< JPEG icon size.
    u8 *icon;                       ///< JPEG icon data.
} TitleApplicationMetadata;

/// Generated using ncm calls.
typedef struct _TitleInfo {
    u8 storage_id;                                  ///< NcmStorageId.
    NcmContentMetaKey meta_key;                     ///< Used with ncm calls.
    Version version;                                ///< Holds the same value from meta_key.version.
    u32 content_count;                              ///< Content info count.
    NcmContentInfo *content_infos;                  ///< Content info entries from this title.
    u64 size;                                       ///< Total title size.
    char size_str[32];                              ///< Total title size string.
    TitleApplicationMetadata *app_metadata;         ///< User application metadata.
    struct _TitleInfo *parent, *previous, *next;    ///< Used with TitleInfo entries from user applications, patches and add-on contents. The parent pointer is unused in user applications.
} TitleInfo;

/// Used to deal with user applications stored in the eMMC, SD card and/or gamecard.
/// The parent, previous and next pointers from the TitleInfo elements are used to traverse through multiple user applications, patches and/or add-on contents.
typedef struct {
    TitleInfo *app_info;    ///< Pointer to a TitleInfo element holding info for the first detected user application entry matching the provided application ID.
    TitleInfo *patch_info;  ///< Pointer to a TitleInfo element holding info for the first detected patch entry matching the provided application ID.
    TitleInfo *aoc_info;    ///< Pointer to a TitleInfo element holding info for the first detected add-on content entry matching the provided application ID.
} TitleUserApplicationData;

typedef enum {
    TitleNamingConvention_Full             = 0, ///< Individual titles: "{Name} [{Id}][v{Version}][{Type}]".
                                                ///< Gamecards: "{Name1} [{Id1}][v{Version1}] + ... + {NameN} [{IdN}][v{VersionN}]".
    TitleNamingConvention_IdAndVersionOnly = 1, ///< Individual titles: "{Id}_v{Version}_{Type}".
                                                ///< Gamecards: "{TitleId1}_v{TitleVersion1}_{TitleType1} + ... + {TitleIdN}_v{TitleVersionN}_{TitleTypeN}".
    TitleNamingConvention_Count            = 2
} TitleNamingConvention;

typedef enum {
    TitleFileNameIllegalCharReplaceType_None               = 0,
    TitleFileNameIllegalCharReplaceType_IllegalFsChars     = 1,
    TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly = 2
} TitleFileNameIllegalCharReplaceType;

/// Initializes the title interface.
bool titleInitialize(void);

/// Closes the title interface.
void titleExit(void);

/// Returns a pointer to a ncm database handle using a NcmStorageId value.
NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id);

/// Returns a pointer to a ncm storage handle using a NcmStorageId value.
NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id);

/// Returns a pointer to a dynamically allocated array of pointers to TitleApplicationMetadata entries, as well as their count. Returns NULL if an error occurs.
/// If 'is_system' is true, TitleApplicationMetadata entries from available system titles (NcmStorageId_BuiltInSystem) will be returned.
/// Otherwise, TitleApplicationMetadata entries from user applications with available content data (NcmStorageId_BuiltInUser, NcmStorageId_SdCard, NcmStorageId_GameCard) will be returned.
/// The allocated buffer must be freed by the calling function using free().
TitleApplicationMetadata **titleGetApplicationMetadataEntries(bool is_system, u32 *out_count);

/// Returns a pointer to a dynamically allocated array of pointers to TitleApplicationMetadata entries with matching gamecard user titles, as well as their count. Returns NULL if an error occurs.
/// The allocated buffer must be freed by the calling function using free().
TitleApplicationMetadata **titleGetGameCardApplicationMetadataEntries(u32 *out_count);

/// Returns a pointer to a dynamically allocated TitleInfo element with a matching storage ID and title ID. Returns NULL if an error occurs.
/// If NcmStorageId_Any is used, the first entry with a matching title ID is returned.
/// Use titleFreeTitleInfo() to free the returned data.
TitleInfo *titleGetInfoFromStorageByTitleId(u8 storage_id, u64 title_id);

/// Frees a dynamically allocated TitleInfo element.
void titleFreeTitleInfo(TitleInfo **info);

/// Populates a TitleUserApplicationData element with dynamically allocated data using a user application ID.
/// Use titleFreeUserApplicationData() to free the populated data.
bool titleGetUserApplicationData(u64 app_id, TitleUserApplicationData *out);

/// Frees data populated by titleGetUserApplicationData().
void titleFreeUserApplicationData(TitleUserApplicationData *user_app_data);

/// Returns true if orphan titles are available.
/// Orphan titles are patches or add-on contents with no NsApplicationControlData available for their parent user application ID.
bool titleAreOrphanTitlesAvailable(void);

/// Returns a pointer to a dynamically allocated array of orphan TitleInfo entries, as well as their count. Returns NULL if an error occurs.
/// Use titleFreeOrphanTitles() to free the returned data.
TitleInfo **titleGetOrphanTitles(u32 *out_count);

/// Frees orphan title info data returned by titleGetInfoFromOrphanTitles().
void titleFreeOrphanTitles(TitleInfo ***orphan_info);

/// Checks if a gamecard status update has been detected by the background gamecard title info thread (e.g. after a new gamecard has been inserted, of after the current one has been taken out).
/// If this function returns true and functions such as titleGetInfoFromStorageByTitleId(), titleGetUserApplicationData() or titleGetInfoFromOrphanTitles() have been previously called:
///     1. Their returned data must be freed.
///     2. They must be called again.
bool titleIsGameCardInfoUpdated(void);

/// Returns a pointer to a dynamically allocated buffer that holds a filename string suitable for output title dumps. Returns NULL if an error occurs.
char *titleGenerateFileName(TitleInfo *title_info, u8 naming_convention, u8 illegal_char_replace_type);

/// Returns a pointer to a dynamically allocated buffer that holds a filename string suitable for output gamecard dumps. Returns NULL if an error occurs.
/// A valid gamecard must be inserted, and title info must have been loaded from it accordingly.
char *titleGenerateGameCardFileName(u8 naming_convention, u8 illegal_char_replace_type);

/// Returns a pointer to a string holding a user-friendly name for the provided NcmStorageId value. Returns NULL if the provided value is invalid.
const char *titleGetNcmStorageIdName(u8 storage_id);

/// Returns a pointer to a string holding the name of the provided NcmContentType value. Returns NULL if the provided value is invalid.
const char *titleGetNcmContentTypeName(u8 content_type);

/// Returns a pointer to a string holding the name of the provided NcmContentMetaType value. Returns NULL if the provided value is invalid.
const char *titleGetNcmContentMetaTypeName(u8 content_meta_type);

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

NX_INLINE u64 titleGetDeltaIdByApplicationId(u64 app_id)
{
    return (app_id + TITLE_DELTA_TYPE_VALUE);
}

NX_INLINE u64 titleGetApplicationIdByDeltaId(u64 delta_id)
{
    return (delta_id - TITLE_DELTA_TYPE_VALUE);
}

NX_INLINE bool titleCheckIfDeltaIdBelongsToApplicationId(u64 app_id, u64 delta_id)
{
    return (delta_id == titleGetDeltaIdByApplicationId(app_id));
}

NX_INLINE u32 titleGetContentCountByType(TitleInfo *info, u8 content_type)
{
    if (!info || !info->content_count || !info->content_infos || content_type > NcmContentType_DeltaFragment) return 0;

    u32 cnt = 0;

    for(u32 i = 0; i < info->content_count; i++)
    {
        if (info->content_infos[i].content_type == content_type) cnt++;
    }

    return cnt;
}

NX_INLINE NcmContentInfo *titleGetContentInfoByTypeAndIdOffset(TitleInfo *info, u8 content_type, u8 id_offset)
{
    if (!info || !info->content_count || !info->content_infos || content_type > NcmContentType_DeltaFragment) return NULL;

    for(u32 i = 0; i < info->content_count; i++)
    {
        NcmContentInfo *cur_content_info = &(info->content_infos[i]);
        if (cur_content_info->content_type == content_type && cur_content_info->id_offset == id_offset) return cur_content_info;
    }

    return NULL;
}

NX_INLINE u32 titleGetCountFromInfoBlock(TitleInfo *title_info)
{
    if (!title_info) return 0;

    u32 count = 1;
    TitleInfo *cur_info = title_info->previous;

    while(cur_info)
    {
        count++;
        cur_info = cur_info->previous;
    }

    cur_info = title_info->next;

    while(cur_info)
    {
        count++;
        cur_info = cur_info->next;
    }

    return count;
}

#ifdef __cplusplus
}
#endif

#endif /* __TITLE_H__ */
