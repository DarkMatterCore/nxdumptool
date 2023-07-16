/*
 * bftff.h
 *
 * Copyright (c) 2018, simontime.
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __BFTTF_H__
#define __BFTTF_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Loosely based on PlSharedFontType.
typedef enum {
    BfttfFontType_Standard             = 0, ///< Japan, US and Europe.
    BfttfFontType_NintendoExt          = 1, ///< Extended Nintendo. This font contains special, Nintendo-specific characters, which aren't available in the other fonts.
    BfttfFontType_Korean               = 2, ///< Korean (Hangul).
    BfttfFontType_ChineseSimplified    = 3, ///< Simplified Chinese.
    BfttfFontType_ExtChineseSimplified = 4, ///< Extended Simplified Chinese.
    BfttfFontType_ChineseTraditional   = 5, ///< Traditional Chinese.
    BfttfFontType_Count                = 6  ///< Total fonts supported by this enum.
} BfttfFontType;

/// Loosely based on PlFontData.
typedef struct {
    u8 type;        ///< BfttfFontType.
    u32 size;       ///< Decoded BFTFF font size.
    void *address;  ///< Font data address.
} BfttfFontData;

/// Initializes the BFTTF interface.
bool bfttfInitialize(void);

/// Closes the BFTTF interface.
void bfttfExit(void);

/// Returns a specific BFTTF font using the provided BfttfFontType.
bool bfttfGetFontByType(BfttfFontData *font, u8 font_type);

#ifdef __cplusplus
}
#endif

#endif /* __BFTTF_H__ */
