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

#include "pfs.h"
#include "utils.h"

bool pfsInitializeContext(PartitionFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    if (!out || !nca_fs_ctx || nca_fs_ctx->section_type != NcaFsSectionType_PartitionFs || !nca_fs_ctx->header || nca_fs_ctx->header->fs_type != NcaFsType_PartitionFs || \
        nca_fs_ctx->header->hash_type != NcaHashType_HierarchicalSha256)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Fill context */
    out->nca_fs_ctx = nca_fs_ctx;
    out->hash_info = &(nca_fs_ctx->header->hash_info.hierarchical_sha256);
    out->offset = 0;
    out->size = 0;
    out->is_exefs = false;
    out->header_size = 0;
    out->header = NULL;
    
    if (!ncaValidateHierarchicalSha256Offsets(out->hash_info, nca_fs_ctx->section_size))
    {
        LOGFILE("Invalid HierarchicalSha256 block!");
        return false;
    }
    
    out->offset = out->hash_info->hash_target_layer_info.offset;
    out->size = out->hash_info->hash_target_layer_info.size;
    
    /* Read partial PFS header */
    u32 magic = 0;
    PartitionFileSystemHeader pfs_header = {0};
    PartitionFileSystemEntry *main_npdm_entry = NULL;
    
    if (!ncaReadFsSection(nca_fs_ctx, &pfs_header, sizeof(PartitionFileSystemHeader), out->offset))
    {
        LOGFILE("Failed to read partial partition FS header!");
        return false;
    }
    
    magic = __builtin_bswap32(pfs_header.magic);
    if (magic != PFS0_MAGIC)
    {
        LOGFILE("Invalid partition FS magic word! (0x%08X)", magic);
        return false;
    }
    
    if (!pfs_header.entry_count || !pfs_header.name_table_size)
    {
        LOGFILE("Invalid partition FS entry count / name table size!");
        return false;
    }
    
    /* Calculate full partition FS header size */
    out->header_size = (sizeof(PartitionFileSystemHeader) + (pfs_header.entry_count * sizeof(PartitionFileSystemEntry)) + pfs_header.name_table_size);
    
    /* Allocate memory for the full partition FS header */
    out->header = calloc(out->header_size, sizeof(u8));
    if (!out->header)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer for the full partition FS header!", out->header_size);
        return false;
    }
    
    /* Read full partition FS header */
    if (!ncaReadFsSection(nca_fs_ctx, out->header, out->header_size, out->offset))
    {
        LOGFILE("Failed to read full partition FS header!");
        return false;
    }
    
    /* Check if we're dealing with an ExeFS section */
    if ((main_npdm_entry = pfsGetEntryByName(out, "main.npdm")) != NULL && pfsReadEntryData(out, main_npdm_entry, &magic, sizeof(u32), 0) && \
        __builtin_bswap32(magic) == NPDM_META_MAGIC) out->is_exefs = true;
    
    return true;
}

bool pfsReadPartitionData(PartitionFileSystemContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->nca_fs_ctx || !ctx->size || !out || !read_size || offset >= ctx->size || (offset + read_size) > ctx->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Read partition data */
    if (!ncaReadFsSection(ctx->nca_fs_ctx, out, read_size, ctx->offset + offset))
    {
        LOGFILE("Failed to read partition FS data!");
        return false;
    }
    
    return true;
}

bool pfsReadEntryData(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !fs_entry || fs_entry->offset >= ctx->size || !fs_entry->size || (fs_entry->offset + fs_entry->size) > ctx->size || !out || !read_size || offset >= fs_entry->size || \
        (offset + read_size) > fs_entry->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Read entry data */
    if (!pfsReadPartitionData(ctx, out, read_size, ctx->header_size + fs_entry->offset + offset))
    {
        LOGFILE("Failed to read partition FS entry data!");
        return false;
    }
    
    return true;
}

bool pfsGenerateEntryPatch(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, const void *data, u64 data_size, u64 data_offset, PartitionFileSystemPatchInfo *out)
{
    NcaContext *nca_ctx = NULL;
    
    if (!ctx || !ctx->nca_fs_ctx || !(nca_ctx = (NcaContext*)ctx->nca_fs_ctx->nca_ctx) || !ctx->hash_info || !ctx->header_size || !ctx->header || !fs_entry || fs_entry->offset >= ctx->size || \
        !fs_entry->size || (fs_entry->offset + fs_entry->size) > ctx->size || !data || !data_size || data_offset >= fs_entry->size || (data_offset + data_size) > fs_entry->size || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Calculate required offsets and sizes */
    u64 partition_offset = (ctx->header_size + fs_entry->offset + data_offset);
    u64 block_size = ctx->hash_info->hash_block_size;
    
    u64 hash_table_offset = ctx->hash_info->hash_data_layer_info.offset;
    u64 hash_table_size = ctx->hash_info->hash_data_layer_info.size;
    
    u64 hash_block_start_offset = ((partition_offset / block_size) * SHA256_HASH_SIZE);
    u64 hash_block_end_offset = (((partition_offset + data_size) / block_size) * SHA256_HASH_SIZE);
    u64 hash_block_size = (hash_block_end_offset != hash_block_start_offset ? (hash_block_end_offset - hash_block_start_offset) : SHA256_HASH_SIZE);
    
    u64 data_block_start_offset = (ctx->offset + ALIGN_DOWN(partition_offset, block_size));
    u64 data_block_end_offset = (ctx->offset + ALIGN_UP(partition_offset + data_size, block_size));
    u64 data_block_size = (data_block_end_offset - data_block_start_offset);
    
    u64 block_count = (hash_block_size / SHA256_HASH_SIZE);
    u64 new_data_offset = (partition_offset - ALIGN_DOWN(partition_offset, block_size));
    
    u8 *hash_table = NULL, *data_block = NULL;
    
    bool success = false;
    
    /* Allocate memory for the full hash table */
    hash_table = malloc(hash_table_size);
    if (!hash_table)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer for the full partition FS hash table!", hash_table_size);
        goto exit;
    }
    
    /* Read full hash table */
    if (!ncaReadFsSection(ctx->nca_fs_ctx, hash_table, hash_table_size, hash_table_offset))
    {
        LOGFILE("Failed to read full partition FS hash table!");
        goto exit;
    }
    
    /* Allocate memory for the modified data block */
    data_block = malloc(data_block_size);
    if (!data_block)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer for the modified partition FS data block!", data_block_size);
        goto exit;
    }
    
    /* Read data block */
    if (!ncaReadFsSection(ctx->nca_fs_ctx, data_block, data_block_size, data_block_start_offset))
    {
        LOGFILE("Failed to read partition FS data block!");
        goto exit;
    }
    
    /* Replace data */
    memcpy(data_block + new_data_offset, data, data_size);
    
    /* Recalculate hashes */
    for(u64 i = 0; i < block_count; i++) sha256CalculateHash(hash_table + hash_block_start_offset + (i * SHA256_HASH_SIZE), data_block + (i * block_size), block_size);
    
    /* Reencrypt hash block */
    out->hash_block = ncaGenerateEncryptedFsSectionBlock(ctx->nca_fs_ctx, hash_table + hash_block_start_offset, hash_block_size, hash_table_offset + hash_block_start_offset, \
                                                         &(out->hash_block_size), &(out->hash_block_offset));
    if (!out->hash_block)
    {
        LOGFILE("Failed to generate encrypted partition FS hash block!");
        goto exit;
    }
    
    /* Reencrypt data block */
    out->data_block = ncaGenerateEncryptedFsSectionBlock(ctx->nca_fs_ctx, data_block, data_block_size, data_block_start_offset, &(out->data_block_size), &(out->data_block_offset));
    if (!out->data_block)
    {
        LOGFILE("Failed to generate encrypted partition FS data block!");
        goto exit;
    }
    
    /* Recalculate master hash from hash info block */
    sha256CalculateHash(ctx->hash_info->master_hash, hash_table, hash_table_size);
    
    /* Recalculate FS header hash */
    sha256CalculateHash(nca_ctx->header.fs_hashes[ctx->nca_fs_ctx->section_num].hash, ctx->header, sizeof(NcaFsHeader));
    
    /* Enable the 'dirty_header' flag */
    nca_ctx->dirty_header = true;
    
    success = true;
    
exit:
    if (data_block) free(data_block);
    
    if (hash_table) free(hash_table);
    
    return success;
}
