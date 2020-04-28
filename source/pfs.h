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
    u64 offset;                         ///< Partition offset (relative to the start of the NCA FS section).
    u64 size;                           ///< Partition size.
    bool is_exefs;                      ///< ExeFS flag.
    u64 header_size;                    ///< Full header size.
    u8 *header;                         ///< PartitionFileSystemHeader + (PartitionFileSystemEntry * entry_count) + Name Table.
} PartitionFileSystemContext;

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
/// Input offset must be relative to the start of the partition FS.
bool pfsReadPartitionData(PartitionFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved PartitionFileSystemEntry using a partition FS context.
/// Input offset must be relative to the start of the partition FS entry.
bool pfsReadEntryData(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset);

/// Generates HierarchicalSha256 FS section patch data using a partition FS context + entry, which can be used to replace NCA data in content dumping operations.
/// Input offset must be relative to the start of the partition FS entry data.
/// This function shares the same limitations as ncaGenerateHierarchicalSha256Patch().
bool pfsGenerateEntryPatch(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out);

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
