/*
 * title_cache.h
 *
 * Copyright (c) 2020-2025, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __TITLE_CACHE_H__
#define __TITLE_CACHE_H__

#include "title.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Initializes the title cache interface.
bool titleCacheInitialize(void);

/// Closes the title cache interface.
void titleCacheExit(void);

/// Checks if the provided title ID exists within the internal title cache.
/// Returns false if the provided title ID can't be found, if the title cache interface hasn't been initialized or if the internal title cache is empty.
bool titleCacheCheckIfEntryExists(u64 title_id);

/// Provides a pointer to a dynamically allocated TitleApplicationMetadata element with data from the internal title cache.
/// Returns NULL if the provided title ID doesn't exist within the internal title cache or if an error occurs.
TitleApplicationMetadata *titleCacheGetApplicationMetadataEntryById(u64 title_id);

/// Updates the internal title cache to add a new entry with information from the provided TitleApplicationMetadata element.
/// Returns true if the new entry has been successfully added, or if the title ID referenced by the provided TitleApplicationMetadata element already exists within the internal title cache (except if `force_add` is set to true).
/// Returns false if an error occurs.
bool titleCacheAddEntry(TitleApplicationMetadata *app_metadata, bool force_add);

/// Flushes the title cache file if there's any pending changes for it.
void titleCacheFlushCacheFile(void);

#ifdef __cplusplus
}
#endif

#endif /* __TITLE_CACHE_H__ */
