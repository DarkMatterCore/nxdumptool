/*
 * bktr.c
 *
 * Copyright (c) 2018-2020, SciresM.
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
#include "bktr.h"
#include "aes.h"

/* Function prototypes. */

static bool bktrInitializeIndirectStorage(NcaFsSectionContext *nca_fs_ctx, bool patch_indirect_bucket, BktrIndirectStorageBlock **out_indirect_block);
static bool bktrInitializeAesCtrExStorage(NcaFsSectionContext *nca_fs_ctx, BktrAesCtrExStorageBlock **out_aes_ctr_ex_block);

static bool bktrPhysicalSectionRead(BktrContext *ctx, void *out, u64 read_size, u64 offset);
static bool bktrAesCtrExStorageRead(BktrContext *ctx, void *out, u64 read_size, u64 virtual_offset, u64 section_offset);
static bool bktrPhysicalSectionSparseRead(BktrContext *ctx, void *out, u64 read_size, u64 offset);

NX_INLINE BktrIndirectStorageBucket *bktrGetIndirectStorageBucket(BktrIndirectStorageBlock *block, u32 bucket_num);
static BktrIndirectStorageEntry *bktrGetIndirectStorageEntry(BktrIndirectStorageBlock *block, u64 offset);

NX_INLINE BktrAesCtrExStorageBucket *bktrGetAesCtrExStorageBucket(BktrAesCtrExStorageBlock *block, u32 bucket_num);
static BktrAesCtrExStorageEntry *bktrGetAesCtrExStorageEntry(BktrAesCtrExStorageBlock *block, u64 offset);

bool bktrInitializeContext(BktrContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *update_nca_fs_ctx)
{
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    bool success = false, dump_patch_romfs_header = false;
    
    if (!out || !base_nca_fs_ctx || !(base_nca_ctx = (NcaContext*)base_nca_fs_ctx->nca_ctx) || \
        !update_nca_fs_ctx || !update_nca_fs_ctx->enabled || !(update_nca_ctx = (NcaContext*)update_nca_fs_ctx->nca_ctx) || \
        update_nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || base_nca_ctx->header.program_id != update_nca_ctx->header.program_id || \
        base_nca_ctx->header.content_type != update_nca_ctx->header.content_type || base_nca_ctx->id_offset != update_nca_ctx->id_offset || \
        base_nca_ctx->title_version > update_nca_ctx->title_version || \
        __builtin_bswap32(update_nca_fs_ctx->header.patch_info.indirect_bucket.header.magic) != NCA_BKTR_MAGIC || \
        update_nca_fs_ctx->header.patch_info.indirect_bucket.header.version != NCA_BKTR_VERSION || \
        __builtin_bswap32(update_nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket.header.magic) != NCA_BKTR_MAGIC || \
        update_nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket.header.version != NCA_BKTR_VERSION || \
        (update_nca_fs_ctx->header.patch_info.indirect_bucket.offset + update_nca_fs_ctx->header.patch_info.indirect_bucket.size) != update_nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket.offset || \
        (update_nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket.offset + update_nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket.size) != update_nca_fs_ctx->section_size || \
        (base_nca_ctx->rights_id_available && !base_nca_ctx->titlekey_retrieved) || (update_nca_ctx->rights_id_available && !update_nca_ctx->titlekey_retrieved))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Free output context beforehand. */
    bktrFreeContext(out);
    
    /* Update missing base NCA RomFS status. */
    out->missing_base_romfs = (!base_nca_fs_ctx->enabled || base_nca_fs_ctx->section_type != NcaFsSectionType_RomFs);
    if (!out->missing_base_romfs)
    {
        if (!base_nca_fs_ctx->has_sparse_layer)
        {
            /* Initialize base NCA RomFS context. */
            if (!romfsInitializeContext(&(out->base_romfs_ctx), base_nca_fs_ctx))
            {
                LOG_MSG("Failed to initialize base NCA RomFS context!");
                return false;
            }
        } else {
            /* Initialize the sparse layer's indirect storage. Don't lose time trying to initialize a RomFS context for an incomplete storage. */
            if (!bktrInitializeIndirectStorage(base_nca_fs_ctx, false, &(out->base_indirect_block))) return false;
            
            /* Update the NCA FS section context from the base RomFS context. */
            out->base_romfs_ctx.nca_fs_ctx = base_nca_fs_ctx;
        }
    }
    
    /* Initialize Patch RomFS indirect storage. */
    if (!bktrInitializeIndirectStorage(update_nca_fs_ctx, true, &(out->indirect_block))) goto end;
    
    /* Initialize Patch RomFS AesCtrEx storage. */
    if (!bktrInitializeAesCtrExStorage(update_nca_fs_ctx, &(out->aes_ctr_ex_block))) goto end;
    
    /* Initialize Patch RomFS context. */
    /* Don't verify offsets from Patch RomFS sections, because they reflect the full, patched RomFS image. */
    out->patch_romfs_ctx.nca_fs_ctx = update_nca_fs_ctx;
    
    if (!ncaGetFsSectionHashTargetProperties(update_nca_fs_ctx, &(out->offset), &(out->size)))
    {
        LOG_MSG("Failed to get target hash layer properties!");
        goto end;
    }
    
    out->patch_romfs_ctx.offset = out->offset;
    out->patch_romfs_ctx.size = out->size;
    
    /* Read Patch RomFS header. */
    if (!bktrPhysicalSectionRead(out, &(out->patch_romfs_ctx.header), sizeof(RomFileSystemHeader), out->patch_romfs_ctx.offset))
    {
        LOG_MSG("Failed to read update NCA RomFS header!");
        goto end;
    }
    
    if (out->patch_romfs_ctx.header.cur_format.header_size != ROMFS_HEADER_SIZE)
    {
        LOG_MSG("Invalid update NCA RomFS header size!");
        dump_patch_romfs_header = true;
        goto end;
    }
    
    /* Read Patch RomFS directory entries table. */
    u64 dir_table_offset = out->patch_romfs_ctx.header.cur_format.directory_entry_offset;
    out->patch_romfs_ctx.dir_table_size = out->patch_romfs_ctx.header.cur_format.directory_entry_size;
    
    if (!dir_table_offset || !out->patch_romfs_ctx.dir_table_size)
    {
        LOG_MSG("Invalid update NCA RomFS directory entries table!");
        dump_patch_romfs_header = true;
        goto end;
    }
    
    out->patch_romfs_ctx.dir_table = malloc(out->patch_romfs_ctx.dir_table_size);
    if (!out->patch_romfs_ctx.dir_table)
    {
        LOG_MSG("Unable to allocate memory for the update NCA RomFS directory entries table!");
        goto end;
    }
    
    if (!bktrPhysicalSectionRead(out, out->patch_romfs_ctx.dir_table, out->patch_romfs_ctx.dir_table_size, out->patch_romfs_ctx.offset + dir_table_offset))
    {
        LOG_MSG("Failed to read update NCA RomFS directory entries table!");
        goto end;
    }
    
    /* Read Patch RomFS file entries table. */
    u64 file_table_offset = out->patch_romfs_ctx.header.cur_format.file_entry_offset;
    out->patch_romfs_ctx.file_table_size = out->patch_romfs_ctx.header.cur_format.file_entry_size;
    
    if (!file_table_offset || !out->patch_romfs_ctx.file_table_size)
    {
        LOG_MSG("Invalid update NCA RomFS file entries table!");
        dump_patch_romfs_header = true;
        goto end;
    }
    
    out->patch_romfs_ctx.file_table = malloc(out->patch_romfs_ctx.file_table_size);
    if (!out->patch_romfs_ctx.file_table)
    {
        LOG_MSG("Unable to allocate memory for the update NCA RomFS file entries table!");
        goto end;
    }
    
    if (!bktrPhysicalSectionRead(out, out->patch_romfs_ctx.file_table, out->patch_romfs_ctx.file_table_size, out->patch_romfs_ctx.offset + file_table_offset))
    {
        LOG_MSG("Failed to read update NCA RomFS file entries table!");
        goto end;
    }
    
    /* Get Patch RomFS file data body offset. */
    out->patch_romfs_ctx.body_offset = out->body_offset = out->patch_romfs_ctx.header.cur_format.body_offset;
    
    success = true;
    
end:
    if (!success)
    {
        if (dump_patch_romfs_header) LOG_DATA(&(out->patch_romfs_ctx.header), sizeof(RomFileSystemHeader), "Update RomFS header dump:");
        bktrFreeContext(out);
    }
    
    return success;
}

bool bktrReadFileSystemData(BktrContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->size || !out || !read_size || (offset + read_size) > ctx->size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Read filesystem data. */
    if (!bktrPhysicalSectionRead(ctx, out, read_size, ctx->offset + offset))
    {
        LOG_MSG("Failed to read Patch RomFS data!");
        return false;
    }
    
    return true;
}

bool bktrReadFileEntryData(BktrContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->body_offset || !file_entry || !file_entry->size || (file_entry->offset + file_entry->size) > ctx->size || !out || !read_size || (offset + read_size) > file_entry->size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Read entry data. */
    if (!bktrReadFileSystemData(ctx, out, read_size, ctx->body_offset + file_entry->offset + offset))
    {
        LOG_MSG("Failed to read Patch RomFS file entry data!");
        return false;
    }
    
    return true;
}

bool bktrIsFileEntryUpdated(BktrContext *ctx, RomFileSystemFileEntry *file_entry, bool *out)
{
    if (!ctx || !ctx->body_offset || !ctx->indirect_block || !file_entry || !file_entry->size || (file_entry->offset + file_entry->size) > ctx->size || !out)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    bool updated = false;
    u64 file_offset = (ctx->offset + ctx->body_offset + file_entry->offset);
    BktrIndirectStorageEntry *indirect_entry = NULL, *last_indirect_entry = NULL;
    
    indirect_entry = bktrGetIndirectStorageEntry(ctx->indirect_block, file_offset);
    if (!indirect_entry)
    {
        LOG_MSG("Error retrieving BKTR Indirect Storage Entry at offset 0x%lX!", file_offset);
        return false;
    }
    
    last_indirect_entry = indirect_entry;
    
    while(last_indirect_entry->virtual_offset < (file_offset + file_entry->size)) last_indirect_entry++;
    
    while(indirect_entry < last_indirect_entry)
    {
        if (indirect_entry->indirect_storage_index == BktrIndirectStorageIndex_Patch)
        {
            updated = true;
            break;
        }
        
        indirect_entry++;
    }
    
    *out = updated;
    return true;
}

static bool bktrInitializeIndirectStorage(NcaFsSectionContext *nca_fs_ctx, bool patch_indirect_bucket, BktrIndirectStorageBlock **out_indirect_block)
{
    if (!nca_fs_ctx || (patch_indirect_bucket && nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs) || \
        (!patch_indirect_bucket && (nca_fs_ctx->section_type != NcaFsSectionType_RomFs || !nca_fs_ctx->has_sparse_layer)) || !out_indirect_block)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    NcaBucketInfo *indirect_bucket = (patch_indirect_bucket ? &(nca_fs_ctx->header.patch_info.indirect_bucket) : &(nca_fs_ctx->header.sparse_info.bucket));
    BktrIndirectStorageBlock *indirect_block = NULL;
    bool success = false;
    
    /* Allocate space for an extra (fake) indirect storage entry, to simplify our logic. */
    indirect_block = calloc(1, indirect_bucket->size + ((0x3FF0 / sizeof(u64)) * sizeof(BktrIndirectStorageEntry)));
    if (!indirect_block)
    {
        LOG_MSG("Unable to allocate memory for the BKTR Indirect Storage Block! (%s).", patch_indirect_bucket ? "patch" : "sparse");
        goto end;
    }
    
    /* Read indirect storage block data. */
    if ((patch_indirect_bucket && !ncaReadFsSection(nca_fs_ctx, indirect_block, indirect_bucket->size, indirect_bucket->offset)) || \
        (!patch_indirect_bucket && !ncaReadContentFile((NcaContext*)nca_fs_ctx->nca_ctx, indirect_block, nca_fs_ctx->sparse_table_size, nca_fs_ctx->sparse_table_offset)))
    {
        LOG_MSG("Failed to read BKTR Indirect Storage Block data! (%s).", patch_indirect_bucket ? "patch" : "sparse");
        goto end;
    }
    
    /* Decrypt indirect block, if needed. */
    if (!patch_indirect_bucket) aes128CtrCrypt(&(nca_fs_ctx->sparse_ctr_ctx), indirect_block, indirect_block, nca_fs_ctx->sparse_table_size);
    
    /* This simplifies logic greatly... */
    for(u32 i = (indirect_block->bucket_count - 1); i > 0; i--)
    {
        BktrIndirectStorageBucket tmp_bucket = {0};
        memcpy(&tmp_bucket, &(indirect_block->indirect_storage_buckets[i]), sizeof(BktrIndirectStorageBucket));
        memcpy(bktrGetIndirectStorageBucket(indirect_block, i), &tmp_bucket, sizeof(BktrIndirectStorageBucket));
    }
    
    for(u32 i = 0; (i + 1) < indirect_block->bucket_count; i++)
    {
        BktrIndirectStorageBucket *cur_bucket = bktrGetIndirectStorageBucket(indirect_block, i);
        cur_bucket->indirect_storage_entries[cur_bucket->entry_count].virtual_offset = indirect_block->virtual_offsets[i + 1];
    }
    
    BktrIndirectStorageBucket *last_indirect_bucket = bktrGetIndirectStorageBucket(indirect_block, indirect_block->bucket_count - 1);
    last_indirect_bucket->indirect_storage_entries[last_indirect_bucket->entry_count].virtual_offset = indirect_block->virtual_size;
    
    /* Update output values. */
    *out_indirect_block = indirect_block;
    success = true;
    
end:
    if (!success && indirect_block) free(indirect_block);
    
    return success;
}

static bool bktrInitializeAesCtrExStorage(NcaFsSectionContext *nca_fs_ctx, BktrAesCtrExStorageBlock **out_aes_ctr_ex_block)
{
    if (!nca_fs_ctx || nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || !out_aes_ctr_ex_block)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    NcaBucketInfo *aes_ctr_ex_bucket = &(nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket);
    BktrAesCtrExStorageBlock *aes_ctr_ex_block = NULL;
    bool success = false;
    
    /* Allocate space for an extra (fake) AesCtrEx storage entry, to simplify our logic. */
    aes_ctr_ex_block = calloc(1, aes_ctr_ex_bucket->size + (((0x3FF0 / sizeof(u64)) + 1) * sizeof(BktrAesCtrExStorageEntry)));
    if (!aes_ctr_ex_block)
    {
        LOG_MSG("Unable to allocate memory for the BKTR AesCtrEx Storage Block!");
        goto end;
    }
    
    /* Read AesCtrEx storage block data. */
    if (!ncaReadFsSection(nca_fs_ctx, aes_ctr_ex_block, aes_ctr_ex_bucket->size, aes_ctr_ex_bucket->offset))
    {
        LOG_MSG("Failed to read BKTR AesCtrEx Storage Block data!");
        goto end;
    }
    
    if (aes_ctr_ex_block->physical_size != aes_ctr_ex_bucket->offset)
    {
        LOG_DATA(aes_ctr_ex_block, aes_ctr_ex_bucket->size, "Invalid BKTR AesCtrEx Storage Block size! AesCtrEx Storage Block dump:");
        goto end;
    }
    
    /* This simplifies logic greatly... */
    for(u32 i = (aes_ctr_ex_block->bucket_count - 1); i > 0; i--)
    {
        BktrAesCtrExStorageBucket tmp_bucket = {0};
        memcpy(&tmp_bucket, &(aes_ctr_ex_block->aes_ctr_ex_storage_buckets[i]), sizeof(BktrAesCtrExStorageBucket));
        memcpy(bktrGetAesCtrExStorageBucket(aes_ctr_ex_block, i), &tmp_bucket, sizeof(BktrAesCtrExStorageBucket));
    }
    
    for(u32 i = 0; (i + 1) < aes_ctr_ex_block->bucket_count; i++)
    {
        BktrAesCtrExStorageBucket *cur_bucket = bktrGetAesCtrExStorageBucket(aes_ctr_ex_block, i);
        BktrAesCtrExStorageBucket *next_bucket = bktrGetAesCtrExStorageBucket(aes_ctr_ex_block, i + 1);
        cur_bucket->aes_ctr_ex_storage_entries[cur_bucket->entry_count].offset = next_bucket->aes_ctr_ex_storage_entries[0].offset;
        cur_bucket->aes_ctr_ex_storage_entries[cur_bucket->entry_count].generation = next_bucket->aes_ctr_ex_storage_entries[0].generation;
    }
    
    BktrAesCtrExStorageBucket *last_aes_ctr_ex_bucket = bktrGetAesCtrExStorageBucket(aes_ctr_ex_block, aes_ctr_ex_block->bucket_count - 1);
    
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count].offset = nca_fs_ctx->header.patch_info.indirect_bucket.offset;
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count].generation = nca_fs_ctx->header.aes_ctr_upper_iv.generation;
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count + 1].offset = nca_fs_ctx->section_size;
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count + 1].generation = 0;
    
    /* Update output values. */
    *out_aes_ctr_ex_block = aes_ctr_ex_block;
    success = true;
    
end:
    if (!success && aes_ctr_ex_block) free(aes_ctr_ex_block);
    
    return success;
}

static bool bktrPhysicalSectionRead(BktrContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || (!ctx->missing_base_romfs && ((!ctx->base_romfs_ctx.nca_fs_ctx->has_sparse_layer && !ctx->base_romfs_ctx.nca_fs_ctx) || \
        (ctx->base_romfs_ctx.nca_fs_ctx->has_sparse_layer && !ctx->base_indirect_block))) || !ctx->indirect_block || !out || !read_size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    BktrIndirectStorageEntry *indirect_entry = NULL, *next_indirect_entry = NULL;
    u64 section_offset = 0, indirect_block_size = 0;
    
    /* Determine which FS section to use + the actual offset to start reading from. */
    /* There's no better way to do this than making all BKTR addresses virtual. */
    indirect_entry = bktrGetIndirectStorageEntry(ctx->indirect_block, offset);
    if (!indirect_entry)
    {
        LOG_MSG("Error retrieving BKTR Indirect Storage Entry at offset 0x%lX!", offset);
        return false;
    }
    
    next_indirect_entry = (indirect_entry + 1);
    section_offset = (offset - indirect_entry->virtual_offset + indirect_entry->physical_offset);
    
    /* Perform read operation. */
    bool success = false;
    if ((offset + read_size) <= next_indirect_entry->virtual_offset)
    {
        /* Read only within the current indirect storage entry. */
        if (indirect_entry->indirect_storage_index == BktrIndirectStorageIndex_Patch)
        {
            /* Retrieve data from the Patch RomFS' AesCtrEx storage. */
            success = bktrAesCtrExStorageRead(ctx, out, read_size, offset, section_offset);
            if (!success) LOG_MSG("Failed to read 0x%lX bytes block from BKTR AesCtrEx storage at offset 0x%lX!", read_size, section_offset);
        } else
        if (!ctx->missing_base_romfs)
        {
            if (!ctx->base_romfs_ctx.nca_fs_ctx->has_sparse_layer)
            {
                /* Retrieve data from the base NCA. */
                success = ncaReadFsSection(ctx->base_romfs_ctx.nca_fs_ctx, out, read_size, section_offset);
                if (!success) LOG_MSG("Failed to read 0x%lX bytes block from base RomFS at offset 0x%lX!", read_size, section_offset);
            } else {
                /* Retrieve data from the sparse layer in the base NCA. */
                success = bktrPhysicalSectionSparseRead(ctx, out, read_size, section_offset);
                if (!success) LOG_MSG("Failed to read 0x%lX bytes block from sparse layer at offset 0x%lX!", read_size, section_offset);
            }
        } else {
            LOG_MSG("Attempting to read 0x%lX bytes block from non-existent base RomFS at offset 0x%lX!", read_size, section_offset);
        }
    } else {
        /* Handle reads that span multiple indirect storage entries. */
        indirect_block_size = (next_indirect_entry->virtual_offset - offset);
        
        success = (bktrPhysicalSectionRead(ctx, out, indirect_block_size, offset) && \
                   bktrPhysicalSectionRead(ctx, (u8*)out + indirect_block_size, read_size - indirect_block_size, offset + indirect_block_size));
        if (!success) LOG_MSG("Failed to read 0x%lX bytes block from multiple BKTR indirect storage entries at offset 0x%lX!", read_size, section_offset);
    }
    
    return success;
}

static bool bktrAesCtrExStorageRead(BktrContext *ctx, void *out, u64 read_size, u64 virtual_offset, u64 section_offset)
{
    BktrAesCtrExStorageEntry *aes_ctr_ex_entry = NULL, *next_aes_ctr_ex_entry = NULL;
    
    if (!ctx || !ctx->patch_romfs_ctx.nca_fs_ctx || !ctx->aes_ctr_ex_block || !out || !read_size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    aes_ctr_ex_entry = bktrGetAesCtrExStorageEntry(ctx->aes_ctr_ex_block, section_offset);
    if (!aes_ctr_ex_entry)
    {
        LOG_MSG("Error retrieving BKTR AesCtrEx Storage Entry at offset 0x%lX!", section_offset);
        return false;
    }
    
    next_aes_ctr_ex_entry = (aes_ctr_ex_entry + 1);
    
    /* Perform read operation. */
    bool success = false;
    if ((section_offset + read_size) <= next_aes_ctr_ex_entry->offset)
    {
        /* Read only within the current AesCtrEx storage entry. */
        success = ncaReadAesCtrExStorageFromBktrSection(ctx->patch_romfs_ctx.nca_fs_ctx, out, read_size, section_offset, aes_ctr_ex_entry->generation);
    } else {
        /* Handle read that spans multiple AesCtrEx storage entries. */
        u64 aes_ctr_ex_block_size = (next_aes_ctr_ex_entry->offset - section_offset);
        
        success = (bktrPhysicalSectionRead(ctx, out, aes_ctr_ex_block_size, virtual_offset) && \
                   bktrPhysicalSectionRead(ctx, (u8*)out + aes_ctr_ex_block_size, read_size - aes_ctr_ex_block_size, virtual_offset + aes_ctr_ex_block_size));
    }
    
    return success;
}

static bool bktrPhysicalSectionSparseRead(BktrContext *ctx, void *out, u64 read_size, u64 offset)
{
    NcaFsSectionContext *nca_fs_ctx = NULL;
    
    if (!ctx || !(nca_fs_ctx = ctx->base_romfs_ctx.nca_fs_ctx) || !nca_fs_ctx->has_sparse_layer || !ctx->base_indirect_block || !out || !read_size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    BktrIndirectStorageEntry *indirect_entry = NULL, *next_indirect_entry = NULL;
    u64 section_offset = 0, indirect_block_size = 0;
    
    indirect_entry = bktrGetIndirectStorageEntry(ctx->base_indirect_block, offset);
    if (!indirect_entry)
    {
        LOG_MSG("Error retrieving BKTR Indirect Storage Entry at offset 0x%lX!", offset);
        return false;
    }
    
    next_indirect_entry = (indirect_entry + 1);
    section_offset = (offset - indirect_entry->virtual_offset + indirect_entry->physical_offset);
    
    /* Perform read operation. */
    bool success = false;
    if ((offset + read_size) <= next_indirect_entry->virtual_offset)
    {
        /* Read only within the current indirect storage entry. */
        if (indirect_entry->indirect_storage_index == BktrIndirectStorageIndex_Patch)
        {
            /* Fill output buffer with zeroes. */
            memset(out, 0, read_size);
            success = true;
        } else {
            /* Set the virtual offset used for data decryption within the sparse layer. */
            /* This will be used by ncaReadFsSection(). */
            nca_fs_ctx->cur_sparse_virtual_offset = offset;
            
            /* Retrieve data from the base NCA's sparse layer. */
            success = ncaReadFsSection(nca_fs_ctx, out, read_size, section_offset);
            if (!success) LOG_MSG("Failed to read 0x%lX bytes block from sparse layer at offset 0x%lX!", read_size, section_offset);
        }
    } else {
        /* Handle reads that span multiple indirect storage entries. */
        indirect_block_size = (next_indirect_entry->virtual_offset - offset);
        
        success = (bktrPhysicalSectionSparseRead(ctx, out, indirect_block_size, offset) && \
                   bktrPhysicalSectionSparseRead(ctx, (u8*)out + indirect_block_size, read_size - indirect_block_size, offset + indirect_block_size));
        if (!success) LOG_MSG("Failed to read 0x%lX bytes block from multiple BKTR indirect storage entries at offset 0x%lX!", read_size, offset);
    }
    
    return success;
}

NX_INLINE BktrIndirectStorageBucket *bktrGetIndirectStorageBucket(BktrIndirectStorageBlock *block, u32 bucket_num)
{
    if (!block || bucket_num >= block->bucket_count) return NULL;
    return (BktrIndirectStorageBucket*)((u8*)block->indirect_storage_buckets + ((sizeof(BktrIndirectStorageBucket) + sizeof(BktrIndirectStorageEntry)) * (u64)bucket_num));
}

static BktrIndirectStorageEntry *bktrGetIndirectStorageEntry(BktrIndirectStorageBlock *block, u64 offset)
{
    if (!block || !block->bucket_count || offset >= block->virtual_size)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }
    
    u32 bucket_num = 0;
    BktrIndirectStorageBucket *bucket = NULL;
    
    for(u32 i = 1; i < block->bucket_count; i++)
    {
        if (block->virtual_offsets[i] <= offset) bucket_num++;
    }
    
    bucket = bktrGetIndirectStorageBucket(block, bucket_num);
    if (!bucket || !bucket->entry_count)
    {
        LOG_MSG("Error retrieving BKTR indirect storage bucket #%u!", bucket_num);
        return NULL;
    }
    
    /* Check for edge case, short circuit. */
    if (bucket->entry_count == 1) return &(bucket->indirect_storage_entries[0]);
    
    /* Binary search. */
    u32 low = 0, high = (bucket->entry_count - 1);
    while(low <= high)
    {
        u32 mid = ((low + high) / 2);
        
        if (bucket->indirect_storage_entries[mid].virtual_offset > offset)
        {
            /* Too high. */
            high = (mid - 1);
        } else {
            /* Check for success. */
            if (mid == (bucket->entry_count - 1) || bucket->indirect_storage_entries[mid + 1].virtual_offset > offset) return &(bucket->indirect_storage_entries[mid]);
            low = (mid + 1);
        }
    }
    
    LOG_MSG("Failed to find offset 0x%lX in BKTR indirect storage block!", offset);
    return NULL;
}

NX_INLINE BktrAesCtrExStorageBucket *bktrGetAesCtrExStorageBucket(BktrAesCtrExStorageBlock *block, u32 bucket_num)
{
    if (!block || bucket_num >= block->bucket_count) return NULL;
    return (BktrAesCtrExStorageBucket*)((u8*)block->aes_ctr_ex_storage_buckets + ((sizeof(BktrAesCtrExStorageBucket) + sizeof(BktrAesCtrExStorageEntry)) * (u64)bucket_num));
}

static BktrAesCtrExStorageEntry *bktrGetAesCtrExStorageEntry(BktrAesCtrExStorageBlock *block, u64 offset)
{
    if (!block || !block->bucket_count || offset >= block->physical_size)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }
    
    u32 bucket_num = 0;
    BktrAesCtrExStorageBucket *last_bucket = NULL, *bucket = NULL;
    
    last_bucket = bktrGetAesCtrExStorageBucket(block, block->bucket_count - 1);
    if (!last_bucket || !last_bucket->entry_count)
    {
        LOG_MSG("Error retrieving last BKTR AesCtrEx storage bucket!");
        return NULL;
    }
    
    if (offset >= last_bucket->aes_ctr_ex_storage_entries[last_bucket->entry_count].offset) return &(last_bucket->aes_ctr_ex_storage_entries[last_bucket->entry_count]);
    
    for(u32 i = 1; i < block->bucket_count; i++)
    {
        if (block->physical_offsets[i] <= offset) bucket_num++;
    }
    
    bucket = bktrGetAesCtrExStorageBucket(block, bucket_num);
    if (!bucket || !bucket->entry_count)
    {
        LOG_MSG("Error retrieving BKTR AesCtrEx storage bucket #%u!", bucket_num);
        return NULL;
    }
    
    /* Check for edge case, short circuit. */
    if (bucket->entry_count == 1) return &(bucket->aes_ctr_ex_storage_entries[0]);
    
    /* Binary search. */
    u32 low = 0, high = (bucket->entry_count - 1);
    while(low <= high)
    {
        u32 mid = ((low + high) / 2);
        
        if (bucket->aes_ctr_ex_storage_entries[mid].offset > offset)
        {
            /* Too high. */
            high = (mid - 1);
        } else {
            /* Check for success. */
            if (mid == (bucket->entry_count - 1) || bucket->aes_ctr_ex_storage_entries[mid + 1].offset > offset) return &(bucket->aes_ctr_ex_storage_entries[mid]);
            low = (mid + 1);
        }
    }
    
    LOG_MSG("Failed to find offset 0x%lX in BKTR AesCtrEx storage block!", offset);
    return NULL;
}
