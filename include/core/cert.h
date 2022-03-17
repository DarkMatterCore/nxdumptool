/*
 * cert.h
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

#ifndef __CERT_H__
#define __CERT_H__

#include "signature.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIGNED_CERT_MAX_SIZE    sizeof(CertSigRsa4096PubKeyRsa4096)
#define SIGNED_CERT_MIN_SIZE    sizeof(CertSigHmac160PubKeyEcc480)

#define GENERATE_CERT_STRUCT(sigtype, pubkeytype, certsize) \
\
typedef struct { \
    SignatureBlock##sigtype sig_block; \
    CertCommonBlock cert_common_block; \
    CertPublicKeyBlock##pubkeytype pub_key_block; \
} CertSig##sigtype##PubKey##pubkeytype; \
\
NXDT_ASSERT(CertSig##sigtype##PubKey##pubkeytype, certsize);

typedef enum {
    CertPubKeyType_Rsa4096 = 0,
    CertPubKeyType_Rsa2048 = 1,
    CertPubKeyType_Ecc480  = 2
} CertPubKeyType;

/// Placed after the certificate signature block.
typedef struct {
    char issuer[0x40];
    u32 pub_key_type;   ///< CertPubKeyType. Always stored using big endian byte order.
    char name[0x40];
    u32 date;           ///< Stored using big endian byte order.
} CertCommonBlock;

NXDT_ASSERT(CertCommonBlock, 0x88);

/// RSA-4096 public key block. Placed after the certificate common block.
typedef struct {
    u8 public_key[0x200];
    u32 public_exponent;
    u8 padding[0x34];
} CertPublicKeyBlockRsa4096;

NXDT_ASSERT(CertPublicKeyBlockRsa4096, 0x238);

/// RSA-2048 public key block. Placed after the certificate common block.
typedef struct {
    u8 public_key[0x100];
    u32 public_exponent;
    u8 padding[0x34];
} CertPublicKeyBlockRsa2048;

NXDT_ASSERT(CertPublicKeyBlockRsa2048, 0x138);

/// ECC public key block. Placed after the certificate common block.
typedef struct {
    u8 public_key[0x3C];
    u8 padding[0x3C];
} CertPublicKeyBlockEcc480;

NXDT_ASSERT(CertPublicKeyBlockEcc480, 0x78);

/// All certificates generated below use a big endian sig_type field.

/// Certificates with RSA-4096 signatures.
GENERATE_CERT_STRUCT(Rsa4096, Rsa4096, 0x500);  /// pub_key_type field must be CertPubKeyType_Rsa4096.
GENERATE_CERT_STRUCT(Rsa4096, Rsa2048, 0x400);  /// pub_key_type field must be CertPubKeyType_Rsa2048.
GENERATE_CERT_STRUCT(Rsa4096, Ecc480, 0x340);   /// pub_key_type field must be CertPubKeyType_Ecc480.

/// Certificates with RSA-2048 signatures.
GENERATE_CERT_STRUCT(Rsa2048, Rsa4096, 0x400);  /// pub_key_type field must be CertPubKeyType_Rsa4096.
GENERATE_CERT_STRUCT(Rsa2048, Rsa2048, 0x300);  /// pub_key_type field must be CertPubKeyType_Rsa2048.
GENERATE_CERT_STRUCT(Rsa2048, Ecc480, 0x240);   /// pub_key_type field must be CertPubKeyType_Ecc480.

/// Certificates with ECC signatures.
GENERATE_CERT_STRUCT(Ecc480, Rsa4096, 0x340);   /// pub_key_type field must be CertPubKeyType_Rsa4096.
GENERATE_CERT_STRUCT(Ecc480, Rsa2048, 0x240);   /// pub_key_type field must be CertPubKeyType_Rsa2048.
GENERATE_CERT_STRUCT(Ecc480, Ecc480, 0x180);    /// pub_key_type field must be CertPubKeyType_Ecc480.

/// Certificates with HMAC signatures.
GENERATE_CERT_STRUCT(Hmac160, Rsa4096, 0x300);  /// pub_key_type field must be CertPubKeyType_Rsa4096.
GENERATE_CERT_STRUCT(Hmac160, Rsa2048, 0x200);  /// pub_key_type field must be CertPubKeyType_Rsa2048.
GENERATE_CERT_STRUCT(Hmac160, Ecc480, 0x140);   /// pub_key_type field must be CertPubKeyType_Ecc480.

/// Certificate type.
typedef enum {
    CertType_None                     = 0,
    CertType_SigRsa4096_PubKeyRsa4096 = 1,
    CertType_SigRsa4096_PubKeyRsa2048 = 2,
    CertType_SigRsa4096_PubKeyEcc480  = 3,
    CertType_SigRsa2048_PubKeyRsa4096 = 4,
    CertType_SigRsa2048_PubKeyRsa2048 = 5,
    CertType_SigRsa2048_PubKeyEcc480  = 6,
    CertType_SigEcc480_PubKeyRsa4096  = 7,
    CertType_SigEcc480_PubKeyRsa2048  = 8,
    CertType_SigEcc480_PubKeyEcc480   = 9,
    CertType_SigHmac160_PubKeyRsa4096 = 10,
    CertType_SigHmac160_PubKeyRsa2048 = 11,
    CertType_SigHmac160_PubKeyEcc480  = 12
} CertType;

/// Used to store certificate type, size and raw data.
typedef struct {
    u8 type;                        ///< CertType.
    u64 size;                       ///< Raw certificate size.
    u8 data[SIGNED_CERT_MAX_SIZE];  ///< Raw certificate data.
} Certificate;

/// Used to store two or more certificates.
typedef struct {
    u32 count;
    Certificate *certs;
} CertificateChain;

/// Retrieves a certificate by its name (e.g. "CA00000003", "XS00000020", etc.).
bool certRetrieveCertificateByName(Certificate *dst, const char *name);

/// Retrieves a certificate chain by a full signature issuer string (e.g. "Root-CA00000003-XS00000020").
bool certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer);

/// Returns a pointer to a dynamically allocated buffer that contains the raw contents from the certificate chain matching the input signature issuer. It must be freed by the user.
/// NULL is returned if an error occurs.
u8 *certGenerateRawCertificateChainBySignatureIssuer(const char *issuer, u64 *out_size);

/// Returns a pointer to a dynamically allocated buffer that contains the raw contents from the certificate chain matching the input Rights ID (stored in the inserted gamecard).
/// It must be freed by the user.
/// NULL is returned if an error occurs.
u8 *certRetrieveRawCertificateChainFromGameCardByRightsId(const FsRightsId *id, u64 *out_size);

/// Helper inline functions.

NX_INLINE void certFreeCertificateChain(CertificateChain *chain)
{
    if (!chain) return;
    if (chain->certs) free(chain->certs);
    memset(chain, 0, sizeof(CertificateChain));
}

NX_INLINE CertCommonBlock *certGetCommonBlock(void *buf)
{
    return (CertCommonBlock*)signatureGetPayload(buf, true);
}

NX_INLINE bool certIsValidPublicKeyType(u32 type)
{
    return (type == CertPubKeyType_Rsa4096 || type == CertPubKeyType_Rsa2048 || type == CertPubKeyType_Ecc480);
}

NX_INLINE u8 *certGetPublicKey(CertCommonBlock *cert_common_block)
{
    return ((cert_common_block != NULL && certIsValidPublicKeyType(__builtin_bswap32(cert_common_block->pub_key_type))) ? ((u8*)cert_common_block + sizeof(CertCommonBlock)) : NULL);
}

NX_INLINE u64 certGetPublicKeySize(u32 type)
{
    return (u64)(type == CertPubKeyType_Rsa4096 ? MEMBER_SIZE(CertPublicKeyBlockRsa4096, public_key) : \
                (type == CertPubKeyType_Rsa2048 ? MEMBER_SIZE(CertPublicKeyBlockRsa2048, public_key) : \
                (type == CertPubKeyType_Ecc480  ? MEMBER_SIZE(CertPublicKeyBlockEcc480,  public_key) : 0)));
}

NX_INLINE u64 certGetPublicKeyBlockSize(u32 type)
{
    return (u64)(type == CertPubKeyType_Rsa4096 ? sizeof(CertPublicKeyBlockRsa4096) : \
                (type == CertPubKeyType_Rsa2048 ? sizeof(CertPublicKeyBlockRsa2048) : \
                (type == CertPubKeyType_Ecc480  ? sizeof(CertPublicKeyBlockEcc480)  : 0)));
}

NX_INLINE u32 certGetPublicExponent(CertCommonBlock *cert_common_block)
{
    u8 *public_key = certGetPublicKey(cert_common_block);
    return ((cert_common_block != NULL && public_key != NULL) ? __builtin_bswap32(*((u32*)(public_key + certGetPublicKeySize(__builtin_bswap32(cert_common_block->pub_key_type))))) : 0);
}

NX_INLINE bool certIsValidCertificate(void *buf)
{
    CertCommonBlock *cert_common_block = certGetCommonBlock(buf);
    return (cert_common_block != NULL && certIsValidPublicKeyType(__builtin_bswap32(cert_common_block->pub_key_type)));
}

NX_INLINE u64 certGetSignedCertificateSize(void *buf)
{
    if (!certIsValidCertificate(buf)) return 0;
    CertCommonBlock *cert_common_block = certGetCommonBlock(buf);
    return (signatureGetBlockSize(signatureGetSigType(buf, true)) + (u64)sizeof(CertCommonBlock) + certGetPublicKeyBlockSize(__builtin_bswap32(cert_common_block->pub_key_type)));
}

NX_INLINE u64 certGetSignedCertificateHashAreaSize(void *buf)
{
    if (!certIsValidCertificate(buf)) return 0;
    CertCommonBlock *cert_common_block = certGetCommonBlock(buf);
    return ((u64)sizeof(CertCommonBlock) + certGetPublicKeyBlockSize(__builtin_bswap32(cert_common_block->pub_key_type)));
}

#ifdef __cplusplus
}
#endif

#endif /* __CERT_H__ */
