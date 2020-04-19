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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "nca.h"
#include "keys.h"
#include "rsa.h"
#include "gamecard.h"
#include "utils.h"

/* Global variables. */

static const u8 g_nca0KeyAreaHash[SHA256_HASH_SIZE] = {
    0x9A, 0xBB, 0xD2, 0x11, 0x86, 0x00, 0x21, 0x9D, 0x7A, 0xDC, 0x5B, 0x43, 0x95, 0xF8, 0x4E, 0xFD,
    0xFF, 0x6B, 0x25, 0xEF, 0x9F, 0x96, 0x85, 0x28, 0x18, 0x9E, 0x76, 0xB0, 0x92, 0xF0, 0x6A, 0xCB
};

/* Function prototypes. */

static inline bool ncaCheckIfVersion0KeyAreaIsEncrypted(NcaContext *ctx);
static void ncaUpdateAesCtrIv(u8 *ctr, u64 offset);
static void ncaUpdateAesCtrExIv(u8 *ctr, u32 ctr_val, u64 offset);

size_t aes128XtsNintendoCrypt(Aes128XtsContext *ctx, void *dst, const void *src, size_t size, u64 sector, size_t sector_size, bool encrypt)
{
    if (!ctx || !dst || !src || !size || !sector_size || (size % sector_size) != 0)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    size_t i, crypt_res = 0;
    u64 cur_sector = sector;
    
    u8 *dst_u8 = (u8*)dst;
    const u8 *src_u8 = (const u8*)src;
    
    for(i = 0; i < size; i += sector_size, cur_sector++)
    {
        /* We have to force a sector reset on each new sector to actually enable Nintendo AES-XTS cipher tweak */
        aes128XtsContextResetSector(ctx, cur_sector, true);
        
        if (encrypt)
        {
            crypt_res = aes128XtsEncrypt(ctx, dst_u8 + i, src_u8 + i, sector_size);
        } else {
            crypt_res = aes128XtsDecrypt(ctx, dst_u8 + i, src_u8 + i, sector_size);
        }
        
        if (crypt_res != sector_size) break;
    }
    
    return i;
}

bool ncaRead(NcaContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || (ctx->storage_id != NcmStorageId_GameCard && !ctx->ncm_storage) || (ctx->storage_id == NcmStorageId_GameCard && !ctx->gamecard_offset) || !out || !read_size || \
        offset >= ctx->size || (offset + read_size) > ctx->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    bool ret = false;
    
    if (ctx->storage_id == NcmStorageId_GameCard)
    {
        ret = gamecardRead(out, read_size, ctx->gamecard_offset + offset);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (gamecard)", read_size, offset, ctx->id_str);
    } else {
        rc = ncmContentStorageReadContentIdFile(ctx->ncm_storage, out, read_size, &(ctx->id), offset);
        ret = R_SUCCEEDED(rc);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (0x%08X) (ncm)", read_size, offset, ctx->id_str, rc);
    }
    
    return ret;
}















bool ncaDecryptKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    Result rc = 0;
    const u8 *kek_src = NULL;
    u8 key_count, tmp_kek[0x10] = {0};
    
    /* Check if we're dealing with a NCA0 with a plain text key area */
    if (ctx->format_version == NcaVersion_Nca0 && !ncaCheckIfVersion0KeyAreaIsEncrypted(ctx))
    {
        memcpy(ctx->decrypted_keys, ctx->header.encrypted_keys, 0x40);
        return true;
    }
    
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
    
    key_count = (ctx->format_version == NcaVersion_Nca0 ? 2 : 4);
    
    for(u8 i = 0; i < key_count; i++)
    {
        rc = splCryptoGenerateAesKey(tmp_kek, ctx->header.encrypted_keys[i].key, ctx->decrypted_keys[i].key);
        if (R_FAILED(rc))
        {
            LOGFILE("splCryptoGenerateAesKey failed! (0x%08X)", rc);
            return false;
        }
    }
    
    return true;
}

bool ncaEncryptKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    u8 key_count;
    const u8 *kaek = NULL;
    Aes128Context key_area_ctx = {0};
    
    /* Check if we're dealing with a NCA0 with a plain text key area */
    if (ctx->format_version == NcaVersion_Nca0 && !ncaCheckIfVersion0KeyAreaIsEncrypted(ctx))
    {
        memcpy(ctx->header.encrypted_keys, ctx->decrypted_keys, 0x40);
        return true;
    }
    
    kaek = keysGetKeyAreaEncryptionKey(ctx->key_generation, ctx->header.kaek_index);
    if (!kaek)
    {
        LOGFILE("Unable to retrieve KAEK for key generation 0x%02X and KAEK index 0x%02X!", ctx->key_generation, ctx->header.kaek_index);
        return false;
    }
    
    key_count = (ctx->format_version == NcaVersion_Nca0 ? 2 : 4);
    
    aes128ContextCreate(&key_area_ctx, kaek, true);
    for(u8 i = 0; i < key_count; i++) aes128EncryptBlock(&key_area_ctx, ctx->header.encrypted_keys[i].key, ctx->decrypted_keys[i].key);
    
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
    u64 fs_header_offset = 0;
    const u8 *header_key = NULL;
    Aes128XtsContext hdr_aes_ctx = {0}, nca0_fs_header_ctx = {0};
    
    header_key = keysGetNcaHeaderKey();
    
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + 0x10, false);
    
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, false);
    if (crypt_res != NCA_HEADER_LENGTH)
    {
        LOGFILE("Invalid output length for decrypted NCA header! (0x%X != 0x%lX)", NCA_HEADER_LENGTH, crypt_res);
        return false;
    }
    
    magic = __builtin_bswap32(ctx->header.magic);
    
    switch(magic)
    {
        case NCA_NCA3_MAGIC:
            ctx->format_version = NcaVersion_Nca3;
            
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_FULL_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, false);
            if (crypt_res != NCA_FULL_HEADER_LENGTH)
            {
                LOGFILE("Error decrypting full NCA3 header!");
                return false;
            }
            
            break;
        case NCA_NCA2_MAGIC:
            ctx->format_version = NcaVersion_Nca2;
            
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, false);
                if (crypt_res != NCA_FS_HEADER_LENGTH)
                {
                    LOGFILE("Error decrypting NCA2 FS section header #%u!", i);
                    return false;
                }
            }
            
            break;
        case NCA_NCA0_MAGIC:
            ctx->format_version = NcaVersion_Nca0;
            
            /* We first need to decrypt the key area from the NCA0 header in order to access its FS section headers */
            if (!ncaDecryptKeyArea(ctx))
            {
                LOGFILE("Error decrypting key area from NCA0 header!");
                return false;
            }
            
            aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_keys[0].key, ctx->decrypted_keys[1].key, false);
            
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                /* FS headers are not part of NCA0 headers */
                fs_header_offset = NCA_FS_ENTRY_BLOCK_OFFSET(ctx->header.fs_entries[i].start_block_offset);
                if (!ncaRead(ctx, &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, fs_header_offset))
                {
                    LOGFILE("Failed to read NCA0 FS section header #%u at offset 0x%lX!", i, fs_header_offset);
                    return false;
                }
                
                crypt_res = aes128XtsNintendoCrypt(&nca0_fs_header_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, (fs_header_offset - 0x400) >> 9, \
                                                   NCA_AES_XTS_SECTOR_SIZE, false);
                if (crypt_res != NCA_FS_HEADER_LENGTH)
                {
                    LOGFILE("Error decrypting NCA0 FS section header #%u!", i);
                    return false;
                }
            }
            
            break;
        default:
            LOGFILE("Invalid NCA magic word! Wrong header key? (0x%08X)", magic);
            return false;
    }
    
    /* Fill additional context info */
    ctx->key_generation = ncaGetKeyGenerationValue(ctx);
    ctx->rights_id_available = ncaCheckRightsIdAvailability(ctx);
    
    return true;
}

bool ncaEncryptHeader(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    u32 i;
    size_t crypt_res = 0;
    const u8 *header_key = NULL;
    Aes128XtsContext hdr_aes_ctx = {0};
    
    header_key = keysGetNcaHeaderKey();
    
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + 0x10, true);
    
    switch(ctx->format_version)
    {
        case NcaVersion_Nca3:
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_FULL_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, true);
            if (crypt_res != NCA_FULL_HEADER_LENGTH)
            {
                LOGFILE("Error encrypting full NCA3 header!");
                return false;
            }
            
            break;
        case NcaVersion_Nca2:
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, true);
            if (crypt_res != NCA_HEADER_LENGTH)
            {
                LOGFILE("Error encrypting partial NCA2 header!");
                return false;
            }
            
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, true);
                if (crypt_res != NCA_FS_HEADER_LENGTH)
                {
                    LOGFILE("Error encrypting NCA2 FS section header #%u!", i);
                    return false;
                }
            }
            
            break;
        case NcaVersion_Nca0:
            /* There's nothing else to do */
            break;
        default:
            LOGFILE("Invalid NCA format version! (0x%02X)", ctx->format_version);
            return false;
    }
    
    return true;
}















static inline bool ncaCheckIfVersion0KeyAreaIsEncrypted(NcaContext *ctx)
{
    if (!ctx || ctx->format_version != NcaVersion_Nca0) return false;
    
    u8 nca0_key_area_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(nca0_key_area_hash, ctx->header.encrypted_keys, 0x40);
    
    if (!memcmp(nca0_key_area_hash, g_nca0KeyAreaHash, SHA256_HASH_SIZE)) return false;
    
    return true;
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
