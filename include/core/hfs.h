/*
 * hfs.h
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

typedef enum {
    HashFileSystemPartitionType_None   = 0, ///< Not a real value.
    HashFileSystemPartitionType_Root   = 1,
    HashFileSystemPartitionType_Update = 2,
    HashFileSystemPartitionType_Logo   = 3, ///< Only available in GameCardFwVersion_Since400NUP or greater gamecards.
    HashFileSystemPartitionType_Normal = 4,
    HashFileSystemPartitionType_Secure = 5,
    HashFileSystemPartitionType_Count  = 6  ///< Total values supported by this enum.
} HashFileSystemPartitionType;

/// Internally used by gamecard functions.
/// Use gamecardGetHashFileSystemContext() to retrieve a Hash FS context.
typedef struct {
    u8 type;            ///< HashFileSystemPartitionType.
    char *name;         ///< Dynamically allocated partition name.
    u64 offset;         ///< Partition offset (relative to the start of gamecard image).
    u64 size;           ///< Partition size.
    u64 header_size;    ///< Full header size.
    u8 *header;         ///< HashFileSystemHeader + (HashFileSystemEntry * entry_count) + Name Table.
} HashFileSystemContext;

/// Reads raw partition data using a Hash FS context.
/// Input offset must be relative to the start of the Hash FS.
bool hfsReadPartitionData(HashFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved HashFileSystemEntry using a Hash FS context.
/// Input offset must be relative to the start of the Hash FS entry.
bool hfsReadEntryData(HashFileSystemContext *ctx, HashFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset);

/// Calculates the extracted Hash FS size.
/// If the target partition is empty, 'out_size' will be set to zero and true will be returned.
bool hfsGetTotalDataSize(HashFileSystemContext *ctx, u64 *out_size);

/// Retrieves a Hash FS entry index by its name.
bool hfsGetEntryIndexByName(HashFileSystemContext *ctx, const char *name, u32 *out_idx);

/// Takes a HashFileSystemPartitionType value. Returns a pointer to a string that represents the partition name that matches the provided Hash FS partition type.
/// Returns NULL if the provided value is out of range.
const char *hfsGetPartitionNameString(u8 hfs_partition_type);

/// Miscellaneous functions.

NX_INLINE void hfsFreeContext(HashFileSystemContext *ctx)
{
    if (!ctx) return;
    if (ctx->name) free(ctx->name);
    if (ctx->header) free(ctx->header);
    memset(ctx, 0, sizeof(HashFileSystemContext));
}

NX_INLINE bool hfsIsValidContext(HashFileSystemContext *ctx)
{
    return (ctx && ctx->type > HashFileSystemPartitionType_None && ctx->type < HashFileSystemPartitionType_Count && ctx->name && ctx->size && ctx->header_size && ctx->header);
}

NX_INLINE u32 hfsGetEntryCount(HashFileSystemContext *ctx)
{
    return (hfsIsValidContext(ctx) ? ((HashFileSystemHeader*)ctx->header)->entry_count : 0);
}

NX_INLINE HashFileSystemEntry *hfsGetEntryByIndex(HashFileSystemContext *ctx, u32 idx)
{
    return (idx < hfsGetEntryCount(ctx) ? (HashFileSystemEntry*)(ctx->header + sizeof(HashFileSystemHeader) + (idx * sizeof(HashFileSystemEntry))) : NULL);
}

NX_INLINE char *hfsGetNameTable(HashFileSystemContext *ctx)
{
    u32 entry_count = hfsGetEntryCount(ctx);
    return (entry_count ? (char*)(ctx->header + sizeof(HashFileSystemHeader) + (entry_count * sizeof(HashFileSystemEntry))) : NULL);
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
    return ((fs_entry && name_table) ? (name_table + fs_entry->name_offset) : NULL);
}

NX_INLINE HashFileSystemEntry *hfsGetEntryByName(HashFileSystemContext *ctx, const char *name)
{
    u32 idx = 0;
    return (hfsGetEntryIndexByName(ctx, name, &idx) ? hfsGetEntryByIndex(ctx, idx) : NULL);
}

#ifdef __cplusplus
}
#endif

#endif /* __HFS_H__ */
