#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "nca.h"
#include "keys.h"
#include "utils.h"







size_t aes128XtsNintendoCrypt(Aes128XtsContext *ctx, void *dst, const void *src, size_t size, u64 sector, bool encrypt)
{
    if (!ctx || !dst || !src || !size || (size % NCA_AES_XTS_SECTOR_SIZE) != 0)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    size_t i, crypt_res = 0;
    u64 cur_sector = sector;
    
    u8 *dst_u8 = (u8*)dst;
    const u8 *src_u8 = (const u8*)src;
    
    for(i = 0; i < size; i += NCA_AES_XTS_SECTOR_SIZE, cur_sector++)
    {
        /* We have to force a sector reset on each new sector to actually enable Nintendo AES-XTS cipher tweak */
        aes128XtsContextResetSector(ctx, cur_sector, true);
        
        if (encrypt)
        {
            crypt_res = aes128XtsEncrypt(ctx, dst_u8 + i, src_u8 + i, NCA_AES_XTS_SECTOR_SIZE);
        } else {
            crypt_res = aes128XtsDecrypt(ctx, dst_u8 + i, src_u8 + i, NCA_AES_XTS_SECTOR_SIZE);
        }
        
        if (crypt_res != NCA_AES_XTS_SECTOR_SIZE) break;
    }
    
    return i;
}




bool ncaDecryptKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    Result rc = 0;
    u8 tmp_kek[0x10] = {0};
    const u8 *kek_src = NULL;
    
    kek_src = keysGetKeyAreaEncryptionKeySource(ctx->header.kaek_index);
    if (!kek_src)
    {
        LOGFILE("Unable to retrieve KAEK source for index 0x%02X!", ctx->header.kaek_index);
        return false;
    }
    
    rc = splCryptoGenerateAesKek(kek_src, ctx->key_generation, 0, tmp_kek);
    if (R_FAILED(rc))
    {
        LOGFILE("splCryptoGenerateAesKek failed! (0x%08X)", rc);
        return false;
    }
    
    for(u8 i = 0; i < 4; i++)
    {
        rc = splCryptoGenerateAesKey(tmp_kek, ctx->header.encrypted_keys[i], ctx->decrypted_keys[i]);
        if (R_FAILED(rc))
        {
            LOGFILE("splCryptoGenerateAesKey failed! (0x%08X)", rc);
            return false;
        }
    }
    
    return true;
}

bool ncaEncryptNcaKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    Aes128Context key_area_ctx = {0};
    const u8 *kaek = NULL;
    
    kaek = keysGetKeyAreaEncryptionKey(ctx->key_generation, ctx->header.kaek_index);
    if (!kaek)
    {
        LOGFILE("Unable to retrieve KAEK for key generation 0x%02X and KAEK index 0x%02X!", ctx->key_generation, ctx->header.kaek_index);
        return false;
    }
    
    aes128ContextCreate(&key_area_ctx, kaek, true);
    for(u8 i = 0; i < 4; i++) aes128EncryptBlock(&key_area_ctx, ctx->header.encrypted_keys[i], ctx->decrypted_keys[i]);
    
    return true;
}









bool ncaDecryptHeader(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    u32 i, magic = 0;
    size_t crypt_res = 0;
    const u8 *header_key = NULL;
    Aes128XtsContext hdr_aes_ctx = {0};
    
    header_key = keysGetNcaHeaderKey();
    
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + 0x10, false);
    
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_HEADER_LENGTH, 0, false);
    if (crypt_res != NCA_HEADER_LENGTH)
    {
        LOGFILE("Invalid output length for decrypted NCA header! (0x%X != 0x%lX)", NCA_HEADER_LENGTH, crypt_res);
        return false;
    }
    
    magic = __builtin_bswap32(ctx->header.magic);
    
    switch(magic)
    {
        case NCA_NCA3_MAGIC:
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_FULL_HEADER_LENGTH, 0, false);
            break;
        case NCA_NCA2_MAGIC:
            for(i = 0; i < 4; i++)
            {
                if (ctx->header.fs_entries[i].enable_entry)
                {
                    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), sizeof(NcaFsHeader), 0, false);
                    if (crypt_res != sizeof(NcaFsHeader)) break;
                } else {
                    memset(&(ctx->header.fs_headers[i]), 0, sizeof(NcaFsHeader));
                }
            }
            break;
        case NCA_NCA0_MAGIC:
            
            break;
        default:
            LOGFILE("Invalid NCA magic word! Wrong header key? (0x%08X)", magic);
            return false;
    }
    
    
    
    
    
}









static void ncaUpdateAesCtrIv(u8 *ctr, u64 offset)
{
    if (!ctr) return;
    
    offset >>= 4;
    
    for(u8 i = 0; i < 8; i++)
    {
        ctr[0x10 - i - 1] = (u8)(offset & 0xFF);
        offset >>= 8;
    }
}

static void ncaUpdateAesCtrExIv(u8 *ctr, u32 ctr_val, u64 offset)
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