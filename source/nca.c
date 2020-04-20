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

static bool ncaCheckIfVersion0KeyAreaIsEncrypted(NcaContext *ctx);

static void ncaInitializeAesCtrIv(u8 *out, const u8 *ctr, u64 offset);
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

bool ncaReadContent(NcaContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || (ctx->storage_id != NcmStorageId_GameCard && !ctx->ncm_storage) || (ctx->storage_id == NcmStorageId_GameCard && !ctx->gamecard_offset) || !out || !read_size || \
        offset >= ctx->size || (offset + read_size) > ctx->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    bool ret = false;
    
    if (ctx->storage_id != NcmStorageId_GameCard)
    {
        /* Retrieve NCA data normally */
        /* This strips NAX0 crypto from SD card NCAs (not used on eMMC NCAs) */
        rc = ncmContentStorageReadContentIdFile(ctx->ncm_storage, out, read_size, &(ctx->id), offset);
        ret = R_SUCCEEDED(rc);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (0x%08X) (ncm)", read_size, offset, ctx->id_str, rc);
    } else {
        /* Retrieve NCA data using raw gamecard reads */
        /* Fixes NCA read issues with gamecards under HOS < 4.0.0 when using ncmContentStorageReadContentIdFile() */
        ret = gamecardRead(out, read_size, ctx->gamecard_offset + offset);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (gamecard)", read_size, offset, ctx->id_str);
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
    u8 tmp_hdr[NCA_HEADER_LENGTH] = {0};
    Aes128XtsContext hdr_aes_ctx = {0}, nca0_fs_header_ctx = {0};
    
    header_key = keysGetNcaHeaderKey();
    
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + 0x10, false);
    
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, tmp_hdr, &(ctx->header), NCA_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, false);
    if (crypt_res != NCA_HEADER_LENGTH)
    {
        LOGFILE("Invalid output length for decrypted NCA header! (0x%X != 0x%lX)", NCA_HEADER_LENGTH, crypt_res);
        return false;
    }
    
    memcpy(&magic, tmp_hdr + 0x200, sizeof(u32));
    magic = __builtin_bswap32(magic);
    
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
                LOGFILE("Error decrypting NCA0 key area!");
                return false;
            }
            
            aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_keys[0].key, ctx->decrypted_keys[1].key, false);
            
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                /* FS headers are not part of NCA0 headers */
                fs_header_offset = NCA_FS_ENTRY_BLOCK_OFFSET(ctx->header.fs_entries[i].start_block_offset);
                if (!ncaReadContent(ctx, &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, fs_header_offset))
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
    u64 fs_header_offset = 0;
    const u8 *header_key = NULL;
    Aes128XtsContext hdr_aes_ctx = {0}, nca0_fs_header_ctx = {0};
    
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
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, true);
            if (crypt_res != NCA_HEADER_LENGTH)
            {
                LOGFILE("Error encrypting NCA0 header!");
                return false;
            }
            
            /* NCA0 FS section headers will be encrypted in-place, but they need to be written to their proper offsets */
            aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_keys[0].key, ctx->decrypted_keys[1].key, true);
            
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                fs_header_offset = NCA_FS_ENTRY_BLOCK_OFFSET(ctx->header.fs_entries[i].start_block_offset);
                
                crypt_res = aes128XtsNintendoCrypt(&nca0_fs_header_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, (fs_header_offset - 0x400) >> 9, \
                                                   NCA_AES_XTS_SECTOR_SIZE, true);
                if (crypt_res != NCA_FS_HEADER_LENGTH)
                {
                    LOGFILE("Error decrypting NCA0 FS section header #%u!", i);
                    return false;
                }
            }
            
            break;
        default:
            LOGFILE("Invalid NCA format version! (0x%02X)", ctx->format_version);
            return false;
    }
    
    return true;
}

bool ncaProcessContent(NcaContext *out, Ticket *tik, u8 storage_id, NcmContentStorage *ncm_storage, const NcmContentId *id, const u8 *hash, u8 type, const u8 *size, u8 id_offset, u8 hfs_partition_type)
{
    if (!out || !tik || (storage_id != NcmStorageId_GameCard && !ncm_storage) || !id || !hash || type > NcmContentType_DeltaFragment || !size || \
        (storage_id == NcmStorageId_GameCard && hfs_partition_type > GameCardHashFileSystemPartitionType_Secure))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Fill NCA context */
    out->storage_id = storage_id;
    out->ncm_storage = (out->storage_id != NcmStorageId_GameCard ? ncm_storage : NULL);
    
    memcpy(&(out->id), id, sizeof(NcmContentId));
    utilsGenerateHexStringFromData(out->id_str, sizeof(out->id_str), out->id.c, sizeof(out->id.c));
    
    memcpy(out->hash, hash, SHA256_HASH_SIZE);
    utilsGenerateHexStringFromData(out->hash_str, sizeof(out->hash_str), out->hash, sizeof(out->hash));
    
    out->type = type;
    ncaConvertNcmContentSizeToU64(size, &(out->size));
    out->id_offset = id_offset;
    
    out->rights_id_available = out->dirty_header = false;
    
    if (out->storage_id == NcmStorageId_GameCard)
    {
        /* Retrieve gamecard NCA offset */
        char nca_filename[0x30] = {0};
        sprintf(nca_filename, "%s.%s", out->id_str, out->type == NcmContentType_Meta ? "cnmt.nca" : "nca");
        
        if (!gamecardGetOffsetAndSizeFromHashFileSystemPartitionEntryByName(hfs_partition_type, nca_filename, &(out->gamecard_offset), NULL))
        {
            LOGFILE("Error retrieving offset for \"%s\" entry in secure hash FS partition!", nca_filename);
            return false;
        }
    }
    
    /* Read NCA header */
    if (!ncaReadContent(out, &(out->header), sizeof(NcaHeader), 0))
    {
        LOGFILE("Failed to read NCA \"%s\" header!", out->id_str);
        return false;
    }
    
    /* Decrypt header */
    if (!ncaDecryptHeader(out))
    {
        LOGFILE("Failed to decrypt NCA \"%s\" header!", out->id_str);
        return false;
    }
    
    if (out->header.content_size != out->size)
    {
        LOGFILE("Content size mismatch for NCA \"%s\"! (0x%lX != 0x%lX)", out->header.content_size, out->size);
        return false;
    }
    
    /* Fill additional NCA context info */
    out->key_generation = ncaGetKeyGenerationValue(out);
    out->rights_id_available = ncaCheckRightsIdAvailability(out);
    
    if (out->rights_id_available)
    {
        /* Retrieve ticket */
        /* This will return true if it has already been retrieved */
        if (!tikRetrieveTicketByRightsId(tik, &(out->header.rights_id), out->storage_id == NcmStorageId_GameCard))
        {
            LOGFILE("Error retrieving ticket for NCA \"%s\"!", out->id_str);
            return false;
        }
        
        /* Copy decrypted titlekey */
        memcpy(out->titlekey, tik->dec_titlekey, 0x10);
    } else {
        /* Decrypt key area */
        if (out->format_version != NcaVersion_Nca0 && !ncaDecryptKeyArea(out))
        {
            LOGFILE("Error decrypting NCA key area!");
            return false;
        }
    }
    
    /* Parse sections */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        if (!out->header.fs_entries[i].enable_entry) continue;
        
        /* Fill section context */
        out->fs_contexts[i].nca_ctx = out;
        out->fs_contexts[i].section_num = i;
        out->fs_contexts[i].offset = NCA_FS_ENTRY_BLOCK_OFFSET(out->header.fs_entries[i].start_block_offset);
        out->fs_contexts[i].size = (NCA_FS_ENTRY_BLOCK_OFFSET(out->header.fs_entries[i].end_block_offset) - out->fs_contexts[i].offset);
        out->fs_contexts[i].section_type = NcaSectionType_Invalid; /* Placeholder */
        out->fs_contexts[i].encryption_type = (out->format_version == NcaVersion_Nca0 ? NcaEncryptionType_Nca0 : out->header.fs_headers[i].encryption_type);
        out->fs_contexts[i].header = &(out->header.fs_headers[i]);
        out->fs_contexts[i].use_xts = false;
        
        /* Determine FS section type */
        if (out->fs_contexts[i].header->fs_type == NcaFsType_PartitionFs && out->fs_contexts[i].header->hash_type == NcaHashType_HierarchicalSha256)
        {
            out->fs_contexts[i].section_type = NcaSectionType_PartitionFs;
        } else
        if (out->fs_contexts[i].header->fs_type == NcaFsType_RomFs && out->fs_contexts[i].header->hash_type == NcaHashType_HierarchicalIntegrity)
        {
            out->fs_contexts[i].section_type = (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtrEx ? NcaSectionType_PatchRomFs : NcaSectionType_RomFs);
        } else
        if (out->fs_contexts[i].header->fs_type == NcaFsType_RomFs && out->fs_contexts[i].header->hash_type == NcaHashType_HierarchicalSha256 && out->format_version == NcaVersion_Nca0)
        {
            out->fs_contexts[i].section_type = NcaSectionType_Nca0RomFs;
        }
        
        if (out->fs_contexts[i].section_type == NcaSectionType_Invalid || out->fs_contexts[i].encryption_type <= NcaEncryptionType_None) continue;
        
        /* Initialize section CTR */
        ncaInitializeAesCtrIv(out->fs_contexts[i].ctr, out->fs_contexts[i].header->section_ctr, out->fs_contexts[i].offset);
        
        /* Initialize AES context */
        if (out->rights_id_available)
        {
            /* AES-128-CTR */
            aes128CtrContextCreate(&(out->fs_contexts[i].ctr_ctx), out->titlekey, out->fs_contexts[i].ctr);
        } else {
            if (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtr || out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtrEx)
            {
                /* AES-128-CTR */
                aes128CtrContextCreate(&(out->fs_contexts[i].ctr_ctx), out->decrypted_keys[2].key, out->fs_contexts[i].ctr);
            } else
            if (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesXts || out->fs_contexts[i].encryption_type == NcaEncryptionType_Nca0)
            {
                /* AES-128-XTS */
                aes128XtsContextCreate(&(out->fs_contexts[i].xts_ctx), out->decrypted_keys[0].key, out->decrypted_keys[1].key, false);
                out->fs_contexts[i].use_xts = true;
            }
        }
    }
    
    return true;
}















static bool ncaCheckIfVersion0KeyAreaIsEncrypted(NcaContext *ctx)
{
    if (!ctx || ctx->format_version != NcaVersion_Nca0) return false;
    
    u8 nca0_key_area_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(nca0_key_area_hash, ctx->header.encrypted_keys, 0x40);
    
    if (!memcmp(nca0_key_area_hash, g_nca0KeyAreaHash, SHA256_HASH_SIZE)) return false;
    
    return true;
}

static void ncaInitializeAesCtrIv(u8 *out, const u8 *ctr, u64 offset)
{
    if (!out || !ctr) return;
    
    offset >>= 4;
    
    for(u8 i = 0; i < 8; i++)
    {
        out[i] = ctr[0x8 - i - 1];
        out[0x10 - i - 1] = (u8)(offset & 0xFF);
        offset >>= 8;
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
