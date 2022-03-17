/*
 * aes.h
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

#ifndef __AES_H__
#define __AES_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Performs an AES-128-XTS crypto operation using the non-standard Nintendo XTS tweak.
/// The Aes128XtsContext element should have been previously initialized with aes128XtsContextCreate(). 'encrypt' should match the value of 'is_encryptor' used with that call.
/// 'dst' and 'src' can both point to the same address.
size_t aes128XtsNintendoCrypt(Aes128XtsContext *ctx, void *dst, const void *src, size_t size, u64 sector, size_t sector_size, bool encrypt);

/// Initializes an output AES partial counter using an initial CTR value and an offset.
/// The sizes for 'out' and 'ctr' should be at least AES_BLOCK_SIZE and 8 bytes, respectively.
NX_INLINE void aes128CtrInitializePartialCtr(u8 *out, const u8 *ctr, u64 offset)
{
    if (!out || !ctr) return;
    
    offset >>= 4;
    
    for(u8 i = 0; i < 8; i++)
    {
        out[i] = ctr[0x8 - i - 1];
        out[0x10 - i - 1] = (u8)(offset & 0xFF);
        offset >>= 8;
    }
}

/// Updates the provided AES partial counter using an offset.
/// Size for 'out' should be at least AES_BLOCK_SIZE.
NX_INLINE void aes128CtrUpdatePartialCtr(u8 *ctr, u64 offset)
{
    if (!ctr) return;
    
    offset >>= 4;
    
    for(u8 i = 0; i < 8; i++)
    {
        ctr[0x10 - i - 1] = (u8)(offset & 0xFF);
        offset >>= 8;
    }
}

/// Updates the provided AES partial counter using an offset and a 32-bit CTR value.
/// Size for 'out' should be at least AES_BLOCK_SIZE.
NX_INLINE void aes128CtrUpdatePartialCtrEx(u8 *ctr, u32 ctr_val, u64 offset)
{
    if (!ctr) return;
    
    offset >>= 4;
    
    for(u8 i = 0; i < 8; i++)
    {
        ctr[0x10 - i - 1] = (u8)(offset & 0xFF);
        offset >>= 8;
    }
    
    for(u8 i = 0; i < 4; i++)
    {
        ctr[0x8 - i - 1] = (u8)(ctr_val & 0xFF);
        ctr_val >>= 8;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* __AES_H__ */
