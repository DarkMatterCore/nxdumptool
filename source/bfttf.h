/*
 * bftff.h
 *
 * Copyright (c) 2018, simontime.
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __BFTTF_H__
#define __BFTTF_H__

/// Loosely based on PlSharedFontType.
typedef enum {
    BfttfFontType_Standard             = 0, ///< Japan, US and Europe
    BfttfFontType_NintendoExt1         = 1, ///< Nintendo Extended (1). This font only has the special Nintendo-specific characters, which aren't available with the other fonts.
    BfttfFontType_NintendoExt2         = 2, ///< Nintendo Extended (2). This font only has the special Nintendo-specific characters, which aren't available with the other fonts.
    BfttfFontType_Korean               = 3, ///< Korean (Hangul).
    BfttfFontType_ChineseSimplified    = 4, ///< Chinese Simplified.
    BfttfFontType_ExtChineseSimplified = 5, ///< Extended Chinese Simplified.
    BfttfFontType_ChineseTraditional   = 6, ///< Chinese Traditional.
    BfttfFontType_Total                = 7  ///< Total fonts supported by this enum.
} BfttfFontType;

/// Loosely based on PlFontData.
typedef struct {
    u8 type;    ///< BfttfFontType.
    u32 size;   ///< Decoded BFTFF font size.
    void *ptr;  ///< Pointer to font data.
} BfttfFontData;

/// Initializes the BFTTF interface.
bool bfttfInitialize(void);

/// Closes the BFTTF interface.
void bfttfExit(void);

/// Returns a specific BFTTF font using the provided BfttfFontType.
bool bfttfGetFontByType(BfttfFontData *font, u8 font_type);

#endif /* __BFTTF_H__ */

#ifdef __cplusplus
}
#endif