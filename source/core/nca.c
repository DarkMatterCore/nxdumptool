/*
 * nca.c
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "nxdt_utils.h"
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

/// Used to verify if the key area from a NCA0 is encrypted.
static const u8 g_nca0KeyAreaHash[SHA256_HASH_SIZE] = {
    0x9A, 0xBB, 0xD2, 0x11, 0x86, 0x00, 0x21, 0x9D, 0x7A, 0xDC, 0x5B, 0x43, 0x95, 0xF8, 0x4E, 0xFD,
    0xFF, 0x6B, 0x25, 0xEF, 0x9F, 0x96, 0x85, 0x28, 0x18, 0x9E, 0x76, 0xB0, 0x92, 0xF0, 0x6A, 0xCB
};

/// Used to verify the NCA header main signature.
static const u8 g_ncaHeaderMainSignaturePublicExponent[3] = { 0x01, 0x00, 0x01 };

/* Function prototypes. */

NX_INLINE bool ncaIsFsInfoEntryValid(NcaFsInfo *fs_info);

static bool ncaReadDecryptedHeader(NcaContext *ctx);
static bool ncaDecryptKeyArea(NcaContext *ctx);
static bool ncaEncryptKeyArea(NcaContext *ctx);

static bool ncaVerifyMainSignature(NcaContext *ctx);

NX_INLINE bool ncaIsVersion0KeyAreaEncrypted(NcaContext *ctx);
NX_INLINE u8 ncaGetKeyGenerationValue(NcaContext *ctx);
NX_INLINE bool ncaCheckRightsIdAvailability(NcaContext *ctx);

static bool ncaInitializeFsSectionContext(NcaContext *nca_ctx, u32 section_idx);
static bool ncaFsSectionValidateHashDataBoundaries(NcaFsSectionContext *ctx);

static bool _ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset);
static bool ncaFsSectionCheckHashRegionAccess(NcaFsSectionContext *ctx, u64 offset, u64 size, u64 *out_chunk_size);

static bool _ncaReadAesCtrExStorage(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool decrypt);

static void ncaCalculateLayerHash(void *dst, const void *src, size_t size, bool use_sha3);
static bool ncaGenerateHashDataPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, void *out, bool is_integrity_patch);
static bool ncaWritePatchToMemoryBuffer(NcaContext *ctx, const void *patch, u64 patch_size, u64 patch_offset, void *buf, u64 buf_size, u64 buf_offset);

static void *ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset);

bool ncaAllocateCryptoBuffer(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_ncaCryptoBufferMutex)
    {
        if (!g_ncaCryptoBuffer) g_ncaCryptoBuffer = malloc(NCA_CRYPTO_BUFFER_SIZE);
        ret = (g_ncaCryptoBuffer != NULL);
    }

    return ret;
}

void ncaFreeCryptoBuffer(void)
{
    SCOPED_LOCK(&g_ncaCryptoBufferMutex)
    {
        if (!g_ncaCryptoBuffer) break;
        free(g_ncaCryptoBuffer);
        g_ncaCryptoBuffer = NULL;
    }
}

bool ncaInitializeContext(NcaContext *out, u8 storage_id, u8 hfs_partition_type, const NcmContentInfo *content_info, u32 title_version, Ticket *tik)
{
    NcmContentStorage *ncm_storage = NULL;
    u8 valid_fs_section_cnt = 0;

    if (!out || (storage_id != NcmStorageId_GameCard && !(ncm_storage = titleGetNcmStorageByStorageId(storage_id))) || \
        (storage_id == NcmStorageId_GameCard && (!hfs_partition_type || hfs_partition_type >= GameCardHashFileSystemPartitionType_Count)) || !content_info || \
        content_info->content_type >= NcmContentType_DeltaFragment)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Clear output NCA context. */
    memset(out, 0, sizeof(NcaContext));

    /* Fill NCA context. */
    out->storage_id = storage_id;
    out->ncm_storage = (out->storage_id != NcmStorageId_GameCard ? ncm_storage : NULL);

    memcpy(&(out->content_id), &(content_info->content_id), sizeof(NcmContentId));
    utilsGenerateHexStringFromData(out->content_id_str, sizeof(out->content_id_str), out->content_id.c, sizeof(out->content_id.c), false);

    utilsGenerateHexStringFromData(out->hash_str, sizeof(out->hash_str), out->hash, sizeof(out->hash), false);  /* Placeholder, needs to be manually calculated. */

    out->content_type = content_info->content_type;
    out->id_offset = content_info->id_offset;
    out->title_version = title_version;

    titleConvertNcmContentSizeToU64(content_info->size, &(out->content_size));
    if (out->content_size < NCA_FULL_HEADER_LENGTH)
    {
        LOG_MSG_ERROR("Invalid size for NCA \"%s\"!", out->content_id_str);
        return false;
    }

    if (out->storage_id == NcmStorageId_GameCard)
    {
        /* Generate gamecard NCA filename. */
        char nca_filename[0x30] = {0};
        sprintf(nca_filename, "%s.%s", out->content_id_str, out->content_type == NcmContentType_Meta ? "cnmt.nca" : "nca");

        /* Retrieve gamecard NCA offset. */
        if (!gamecardGetHashFileSystemEntryInfoByName(hfs_partition_type, nca_filename, &(out->gamecard_offset), NULL))
        {
            LOG_MSG_ERROR("Error retrieving offset for \"%s\" entry in secure hash FS partition!", nca_filename);
            return false;
        }
    }

    /* Read decrypted NCA header and NCA FS section headers. */
    if (!ncaReadDecryptedHeader(out))
    {
        LOG_MSG_ERROR("Failed to read decrypted NCA \"%s\" header!", out->content_id_str);
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
            LOG_MSG_ERROR("Error retrieving ticket for NCA \"%s\"!", out->content_id_str);
        }
    }

    /* Parse NCA FS sections. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        /* Increase valid NCA FS section count if the FS section is valid. */
        if (ncaInitializeFsSectionContext(out, i)) valid_fs_section_cnt++;
    }

    if (!valid_fs_section_cnt) LOG_MSG_ERROR("Unable to identify any valid FS sections in NCA \"%s\"!", out->content_id_str);

    return (valid_fs_section_cnt > 0);
}

bool ncaReadContentFile(NcaContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !*(ctx->content_id_str) || (ctx->storage_id != NcmStorageId_GameCard && !ctx->ncm_storage) || (ctx->storage_id == NcmStorageId_GameCard && !ctx->gamecard_offset) || !out || \
        !read_size || (offset + read_size) > ctx->content_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
        if (!ret) LOG_MSG_ERROR("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (0x%X) (ncm).", read_size, offset, ctx->content_id_str, rc);
    } else {
        /* Retrieve NCA data using raw gamecard reads. */
        /* Fixes NCA read issues with gamecards under HOS < 4.0.0 when using ncmContentStorageReadContentIdFile(). */
        ret = gamecardReadStorage(out, read_size, ctx->gamecard_offset + offset);
        if (!ret) LOG_MSG_ERROR("Failed to read 0x%lX bytes block at offset 0x%lX from NCA \"%s\"! (gamecard).", read_size, offset, ctx->content_id_str);
    }

    return ret;
}

bool ncaGetFsSectionHashTargetExtents(NcaFsSectionContext *ctx, u64 *out_offset, u64 *out_size)
{
    if (!ctx || (!out_offset && !out_size))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = true;

    switch(ctx->hash_type)
    {
        case NcaHashType_None:
            if (out_offset) *out_offset = 0;
            if (out_size) *out_size = ctx->section_size;
            break;
        case NcaHashType_HierarchicalSha256:
        case NcaHashType_HierarchicalSha3256:
            {
                u32 layer_count = ctx->header.hash_data.hierarchical_sha256_data.hash_region_count;
                NcaRegion *hash_region = &(ctx->header.hash_data.hierarchical_sha256_data.hash_region[layer_count - 1]);
                if (out_offset) *out_offset = hash_region->offset;
                if (out_size) *out_size = hash_region->size;
            }
            break;
        case NcaHashType_HierarchicalIntegrity:
        case NcaHashType_HierarchicalIntegritySha3:
            {
                NcaHierarchicalIntegrityVerificationLevelInformation *lvl_info = &(ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[NCA_IVFC_LEVEL_COUNT - 1]);
                if (out_offset) *out_offset = lvl_info->offset;
                if (out_size) *out_size = lvl_info->size;
            }
            break;
        default:
            success = false;
            break;
    }

    return success;
}

bool ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset)
{
    bool ret = false;
    SCOPED_LOCK(&g_ncaCryptoBufferMutex) ret = _ncaReadFsSection(ctx, out, read_size, offset);
    return ret;
}

bool ncaReadAesCtrExStorage(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool decrypt)
{
    bool ret = false;
    SCOPED_LOCK(&g_ncaCryptoBufferMutex) ret = _ncaReadAesCtrExStorage(ctx, out, read_size, offset, ctr_val, decrypt);
    return ret;
}

bool ncaGenerateHierarchicalSha256Patch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out)
{
    bool ret = false;
    SCOPED_LOCK(&g_ncaCryptoBufferMutex) ret = ncaGenerateHashDataPatch(ctx, data, data_size, data_offset, out, false);
    return ret;
}

void ncaWriteHierarchicalSha256PatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalSha256Patch *patch, void *buf, u64 buf_size, u64 buf_offset)
{
    if (!ctx || !*(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH || !patch || patch->written || memcmp(patch->content_id.c, ctx->content_id.c, 0x10) != 0 || \
        !patch->hash_region_count || patch->hash_region_count > NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT || !buf || !buf_size || (buf_offset + buf_size) > ctx->content_size) return;

    patch->written = true;

    for(u32 i = 0; i < patch->hash_region_count; i++)
    {
        NcaHashDataPatch *hash_region_patch = &(patch->hash_region_patch[i]);
        if (hash_region_patch->written) continue;

        hash_region_patch->written = ncaWritePatchToMemoryBuffer(ctx, hash_region_patch->data, hash_region_patch->size, hash_region_patch->offset, buf, buf_size, buf_offset);
        if (!hash_region_patch->written) patch->written = false;
    }
}

bool ncaGenerateHierarchicalIntegrityPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalIntegrityPatch *out)
{
    bool ret = false;
    SCOPED_LOCK(&g_ncaCryptoBufferMutex) ret = ncaGenerateHashDataPatch(ctx, data, data_size, data_offset, out, true);
    return ret;
}

void ncaWriteHierarchicalIntegrityPatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalIntegrityPatch *patch, void *buf, u64 buf_size, u64 buf_offset)
{
    if (!ctx || !*(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH || !patch || patch->written || memcmp(patch->content_id.c, ctx->content_id.c, 0x10) != 0 || \
        !buf || !buf_size || (buf_offset + buf_size) > ctx->content_size) return;

    patch->written = true;

    for(u32 i = 0; i < NCA_IVFC_LEVEL_COUNT; i++)
    {
        NcaHashDataPatch *hash_level_patch = &(patch->hash_level_patch[i]);
        if (hash_level_patch->written) continue;

        hash_level_patch->written = ncaWritePatchToMemoryBuffer(ctx, hash_level_patch->data, hash_level_patch->size, hash_level_patch->offset, buf, buf_size, buf_offset);
        if (!hash_level_patch->written) patch->written = false;
    }
}

void ncaSetDownloadDistributionType(NcaContext *ctx)
{
    if (!ctx || ctx->content_size < NCA_FULL_HEADER_LENGTH || !*(ctx->content_id_str) || ctx->content_type > NcmContentType_DeltaFragment || \
        ctx->header.distribution_type == NcaDistributionType_Download) return;
    ctx->header.distribution_type = NcaDistributionType_Download;
    LOG_MSG_INFO("Set download distribution type to %s NCA \"%s\".", titleGetNcmContentTypeName(ctx->content_type), ctx->content_id_str);
}

bool ncaRemoveTitleKeyCrypto(NcaContext *ctx)
{
    if (!ctx || ctx->content_size < NCA_FULL_HEADER_LENGTH || !*(ctx->content_id_str) || ctx->content_type > NcmContentType_DeltaFragment)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Don't proceed if we're not dealing with a NCA with a populated rights ID field, or if we couldn't retrieve the titlekey for it. */
    if (!ctx->rights_id_available || !ctx->titlekey_retrieved) return true;

    /* Copy decrypted titlekey to the decrypted NCA key area. This will be reencrypted at a later stage. */
    /* AES-128-XTS is not used in FS sections from NCAs with titlekey crypto. */
    /* Patch RomFS sections also use the AES-128-CTR key from the decrypted NCA key area, for some reason. */
    memcpy(ctx->decrypted_key_area.aes_ctr, ctx->titlekey, AES_128_KEY_SIZE);

    /* Encrypt NCA key area. */
    if (!ncaEncryptKeyArea(ctx))
    {
        LOG_MSG_ERROR("Error encrypting %s NCA \"%s\" key area!", titleGetNcmContentTypeName(ctx->content_type), ctx->content_id_str);
        return false;
    }

    /* Wipe Rights ID. */
    memset(&(ctx->header.rights_id), 0, sizeof(FsRightsId));

    /* Update context flags. */
    ctx->rights_id_available = false;

    LOG_MSG_INFO("Removed titlekey crypto from %s NCA \"%s\".", titleGetNcmContentTypeName(ctx->content_type), ctx->content_id_str);

    return true;
}

bool ncaEncryptHeader(NcaContext *ctx)
{
    if (!ctx || !*(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH)
    {
        LOG_MSG_ERROR("Invalid NCA context!");
        return false;
    }

    /* Safety check: don't encrypt the header if we don't need to. */
    if (!ncaIsHeaderDirty(ctx)) return true;

    size_t crypt_res = 0;
    const u8 *header_key = keysGetNcaHeaderKey();
    Aes128XtsContext hdr_aes_ctx = {0}, nca0_fs_header_ctx = {0};

    if (!header_key)
    {
        LOG_MSG_ERROR("Failed to retrieve NCA header key!");
        return false;
    }

    /* Prepare AES-128-XTS contexts. */
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + AES_128_KEY_SIZE, true);
    if (ctx->format_version == NcaVersion_Nca0) aes128XtsContextCreate(&nca0_fs_header_ctx, ctx->decrypted_key_area.aes_xts_1, ctx->decrypted_key_area.aes_xts_2, true);

    /* Encrypt NCA header. */
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->encrypted_header), &(ctx->header), sizeof(NcaHeader), 0, NCA_AES_XTS_SECTOR_SIZE, true);
    if (crypt_res != sizeof(NcaHeader))
    {
        LOG_MSG_ERROR("Error encrypting NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }

    /* Encrypt NCA FS section headers. */
    /* Both NCA2 and NCA3 place the NCA FS section headers right after the NCA header. However, NCA0 places them at the start sector from each NCA FS section. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsInfo *fs_info = &(ctx->header.fs_info[i]);
        NcaFsSectionContext *fs_ctx = &(ctx->fs_ctx[i]);

        /* Don't proceed if this NCA FS section isn't populated. */
        if (!ncaIsFsInfoEntryValid(fs_info)) continue;

        /* The AES-XTS sector number for each NCA FS header varies depending on the NCA format version. */
        /* NCA3 uses sector number 0 for the NCA header, then increases it with each new sector (e.g. making the first NCA FS section header use sector number 2, and so on). */
        /* NCA2 uses sector number 0 for each NCA FS section header. */
        /* NCA0 uses sector number 0 for the NCA header, then uses sector number 0 for the rest of the data and increases it with each new sector. */
        Aes128XtsContext *aes_xts_ctx = (ctx->format_version != NcaVersion_Nca0 ? &hdr_aes_ctx : &nca0_fs_header_ctx);
        u64 sector = (ctx->format_version == NcaVersion_Nca3 ? (2U + i) : (ctx->format_version == NcaVersion_Nca2 ? 0 : (fs_info->start_sector - 2)));

        crypt_res = aes128XtsNintendoCrypt(aes_xts_ctx, &(fs_ctx->encrypted_header), &(fs_ctx->header), sizeof(NcaFsHeader), sector, NCA_AES_XTS_SECTOR_SIZE, true);
        if (crypt_res != sizeof(NcaFsHeader))
        {
            LOG_MSG_ERROR("Error encrypting NCA%u \"%s\" FS section header #%u!", ctx->format_version, ctx->content_id_str, i);
            return false;
        }
    }

    return true;
}

void ncaWriteEncryptedHeaderDataToMemoryBuffer(NcaContext *ctx, void *buf, u64 buf_size, u64 buf_offset)
{
    /* Return right away if we're dealing with invalid parameters. */
    /* In order to avoid taking up too much execution time when this function is called (ideally inside a loop), we won't use ncaIsHeaderDirty() here. Let the user take care of it instead. */
    if (!ctx || ctx->header_written || ctx->content_size < NCA_FULL_HEADER_LENGTH || !buf || !buf_size || (buf_offset + buf_size) > ctx->content_size) return;

    ctx->header_written = true;

    /* Attempt to write the NCA header. */
    /* Return right away if the NCA header was only partially written. */
    if (buf_offset < sizeof(NcaHeader) && !ncaWritePatchToMemoryBuffer(ctx, &(ctx->encrypted_header), sizeof(NcaHeader), 0, buf, buf_size, buf_offset))
    {
        ctx->header_written = false;
        return;
    }

    /* Attempt to write NCA FS section headers. */
    for(u8 i = 0; i < NCA_FS_HEADER_COUNT; i++)
    {
        NcaFsSectionContext *fs_ctx = &(ctx->fs_ctx[i]);
        if (!fs_ctx->enabled || fs_ctx->header_written) continue;

        u64 fs_header_offset = (ctx->format_version != NcaVersion_Nca0 ? (sizeof(NcaHeader) + (i * sizeof(NcaFsHeader))) : fs_ctx->section_offset);
        fs_ctx->header_written = ncaWritePatchToMemoryBuffer(ctx, &(fs_ctx->encrypted_header), sizeof(NcaFsHeader), fs_header_offset, buf, buf_size, buf_offset);
        if (!fs_ctx->header_written) ctx->header_written = false;
    }
}

void ncaUpdateContentIdAndHash(NcaContext *ctx, u8 hash[SHA256_HASH_SIZE])
{
    if (!ctx) return;

    /* Update content ID. */
    memcpy(ctx->content_id.c, hash, sizeof(ctx->content_id.c));
    utilsGenerateHexStringFromData(ctx->content_id_str, sizeof(ctx->content_id_str), ctx->content_id.c, sizeof(ctx->content_id.c), false);

    /* Update content hash. */
    memcpy(ctx->hash, hash, sizeof(ctx->hash));
    utilsGenerateHexStringFromData(ctx->hash_str, sizeof(ctx->hash_str), ctx->hash, sizeof(ctx->hash), false);
}

const char *ncaGetFsSectionTypeName(NcaFsSectionContext *ctx)
{
    NcaContext *nca_ctx = NULL;
    const char *str = "Invalid";
    bool is_exefs = false;

    if (!ctx || !(nca_ctx = (NcaContext*)ctx->nca_ctx)) return str;

    is_exefs = (nca_ctx->content_type == NcmContentType_Program && ctx->section_idx == 0);

    switch(ctx->section_type)
    {
        case NcaFsSectionType_PartitionFs:
            str = (is_exefs ? "ExeFS" : "Partition FS");
            break;
        case NcaFsSectionType_RomFs:
            str = "RomFS";
            break;
        case NcaFsSectionType_PatchRomFs:
            str = "Patch RomFS";
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
    if (!ctx || !*(ctx->content_id_str) || ctx->content_size < NCA_FULL_HEADER_LENGTH)
    {
        LOG_MSG_ERROR("Invalid NCA context!");
        return false;
    }

    u32 magic = 0;
    size_t crypt_res = 0;
    const u8 *header_key = keysGetNcaHeaderKey();
    Aes128XtsContext hdr_aes_ctx = {0}, nca0_fs_header_ctx = {0};

    if (!header_key)
    {
        LOG_MSG_ERROR("Failed to retrieve NCA header key!");
        return false;
    }

    /* Read NCA header. */
    if (!ncaReadContentFile(ctx, &(ctx->encrypted_header), sizeof(NcaHeader), 0))
    {
        LOG_MSG_ERROR("Failed to read NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }

    /* Prepare NCA header AES-128-XTS context. */
    aes128XtsContextCreate(&hdr_aes_ctx, header_key, header_key + AES_128_KEY_SIZE, false);

    /* Decrypt NCA header. */
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(ctx->header), &(ctx->encrypted_header), sizeof(NcaHeader), 0, NCA_AES_XTS_SECTOR_SIZE, false);
    magic = __builtin_bswap32(ctx->header.magic);

    if (crypt_res != sizeof(NcaHeader) || (magic != NCA_NCA3_MAGIC && magic != NCA_NCA2_MAGIC && magic != NCA_NCA0_MAGIC) || ctx->header.content_size != ctx->content_size)
    {
        LOG_MSG_ERROR("Error decrypting NCA \"%s\" header!", ctx->content_id_str);
        return false;
    }

    /* Fill additional NCA context info. */
    ctx->format_version = (magic == NCA_NCA3_MAGIC ? NcaVersion_Nca3 : (magic == NCA_NCA2_MAGIC ? NcaVersion_Nca2 : NcaVersion_Nca0));
    ctx->key_generation = ncaGetKeyGenerationValue(ctx);
    ctx->rights_id_available = ncaCheckRightsIdAvailability(ctx);
    sha256CalculateHash(ctx->header_hash, &(ctx->header), sizeof(NcaHeader));
    ctx->valid_main_signature = ncaVerifyMainSignature(ctx);

    /* Decrypt NCA key area (if needed). */
    if (!ctx->rights_id_available && !ncaDecryptKeyArea(ctx))
    {
        LOG_MSG_ERROR("Error decrypting NCA \"%s\" key area!", ctx->content_id_str);
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
        if (!ncaIsFsInfoEntryValid(fs_info)) continue;

        /* Read NCA FS section header. */
        u64 fs_header_offset = (ctx->format_version != NcaVersion_Nca0 ? (sizeof(NcaHeader) + (i * sizeof(NcaFsHeader))) : NCA_FS_SECTOR_OFFSET(fs_info->start_sector));
        if (!ncaReadContentFile(ctx, &(fs_ctx->encrypted_header), sizeof(NcaFsHeader), fs_header_offset))
        {
            LOG_MSG_ERROR("Failed to read NCA%u \"%s\" FS section header #%u at offset 0x%lX!", ctx->format_version, ctx->content_id_str, i, fs_header_offset);
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
            LOG_MSG_ERROR("Error decrypting NCA%u \"%s\" FS section header #%u!", ctx->format_version, ctx->content_id_str, i);
            return false;
        }
    }

    return true;
}

static bool ncaDecryptKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOG_MSG_ERROR("Invalid NCA context!");
        return false;
    }

    const u8 null_key[AES_128_KEY_SIZE] = {0};

    u8 key_count = NCA_KEY_AREA_USED_KEY_COUNT;
    if (ctx->format_version == NcaVersion_Nca0) key_count--;

    /* Check if we're dealing with a NCA0 with a plaintext key area. */
    if (ncaIsVersion0KeyAreaEncrypted(ctx))
    {
        memcpy(&(ctx->decrypted_key_area), &(ctx->header.encrypted_key_area), sizeof(NcaDecryptedKeyArea));
        return true;
    }

    /* Clear decrypted key area. */
    memset(&(ctx->decrypted_key_area), 0, sizeof(NcaDecryptedKeyArea));

    /* Process key area. */
    for(u8 i = 0; i < key_count; i++)
    {
        const u8 *src_key = ctx->header.encrypted_key_area.keys[i];
        u8 *dst_key = ctx->decrypted_key_area.keys[i];

        /* Don't proceed if we're dealing with a null key. */
        if (!memcmp(src_key, null_key, AES_128_KEY_SIZE)) continue;

        /* Decrypt current key area entry. */
        if (!keysDecryptNcaKeyAreaEntry(ctx->header.kaek_index, ctx->key_generation, dst_key, src_key))
        {
            LOG_MSG_ERROR("Failed to decrypt NCA key area entry #%u!", i);
            return false;
        }
    }

    return true;
}

static bool ncaEncryptKeyArea(NcaContext *ctx)
{
    if (!ctx)
    {
        LOG_MSG_ERROR("Invalid NCA context!");
        return false;
    }

    u8 key_count = NCA_KEY_AREA_USED_KEY_COUNT;
    if (ctx->format_version == NcaVersion_Nca0) key_count--;

    const u8 *kaek = NULL, null_key[AES_128_KEY_SIZE] = {0};
    Aes128Context key_area_ctx = {0};

    /* Check if we're dealing with a NCA0 with a plaintext key area. */
    if (ncaIsVersion0KeyAreaEncrypted(ctx))
    {
        memcpy(&(ctx->header.encrypted_key_area), &(ctx->decrypted_key_area), sizeof(NcaDecryptedKeyArea));
        return true;
    }

    /* Get KAEK for these key generation and KAEK index values. */
    kaek = keysGetNcaKeyAreaEncryptionKey(ctx->header.kaek_index, ctx->key_generation);
    if (!kaek)
    {
        LOG_MSG_ERROR("Unable to retrieve KAEK for KAEK index 0x%02X and key generation 0x%02X!", ctx->header.kaek_index, ctx->key_generation);
        return false;
    }

    /* Clear encrypted key area. */
    memset(&(ctx->header.encrypted_key_area), 0, sizeof(NcaEncryptedKeyArea));

    /* Initialize AES-128-ECB encryption context using the retrieved KAEK. */
    aes128ContextCreate(&key_area_ctx, kaek, true);

    /* Process key area. */
    for(u8 i = 0; i < key_count; i++)
    {
        const u8 *src_key = ctx->decrypted_key_area.keys[i];
        u8 *dst_key = ctx->header.encrypted_key_area.keys[i];

        /* Don't proceed if we're dealing with a null key. */
        if (!memcmp(src_key, null_key, AES_128_KEY_SIZE)) continue;

        /* Encrypt current key area entry. */
        aes128EncryptBlock(&key_area_ctx, dst_key, src_key);
    }

    return true;
}

static bool ncaVerifyMainSignature(NcaContext *ctx)
{
    if (!ctx)
    {
        LOG_MSG_ERROR("Invalid NCA context!");
        return false;
    }

    /* Retrieve modulus for the NCA main signature. */
    const u8 *modulus = keysGetNcaMainSignatureModulus(ctx->header.main_signature_key_generation);
    if (!modulus) return false;

    /* Verify NCA signature. */
    bool ret = rsa2048VerifySha256BasedPssSignature(&(ctx->header.magic), NCA_SIGNATURE_AREA_SIZE, ctx->header.main_signature, modulus, g_ncaHeaderMainSignaturePublicExponent, \
                                                    sizeof(g_ncaHeaderMainSignaturePublicExponent));

    LOG_MSG_INFO("Header signature for %s NCA \"%s\" is %s.", titleGetNcmContentTypeName(ctx->content_type), ctx->content_id_str, ret ? "valid" : "invalid");

    return ret;
}

NX_INLINE bool ncaIsVersion0KeyAreaEncrypted(NcaContext *ctx)
{
    if (!ctx || ctx->format_version != NcaVersion_Nca0) return false;

    u8 nca0_key_area_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(nca0_key_area_hash, &(ctx->header.encrypted_key_area), 4 * AES_128_KEY_SIZE);
    return (memcmp(nca0_key_area_hash, g_nca0KeyAreaHash, SHA256_HASH_SIZE) != 0);
}

NX_INLINE u8 ncaGetKeyGenerationValue(NcaContext *ctx)
{
    if (!ctx) return 0;
    return (ctx->header.key_generation > ctx->header.key_generation_old ? ctx->header.key_generation : ctx->header.key_generation_old);
}

NX_INLINE bool ncaCheckRightsIdAvailability(NcaContext *ctx)
{
    if (!ctx) return false;

    for(u8 i = 0; i < 0x10; i++)
    {
        if (ctx->header.rights_id.c[i]) return true;
    }

    return false;
}

static bool ncaInitializeFsSectionContext(NcaContext *nca_ctx, u32 section_idx)
{
    if (!nca_ctx || section_idx >= NCA_FS_HEADER_COUNT)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    NcaFsInfo *fs_info = &(nca_ctx->header.fs_info[section_idx]);
    NcaFsSectionContext *fs_ctx = &(nca_ctx->fs_ctx[section_idx]);

    u8 fs_header_hash_calc[SHA256_HASH_SIZE] = {0};
    u8 *fs_header_hash = nca_ctx->header.fs_header_hash[section_idx].hash;

    NcaSparseInfo *sparse_info = &(fs_ctx->header.sparse_info);
    NcaBucketInfo *sparse_bucket = &(sparse_info->bucket);

    NcaBucketInfo *compression_bucket = &(fs_ctx->header.compression_info.bucket);

    bool success = false;

    /* Fill section context. */
    fs_ctx->enabled = false;
    fs_ctx->nca_ctx = nca_ctx;
    fs_ctx->section_idx = section_idx;
    fs_ctx->section_type = NcaFsSectionType_Invalid;    /* Placeholder. */
    fs_ctx->has_patch_indirect_layer = (fs_ctx->header.patch_info.indirect_bucket.size > 0);
    fs_ctx->has_patch_aes_ctr_ex_layer = (fs_ctx->header.patch_info.aes_ctr_ex_bucket.size > 0);
    fs_ctx->has_sparse_layer = (sparse_info->generation != 0);
    fs_ctx->has_compression_layer = (compression_bucket->offset != 0 && compression_bucket->size != 0);
    fs_ctx->cur_sparse_virtual_offset = 0;

    /* Don't proceed if this NCA FS section isn't populated. */
    if (!ncaIsFsInfoEntryValid(fs_info))
    {
        LOG_MSG_INFO("Invalid FsInfo entry for section #%u in \"%s\". Skipping FS section.", section_idx, nca_ctx->content_id_str);
        goto end;
    }

    /* Calculate NCA FS section header hash. Don't proceed if there's a checksum mismatch. */
    sha256CalculateHash(fs_header_hash_calc, &(fs_ctx->header), sizeof(NcaFsHeader));
    if (memcmp(fs_header_hash_calc, fs_header_hash, SHA256_HASH_SIZE) != 0)
    {
        LOG_MSG_ERROR("Checksum mismatch for FS section header #%u in \"%s\". Skipping FS section.", section_idx, nca_ctx->content_id_str);
        goto end;
    }

    /* Calculate section offset and size. */
    fs_ctx->section_offset = NCA_FS_SECTOR_OFFSET(fs_info->start_sector);
    fs_ctx->section_size = (NCA_FS_SECTOR_OFFSET(fs_info->end_sector) - fs_ctx->section_offset);

    /* Check if we're dealing with an invalid start offset or an empty size. */
    if (fs_ctx->section_offset < sizeof(NcaHeader) || !fs_ctx->section_size)
    {
        LOG_MSG_ERROR("Invalid offset/size for FS section #%u in \"%s\" (0x%lX, 0x%lX). Skipping FS section.", section_idx, nca_ctx->content_id_str, fs_ctx->section_offset, \
                fs_ctx->section_size);
        goto end;
    }

    /* Determine FS section hash type. */
    fs_ctx->hash_type = fs_ctx->header.hash_type;
    if (fs_ctx->hash_type == NcaHashType_Auto || fs_ctx->hash_type == NcaHashType_AutoSha3)
    {
        switch(fs_ctx->section_idx)
        {
            case 0: /* ExeFS Partition FS. */
            case 2: /* Logo Partition FS. */
                fs_ctx->hash_type = (fs_ctx->hash_type == NcaHashType_Auto ? NcaHashType_HierarchicalSha256 : NcaHashType_HierarchicalSha3256);
                break;
            case 1: /* RomFS. */
                fs_ctx->hash_type = (fs_ctx->hash_type == NcaHashType_Auto ? NcaHashType_HierarchicalIntegrity : NcaHashType_HierarchicalIntegritySha3);
                break;
            default:
                break;
        }
    }

    if (fs_ctx->hash_type == NcaHashType_Auto || fs_ctx->hash_type == NcaHashType_AutoSha3 || fs_ctx->hash_type > NcaHashType_HierarchicalIntegritySha3)
    {
        LOG_MSG_ERROR("Invalid hash type for FS section #%u in \"%s\" (0x%02X). Skipping FS section.", section_idx, nca_ctx->content_id_str, fs_ctx->hash_type);
        goto end;
    }

    /* Determine FS section encryption type. */
    fs_ctx->encryption_type = (nca_ctx->format_version == NcaVersion_Nca0 ? NcaEncryptionType_AesXts : fs_ctx->header.encryption_type);
    if (fs_ctx->encryption_type == NcaEncryptionType_Auto)
    {
        switch(fs_ctx->section_idx)
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

    if (fs_ctx->encryption_type == NcaEncryptionType_Auto || fs_ctx->encryption_type > NcaEncryptionType_AesCtrExSkipLayerHash)
    {
        LOG_MSG_ERROR("Invalid encryption type for FS section #%u in \"%s\" (0x%02X). Skipping FS section.", section_idx, nca_ctx->content_id_str, fs_ctx->encryption_type);
        goto end;
    }

    /* Check if we're dealing with a sparse layer. */
    if (fs_ctx->has_sparse_layer)
    {
        /* Check if the sparse bucket is valid. */
        u64 raw_storage_offset = sparse_info->physical_offset;
        u64 raw_storage_size = (sparse_bucket->offset + sparse_bucket->size);

        if (!ncaVerifyBucketInfo(sparse_bucket) || raw_storage_offset < sizeof(NcaHeader) || (raw_storage_offset + raw_storage_size) > nca_ctx->content_size)
        {
            LOG_DATA_ERROR(sparse_info, sizeof(NcaSparseInfo), "Invalid SparseInfo data for FS section #%u in \"%s\" (0x%lX). Skipping FS section. SparseInfo dump:", section_idx, \
                           nca_ctx->content_id_str, nca_ctx->content_size);
            goto end;
        }

        if (!raw_storage_size || !sparse_bucket->header.entry_count)
        {
            /* Return true but don't set this FS section as enabled, since we can't really use it. */
            LOG_MSG_ERROR("Empty SparseInfo data detected for FS section #%u in \"%s\". Skipping FS section.", section_idx, nca_ctx->content_id_str);
            success = true;
            goto end;
        }

        /* Update context. */
        fs_ctx->sparse_table_offset = (sparse_info->physical_offset + sparse_bucket->offset);
        fs_ctx->section_size = raw_storage_size;
    }

    /* Check if we're within boundaries. */
    if ((fs_ctx->section_offset + fs_ctx->section_size) > nca_ctx->content_size)
    {
        LOG_MSG_ERROR("FS section #%u in \"%s\" is out of NCA boundaries. Skipping FS section.", section_idx, nca_ctx->content_id_str);
        goto end;
    }

    /* Determine FS section type. */
    /* TODO: should NcaHashType_None be handled here as well? */
    switch(fs_ctx->header.fs_type)
    {
        case NcaFsType_PartitionFs:
            if ((fs_ctx->hash_type == NcaHashType_HierarchicalSha256 || fs_ctx->hash_type == NcaHashType_HierarchicalSha3256) && \
                (fs_ctx->encryption_type < NcaEncryptionType_AesCtrEx || fs_ctx->encryption_type == NcaEncryptionType_AesCtrSkipLayerHash))
            {
                /* Partition FS with None, XTS or CTR encryption. */
                fs_ctx->section_type = NcaFsSectionType_PartitionFs;
            }

            break;
        case NcaFsType_RomFs:
            if (fs_ctx->hash_type == NcaHashType_HierarchicalIntegrity || fs_ctx->hash_type == NcaHashType_HierarchicalIntegritySha3)
            {
                if (fs_ctx->has_patch_indirect_layer && fs_ctx->has_patch_aes_ctr_ex_layer && \
                    (fs_ctx->encryption_type == NcaEncryptionType_None || fs_ctx->encryption_type == NcaEncryptionType_AesCtrEx || \
                    fs_ctx->encryption_type == NcaEncryptionType_AesCtrExSkipLayerHash))
                {
                    /* Patch RomFS. */
                    fs_ctx->section_type = NcaFsSectionType_PatchRomFs;
                } else
                if (!fs_ctx->has_patch_indirect_layer && !fs_ctx->has_patch_aes_ctr_ex_layer && \
                    ((fs_ctx->encryption_type >= NcaEncryptionType_None && fs_ctx->encryption_type <= NcaEncryptionType_AesCtr) || \
                    fs_ctx->encryption_type == NcaEncryptionType_AesCtrSkipLayerHash))
                {
                    /* Regular RomFS. */
                    fs_ctx->section_type = NcaFsSectionType_RomFs;
                }
            } else
            if (nca_ctx->format_version == NcaVersion_Nca0 && fs_ctx->hash_type == NcaHashType_HierarchicalSha256)
            {
                /* NCA0 RomFS with XTS encryption. */
                fs_ctx->section_type = NcaFsSectionType_Nca0RomFs;
            }

            break;
        default:
            break;
    }

    if (fs_ctx->section_type >= NcaFsSectionType_Invalid)
    {
        LOG_DATA_ERROR(&(fs_ctx->header), sizeof(NcaFsHeader), "Unable to determine section type for FS section #%u in \"%s\" (0x%02X, 0x%02X). Skipping FS section. FS header dump:", \
                       section_idx, nca_ctx->content_id_str, fs_ctx->hash_type, fs_ctx->encryption_type);
        goto end;
    }

    /* Validate HashData boundaries. */
    if (!ncaFsSectionValidateHashDataBoundaries(fs_ctx)) goto end;

    /* Get hash layer region size (offset must always be 0). */
    fs_ctx->hash_region.offset = 0;
    if (!ncaGetFsSectionHashTargetExtents(fs_ctx, &(fs_ctx->hash_region.size), NULL))
    {
        LOG_MSG_ERROR("Invalid hash type for FS section #%u in \"%s\" (0x%02X). Skipping FS section.", fs_ctx->section_idx, nca_ctx->content_id_str, fs_ctx->hash_type);
        goto end;
    }

    /* Check if we're within boundaries. */
    if (fs_ctx->hash_region.size > fs_ctx->section_size || (fs_ctx->section_offset + fs_ctx->hash_region.size) > nca_ctx->content_size)
    {
        LOG_MSG_ERROR("Hash layer region for FS section #%u in \"%s\" is out of NCA boundaries. Skipping FS section.", section_idx, nca_ctx->content_id_str);
        goto end;
    }

    /* Check if we should skip hash layer decryption while reading this FS section. */
    fs_ctx->skip_hash_layer_crypto = (fs_ctx->encryption_type == NcaEncryptionType_AesCtrSkipLayerHash || fs_ctx->encryption_type == NcaEncryptionType_AesCtrExSkipLayerHash);
    if (fs_ctx->skip_hash_layer_crypto && fs_ctx->hash_type == NcaHashType_None)
    {
        LOG_MSG_ERROR("NcaHashType_None used with SkipLayerHash crypto for FS section #%u in \"%s\". Skipping FS section.", section_idx, nca_ctx->content_id_str);
        goto end;
    }

    /* Check if we're dealing with a compression layer. */
    if (fs_ctx->has_compression_layer)
    {
        u64 raw_storage_offset = 0;
        u64 raw_storage_size = compression_bucket->size;

        if (fs_ctx->section_type != NcaFsSectionType_PatchRomFs)
        {
            /* Get target hash layer offset. */
            if (!ncaGetFsSectionHashTargetExtents(fs_ctx, &raw_storage_offset, NULL))
            {
                LOG_MSG_ERROR("Invalid hash type for FS section #%u in \"%s\" (0x%02X). Skipping FS section.", fs_ctx->section_idx, nca_ctx->content_id_str, fs_ctx->hash_type);
                goto end;
            }

            /* Update compression layer offset. */
            raw_storage_offset += compression_bucket->offset;
        }

        /* Check if the compression bucket is valid. Don't verify extents if we're dealing with a Patch RomFS. */
        if (!ncaVerifyBucketInfo(compression_bucket) || !compression_bucket->header.entry_count || (raw_storage_offset && (raw_storage_offset < sizeof(NcaHeader) || \
            (raw_storage_offset + raw_storage_size) > fs_ctx->section_size || (fs_ctx->section_offset + raw_storage_offset + raw_storage_size) > nca_ctx->content_size)))
        {
            LOG_DATA_ERROR(compression_bucket, sizeof(NcaBucketInfo), "Invalid CompressionInfo data for FS section #%u in \"%s\" (0x%lX). Skipping FS section. CompressionInfo dump:", \
                           section_idx, nca_ctx->content_id_str, nca_ctx->content_size);
            goto end;
        }
    }

    /* Initialize crypto data. */
    if ((!nca_ctx->rights_id_available || (nca_ctx->rights_id_available && nca_ctx->titlekey_retrieved)) && fs_ctx->encryption_type > NcaEncryptionType_None && \
        fs_ctx->encryption_type <= NcaEncryptionType_AesCtrExSkipLayerHash)
    {
        /* Initialize the partial AES counter for this section. */
        aes128CtrInitializePartialCtr(fs_ctx->ctr, fs_ctx->header.aes_ctr_upper_iv.value, fs_ctx->section_offset);

        /* Initialize AES context. */
        if (nca_ctx->rights_id_available)
        {
            /* AES-128-CTR is always used for FS crypto in NCAs with a rights ID. */
            aes128CtrContextCreate(&(fs_ctx->ctr_ctx), nca_ctx->titlekey, fs_ctx->ctr);
        } else {
            if (fs_ctx->encryption_type == NcaEncryptionType_AesXts)
            {
                /* We need to create two different contexts with AES-128-XTS: one for decryption and another one for encryption. */
                aes128XtsContextCreate(&(fs_ctx->xts_decrypt_ctx), nca_ctx->decrypted_key_area.aes_xts_1, nca_ctx->decrypted_key_area.aes_xts_2, false);
                aes128XtsContextCreate(&(fs_ctx->xts_encrypt_ctx), nca_ctx->decrypted_key_area.aes_xts_1, nca_ctx->decrypted_key_area.aes_xts_2, true);
            } else
            if (fs_ctx->encryption_type >= NcaEncryptionType_AesCtr && fs_ctx->encryption_type <= NcaEncryptionType_AesCtrExSkipLayerHash)
            {
                /* Patch RomFS sections also use the AES-128-CTR key from the decrypted NCA key area, for some reason. */
                aes128CtrContextCreate(&(fs_ctx->ctr_ctx), nca_ctx->decrypted_key_area.aes_ctr, fs_ctx->ctr);
            }
        }
    }

    /* Enable FS context if we got up to this point. */
    fs_ctx->enabled = success = true;

end:
    return success;
}

static bool ncaFsSectionValidateHashDataBoundaries(NcaFsSectionContext *ctx)
{
#if LOG_LEVEL <= LOG_LEVEL_WARNING
    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
#endif

    bool success = false, valid = true;
    u64 accum = 0;

    switch(ctx->hash_type)
    {
        case NcaHashType_None:
            /* Nothing to validate. */
            success = true;
            break;
        case NcaHashType_HierarchicalSha256:
        case NcaHashType_HierarchicalSha3256:
            {
                NcaHierarchicalSha256Data *hash_data = &(ctx->header.hash_data.hierarchical_sha256_data);
                if (!hash_data->hash_block_size || !hash_data->hash_region_count || hash_data->hash_region_count > NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT)
                {
                    LOG_DATA_WARNING(hash_data, sizeof(NcaHierarchicalSha256Data), "Invalid HierarchicalSha256 data for FS section #%u in \"%s\". Skipping FS section. Hash data dump:", \
                                     ctx->section_idx, nca_ctx->content_id_str);
                    break;
                }

                for(u32 i = 0; i < hash_data->hash_region_count; i++)
                {
                    /* Validate all hash regions boundaries. Skip the last one if a sparse layer is used. */
                    NcaRegion *hash_region = &(hash_data->hash_region[i]);
                    if (hash_region->offset < accum || !hash_region->size || \
                        ((i < (hash_data->hash_region_count - 1) || !ctx->has_sparse_layer) && (hash_region->offset + hash_region->size) > ctx->section_size))
                    {
                        LOG_MSG_WARNING("HierarchicalSha256 region #%u for FS section #%u in \"%s\" is out of NCA boundaries. Skipping FS section.", \
                                        i, ctx->section_idx, nca_ctx->content_id_str);
                        valid = false;
                        break;
                    }

                    accum = (hash_region->offset + hash_region->size);
                }

                success = valid;
            }

            break;
        case NcaHashType_HierarchicalIntegrity:
        case NcaHashType_HierarchicalIntegritySha3:
            {
                NcaIntegrityMetaInfo *hash_data = &(ctx->header.hash_data.integrity_meta_info);
                if (__builtin_bswap32(hash_data->magic) != NCA_IVFC_MAGIC || hash_data->master_hash_size != SHA256_HASH_SIZE || \
                    hash_data->info_level_hash.max_level_count != NCA_IVFC_MAX_LEVEL_COUNT)
                {
                    LOG_DATA_WARNING(hash_data, sizeof(NcaIntegrityMetaInfo), "Invalid HierarchicalIntegrity data for FS section #%u in \"%s\". Skipping FS section. Hash data dump:", \
                                     ctx->section_idx, nca_ctx->content_id_str);
                    break;
                }

                for(u32 i = 0; i < NCA_IVFC_LEVEL_COUNT; i++)
                {
                    /* Validate all level informations boundaries. Skip the last one if we're dealing with a Patch RomFS, or if a sparse layer is used. */
                    NcaHierarchicalIntegrityVerificationLevelInformation *lvl_info = &(hash_data->info_level_hash.level_information[i]);
                    if (lvl_info->offset < accum || !lvl_info->size || !lvl_info->block_order || ((i < (NCA_IVFC_LEVEL_COUNT - 1) || \
                        (!ctx->has_sparse_layer && ctx->section_type != NcaFsSectionType_PatchRomFs)) && (lvl_info->offset + lvl_info->size) > ctx->section_size))
                    {
                        LOG_MSG_WARNING("HierarchicalIntegrity level #%u for FS section #%u in \"%s\" is out of NCA boundaries. Skipping FS section.", \
                                        i, ctx->section_idx, nca_ctx->content_id_str);
                        valid = false;
                        break;
                    }

                    accum = (lvl_info->offset + lvl_info->size);
                }

                success = valid;
            }

            break;
        default:
            LOG_MSG_WARNING("Invalid hash type for FS section #%u in \"%s\" (0x%02X). Skipping FS section.", ctx->section_idx, nca_ctx->content_id_str, ctx->hash_type);
            break;
    }

    return success;
}

static bool _ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_idx >= NCA_FS_HEADER_COUNT || ctx->section_offset < sizeof(NcaHeader) || \
        ctx->section_type >= NcaFsSectionType_Invalid || ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type > NcaEncryptionType_AesCtrExSkipLayerHash || \
        !out || !read_size || (offset + read_size) > ctx->section_size)
    {
        LOG_MSG_ERROR("Invalid NCA FS section header parameters!");
        return false;
    }

    size_t crypt_res = 0;
    u64 sector_num = 0;

    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + offset);

    u64 sparse_virtual_offset = ((ctx->has_sparse_layer && ctx->cur_sparse_virtual_offset) ? (ctx->section_offset + ctx->cur_sparse_virtual_offset) : 0);
    u64 iv_offset = (sparse_virtual_offset ? sparse_virtual_offset : content_offset);

    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 data_start_offset = 0, chunk_size = 0, out_chunk_size = 0;

    bool ret = false;

    if (!*(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || \
        (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        (nca_ctx->format_version != NcaVersion_Nca0 && nca_ctx->format_version != NcaVersion_Nca2 && nca_ctx->format_version != NcaVersion_Nca3) || \
        (content_offset + read_size) > nca_ctx->content_size)
    {
        LOG_MSG_ERROR("Invalid NCA header parameters!");
        goto end;
    }

    /* Check if we're supposed to read a hash layer without encryption. */
    if (ncaFsSectionCheckHashRegionAccess(ctx, offset, read_size, &block_size))
    {
        /* Read plaintext area. Use NCA-relative offset. */
        if (!ncaReadContentFile(nca_ctx, out, block_size, content_offset))
        {
            LOG_MSG_ERROR("Failed to read 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (plaintext hash region) (#1).", block_size, content_offset, \
                    nca_ctx->content_id_str, ctx->section_idx);
            goto end;
        }

        /* Read remaining encrypted data, if needed. Use FS-section-relative offset. */
        if (sparse_virtual_offset) ctx->cur_sparse_virtual_offset += block_size;
        ret = (read_size ? _ncaReadFsSection(ctx, (u8*)out + block_size, read_size - block_size, offset + block_size) : true);
        goto end;
    } else
    if (block_size && block_size < read_size)
    {
        /* Read encrypted area. Use FS-section-relative offset. */
        if (!_ncaReadFsSection(ctx, out, block_size, offset)) goto end;

        /* Update parameters. */
        read_size -= block_size;
        content_offset += block_size;

        /* Read remaining plaintext data. Use NCA-relative offset. */
        ret = ncaReadContentFile(nca_ctx, (u8*)out + block_size, read_size, content_offset);
        if (!ret) LOG_MSG_ERROR("Failed to read 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (plaintext hash region) (#2).", read_size, content_offset, \
                          nca_ctx->content_id_str, ctx->section_idx);
        goto end;
    }

    /* Optimization for reads from plaintext FS sections or reads that are aligned to the AES-CTR / AES-XTS sector size. */
    if (ctx->encryption_type == NcaEncryptionType_None || \
        (ctx->encryption_type == NcaEncryptionType_AesXts && !(content_offset % NCA_AES_XTS_SECTOR_SIZE) && !(read_size % NCA_AES_XTS_SECTOR_SIZE)) || \
        (ctx->encryption_type >= NcaEncryptionType_AesCtr && ctx->encryption_type <= NcaEncryptionType_AesCtrExSkipLayerHash && !(content_offset % AES_BLOCK_SIZE) && !(read_size % AES_BLOCK_SIZE)))
    {
        /* Read data. */
        if (!ncaReadContentFile(nca_ctx, out, read_size, content_offset))
        {
            LOG_MSG_ERROR("Failed to read 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, ctx->section_idx);
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
                LOG_MSG_ERROR("Failed to AES-XTS decrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, \
                        ctx->section_idx);
                goto end;
            }
        } else
        if (ctx->encryption_type >= NcaEncryptionType_AesCtr && ctx->encryption_type <= NcaEncryptionType_AesCtrExSkipLayerHash)
        {
            aes128CtrUpdatePartialCtr(ctx->ctr, iv_offset);
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
        LOG_MSG_ERROR("Failed to read 0x%lX bytes encrypted data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, \
                ctx->section_idx);
        goto end;
    }

    /* Decrypt data. */
    if (ctx->encryption_type == NcaEncryptionType_AesXts)
    {
        sector_num = ((nca_ctx->format_version != NcaVersion_Nca0 ? offset : (content_offset - sizeof(NcaHeader))) / NCA_AES_XTS_SECTOR_SIZE);

        crypt_res = aes128XtsNintendoCrypt(&(ctx->xts_decrypt_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size, sector_num, NCA_AES_XTS_SECTOR_SIZE, false);
        if (crypt_res != chunk_size)
        {
            LOG_MSG_ERROR("Failed to AES-XTS decrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, \
                    ctx->section_idx);
            goto end;
        }
    } else
    if (ctx->encryption_type >= NcaEncryptionType_AesCtr && ctx->encryption_type <= NcaEncryptionType_AesCtrExSkipLayerHash)
    {
        aes128CtrUpdatePartialCtr(ctx->ctr, ALIGN_DOWN(iv_offset, AES_BLOCK_SIZE));
        aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
        aes128CtrCrypt(&(ctx->ctr_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size);
    }

    /* Copy decrypted data. */
    memcpy(out, g_ncaCryptoBuffer + data_start_offset, out_chunk_size);

    /* Perform another read if required. */
    if (sparse_virtual_offset && block_size > NCA_CRYPTO_BUFFER_SIZE) ctx->cur_sparse_virtual_offset += out_chunk_size;
    ret = (block_size > NCA_CRYPTO_BUFFER_SIZE ? _ncaReadFsSection(ctx, (u8*)out + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size) : true);

end:
    if (ctx->has_sparse_layer) ctx->cur_sparse_virtual_offset = 0;

    return ret;
}

static bool ncaFsSectionCheckHashRegionAccess(NcaFsSectionContext *ctx, u64 offset, u64 size, u64 *out_chunk_size)
{
    if (!ctx->skip_hash_layer_crypto) return false;

    NcaRegion *hash_region = &(ctx->hash_region);

    /* Check if our region contains the access. */
    if (hash_region->offset <= offset)
    {
        if (offset < (hash_region->offset + hash_region->size))
        {
            if ((hash_region->offset + hash_region->size) <= (offset + size))
            {
                *out_chunk_size = ((hash_region->offset + hash_region->size) - offset);
            } else {
                *out_chunk_size = size;
            }

            return true;
        } else {
            return false;
        }
    } else {
        if (hash_region->offset <= (offset + size)) *out_chunk_size = (hash_region->offset - offset);

        return false;
    }
}

static bool _ncaReadAesCtrExStorage(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool decrypt)
{
    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || !ctx->nca_ctx || ctx->section_idx >= NCA_FS_HEADER_COUNT || ctx->section_offset < sizeof(NcaHeader) || \
        ctx->section_type != NcaFsSectionType_PatchRomFs || (ctx->encryption_type != NcaEncryptionType_None && ctx->encryption_type != NcaEncryptionType_AesCtrEx && \
        ctx->encryption_type != NcaEncryptionType_AesCtrExSkipLayerHash) || !out || !read_size || (offset + read_size) > ctx->section_size)
    {
        LOG_MSG_ERROR("Invalid NCA FS section header parameters!");
        return false;
    }

    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + offset);

    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 data_start_offset = 0, chunk_size = 0, out_chunk_size = 0;

    bool ret = false;

    if (!*(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        (content_offset + read_size) > nca_ctx->content_size)
    {
        LOG_MSG_ERROR("Invalid NCA header parameters!");
        goto end;
    }

    /* Optimization for reads that are aligned to the AES-CTR sector size. */
    if (!decrypt || (!(content_offset % AES_BLOCK_SIZE) && !(read_size % AES_BLOCK_SIZE)))
    {
        /* Read data. */
        if (!ncaReadContentFile(nca_ctx, out, read_size, content_offset))
        {
            LOG_MSG_ERROR("Failed to read 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", read_size, content_offset, nca_ctx->content_id_str, ctx->section_idx);
            goto end;
        }

        /* Decrypt data, if needed. */
        if (decrypt)
        {
            aes128CtrUpdatePartialCtrEx(ctx->ctr, ctr_val, content_offset);
            aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
            aes128CtrCrypt(&(ctx->ctr_ctx), out, out, read_size);
        }

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
        LOG_MSG_ERROR("Failed to read 0x%lX bytes encrypted data block at offset 0x%lX from NCA \"%s\" FS section #%u! (unaligned).", chunk_size, block_start_offset, nca_ctx->content_id_str, \
                ctx->section_idx);
        goto end;
    }

    /* Decrypt data. */
    aes128CtrUpdatePartialCtrEx(ctx->ctr, ctr_val, block_start_offset);
    aes128CtrContextResetCtr(&(ctx->ctr_ctx), ctx->ctr);
    aes128CtrCrypt(&(ctx->ctr_ctx), g_ncaCryptoBuffer, g_ncaCryptoBuffer, chunk_size);

    /* Copy decrypted data. */
    memcpy(out, g_ncaCryptoBuffer + data_start_offset, out_chunk_size);

    ret = (block_size > NCA_CRYPTO_BUFFER_SIZE ? _ncaReadAesCtrExStorage(ctx, (u8*)out + out_chunk_size, read_size - out_chunk_size, offset + out_chunk_size, ctr_val, decrypt) : true);

end:
    return ret;
}

static void ncaCalculateLayerHash(void *dst, const void *src, size_t size, bool use_sha3)
{
    if (use_sha3)
    {
        sha256CalculateHash(dst, src, size);
    } else {
        sha3256CalculateHash(dst, src, size);
    }
}

/* In this function, the term "layer" is used as a generic way to refer to both HierarchicalSha256 hash regions and HierarchicalIntegrity verification levels. */
static bool ncaGenerateHashDataPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, void *out, bool is_integrity_patch)
{
    NcaContext *nca_ctx = NULL;
    NcaHierarchicalSha256Patch *hierarchical_sha256_patch = (!is_integrity_patch ? ((NcaHierarchicalSha256Patch*)out) : NULL);
    NcaHierarchicalIntegrityPatch *hierarchical_integrity_patch = (is_integrity_patch ? ((NcaHierarchicalIntegrityPatch*)out) : NULL);

    u8 *cur_data = NULL;
    u64 cur_data_offset = data_offset;
    u64 cur_data_size = data_size;

    u32 layer_count = 0;
    u8 *parent_layer_block = NULL, *cur_layer_block = NULL;
    u64 last_layer_size = 0;

    bool use_sha3 = false, success = false;

    if (!ctx || !ctx->enabled || ctx->has_sparse_layer || ctx->has_compression_layer || !(nca_ctx = (NcaContext*)ctx->nca_ctx) || \
        (!is_integrity_patch && ((ctx->hash_type != NcaHashType_HierarchicalSha256 && ctx->hash_type != NcaHashType_HierarchicalSha3256) || \
        !ctx->header.hash_data.hierarchical_sha256_data.hash_block_size || !(layer_count = ctx->header.hash_data.hierarchical_sha256_data.hash_region_count) || \
        layer_count > NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT || !(last_layer_size = ctx->header.hash_data.hierarchical_sha256_data.hash_region[layer_count - 1].size))) || \
        (is_integrity_patch && ((ctx->hash_type != NcaHashType_HierarchicalIntegrity && ctx->hash_type != NcaHashType_HierarchicalIntegritySha3) || \
        !(layer_count = (ctx->header.hash_data.integrity_meta_info.info_level_hash.max_level_count - 1)) || layer_count != NCA_IVFC_LEVEL_COUNT || \
        !(last_layer_size = ctx->header.hash_data.integrity_meta_info.info_level_hash.level_information[NCA_IVFC_LEVEL_COUNT - 1].size))) || !data || !data_size || \
        (data_offset + data_size) > last_layer_size || !out || ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type == NcaEncryptionType_AesCtrEx || \
        ctx->encryption_type >= NcaEncryptionType_AesCtrExSkipLayerHash)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        goto end;
    }

    /* Clear output patch. */
    if (!is_integrity_patch)
    {
        ncaFreeHierarchicalSha256Patch(hierarchical_sha256_patch);
    } else {
        ncaFreeHierarchicalIntegrityPatch(hierarchical_integrity_patch);
    }

    /* Check if we should use SHA3-256 instead of SHA-256 for layer hash calculation. */
    use_sha3 = (ctx->hash_type == NcaHashType_HierarchicalSha3256 || ctx->hash_type == NcaHashType_HierarchicalIntegritySha3);

    /* Process each layer. */
    for(u32 i = layer_count; i > 0; i--)
    {
        u64 hash_block_size = 0;

        u64 cur_layer_offset = 0, cur_layer_size = 0;
        u64 cur_layer_read_start_offset = 0, cur_layer_read_end_offset = 0, cur_layer_read_size = 0, cur_layer_read_patch_offset = 0;

        u64 parent_layer_offset = 0, parent_layer_size = 0;
        u64 parent_layer_read_start_offset = 0, parent_layer_read_size = 0;

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
        if (hash_block_size <= 1 || !cur_layer_size || (cur_layer_offset + cur_layer_size) > ctx->section_size || (i > 1 && (!parent_layer_size || \
            (parent_layer_offset + parent_layer_size) > ctx->section_size)))
        {
            LOG_MSG_ERROR("Invalid hierarchical parent/child layer! (%u).", i - 1);
            goto end;
        }

        /* Retrieve pointer to the current layer patch. */
        cur_layer_patch = (!is_integrity_patch ? &(hierarchical_sha256_patch->hash_region_patch[i - 1]) : &(hierarchical_integrity_patch->hash_level_patch[i - 1]));

        /* Calculate required offsets and sizes. */
        if (i > 1)
        {
            /* HierarchicalSha256 hash region with index 1 through 4, or HierarchicalIntegrity verification level with index 1 through 5. */
            cur_layer_read_start_offset = (cur_layer_offset + ALIGN_DOWN(cur_data_offset, hash_block_size));
            cur_layer_read_end_offset = (cur_layer_offset + ALIGN_UP(cur_data_offset + cur_data_size, hash_block_size));
            cur_layer_read_size = (cur_layer_read_end_offset - cur_layer_read_start_offset);

            parent_layer_read_start_offset = ((cur_data_offset / hash_block_size) * SHA256_HASH_SIZE);
            parent_layer_read_size = ((cur_layer_read_size / hash_block_size) * SHA256_HASH_SIZE);
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
            LOG_MSG_ERROR("Unable to allocate 0x%lX bytes for hierarchical layer #%u data block! (current).", cur_layer_read_size, i - 1);
            goto end;
        }

        /* Adjust current layer read size to avoid read errors (if needed). */
        if (cur_layer_read_end_offset > (cur_layer_offset + cur_layer_size))
        {
            cur_layer_read_end_offset = (cur_layer_offset + cur_layer_size);
            cur_layer_read_size = (cur_layer_read_end_offset - cur_layer_read_start_offset);
        }

        /* Read current layer block. */
        if (!_ncaReadFsSection(ctx, cur_layer_block, cur_layer_read_size, cur_layer_read_start_offset))
        {
            LOG_MSG_ERROR("Failed to read 0x%lX bytes long hierarchical layer #%u data block from offset 0x%lX! (current).", cur_layer_read_size, i - 1, cur_layer_read_start_offset);
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
                LOG_MSG_ERROR("Unable to allocate 0x%lX bytes for hierarchical layer #%u data block! (parent).", parent_layer_read_size, i - 2);
                goto end;
            }

            /* Read parent layer block. */
            if (!_ncaReadFsSection(ctx, parent_layer_block, parent_layer_read_size, parent_layer_offset + parent_layer_read_start_offset))
            {
                LOG_MSG_ERROR("Failed to read 0x%lX bytes long hierarchical layer #%u data block from offset 0x%lX! (parent).", parent_layer_read_size, i - 2, parent_layer_read_start_offset);
                goto end;
            }

            /* HierarchicalSha256: size is truncated for blocks smaller than the hash block size. */
            /* HierarchicalIntegrity: size *isn't* truncated for blocks smaller than the hash block size, so we just keep using the same hash block size throughout the loop. */
            /*                        For these specific cases, the rest of the block should be filled with zeroes (already taken care of by using calloc()). */
            for(u64 j = 0, k = 0; j < cur_layer_read_size; j += hash_block_size, k++)
            {
                if (!is_integrity_patch && hash_block_size > (cur_layer_read_size - j)) hash_block_size = (cur_layer_read_size - j);
                ncaCalculateLayerHash(parent_layer_block + (k * SHA256_HASH_SIZE), cur_layer_block + j, hash_block_size, use_sha3);
            }
        } else {
            /* Recalculate master hash from the HashData area. */
            u8 *master_hash = (!is_integrity_patch ? ctx->header.hash_data.hierarchical_sha256_data.master_hash : ctx->header.hash_data.integrity_meta_info.master_hash);
            ncaCalculateLayerHash(master_hash, cur_layer_block, cur_layer_read_size, use_sha3);
        }

        if (!ctx->skip_hash_layer_crypto || i == layer_count)
        {
            /* Reencrypt current layer block (if needed). */
            cur_layer_patch->data = ncaGenerateEncryptedFsSectionBlock(ctx, cur_layer_block + cur_layer_read_patch_offset, cur_data_size, cur_layer_offset + cur_data_offset, \
                                                                        &(cur_layer_patch->size), &(cur_layer_patch->offset));
            if (!cur_layer_patch->data)
            {
                LOG_MSG_ERROR("Failed to generate encrypted 0x%lX bytes long hierarchical layer #%u data block!", cur_data_size, i - 1);
                goto end;
            }
        } else {
            /* Allocate memory for the data block and copy its information. */
            cur_layer_patch->data = malloc(cur_data_size);
            if (!cur_layer_patch->data)
            {
                LOG_MSG_ERROR("Failed to allocate 0x%lX bytes long buffer for hierarchical layer #%u data block!", cur_data_size, i - 1);
                goto end;
            }

            memcpy(cur_layer_patch->data, cur_layer_block + cur_layer_read_patch_offset, cur_data_size);
            cur_layer_patch->size = cur_data_size;
            cur_layer_patch->offset = (ctx->section_offset + cur_layer_offset + cur_data_offset);
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
    sha256CalculateHash(nca_ctx->header.fs_header_hash[ctx->section_idx].hash, &(ctx->header), sizeof(NcaFsHeader));

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

    return success;
}

static bool ncaWritePatchToMemoryBuffer(NcaContext *ctx, const void *patch, u64 patch_size, u64 patch_offset, void *buf, u64 buf_size, u64 buf_offset)
{
    /* Return right away if we're dealing with invalid parameters, or if the buffer data is not part of the range covered by the patch (last two conditions). */
    if (!ctx || !patch || !patch_size || (patch_offset + patch_size) > ctx->content_size || (buf_offset + buf_size) <= patch_offset || \
        (patch_offset + patch_size) <= buf_offset) return false;

    /* Overwrite buffer data using patch data. */
    u64 patch_block_offset = (patch_offset < buf_offset ? (buf_offset - patch_offset) : 0);
    u64 patch_remaining_size = (patch_size - patch_block_offset);

    u64 buf_block_offset = (buf_offset < patch_offset ? (patch_offset - buf_offset) : 0);
    u64 buf_remaining_size = (buf_size - buf_block_offset);

    u64 buf_block_size = (buf_remaining_size < patch_remaining_size ? buf_remaining_size : patch_remaining_size);

    memcpy((u8*)buf + buf_block_offset, (const u8*)patch + patch_block_offset, buf_block_size);

    LOG_MSG_INFO("Overwrote 0x%lX bytes block at offset 0x%lX from raw %s NCA \"%s\" buffer (size 0x%lX, NCA offset 0x%lX).", buf_block_size, buf_block_offset, titleGetNcmContentTypeName(ctx->content_type), \
                 ctx->content_id_str, buf_size, buf_offset);

    return ((patch_block_offset + buf_block_size) == patch_size);
}

/// Returns a pointer to a dynamically allocated buffer used to encrypt the input plaintext data, based on the encryption type used by the input NCA FS section, as well as its offset and size.
/// Input offset must be relative to the start of the NCA FS section.
/// Output size and offset are guaranteed to be aligned to the AES sector size used by the encryption type from the FS section.
/// Output offset is relative to the start of the NCA content file, making it easier to use the output encrypted block to seamlessly replace data while dumping a NCA.
/// This function doesn't support Patch RomFS sections, nor sections with Sparse and/or Compressed storage.
static void *ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset)
{
    u8 *out = NULL;
    bool success = false;

    if (!g_ncaCryptoBuffer || !ctx || !ctx->enabled || ctx->has_sparse_layer || ctx->has_compression_layer || !ctx->nca_ctx || ctx->section_idx >= NCA_FS_HEADER_COUNT || \
        ctx->section_offset < sizeof(NcaHeader) || ctx->hash_type <= NcaHashType_None || ctx->hash_type == NcaHashType_AutoSha3 || ctx->hash_type > NcaHashType_HierarchicalIntegritySha3 || \
        ctx->encryption_type == NcaEncryptionType_Auto || ctx->encryption_type == NcaEncryptionType_AesCtrEx || ctx->encryption_type >= NcaEncryptionType_AesCtrExSkipLayerHash || \
        ctx->section_type >= NcaFsSectionType_Invalid || !data || !data_size || (data_offset + data_size) > ctx->section_size || !out_block_size || !out_block_offset)
    {
        LOG_MSG_ERROR("Invalid NCA FS section header parameters!");
        goto end;
    }

    size_t crypt_res = 0;
    u64 sector_num = 0;

    NcaContext *nca_ctx = (NcaContext*)ctx->nca_ctx;
    u64 content_offset = (ctx->section_offset + data_offset);

    u64 block_start_offset = 0, block_end_offset = 0, block_size = 0;
    u64 plain_chunk_offset = 0;

    if (!*(nca_ctx->content_id_str) || (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        (nca_ctx->format_version != NcaVersion_Nca0 && nca_ctx->format_version != NcaVersion_Nca2 && nca_ctx->format_version != NcaVersion_Nca3) || (content_offset + data_size) > nca_ctx->content_size)
    {
        LOG_MSG_ERROR("Invalid NCA header parameters!");
        goto end;
    }

    /* Optimization for blocks from plaintext FS sections or blocks that are aligned to the AES-CTR / AES-XTS sector size. */
    if (ctx->encryption_type == NcaEncryptionType_None || \
        (ctx->encryption_type == NcaEncryptionType_AesXts && !(content_offset % NCA_AES_XTS_SECTOR_SIZE) && !(data_size % NCA_AES_XTS_SECTOR_SIZE)) || \
        ((ctx->encryption_type == NcaEncryptionType_AesCtr || ctx->encryption_type == NcaEncryptionType_AesCtrSkipLayerHash) && !(content_offset % AES_BLOCK_SIZE) && !(data_size % AES_BLOCK_SIZE)))
    {
        /* Allocate memory. */
        out = malloc(data_size);
        if (!out)
        {
            LOG_MSG_ERROR("Unable to allocate 0x%lX bytes buffer! (aligned).", data_size);
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
                LOG_MSG_ERROR("Failed to AES-XTS encrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", data_size, content_offset, nca_ctx->content_id_str, ctx->section_idx);
                goto end;
            }
        } else
        if (ctx->encryption_type == NcaEncryptionType_AesCtr || ctx->encryption_type == NcaEncryptionType_AesCtrSkipLayerHash)
        {
            aes128CtrUpdatePartialCtr(ctx->ctr, content_offset);
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
        LOG_MSG_ERROR("Unable to allocate 0x%lX bytes buffer! (unaligned).", block_size);
        goto end;
    }

    /* Read decrypted data using aligned offset and size. */
    if (!_ncaReadFsSection(ctx, out, block_size, block_start_offset))
    {
        LOG_MSG_ERROR("Failed to read decrypted NCA \"%s\" FS section #%u data block!", nca_ctx->content_id_str, ctx->section_idx);
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
            LOG_MSG_ERROR("Failed to AES-XTS encrypt 0x%lX bytes data block at offset 0x%lX from NCA \"%s\" FS section #%u! (aligned).", block_size, content_offset, nca_ctx->content_id_str, ctx->section_idx);
            goto end;
        }
    } else
    if (ctx->encryption_type == NcaEncryptionType_AesCtr || ctx->encryption_type == NcaEncryptionType_AesCtrSkipLayerHash)
    {
        aes128CtrUpdatePartialCtr(ctx->ctr, content_offset);
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

    return out;
}
