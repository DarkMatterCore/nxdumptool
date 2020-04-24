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

#ifndef __PFS0_H__
#define __PFS0_H__

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
    NcaFsSectionContext *nca_fs_ctx;            ///< Used to read NCA FS section data.
    NcaHierarchicalSha256 *hash_info;           ///< Hash table information.
    u64 offset;                                 ///< Partition offset (relative to the start of the NCA FS section).
    u64 size;                                   ///< Partition size.
    bool is_exefs;                              ///< ExeFS flag.
    u64 header_size;                            ///< Full header size.
    u8 *header;                                 ///< PartitionFileSystemHeader + (PartitionFileSystemEntry * entry_count) + Name Table.
} PartitionFileSystemContext;

/// Initializes a PFS0 context.
bool pfs0InitializeContext(PartitionFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx);

/// Cleanups a previously initialized PFS0 context.
static inline void pfs0FreeContext(PartitionFileSystemContext *ctx)
{
    if (!ctx) return;
    if (ctx->header) free(ctx->header);
    memset(ctx, 0, sizeof(PartitionFileSystemContext));
}

/// Miscellaneous functions.

static inline u32 pfs0GetEntryCount(PartitionFileSystemContext *ctx)
{
    if (!ctx || !ctx->header_size || !ctx->header) return 0;
    return ((PartitionFileSystemHeader*)ctx->header)->entry_count;
}

static inline bool pfs0GetEntryDataOffset(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, u64 *out_offset)
{
    if (!ctx || !ctx->header_size || !ctx->header || !fs_entry || !out_offset) return false;
    *out_offset = (ctx->offset + ctx->header_size + fs_entry->offset);  /* Relative to the start of the NCA FS section */
    return true;
}

static inline PartitionFileSystemEntry *pfs0GetEntryByIndex(PartitionFileSystemContext *ctx, u32 idx)
{
    if (idx >= pfs0GetEntryCount(ctx)) return NULL;
    return (PartitionFileSystemEntry*)(ctx->header + sizeof(PartitionFileSystemHeader) + (idx * sizeof(PartitionFileSystemEntry)));
}

static inline char *pfs0GetNameTable(PartitionFileSystemContext *ctx)
{
    u32 entry_count = pfs0GetEntryCount(ctx);
    if (!entry_count) return NULL;
    return (char*)(ctx->header + sizeof(PartitionFileSystemHeader) + (entry_count * sizeof(PartitionFileSystemEntry)));
}

static inline char *pfs0GetEntryNameByIndex(PartitionFileSystemContext *ctx, u32 idx)
{
    PartitionFileSystemEntry *fs_entry = pfs0GetEntryByIndex(ctx, idx);
    char *name_table = pfs0GetNameTable(ctx);
    if (!fs_entry || !name_table) return NULL;
    return (name_table + fs_entry->name_offset);
}

static inline bool pfs0GetEntryIndexByName(PartitionFileSystemContext *ctx, const char *name, u32 *out_idx)
{
    size_t name_len = 0;
    u32 entry_count = pfs0GetEntryCount(ctx);
    char *name_table = pfs0GetNameTable(ctx);
    if (!entry_count || !name_table || !name || !(name_len = strlen(name)) || !out_idx) return false;
    
    for(u32 i = 0; i < entry_count; i++)
    {
        PartitionFileSystemEntry *fs_entry = pfs0GetEntryByIndex(ctx, i);
        if (!fs_entry) continue;
        
        if (!strncmp(name_table + fs_entry->name_offset, name, name_len))
        {
            *out_idx = i;
            return true;
        }
    }
    
    return false;
}

static inline PartitionFileSystemEntry *pfs0GetEntryByName(PartitionFileSystemContext *ctx, const char *name)
{
    u32 idx = 0;
    if (!pfs0GetEntryIndexByName(ctx, name, &idx)) return NULL;
    return pfs0GetEntryByIndex(ctx, idx);
}

#endif /* __PFS0_H__ */
