/*
 * nca.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include "nca.h"
#include "keys.h"
#include "aes.h"
#include "rsa.h"
#include "gamecard.h"

#define NCA_CRYPTO_BUFFER_SIZE  0x800000    /* 8 MiB. */

/* Global variables. */

static u8 *g_ncaCryptoBuffer = NULL;
static Mutex g_ncaCryptoBufferMutex = 0;

static const u8 g_nca0KeyAreaHash[SHA256_HASH_SIZE] = {
    0x9A, 0xBB, 0xD2, 0x11, 0x86, 0x00, 0x21, 0x9D, 0x7A, 0xDC, 0x5B, 0x43, 0x95, 0xF8, 0x4E, 0xFD,
    0xFF, 0x6B, 0x25, 0xEF, 0x9F, 0x96, 0x85, 0x28, 0x18, 0x9E, 0x76, 0xB0, 0x92, 0xF0, 0x6A, 0xCB
};

/* Function prototypes. */

static bool ncaDecryptHeader(NcaContext *ctx);
static bool ncaDecryptKeyArea(NcaContext *ctx);

NX_INLINE bool ncaIsVersion0KeyAreaEncrypted(NcaContext *ctx);
NX_INLINE u8 ncaGetKeyGenerationValue(NcaContext *ctx);
NX_INLINE bool ncaCheckRightsIdAvailability(NcaContext *ctx);

NX_INLINE void ncaInitializeAesCtrIv(u8 *out, const u8 *ctr, u64 offset);
NX_INLINE void ncaUpdateAesCtrIv(u8 *ctr, u64 offset);
NX_INLINE void ncaUpdateAesCtrExIv(u8 *ctr, u32 ctr_val, u64 offset);

static bool _ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, bool lock);
static bool _ncaReadAesCtrExStorageFromBktrSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool lock);

static void *_ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset, bool lock);

bool ncaAllocateCryptoBuffer(void)
{
    mutexLock(&g_ncaCryptoBufferMutex);
    if (!g_ncaCryptoBuffer) g_ncaCryptoBuffer = malloc(NCA_CRYPTO_BUFFER_SIZE);
    bool ret = (g_ncaCryptoBuffer != NULL);
    mutexUnlock(&g_ncaCryptoBufferMutex);
    return ret;
}

void ncaFreeCryptoBuffer(void)
{
    mutexLock(&g_ncaCryptoBufferMutex);
    if (g_ncaCryptoBuffer)
    {
        free(g_ncaCryptoBuffer);
        g_ncaCryptoBuffer = NULL;
    }
    mutexUnlock(&g_ncaCryptoBufferMutex);
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
    
    /* Check if we're dealing with a NCA0 with a plain text key area. */
    if (ncaIsVersion0KeyAreaEncrypted(ctx))
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
            if (crypt_res != (NCA_FULL_HEADER_LENGTH - NCA_HEADER_LENGTH))
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
            /* NCA0 FS section headers will be encrypted in-place, but they need to be written to their proper offsets. */
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

bool ncaInitializeContext(NcaContext *out, u8 storage_id, NcmContentStorage *ncm_storage, u8 hfs_partition_type, const NcmContentInfo *content_info, Ticket *tik)
{
    if (!out || !tik || (storage_id != NcmStorageId_GameCard && !ncm_storage) || (storage_id == NcmStorageId_GameCard && hfs_partition_type > GameCardHashFileSystemPartitionType_Secure) || \
        !content_info || content_info->content_type > NcmContentType_DeltaFragment)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Clear output NCA context. */
    memset(out, 0, sizeof(NcaContext));
    
    /* Fill NCA context. */
    out->storage_id = storage_id;
    out->ncm_storage = (out->storage_id != NcmStorageId_GameCard ? ncm_storage : NULL);
    
    memcpy(&(out->content_id), &(content_info->content_id), sizeof(NcmContentId));
    utilsGenerateHexStringFromData(out->content_id_str, sizeof(out->content_id_str), out->content_id.c, sizeof(out->content_id.c));
    
    out->content_type = content_info->content_type;
    out->id_offset = content_info->id_offset;
    
    ncaConvertNcmContentSizeToU64(content_info->size, &(out->content_size));
    if (out->content_size < NCA_FULL_HEADER_LENGTH)
    {
        LOGFILE("Invalid size for NCA \"%s\"!", out->content_id_str);
        return false;
    }
    
    if (out->storage_id == NcmStorageId_GameCard)
    {
        /* Retrieve gamecard NCA offset. */
        char nca_filename[0x30] = {0};
        sprintf(nca_filename, "%s.%s", out->content_id_str, out->content_type == NcmContentType_Meta ? "cnmt.nca" : "nca");
        
        if (!gamecardGetEntryInfoFromHashFileSystemPartitionByName(hfs_partition_type, nca_filename, &(out->gamecard_offset), NULL))
        {
            LOGFILE("Error retrieving offset for \"%s\" entry in secure hash FS partition!", nca_filename);
            return false;
        }
    }
    
    /* Read NCA header. */
    if (!ncaReadContentFile(out, &(out->header), sizeof(NcaHeader), 0))
    {
        LOGFILE("Failed to read NCA \"%s\" header!", out->content_id_str);
        return false;
    }
    
    /* Decrypt NCA header. */
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
    
    /* Fill additional NCA context info. */
    out->key_generation = ncaGetKeyGenerationValue(out);
    out->rights_id_available = ncaCheckRightsIdAvailability(out);
    
    if (out->rights_id_available)
    {
        /* Retrieve ticket. */
        /* This will return true if it has already been retrieved. */
        if (tikRetrieveTicketByRightsId(tik, &(out->header.rights_id), out->storage_id == NcmStorageId_GameCard))
        {
            /* Copy decrypted titlekey. */
            memcpy(out->titlekey, tik->dec_titlekey, 0x10);
            out->titlekey_retrieved = true;
        } else {
            LOGFILE("Error retrieving ticket for NCA \"%s\"!", out->content_id_str);
        }
    } else {
        /* Decrypt key area. */
        if (out->format_version != NcaVersion_Nca0 && !ncaDecryptKeyArea(out))
        {
            LOGFILE("Error decrypting NCA key area!");
            return false;
        }
    }
    
    /* Return right away if the NCA uses titlekey crypto and the titlekey couldn't be retrieved. */
    if (out->rights_id_available && !out->titlekey_retrieved) return true;
    
    /* Parse sections. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        /* Skip NCA section if it's not enabled in the FS entries. */
        if (!out->header.fs_entries[i].enable_entry) continue;
        
        /* Fill section context. */
        out->fs_contexts[i].nca_ctx = out;
        out->fs_contexts[i].section_num = i;
        out->fs_contexts[i].section_offset = NCA_FS_ENTRY_BLOCK_OFFSET(out->header.fs_entries[i].start_block_offset);
        out->fs_contexts[i].section_size = (NCA_FS_ENTRY_BLOCK_OFFSET(out->header.fs_entries[i].end_block_offset) - out->fs_contexts[i].section_offset);
        out->fs_contexts[i].section_type = NcaFsSectionType_Invalid; /* Placeholder. */
        out->fs_contexts[i].header = &(out->header.fs_headers[i]);
        
        /* Determine encryption type. */
        out->fs_contexts[i].encryption_type = (out->format_version == NcaVersion_Nca0 ? NcaEncryptionType_AesXts : out->header.fs_headers[i].encryption_type);
        if (out->fs_contexts[i].encryption_type == NcaEncryptionType_Auto)
        {
            switch(out->fs_contexts[i].section_num)
            {
                case 0: /* ExeFS Partition FS. */
                case 1: /* RomFS. */
                    out->fs_contexts[i].encryption_type = NcaEncryptionType_AesCtr;
                    break;
                case 2: /* Logo Partition FS. */
                    out->fs_contexts[i].encryption_type = NcaEncryptionType_None;
                    break;
                default:
                    break;
            }
        }
        
        /* Check if we're dealing with an invalid encryption type value. */
        if (out->fs_contexts[i].encryption_type == NcaEncryptionType_Auto || out->fs_contexts[i].encryption_type > NcaEncryptionType_AesCtrEx)
        {
            memset(&(out->fs_contexts[i]), 0, sizeof(NcaFsSectionContext));
            continue;
        }
        
        /* Determine FS section type. */
        if (out->fs_contexts[i].header->fs_type == NcaFsType_PartitionFs && out->fs_contexts[i].header->hash_type == NcaHashType_HierarchicalSha256)
        {
            out->fs_contexts[i].section_type = NcaFsSectionType_PartitionFs;
        } else
        if (out->fs_contexts[i].header->fs_type == NcaFsType_RomFs && out->fs_contexts[i].header->hash_type == NcaHashType_HierarchicalIntegrity)
        {
            out->fs_contexts[i].section_type = (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtrEx ? NcaFsSectionType_PatchRomFs : NcaFsSectionType_RomFs);
        } else
        if (out->fs_contexts[i].header->fs_type == NcaFsType_RomFs && out->fs_contexts[i].header->hash_type == NcaHashType_HierarchicalSha256 && out->format_version == NcaVersion_Nca0)
        {
            out->fs_contexts[i].section_type = NcaFsSectionType_Nca0RomFs;
        }
        
        /* Check if we're dealing with an invalid section type value. */
        if (out->fs_contexts[i].section_type >= NcaFsSectionType_Invalid)
        {
            memset(&(out->fs_contexts[i]), 0, sizeof(NcaFsSectionContext));
            continue;
        }
        
        /* Initialize crypto related fields. */
        if (out->fs_contexts[i].encryption_type > NcaEncryptionType_None && out->fs_contexts[i].encryption_type <= NcaEncryptionType_AesCtrEx)
        {
            /* Initialize section CTR. */
            ncaInitializeAesCtrIv(out->fs_contexts[i].ctr, out->fs_contexts[i].header->section_ctr, out->fs_contexts[i].section_offset);
            
            /* Initialize AES context. */
            if (out->rights_id_available)
            {
                aes128CtrContextCreate(&(out->fs_contexts[i].ctr_ctx), out->titlekey, out->fs_contexts[i].ctr);
            } else {
                if (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtr || out->fs_contexts[i].encryption_type == NcaEncryptionType_AesCtrEx)
                {
                    aes128CtrContextCreate(&(out->fs_contexts[i].ctr_ctx), out->decrypted_keys[2].key, out->fs_contexts[i].ctr);
                } else
                if (out->fs_contexts[i].encryption_type == NcaEncryptionType_AesXts)
                {
                    /* We need to create two different contexts: one for decryption and another one for encryption. */
                    aes128XtsContextCreate(&(out->fs_contexts[i].xts_decrypt_ctx), out->decrypted_keys[0].key, out->decrypted_keys[1].key, false);
                    aes128XtsContextCreate(&(out->fs_contexts[i].xts_encrypt_ctx), out->decrypted_keys[0].key, out->decrypted_keys[1].key, true);
                }
            }
        }
        
        /* Enable FS context if we got up to this point. */
        out->fs_contexts[i].enabled = true;
    }
    
    return true;
}

bool ncaReadContentFile(NcaContext *ctx, void *out, u64 read_size, u64 offset)
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
        /* Retrieve NCA data normally. */
        /* This strips NAX0 crypto from SD card NCAs (not used on eMMC NCAs). */
        rc = ncmContentStorageReadContentIdFile(ctx->ncm_storage, out, read_size, &(ctx->content_id), offset);
        ret = R_SUCCEEDED(rc);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (0x%08X) (ncm).", read_size, offset, ctx->content_id_str, rc);
    } else {
        /* Retrieve NCA data using raw gamecard reads. */
        /* Fixes NCA read issues with gamecards under HOS < 4.0.0 when using ncmContentStorageReadContentIdFile(). */
        ret = gamecardReadStorage(out, read_size, ctx->gamecard_offset + offset);
        if (!ret) LOGFILE("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (gamecard).", read_size, offset, ctx->content_id_str);
    }
    
    return ret;
}

bool ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset)
{
    return _ncaReadFsSection(ctx, out, read_size, offset, true);
}

bool ncaReadAesCtrExStorageFromBktrSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val)
{
    return _ncaReadAesCtrExStorageFromBktrSection(ctx, out, read_size, offset, ctr_val, true);
}

void *ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset)
{
    return _ncaGenerateEncryptedFsSectionBlock(ctx, data, data_size, data_offset, out_block_size, out_block_offset, true);
}

bool ncaGenerateHierarchicalSha256Patch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out)
{
    mutexLock(&g_ncaCryptoBufferMutex);
    
    NcaContext *nca_ctx = NULL;
    
    u64 hash_block_size = 0;
    u64 hash_data_layer_offset = 0, hash_data_layer_size = 0;
    u64 hash_target_layer_offset = 0, hash_target_layer_size = 0;
    u8 *hash_data_layer = NULL, *hash_target_block = NULL;
    
    bool success = false;
    
    if (!ctx || !ctx->enabled || !(nca_ctx = (NcaContext*)ctx->nca_ctx) || !ctx->header || ctx->header->hash_type != NcaHashType_HierarchicalSha256 || \
        ctx->header->encryption_type == NcaEncryptionType_AesCtrEx || !data || !data_size || !(hash_block_size = ctx->header->hash_info.hierarchical_sha256.hash_block_size) || \
        !(hash_data_layer_size = ctx->header->hash_info.hierarchical_sha256.hash_data_layer_info.size) || \
        !(hash_target_layer_size = ctx->header->hash_info.hierarchical_sha256.hash_target_layer_info.size) || data_offset >= hash_target_layer_size || \
        (data_offset + data_size) > hash_target_layer_size || !out)
    {
        LOGFILE("Invalid parameters!");
        goto exit;
    }
    
    /* Calculate required offsets and sizes. */
    hash_data_layer_offset = ctx->header->hash_info.hierarchical_sha256.hash_data_layer_info.offset;
    hash_target_layer_offset = ctx->header->hash_info.hierarchical_sha256.hash_target_layer_info.offset;
    
    u64 hash_data_start_offset = ((data_offset / hash_block_size) * SHA256_HASH_SIZE);
    u64 hash_data_end_offset = (((data_offset + data_size) / hash_block_size) * SHA256_HASH_SIZE);
    u64 hash_data_size = (hash_data_end_offset != hash_data_start_offset ? (hash_data_end_offset - hash_data_start_offset) : SHA256_HASH_SIZE);
    
    u64 hash_target_start_offset = (hash_target_layer_offset + ALIGN_DOWN(data_offset, hash_block_size));
    u64 hash_target_end_offset = (hash_target_layer_offset + ALIGN_UP(data_offset + data_size, hash_block_size));
    if (hash_target_end_offset > (hash_target_layer_offset + hash_target_layer_size)) hash_target_end_offset = (hash_target_layer_offset + hash_target_layer_size);
    u64 hash_target_size = (hash_target_end_offset - hash_target_start_offset);
    
    u64 hash_target_data_offset = (data_offset - ALIGN_DOWN(data_offset, hash_block_size));
    
    /* Allocate memory for the full hash data layer. */
    hash_data_layer = malloc(hash_data_layer_size);
    if (!hash_data_layer)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer for the full HierarchicalSha256 hash data layer!", hash_data_layer_size);
        goto exit;
    }
    
    /* Read full hash data layer. */
    if (!_ncaReadFsSection(ctx, hash_data_layer, hash_data_layer_size, hash_data_layer_offset, false))
    {
        LOGFILE("Failed to read full HierarchicalSha256 hash data layer!");
        goto exit;
    }
    
    /* Allocate memory for the modified hash target layer block. */
    hash_target_block = malloc(hash_target_size);
    if (!hash_target_block)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer for the modified HierarchicalSha256 hash target layer block!", hash_target_size);
        goto exit;
    }
    
    /* Read hash target layer block. */
    if (!_ncaReadFsSection(ctx, hash_target_block, hash_target_size, hash_target_start_offset, false))
    {
        LOGFILE("Failed to read HierarchicalSha256 hash target layer block!");
        goto exit;
    }
    
    /* Replace data. */
    memcpy(hash_target_block + hash_target_data_offset, data, data_size);
    
    /* Recalculate hashes. */
    for(u64 i = 0, j = 0; i < hash_target_size; i += hash_block_size, j++)
    {
        if (hash_block_size > (hash_target_size - i)) hash_block_size = (hash_target_size - i);
        sha256CalculateHash(hash_data_layer + hash_data_start_offset + (j * SHA256_HASH_SIZE), hash_target_block + i, hash_block_size);
    }
    
    /* Reencrypt modified hash data layer block. */
    out->hash_data_layer_patch.data = _ncaGenerateEncryptedFsSectionBlock(ctx, hash_data_layer + hash_data_start_offset, hash_data_size, hash_data_layer_offset + hash_data_start_offset, \
                                                                          &(out->hash_data_layer_patch.size), &(out->hash_data_layer_patch.offset), false);
    if (!out->hash_data_layer_patch.data)
    {
        LOGFILE("Failed to generate encrypted HierarchicalSha256 hash data layer block!");
        goto exit;
    }
    
    /* Reencrypt hash target layer block. */
    out->hash_target_layer_patch.data = _ncaGenerateEncryptedFsSectionBlock(ctx, hash_target_block + hash_target_data_offset, data_size, hash_target_layer_offset + data_offset, \
                                                                            &(out->hash_target_layer_patch.size), &(out->hash_target_layer_patch.offset), false);
    if (!out->hash_target_layer_patch.data)
    {
        LOGFILE("Failed to generate encrypted HierarchicalSha256 hash target layer block!");
        goto exit;
    }
    
    /* Recalculate master hash from hash info block. */
    sha256CalculateHash(ctx->header->hash_info.hierarchical_sha256.master_hash, hash_data_layer, hash_data_layer_size);
    
    /* Recalculate FS header hash. */
    sha256CalculateHash(nca_ctx->header.fs_hashes[ctx->section_num].hash, ctx->header, sizeof(NcaFsHeader));
    
    /* Enable the 'dirty_header' flag. */
    nca_ctx->dirty_header = true;
    
    success = true;
    
exit:
    if (hash_target_block) free(hash_target_block);
    
    if (hash_data_layer) free(hash_data_layer);
    
    if (!success) ncaFreeHierarchicalSha256Patch(out);
    
    mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return success;
}

bool ncaGenerateHierarchicalIntegrityPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalIntegrityPatch *out)
{
    mutexLock(&g_ncaCryptoBufferMutex);
    
    NcaContext *nca_ctx = NULL;
    bool success = false;
    
    u8 *cur_data = NULL;
    u64 cur_data_offset = data_offset;
    u64 cur_data_size = data_size;
    
    u8 *hash_data_block = NULL, *hash_target_block = NULL;
    
    if (!ctx || !ctx->enabled || !(nca_ctx = (NcaContext*)ctx->nca_ctx) || !ctx->header || ctx->header->hash_type != NcaHashType_HierarchicalIntegrity || \
        ctx->header->encryption_type == NcaEncryptionType_AesCtrEx || !data || !data_size || !out || data_offset >= ctx->header->hash_info.hierarchical_integrity.hash_target_layer_info.size || \
        (data_offset + data_size) > ctx->header->hash_info.hierarchical_integrity.hash_target_layer_info.size)
    {
        LOGFILE("Invalid parameters!");
        goto exit;
    }
    
    /* Process each IVFC layer. */
    for(u8 i = (NCA_IVFC_HASH_DATA_LAYER_COUNT + 1); i > 0; i--)
    {
        NcaHierarchicalIntegrityLayerInfo *cur_layer_info = (i > NCA_IVFC_HASH_DATA_LAYER_COUNT ? &(ctx->header->hash_info.hierarchical_integrity.hash_target_layer_info) : \
                                                             &(ctx->header->hash_info.hierarchical_integrity.hash_data_layer_info[i - 1]));
        
        NcaHierarchicalIntegrityLayerInfo *parent_layer_info = (i > 1 ? &(ctx->header->hash_info.hierarchical_integrity.hash_data_layer_info[i - 2]) : NULL);
        
        NcaHashInfoLayerPatch *cur_layer_patch = (i > NCA_IVFC_HASH_DATA_LAYER_COUNT ? &(out->hash_target_layer_patch) : &(out->hash_data_layer_patch[i - 1]));
        
        if (!cur_layer_info->size || !cur_layer_info->block_size || (parent_layer_info && (!parent_layer_info->size || !parent_layer_info->block_size)))
        {
            LOGFILE("Invalid HierarchicalIntegrity parent/child layer!");
            goto exit;
        }
        
        /* Calculate required offsets and sizes. */
        u64 hash_block_size = NCA_IVFC_BLOCK_SIZE(cur_layer_info->block_size);
        
        u64 hash_data_layer_offset = 0;
        u64 hash_data_start_offset = 0, hash_data_end_offset = 0, hash_data_size = 0;
        
        u64 hash_target_layer_offset = cur_layer_info->offset, hash_target_layer_size = cur_layer_info->size;
        u64 hash_target_start_offset = 0, hash_target_end_offset = 0, hash_target_size = 0, hash_target_data_offset = 0;
        
        if (parent_layer_info)
        {
            /* HierarchicalIntegrity layer from L1 to L5. */
            hash_data_layer_offset = parent_layer_info->offset;
            
            hash_data_start_offset = ((cur_data_offset / hash_block_size) * SHA256_HASH_SIZE);
            hash_data_end_offset = (((cur_data_offset + cur_data_size) / hash_block_size) * SHA256_HASH_SIZE);
            hash_data_size = (hash_data_end_offset != hash_data_start_offset ? (hash_data_end_offset - hash_data_start_offset) : SHA256_HASH_SIZE);
            
            hash_target_start_offset = (hash_target_layer_offset + ALIGN_DOWN(cur_data_offset, hash_block_size));
            hash_target_end_offset = (hash_target_layer_offset + ALIGN_UP(cur_data_offset + cur_data_size, hash_block_size));
            hash_target_size = (hash_target_end_offset - hash_target_start_offset);
        } else {
            /* HierarchicalIntegrity master layer. */
            /* The master hash is calculated over the whole layer and saved to the NCA FS header. */
            hash_target_start_offset = hash_target_layer_offset;
            hash_target_end_offset = (hash_target_layer_offset + hash_target_layer_size);
            hash_target_size = hash_target_layer_size;
        }
        
        hash_target_data_offset = (cur_data_offset - ALIGN_DOWN(cur_data_offset, hash_block_size));
        
        /* Allocate memory for our hash target layer block. */
        hash_target_block = calloc(hash_target_size, sizeof(u8));
        if (!hash_target_block)
        {
            LOGFILE("Unable to allocate 0x%lX bytes for the HierarchicalIntegrity hash target layer block!");
            goto exit;
        }
        
        /* Adjust hash target layer end offset and size if needed to avoid read errors. */
        if (hash_target_end_offset > (hash_target_layer_offset + hash_target_layer_size))
        {
            hash_target_end_offset = (hash_target_layer_offset + hash_target_layer_size);
            hash_target_size = (hash_target_end_offset - hash_target_start_offset);
        }
        
        /* Read hash target layer block. */
        if (!_ncaReadFsSection(ctx, hash_target_block, hash_target_size, hash_target_start_offset, false))
        {
            LOGFILE("Failed to read HierarchicalIntegrity hash target layer block!");
            goto exit;
        }
        
        /* Replace hash target layer block data. */
        memcpy(hash_target_block + hash_target_data_offset, (i > NCA_IVFC_HASH_DATA_LAYER_COUNT ? data : cur_data), cur_data_size);
        
        if (parent_layer_info)
        {
            /* Allocate memory for our hash data layer block. */
            hash_data_block = calloc(hash_data_size, sizeof(u8));
            if (!hash_data_block)
            {
                LOGFILE("Unable to allocate 0x%lX bytes for the HierarchicalIntegrity hash data layer block!");
                goto exit;
            }
            
            /* Read hash target layer block. */
            if (!_ncaReadFsSection(ctx, hash_data_block, hash_data_size, hash_data_layer_offset + hash_data_start_offset, false))
            {
                LOGFILE("Failed to read HierarchicalIntegrity hash data layer block!");
                goto exit;
            }
            
            /* Recalculate hashes. */
            /* Size isn't truncated for blocks smaller than the hash block size, unlike HierarchicalSha256, so we just keep using the same hash block size throughout the loop. */
            /* For these specific cases, the rest of the block should be filled with zeroes (already taken care of by using calloc()). */
            for(u64 i = 0, j = 0; i < hash_target_size; i += hash_block_size, j++) sha256CalculateHash(hash_data_block + (j * SHA256_HASH_SIZE), hash_target_block + i, hash_block_size);
        } else {
            /* Recalculate master hash from hash info block. */
            sha256CalculateHash(ctx->header->hash_info.hierarchical_integrity.master_hash, hash_target_block, hash_target_size);
        }
        
        /* Reencrypt hash target layer block. */
        cur_layer_patch->data = _ncaGenerateEncryptedFsSectionBlock(ctx, hash_target_block + hash_target_data_offset, cur_data_size, hash_target_layer_offset + cur_data_offset, \
                                                                    &(cur_layer_patch->size), &(cur_layer_patch->offset), false);
        if (!cur_layer_patch->data)
        {
            LOGFILE("Failed to generate encrypted HierarchicalIntegrity hash target layer block!");
            goto exit;
        }
        
        /* Free hash target layer block. */
        free(hash_target_block);
        hash_target_block = NULL;
        
        if (parent_layer_info)
        {
            /* Free previous layer data if necessary. */
            if (cur_data) free(cur_data);
            
            /* Prepare data for the next target layer. */
            cur_data = hash_data_block;
            cur_data_offset = hash_data_start_offset;
            cur_data_size = hash_data_size;
            hash_data_block = NULL;
        }
    }
    
    /* Recalculate FS header hash. */
    sha256CalculateHash(nca_ctx->header.fs_hashes[ctx->section_num].hash, ctx->header, sizeof(NcaFsHeader));
    
    /* Enable the 'dirty_header' flag. */
    nca_ctx->dirty_header = true;
    
    success = true;
    
exit:
    if (hash_data_block) free(hash_data_block);
    
    if (hash_target_block) free(hash_target_block);
    
    if (cur_data) free(cur_data);
    
    if (!success) ncaFreeHierarchicalIntegrityPatch(out);
    
    mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return success;
}

static bool ncaDecryptHeader(NcaContext *ctx)
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
            
            /* We first need to decrypt the key area from the NCA0 header in order to access its FS section headers. */
            if (!ncaDecryptKeyArea(ctx))
            {
                LOGFILE("Error decrypting NCA0 \"%s\" key area!", ctx->content_id_str);
                return false;
            }
            
            aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_keys[0].key, ctx->decrypted_keys[1].key, false);
            
            for(i = 0; i < NCA_FS_HEADER_COUNT; i++)
            {
                if (!ctx->header.fs_entries[i].enable_entry) continue;
                
                /* FS headers are not part of NCA0 headers. */
                fs_header_offset = NCA_FS_ENTRY_BLOCK_OFFSET(ctx->header.fs_entries[i].start_block_offset);
                if (!ncaReadContentFile(ctx, &(ctx->header.fs_headers[i]), NCA_FS_HEADER_LENGTH, fs_header_offset))
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
            LOGFILE("Invalid NCA \"%s\" magic word! Wrong header key? (0x%08X).", ctx->content_id_str, magic);
            return false;
    }
    
    return true;
}

static bool ncaDecryptKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    Result rc = 0;
    const u8 *kek_src = NULL;
    u8 key_count, tmp_kek[0x10] = {0};
    
    /* Check if we're dealing with a NCA0 with a plain text key area. */
    if (ncaIsVersion0KeyAreaEncrypted(ctx))
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
        LOGFILE("splCryptoGenerateAesKek failed! (0x%08X).", rc);
        return false;
    }
    
    key_count = (ctx->format_version == NcaVersion_Nca0 ? 2 : 4);
    
    for(u8 i = 0; i < key_count; i++)
    {
        rc = splCryptoGenerateAesKey(tmp_kek, ctx->header.encrypted_keys[i].key, ctx->decrypted_keys[i].key);
        if (R_FAILED(rc))
        {
            LOGFILE("splCryptoGenerateAesKey failed! (0x%08X).", rc);
            return false;
        }
    }
    
    return true;
}

NX_INLINE bool ncaIsVersion0KeyAreaEncrypted(NcaContext *ctx)
{
    if (!ctx || ctx->format_version != NcaVersion_Nca0) return false;
    
    u8 nca0_key_area_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(nca0_key_area_hash, ctx->header.encrypted_keys, 0x40);
    if (!memcmp(nca0_key_area_hash, g_nca0KeyAreaHash, SHA256_HASH_SIZE)) return false;
    
    return true;
}

NX_INLINE u8 ncaGetKeyGenerationValue(NcaContext *ctx)
{
    if (!ctx) return 0;
    return (ctx->header.key_generation > ctx->header.key_generation_old ? ctx->header.key_generation : ctx->header.key_generation_old);
}

NX_INLINE bool ncaCheckRightsIdAvailability(NcaContext *ctx)
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

NX_INLINE void ncaInitializeAesCtrIv(u8 *out, const u8 *ctr, u64 offset)
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

NX_INLINE void ncaUpdateAesCtrIv(u8 *ctr, u64 offset)
{
    if (!ctr) return;
    
    offset >>= 4;
    
    for(u8 i = 0; i < 8; i++)
    {
        ctr[0x10 - i - 1] = (u8)(offset & 0xFF);
        offset >>= 8;
    }
}

NX_INLINE void ncaUpdateAesCtrExIv(u8 *ctr, u32 ctr_val, u64 offset)
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

static bool _ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, bool lock)
{
    if (lock) mutexLock(&g_ncaCryptoBufferMutex);
    
    bool ret = false;
    
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_num >= NCA_FS_HEADER_COUNT || ctx->section_offset < NCA_FULL_HEADER_LENGTH || \
        ctx->section_type >= NcaFsSectionType_Invalid || ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type > NcaEncryptionType_AesCtrEx || !ctx->header || !out || !read_size || \
        offset >= ctx->section_size || (offset + read_size) > ctx->section_size)
    {
        LOGFILE("Invalid NCA FS section header parameters!");
        goto exit;
    }
    
    size_t crypt_res = 0;
    u64 sector_num = 0;
    
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + offset);
    
    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 data_start_offset = 0, chunk_size = 0, out_chunk_size = 0;
    
    if (!strlen(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        content_offset >= nca_ctx->content_size || (content_offset + read_size) > nca_ctx->content_size)
    {
        LOGFILE("Invalid NCA header parameters!");
        goto exit;
    }
    
    /* Optimization for reads from plaintext FS sections or reads that are aligned to the AES-CTR / AES-XTS sector size. */
    if (ctx->encryption_type == NcaEncryptionType_None || \
        (ctx->encryption_type == NcaEncryptionType_AesXts && !(content_offset % NCA_AES_XTS_SECTOR_SIZE) && !(read_size % NCA_AES_XTS_SECTOR_SIZE)) || \
        ((ctx->encryption_type == NcaEncryptionType_AesCtr || ctx->encryption_type == NcaEncryptionType_AesCtrEx) && !(content_offset % AES_BLOCK_SIZE) && !(read_size % AES_BLOCK_SIZE)))
    {
        /* Read data. */
        if (!ncaReadContentFile(nca_ctx, out, read_size, content_offset))
        {
            LOGFILE("Failed to read 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
            goto exit;
        }
        
        /* Return right away if we're dealing with a plaintext FS section. */
        if (ctx->encryption_type == NcaEncryptionType_None)
        {
            ret = true;
            goto exit;
        }
        
        /* Decrypt data. */
        if (ctx->encryption_type == NcaEncryptionType_AesXts)
        {
            sector_num = ((ctx->encryption_type == NcaEncryptionType_AesXts ? offset : (content_offset - NCA_HEADER_LENGTH)) / NCA_AES_XTS_SECTOR_SIZE);
            
            crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_decrypt_ctx), out, out, read_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, false);
            if (crypt_res != read_size)
            {
                LOGFILE("Failed to AES-XTS decrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
                goto exit;
            }
        } else
        if (ctx->encryption_type == NcaEncryptionType_AesCtr || ctx->encryption_type == NcaEncryptionType_AesCtrEx)
        {
            ncaUpdateAesCtrIv(ctx->ctr, content_offset);
            aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
            aes128CtrCrypt(&(ctx->ctr_ctx), out, out, read_size);
        }
        
        ret = true;
        goto exit;
    }
    
    /* Calculate offsets and block sizes. */
    block_start_offset = ALIGN_DOWN(content_offset, ctx->encryption_type == NcaEncryptionType_AesXts ? NCA_AES_XTS_SECTOR_SIZE : AES_BLOCK_SIZE);
    block_end_offset = ALIGN_UP(content_offset + read_size, ctx->encryption_type == NcaEncryptionType_AesXts ? NCA_AES_XTS_SECTOR_SIZE : AES_BLOCK_SIZE);
    block_size = (block_end_offset - block_start_offset);
    
    data_start_offset = (content_offset - block_start_offset);
    chunk_size = (block_size > NCA_CRYPTO_BUFFER_SIZE ? NCA_CRYPTO_BUFFER_SIZE : block_size);
    out_chunk_size = (block_size > NCA_CRYPTO_BUFFER_SIZE ? (NCA_CRYPTO_BUFFER_SIZE - data_start_offset) : read_size);
    
    /* Read data. */
    if (!ncaReadContentFile(nca_ctx, g_ncaCryptoBuffer, chunk_size, block_start_offset))
    {
        LOGFILE("Failed to read 0x%lX bytes encrypted data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, ctx->section_num);
        goto exit;
    }
    
    /* Decrypt data. */
    if (ctx->encryption_type == NcaEncryptionType_AesXts)
    {
        sector_num = ((ctx->encryption_type == NcaEncryptionType_AesXts ? offset : (content_offset - NCA_HEADER_LENGTH)) / NCA_AES_XTS_SECTOR_SIZE);
        
        crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_decrypt_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, false);
        if (crypt_res != chunk_size)
        {
            LOGFILE("Failed to AES-XTS decrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, ctx->section_num);
            goto exit;
        }
    } else
    if (ctx->encryption_type == NcaEncryptionType_AesCtr || ctx->encryption_type == NcaEncryptionType_AesCtrEx)
    {
        ncaUpdateAesCtrIv(ctx->ctr, block_start_offset);
        aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
        aes128CtrCrypt(&(ctx->ctr_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size);
    }
    
    /* Copy decrypted data. */
    memcpy(out, g_ncaCryptoBuffer + data_start_offset, out_chunk_size);
    
    ret = (block_size > NCA_CRYPTO_BUFFER_SIZE ? _ncaReadFsSection(ctx, (u8*)out + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size, false) : true);
    
exit:
    if (lock) mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return ret;
}

static bool _ncaReadAesCtrExStorageFromBktrSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool lock)
{
    if (lock) mutexLock(&g_ncaCryptoBufferMutex);
    
    bool ret = false;
    
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_num >= NCA_FS_HEADER_COUNT || ctx->section_offset < NCA_FULL_HEADER_LENGTH || \
        ctx->section_type != NcaFsSectionType_PatchRomFs || ctx->encryption_type != NcaEncryptionType_AesCtrEx || !ctx->header || !out || !read_size || offset >= ctx->section_size || \
        (offset + read_size) > ctx->section_size)
    {
        LOGFILE("Invalid NCA FS section header parameters!");
        goto exit;
    }
    
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + offset);
    
    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 data_start_offset = 0, chunk_size = 0, out_chunk_size = 0;
    
    if (!strlen(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        content_offset >= nca_ctx->content_size || (content_offset + read_size) > nca_ctx->content_size)
    {
        LOGFILE("Invalid NCA header parameters!");
        goto exit;
    }
    
    /* Optimization for reads that are aligned to the AES-CTR sector size. */
    if (!(content_offset % AES_BLOCK_SIZE) && !(read_size % AES_BLOCK_SIZE))
    {
        /* Read data. */
        if (!ncaReadContentFile(nca_ctx, out, read_size, content_offset))
        {
            LOGFILE("Failed to read 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
            goto exit;
        }
        
        /* Decrypt data */
        ncaUpdateAesCtrExIv(ctx->ctr, ctr_val, content_offset);
        aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
        aes128CtrCrypt(&(ctx->ctr_ctx), out, out, read_size);
        
        ret = true;
        goto exit;
    }
    
    /* Calculate offsets and block sizes. */
    block_start_offset = ALIGN_DOWN(content_offset, AES_BLOCK_SIZE);
    block_end_offset = ALIGN_UP(content_offset + read_size, AES_BLOCK_SIZE);
    block_size = (block_end_offset - block_start_offset);
    
    data_start_offset = (content_offset - block_start_offset);
    chunk_size = (block_size > NCA_CRYPTO_BUFFER_SIZE ? NCA_CRYPTO_BUFFER_SIZE : block_size);
    out_chunk_size = (block_size > NCA_CRYPTO_BUFFER_SIZE ? (NCA_CRYPTO_BUFFER_SIZE - data_start_offset) : read_size);
    
    /* Read data. */
    if (!ncaReadContentFile(nca_ctx, g_ncaCryptoBuffer, chunk_size, block_start_offset))
    {
        LOGFILE("Failed to read 0x%lX bytes encrypted data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, ctx->section_num);
        goto exit;
    }
    
    /* Decrypt data. */
    ncaUpdateAesCtrExIv(ctx->ctr, ctr_val, block_start_offset);
    aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
    aes128CtrCrypt(&(ctx->ctr_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size);
    
    /* Copy decrypted data. */
    memcpy(out, g_ncaCryptoBuffer + data_start_offset, out_chunk_size);
    
    ret = (block_size > NCA_CRYPTO_BUFFER_SIZE ? _ncaReadAesCtrExStorageFromBktrSection(ctx, (u8*)out + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size, ctr_val, false) : true);
    
exit:
    if (lock) mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return ret;
}

static void *_ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset, bool lock)
{
    if (lock) mutexLock(&g_ncaCryptoBufferMutex);
    
    u8 *out = NULL;
    bool success = false;
    
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_num >= NCA_FS_HEADER_COUNT || ctx->section_offset < NCA_FULL_HEADER_LENGTH || \
        ctx->section_type >= NcaFsSectionType_Invalid || ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type >= NcaEncryptionType_AesCtrEx || !ctx->header || !data || !data_size || \
        data_offset >= ctx->section_size || (data_offset + data_size) > ctx->section_size || !out_block_size || !out_block_offset)
    {
        LOGFILE("Invalid NCA FS section header parameters!");
        goto exit;
    }
    
    size_t crypt_res = 0;
    u64 sector_num = 0;
    
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + data_offset);
    
    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 plain_chunk_offset = 0;
    
    if (!strlen(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        content_offset >= nca_ctx->content_size || (content_offset + data_size) > nca_ctx->content_size)
    {
        LOGFILE("Invalid NCA header parameters!");
        goto exit;
    }
    
    /* Optimization for blocks from plaintext FS sections or blocks that are aligned to the AES-CTR / AES-XTS sector size. */
    if (ctx->encryption_type == NcaEncryptionType_None || \
        (ctx->encryption_type == NcaEncryptionType_AesXts && !(content_offset % NCA_AES_XTS_SECTOR_SIZE) && !(data_size % NCA_AES_XTS_SECTOR_SIZE)) || \
        (ctx->encryption_type == NcaEncryptionType_AesCtr && !(content_offset % AES_BLOCK_SIZE) && !(data_size % AES_BLOCK_SIZE)))
    {
        /* Allocate memory. */
        out = malloc(data_size);
        if (!out)
        {
            LOGFILE("Unable to allocate 0x%lX bytes buffer! (aligned).", data_size);
            goto exit;
        }
        
        /* Copy data. */
        memcpy(out, data, data_size);
        
        /* Encrypt data. */
        if (ctx->encryption_type == NcaEncryptionType_AesXts)
        {
            sector_num = ((ctx->encryption_type == NcaEncryptionType_AesXts ? data_offset : (content_offset - NCA_HEADER_LENGTH)) / NCA_AES_XTS_SECTOR_SIZE);
            
            crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_encrypt_ctx), out, out, data_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, true);
            if (crypt_res != data_size)
            {
                LOGFILE("Failed to AES-XTS encrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", data_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
                goto exit;
            }
        } else
        if (ctx->encryption_type == NcaEncryptionType_AesCtr)
        {
            ncaUpdateAesCtrIv(ctx->ctr, content_offset);
            aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
            aes128CtrCrypt(&(ctx->ctr_ctx), out, out, data_size);
        }
        
        *out_block_size = data_size;
        *out_block_offset = content_offset;
        
        success = true;
        goto exit;
    }
    
    /* Calculate block offsets and size. */
    block_start_offset = ALIGN_DOWN(data_offset, ctx->encryption_type == NcaEncryptionType_AesXts ? NCA_AES_XTS_SECTOR_SIZE : AES_BLOCK_SIZE);
    block_end_offset = ALIGN_UP(data_offset + data_size, ctx->encryption_type == NcaEncryptionType_AesXts ? NCA_AES_XTS_SECTOR_SIZE : AES_BLOCK_SIZE);
    block_size = (block_end_offset - block_start_offset);
    
    plain_chunk_offset = (data_offset - block_start_offset);
    content_offset = (ctx->section_offset + block_start_offset);
    
    /* Allocate memory. */
    out = malloc(block_size);
    if (!out)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer! (unaligned).", block_size);
        goto exit;
    }
    
    /* Read decrypted data using aligned offset and size. */
    if (!_ncaReadFsSection(ctx, out, block_size, block_start_offset, false))
    {
        LOGFILE("Failed to read decrypted NCA \"%s\" FS section #%u data block!", nca_ctx->content_id_str, ctx->section_num);
        goto exit;
    }
    
    /* Replace plaintext data. */
    memcpy(out + plain_chunk_offset, data, data_size);
    
    /* Reencrypt data. */
    if (ctx->encryption_type == NcaEncryptionType_AesXts)
    {
        sector_num = ((ctx->encryption_type == NcaEncryptionType_AesXts ? block_start_offset : (content_offset - NCA_HEADER_LENGTH)) / NCA_AES_XTS_SECTOR_SIZE);
        
        crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_encrypt_ctx), out, out, block_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, true);
        if (crypt_res != block_size)
        {
            LOGFILE("Failed to AES-XTS encrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", block_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
            goto exit;
        }
    } else
    if (ctx->encryption_type == NcaEncryptionType_AesCtr)
    {
        ncaUpdateAesCtrIv(ctx->ctr, content_offset);
        aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
        aes128CtrCrypt(&(ctx->ctr_ctx), out, out, block_size);
    }
    
    *out_block_size = block_size;
    *out_block_offset = content_offset;
    
    success = true;
    
exit:
    if (!success && out)
    {
        free(out);
        out = NULL;
    }
    
    if (lock) mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return out;
}
