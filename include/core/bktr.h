/*
 * bktr.h
 *
 * Copyright (c) 2018-2020, SciresM.
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __BKTR_H__
#define __BKTR_H__

#include "romfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BktrIndirectStorageIndex_Original = 0,
    BktrIndirectStorageIndex_Patch    = 1
} BktrIndirectStorageIndex;

#pragma pack(push, 1)
typedef struct {
    u64 virtual_offset;
    u64 physical_offset;
    u32 indirect_storage_index; ///< BktrIndirectStorageIndex.
} BktrIndirectStorageEntry;
#pragma pack(pop)

NXDT_ASSERT(BktrIndirectStorageEntry, 0x14);

typedef struct {
    u32 index;
    u32 entry_count;
    u64 end_offset;
    BktrIndirectStorageEntry indirect_storage_entries[0x3FF0 / sizeof(BktrIndirectStorageEntry)];
    u8 reserved[0x3FF0 % sizeof(BktrIndirectStorageEntry)];
} BktrIndirectStorageBucket;

NXDT_ASSERT(BktrIndirectStorageBucket, 0x4000);

typedef struct {
    u32 index;
    u32 bucket_count;
    u64 virtual_size;
    u64 virtual_offsets[0x3FF0 / sizeof(u64)];
    BktrIndirectStorageBucket indirect_storage_buckets[];
} BktrIndirectStorageBlock;

NXDT_ASSERT(BktrIndirectStorageBlock, 0x4000);

typedef struct {
    u64 offset;
    u32 size;
    u32 generation;
} BktrAesCtrExStorageEntry;

NXDT_ASSERT(BktrAesCtrExStorageEntry, 0x10);

typedef struct {
    u32 index;
    u32 entry_count;
    u64 end_offset;
    BktrAesCtrExStorageEntry aes_ctr_ex_storage_entries[0x3FF];
} BktrAesCtrExStorageBucket;

NXDT_ASSERT(BktrAesCtrExStorageBucket, 0x4000);

typedef struct {
    u32 index;
    u32 bucket_count;
    u64 physical_size;
    u64 physical_offsets[0x3FF0 / sizeof(u64)];
    BktrAesCtrExStorageBucket aes_ctr_ex_storage_buckets[];
} BktrAesCtrExStorageBlock;

NXDT_ASSERT(BktrAesCtrExStorageBlock, 0x4000);

typedef struct {
    RomFileSystemContext base_romfs_ctx;        ///< Base NCA RomFS context.
    RomFileSystemContext patch_romfs_ctx;       ///< Update NCA RomFS context. Must be used with RomFS directory/file entry functions, because it holds the updated directory/file tables.
    u64 offset;                                 ///< Patched RomFS image offset (relative to the start of the update NCA FS section).
    u64 size;                                   ///< Patched RomFS image size.
    u64 body_offset;                            ///< Patched RomFS image file data body offset (relative to the start of the RomFS).
    BktrIndirectStorageBlock *indirect_block;   ///< BKTR Indirect Storage Block.
    BktrAesCtrExStorageBlock *aes_ctr_ex_block; ///< BKTR AesCtrEx Storage Block.
    bool missing_base_romfs;                    ///< If true, only Patch RomFS data is used. Needed for games with base Program NCAs without a RomFS section (e.g. Fortnite, World of Tanks Blitz, etc.).
} BktrContext;

/// Initializes a BKTR context.
bool bktrInitializeContext(BktrContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *update_nca_fs_ctx);

/// Reads raw filesystem data using a BKTR context.
/// Input offset must be relative to the start of the patched RomFS image.
bool bktrReadFileSystemData(BktrContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved RomFileSystemFileEntry using a BKTR context.
/// Input offset must be relative to the start of the RomFS file entry data.
bool bktrReadFileEntryData(BktrContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset);

/// Checks if a RomFS file entry is updated by the Patch RomFS.
bool bktrIsFileEntryUpdated(BktrContext *ctx, RomFileSystemFileEntry *file_entry, bool *out);

/// Miscellaneous functions.

NX_INLINE void bktrFreeContext(BktrContext *ctx)
{
    if (!ctx) return;
    romfsFreeContext(&(ctx->base_romfs_ctx));
    romfsFreeContext(&(ctx->patch_romfs_ctx));
    if (ctx->indirect_block) free(ctx->indirect_block);
    if (ctx->aes_ctr_ex_block) free(ctx->aes_ctr_ex_block);
    memset(ctx, 0, sizeof(BktrContext));
}

NX_INLINE RomFileSystemDirectoryEntry *bktrGetDirectoryEntryByOffset(BktrContext *ctx, u32 dir_entry_offset)
{
    return (ctx ? romfsGetDirectoryEntryByOffset(&(ctx->patch_romfs_ctx), dir_entry_offset) : NULL);
}

NX_INLINE RomFileSystemFileEntry *bktrGetFileEntryByOffset(BktrContext *ctx, u32 file_entry_offset)
{
    return (ctx ? romfsGetFileEntryByOffset(&(ctx->patch_romfs_ctx), file_entry_offset) : NULL);
}

NX_INLINE bool bktrGetTotalDataSize(BktrContext *ctx, u64 *out_size)
{
    return (ctx ? romfsGetTotalDataSize(&(ctx->patch_romfs_ctx), out_size) : false);
}

NX_INLINE bool bktrGetDirectoryDataSize(BktrContext *ctx, RomFileSystemDirectoryEntry *dir_entry, u64 *out_size)
{
    return (ctx ? romfsGetDirectoryDataSize(&(ctx->patch_romfs_ctx), dir_entry, out_size) : false);
}

NX_INLINE RomFileSystemDirectoryEntry *bktrGetDirectoryEntryByPath(BktrContext *ctx, const char *path)
{
    return (ctx ? romfsGetDirectoryEntryByPath(&(ctx->patch_romfs_ctx), path) : NULL);
}

NX_INLINE RomFileSystemFileEntry *bktrGetFileEntryByPath(BktrContext *ctx, const char *path)
{
    return (ctx ? romfsGetFileEntryByPath(&(ctx->patch_romfs_ctx), path) : NULL);
}

NX_INLINE bool bktrGeneratePathFromDirectoryEntry(BktrContext *ctx, RomFileSystemDirectoryEntry *dir_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    return (ctx ? romfsGeneratePathFromDirectoryEntry(&(ctx->patch_romfs_ctx), dir_entry, out_path, out_path_size, illegal_char_replace_type) : false);
}

NX_INLINE bool bktrGeneratePathFromFileEntry(BktrContext *ctx, RomFileSystemFileEntry *file_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    return (ctx ? romfsGeneratePathFromFileEntry(&(ctx->patch_romfs_ctx), file_entry, out_path, out_path_size, illegal_char_replace_type) : false);
}

#ifdef __cplusplus
}
#endif

#endif /* __BKTR_H__ */
