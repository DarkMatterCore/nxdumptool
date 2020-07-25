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

/// Retrieved from ns application records.
typedef struct {
    u64 title_id;                   ///< Title ID from the application this data belongs to.
    NacpLanguageEntry lang_entry;   ///< UTF-8 strings in the language set in the console settings.
    u32 icon_size;                  ///< JPEG icon size.
    u8 icon[0x20000];               ///< JPEG icon data.
} TitleApplicationMetadata;

/// Retrieved from ncm databases.
typedef struct {
    u8 storage_id;                          ///< NcmStorageId.
    NcmContentMetaKey meta_key;             ///< Used with ncm calls.
    u64 title_size;                         ///< Total title size.
    TitleApplicationMetadata *app_metadata; ///< Not available for all titles.
} TitleInfo;









bool titleInitialize(void);
void titleExit(void);

NcmContentMetaDatabase *titleGetNcmDatabaseByStorageId(u8 storage_id);
NcmContentStorage *titleGetNcmStorageByStorageId(u8 storage_id);

/// Returns true if gamecard title info could be loaded.
/// Suitable for being called between UI updates.
bool titleRefreshGameCardTitleInfo(void);



/// Miscellaneous functions.

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

#endif /* __TITLE_H__ */
