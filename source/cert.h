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

#ifndef __CERT_H__
#define __CERT_H__

#include <switch/types.h>
#include "signature.h"

#define CERT_MAX_SIZE   0x500   /* Equivalent to sizeof(CertSigRsa4096PubKeyRsa4096) */
#define CERT_MIN_SIZE   0x140   /* Equivalent to sizeof(CertSigHmac160PubKeyEcc480) */

typedef enum {
    CertType_None                       = 0,
    CertType_SigRsa4096_PubKeyRsa4096   = 1,
    CertType_SigRsa4096_PubKeyRsa2048   = 2,
    CertType_SigRsa4096_PubKeyEcc480  = 3,
    CertType_SigRsa2048_PubKeyRsa4096   = 4,
    CertType_SigRsa2048_PubKeyRsa2048   = 5,
    CertType_SigRsa2048_PubKeyEcc480  = 6,
    CertType_SigEcc480_PubKeyRsa4096  = 7,
    CertType_SigEcc480_PubKeyRsa2048  = 8,
    CertType_SigEcc480_PubKeyEcc480 = 9,
    CertType_SigHmac160_PubKeyRsa4096   = 10,
    CertType_SigHmac160_PubKeyRsa2048   = 11,
    CertType_SigHmac160_PubKeyEcc480  = 12
} CertType;

/// Always stored using big endian byte order.
typedef enum {
    CertPubKeyType_Rsa4096  = 0,
    CertPubKeyType_Rsa2048  = 1,
    CertPubKeyType_Ecc480 = 2
} CertPubKeyType;

typedef struct {
    u8 public_key[0x200];
    u32 public_exponent;
    u8 padding[0x34];
} CertPublicKeyBlockRsa4096;

typedef struct {
    u8 public_key[0x100];
    u32 public_exponent;
    u8 padding[0x34];
} CertPublicKeyBlockRsa2048;

typedef struct {
    u8 public_key[0x3C];
    u8 padding[0x3C];
} CertPublicKeyBlockEcc480;

/// Placed after the certificate signature block.
typedef struct {
    char issuer[0x40];
    u32 pub_key_type;   ///< CertPubKeyType.
    char name[0x40];
    u32 date;
} CertCommonBlock;

typedef struct {
    SignatureBlockRsa4096 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa4096.
    CertPublicKeyBlockRsa4096 pub_key_block;
} CertSigRsa4096PubKeyRsa4096;

typedef struct {
    SignatureBlockRsa4096 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa2048.
    CertPublicKeyBlockRsa2048 pub_key_block;
} CertSigRsa4096PubKeyRsa2048;

typedef struct {
    SignatureBlockRsa4096 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Ecc480.
    CertPublicKeyBlockEcc480 pub_key_block;
} CertSigRsa4096PubKeyEcc480;

typedef struct {
    SignatureBlockRsa2048 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa4096.
    CertPublicKeyBlockRsa4096 pub_key_block;
} CertSigRsa2048PubKeyRsa4096;

typedef struct {
    SignatureBlockRsa2048 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa2048.
    CertPublicKeyBlockRsa2048 pub_key_block;
} CertSigRsa2048PubKeyRsa2048;

typedef struct {
    SignatureBlockRsa2048 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Ecc480.
    CertPublicKeyBlockEcc480 pub_key_block;
} CertSigRsa2048PubKeyEcc480;

typedef struct {
    SignatureBlockEcc480 sig_block;             ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa4096.
    CertPublicKeyBlockRsa4096 pub_key_block;
} CertSigEcc480PubKeyRsa4096;

typedef struct {
    SignatureBlockEcc480 sig_block;             ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa2048.
    CertPublicKeyBlockRsa2048 pub_key_block;
} CertSigEcc480PubKeyRsa2048;

typedef struct {
    SignatureBlockEcc480 sig_block;             ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Ecc480.
    CertPublicKeyBlockEcc480 pub_key_block;
} CertSigEcc480PubKeyEcc480;

typedef struct {
    SignatureBlockHmac160 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa4096.
    CertPublicKeyBlockRsa4096 pub_key_block;
} CertSigHmac160PubKeyRsa4096;

typedef struct {
    SignatureBlockHmac160 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Rsa2048.
    CertPublicKeyBlockRsa2048 pub_key_block;
} CertSigHmac160PubKeyRsa2048;

typedef struct {
    SignatureBlockHmac160 sig_block;            ///< sig_type field is stored using big endian byte order.
    CertCommonBlock cert_common_blk;            ///< pub_key_type field must be CertPubKeyType_Ecc480.
    CertPublicKeyBlockEcc480 pub_key_block;
} CertSigHmac160PubKeyEcc480;

/// Used to store certificate type, size and raw data.
typedef struct {
    u8 type;                ///< CertType.
    u64 size;               ///< Raw certificate size.
    u8 data[CERT_MAX_SIZE]; ///< Raw certificate data.
} Certificate;

/// Used to store two or more certificates.
typedef struct {
    u32 count;
    Certificate *certs;
} CertificateChain;

bool certRetrieveCertificateByName(Certificate *dst, const char *name);

bool certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer);
void certFreeCertificateChain(CertificateChain *chain);

/// Retrieves the common block from an input Certificate.
CertCommonBlock *certGetCommonBlockFromCertificate(Certificate *cert);

/// Returns a pointer to a heap allocated buffer that must be freed by the user.
u8 *certGenerateRawCertificateChainBySignatureIssuer(const char *issuer, u64 *out_size);

#endif /* __CERT_H__ */
