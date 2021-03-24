/*
 * hfs.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#pragma once

#ifndef __HFS_H__
#define __HFS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define HFS0_MAGIC  0x48465330  /* "HFS0". */

typedef struct {
    u32 magic;              ///< "HFS0".
    u32 entry_count;
    u32 name_table_size;
    u8 reserved[0x4];
} HashFileSystemHeader;

NXDT_ASSERT(HashFileSystemHeader, 0x10);

typedef struct {
    u64 offset;
    u64 size;
    u32 name_offset;
    u32 hash_target_size;
    u64 hash_target_offset;
    u8 hash[SHA256_HASH_SIZE];
} HashFileSystemEntry;

NXDT_ASSERT(HashFileSystemEntry, 0x40);

/// Internally used by gamecard functions.
/// Use gamecardGetHashFileSystemContext() to retrieve a Hash FS context.
typedef struct {
    u8 type;            ///< GameCardHashFileSystemPartitionType.
    char *name;         ///< Dynamically allocated partition name.
    u64 offset;         ///< Partition offset (relative to the start of gamecard image).
    u64 size;           ///< Partition size.
    u64 header_size;    ///< Full header size.
    u8 *header;         ///< HashFileSystemHeader + (HashFileSystemEntry * entry_count) + Name Table.
} HashFileSystemContext;

/// Retrieves a Hash FS entry index by its name.
bool hfsGetEntryIndexByName(HashFileSystemContext *ctx, const char *name, u32 *out_idx);

/// Reads raw partition data using a Hash FS context.
/// Input offset must be relative to the start of the Hash FS.
bool hfsReadPartitionData(HashFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved HashFileSystemEntry using a Hash FS context.
/// Input offset must be relative to the start of the Hash FS entry.
bool hfsReadEntryData(HashFileSystemContext *ctx, HashFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset);

/// Calculates the extracted Hash FS size.
bool hfsGetTotalDataSize(HashFileSystemContext *ctx, u64 *out_size);

/// Miscellaneous functions.

NX_INLINE void hfsFreeContext(HashFileSystemContext *ctx)
{
    if (!ctx) return;
    if (ctx->name) free(ctx->name);
    if (ctx->header) free(ctx->header);
    memset(ctx, 0, sizeof(HashFileSystemContext));
}

NX_INLINE u32 hfsGetEntryCount(HashFileSystemContext *ctx)
{
    if (!ctx || !ctx->header_size || !ctx->header) return 0;
    return ((HashFileSystemHeader*)ctx->header)->entry_count;
}

NX_INLINE HashFileSystemEntry *hfsGetEntryByIndex(HashFileSystemContext *ctx, u32 idx)
{
    if (idx >= hfsGetEntryCount(ctx)) return NULL;
    return (HashFileSystemEntry*)(ctx->header + sizeof(HashFileSystemHeader) + (idx * sizeof(HashFileSystemEntry)));
}

NX_INLINE char *hfsGetNameTable(HashFileSystemContext *ctx)
{
    u32 entry_count = hfsGetEntryCount(ctx);
    if (!entry_count) return NULL;
    return (char*)(ctx->header + sizeof(HashFileSystemHeader) + (entry_count * sizeof(HashFileSystemEntry)));
}

NX_INLINE char *hfsGetEntryName(HashFileSystemContext *ctx, HashFileSystemEntry *fs_entry)
{
    char *name_table = hfsGetNameTable(ctx);
    if (!name_table || !fs_entry || fs_entry->name_offset >= ((HashFileSystemHeader*)ctx->header)->name_table_size || !name_table[fs_entry->name_offset]) return NULL;
    return (name_table + fs_entry->name_offset);
}

NX_INLINE char *hfsGetEntryNameByIndex(HashFileSystemContext *ctx, u32 idx)
{
    HashFileSystemEntry *fs_entry = hfsGetEntryByIndex(ctx, idx);
    char *name_table = hfsGetNameTable(ctx);
    if (!fs_entry || !name_table) return NULL;
    return (name_table + fs_entry->name_offset);
}

NX_INLINE HashFileSystemEntry *hfsGetEntryByName(HashFileSystemContext *ctx, const char *name)
{
    u32 idx = 0;
    if (!hfsGetEntryIndexByName(ctx, name, &idx)) return NULL;
    return hfsGetEntryByIndex(ctx, idx);
}

#ifdef __cplusplus
}
#endif

#endif /* __HFS_H__ */
