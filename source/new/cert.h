#pragma once

#ifndef __CERT_H__
#define __CERT_H__

#include "signature.h"

#define CERT_MAX_SIZE   0x500   /* Equivalent to sizeof(CertSigRsa4096PubKeyRsa4096) */
#define CERT_MIN_SIZE   0x180   /* Equivalent to sizeof(CertSigEcsda240PubKeyEcsda240) */

typedef enum {
    CertType_SigRsa4096_PubKeyRsa4096   = 0,
    CertType_SigRsa4096_PubKeyRsa2048   = 1,
    CertType_SigRsa4096_PubKeyEcsda240  = 2,
    CertType_SigRsa2048_PubKeyRsa4096   = 3,
    CertType_SigRsa2048_PubKeyRsa2048   = 4,
    CertType_SigRsa2048_PubKeyEcsda240  = 5,
    CertType_SigEcsda240_PubKeyRsa4096  = 6,
    CertType_SigEcsda240_PubKeyRsa2048  = 7,
    CertType_SigEcsda240_PubKeyEcsda240 = 8,
    CertType_Invalid                    = 255
} CertType;

/// Always stored using big endian byte order.
typedef enum {
    CertPubKeyType_Rsa4096  = 0,
    CertPubKeyType_Rsa2048  = 1,
    CertPubKeyType_Ecsda240 = 2
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
} CertPublicKeyBlockEcsda240;

typedef struct {
    SignatureBlockRsa4096 sig_block;        ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Rsa4096.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockRsa4096 pub_key_block;
} CertSigRsa4096PubKeyRsa4096;

typedef struct {
    SignatureBlockRsa4096 sig_block;        ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Rsa2048.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockRsa2048 pub_key_block;
} CertSigRsa4096PubKeyRsa2048;

typedef struct {
    SignatureBlockRsa4096 sig_block;        ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Ecsda240.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockEcsda240 pub_key_block;
} CertSigRsa4096PubKeyEcsda240;

typedef struct {
    SignatureBlockRsa2048 sig_block;        ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Rsa4096.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockRsa4096 pub_key_block;
} CertSigRsa2048PubKeyRsa4096;

typedef struct {
    SignatureBlockRsa2048 sig_block;        ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Rsa2048.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockRsa2048 pub_key_block;
} CertSigRsa2048PubKeyRsa2048;

typedef struct {
    SignatureBlockRsa2048 sig_block;        ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Ecsda240.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockEcsda240 pub_key_block;
} CertSigRsa2048PubKeyEcsda240;

typedef struct {
    SignatureBlockEcsda240 sig_block;       ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Rsa4096.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockRsa4096 pub_key_block;
} CertSigEcsda240PubKeyRsa4096;

typedef struct {
    SignatureBlockEcsda240 sig_block;       ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Rsa2048.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockRsa2048 pub_key_block;
} CertSigEcsda240PubKeyRsa2048;

typedef struct {
    SignatureBlockEcsda240 sig_block;       ///< sig_type field is stored using big endian byte order.
    char issuer[0x40];
    u32 pub_key_type;                       ///< CertPubKeyType_Ecsda240.
    char name[0x40];
    u32 cert_id;
    CertPublicKeyBlockEcsda240 pub_key_block;
} CertSigEcsda240PubKeyEcsda240;

/// Used to store certificate type, size and raw data.
typedef struct {
    u8 type;                ///< CertType.
    u64 size;
    u8 data[CERT_MAX_SIZE];
} Certificate;

/// Used to store two or more certificates.
typedef struct {
    u32 count;
    Certificate *certs;
} CertificateChain;

bool certRetrieveCertificateByName(Certificate *dst, const char *name);

void certFreeCertificateChain(CertificateChain *chain);
bool certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer);

/// Returns a pointer to a heap allocated buffer that must be freed by the user.
u8 *certGenerateRawCertificateChainBySignatureIssuer(const char *issuer, u64 *out_size);

#endif /* __CERT_H__ */
