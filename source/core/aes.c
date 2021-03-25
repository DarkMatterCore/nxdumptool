/*
 * aes.c
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

#include "utils.h"

size_t aes128XtsNintendoCrypt(Aes128XtsContext *ctx, void *dst, const void *src, size_t size, u64 sector, size_t sector_size, bool encrypt)
{
    if (!ctx || !dst || !src || !size || !sector_size || (size % sector_size) != 0)
    {
        LOG_MSG("Invalid parameters!");
        return 0;
    }
    
    size_t i, crypt_res = 0;
    u64 cur_sector = sector;
    
    u8 *dst_u8 = (u8*)dst;
    const u8 *src_u8 = (const u8*)src;
    
    for(i = 0; i < size; i += sector_size, cur_sector++)
    {
        /* We have to force a sector reset on each new sector to actually enable Nintendo AES-XTS cipher tweak. */
        aes128XtsContextResetSector(ctx, cur_sector, true);
        crypt_res = (encrypt ? aes128XtsEncrypt(ctx, dst_u8 + i, src_u8 + i, sector_size) : aes128XtsDecrypt(ctx, dst_u8 + i, src_u8 + i, sector_size));
        if (crypt_res != sector_size) break;
    }
    
    return i;
}
