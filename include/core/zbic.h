/*
 * zbic.h
 *
 * Copyright (c) 2020-2026, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __ZBIC_H__
#define __ZBIC_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Wrapper for ZSTD_decompressionMargin() that calculates the minimum buffer size required to carry out an in-place decompression.
/// BIC support is enabled.
size_t zbicGetInPlaceDecompressionBufferSize(const void *src, size_t src_size, size_t expected_dec_size);

/// Wrapper for ZSTD_decompress() with support for in-place decompression.
/// BIC support is enabled.
size_t zbicDecompress(void *dst, size_t dst_size, const void *src, size_t src_size);

#ifdef __cplusplus
}
#endif

#endif /* __ZBIC_H__ */
