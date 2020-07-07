/*
 * bktr.c
 *
 * Copyright (c) 2018-2020, SciresM.
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
#include "bktr.h"

/* Function prototypes. */

static bool bktrPhysicalSectionRead(BktrContext *ctx, void *out, u64 read_size, u64 offset);
static bool bktrAesCtrExStorageRead(BktrContext *ctx, void *out, u64 read_size, u64 virtual_offset, u64 section_offset);

NX_INLINE BktrIndirectStorageBucket *bktrGetIndirectStorageBucket(BktrIndirectStorageBlock *block, u32 bucket_num);
static BktrIndirectStorageEntry *bktrGetIndirectStorageEntry(BktrIndirectStorageBlock *block, u64 offset);

NX_INLINE BktrAesCtrExStorageBucket *bktrGetAesCtrExStorageBucket(BktrAesCtrExStorageBlock *block, u32 bucket_num);
static BktrAesCtrExStorageEntry *bktrGetAesCtrExStorageEntry(BktrAesCtrExStorageBlock *block, u64 offset);

bool bktrInitializeContext(BktrContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *update_nca_fs_ctx)
{
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    
    if (!out || !base_nca_fs_ctx || !base_nca_fs_ctx->enabled || !(base_nca_ctx = (NcaContext*)base_nca_fs_ctx->nca_ctx) || !base_nca_fs_ctx->header || \
        base_nca_fs_ctx->section_type != NcaFsSectionType_RomFs || base_nca_fs_ctx->encryption_type == NcaEncryptionType_AesCtrEx || !update_nca_fs_ctx || !update_nca_fs_ctx->enabled || \
        !update_nca_fs_ctx->header || !(update_nca_ctx = (NcaContext*)update_nca_fs_ctx->nca_ctx) || update_nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || \
        update_nca_fs_ctx->encryption_type != NcaEncryptionType_AesCtrEx || base_nca_ctx->header.program_id != update_nca_ctx->header.program_id || \
        base_nca_ctx->header.content_type != update_nca_ctx->header.content_type || __builtin_bswap32(update_nca_fs_ctx->header->patch_info.indirect_header.magic) != NCA_BKTR_MAGIC || \
        __builtin_bswap32(update_nca_fs_ctx->header->patch_info.aes_ctr_ex_header.magic) != NCA_BKTR_MAGIC || \
        (update_nca_fs_ctx->header->patch_info.indirect_offset + update_nca_fs_ctx->header->patch_info.indirect_size) != update_nca_fs_ctx->header->patch_info.aes_ctr_ex_offset || \
        (update_nca_fs_ctx->header->patch_info.aes_ctr_ex_offset + update_nca_fs_ctx->header->patch_info.aes_ctr_ex_size) != update_nca_fs_ctx->section_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Initialize base NCA RomFS context. */
    if (!romfsInitializeContext(&(out->base_romfs_ctx), base_nca_fs_ctx))
    {
        LOGFILE("Failed to initialize base NCA RomFS context!");
        return false;
    }
    
    /* Fill context. */
    bool success = false;
    NcaPatchInfo *patch_info = &(update_nca_fs_ctx->header->patch_info);
    
    /* Allocate space for an extra (fake) indirect storage entry, to simplify our logic. */
    out->indirect_block = calloc(1, patch_info->indirect_size + ((0x3FF0 / sizeof(u64)) * sizeof(BktrIndirectStorageEntry)));
    if (!out->indirect_block)
    {
        LOGFILE("Unable to allocate memory for the BKTR Indirect Storage Block!");
        goto exit;
    }
    
    /* Read indirect storage block data. */
    if (!ncaReadFsSection(update_nca_fs_ctx, out->indirect_block, patch_info->indirect_size, patch_info->indirect_offset))
    {
        LOGFILE("Failed to read BKTR Indirect Storage Block data!");
        goto exit;
    }
    
    /* Allocate space for an extra (fake) AesCtrEx storage entry, to simplify our logic. */
    out->aes_ctr_ex_block = calloc(1, patch_info->aes_ctr_ex_size + (((0x3FF0 / sizeof(u64)) + 1) * sizeof(BktrAesCtrExStorageEntry)));
    if (!out->aes_ctr_ex_block)
    {
        LOGFILE("Unable to allocate memory for the BKTR AesCtrEx Storage Block!");
        goto exit;
    }
    
    /* Read AesCtrEx storage block data. */
    if (!ncaReadFsSection(update_nca_fs_ctx, out->aes_ctr_ex_block, patch_info->aes_ctr_ex_size, patch_info->aes_ctr_ex_offset))
    {
        LOGFILE("Failed to read BKTR AesCtrEx Storage Block data!");
        goto exit;
    }
    
    if (out->aes_ctr_ex_block->physical_size != patch_info->aes_ctr_ex_offset)
    {
        LOGFILE("Invalid BKTR AesCtrEx Storage Block size!");
        goto exit;
    }
    
    /* This simplifies logic greatly... */
    for(u32 i = (out->indirect_block->bucket_count - 1); i > 0; i--)
    {
        BktrIndirectStorageBucket tmp_bucket = {0};
        memcpy(&tmp_bucket, &(out->indirect_block->indirect_storage_buckets[i]), sizeof(BktrIndirectStorageBucket));
        memcpy(bktrGetIndirectStorageBucket(out->indirect_block, i), &tmp_bucket, sizeof(BktrIndirectStorageBucket));
    }
    
    for(u32 i = 0; (i + 1) < out->indirect_block->bucket_count; i++)
    {
        BktrIndirectStorageBucket *cur_bucket = bktrGetIndirectStorageBucket(out->indirect_block, i);
        cur_bucket->indirect_storage_entries[cur_bucket->entry_count].virtual_offset = out->indirect_block->virtual_offsets[i + 1];
    }
    
    for(u32 i = (out->aes_ctr_ex_block->bucket_count - 1); i > 0; i--)
    {
        BktrAesCtrExStorageBucket tmp_bucket = {0};
        memcpy(&tmp_bucket, &(out->aes_ctr_ex_block->aes_ctr_ex_storage_buckets[i]), sizeof(BktrAesCtrExStorageBucket));
        memcpy(bktrGetAesCtrExStorageBucket(out->aes_ctr_ex_block, i), &tmp_bucket, sizeof(BktrAesCtrExStorageBucket));
    }
    
    for(u32 i = 0; (i + 1) < out->aes_ctr_ex_block->bucket_count; i++)
    {
        BktrAesCtrExStorageBucket *cur_bucket = bktrGetAesCtrExStorageBucket(out->aes_ctr_ex_block, i);
        BktrAesCtrExStorageBucket *next_bucket = bktrGetAesCtrExStorageBucket(out->aes_ctr_ex_block, i + 1);
        cur_bucket->aes_ctr_ex_storage_entries[cur_bucket->entry_count].offset = next_bucket->aes_ctr_ex_storage_entries[0].offset;
        cur_bucket->aes_ctr_ex_storage_entries[cur_bucket->entry_count].generation = next_bucket->aes_ctr_ex_storage_entries[0].generation;
    }
    
    BktrIndirectStorageBucket *last_indirect_bucket = bktrGetIndirectStorageBucket(out->indirect_block, out->indirect_block->bucket_count - 1);
    BktrAesCtrExStorageBucket *last_aes_ctr_ex_bucket = bktrGetAesCtrExStorageBucket(out->aes_ctr_ex_block, out->aes_ctr_ex_block->bucket_count - 1);
    last_indirect_bucket->indirect_storage_entries[last_indirect_bucket->entry_count].virtual_offset = out->indirect_block->virtual_size;
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count].offset = patch_info->indirect_offset;
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count].generation = update_nca_fs_ctx->header->generation;
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count + 1].offset = update_nca_fs_ctx->section_size;
    last_aes_ctr_ex_bucket->aes_ctr_ex_storage_entries[last_aes_ctr_ex_bucket->entry_count + 1].generation = 0;
    
    /* Initialize update NCA RomFS context. */
    /* Don't verify offsets from Patch RomFS sections, because they reflect the full, patched RomFS image. */
    out->patch_romfs_ctx.nca_fs_ctx = update_nca_fs_ctx;
    out->patch_romfs_ctx.offset = out->offset = update_nca_fs_ctx->header->hash_info.hierarchical_integrity.hash_target_layer_info.offset;
    out->patch_romfs_ctx.size = out->size = update_nca_fs_ctx->header->hash_info.hierarchical_integrity.hash_target_layer_info.size;
    
    /* Read update NCA RomFS header. */
    if (!bktrPhysicalSectionRead(out, &(out->patch_romfs_ctx.header), sizeof(RomFileSystemHeader), out->patch_romfs_ctx.offset))
    {
        LOGFILE("Failed to read update NCA RomFS header!");
        goto exit;
    }
    
    if (out->patch_romfs_ctx.header.cur_format.header_size != ROMFS_HEADER_SIZE)
    {
        LOGFILE("Invalid update NCA RomFS header size!");
        goto exit;
    }
    
    /* Read directory entries table. */
    u64 dir_table_offset = out->patch_romfs_ctx.header.cur_format.directory_entry_offset;
    out->patch_romfs_ctx.dir_table_size = out->patch_romfs_ctx.header.cur_format.directory_entry_size;
    
    if (!dir_table_offset || !out->patch_romfs_ctx.dir_table_size)
    {
        LOGFILE("Invalid update NCA RomFS directory entries table!");
        goto exit;
    }
    
    out->patch_romfs_ctx.dir_table = malloc(out->patch_romfs_ctx.dir_table_size);
    if (!out->patch_romfs_ctx.dir_table)
    {
        LOGFILE("Unable to allocate memory for the update NCA RomFS directory entries table!");
        goto exit;
    }
    
    if (!bktrPhysicalSectionRead(out, out->patch_romfs_ctx.dir_table, out->patch_romfs_ctx.dir_table_size, out->patch_romfs_ctx.offset + dir_table_offset))
    {
        LOGFILE("Failed to read update NCA RomFS directory entries table!");
        goto exit;
    }
    
    /* Read file entries table. */
    u64 file_table_offset = out->patch_romfs_ctx.header.cur_format.file_entry_offset;
    out->patch_romfs_ctx.file_table_size = out->patch_romfs_ctx.header.cur_format.file_entry_size;
    
    if (!file_table_offset || !out->patch_romfs_ctx.file_table_size)
    {
        LOGFILE("Invalid update NCA RomFS file entries table!");
        goto exit;
    }
    
    out->patch_romfs_ctx.file_table = malloc(out->patch_romfs_ctx.file_table_size);
    if (!out->patch_romfs_ctx.file_table)
    {
        LOGFILE("Unable to allocate memory for the update NCA RomFS file entries table!");
        goto exit;
    }
    
    if (!bktrPhysicalSectionRead(out, out->patch_romfs_ctx.file_table, out->patch_romfs_ctx.file_table_size, out->patch_romfs_ctx.offset + file_table_offset))
    {
        LOGFILE("Failed to read update NCA RomFS file entries table!");
        goto exit;
    }
    
    /* Get file data body offset. */
    out->patch_romfs_ctx.body_offset = out->body_offset = out->patch_romfs_ctx.header.cur_format.body_offset;
    
    success = true;
    
exit:
    if (!success) bktrFreeContext(out);
    
    return success;
}

bool bktrReadFileSystemData(BktrContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->size || !out || !read_size || offset >= ctx->size || (offset + read_size) > ctx->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Read filesystem data. */
    if (!bktrPhysicalSectionRead(ctx, out, read_size, ctx->offset + offset))
    {
        LOGFILE("Failed to read Patch RomFS data!");
        return false;
    }
    
    return true;
}

bool bktrReadFileEntryData(BktrContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->body_offset || !file_entry || !file_entry->size || file_entry->offset >= ctx->size || (file_entry->offset + file_entry->size) > ctx->size || \
        !out || !read_size || offset >= file_entry->size || (offset + read_size) > file_entry->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Read entry data. */
    if (!bktrReadFileSystemData(ctx, out, read_size, ctx->body_offset + file_entry->offset + offset))
    {
        LOGFILE("Failed to read Patch RomFS file entry data!");
        return false;
    }
    
    return true;
}

bool bktrIsFileEntryUpdated(BktrContext *ctx, RomFileSystemFileEntry *file_entry, bool *out)
{
    if (!ctx || !ctx->body_offset || !ctx->indirect_block || !file_entry || !file_entry->size || file_entry->offset >= ctx->size || (file_entry->offset + file_entry->size) > ctx->size || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    bool updated = false;
    u64 file_offset = (ctx->offset + ctx->body_offset + file_entry->offset);
    BktrIndirectStorageEntry *indirect_entry = NULL, *last_indirect_entry = NULL;
    
    indirect_entry = bktrGetIndirectStorageEntry(ctx->indirect_block, file_offset);
    if (!indirect_entry)
    {
        LOGFILE("Error retrieving BKTR Indirect Storage Entry at offset 0x%lX!", file_offset);
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

static bool bktrPhysicalSectionRead(BktrContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->base_romfs_ctx.nca_fs_ctx || !ctx->indirect_block || !out || !read_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    BktrIndirectStorageEntry *indirect_entry = NULL, *next_indirect_entry = NULL;
    u64 section_offset = 0, indirect_block_size = 0;
    
    /* Determine which FS section to use + the actual offset to start reading from. */
    /* There's no better way to do this than making all BKTR addresses virtual. */
    indirect_entry = bktrGetIndirectStorageEntry(ctx->indirect_block, offset);
    if (!indirect_entry)
    {
        LOGFILE("Error retrieving BKTR Indirect Storage Entry at offset 0x%lX!", offset);
        return false;
    }
    
    next_indirect_entry = (indirect_entry + 1);
    section_offset = (offset - indirect_entry->virtual_offset + indirect_entry->physical_offset);
    
    /* Perform read operation. */
    bool success = false;
    if ((offset + read_size) <= next_indirect_entry->virtual_offset)
    {
        /* Read only within the current indirect storage entry. */
        /* If we're not dealing with an indirect storage entry with a patch index, just retrieve the data from the base RomFS. */
        if (indirect_entry->indirect_storage_index == BktrIndirectStorageIndex_Patch)
        {
            success = bktrAesCtrExStorageRead(ctx, out, read_size, offset, section_offset);
            if (!success) LOGFILE("Failed to read 0x%lX bytes block from BKTR AesCtrEx storage at offset 0x%lX!", read_size, section_offset);
        } else {
            success = ncaReadFsSection(ctx->base_romfs_ctx.nca_fs_ctx, out, read_size, section_offset);
            if (!success) LOGFILE("Failed to read 0x%lX bytes block from base RomFS at offset 0x%lX!", read_size, section_offset);
        }
    } else {
        /* Handle reads that span multiple indirect storage entries. */
        indirect_block_size = (next_indirect_entry->virtual_offset - offset);
        
        success = (bktrPhysicalSectionRead(ctx, out, indirect_block_size, offset) && \
                   bktrPhysicalSectionRead(ctx, (u8*)out + indirect_block_size, read_size - indirect_block_size, offset + indirect_block_size));
        if (!success) LOGFILE("Failed to read 0x%lX bytes block from multiple BKTR indirect storage entries at offset 0x%lX!", read_size, section_offset);
    }
    
    return success;
}

static bool bktrAesCtrExStorageRead(BktrContext *ctx, void *out, u64 read_size, u64 virtual_offset, u64 section_offset)
{
    BktrAesCtrExStorageEntry *aes_ctr_ex_entry = NULL, *next_aes_ctr_ex_entry = NULL;
    
    if (!ctx || !ctx->patch_romfs_ctx.nca_fs_ctx || !ctx->aes_ctr_ex_block || !out || !read_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    aes_ctr_ex_entry = bktrGetAesCtrExStorageEntry(ctx->aes_ctr_ex_block, section_offset);
    if (!aes_ctr_ex_entry)
    {
        LOGFILE("Error retrieving BKTR AesCtrEx Storage Entry at offset 0x%lX!", section_offset);
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

NX_INLINE BktrIndirectStorageBucket *bktrGetIndirectStorageBucket(BktrIndirectStorageBlock *block, u32 bucket_num)
{
    if (!block || bucket_num >= block->bucket_count) return NULL;
    return (BktrIndirectStorageBucket*)((u8*)block->indirect_storage_buckets + ((sizeof(BktrIndirectStorageBucket) + sizeof(BktrIndirectStorageEntry)) * (u64)bucket_num));
}

static BktrIndirectStorageEntry *bktrGetIndirectStorageEntry(BktrIndirectStorageBlock *block, u64 offset)
{
    if (!block || !block->bucket_count || offset >= block->virtual_size)
    {
        LOGFILE("Invalid parameters!");
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
        LOGFILE("Error retrieving BKTR indirect storage bucket #%u!", bucket_num);
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
    
    LOGFILE("Failed to find offset 0x%lX in BKTR indirect storage block!", offset);
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
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    u32 bucket_num = 0;
    BktrAesCtrExStorageBucket *last_bucket = NULL, *bucket = NULL;
    
    last_bucket = bktrGetAesCtrExStorageBucket(block, block->bucket_count - 1);
    if (!last_bucket || !last_bucket->entry_count)
    {
        LOGFILE("Error retrieving last BKTR AesCtrEx storage bucket!");
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
        LOGFILE("Error retrieving BKTR AesCtrEx storage bucket #%u!", bucket_num);
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
    
    LOGFILE("Failed to find offset 0x%lX in BKTR AesCtrEx storage block!", offset);
    return NULL;
}
