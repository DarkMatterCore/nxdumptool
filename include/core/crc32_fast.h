/*
 * crc32_fast.h
 *
 * Based on the standard CRC32 checksum fast public domain implementation for
 * little-endian architecures by Björn Samuelsson (http://home.thep.lu.se/~bjorn/crc).
 *
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

#pragma once

#ifndef __CRC32_FAST_H__
#define __CRC32_FAST_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Calculates a CRC32 checksum over the provided input buffer. Checksum calculation in chunks is supported.
/// CRC32 calculation state is both read from and saved to 'crc', which should be zero during the first call to this function.
void crc32FastCalculate(const void *data, u64 n_bytes, u32 *crc);

#ifdef __cplusplus
}
#endif

#endif /* __CRC32_FAST_H__ */
