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
