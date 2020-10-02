/*
 * pfs.c
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
#include "pfs.h"

bool pfsInitializeContext(PartitionFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    if (!out || !nca_fs_ctx || !nca_fs_ctx->enabled || nca_fs_ctx->section_type != NcaFsSectionType_PartitionFs || nca_fs_ctx->header.fs_type != NcaFsType_PartitionFs || \
        nca_fs_ctx->header.hash_type != NcaHashType_HierarchicalSha256)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 magic = 0;
    
    PartitionFileSystemHeader pfs_header = {0};
    PartitionFileSystemEntry *main_npdm_entry = NULL;
    
    u32 hash_region_count = 0;
    NcaRegion *hash_region = NULL;
    
    /* Clear output Partition FS context. */
    memset(out, 0, sizeof(PartitionFileSystemContext));
    
    /* Fill context. */
    out->nca_fs_ctx = nca_fs_ctx;
    
    if (!ncaValidateHierarchicalSha256Offsets(&(nca_fs_ctx->header.hash_data.hierarchical_sha256_data), nca_fs_ctx->section_size))
    {
        LOGFILE("Invalid HierarchicalSha256 block!");
        return false;
    }
    
    hash_region_count = nca_fs_ctx->header.hash_data.hierarchical_sha256_data.hash_region_count;
    hash_region = &(nca_fs_ctx->header.hash_data.hierarchical_sha256_data.hash_region[hash_region_count - 1]);
    
    out->offset = hash_region->offset;
    out->size = hash_region->size;
    
    /* Read partial PFS header. */
    if (!ncaReadFsSection(nca_fs_ctx, &pfs_header, sizeof(PartitionFileSystemHeader), out->offset))
    {
        LOGFILE("Failed to read partial Partition FS header!");
        return false;
    }
    
    magic = __builtin_bswap32(pfs_header.magic);
    if (magic != PFS0_MAGIC)
    {
        LOGFILE("Invalid Partition FS magic word! (0x%08X).", magic);
        return false;
    }
    
    if (!pfs_header.entry_count || !pfs_header.name_table_size)
    {
        LOGFILE("Invalid Partition FS entry count / name table size!");
        return false;
    }
    
    /* Calculate full Partition FS header size. */
    out->header_size = (sizeof(PartitionFileSystemHeader) + (pfs_header.entry_count * sizeof(PartitionFileSystemEntry)) + pfs_header.name_table_size);
    
    /* Allocate memory for the full Partition FS header. */
    out->header = calloc(out->header_size, sizeof(u8));
    if (!out->header)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer for the full Partition FS header!", out->header_size);
        return false;
    }
    
    /* Read full Partition FS header. */
    if (!ncaReadFsSection(nca_fs_ctx, out->header, out->header_size, out->offset))
    {
        LOGFILE("Failed to read full Partition FS header!");
        pfsFreeContext(out);
        return false;
    }
    
    /* Check if we're dealing with an ExeFS section. */
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
    
    /* Read partition data. */
    if (!ncaReadFsSection(ctx->nca_fs_ctx, out, read_size, ctx->offset + offset))
    {
        LOGFILE("Failed to read Partition FS data!");
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
    
    /* Read entry data. */
    if (!pfsReadPartitionData(ctx, out, read_size, ctx->header_size + fs_entry->offset + offset))
    {
        LOGFILE("Failed to read Partition FS entry data!");
        return false;
    }
    
    return true;
}

bool pfsGetEntryIndexByName(PartitionFileSystemContext *ctx, const char *name, u32 *out_idx)
{
    size_t name_len = 0;
    PartitionFileSystemEntry *fs_entry = NULL;
    u32 entry_count = pfsGetEntryCount(ctx);
    char *name_table = pfsGetNameTable(ctx);
    
    if (!entry_count || !name_table || !name || !(name_len = strlen(name)) || !out_idx)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = pfsGetEntryByIndex(ctx, i)))
        {
            LOGFILE("Failed to retrieve Partition FS entry #%u!", i);
            return false;
        }
        
        if (strlen(name_table + fs_entry->name_offset) == name_len && !strcmp(name_table + fs_entry->name_offset, name))
        {
            *out_idx = i;
            return true;
        }
    }
    
    //LOGFILE("Unable to find Partition FS entry \"%s\"!", name);
    
    return false;
}

bool pfsGetTotalDataSize(PartitionFileSystemContext *ctx, u64 *out_size)
{
    u64 total_size = 0;
    u32 entry_count = pfsGetEntryCount(ctx);
    PartitionFileSystemEntry *fs_entry = NULL;
    
    if (!entry_count || !out_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = pfsGetEntryByIndex(ctx, i)))
        {
            LOGFILE("Failed to retrieve Partition FS entry #%u!", i);
            return false;
        }
        
        total_size += fs_entry->size;
    }
    
    *out_size = total_size;
    
    return true;
}

bool pfsGenerateEntryPatch(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out)
{
    if (!ctx || !ctx->nca_fs_ctx || !ctx->header_size || !ctx->header || !fs_entry || fs_entry->offset >= ctx->size || !fs_entry->size || (fs_entry->offset + fs_entry->size) > ctx->size || !data || \
        !data_size || data_offset >= fs_entry->size || (data_offset + data_size) > fs_entry->size || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u64 partition_offset = (ctx->header_size + fs_entry->offset + data_offset);
    
    if (!ncaGenerateHierarchicalSha256Patch(ctx->nca_fs_ctx, data, data_size, partition_offset, out))
    {
        LOGFILE("Failed to generate 0x%lX bytes HierarchicalSha256 patch at offset 0x%lX for Partition FS entry!", data_size, partition_offset);
        return false;
    }
    
    return true;
}
