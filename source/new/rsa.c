#include <stdio.h>
#include <string.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509.h>

#include "rsa.h"
#include "utils.h"

/* Self-generated private key */
static const char g_rsa2048CustomAcidPrivateKey[] = "-----BEGIN RSA PRIVATE KEY-----\r\n"
"MIIEowIBAAKCAQEAvVRzt+8mE7oE4RkmSh3ws4CGlBj7uhHkfwCpPFsn4TNVdLRo\r\n"
"YYY17jQYWTtcOYPMcHxwUpgJyspGN8QGXEkJqY8jILv2eO0jBGtg7Br2afUBp6/x\r\n"
"BOMT2RlYVX6H4a1UA19Hzmcn+T1hdDwS6oBYpi8rJSm0+q+yB34dueNkVsk4eKbj\r\n"
"CNNKFi+XgyNBi41d57SPCrkcm/9tkagRorE8vLcFPcXcYOjdXH3L4XTXq7sxxytA\r\n"
"I66erfSc4XunkoLifcbfMOB3gjGCoQs6GfaiAU3TwxewQ7hdoqvj5Gm9VyHqzeDF\r\n"
"5mUTlmed2I6m4ELxbV1b0lUguR5ZEzwXwiVWxwIDAQABAoIBADvLYkijFOmCBGx7\r\n"
"HualkhF+9AHt6gKYCAw8Tzaqq2uqZMDZAWZblsjGVzJHVxcrEvQruOW88srDG24d\r\n"
"UMzwnEaa2ENMWclTS43nw9KNqWlJYd5t6LbcaLZWFNnbflq9/RybiPgdCDjlM9Qb\r\n"
"7PV214iUuRGhnHDX8GgBYq4ErPnjQ7+Gv1ducpMYjZencLWCl4fFX86U0/MU0+Qf\r\n"
"jKGegQTnk52aaeScbDOjjx5h+m0hkDNSfsmXTlvJt2c8wy/Yx+leVgCPjMC1nbft\r\n"
"Ob1TlpjuEAKBOGt4+DkWwVmIlxilmx9wCTZnwvPKd7A0e0FGsdHnQienPrMqlgbl\r\n"
"JPYwJuECgYEA6yLZHTfX3ebpzcdQQqmuHZtbOcs+EGRy24gAzd+9vCGKf0VtKSl9\r\n"
"3oA3XBOe2C2TgSgbWFZ7v/2efWRjgwJta0BQlpkzkh6NUQa2LI2M3zgZwHCZ7Ihr\r\n"
"skG73qZsMHOOv7VQz/wDp6AZNasfz21Mcyh4uFzpkb3NKLXqsJ9LeG8CgYEAziEb\r\n"
"yBCuhCKq7YZt/cHlbCbi7HbCYbub0isOCUtV0qPsX+kVZdPS+oGLPq1905JKdAe9\r\n"
"O+4SltCw6qn9RgYnCCVQ47SGHg7KO8Z5vdcNUiDvsQ+jNFlmM5QBuf1UV/Y+DV/Q\r\n"
"fZdA06OeYxkfPuBMtjdS9qMKwm3OsCkiQasWQykCgYAqALieAoq6JfSgALmyntLu\r\n"
"kQDzyv2UOg1Wb+4M2KnxAGDYKVO9pZ7Jb0f0V8DpRwLxcHOqDRDgE/MK3TL1hSp8\r\n"
"nSmILWfL8081KSjDvqlqeoAHI1YrrZbnadyggkQTR6E5V69O5+rTN8MpFh+Bkzmz\r\n"
"3IfsDxTeJvSOECkTUfFOWwKBgQDG/id3yMLxRRaGH5TnuNvmwNOpPC0DdL5E8tOm\r\n"
"HVhI9X8oSDgkCY5Pz+fBJnOmYEAIK8B/rqG7ftSMdnbPtvjPYFbqvEgNlHGfq0e0\r\n"
"AXwWoT1ETbhcvUFw4Z2ZE/rswAe/mZQI6o/mwLoTKRmE9byY3Gf3OgcVFDTI060C\r\n"
"gEwJoQKBgHpOmtGum3JuLpPc+PTXZOe29tdWndkFWktjPoow60d+NO2jpTFuEpmW\r\n"
"XRW35vXI8PqMCmHOQ8YU59aMN9juAnsJmPUxbAW5fZfvVwWUo0cTOenfT6syrEYO\r\n"
"n5NEG+mY4WZaOFRNiZu8+4aJI1yycXMyA22iKcU8+nN/sMAJs3Nx\r\n"
"-----END RSA PRIVATE KEY-----\r\n";

/* Self-generated public key */
static const u8 g_rsa2048CustomAcidPublicKey[] = {
    0xBD, 0x54, 0x73, 0xB7, 0xEF, 0x26, 0x13, 0xBA, 0x04, 0xE1, 0x19, 0x26, 0x4A, 0x1D, 0xF0, 0xB3,
    0x80, 0x86, 0x94, 0x18, 0xFB, 0xBA, 0x11, 0xE4, 0x7F, 0x00, 0xA9, 0x3C, 0x5B, 0x27, 0xE1, 0x33,
    0x55, 0x74, 0xB4, 0x68, 0x61, 0x86, 0x35, 0xEE, 0x34, 0x18, 0x59, 0x3B, 0x5C, 0x39, 0x83, 0xCC,
    0x70, 0x7C, 0x70, 0x52, 0x98, 0x09, 0xCA, 0xCA, 0x46, 0x37, 0xC4, 0x06, 0x5C, 0x49, 0x09, 0xA9,
    0x8F, 0x23, 0x20, 0xBB, 0xF6, 0x78, 0xED, 0x23, 0x04, 0x6B, 0x60, 0xEC, 0x1A, 0xF6, 0x69, 0xF5,
    0x01, 0xA7, 0xAF, 0xF1, 0x04, 0xE3, 0x13, 0xD9, 0x19, 0x58, 0x55, 0x7E, 0x87, 0xE1, 0xAD, 0x54,
    0x03, 0x5F, 0x47, 0xCE, 0x67, 0x27, 0xF9, 0x3D, 0x61, 0x74, 0x3C, 0x12, 0xEA, 0x80, 0x58, 0xA6,
    0x2F, 0x2B, 0x25, 0x29, 0xB4, 0xFA, 0xAF, 0xB2, 0x07, 0x7E, 0x1D, 0xB9, 0xE3, 0x64, 0x56, 0xC9,
    0x38, 0x78, 0xA6, 0xE3, 0x08, 0xD3, 0x4A, 0x16, 0x2F, 0x97, 0x83, 0x23, 0x41, 0x8B, 0x8D, 0x5D,
    0xE7, 0xB4, 0x8F, 0x0A, 0xB9, 0x1C, 0x9B, 0xFF, 0x6D, 0x91, 0xA8, 0x11, 0xA2, 0xB1, 0x3C, 0xBC,
    0xB7, 0x05, 0x3D, 0xC5, 0xDC, 0x60, 0xE8, 0xDD, 0x5C, 0x7D, 0xCB, 0xE1, 0x74, 0xD7, 0xAB, 0xBB,
    0x31, 0xC7, 0x2B, 0x40, 0x23, 0xAE, 0x9E, 0xAD, 0xF4, 0x9C, 0xE1, 0x7B, 0xA7, 0x92, 0x82, 0xE2,
    0x7D, 0xC6, 0xDF, 0x30, 0xE0, 0x77, 0x82, 0x31, 0x82, 0xA1, 0x0B, 0x3A, 0x19, 0xF6, 0xA2, 0x01,
    0x4D, 0xD3, 0xC3, 0x17, 0xB0, 0x43, 0xB8, 0x5D, 0xA2, 0xAB, 0xE3, 0xE4, 0x69, 0xBD, 0x57, 0x21,
    0xEA, 0xCD, 0xE0, 0xC5, 0xE6, 0x65, 0x13, 0x96, 0x67, 0x9D, 0xD8, 0x8E, 0xA6, 0xE0, 0x42, 0xF1,
    0x6D, 0x5D, 0x5B, 0xD2, 0x55, 0x20, 0xB9, 0x1E, 0x59, 0x13, 0x3C, 0x17, 0xC2, 0x25, 0x56, 0xC7
};

static void rsaCalculateMgf1AndXor(void *data, size_t data_size, const void *h_src, size_t h_src_size);

bool rsa2048GenerateSha256BasedCustomAcidSignature(void *dst, const void *src, size_t size)
{
    if (!dst || !src || !size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u8 hash[SHA256_HASH_SIZE];
    u8 buf[MBEDTLS_MPI_MAX_SIZE];
    const char *pers = "rsa_sign_pss";
    size_t olen = 0;
    
    int ret;
    bool success = false;
    
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ctr_drbg_init(&ctr_drbg);
    
    mbedtls_entropy_context entropy;
    mbedtls_entropy_init(&entropy);
    
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    
    /* Calculate SHA-256 checksum for the input data */
    sha256CalculateHash(hash, src, size);
    
    /* Seed the random number generator */
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const u8*)pers, strlen(pers));
    if (ret != 0)
    {
        LOGFILE("mbedtls_ctr_drbg_seed failed! (%d)", ret);
        goto out;
    }
    
    /* Parse private key */
    ret = mbedtls_pk_parse_key(&pk, (u8*)g_rsa2048CustomAcidPrivateKey, strlen(g_rsa2048CustomAcidPrivateKey) + 1, NULL, 0);
    if (ret != 0)
    {
        LOGFILE("mbedtls_pk_parse_key failed! (%d)", ret);
        goto out;
    }
    
    /* Set RSA padding */
    mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
    
    /* Calculate hash signature */
    ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, 0, buf, &olen, mbedtls_ctr_drbg_random, &ctr_drbg);
    if (ret != 0)
    {
        LOGFILE("mbedtls_pk_sign failed! (%d)", ret);
        goto out;
    }
    
    /* Copy signature to output buffer */
    memcpy(dst, buf, RSA2048_SIGNATURE_SIZE);
    success = true;
    
out:
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    
    return success;
}

const u8 rsa2048GetCustomAcidPublicKey(void)
{
    return g_rsa2048CustomAcidPublicKey;
}

bool rsa2048OaepDecryptAndVerify(void *dst, size_t dst_size, const void *signature, const void *modulus, const void *exponent, size_t exponent_size, const void *label_hash, size_t *out_size)
{
    if (!dst || !dst_size || !signature || !modulus || !exponent || !exponent_size || !label_hash || !out_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u8 m_buf[RSA2048_SIGNATURE_SIZE] = {0};
    
    rc = splUserExpMod(signature, modulus, exponent, exponent_size, m_buf);
    if (R_FAILED(rc))
    {
        LOGFILE("splUserExpMod failed! (0x%08X)", rc);
        return false;
    }
    
    if (m_buf[0] != 0)
    {
        LOGFILE("Invalid PSS!");
        return false;
    }
    
    /* Unmask salt */
    rsaCalculateMgf1AndXor(m_buf + 1, 0x20, m_buf + 0x21, RSA2048_SIGNATURE_SIZE - 0x21);
    
    /* Unmask DB */
    rsaCalculateMgf1AndXor(m_buf + 0x21, RSA2048_SIGNATURE_SIZE - 0x21, m_buf + 1, 0x20);
    
    /* Validate label hash */
    const u8 *db = (const u8*)(m_buf + 0x21);
    if (memcmp(db, label_hash, SHA256_HASH_SIZE) != 0)
    {
        LOGFILE("Label hash validation failed! Wrong decryption keys?");
        return false;
    }
    
    /* Validate message prefix */
    const u8 *data = (const u8*)(db + 0x20);
    size_t remaining = (RSA2048_SIGNATURE_SIZE - 0x41);
    
    while(!*data && remaining)
    {
        data++;
        remaining--;
    }
    
    if (!remaining || *data++ != 1)
    {
        LOGFILE("Message prefix validation failed! Wrong decryption keys?");
        return false;
    }
    
    remaining--;
    *out_size = remaining;
    
    if (remaining > dst_size) remaining = dst_size;
    memcpy(dst, data, remaining);
    
    return true;
}

static void rsaCalculateMgf1AndXor(void *data, size_t data_size, const void *h_src, size_t h_src_size)
{
    if (!data || !data_size || !h_src || !h_src_size || h_src_size > RSA2048_SIGNATURE_SIZE)
    {
        LOGFILE("Invalid parameters!");
        return;
    }
    
    u32 seed = 0;
    size_t i, offset = 0;
    u8 mgf1_buf[SHA256_HASH_SIZE] = {0};
    u8 h_buf[RSA2048_SIGNATURE_SIZE] = {0};
    
    memcpy(h_buf, h_src, h_src_size);
    
    while(offset < data_size)
    {
        for(i = 0; i < 4; i++) h_buf[h_src_size + 3 - i] = ((seed >> (8 * i)) & 0xFF);
        
        sha256CalculateHash(mgf1_buf, h_buf, h_src_size + 4);
        
        for(i = offset; i < data_size && i < (offset + 0x20); i++) data[i] ^= mgf1_buf[i - offset];
        
        seed++;
        offset += 0x20;
    }
}
