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

#ifndef __BKTR_H__
#define __BKTR_H__

#include <switch.h>
#include "romfs.h"

typedef enum {
    BktrIndirectStorageIndex_Original = 0,
    BktrIndirectStorageIndex_Patch    = 1
} PACKED BktrIndirectStorageIndex;

typedef struct {
    u64 virtual_offset;
    u64 physical_offset;
    u32 indirect_storage_index; ///< BktrIndirectStorageIndex.
} PACKED BktrIndirectStorageEntry;

typedef struct {
    u32 index;
    u32 entry_count;
    u64 end_offset;
    BktrIndirectStorageEntry indirect_storage_entries[0x3FF0 / sizeof(BktrIndirectStorageEntry)];
    u8 reserved[0x3FF0 % sizeof(BktrIndirectStorageEntry)];
} PACKED BktrIndirectStorageBucket;

typedef struct {
    u32 index;
    u32 bucket_count;
    u64 virtual_size;
    u64 virtual_offsets[0x3FF0 / sizeof(u64)];
    BktrIndirectStorageBucket indirect_storage_buckets[];
} PACKED BktrIndirectStorageBlock;

typedef struct {
    u64 offset;
    u32 size;
    u32 generation;
} PACKED BktrAesCtrExStorageEntry;

typedef struct {
    u32 index;
    u32 entry_count;
    u64 end_offset;
    BktrAesCtrExStorageEntry aes_ctr_ex_storage_entries[0x3FF];
} PACKED BktrAesCtrExStorageBucket;

typedef struct {
    u32 index;
    u32 bucket_count;
    u64 physical_size;
    u64 physical_offsets[0x3FF0 / sizeof(u64)];
    BktrAesCtrExStorageBucket aes_ctr_ex_storage_buckets[];
} PACKED BktrAesCtrExStorageBlock;

typedef struct {
    RomFileSystemContext base_romfs_ctx;        ///< Base NCA RomFS context.
    RomFileSystemContext patch_romfs_ctx;       ///< Update NCA RomFS context. Must be used with RomFS directory/file entry functions, because it holds the updated directory/file tables.
    u64 offset;                                 ///< Patched RomFS image offset (relative to the start of the update NCA FS section).
    u64 size;                                   ///< Patched RomFS image size.
    u64 body_offset;                            ///< Patched RomFS image file data body offset (relative to the start of the RomFS).
    BktrIndirectStorageBlock *indirect_block;   ///< BKTR Indirect Storage Block.
    BktrAesCtrExStorageBlock *aes_ctr_ex_block; ///< BKTR AesCtrEx Storage Block.
} BktrContext;

/// Initializes a BKTR context.
bool bktrInitializeContext(BktrContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *update_nca_fs_ctx);

/// Cleanups a previously initialized BKTR context.
NX_INLINE void bktrFreeContext(BktrContext *ctx)
{
    if (!ctx) return;
    romfsFreeContext(&(ctx->base_romfs_ctx));
    romfsFreeContext(&(ctx->patch_romfs_ctx));
    if (ctx->indirect_block) free(ctx->indirect_block);
    if (ctx->aes_ctr_ex_block) free(ctx->aes_ctr_ex_block);
    memset(ctx, 0, sizeof(BktrContext));
}

/// Reads raw filesystem data using a BKTR context.
/// Input offset must be relative to the start of the patched RomFS image.
bool bktrReadFileSystemData(BktrContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved RomFileSystemFileEntry using a BKTR context.
/// Input offset must be relative to the start of the RomFS file entry data.
bool bktrReadFileEntryData(BktrContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset);

/// Checks if a RomFS file entry is updated by the Patch RomFS.
bool bktrIsFileEntryUpdated(BktrContext *ctx, RomFileSystemFileEntry *file_entry, bool *out);

/// Miscellaneous functions.
/// These are just wrappers for RomFS functions.

NX_INLINE RomFileSystemDirectoryEntry *bktrGetDirectoryEntryByOffset(BktrContext *ctx, u32 dir_entry_offset)
{
    if (!ctx) return NULL;
    return romfsGetDirectoryEntryByOffset(&(ctx->patch_romfs_ctx), dir_entry_offset);
}

NX_INLINE RomFileSystemFileEntry *bktrGetFileEntryByOffset(BktrContext *ctx, u32 file_entry_offset)
{
    if (!ctx) return NULL;
    return romfsGetFileEntryByOffset(&(ctx->patch_romfs_ctx), file_entry_offset);
}

NX_INLINE bool bktrGetTotalDataSize(BktrContext *ctx, u64 *out_size)
{
    if (!ctx) return false;
    return romfsGetTotalDataSize(&(ctx->patch_romfs_ctx), out_size);
}

NX_INLINE bool bktrGetDirectoryDataSize(BktrContext *ctx, RomFileSystemDirectoryEntry *dir_entry, u64 *out_size)
{
    if (!ctx) return false;
    return romfsGetDirectoryDataSize(&(ctx->patch_romfs_ctx), dir_entry, out_size);
}

NX_INLINE RomFileSystemDirectoryEntry *bktrGetDirectoryEntryByPath(BktrContext *ctx, const char *path)
{
    if (!ctx) return NULL;
    return romfsGetDirectoryEntryByPath(&(ctx->patch_romfs_ctx), path);
}

NX_INLINE RomFileSystemFileEntry *bktrGetFileEntryByPath(BktrContext *ctx, const char *path)
{
    if (!ctx) return NULL;
    return romfsGetFileEntryByPath(&(ctx->patch_romfs_ctx), path);
}

NX_INLINE bool bktrGeneratePathFromDirectoryEntry(BktrContext *ctx, RomFileSystemDirectoryEntry *dir_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    if (!ctx) return false;
    return romfsGeneratePathFromDirectoryEntry(&(ctx->patch_romfs_ctx), dir_entry, out_path, out_path_size, illegal_char_replace_type);
}

NX_INLINE bool bktrGeneratePathFromFileEntry(BktrContext *ctx, RomFileSystemFileEntry *file_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    if (!ctx) return false;
    return romfsGeneratePathFromFileEntry(&(ctx->patch_romfs_ctx), file_entry, out_path, out_path_size, illegal_char_replace_type);
}

#endif /* __BKTR_H__ */
