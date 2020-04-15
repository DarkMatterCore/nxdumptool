#pragma once

#ifndef __RSA_H__
#define __RSA_H__

#include <switch/types.h>

#define RSA2048_SIGNATURE_SIZE  0x100

bool rsa2048GenerateSha256BasedCustomAcidSignature(void *dst, const void *src, size_t size);
const u8 *rsa2048GetCustomAcidPublicKey(void);

bool rsa2048OaepDecryptAndVerify(void *dst, size_t dst_size, const void *signature, const void *modulus, const void *exponent, size_t exponent_size, const void *label_hash, size_t *out_size);

#endif /* __RSA_H__ */
