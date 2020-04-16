/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __SIGNATURE_H__
#define __SIGNATURE_H__

typedef enum {
    SignatureType_Rsa4096Sha1    = 0x10000,
    SignatureType_Rsa2048Sha1    = 0x10001,
    SignatureType_Ecsda240Sha1   = 0x10002,
    SignatureType_Rsa4096Sha256  = 0x10003,
    SignatureType_Rsa2048Sha256  = 0x10004,
    SignatureType_Ecsda240Sha256 = 0x10005
} SignatureType;

typedef struct {
    u32 sig_type;           ///< SignatureType_Rsa4096Sha1, SignatureType_Rsa4096Sha256.
    u8 signature[0x200];
    u8 padding[0x3C];
} SignatureBlockRsa4096;

typedef struct {
    u32 sig_type;           ///< SignatureType_Rsa2048Sha1, SignatureType_Rsa2048Sha256.
    u8 signature[0x100];
    u8 padding[0x3C];
} SignatureBlockRsa2048;

typedef struct {
    u32 sig_type;           ///< SignatureType_Ecsda240Sha1, SignatureType_Ecsda240Sha256.
    u8 signature[0x3C];
    u8 padding[0x40];
} SignatureBlockEcsda240;

#endif /* __SIGNATURE_H__ */
