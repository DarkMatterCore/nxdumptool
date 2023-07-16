/*
 * romfs.h
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

#ifndef __ROMFS_H__
#define __ROMFS_H__

#include "nca_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ROMFS_OLD_HEADER_SIZE       0x28
#define ROMFS_HEADER_SIZE           0x50

#define ROMFS_VOID_ENTRY            UINT32_MAX

#define ROMFS_TABLE_ENTRY_ALIGNMENT 0x4

/// Header used by NCA0 RomFS sections.
typedef struct {
    u32 header_size;                ///< Header size. Must be equal to ROMFS_OLD_HEADER_SIZE.
    u32 directory_bucket_offset;    ///< Directory buckets table offset.
    u32 directory_bucket_size;      ///< Directory buckets table size.
    u32 directory_entry_offset;     ///< Directory entries table offset.
    u32 directory_entry_size;       ///< Directory entries table size.
    u32 file_bucket_offset;         ///< File buckets table offset.
    u32 file_bucket_size;           ///< File buckets table size.
    u32 file_entry_offset;          ///< File entries table offset.
    u32 file_entry_size;            ///< File entries table size.
    u32 body_offset;                ///< File data body offset.
} RomFileSystemInformationOld;

NXDT_ASSERT(RomFileSystemInformationOld, ROMFS_OLD_HEADER_SIZE);

/// Header used by NCA2/NCA3 RomFS sections.
typedef struct {
    u64 header_size;                ///< Header size. Must be equal to ROMFS_HEADER_SIZE.
    u64 directory_bucket_offset;    ///< Directory buckets table offset.
    u64 directory_bucket_size;      ///< Directory buckets table size.
    u64 directory_entry_offset;     ///< Directory entries table offset.
    u64 directory_entry_size;       ///< Directory entries table size.
    u64 file_bucket_offset;         ///< File buckets table offset.
    u64 file_bucket_size;           ///< File buckets table size.
    u64 file_entry_offset;          ///< File entries table offset.
    u64 file_entry_size;            ///< File entries table size.
    u64 body_offset;                ///< File data body offset.
} RomFileSystemInformation;

NXDT_ASSERT(RomFileSystemInformation, ROMFS_HEADER_SIZE);

/// Header union.
typedef struct {
    union {
        struct {
            RomFileSystemInformationOld old_format;
            u8 padding[ROMFS_OLD_HEADER_SIZE];
        };
        RomFileSystemInformation cur_format;
    };
} RomFileSystemHeader;

NXDT_ASSERT(RomFileSystemHeader, ROMFS_HEADER_SIZE);

/// Directory entry. Always aligned to a ROMFS_TABLE_ENTRY_ALIGNMENT boundary past the directory name.
typedef struct {
    u32 parent_offset;      ///< Parent directory offset.
    u32 next_offset;        ///< Next sibling directory offset. May be set to ROMFS_VOID_ENTRY if there are no other directory entries at this level.
    u32 directory_offset;   ///< First child directory offset. May be set to ROMFS_VOID_ENTRY if there are no child directories entries.
    u32 file_offset;        ///< First child file offset. May be set to ROMFS_VOID_ENTRY if there are no child file entries.
    u32 bucket_offset;      ///< Directory bucket offset.
    u32 name_length;        ///< Name length.
    char name[];            ///< Name (UTF-8, may not be NULL terminated depending on the whole entry alignment).
} RomFileSystemDirectoryEntry;

NXDT_ASSERT(RomFileSystemDirectoryEntry, 0x18);

/// Directory entry. Always aligned to a ROMFS_TABLE_ENTRY_ALIGNMENT boundary past the file name.
typedef struct {
    u32 parent_offset;      ///< Parent directory offset.
    u32 next_offset;        ///< Next sibling file offset. May be set to ROMFS_VOID_ENTRY if there are no other file entries at this level.
    u64 offset;             ///< File data offset.
    u64 size;               ///< File data size.
    u32 bucket_offset;      ///< File bucket offset.
    u32 name_length;        ///< Name length.
    char name[];            ///< Name (UTF-8, may not be NULL terminated depending on the whole entry alignment).
} RomFileSystemFileEntry;

NXDT_ASSERT(RomFileSystemFileEntry, 0x20);

typedef struct {
    bool is_patch;                          ///< Set to true if this we're dealing with a Patch RomFS.
    NcaStorageContext storage_ctx[2];       ///< Used to read NCA FS section data. Index 0: base storage. Index 1: patch storage.
    NcaStorageContext *default_storage_ctx; ///< Default NCA storage context. Points to one of the two contexts from 'storage_ctx'. Placed here for convenience.
    u64 offset;                             ///< RomFS offset (relative to the start of the NCA FS section).
    u64 size;                               ///< RomFS size.
    RomFileSystemHeader header;             ///< RomFS header.
    u64 dir_table_size;                     ///< RomFS directory entries table size.
    RomFileSystemDirectoryEntry *dir_table; ///< RomFS directory entries table.
    u64 file_table_size;                    ///< RomFS file entries table size.
    RomFileSystemFileEntry *file_table;     ///< RomFS file entries table.
    u64 body_offset;                        ///< RomFS file data body offset (relative to the start of the RomFS).
    u64 cur_dir_offset;                     ///< Current RomFS directory offset (relative to the start of the directory entries table). Used for RomFS browsing.
    u64 cur_file_offset;                    ///< Current RomFS file offset (relative to the start of the file entries table). Used for RomFS browsing.
} RomFileSystemContext;

typedef struct {
    bool use_old_format_patch;                      ///< Old format patch flag.
    bool written;                                   ///< Set to true if the patch has been completely written.
    NcaHierarchicalSha256Patch old_format_patch;    ///< Used with NCA0 RomFS sections.
    NcaHierarchicalIntegrityPatch cur_format_patch; ///< Used with NCA2/NCA3 RomFS sections.
} RomFileSystemFileEntryPatch;

typedef enum {
    RomFileSystemPathIllegalCharReplaceType_None               = 0,
    RomFileSystemPathIllegalCharReplaceType_IllegalFsChars     = 1,
    RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly = 2,
    RomFileSystemPathIllegalCharReplaceType_Count              = 3  ///< Total values supported by this enum.
} RomFileSystemPathIllegalCharReplaceType;

/// Initializes a RomFS or Patch RomFS context.
/// 'base_nca_fs_ctx' must always be provided.
/// 'patch_nca_fs_ctx' shall be NULL if not dealing with a Patch RomFS.
bool romfsInitializeContext(RomFileSystemContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *patch_nca_fs_ctx);

/// Reads raw filesystem data using a RomFS context.
/// Input offset must be relative to the start of the RomFS.
bool romfsReadFileSystemData(RomFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved RomFileSystemFileEntry using a RomFS context.
/// Input offset must be relative to the start of the RomFS file entry data.
bool romfsReadFileEntryData(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset);

/// Calculates the extracted RomFS size.
/// If 'only_updated' is set to true and the provided RomFS context was initialized as a Patch RomFS context, only files modified by the update will be considered.
bool romfsGetTotalDataSize(RomFileSystemContext *ctx, bool only_updated, u64 *out_size);

/// Calculates the extracted size from a RomFS directory.
bool romfsGetDirectoryDataSize(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, u64 *out_size);

/// Retrieves a RomFS directory entry by path.
/// Input path must have a leading slash ('/'). If just a single slash is provided, a pointer to the root directory entry shall be returned.
RomFileSystemDirectoryEntry *romfsGetDirectoryEntryByPath(RomFileSystemContext *ctx, const char *path);

/// Retrieves a RomFS file entry by path.
/// Input path must have a leading slash ('/').
RomFileSystemFileEntry *romfsGetFileEntryByPath(RomFileSystemContext *ctx, const char *path);

/// Generates a path string from a RomFS directory entry.
bool romfsGeneratePathFromDirectoryEntry(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type);

/// Generates a path string from a RomFS file entry.
bool romfsGeneratePathFromFileEntry(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type);

/// Checks if a RomFS file entry is updated by the Patch RomFS.
/// Only works if the provided RomFileSystemContext was initialized as a Patch RomFS context.
bool romfsIsFileEntryUpdated(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, bool *out);

/// Generates HierarchicalSha256 (NCA0) / HierarchicalIntegrity (NCA2/NCA3) FS section patch data using a RomFS context + file entry, which can be used to seamlessly replace NCA data.
/// Input offset must be relative to the start of the RomFS file entry data.
/// This function shares the same limitations as ncaGenerateHierarchicalSha256Patch() / ncaGenerateHierarchicalIntegrityPatch().
/// Use the romfsWriteFileEntryPatchToMemoryBuffer() wrapper to write patch data generated by this function.
bool romfsGenerateFileEntryPatch(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, const void *data, u64 data_size, u64 data_offset, RomFileSystemFileEntryPatch *out);

/// Resets a previously initialized RomFileSystemContext.
NX_INLINE void romfsFreeContext(RomFileSystemContext *ctx)
{
    if (!ctx) return;
    ncaStorageFreeContext(&(ctx->storage_ctx[0]));
    ncaStorageFreeContext(&(ctx->storage_ctx[1]));
    if (ctx->dir_table) free(ctx->dir_table);
    if (ctx->file_table) free(ctx->file_table);
    memset(ctx, 0, sizeof(RomFileSystemContext));
}

/// Functions to reset the current directory/file entry offset.

NX_INLINE void romfsResetDirectoryTableOffset(RomFileSystemContext *ctx)
{
    if (ctx) ctx->cur_dir_offset = 0;
}

NX_INLINE void romfsResetFileTableOffset(RomFileSystemContext *ctx)
{
    if (ctx) ctx->cur_file_offset = 0;
}

/// Checks if the provided RomFileSystemContext is valid.
NX_INLINE bool romfsIsValidContext(RomFileSystemContext *ctx)
{
    return (ctx && ncaStorageIsValidContext(ctx->default_storage_ctx) && ctx->size && ctx->dir_table_size && ctx->dir_table && ctx->file_table_size && ctx->file_table && \
            ctx->body_offset >= ctx->header.old_format.header_size && ctx->body_offset < ctx->size);
}

/// Functions to retrieve a directory/file entry.

NX_INLINE void *romfsGetEntryByOffset(RomFileSystemContext *ctx, void *entry_table, u64 entry_table_size, u64 entry_size, u64 entry_offset)
{
    if (!romfsIsValidContext(ctx) || !entry_table || !entry_table_size || !entry_size || (entry_offset + entry_size) > entry_table_size) return NULL;
    return ((u8*)entry_table + entry_offset);
}

NX_INLINE RomFileSystemDirectoryEntry *romfsGetDirectoryEntryByOffset(RomFileSystemContext *ctx, u64 dir_entry_offset)
{
    return (ctx ? (RomFileSystemDirectoryEntry*)romfsGetEntryByOffset(ctx, ctx->dir_table, ctx->dir_table_size, sizeof(RomFileSystemDirectoryEntry), dir_entry_offset) : NULL);
}

NX_INLINE RomFileSystemDirectoryEntry *romfsGetCurrentDirectoryEntry(RomFileSystemContext *ctx)
{
    return (ctx ? romfsGetDirectoryEntryByOffset(ctx, ctx->cur_dir_offset) : NULL);
}

NX_INLINE RomFileSystemFileEntry *romfsGetFileEntryByOffset(RomFileSystemContext *ctx, u64 file_entry_offset)
{
    return (ctx ? (RomFileSystemFileEntry*)romfsGetEntryByOffset(ctx, ctx->file_table, ctx->file_table_size, sizeof(RomFileSystemFileEntry), file_entry_offset) : NULL);
}

NX_INLINE RomFileSystemFileEntry *romfsGetCurrentFileEntry(RomFileSystemContext *ctx)
{
    return (ctx ? romfsGetFileEntryByOffset(ctx, ctx->cur_file_offset) : NULL);
}

/// Functions to check if it's possible to move to the next directory/file entry based on the current directory/file entry offset.

NX_INLINE bool romfsCanMoveToNextEntry(RomFileSystemContext *ctx, void *entry_table, u64 entry_table_size, u64 entry_size, u64 entry_offset)
{
    if (!romfsIsValidContext(ctx) || !entry_table || !entry_table_size || entry_size < 4 || (entry_offset + entry_size) > entry_table_size) return false;
    u32 name_length = *((u32*)((u8*)entry_table + entry_offset + entry_size - 4));
    return ((entry_offset + ALIGN_UP(entry_size + name_length, ROMFS_TABLE_ENTRY_ALIGNMENT)) <= entry_table_size);
}

NX_INLINE bool romfsCanMoveToNextDirectoryEntry(RomFileSystemContext *ctx)
{
    return (ctx ? romfsCanMoveToNextEntry(ctx, ctx->dir_table, ctx->dir_table_size, sizeof(RomFileSystemDirectoryEntry), ctx->cur_dir_offset) : false);
}

NX_INLINE bool romfsCanMoveToNextFileEntry(RomFileSystemContext *ctx)
{
    return (ctx ? romfsCanMoveToNextEntry(ctx, ctx->file_table, ctx->file_table_size, sizeof(RomFileSystemFileEntry), ctx->cur_file_offset) : false);
}

/// Functions to update the current directory/file entry offset to make it point to the next directory/file entry.

NX_INLINE bool romfsMoveToNextEntry(RomFileSystemContext *ctx, void *entry_table, u64 entry_table_size, u64 entry_size, u64 *entry_offset)
{
    if (!romfsIsValidContext(ctx) || !entry_table || !entry_table_size || entry_size < 4 || !entry_offset || (*entry_offset + entry_size) > entry_table_size) return false;
    u32 name_length = *((u32*)((u8*)entry_table + *entry_offset + entry_size - 4));
    *entry_offset += ALIGN_UP(entry_size + name_length, ROMFS_TABLE_ENTRY_ALIGNMENT);
    return true;
}

NX_INLINE bool romfsMoveToNextDirectoryEntry(RomFileSystemContext *ctx)
{
    return (ctx ? romfsMoveToNextEntry(ctx, ctx->dir_table, ctx->dir_table_size, sizeof(RomFileSystemDirectoryEntry), &(ctx->cur_dir_offset)) : false);
}

NX_INLINE bool romfsMoveToNextFileEntry(RomFileSystemContext *ctx)
{
    return (ctx ? romfsMoveToNextEntry(ctx, ctx->file_table, ctx->file_table_size, sizeof(RomFileSystemFileEntry), &(ctx->cur_file_offset)) : false);
}

/// NCA patch management functions.

NX_INLINE void romfsWriteFileEntryPatchToMemoryBuffer(RomFileSystemContext *ctx, RomFileSystemFileEntryPatch *patch, void *buf, u64 buf_size, u64 buf_offset)
{
    if (!romfsIsValidContext(ctx) || ctx->is_patch || ctx->default_storage_ctx->base_storage_type != NcaStorageBaseStorageType_Regular || !patch || \
        (!patch->use_old_format_patch && ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_RomFs) || \
        (patch->use_old_format_patch && ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_Nca0RomFs)) return;

    NcaContext *nca_ctx = ctx->default_storage_ctx->nca_fs_ctx->nca_ctx;

    if (patch->use_old_format_patch)
    {
        ncaWriteHierarchicalSha256PatchToMemoryBuffer(nca_ctx, &(patch->old_format_patch), buf, buf_size, buf_offset);
        patch->written = patch->old_format_patch.written;
    } else {
        ncaWriteHierarchicalIntegrityPatchToMemoryBuffer(nca_ctx, &(patch->cur_format_patch), buf, buf_size, buf_offset);
        patch->written = patch->cur_format_patch.written;
    }
}

NX_INLINE void romfsFreeFileEntryPatch(RomFileSystemFileEntryPatch *patch)
{
    if (!patch) return;
    ncaFreeHierarchicalSha256Patch(&(patch->old_format_patch));
    ncaFreeHierarchicalIntegrityPatch(&(patch->cur_format_patch));
    memset(patch, 0, sizeof(RomFileSystemFileEntryPatch));
}

#ifdef __cplusplus
}
#endif

#endif /* __ROMFS_H__ */
