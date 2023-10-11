/*
 * nca_storage.c
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include "nca_storage.h"

/* Function prototypes. */

static bool ncaStorageInitializeBucketTreeContext(BucketTreeContext **out, NcaFsSectionContext *nca_fs_ctx, u8 storage_type);
static bool ncaStorageSetPatchOriginalSubStorage(NcaStorageContext *patch_ctx, NcaFsSectionContext *patch_nca_fs_ctx, NcaStorageContext *base_ctx);
static bool ncaStorageInitializeCompressedStorageBucketTreeContext(NcaStorageContext *out, NcaFsSectionContext *nca_fs_ctx);

bool ncaStorageInitializeContext(NcaStorageContext *out, NcaFsSectionContext *nca_fs_ctx, NcaStorageContext *base_ctx)
{
    if (!out || !nca_fs_ctx || !nca_fs_ctx->enabled || (nca_fs_ctx->section_type == NcaFsSectionType_PatchRomFs && \
        (!nca_fs_ctx->has_patch_indirect_layer || !nca_fs_ctx->has_patch_aes_ctr_ex_layer || nca_fs_ctx->has_sparse_layer || !base_ctx)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = false;

    /* Free output context beforehand. */
    ncaStorageFreeContext(out);

    /* Set initial base storage type. */
    out->base_storage_type = NcaStorageBaseStorageType_Regular;

    /* Check if a sparse layer is available. */
    if (nca_fs_ctx->has_sparse_layer)
    {
        /* Initialize sparse layer. */
        if (!ncaStorageInitializeBucketTreeContext(&(out->sparse_storage), nca_fs_ctx, BucketTreeStorageType_Sparse)) goto end;

        /* Set sparse layer's substorage. */
        if (!bktrSetRegularSubStorage(out->sparse_storage, nca_fs_ctx)) goto end;

        /* Update base storage type. */
        out->base_storage_type = NcaStorageBaseStorageType_Sparse;
    }

    /* Check if both Indirect and AesCtrEx layers are available. */
    if (nca_fs_ctx->section_type == NcaFsSectionType_PatchRomFs)
    {
        /* Initialize AesCtrEx and Indirect layers. */
        if (!ncaStorageInitializeBucketTreeContext(&(out->aes_ctr_ex_storage), nca_fs_ctx, BucketTreeStorageType_AesCtrEx) || \
            !ncaStorageInitializeBucketTreeContext(&(out->indirect_storage), nca_fs_ctx, BucketTreeStorageType_Indirect)) goto end;

        /* Set AesCtrEx layer's substorage (plain NCA reads). */
        if (!bktrSetRegularSubStorage(out->aes_ctr_ex_storage, nca_fs_ctx)) goto end;

        /* Set Indirect layer's substorages (Base + AesCtrEx). */
        if (!ncaStorageSetPatchOriginalSubStorage(out, nca_fs_ctx, base_ctx)) goto end;
        if (!bktrSetBucketTreeSubStorage(out->indirect_storage, out->aes_ctr_ex_storage, 1)) goto end;

        /* Update base storage type. */
        out->base_storage_type = NcaStorageBaseStorageType_Indirect;
    }

    /* Initialize compression layer if it's available, but only if we're also not dealing with a sparse layer. */
    if (nca_fs_ctx->has_compression_layer && !nca_fs_ctx->has_sparse_layer && !ncaStorageInitializeCompressedStorageBucketTreeContext(out, nca_fs_ctx)) goto end;

    /* Update output context. */
    out->nca_fs_ctx = nca_fs_ctx;

    /* Update return value. */
    success = true;

end:
    if (!success) ncaStorageFreeContext(out);

    return success;
}

bool ncaStorageGetHashTargetExtents(NcaStorageContext *ctx, u64 *out_offset, u64 *out_size)
{
    if (!ncaStorageIsValidContext(ctx) || (!out_offset && !out_size))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = false;

    /* Set proper hash target extents. */
    switch(ctx->base_storage_type)
    {
        case NcaStorageBaseStorageType_Regular:
        case NcaStorageBaseStorageType_Sparse:
        case NcaStorageBaseStorageType_Indirect:
        {
            u64 hash_target_offset = 0, hash_target_size = 0;

            /* Get hash target extents from the NCA FS section. */
            if (!ncaGetFsSectionHashTargetExtents(ctx->nca_fs_ctx, &hash_target_offset, &hash_target_size))
            {
                LOG_MSG_ERROR("Failed to retrieve NCA FS section's hash target extents!");
                goto end;
            }

            /* Regular: just provide the NCA FS section hash target extents -- they already represent physical information. */
            /* Sparse/Indirect: the base storage's virtual section encompasses the hash layers, too. The NCA FS section hash target extents represent valid virtual information. */
            if (out_offset) *out_offset = hash_target_offset;
            if (out_size) *out_size = hash_target_size;
            break;
        }
        case NcaStorageBaseStorageType_Compressed:
        {
            /* Compressed sections already point to the hash target layer. */
            if (out_offset) *out_offset = ctx->compressed_storage->start_offset;
            if (out_size) *out_size = ctx->compressed_storage->end_offset;
            break;
        }
        default:
            break;
    }

    /* Update return value. */
    success = true;

end:
    return success;
}

bool ncaStorageRead(NcaStorageContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ncaStorageIsValidContext(ctx) || !out || !read_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = false;

    switch(ctx->base_storage_type)
    {
        case NcaStorageBaseStorageType_Regular:
            success = ncaReadFsSection(ctx->nca_fs_ctx, out, read_size, offset);
            break;
        case NcaStorageBaseStorageType_Sparse:
            success = bktrReadStorage(ctx->sparse_storage, out, read_size, offset);
            break;
        case NcaStorageBaseStorageType_Indirect:
            success = bktrReadStorage(ctx->indirect_storage, out, read_size, offset);
            break;
        case NcaStorageBaseStorageType_Compressed:
            success = bktrReadStorage(ctx->compressed_storage, out, read_size, offset);
            break;
        default:
            break;
    }

    if (!success) LOG_MSG_ERROR("Failed to read 0x%lX-byte long block from offset 0x%lX in base storage! (type: %u).", read_size, offset, ctx->base_storage_type);

    return success;
}

bool ncaStorageIsBlockWithinPatchStorageRange(NcaStorageContext *ctx, u64 offset, u64 size, bool *out)
{
    if (!ncaStorageIsValidContext(ctx) || ctx->nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || (ctx->base_storage_type != NcaStorageBaseStorageType_Indirect && \
        ctx->base_storage_type != NcaStorageBaseStorageType_Compressed) || (ctx->base_storage_type == NcaStorageBaseStorageType_Indirect && !ctx->indirect_storage) || \
        (ctx->base_storage_type == NcaStorageBaseStorageType_Compressed && !ctx->compressed_storage))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Get base storage. */
    BucketTreeContext *bktr_ctx = (ctx->base_storage_type == NcaStorageBaseStorageType_Indirect ? ctx->indirect_storage : ctx->compressed_storage);

    /* Check if the provided block extents are within the Indirect Storage's range. */
    bool success = bktrIsBlockWithinIndirectStorageRange(bktr_ctx, offset, size, out);
    if (!success) LOG_MSG_ERROR("Failed to determine if block extents are within the Indirect Storage's range!");

    return success;
}

void ncaStorageFreeContext(NcaStorageContext *ctx)
{
    if (!ctx) return;

    if (ctx->sparse_storage)
    {
        bktrFreeContext(ctx->sparse_storage);
        free(ctx->sparse_storage);
    }

    if (ctx->aes_ctr_ex_storage)
    {
        bktrFreeContext(ctx->aes_ctr_ex_storage);
        free(ctx->aes_ctr_ex_storage);
    }

    if (ctx->indirect_storage)
    {
        bktrFreeContext(ctx->indirect_storage);
        free(ctx->indirect_storage);
    }

    if (ctx->compressed_storage)
    {
        bktrFreeContext(ctx->compressed_storage);
        free(ctx->compressed_storage);
    }

    memset(ctx, 0, sizeof(NcaStorageContext));
}

static bool ncaStorageInitializeBucketTreeContext(BucketTreeContext **out, NcaFsSectionContext *nca_fs_ctx, u8 storage_type)
{
    if (!out || !nca_fs_ctx || storage_type >= BucketTreeStorageType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeContext *bktr_ctx = NULL;
    bool success = false;

    /* Allocate memory for the Bucket Tree context. */
    bktr_ctx = calloc(1, sizeof(BucketTreeContext));
    if (!bktr_ctx)
    {
        LOG_MSG_ERROR("Unable to allocate memory for Bucket Tree context! (%u).", storage_type);
        goto end;
    }

    /* Initialize Bucket Tree context. */
    success = bktrInitializeContext(bktr_ctx, nca_fs_ctx, storage_type);
    if (!success)
    {
        LOG_MSG_ERROR("Failed to initialize Bucket Tree context! (%u).", storage_type);
        goto end;
    }

    /* Update output context pointer. */
    *out = bktr_ctx;

end:
    if (!success && bktr_ctx) free(bktr_ctx);

    return success;
}

static bool ncaStorageSetPatchOriginalSubStorage(NcaStorageContext *patch_ctx, NcaFsSectionContext *patch_nca_fs_ctx, NcaStorageContext *base_ctx)
{
    NcaContext *patch_nca_ctx = NULL, *base_nca_ctx = NULL;

    if (!patch_ctx || !patch_ctx->indirect_storage || !patch_ctx->aes_ctr_ex_storage || !patch_nca_fs_ctx || !ncaStorageIsValidContext(base_ctx) || \
        !(patch_nca_ctx = patch_nca_fs_ctx->nca_ctx) || !(base_nca_ctx = base_ctx->nca_fs_ctx->nca_ctx) || \
        patch_nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || base_ctx->nca_fs_ctx->section_type != NcaFsSectionType_RomFs || \
        patch_nca_ctx->header.program_id != base_nca_ctx->header.program_id || patch_nca_ctx->header.content_type != base_nca_ctx->header.content_type || \
        patch_nca_ctx->id_offset != base_nca_ctx->id_offset || patch_nca_ctx->title_version.value < base_nca_ctx->title_version.value)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = false;

    /* Set original substorage. */
    switch(base_ctx->base_storage_type)
    {
        case NcaStorageBaseStorageType_Regular:
        case NcaStorageBaseStorageType_Compressed:
            /* Regular: we just make the Patch's Indirect Storage's SubStorage #0 point to the Base NCA FS section as-is and call it a day. */

            /* Compressed: if a Compressed Storage is available in the Base NCA FS section, the corresponding Patch NCA FS section *must* also have one. */
            /* This is because the Patch's Compressed Storage also takes care of LZ4-compressed chunks within Base NCA FS section areas. */
            /* Furthermore, the Patch's Indirect Storage already provides section-relative physical offsets for the Base NCA FS section. */
            /* In other words, we don't need to parse the Base NCA's Compressed Storage on every read. */
            success = bktrSetRegularSubStorage(patch_ctx->indirect_storage, base_ctx->nca_fs_ctx);
            break;
        case NcaStorageBaseStorageType_Sparse:
            /* Sparse: we should *always* arrive here if a Sparse Storage is available in the Base NCA FS section, regardless if a Compressed Storage is available or not. */
            /* This is because compression bucket trees are non-existent in Base NCA FS sections that have both Sparse and Compressed Storages. */
            /* Furthermore, in these cases, the compression BucketInfo from the NCA FS section header references the full, patched FS section, so we can't really use it. */
            /* We just completely ignore the Base's Compressed Storage and let the Patch's Compressed Storage take care of LZ4-compressed chunks. */
            /* Anyway, we just make the Patch's Indirect Storage's SubStorage #0 point to the Base's Sparse Storage and call it a day. */
            success = bktrSetBucketTreeSubStorage(patch_ctx->indirect_storage, base_ctx->sparse_storage, 0);
            break;
        default:
            break;
    }

    if (!success) LOG_MSG_ERROR("Failed to set base storage to patch storage! (0x%02X, 0x%02X).", base_ctx->base_storage_type, patch_ctx->base_storage_type);

    return success;
}

static bool ncaStorageInitializeCompressedStorageBucketTreeContext(NcaStorageContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    if (!out || out->base_storage_type < NcaStorageBaseStorageType_Regular || out->base_storage_type > NcaStorageBaseStorageType_Indirect || !nca_fs_ctx || \
        !nca_fs_ctx->has_compression_layer || (out->base_storage_type == NcaStorageBaseStorageType_Sparse && !out->sparse_storage) || \
        (out->base_storage_type == NcaStorageBaseStorageType_Indirect && !out->indirect_storage))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeContext *bktr_ctx = NULL;
    BucketTreeSubStorage bktr_substorage = {0};
    bool success = false;

    /* Allocate memory for the Bucket Tree context. */
    bktr_ctx = calloc(1, sizeof(BucketTreeContext));
    if (!bktr_ctx)
    {
        LOG_MSG_ERROR("Unable to allocate memory for Bucket Tree context!");
        goto end;
    }

    /* Prepare compression layer's substorage. */
    bktr_substorage.index = 0;
    bktr_substorage.nca_fs_ctx = nca_fs_ctx;

    switch(out->base_storage_type)
    {
        case NcaStorageBaseStorageType_Regular:
            bktr_substorage.type = BucketTreeSubStorageType_Regular;
            bktr_substorage.bktr_ctx = NULL;
            break;
        case NcaStorageBaseStorageType_Sparse:
            bktr_substorage.type = BucketTreeSubStorageType_Sparse;
            bktr_substorage.bktr_ctx = out->sparse_storage;
            break;
        case NcaStorageBaseStorageType_Indirect:
            bktr_substorage.type = BucketTreeSubStorageType_Indirect;
            bktr_substorage.bktr_ctx = out->indirect_storage;
            break;
        default:
            break;
    }

    /* Initialize Bucket Tree context. */
    success = bktrInitializeCompressedStorageContext(bktr_ctx, &bktr_substorage);
    if (!success)
    {
        LOG_MSG_ERROR("Failed to initialize Bucket Tree context!");
        goto end;
    }

    /* Update output context. */
    out->compressed_storage = bktr_ctx;
    out->base_storage_type = NcaStorageBaseStorageType_Compressed;

end:
    if (!success && bktr_ctx) free(bktr_ctx);

    return success;
}
