/*
 * aes.h
 *
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

#ifndef __AES_H__
#define __AES_H__

/// Performs an AES-128-XTS crypto operation using the non-standard Nintendo XTS tweak.
/// The Aes128XtsContext element should have been previously initialized with aes128XtsContextCreate(). 'encrypt' should match the value of 'is_encryptor' used with that call.
/// 'dst' and 'src' can both point to the same address.
size_t aes128XtsNintendoCrypt(Aes128XtsContext *ctx, void *dst, const void *src, size_t size, u64 sector, size_t sector_size, bool encrypt);

/// Initializes an output AES partial counter using an initial CTR value and an offset.
/// The size for both 'out' and 'ctr' should be at least AES_BLOCK_SIZE.
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
/// 'out' size should be at least AES_BLOCK_SIZE.
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
/// 'out' size should be at least AES_BLOCK_SIZE.
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

#endif /* __AES_H__ */

#ifdef __cplusplus
}
#endif