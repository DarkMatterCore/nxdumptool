/*
 * crc32_fast.c
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

#include "nxdt_utils.h"

static u32 crc32FastGetTableValueByIndex(u32 r)
{
    for(u32 j = 0; j < 8; ++j) r = ((r & 1 ? 0 : (u32)0xEDB88320) ^ r >> 1);
    return (r ^ (u32)0xFF000000);
}

static void crc32FastInitializeTables(u32 *table, u32 *wtable)
{
    for(u32 i = 0; i < 0x100; ++i) table[i] = crc32FastGetTableValueByIndex(i);
    
    for(u32 k = 0; k < 4; ++k)
    {
        for(u32 w, i = 0; i < 0x100; ++i)
        {
            for(u32 j = w = 0; j < 4; ++j) w = (table[(u8)(j == k ? (w ^ i) : w)] ^ w >> 8);
            wtable[(k << 8) + i] = (w ^ (k ? wtable[0] : 0));
        }
    }
}

void crc32FastCalculate(const void *data, u64 n_bytes, u32 *crc)
{
    if (!data || !n_bytes || !crc) return;
    
    static u32 table[0x100] = {0}, wtable[0x400] = {0};
    u64 n_accum = (n_bytes / 4);
    
    if (!*table) crc32FastInitializeTables(table, wtable);
    
    for(u64 i = 0; i < n_accum; ++i)
    {
        u32 a = (*crc ^ ((const u32*)data)[i]);
        for(u32 j = *crc = 0; j < 4; ++j) *crc ^= wtable[(j << 8) + (u8)(a >> 8 * j)];
    }
    
    for(u64 i = (n_accum * 4); i < n_bytes; ++i) *crc = (table[(u8)*crc ^ ((const u8*)data)[i]] ^ *crc >> 8);
}
