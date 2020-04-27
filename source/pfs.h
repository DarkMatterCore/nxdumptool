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

#pragma once

#ifndef __PFS_H__
#define __PFS_H__

#include <switch.h>
#include "nca.h"

#define PFS0_MAGIC  0x50465330  /* "PFS0" */

typedef struct {
    u32 magic;              ///< "PFS0".
    u32 entry_count;
    u32 name_table_size;
    u8 reserved[0x4];
} PartitionFileSystemHeader;

typedef struct {
    u64 offset;
    u64 size;
    u32 name_offset;
    u8 reserved[0x4];
} PartitionFileSystemEntry;

typedef struct {
    NcaFsSectionContext *nca_fs_ctx;    ///< Used to read NCA FS section data.
    NcaHierarchicalSha256 *hash_info;   ///< Hash table information.
    u64 offset;                         ///< Partition offset (relative to the start of the NCA FS section).
    u64 size;                           ///< Partition size.
    bool is_exefs;                      ///< ExeFS flag.
    u64 header_size;                    ///< Full header size.
    u8 *header;                         ///< PartitionFileSystemHeader + (PartitionFileSystemEntry * entry_count) + Name Table.
} PartitionFileSystemContext;

typedef struct {
    u64 hash_block_offset;  ///< New hash block offset (relative to the start of the NCA content file).
    u64 hash_block_size;    ///< New hash block size.
    u8 *hash_block;         ///< New hash block contents.
    u64 data_block_offset;  ///< New data block offset (relative to the start of the NCA content file).
    u64 data_block_size;    ///< New data block size (aligned to the NcaHierarchicalSha256 block size).
    u8 *data_block;         ///< New data block contents.
} PartitionFileSystemPatchInfo;

/// Initializes a partition FS context.
bool pfsInitializeContext(PartitionFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx);

/// Cleanups a previously initialized partition FS context.
NX_INLINE void pfsFreeContext(PartitionFileSystemContext *ctx)
{
    if (!ctx) return;
    if (ctx->header) free(ctx->header);
    memset(ctx, 0, sizeof(PartitionFileSystemContext));
}

/// Reads raw partition data using a partition FS context.
bool pfsReadPartitionData(PartitionFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved PartitionFileSystemEntry using a partition FS context.
bool pfsReadEntryData(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset);

/// Generates modified + encrypted hash and data blocks using a partition FS context + entry information. Both blocks are ready to be used to replace NCA content data during writing operations.
/// Input offset must be relative to the start of the partition FS entry data.
/// Bear in mind that this function recalculates both the NcaHashInfo block master hash and the NCA FS header hash from the NCA header, and enables the 'dirty_header' flag from the NCA context.
/// As such, this function is only capable of modifying a single file from a partition FS in a NCA content file.
bool pfsGenerateEntryPatch(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, const void *data, u64 data_size, u64 data_offset, PartitionFileSystemPatchInfo *out);

/// Miscellaneous functions.

NX_INLINE u32 pfsGetEntryCount(PartitionFileSystemContext *ctx)
{
    if (!ctx || !ctx->header_size || !ctx->header) return 0;
    return ((PartitionFileSystemHeader*)ctx->header)->entry_count;
}

NX_INLINE PartitionFileSystemEntry *pfsGetEntryByIndex(PartitionFileSystemContext *ctx, u32 idx)
{
    if (idx >= pfsGetEntryCount(ctx)) return NULL;
    return (PartitionFileSystemEntry*)(ctx->header + sizeof(PartitionFileSystemHeader) + (idx * sizeof(PartitionFileSystemEntry)));
}

NX_INLINE char *pfsGetNameTable(PartitionFileSystemContext *ctx)
{
    u32 entry_count = pfsGetEntryCount(ctx);
    if (!entry_count) return NULL;
    return (char*)(ctx->header + sizeof(PartitionFileSystemHeader) + (entry_count * sizeof(PartitionFileSystemEntry)));
}

NX_INLINE char *pfsGetEntryNameByIndex(PartitionFileSystemContext *ctx, u32 idx)
{
    PartitionFileSystemEntry *fs_entry = pfsGetEntryByIndex(ctx, idx);
    char *name_table = pfsGetNameTable(ctx);
    if (!fs_entry || !name_table) return NULL;
    return (name_table + fs_entry->name_offset);
}

NX_INLINE bool pfsGetEntryIndexByName(PartitionFileSystemContext *ctx, const char *name, u32 *out_idx)
{
    size_t name_len = 0;
    PartitionFileSystemEntry *fs_entry = NULL;
    u32 entry_count = pfsGetEntryCount(ctx);
    char *name_table = pfsGetNameTable(ctx);
    if (!entry_count || !name_table || !name || !(name_len = strlen(name)) || !out_idx) return false;
    
    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = pfsGetEntryByIndex(ctx, i))) return false;
        
        if (!strncmp(name_table + fs_entry->name_offset, name, name_len))
        {
            *out_idx = i;
            return true;
        }
    }
    
    return false;
}

NX_INLINE PartitionFileSystemEntry *pfsGetEntryByName(PartitionFileSystemContext *ctx, const char *name)
{
    u32 idx = 0;
    if (!pfsGetEntryIndexByName(ctx, name, &idx)) return NULL;
    return pfsGetEntryByIndex(ctx, idx);
}

NX_INLINE bool pfsGetTotalDataSize(PartitionFileSystemContext *ctx, u64 *out_size)
{
    u64 total_size = 0;
    u32 entry_count = pfsGetEntryCount(ctx);
    PartitionFileSystemEntry *fs_entry = NULL;
    if (!entry_count || !out_size) return false;
    
    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = pfsGetEntryByIndex(ctx, i))) return false;
        total_size += fs_entry->size;
    }
    
    *out_size = total_size;
    
    return true;
}

#endif /* __PFS_H__ */
