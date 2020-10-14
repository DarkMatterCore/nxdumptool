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
#include "title.h"

#define NCA_CRYPTO_BUFFER_SIZE  0x800000    /* 8 MiB. */

/* Global variables. */

static u8 *g_ncaCryptoBuffer = NULL;
static Mutex g_ncaCryptoBufferMutex = 0;

static const u8 g_nca0KeyAreaHash[SHA256_HASH_SIZE] = {
    0x9A, 0xBB, 0xD2, 0x11, 0x86, 0x00, 0x21, 0x9D, 0x7A, 0xDC, 0x5B, 0x43, 0x95, 0xF8, 0x4E, 0xFD,
    0xFF, 0x6B, 0x25, 0xEF, 0x9F, 0x96, 0x85, 0x28, 0x18, 0x9E, 0x76, 0xB0, 0x92, 0xF0, 0x6A, 0xCB
};

/* Function prototypes. */

NX_INLINE bool ncaIsFsInfoEntryValid(NcaFsInfo *fs_info);

static bool ncaReadDecryptedHeader(NcaContext *ctx);
static bool ncaDecryptKeyArea(NcaContext *ctx);

static bool ncaEncryptKeyArea(NcaContext *ctx);

NX_INLINE bool ncaIsVersion0KeyAreaEncrypted(NcaContext *ctx);
NX_INLINE u8 ncaGetKeyGenerationValue(NcaContext *ctx);
NX_INLINE bool ncaCheckRightsIdAvailability(NcaContext *ctx);

NX_INLINE void ncaInitializeAesCtrIv(u8 *out, const u8 *ctr, u64 offset);
NX_INLINE void ncaUpdateAesCtrIv(u8 *ctr, u64 offset);
NX_INLINE void ncaUpdateAesCtrExIv(u8 *ctr, u32 ctr_val, u64 offset);

static bool _ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, bool lock);
static bool _ncaReadAesCtrExStorageFromBktrSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool lock);

static bool ncaGenerateHashDataPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, void *out, bool is_integrity_patch);
static void ncaWritePatchToMemoryBuffer(NcaContext *ctx, const void *patch, u64 patch_offset, u64 patch_size, void *buf, u64 buf_offset, u64 buf_size);

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

bool ncaInitializeContext(NcaContext *out, u8 storage_id, u8 hfs_partition_type, const NcmContentInfo *content_info, Ticket *tik)
{
    NcmContentStorage *ncm_storage = NULL;
    
    if (!out || (storage_id != NcmStorageId_GameCard && !(ncm_storage = titleGetNcmStorageByStorageId(storage_id))) || \
        (storage_id == NcmStorageId_GameCard && hfs_partition_type >= GameCardHashFileSystemPartitionType_Count) || !content_info || content_info->content_type > NcmContentType_DeltaFragment)
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
    
    utilsGenerateHexStringFromData(out->hash_str, sizeof(out->hash_str), out->hash, sizeof(out->hash)); /* Placeholder, needs to be manually calculated. */
    
    out->content_type = content_info->content_type;
    out->id_offset = content_info->id_offset;
    
    titleConvertNcmContentSizeToU64(content_info->size, &(out->content_size));
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
    
    /* Read decrypted NCA header and NCA FS section headers. */
    if (!ncaReadDecryptedHeader(out))
    {
        LOGFILE("Failed to read decrypted NCA \"%s\" header!", out->content_id_str);
        return false;
    }
    
    if (out->rights_id_available)
    {
        Ticket tmp_tik = {0};
        Ticket *usable_tik = (tik ? tik : &tmp_tik);
        
        /* Retrieve ticket. */
        /* This will return true if it has already been retrieved. */
        if (tikRetrieveTicketByRightsId(usable_tik, &(out->header.rights_id), out->storage_id == NcmStorageId_GameCard))
        {
            /* Copy decrypted titlekey. */
            memcpy(out->titlekey, usable_tik->dec_titlekey, 0x10);
            out->titlekey_retrieved = true;
        } else {
            LOGFILE("Error retrieving ticket for NCA \"%s\"!", out->content_id_str);
        }
    }
    
    /* Parse sections. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsInfo *fs_info = &(out->header.fs_info[i]);
        NcaFsSectionContext *fs_ctx = &(out->fs_ctx[i]);
        
        /* Fill section context. */
        fs_ctx->nca_ctx = out;
        fs_ctx->section_num = i;
        fs_ctx->section_type = NcaFsSectionType_Invalid; /* Placeholder. */
        
        /* Don't proceed if this NCA FS section isn't populated. */
        if (!ncaIsFsInfoEntryValid(fs_info)) continue;
        
        /* Calculate section offset and size. */
        fs_ctx->section_offset = NCA_FS_SECTOR_OFFSET(fs_info->start_sector);
        fs_ctx->section_size = (NCA_FS_SECTOR_OFFSET(fs_info->end_sector) - fs_ctx->section_offset);
        
        /* Check if we're dealing with an invalid offset/size. */
        if (fs_ctx->section_offset < sizeof(NcaHeader) || !fs_ctx->section_size || \
            (fs_ctx->section_offset + fs_ctx->section_size) > out->content_size) continue;
        
        /* Determine encryption type. */
        fs_ctx->encryption_type = (out->format_version == NcaVersion_Nca0 ? NcaEncryptionType_AesXts : fs_ctx->header.encryption_type);
        if (fs_ctx->encryption_type == NcaEncryptionType_Auto)
        {
            switch(fs_ctx->section_num)
            {
                case 0: /* ExeFS Partition FS. */
                case 1: /* RomFS. */
                    fs_ctx->encryption_type = NcaEncryptionType_AesCtr;
                    break;
                case 2: /* Logo Partition FS. */
                    fs_ctx->encryption_type = NcaEncryptionType_None;
                    break;
                default:
                    break;
            }
        }
        
        /* Check if we're dealing with an invalid encryption type value. */
        if (fs_ctx->encryption_type == NcaEncryptionType_Auto || fs_ctx->encryption_type > NcaEncryptionType_AesCtrEx) continue;
        
        /* Determine FS section type. */
        if (fs_ctx->header.fs_type == NcaFsType_PartitionFs && fs_ctx->header.hash_type == NcaHashType_HierarchicalSha256)
        {
            fs_ctx->section_type = NcaFsSectionType_PartitionFs;
        } else
        if (fs_ctx->header.fs_type == NcaFsType_RomFs && fs_ctx->header.hash_type == NcaHashType_HierarchicalIntegrity)
        {
            fs_ctx->section_type = (fs_ctx->encryption_type == NcaEncryptionType_AesCtrEx ? NcaFsSectionType_PatchRomFs : NcaFsSectionType_RomFs);
        } else
        if (fs_ctx->header.fs_type == NcaFsType_RomFs && fs_ctx->header.hash_type == NcaHashType_HierarchicalSha256 && out->format_version == NcaVersion_Nca0)
        {
            fs_ctx->section_type = NcaFsSectionType_Nca0RomFs;
        }
        
        /* Check if we're dealing with an invalid section type value. */
        if (fs_ctx->section_type >= NcaFsSectionType_Invalid) continue;
        
        /* Initialize crypto data. */
        if ((!out->rights_id_available || (out->rights_id_available && out->titlekey_retrieved)) && fs_ctx->encryption_type > NcaEncryptionType_None && \
            fs_ctx->encryption_type <= NcaEncryptionType_AesCtrEx)
        {
            /* Initialize section CTR. */
            ncaInitializeAesCtrIv(fs_ctx->ctr, fs_ctx->header.aes_ctr_upper_iv.value, fs_ctx->section_offset);
            
            /* Initialize AES context. */
            if (out->rights_id_available)
            {
                /* AES-128-CTR is always used for FS crypto in NCAs with a rights ID. */
                aes128CtrContextCreate(&(fs_ctx->ctr_ctx), out->titlekey, fs_ctx->ctr);
            } else {
                if (fs_ctx->encryption_type == NcaEncryptionType_AesXts)
                {
                    /* We need to create two different contexts: one for decryption and another one for encryption. */
                    aes128XtsContextCreate(&(fs_ctx->xts_decrypt_ctx), out->decrypted_key_area.aes_xts_1, out->decrypted_key_area.aes_xts_2, false);
                    aes128XtsContextCreate(&(fs_ctx->xts_encrypt_ctx), out->decrypted_key_area.aes_xts_1, out->decrypted_key_area.aes_xts_2, true);
                } else
                if (fs_ctx->encryption_type == NcaEncryptionType_AesCtr || fs_ctx->encryption_type == NcaEncryptionType_AesCtrEx)
                {
                    aes128CtrContextCreate(&(fs_ctx->ctr_ctx), out->decrypted_key_area.aes_ctr, fs_ctx->ctr);
                }
            }
        }
        
        /* Enable FS context if we got up to this point. */
        fs_ctx->enabled = true;
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
    return ncaGenerateHashDataPatch(ctx, data, data_size, data_offset, out, false);
}

void ncaWriteHierarchicalSha256PatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalSha256Patch *patch, void *buf, u64 buf_size, u64 buf_offset)
{
    if (!ctx || !strlen(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH || !patch || memcmp(patch->content_id.c, ctx->content_id.c, 0x10) != 0 || !patch->hash_region_count || \
        patch->hash_region_count > NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT || !buf || !buf_size || buf_offset >= ctx->content_size || (buf_offset + buf_size) > ctx->content_size) return;
    
    for(u32 i = 0; i < patch->hash_region_count; i++)
    {
        NcaHashDataPatch *hash_region_patch = &(patch->hash_region_patch[i]);
        ncaWritePatchToMemoryBuffer(ctx, hash_region_patch->data, hash_region_patch->offset, hash_region_patch->size, buf, buf_offset, buf_size);
    }
}

bool ncaGenerateHierarchicalIntegrityPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalIntegrityPatch *out)
{
    return ncaGenerateHashDataPatch(ctx, data, data_size, data_offset, out, true);
}

void ncaWriteHierarchicalIntegrityPatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalIntegrityPatch *patch, void *buf, u64 buf_size, u64 buf_offset)
{
    if (!ctx || !strlen(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH || !patch || memcmp(patch->content_id.c, ctx->content_id.c, 0x10) != 0 || !buf || !buf_size || \
        buf_offset >= ctx->content_size || (buf_offset + buf_size) > ctx->content_size) return;
    
    for(u32 i = 0; i < NCA_IVFC_LEVEL_COUNT; i++)
    {
        NcaHashDataPatch *hash_level_patch = &(patch->hash_level_patch[i]);
        ncaWritePatchToMemoryBuffer(ctx, hash_level_patch->data, hash_level_patch->offset, hash_level_patch->size, buf, buf_offset, buf_size);
    }
}

void ncaRemoveTitlekeyCrypto(NcaContext *ctx)
{
    if (!ctx || ctx->content_size < NCA_FULL_HEADER_LENGTH || !ctx->rights_id_available || !ctx->titlekey_retrieved) return;
    
    /* Copy decrypted titlekey to the decrypted NCA key area. */
    /* This will be reencrypted at a later stage. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        /* AES-128-XTS is not used in FS sections from NCAs with titlekey crypto. */
        NcaFsSectionContext *fs_ctx = &(ctx->fs_ctx[i]);
        if (!fs_ctx->enabled || (fs_ctx->encryption_type != NcaEncryptionType_AesCtr && fs_ctx->encryption_type != NcaEncryptionType_AesCtrEx)) continue;
        
        u8 *key_ptr = (fs_ctx->encryption_type == NcaEncryptionType_AesCtr ? ctx->decrypted_key_area.aes_ctr : ctx->decrypted_key_area.aes_ctr_ex);
        memcpy(key_ptr, ctx->titlekey, AES_128_KEY_SIZE);
    }
    
    /* Wipe Rights ID. */
    memset(&(ctx->header.rights_id), 0, sizeof(FsRightsId));
    
    /* Update context flags. */
    ctx->rights_id_available = false;
}

bool ncaEncryptHeader(NcaContext *ctx)
{
    if (!ctx || !strlen(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    /* Safety check: don't encrypt the header if we don't need to. */
    if (!ncaIsHeaderDirty(ctx)) return true;
    
    size_t crypt_res = 0;
    const u8 *header_key = keysGetNcaHeaderKey();
    Aes128XtsContext hdr_aes_ctx = {0}, nca0_fs_header_ctx = {0};
    
    /* Encrypt NCA key area. */
    if (!ctx->rights_id_available && !ncaEncryptKeyArea(ctx))
    {
        LOGFILE("Error encrypting NCA \"%s\" key area!", ctx->content_id_str);
        return false;
    }
    
    /* Prepare AES-128-XTS contexts. */
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + AES_128_KEY_SIZE, true);
    if (ctx->format_version == NcaVersion_Nca0) aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_key_area.aes_xts_1, ctx->decrypted_key_area.aes_xts_2, true);
    
    /* Encrypt NCA header. */
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->encrypted_header), &(ctx->header), sizeof(NcaHeader), 0, NCA_AES_XTS_SECTOR_SIZE, true);
    if (crypt_res != sizeof(NcaHeader))
    {
        LOGFILE("Error encrypting NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }
    
    /* Encrypt NCA FS section headers. */
    /* Both NCA2 and NCA3 place the NCA FS section headers right after the NCA header. However, NCA0 places them at the start sector from each NCA FS section. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsInfo *fs_info = &(ctx->header.fs_info[i]);
        NcaFsSectionContext *fs_ctx = &(ctx->fs_ctx[i]);
        
        /* Don't proceed if this NCA FS section isn't populated. */
        if (ctx->format_version != NcaVersion_Nca3 && !ncaIsFsInfoEntryValid(fs_info)) continue;
        
        /* The AES-XTS sector number for each NCA FS header varies depending on the NCA format version. */
        /* NCA3 uses sector number 0 for the NCA header, then increases it with each new sector (e.g. making the first NCA FS section header use sector number 2, and so on). */
        /* NCA2 uses sector number 0 for each NCA FS section header. */
        /* NCA0 uses sector number 0 for the NCA header, then uses sector number 0 for the rest of the data and increases it with each new sector. */
        Aes128XtsContext *aes_xts_ctx = (ctx->format_version != NcaVersion_Nca0 ? &hdr_aes_ctx : &nca0_fs_header_ctx);
        u64 sector = (ctx->format_version == NcaVersion_Nca3 ? (2U + i) : (ctx->format_version == NcaVersion_Nca2 ? 0 : (fs_info->start_sector - 2)));
        
        crypt_res = aes128XtsNintendoCrypt(aes_xts_ctx, &(fs_ctx->encrypted_header), &(fs_ctx->header), sizeof(NcaFsHeader), sector, NCA_AES_XTS_SECTOR_SIZE, true);
        if (crypt_res != sizeof(NcaFsHeader))
        {
            LOGFILE("Error encrypting NCA%u \"%s\" FS section header #%u!", ctx->format_version, ctx->content_id_str, i);
            return false;
        }
    }
    
    return true;
}

void ncaWriteEncryptedHeaderDataToMemoryBuffer(NcaContext *ctx, void *buf, u64 buf_size, u64 buf_offset)
{
    /* Return right away if we're dealing with invalid parameters, or if the buffer data is not part of the range covered by the encrypted header data (NCA2/3 optimization, last condition). */
    /* In order to avoid taking up too much execution time when this function is called (ideally inside a loop), we won't use ncaIsHeaderDirty() here. Let the user take care of it instead. */
    if (!ctx || !strlen(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH || !buf || !buf_size || buf_offset >= ctx->content_size || \
        (buf_offset + buf_size) > ctx->content_size || (ctx->format_version != NcaVersion_Nca0 && buf_offset >= NCA_FULL_HEADER_LENGTH)) return;
    
    /* Attempt to write the NCA header. */
    if (buf_offset < sizeof(NcaHeader)) ncaWritePatchToMemoryBuffer(ctx, &(ctx->encrypted_header), 0, sizeof(NcaHeader), buf, buf_offset, buf_size);
    
    /* Attempt to write NCA FS section headers. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsSectionContext *fs_ctx = &(ctx->fs_ctx[i]);
        if (!fs_ctx->enabled) continue;
        
        u64 fs_header_offset = (ctx->format_version != NcaVersion_Nca0 ? (sizeof(NcaHeader) + (i * sizeof(NcaFsHeader))) : fs_ctx->section_offset);
        ncaWritePatchToMemoryBuffer(ctx, &(fs_ctx->encrypted_header), fs_header_offset, sizeof(NcaFsHeader), buf, buf_offset, buf_size);
    }
}

void ncaUpdateContentIdAndHash(NcaContext *ctx, u8 hash[SHA256_HASH_SIZE])
{
    if (!ctx) return;
    
    memcpy(ctx->content_id.c, hash, sizeof(ctx->content_id.c));
    utilsGenerateHexStringFromData(ctx->content_id_str, sizeof(ctx->content_id_str), ctx->content_id.c, sizeof(ctx->content_id.c));
    
    memcpy(ctx->hash, hash, sizeof(ctx->hash));
    utilsGenerateHexStringFromData(ctx->hash_str, sizeof(ctx->hash_str), ctx->hash, sizeof(ctx->hash));
}

const char *ncaGetFsSectionTypeName(NcaFsSectionContext *ctx)
{
    NcaContext *nca_ctx = NULL;
    const char *str = "Invalid";
    if (!ctx || !ctx->enabled || !(nca_ctx = (NcaContext*)ctx->nca_ctx)) return str;
    
    switch(ctx->section_type)
    {
        case NcaFsSectionType_PartitionFs:
            str = ((nca_ctx->content_type == NcmContentType_Program && ctx->section_num == 0) ? "ExeFS" : "Partition FS");
            break;
        case NcaFsSectionType_RomFs:
            str = "RomFS";
            break;
        case NcaFsSectionType_PatchRomFs:
            str = "Patch RomFS [BKTR]";
            break;
        case NcaFsSectionType_Nca0RomFs:
            str = "NCA0 RomFS";
            break;
        default:
            break;
    }
    
    return str;
}

NX_INLINE bool ncaIsFsInfoEntryValid(NcaFsInfo *fs_info)
{
    if (!fs_info) return false;
    NcaFsInfo tmp_fs_info = {0};
    return (memcmp(&tmp_fs_info, fs_info, sizeof(NcaFsInfo)) != 0);
}

static bool ncaReadDecryptedHeader(NcaContext *ctx)
{
    if (!ctx || !strlen(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    u32 magic = 0;
    size_t crypt_res = 0;
    const u8 *header_key = keysGetNcaHeaderKey();
    Aes128XtsContext hdr_aes_ctx = {0}, nca0_fs_header_ctx = {0};
    
    /* Read NCA header. */
    if (!ncaReadContentFile(ctx, &(ctx->encrypted_header), sizeof(NcaHeader), 0))
    {
        LOGFILE("Failed to read NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }
    
    /* Prepare NCA header AES-128-XTS context. */
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + AES_128_KEY_SIZE, false);
    
    /* Decrypt NCA header. */
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->encrypted_header), sizeof(NcaHeader), 0, NCA_AES_XTS_SECTOR_SIZE, false);
    magic = __builtin_bswap32(ctx->header.magic);
    
    if (crypt_res != sizeof(NcaHeader) || (magic != NCA_NCA3_MAGIC && magic != NCA_NCA2_MAGIC && magic != NCA_NCA0_MAGIC) || ctx->header.content_size != ctx->content_size)
    {
        LOGFILE("Error decrypting NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }
    
    /* Fill additional NCA context info. */
    ctx->format_version = (magic == NCA_NCA3_MAGIC ? NcaVersion_Nca3 : (magic == NCA_NCA2_MAGIC ? NcaVersion_Nca2 : NcaVersion_Nca0));
    ctx->key_generation = ncaGetKeyGenerationValue(ctx);
    ctx->rights_id_available = ncaCheckRightsIdAvailability(ctx);
    sha256CalculateHash(ctx->header_hash, &(ctx->header), sizeof(NcaHeader));
    
    /* Decrypt NCA key area (if needed). */
    if (!ctx->rights_id_available && !ncaDecryptKeyArea(ctx))
    {
        LOGFILE("Error decrypting NCA \"%s\" key area!", ctx->content_id_str);
        return false;
    }
    
    /* Prepare NCA0 FS header AES-128-XTS context (if needed). */
    if (ctx->format_version == NcaVersion_Nca0) aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_key_area.aes_xts_1, ctx->decrypted_key_area.aes_xts_2, false);
    
    /* Read decrypted NCA FS section headers. */
    /* Both NCA2 and NCA3 place the NCA FS section headers right after the NCA header. However, NCA0 places them at the start sector from each NCA FS section. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsInfo *fs_info = &(ctx->header.fs_info[i]);
        NcaFsSectionContext *fs_ctx = &(ctx->fs_ctx[i]);
        
        /* Don't proceed if this NCA FS section isn't populated. */
        if (ctx->format_version != NcaVersion_Nca3 && !ncaIsFsInfoEntryValid(fs_info)) continue;
        
        /* Read NCA FS section header. */
        u64 fs_header_offset = (ctx->format_version != NcaVersion_Nca0 ? (sizeof(NcaHeader) + (i * sizeof(NcaFsHeader))) : NCA_FS_SECTOR_OFFSET(fs_info->start_sector));
        if (!ncaReadContentFile(ctx, &(fs_ctx->encrypted_header), sizeof(NcaFsHeader), fs_header_offset))
        {
            LOGFILE("Failed to read NCA%u \"%s\" FS section header #%u at offset 0x%lX!", ctx->format_version, ctx->content_id_str, i, fs_header_offset);
            return false;
        }
        
        /* The AES-XTS sector number for each NCA FS header varies depending on the NCA format version. */
        /* NCA3 uses sector number 0 for the NCA header, then increases it with each new sector (e.g. making the first NCA FS section header use sector number 2, and so on). */
        /* NCA2 uses sector number 0 for each NCA FS section header. */
        /* NCA0 uses sector number 0 for the NCA header, then uses sector number 0 for the rest of the data and increases it with each new sector. */
        Aes128XtsContext *aes_xts_ctx = (ctx->format_version != NcaVersion_Nca0 ? &hdr_aes_ctx : &nca0_fs_header_ctx);
        u64 sector = (ctx->format_version == NcaVersion_Nca3 ? (2U + i) : (ctx->format_version == NcaVersion_Nca2 ? 0 : (fs_info->start_sector - 2)));
        
        crypt_res = aes128XtsNintendoCrypt(aes_xts_ctx, &(fs_ctx->header), &(fs_ctx->encrypted_header), sizeof(NcaFsHeader), sector, NCA_AES_XTS_SECTOR_SIZE, false);
        if (crypt_res != sizeof(NcaFsHeader))
        {
            LOGFILE("Error decrypting NCA%u \"%s\" FS section header #%u!", ctx->format_version, ctx->content_id_str, i);
            return false;
        }
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
    u8 key_count = 0, tmp_kek[AES_128_KEY_SIZE] = {0};
    
    /* Check if we're dealing with a NCA0 with a plain text key area. */
    if (ncaIsVersion0KeyAreaEncrypted(ctx))
    {
        memcpy(&(ctx->decrypted_key_area), &(ctx->header.encrypted_key_area), NCA_USED_KEY_AREA_SIZE);
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
        rc = splCryptoGenerateAesKey(tmp_kek, (u8*)&(ctx->header.encrypted_key_area) + (i * AES_128_KEY_SIZE), (u8*)&(ctx->decrypted_key_area) + (i * AES_128_KEY_SIZE));
        if (R_FAILED(rc))
        {
            LOGFILE("splCryptoGenerateAesKey failed to decrypt NCA key area entry #%u! (0x%08X).", i, rc);
            return false;
        }
    }
    
    return true;
}

static bool ncaEncryptKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid NCA context!");
        return false;
    }
    
    u8 key_count = 0;
    const u8 *kaek = NULL;
    Aes128Context key_area_ctx = {0};
    
    /* Check if we're dealing with a NCA0 with a plaintext key area. */
    if (ncaIsVersion0KeyAreaEncrypted(ctx))
    {
        memcpy(&(ctx->header.encrypted_key_area), &(ctx->decrypted_key_area), NCA_USED_KEY_AREA_SIZE);
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
    for(u8 i = 0; i < key_count; i++) aes128EncryptBlock(&key_area_ctx, (u8*)&(ctx->header.encrypted_key_area) + (i * AES_128_KEY_SIZE), (u8*)&(ctx->decrypted_key_area) + (i * AES_128_KEY_SIZE));
    
    return true;
}

NX_INLINE bool ncaIsVersion0KeyAreaEncrypted(NcaContext *ctx)
{
    if (!ctx || ctx->format_version != NcaVersion_Nca0) return false;
    
    u8 nca0_key_area_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(nca0_key_area_hash, &(ctx->header.encrypted_key_area), NCA_USED_KEY_AREA_SIZE);
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
    
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_num >= NCA_FS_HEADER_COUNT || ctx->section_offset < sizeof(NcaHeader) || \
        ctx->section_type >= NcaFsSectionType_Invalid || ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type > NcaEncryptionType_AesCtrEx || !out || !read_size || \
        offset >= ctx->section_size || (offset + read_size) > ctx->section_size)
    {
        LOGFILE("Invalid NCA FS section header parameters!");
        goto end;
    }
    
    size_t crypt_res = 0;
    u64 sector_num = 0;
    
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + offset);
    
    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 data_start_offset = 0, chunk_size = 0, out_chunk_size = 0;
    
    if (!strlen(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        (nca_ctx->format_version != NcaVersion_Nca0 && nca_ctx->format_version != NcaVersion_Nca2 && nca_ctx->format_version != NcaVersion_Nca3) || content_offset >= nca_ctx->content_size || \
        (content_offset + read_size) > nca_ctx->content_size)
    {
        LOGFILE("Invalid NCA header parameters!");
        goto end;
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
            goto end;
        }
        
        /* Return right away if we're dealing with a plaintext FS section. */
        if (ctx->encryption_type == NcaEncryptionType_None)
        {
            ret = true;
            goto end;
        }
        
        /* Decrypt data. */
        if (ctx->encryption_type == NcaEncryptionType_AesXts)
        {
            sector_num = ((nca_ctx->format_version != NcaVersion_Nca0 ? offset : (content_offset - sizeof(NcaHeader))) / NCA_AES_XTS_SECTOR_SIZE);
            
            crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_decrypt_ctx), out, out, read_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, false);
            if (crypt_res != read_size)
            {
                LOGFILE("Failed to AES-XTS decrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, \
                        ctx->section_num);
                goto end;
            }
        } else
        if (ctx->encryption_type == NcaEncryptionType_AesCtr || ctx->encryption_type == NcaEncryptionType_AesCtrEx)
        {
            ncaUpdateAesCtrIv(ctx->ctr, content_offset);
            aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
            aes128CtrCrypt(&(ctx->ctr_ctx), out, out, read_size);
        }
        
        ret = true;
        goto end;
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
        LOGFILE("Failed to read 0x%lX bytes encrypted data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, \
                ctx->section_num);
        goto end;
    }
    
    /* Decrypt data. */
    if (ctx->encryption_type == NcaEncryptionType_AesXts)
    {
        sector_num = ((nca_ctx->format_version != NcaVersion_Nca0 ? offset : (content_offset - sizeof(NcaHeader))) / NCA_AES_XTS_SECTOR_SIZE);
        
        crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_decrypt_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, false);
        if (crypt_res != chunk_size)
        {
            LOGFILE("Failed to AES-XTS decrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, \
                    ctx->section_num);
            goto end;
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
    
end:
    if (lock) mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return ret;
}

static bool _ncaReadAesCtrExStorageFromBktrSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool lock)
{
    if (lock) mutexLock(&g_ncaCryptoBufferMutex);
    
    bool ret = false;
    
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_num >= NCA_FS_HEADER_COUNT || ctx->section_offset < sizeof(NcaHeader) || \
        ctx->section_type != NcaFsSectionType_PatchRomFs || ctx->encryption_type != NcaEncryptionType_AesCtrEx || !out || !read_size || offset >= ctx->section_size || \
        (offset + read_size) > ctx->section_size)
    {
        LOGFILE("Invalid NCA FS section header parameters!");
        goto end;
    }
    
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + offset);
    
    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 data_start_offset = 0, chunk_size = 0, out_chunk_size = 0;
    
    if (!strlen(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        content_offset >= nca_ctx->content_size || (content_offset + read_size) > nca_ctx->content_size)
    {
        LOGFILE("Invalid NCA header parameters!");
        goto end;
    }
    
    /* Optimization for reads that are aligned to the AES-CTR sector size. */
    if (!(content_offset % AES_BLOCK_SIZE) && !(read_size % AES_BLOCK_SIZE))
    {
        /* Read data. */
        if (!ncaReadContentFile(nca_ctx, out, read_size, content_offset))
        {
            LOGFILE("Failed to read 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
            goto end;
        }
        
        /* Decrypt data */
        ncaUpdateAesCtrExIv(ctx->ctr, ctr_val, content_offset);
        aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
        aes128CtrCrypt(&(ctx->ctr_ctx), out, out, read_size);
        
        ret = true;
        goto end;
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
        LOGFILE("Failed to read 0x%lX bytes encrypted data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, \
                ctx->section_num);
        goto end;
    }
    
    /* Decrypt data. */
    ncaUpdateAesCtrExIv(ctx->ctr, ctr_val, block_start_offset);
    aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
    aes128CtrCrypt(&(ctx->ctr_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size);
    
    /* Copy decrypted data. */
    memcpy(out, g_ncaCryptoBuffer + data_start_offset, out_chunk_size);
    
    ret = (block_size > NCA_CRYPTO_BUFFER_SIZE ? _ncaReadAesCtrExStorageFromBktrSection(ctx, (u8*)out + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size, ctr_val, false) : true);
    
end:
    if (lock) mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return ret;
}

/* In this function, the term "layer" is used as a generic way to refer to both HierarchicalSha256 hash regions and HierarchicalIntegrity verification levels. */
static bool ncaGenerateHashDataPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, void *out, bool is_integrity_patch)
{
    mutexLock(&g_ncaCryptoBufferMutex);
    
    NcaContext *nca_ctx = NULL;
    NcaHierarchicalSha256Patch *hierarchical_sha256_patch = (!is_integrity_patch ? ((NcaHierarchicalSha256Patch*)out) : NULL);
    NcaHierarchicalIntegrityPatch *hierarchical_integrity_patch = (is_integrity_patch ? ((NcaHierarchicalIntegrityPatch*)out) : NULL);
    
    u8 *cur_data = NULL;
    u64 cur_data_offset = data_offset;
    u64 cur_data_size = data_size;
    
    u32 layer_count = 0;
    u8 *parent_layer_block = NULL, *cur_layer_block = NULL;
    u64 last_layer_size = 0;
    
    bool success = false;
    
    if (!ctx || !ctx->enabled || !(nca_ctx = (NcaContext*)ctx->nca_ctx) || (!is_integrity_patch && (ctx->header.hash_type != NcaHashType_HierarchicalSha256 || \
        !ctx->header.hash_data.hierarchical_sha256_data.hash_block_size || !(layer_count = ctx->header.hash_data.hierarchical_sha256_data.hash_region_count) || \
        layer_count > NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT || !(last_layer_size = ctx->header.hash_data.hierarchical_sha256_data.hash_region[layer_count - 1].size))) || \
        (is_integrity_patch && (ctx->header.hash_type != NcaHashType_HierarchicalIntegrity || \
        !(layer_count = (ctx->header.hash_data.integrity_meta_info.info_level_hash.max_level_count - 1)) || layer_count != NCA_IVFC_LEVEL_COUNT || \
        !(last_layer_size = ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[NCA_IVFC_LEVEL_COUNT - 1].size))) || !data || !data_size || \
        data_offset >= last_layer_size || (data_offset + data_size) > last_layer_size || !out)
    {
        LOGFILE("Invalid parameters!");
        goto end;
    }
    
    /* Clear output patch. */
    if (!is_integrity_patch)
    {
        ncaFreeHierarchicalSha256Patch(hierarchical_sha256_patch);
    } else {
        ncaFreeHierarchicalIntegrityPatch(hierarchical_integrity_patch);
    }
    
    /* Process each layer. */
    for(u32 i = layer_count; i > 0; i--)
    {
        u64 hash_block_size = 0;
        
        u64 cur_layer_offset = 0, cur_layer_size = 0;
        u64 cur_layer_read_start_offset = 0, cur_layer_read_end_offset = 0, cur_layer_read_size = 0, cur_layer_read_patch_offset = 0;
        
        u64 parent_layer_offset = 0, parent_layer_size = 0;
        u64 parent_layer_read_start_offset = 0, parent_layer_read_end_offset = 0, parent_layer_read_size = 0;
        
        NcaHashDataPatch *cur_layer_patch = NULL;
        
        /* Retrieve current layer properties. */
        hash_block_size = (!is_integrity_patch ? ctx->header.hash_data.hierarchical_sha256_data.hash_block_size : \
                           NCA_IVFC_BLOCK_SIZE(ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[i - 1].block_order));
        
        cur_layer_offset = (!is_integrity_patch ? ctx->header.hash_data.hierarchical_sha256_data.hash_region[i - 1].offset : \
                            ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[i - 1].offset);
        
        cur_layer_size = (!is_integrity_patch ? ctx->header.hash_data.hierarchical_sha256_data.hash_region[i - 1].size : \
                          ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[i - 1].size);
        
        /* Retrieve parent layer properties. */
        /* If this is the master layer, then no properties are retrieved, since it is verified by the master hash from the HashData block in the NCA FS section header. */
        if (i > 1)
        {
            parent_layer_offset = (!is_integrity_patch ? ctx->header.hash_data.hierarchical_sha256_data.hash_region[i - 2].offset : \
                                   ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[i - 2].offset);
            
            parent_layer_size = (!is_integrity_patch ? ctx->header.hash_data.hierarchical_sha256_data.hash_region[i - 2].size : \
                                 ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[i - 2].size);
        }
        
        /* Validate layer properties. */
        if (hash_block_size <= 1 || cur_layer_offset >= ctx->section_size || !cur_layer_size || (cur_layer_offset + cur_layer_size) > ctx->section_size || \
            (i > 1 && (parent_layer_offset >= ctx->section_size || !parent_layer_size || (parent_layer_offset + parent_layer_size) > ctx->section_size)))
        {
            LOGFILE("Invalid hierarchical parent/child layer!");
            goto end;
        }
        
        /* Retrieve pointer to the current layer patch. */
        cur_layer_patch = (!is_integrity_patch ? &(hierarchical_sha256_patch->hash_region_patch[i - 1]) : &(hierarchical_integrity_patch->hash_level_patch[i - 1]));
        
        /* Calculate required offsets and sizes. */
        if (i > 1)
        {
            /* HierarchicalSha256 hash region with index 1 through 4, or HierarchicalIntegrity verification level with index 1 through 5. */
            parent_layer_read_start_offset = ((cur_data_offset / hash_block_size) * SHA256_HASH_SIZE);
            parent_layer_read_end_offset = (((cur_data_offset + cur_data_size) / hash_block_size) * SHA256_HASH_SIZE);
            parent_layer_read_size = (parent_layer_read_end_offset != parent_layer_read_start_offset ? (parent_layer_read_end_offset - parent_layer_read_start_offset) : SHA256_HASH_SIZE);
            
            cur_layer_read_start_offset = (cur_layer_offset + ALIGN_DOWN(cur_data_offset, hash_block_size));
            cur_layer_read_end_offset = (cur_layer_offset + ALIGN_UP(cur_data_offset + cur_data_size, hash_block_size));
            cur_layer_read_size = (cur_layer_read_end_offset - cur_layer_read_start_offset);
        } else {
            /* HierarchicalSha256 master hash region, or HierarchicalIntegrity master verification level. Both with index 0. */
            /* The master hash is calculated over the whole layer and saved to the HashData block from the NCA FS section header. */
            cur_layer_read_start_offset = cur_layer_offset;
            cur_layer_read_end_offset = (cur_layer_offset + cur_layer_size);
            cur_layer_read_size = cur_layer_size;
        }
        
        cur_layer_read_patch_offset = (cur_data_offset - ALIGN_DOWN(cur_data_offset, hash_block_size));
        
        /* Allocate memory for our current layer block. */
        cur_layer_block = calloc(cur_layer_read_size, sizeof(u8));
        if (!cur_layer_block)
        {
            LOGFILE("Unable to allocate 0x%lX bytes for hierarchical layer #%u data block! (current).", cur_layer_read_size, i - 1);
            goto end;
        }
        
        /* Adjust current layer read size to avoid read errors (if needed). */
        if (cur_layer_read_end_offset > (cur_layer_offset + cur_layer_size))
        {
            cur_layer_read_end_offset = (cur_layer_offset + cur_layer_size);
            cur_layer_read_size = (cur_layer_read_end_offset - cur_layer_read_start_offset);
        }
        
        /* Read current layer block. */
        if (!_ncaReadFsSection(ctx, cur_layer_block, cur_layer_read_size, cur_layer_read_start_offset, false))
        {
            LOGFILE("Failed to read 0x%lX bytes long hierarchical layer #%u data block from offset 0x%lX! (current).", cur_layer_read_size, i - 1, cur_layer_read_start_offset);
            goto end;
        }
        
        /* Replace current layer block data. */
        memcpy(cur_layer_block + cur_layer_read_patch_offset, (i == layer_count ? data : cur_data), cur_data_size);
        
        /* Recalculate hashes. */
        if (i > 1)
        {
            /* Allocate memory for our parent layer block. */
            parent_layer_block = calloc(parent_layer_read_size, sizeof(u8));
            if (!parent_layer_block)
            {
                LOGFILE("Unable to allocate 0x%lX bytes for hierarchical layer #%u data block! (parent).", parent_layer_read_size, i - 2);
                goto end;
            }
            
            /* Read parent layer block. */
            if (!_ncaReadFsSection(ctx, parent_layer_block, parent_layer_read_size, parent_layer_offset + parent_layer_read_start_offset, false))
            {
                LOGFILE("Failed to read 0x%lX bytes long hierarchical layer #%u data block from offset 0x%lX! (parent).", parent_layer_read_size, i - 2, parent_layer_read_start_offset);
                goto end;
            }
            
            /* HierarchicalSha256: size is truncated for blocks smaller than the hash block size. */
            /* HierarchicalIntegrity: size *isn't* truncated for blocks smaller than the hash block size, so we just keep using the same hash block size throughout the loop. */
            /*                        For these specific cases, the rest of the block should be filled with zeroes (already taken care of by using calloc()). */
            for(u64 j = 0, k = 0; j < cur_layer_read_size; j += hash_block_size, k++)
            {
                if (!is_integrity_patch && hash_block_size > (cur_layer_read_size - j)) hash_block_size = (cur_layer_read_size - j);
                sha256CalculateHash(parent_layer_block + (k * SHA256_HASH_SIZE), cur_layer_block + j, hash_block_size);
            }
        } else {
            /* Recalculate master hash from the HashData area. */
            u8 *master_hash = (!is_integrity_patch ? ctx->header.hash_data.hierarchical_sha256_data.master_hash : ctx->header.hash_data.integrity_meta_info.master_hash);
            sha256CalculateHash(master_hash, cur_layer_block, cur_layer_read_size);
        }
        
        /* Reencrypt current layer block. */
        cur_layer_patch->data = _ncaGenerateEncryptedFsSectionBlock(ctx, cur_layer_block + cur_layer_read_patch_offset, cur_data_size, cur_layer_offset + cur_data_offset, \
                                                                    &(cur_layer_patch->size), &(cur_layer_patch->offset), false);
        if (!cur_layer_patch->data)
        {
            LOGFILE("Failed to generate encrypted 0x%lX bytes long hierarchical layer #%u data block!", cur_data_size, i - 1);
            goto end;
        }
        
        /* Free current layer block. */
        free(cur_layer_block);
        cur_layer_block = NULL;
        
        if (i > 1)
        {
            /* Free previous layer block (if needed). */
            if (cur_data) free(cur_data);
            
            /* Prepare data for the next layer. */
            cur_data = parent_layer_block;
            cur_data_offset = parent_layer_read_start_offset;
            cur_data_size = parent_layer_read_size;
            parent_layer_block = NULL;
        }
    }
    
    /* Recalculate FS header hash. */
    sha256CalculateHash(nca_ctx->header.fs_header_hash[ctx->section_num].hash, &(ctx->header), sizeof(NcaFsHeader));
    
    /* Copy content ID. */
    memcpy(!is_integrity_patch ? &(hierarchical_sha256_patch->content_id) : &(hierarchical_integrity_patch->content_id), &(nca_ctx->content_id), sizeof(NcmContentId));
    
    /* Set hash region count (if needed). */
    if (!is_integrity_patch) hierarchical_sha256_patch->hash_region_count = layer_count;
    
    success = true;
    
end:
    if (cur_layer_block) free(cur_layer_block);
    
    if (parent_layer_block) free(parent_layer_block);
    
    if (!success && out)
    {
        if (!is_integrity_patch)
        {
            ncaFreeHierarchicalSha256Patch(hierarchical_sha256_patch);
        } else {
            ncaFreeHierarchicalIntegrityPatch(hierarchical_integrity_patch);
        }
    }
    
    mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return success;
}

static void ncaWritePatchToMemoryBuffer(NcaContext *ctx, const void *patch, u64 patch_offset, u64 patch_size, void *buf, u64 buf_offset, u64 buf_size)
{
    /* Return right away if we're dealing with invalid parameters, or if the buffer data is not part of the range covered by the patch (last two conditions). */
    if (!ctx || !patch || patch_offset >= ctx->content_size || !patch_size || (patch_offset + patch_size) > ctx->content_size || (buf_offset + buf_size) <= patch_offset || \
        (patch_offset + patch_size) <= buf_offset) return;
    
    /* Overwrite buffer data using patch data. */
    u64 patch_block_offset = (patch_offset < buf_offset ? (buf_offset - patch_offset) : 0);
    u64 patch_remaining_size = (patch_size - patch_block_offset);
    
    u64 buf_block_offset = (buf_offset < patch_offset ? (patch_offset - buf_offset) : 0);
    u64 buf_remaining_size = (buf_size - buf_block_offset);
    
    u64 buf_block_size = (buf_remaining_size < patch_remaining_size ? buf_remaining_size : patch_remaining_size);
    
    memcpy((u8*)buf + buf_block_offset, (const u8*)patch + patch_block_offset, buf_block_size);
    
    LOGFILE("Overwrote 0x%lX bytes block at offset 0x%lX from raw NCA \"%s\" buffer (size 0x%lX, NCA offset 0x%lX).", buf_block_size, buf_block_offset, ctx->content_id_str, buf_size, buf_offset);
}

static void *_ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset, bool lock)
{
    if (lock) mutexLock(&g_ncaCryptoBufferMutex);
    
    u8 *out = NULL;
    bool success = false;
    
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_num >= NCA_FS_HEADER_COUNT || ctx->section_offset < sizeof(NcaHeader) || \
        ctx->section_type >= NcaFsSectionType_Invalid || ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type >= NcaEncryptionType_AesCtrEx || !data || !data_size || \
        data_offset >= ctx->section_size || (data_offset + data_size) > ctx->section_size || !out_block_size || !out_block_offset)
    {
        LOGFILE("Invalid NCA FS section header parameters!");
        goto end;
    }
    
    size_t crypt_res = 0;
    u64 sector_num = 0;
    
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + data_offset);
    
    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 plain_chunk_offset = 0;
    
    if (!strlen(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        (nca_ctx->format_version != NcaVersion_Nca0 && nca_ctx->format_version != NcaVersion_Nca2 && nca_ctx->format_version != NcaVersion_Nca3) || content_offset >= nca_ctx->content_size || \
        (content_offset + data_size) > nca_ctx->content_size)
    {
        LOGFILE("Invalid NCA header parameters!");
        goto end;
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
            goto end;
        }
        
        /* Copy data. */
        memcpy(out, data, data_size);
        
        /* Encrypt data. */
        if (ctx->encryption_type == NcaEncryptionType_AesXts)
        {
            sector_num = ((nca_ctx->format_version != NcaVersion_Nca0 ? data_offset : (content_offset - sizeof(NcaHeader))) / NCA_AES_XTS_SECTOR_SIZE);
            
            crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_encrypt_ctx), out, out, data_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, true);
            if (crypt_res != data_size)
            {
                LOGFILE("Failed to AES-XTS encrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", data_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
                goto end;
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
        goto end;
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
        goto end;
    }
    
    /* Read decrypted data using aligned offset and size. */
    if (!_ncaReadFsSection(ctx, out, block_size, block_start_offset, false))
    {
        LOGFILE("Failed to read decrypted NCA \"%s\" FS section #%u data block!", nca_ctx->content_id_str, ctx->section_num);
        goto end;
    }
    
    /* Replace plaintext data. */
    memcpy(out + plain_chunk_offset, data, data_size);
    
    /* Reencrypt data. */
    if (ctx->encryption_type == NcaEncryptionType_AesXts)
    {
        sector_num = ((nca_ctx->format_version != NcaVersion_Nca0 ? block_start_offset : (content_offset - sizeof(NcaHeader))) / NCA_AES_XTS_SECTOR_SIZE);
        
        crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_encrypt_ctx), out, out, block_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, true);
        if (crypt_res != block_size)
        {
            LOGFILE("Failed to AES-XTS encrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", block_size, content_offset, nca_ctx->content_id_str, ctx->section_num);
            goto end;
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
    
end:
    if (!success && out)
    {
        free(out);
        out = NULL;
    }
    
    if (lock) mutexUnlock(&g_ncaCryptoBufferMutex);
    
    return out;
}
