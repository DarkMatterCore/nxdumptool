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

#include "pfs0.h"
#include "utils.h"

#define PFS0_NCA_FS_HEADER_LAYER_COUNT  2

#define NPDM_META_MAGIC                 0x4D455441  /* "META" */

bool pfs0InitializeContext(PartitionFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx)
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
    
    if (!out->hash_info->hash_block_size || out->hash_info->layer_count != PFS0_NCA_FS_HEADER_LAYER_COUNT || out->hash_info->hash_data_layer_info.offset >= out->nca_fs_ctx->section_size || \
        !out->hash_info->hash_data_layer_info.size || (out->hash_info->hash_data_layer_info.offset + out->hash_info->hash_data_layer_info.size) > out->nca_fs_ctx->section_size || \
        out->hash_info->hash_target_layer_info.offset >= out->nca_fs_ctx->section_size || !out->hash_info->hash_target_layer_info.size || \
        (out->hash_info->hash_target_layer_info.offset + out->hash_info->hash_target_layer_info.size) > out->nca_fs_ctx->section_size)
    {
        LOGFILE("Invalid HierarchicalSha256 block!");
        return false;
    }
    
    out->offset = out->hash_info->hash_target_layer_info.offset;
    out->size = out->hash_info->hash_target_layer_info.size;
    
    /* Read partial PFS0 header */
    u32 magic = 0;
    PartitionFileSystemHeader pfs0_header = {0};
    PartitionFileSystemEntry *main_npdm_entry = NULL;
    
    if (!ncaReadFsSection(nca_fs_ctx, &pfs0_header, sizeof(PartitionFileSystemHeader), out->offset))
    {
        LOGFILE("Failed to read partial PFS0 header!");
        return false;
    }
    
    magic = __builtin_bswap32(pfs0_header.magic);
    if (magic != PFS0_MAGIC)
    {
        LOGFILE("Invalid PFS0 magic word! (0x%08X)", magic);
        return false;
    }
    
    if (!pfs0_header.entry_count || !pfs0_header.name_table_size)
    {
        LOGFILE("Invalid PFS0 entry count / name table size!");
        return false;
    }
    
    /* Calculate full PFS0 header size */
    out->header_size = (sizeof(PartitionFileSystemHeader) + (pfs0_header.entry_count * sizeof(PartitionFileSystemEntry)) + pfs0_header.name_table_size);
    
    /* Allocate memory for the full PFS0 header */
    out->header = calloc(out->header_size, sizeof(u8));
    if (!out->header)
    {
        LOGFILE("Unable to allocate 0x%lX bytes buffer for the full PFS0 header!", out->header_size);
        return false;
    }
    
    /* Read full PFS0 header */
    if (!ncaReadFsSection(nca_fs_ctx, out->header, out->header_size, out->offset))
    {
        LOGFILE("Failed to read full PFS0 header!");
        return false;
    }
    
    /* Check if we're dealing with an ExeFS section */
    if ((main_npdm_entry = pfs0GetEntryByName(out, "main.npdm")) != NULL && pfs0ReadEntryData(out, main_npdm_entry, &magic, sizeof(u32), 0) && \
        __builtin_bswap32(magic) == NPDM_META_MAGIC) out->is_exefs = true;
    
    return true;
}

bool pfs0ReadEntryData(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->nca_fs_ctx || !ctx->hash_info || !ctx->size || !ctx->header_size || !ctx->header || !fs_entry || fs_entry->offset >= ctx->size || \
        (fs_entry->offset + fs_entry->size) > ctx->size || !out || !read_size || offset >= fs_entry->size || (offset + read_size) > fs_entry->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Calculate offset relative to the start of the NCA FS section */
    u64 section_offset = (ctx->offset + ctx->header_size + fs_entry->offset + offset);
    
    /* Read entry data */
    if (!ncaReadFsSection(ctx->nca_fs_ctx, out, read_size, section_offset))
    {
        LOGFILE("Failed to read PFS0 entry data!");
        return false;
    }
    
    return true;
}
