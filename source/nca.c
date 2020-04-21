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

#define NCA_CRYPTO_BUFFER_SIZE  0x800000    /* 8 MiB */

/* Global variables. */

static u8 *g_ncaCryptoBuffer = NULL;

static const u8 g_nca0KeyAreaHash[SHA256_HASH_SIZE] = {
    0x9A, 0xBB, 0xD2, 0x11, 0x86, 0x00, 0x21, 0x9D, 0x7A, 0xDC, 0x5B, 0x43, 0x95, 0xF8, 0x4E, 0xFD,
    0xFF, 0x6B, 0x25, 0xEF, 0x9F, 0x96, 0x85, 0x28, 0x18, 0x9E, 0x76, 0xB0, 0x92, 0xF0, 0x6A, 0xCB
};

/* Function prototypes. */

static bool ncaCheckIfVersion0KeyAreaIsEncrypted(NcaContext *ctx);

static inline u8 ncaGetKeyGenerationValue(NcaContext *ctx);
static inline bool ncaCheckRightsIdAvailability(NcaContext *ctx);

static void ncaInitializeAesCtrIv(u8 *out, const u8 *ctr, u64 offset);
static void ncaUpdateAesCtrIv(u8 *ctr, u64 offset);
static void ncaUpdateAesCtrExIv(u8 *ctr, u32 ctr_val, u64 offset);

bool ncaAllocateCryptoBuffer(void)
{
    if (g_ncaCryptoBuffer) return true;
    
    g_ncaCryptoBuffer = malloc(NCA_CRYPTO_BUFFER_SIZE);
    
    return (g_ncaCryptoBuffer != NULL);
}

void ncaFreeCryptoBuffer(void)
{
    if (g_ncaCryptoBuffer)
    {
        free(g_ncaCryptoBuffer);
        g_ncaCryptoBuffer = NULL;
    }
}

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
        crypt_res = (encrypt ? aes128XtsEncrypt(ctx, dst_u8 + i, src_u8 + i, sector_size) : aes128XtsDecrypt(ctx, dst_u8 + i, src_u8 + i, sector_size));
        if (crypt_res != sector_size) break;
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
    if (!ctx || !strlen(ctx->content_id_str))
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
        LOGFILE("Error decrypting partial NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }
    
    magic = __builtin_bswap32(ctx->header.magic);
    
    switch(magic)
    {
        case NCA_NCA3_MAGIC:
            ctx->format_version = NcaVersion_Nca3;
            
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, ctx->header.fs_headers, ctx->header.fs_headers, NCA_FULL_HEADER_LENGTH - NCA_HEADER_LENGTH, 2, NCA_AES_XTS_SECTOR_SIZE, false);
            if (crypt_res != (NCA_FULL_HEADER_LENGTH - NCA_HEADER_LENGTH))
            {
                LOGFILE("Error decrypting NCA3 \"%s\" FS section headers!", ctx->content_id_str);
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
                    LOGFILE("Error decrypting NCA2 \"%s\" FS section header #%u!", ctx->content_id_str, i);
                    return false;
                }
            }
            
            break;
        case NCA_NCA0_MAGIC:
            ctx->format_version = NcaVersion_Nca0;
            
            /* We first need to decrypt the key area from the NCA0 header in order to access its FS section headers */
            if (!ncaDecryptKeyArea(ctx))
            {
                LOGFILE("Error decrypting NCA0 \"%s\" key area!", ctx->content_id_str);
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
                    LOGFILE("Failed to read NCA0 \"%s\" FS section header #%u at offset 0x%lX!", ctx->content_id_str, i, fs_header_offset);
                    return false;
                }
                
                crypt_res = aes128XtsNintendoCrypt(&nca0_fs_header_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, \
                                                   NCA_NCA0_FS_HEADER_AES_XTS_SECTOR(fs_header_offset), NCA_AES_XTS_SECTOR_SIZE, false);
                if (crypt_res != NCA_FS_HEADER_LENGTH)
                {
                    LOGFILE("Error decrypting NCA0 \"%s\" FS section header #%u!", ctx->content_id_str, i);
                    return false;
                }
            }
            
            break;
        default:
            LOGFILE("Invalid NCA \"%s\" magic word! Wrong header key? (0x%08X)", ctx->content_id_str, magic);
            return false;
    }
    
    return true;
}

bool ncaEncryptHeader(NcaContext *ctx)
{
    if (!ctx || !strlen(ctx->content_id_str))
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
    
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->header), NCA_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, true);
    if (crypt_res != NCA_HEADER_LENGTH)
    {
        LOGFILE("Error encrypting partial NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }
    
    switch(ctx->format_version)
    {
        case NcaVersion_Nca3:
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, ctx->header.fs_headers, ctx->header.fs_headers, NCA_FULL_HEADER_LENGTH - NCA_HEADER_LENGTH, 2, NCA_AES_XTS_SECTOR_SIZE, true);
            if (crypt_res != NCA_FULL_HEADER_LENGTH)
            {
                LOGFILE("Error encrypting NCA3 \"%s\" FS section headers!", ctx->content_id_str);
                return false;
            }
            
            break;
        case NcaVersion_Nca2:
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, 0, NCA_AES_XTS_SECTOR_SIZE, true);
                if (crypt_res != NCA_FS_HEADER_LENGTH)
                {
                    LOGFILE("Error encrypting NCA2 \"%s\" FS section header #%u!", ctx->content_id_str, i);
                    return false;
                }
            }
            
            break;
        case NcaVersion_Nca0:
            /* NCA0 FS section headers will be encrypted in-place, but they need to be written to their proper offsets */
            aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_keys[0].key, ctx->decrypted_keys[1].key, true);
            
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                fs_header_offset = NCA_FS_ENTRY_BLOCK_OFFSET(ctx->header.fs_entries[i].start_block_offset);
                
                crypt_res = aes128XtsNintendoCrypt(&nca0_fs_header_ctx, &(ctx->header.fs_headers[i]), &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, \
                                                   NCA_NCA0_FS_HEADER_AES_XTS_SECTOR(fs_header_offset), NCA_AES_XTS_SECTOR_SIZE, true);
                if (crypt_res != NCA_FS_HEADER_LENGTH)
                {
                    LOGFILE("Error decrypting NCA0 \"%s\" FS section header #%u!", ctx->content_id_str, i);
                    return false;
                }
            }
            
            break;
        default:
            LOGFILE("Invalid NCA \"%s\" format version! (0x%02X)", ctx->content_id_str, ctx->format_version);
            return false;
    }
    
    return true;
}

bool ncaInitializeContext(NcaContext *out, Ticket *tik, u8 storage_id, NcmContentStorage *ncm_storage, u8 hfs_partition_type, const NcmPackagedContentInfo *content_info)
{
    if (!out || !tik || (storage_id != NcmStorageId_GameCard && !ncm_storage) || (storage_id == NcmStorageId_GameCard && hfs_partition_type > GameCardHashFileSystemPartitionType_Secure) || \
        !content_info || content_info->info.content_type > NcmContentType_DeltaFragment)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Fill NCA context */
    out->storage_id = storage_id;
    out->ncm_storage = (out->storage_id != NcmStorageId_GameCard ? ncm_storage : NULL);
    
    memcpy(&(out->content_id), &(content_info->info.content_id), sizeof(NcmContentId));
    utilsGenerateHexStringFromData(out->content_id_str, sizeof(out->content_id_str), out->content_id.c, sizeof(out->content_id.c));
    
    memcpy(out->hash, content_info->hash, SHA256_HASH_SIZE);
    utilsGenerateHexStringFromData(out->hash_str, sizeof(out->hash_str), out->hash, sizeof(out->hash));
    
    out->content_type = content_info->info.content_type;
    out->id_offset = content_info->info.id_offset;
    
    ncaConvertNcmContentSizeToU64(content_info->info.size, &(out->content_size));
    if (out->content_size < NCA_FULL_HEADER_LENGTH)
    {
        LOGFILE("Invalid size for NCA \"%s\"!", out->content_id_str);
        return false;
    }
    
    out->rights_id_available = out->dirty_header = false;
    
    if (out->storage_id == NcmStorageId_GameCard)
    {
        /* Retrieve gamecard NCA offset */
        char nca_filename[0x30] = {0};
        sprintf(nca_filename, "%s.%s", out->content_id_str, out->content_type == NcmContentType_Meta ? "cnmt.nca" : "nca");
        
        if (!gamecardGetOffsetAndSizeFromHashFileSystemPartitionEntryByName(hfs_partition_type, nca_filename, &(out->gamecard_offset), NULL))
        {
            LOGFILE("Error retrieving offset for \"%s\" entry in secure hash FS partition!", nca_filename);
            return false;
        }
    }
    
    /* Read NCA header */
    if (!ncaReadContent(out, &(out->header), sizeof(NcaHeader), 0))
    {
        LOGFILE("Failed to read NCA \"%s\" header!", out->content_id_str);
        return false;
    }
    
    /* Decrypt NCA header */
    if (!ncaDecryptHeader(out))
    {
        LOGFILE("Failed to decrypt NCA \"%s\" header!", out->content_id_str);
        return false;
    }
    
    if (out->header.content_size != out->content_size)
    {
        LOGFILE("Content size mismatch for NCA \"%s\"! (0x%lX != 0x%lX)", out->content_id_str, out->header.content_size, out->content_size);
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
            LOGFILE("Error retrieving ticket for NCA \"%s\"!", out->content_id_str);
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
        out->fs_contexts[i].section_offset = NCA_FS_ENTRY_BLOCK_OFFSET(out->header.fs_entries[i].start_block_offset);
        out->fs_contexts[i].section_size = (NCA_FS_ENTRY_BLOCK_OFFSET(out->header.fs_entries[i].end_block_offset) - out->fs_contexts[i].section_offset);
        out->fs_contexts[i].section_type = NcaSectionType_Invalid; /* Placeholder */
        out->fs_contexts[i].header = &(out->header.fs_headers[i]);
        
        /* Determine encryption type */
        out->fs_contexts[i].encryption_type = (out->format_version == NcaVersion_Nca0 ? NcaEncryptionType_Nca0 : out->header.fs_headers[i].encryption_type);
        if (out->fs_contexts[i].encryption_type == NcaEncryptionType_Auto)
        {
            switch(out->fs_contexts[i].section_num)
            {
                case 0: /* ExeFS PFS0 */
                case 1: /* RomFS */
                    out->fs_contexts[i].encryption_type = NcaEncryptionType_AesCtr;
                    break;
                case 2: /* Logo PFS0 */
                    out->fs_contexts[i].encryption_type = NcaEncryptionType_None;
                    break;
                default:
                    break;
            }
        }
        
        /* Check if we're dealing with an invalid encryption type value */
        if (out->fs_contexts[i].encryption_type == NcaEncryptionType_Auto || out->fs_contexts[i].encryption_type > NcaEncryptionType_Nca0) continue;
        
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
        
        /* Check if we're dealing with an invalid section type value */
        if (out->fs_contexts[i].section_type >= NcaSectionType_Invalid) continue;
        
        /* Initialize crypto related fields */
        if (out->fs_contexts[i].encryption_type > NcaEncryptionType_None && out->fs_contexts[i].encryption_type <= NcaEncryptionType_Nca0)
        {
            /* Initialize section CTR */
            ncaInitializeAesCtrIv(out->fs_contexts[i].ctr, out->fs_contexts[i].header->section_ctr, out->fs_contexts[i].section_offset);
            
            /* Initialize AES context */
            if (out->rights_id_available)
            {
                aes128CtrContextCreate(&(out->fs_contexts[i].ctr_ctx), out->titlekey, out->fs_contexts[i].ctr);
            } else {
                if (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtr || out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtrEx)
                {
                    aes128CtrContextCreate(&(out->fs_contexts[i].ctr_ctx), out->decrypted_keys[2].key, out->fs_contexts[i].ctr);
                } else
                if (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesXts || out->fs_contexts[i].encryption_type == NcaEncryptionType_Nca0)
                {
                    aes128XtsContextCreate(&(out->fs_contexts[i].xts_ctx), out->decrypted_keys[0].key, out->decrypted_keys[1].key, false);
                }
            }
        } else {
            memset(out->fs_contexts[i].ctr, 0, sizeof(out->fs_contexts[i].ctr));
            memset(&(out->fs_contexts[i].ctr_ctx), 0, sizeof(Aes128CtrContext));
            memset(&(out->fs_contexts[i].xts_ctx), 0, sizeof(Aes128XtsContext));
        }
    }
    
    return true;
}

bool ncaReadContent(NcaContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !strlen(ctx->content_id_str) || (ctx->storage_id != NcmStorageId_GameCard && !ctx->ncm_storage) || (ctx->storage_id == NcmStorageId_GameCard && !ctx->gamecard_offset) || !out || \
        !read_size || offset >= ctx->content_size || (offset + read_size) > ctx->content_size)
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
        rc = ncmContentStorageReadContentIdFile(ctx->ncm_storage, out, read_size, &(ctx->content_id), offset);
        ret = R_SUCCEEDED(rc);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (0x%08X) (ncm)", read_size, offset, ctx->content_id_str, rc);
    } else {
        /* Retrieve NCA data using raw gamecard reads */
        /* Fixes NCA read issues with gamecards under HOS < 4.0.0 when using ncmContentStorageReadContentIdFile() */
        ret = gamecardRead(out, read_size, ctx->gamecard_offset + offset);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (gamecard)", read_size, offset, ctx->content_id_str);
    }
    
    return ret;
}

bool ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!g_ncaCryptoBuffer || !ctx || !ctx->nca_ctx || ctx->section_num >= NCA_FS_HEADER_COUNT || ctx->section_offset < NCA_FULL_HEADER_LENGTH || ctx->section_type >= NcaSectionType_Invalid || \
        ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type > NcaEncryptionType_Nca0 || !ctx->header || !out || !read_size || offset >= ctx->section_size || \
        (offset + read_size) > ctx->section_size)
    {
        LOGFILE("Invalid NCA FS section header parameters!");
        return false;
    }
    
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + offset);
    
    if (!strlen(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        content_offset >= nca_ctx->content_size || (content_offset + read_size) > nca_ctx->content_size)
    {
        LOGFILE("Invalid NCA header parameters!");
        return false;
    }
    
    /* Read data right away if we're dealing with a FS section with no crypto */
    if (ctx->encryption_type == NcaEncryptionType_None) return ncaReadContent(nca_ctx, out, read_size, content_offset);
    
    /* Calculate offsets and block sizes */
    size_t crypt_res = 0;
    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 chunk_size = 0, out_chunk_size = 0;
    u64 data_start_offset = 0, sector_num = 0;
    
    switch(ctx->encryption_type)
    {
        case NcaEncryptionType_AesXts:
        case NcaEncryptionType_Nca0:
            block_start_offset = ROUND_DOWN(content_offset, NCA_AES_XTS_SECTOR_SIZE);
            block_end_offset = ROUND_UP(content_offset + read_size, NCA_AES_XTS_SECTOR_SIZE);
            sector_num = (ctx->encryption_type == NcaEncryptionType_AesXts ? offset : (content_offset - NCA_HEADER_LENGTH));
            sector_num /= NCA_AES_XTS_SECTOR_SIZE;
            break;
        case NcaEncryptionType_AesCtr:
        case NcaEncryptionType_AesCtrEx:    /* This function is only supposed to be used on Patch RomFS sections when *not* reading BKTR subsections */
            block_start_offset = ROUND_DOWN(content_offset, AES_BLOCK_SIZE);
            block_end_offset = ROUND_UP(content_offset + read_size, AES_BLOCK_SIZE);
            ncaUpdateAesCtrIv(ctx->ctr, block_start_offset);
            break;
        default:
            break;
    }
    
    block_size = (block_end_offset - block_start_offset);
    chunk_size = (block_size > NCA_CRYPTO_BUFFER_SIZE ? NCA_CRYPTO_BUFFER_SIZE : block_size);
    
    data_start_offset = (content_offset - block_start_offset);
    out_chunk_size = (block_size > NCA_CRYPTO_BUFFER_SIZE ? (NCA_CRYPTO_BUFFER_SIZE - data_start_offset) : read_size);
    
    /* Read data */
    if (!ncaReadContent(nca_ctx, g_ncaCryptoBuffer, chunk_size, block_start_offset))
    {
        LOGFILE("Failed to read 0x%lX bytes encrypted data block at offset 0x%lX from NCA \"%s\" FS section #%u!", chunk_size, block_start_offset, nca_ctx->content_id_str, ctx->section_num);
        return false;
    }
    
    /* Decrypt data */
    switch(ctx->encryption_type)
    {
        case NcaEncryptionType_AesXts:
        case NcaEncryptionType_Nca0:
            crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, false);
            if (crypt_res != chunk_size)
            {
                LOGFILE("Failed to decrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u!", chunk_size, block_start_offset, nca_ctx->content_id_str, ctx->section_num);
                return false;
            }
            break;
        case NcaEncryptionType_AesCtr:
        case NcaEncryptionType_AesCtrEx:    /* This function is only supposed to be used on Patch RomFS sections when *not* reading BKTR subsections */
            aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
            aes128CtrCrypt(&(ctx->ctr_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size);
            break;
        default:
            break;
    }
    
    /* Copy decrypted data */
    memcpy(out, g_ncaCryptoBuffer + data_start_offset, out_chunk_size);
    
    if (block_size > NCA_CRYPTO_BUFFER_SIZE) return ncaReadFsSection(ctx, (u8*)out + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size);
    
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

static inline u8 ncaGetKeyGenerationValue(NcaContext *ctx)
{
    if (!ctx) return 0;
    return (ctx->header.key_generation > ctx->header.key_generation_old ? ctx->header.key_generation : ctx->header.key_generation_old);
}

static inline bool ncaCheckRightsIdAvailability(NcaContext *ctx)
{
    if (!ctx) return false;
    
    bool rights_id_available = false;
    
    for(u8 i = 0; i < 0x10; i++)
    {
        if (ctx->header.rights_id.c[i] != 0)
        {
            rights_id_available = true;
            break;
        }
    }
    
    return rights_id_available;
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
