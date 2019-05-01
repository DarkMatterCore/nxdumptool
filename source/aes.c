#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "aes.h"
#include "ui.h"
#include "util.h"

/* Extern variables */

extern int breaks;
extern int font_height;
extern char strbuf[NAME_BUF_LEN * 4];

/* Allocate a new context. */
aes_ctx_t *new_aes_ctx(const void *key, unsigned int key_size, aes_mode_t mode)
{
    int ret;
    aes_ctx_t *ctx;
    
    if ((ctx = malloc(sizeof(*ctx))) == NULL)
    {
        uiDrawString("Error: failed to allocate aes_ctx_t!", 0, breaks * font_height, 255, 0, 0);
        return NULL;
    }
    
    mbedtls_cipher_init(&ctx->cipher_dec);
    mbedtls_cipher_init(&ctx->cipher_enc);
    
    ret = mbedtls_cipher_setup(&ctx->cipher_dec, mbedtls_cipher_info_from_type(mode));
    if (ret)
    {
        free_aes_ctx(ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to set up AES decryption context! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return NULL;
    }
    
    ret = mbedtls_cipher_setup(&ctx->cipher_enc, mbedtls_cipher_info_from_type(mode));
    if (ret)
    {
        free_aes_ctx(ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to set up AES encryption context! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return NULL;
    }
    
    ret = mbedtls_cipher_setkey(&ctx->cipher_dec, key, key_size * 8, AES_DECRYPT);
    if (ret)
    {
        free_aes_ctx(ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to set key for AES decryption context! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return NULL;
    }
    
    ret = mbedtls_cipher_setkey(&ctx->cipher_enc, key, key_size * 8, AES_ENCRYPT);
    if (ret)
    {
        free_aes_ctx(ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to set key for AES encryption context! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return NULL;
    }
    
    return ctx;
}

/* Free an allocated context. */
void free_aes_ctx(aes_ctx_t *ctx)
{
    /* Explicitly allow NULL. */
    if (ctx == NULL) return;
    mbedtls_cipher_free(&ctx->cipher_dec);
    mbedtls_cipher_free(&ctx->cipher_enc);
    free(ctx);
}

/* Set AES CTR or IV for a context. */
int aes_setiv(aes_ctx_t *ctx, const void *iv, size_t l)
{
    int ret;
    
    ret = mbedtls_cipher_set_iv(&ctx->cipher_dec, iv, l);
    if (ret)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to set IV for AES decryption context! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    ret = mbedtls_cipher_set_iv(&ctx->cipher_enc, iv, l);
    if (ret)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to set IV for AES encryption context! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    return 1;
}

/* Calculate CMAC. */
int aes_calculate_cmac(void *dst, void *src, size_t size, const void *key)
{
    int ret;
    mbedtls_cipher_context_t m_ctx;
    
    mbedtls_cipher_init(&m_ctx);
    
    ret = mbedtls_cipher_setup(&m_ctx, mbedtls_cipher_info_from_type(MBEDTLS_CIPHER_AES_128_ECB));
    if (ret)
    {
        mbedtls_cipher_free(&m_ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to set up AES context for CMAC calculation! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    ret = mbedtls_cipher_cmac_starts(&m_ctx, key, 0x80);
    if (ret)
    {
        mbedtls_cipher_free(&m_ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to start CMAC calculation! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    ret = mbedtls_cipher_cmac_update(&m_ctx, src, size);
    if (ret)
    {
        mbedtls_cipher_free(&m_ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to update CMAC calculation! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    ret = mbedtls_cipher_cmac_finish(&m_ctx, dst);
    if (ret)
    {
        mbedtls_cipher_free(&m_ctx);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to finish CMAC calculation! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    mbedtls_cipher_free(&m_ctx);
    
    return 1;
}


/* Encrypt with context. */
int aes_encrypt(aes_ctx_t *ctx, void *dst, const void *src, size_t l)
{
    int ret;
    size_t out_len = 0;
    
    /* Prepare context */
    mbedtls_cipher_reset(&ctx->cipher_enc);
    
    /* XTS doesn't need per-block updating */
    if (mbedtls_cipher_get_cipher_mode(&ctx->cipher_enc) == MBEDTLS_MODE_XTS)
    {
        ret = mbedtls_cipher_update(&ctx->cipher_enc, (const unsigned char *)src, l, (unsigned char *)dst, &out_len);
    } else {
        unsigned int blk_size = mbedtls_cipher_get_block_size(&ctx->cipher_enc);
        
        /* Do per-block updating */
        for (int offset = 0; (unsigned int)offset < l; offset += blk_size)
        {
            int len = (((unsigned int)(l - offset) > blk_size) ? blk_size : (unsigned int)(l - offset));
            ret = mbedtls_cipher_update(&ctx->cipher_enc, (const unsigned char *)src + offset, len, (unsigned char *)dst + offset, &out_len);
            if (ret) break;
        }
    }
    
    if (ret)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: AES encryption failed! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    /* Flush all data */
    size_t strbuf_size = sizeof(strbuf);
    ret = mbedtls_cipher_finish(&ctx->cipher_enc, (unsigned char *)strbuf, &strbuf_size); // Looks ugly, but using NULL,NULL with mbedtls on Switch is a no-no
    if (ret)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to finalize cipher for AES encryption! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    return 1;
}

/* Decrypt with context. */
int aes_decrypt(aes_ctx_t *ctx, void *dst, const void *src, size_t l) 
{
    int ret;
    bool src_equals_dst = false; 
    
    if (src == dst)
    {
        src_equals_dst = true;
        
        dst = malloc(l);
        if (dst == NULL)
        {
            uiDrawString("Error: failed to allocate buffer for AES decryption!", 0, breaks * font_height, 255, 0, 0);
            return 0;
        }
    }
    
    size_t out_len = 0;
    
    /* Prepare context */
    mbedtls_cipher_reset(&ctx->cipher_dec);
    
    /* XTS doesn't need per-block updating */
    if (mbedtls_cipher_get_cipher_mode(&ctx->cipher_dec) == MBEDTLS_MODE_XTS)
    {
        ret = mbedtls_cipher_update(&ctx->cipher_dec, (const unsigned char *)src, l, (unsigned char *)dst, &out_len);
    } else {
        unsigned int blk_size = mbedtls_cipher_get_block_size(&ctx->cipher_dec);
        
        /* Do per-block updating */
        for (int offset = 0; (unsigned int)offset < l; offset += blk_size)
        {
            int len = (((unsigned int)(l - offset) > blk_size) ? blk_size : (unsigned int)(l - offset));
            ret = mbedtls_cipher_update(&ctx->cipher_dec, (const unsigned char *)src + offset, len, (unsigned char *)dst + offset, &out_len);
            if (ret) break;
        }
    }
    
    if (ret)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: AES decryption failed! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    /* Flush all data */
    size_t strbuf_size = sizeof(strbuf);
    ret = mbedtls_cipher_finish(&ctx->cipher_dec, (unsigned char *)strbuf, &strbuf_size); // Looks ugly, but using NULL,NULL with mbedtls on Switch is a no-no
    if (ret)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to finalize cipher for AES decryption! (%d)", ret);
        uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    if (src_equals_dst)
    {
        memcpy((void*)src, dst, l);
        free(dst);
    }
    
    return 1;
}

static void get_tweak(unsigned char *tweak, size_t sector)
{
    /* Nintendo LE custom tweak... */
    for (int i = 0xF; i >= 0; i--)
    {
        tweak[i] = (unsigned char)(sector & 0xFF);
        sector >>= 8;
    }
}

/* Encrypt with context for XTS. */
int aes_xts_encrypt(aes_ctx_t *ctx, void *dst, const void *src, size_t l, size_t sector, size_t sector_size)
{
    int ret = 0;
    unsigned char tweak[0x10];
    
    if ((l % sector_size) != 0)
    {
        uiDrawString("Error: length must be a multiple of sector size in AES-XTS.", 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    for (size_t i = 0; i < l; i += sector_size)
    {
        /* Workaround for Nintendo's custom sector...manually generate the tweak. */
        get_tweak(tweak, sector++);
        
        ret = aes_setiv(ctx, tweak, 16);
        if (!ret) break;
        
        ret = aes_encrypt(ctx, (char *)dst + i, (const char *)src + i, sector_size);
        if (!ret) break;
    }
    
    return ret;
}

/* Decrypt with context for XTS. */
int aes_xts_decrypt(aes_ctx_t *ctx, void *dst, const void *src, size_t l, size_t sector, size_t sector_size)
{
    int ret = 0;
    unsigned char tweak[0x10];
    
    if ((l % sector_size) != 0)
    {
        uiDrawString("Error: length must be a multiple of sector size in AES-XTS.", 0, breaks * font_height, 255, 0, 0);
        return 0;
    }
    
    for (size_t i = 0; i < l; i += sector_size)
    {
        /* Workaround for Nintendo's custom sector...manually generate the tweak. */
        get_tweak(tweak, sector++);
        
        ret = aes_setiv(ctx, tweak, 16);
        if (!ret) break;
        
        ret = aes_decrypt(ctx, (char *)dst + i, (const char *)src + i, sector_size);
        if (!ret) break;
    }
    
    return ret;
}
