/*
 * signature.h
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __SIGNATURE_H__
#define __SIGNATURE_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SignatureType_Rsa4096Sha1   = 0x10000,  ///< RSA-4096 PKCS#1 v1.5 + SHA-1.
    SignatureType_Rsa2048Sha1   = 0x10001,  ///< RSA-2048 PKCS#1 v1.5 + SHA-1.
    SignatureType_Ecc480Sha1    = 0x10002,  ///< Unpadded ECDSA + SHA-1.
    SignatureType_Rsa4096Sha256 = 0x10003,  ///< RSA-4096 PKCS#1 v1.5 + SHA-256.
    SignatureType_Rsa2048Sha256 = 0x10004,  ///< RSA-2048 PKCS#1 v1.5 + SHA-256.
    SignatureType_Ecc480Sha256  = 0x10005,  ///< Unpadded ECDSA + SHA-256.
    SignatureType_Hmac160Sha1   = 0x10006   ///< HMAC-SHA1-160.
} SignatureType;

typedef struct {
    u32 sig_type;           ///< SignatureType_Rsa4096Sha1, SignatureType_Rsa4096Sha256.
    u8 signature[0x200];
    u8 padding[0x3C];
} SignatureBlockRsa4096;

NXDT_ASSERT(SignatureBlockRsa4096, 0x240);

typedef struct {
    u32 sig_type;           ///< SignatureType_Rsa2048Sha1, SignatureType_Rsa2048Sha256.
    u8 signature[0x100];
    u8 padding[0x3C];
} SignatureBlockRsa2048;

NXDT_ASSERT(SignatureBlockRsa2048, 0x140);

typedef struct {
    u32 sig_type;           ///< SignatureType_Ecc480Sha1, SignatureType_Ecc480Sha256.
    u8 signature[0x3C];
    u8 padding[0x40];
} SignatureBlockEcc480;

NXDT_ASSERT(SignatureBlockEcc480, 0x80);

typedef struct {
    u32 sig_type;           ///< SignatureType_Hmac160Sha1.
    u8 signature[0x14];
    u8 padding[0x28];
} SignatureBlockHmac160;

NXDT_ASSERT(SignatureBlockHmac160, 0x40);

/// Helper inline functions.

NX_INLINE bool signatureIsValidType(u32 type)
{
    return (type == SignatureType_Rsa4096Sha1   || type == SignatureType_Rsa2048Sha1   || type == SignatureType_Ecc480Sha1   || \
            type == SignatureType_Rsa4096Sha256 || type == SignatureType_Rsa2048Sha256 || type == SignatureType_Ecc480Sha256 || \
            type == SignatureType_Hmac160Sha1);
}

NX_INLINE u64 signatureGetSigSizeByType(u32 type)
{
    return (u64)((type == SignatureType_Rsa4096Sha1 || type == SignatureType_Rsa4096Sha256) ? MEMBER_SIZE(SignatureBlockRsa4096, signature) : \
                ((type == SignatureType_Rsa2048Sha1 || type == SignatureType_Rsa2048Sha256) ? MEMBER_SIZE(SignatureBlockRsa2048, signature) : \
                ((type == SignatureType_Ecc480Sha1  || type == SignatureType_Ecc480Sha256)  ? MEMBER_SIZE(SignatureBlockEcc480,  signature) : \
                 (type == SignatureType_Hmac160Sha1                                         ? MEMBER_SIZE(SignatureBlockHmac160, signature) : 0))));
}

NX_INLINE u64 signatureGetBlockSizeByType(u32 type)
{
    return (u64)((type == SignatureType_Rsa4096Sha1 || type == SignatureType_Rsa4096Sha256) ? sizeof(SignatureBlockRsa4096) : \
                ((type == SignatureType_Rsa2048Sha1 || type == SignatureType_Rsa2048Sha256) ? sizeof(SignatureBlockRsa2048) : \
                ((type == SignatureType_Ecc480Sha1  || type == SignatureType_Ecc480Sha256)  ? sizeof(SignatureBlockEcc480)  : \
                 (type == SignatureType_Hmac160Sha1                                         ? sizeof(SignatureBlockHmac160) : 0))));
}

NX_INLINE u32 signatureGetTypeFromSignedBlob(void *buf, bool big_endian_sig_type)
{
    if (!buf) return 0;
    return (big_endian_sig_type ? __builtin_bswap32(*((u32*)buf)) : *((u32*)buf));
}

NX_INLINE u8 *signatureGetSigFromSignedBlob(void *buf)
{
    return (buf ? ((u8*)buf + 4) : NULL);
}

NX_INLINE u64 signatureGetBlockSizeFromSignedBlob(void *buf, bool big_endian_sig_type)
{
    return signatureGetBlockSizeByType(signatureGetTypeFromSignedBlob(buf, big_endian_sig_type));
}

NX_INLINE void *signatureGetPayloadFromSignedBlob(void *buf, bool big_endian_sig_type)
{
    if (!buf) return NULL;
    u32 sig_type = signatureGetTypeFromSignedBlob(buf, big_endian_sig_type);
    return (signatureIsValidType(sig_type) ? (void*)((u8*)buf + signatureGetBlockSizeByType(sig_type)) : NULL);
}

#ifdef __cplusplus
}
#endif

#endif /* __SIGNATURE_H__ */
