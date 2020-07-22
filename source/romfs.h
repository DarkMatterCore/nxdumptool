/*
 * romfs.h
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

#pragma once

#ifndef __ROMFS_H__
#define __ROMFS_H__

#include "nca.h"

#define ROMFS_OLD_HEADER_SIZE   0x28
#define ROMFS_HEADER_SIZE       0x50

#define ROMFS_VOID_ENTRY        0xFFFFFFFF

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

/// Directory entry. Always aligned to a 4-byte boundary past the directory name.
typedef struct {
    u32 parent_offset;      ///< Parent directory offset.
    u32 next_offset;        ///< Next sibling directory offset.
    u32 directory_offset;   ///< First child directory offset.
    u32 file_offset;        ///< First child file offset.
    u32 bucket_offset;      ///< Directory bucket offset.
    u32 name_length;        ///< Name length.
    char name[];            ///< Name (UTF-8).
} RomFileSystemDirectoryEntry;

/// Directory entry. Always aligned to a 4-byte boundary past the file name.
typedef struct {
    u32 parent_offset;      ///< Parent directory offset.
    u32 next_offset;        ///< Next sibling file offset.
    u64 offset;             ///< File data offset.
    u64 size;               ///< File data size.
    u32 bucket_offset;      ///< File bucket offset.
    u32 name_length;        ///< Name length.
    char name[];            ///< Name (UTF-8).
} RomFileSystemFileEntry;

typedef struct {
    NcaFsSectionContext *nca_fs_ctx;                ///< Used to read NCA FS section data.
    u64 offset;                                     ///< RomFS offset (relative to the start of the NCA FS section).
    u64 size;                                       ///< RomFS size.
    RomFileSystemHeader header;                     ///< RomFS header.
    u64 dir_table_size;                             ///< RomFS directory entries table size.
    RomFileSystemDirectoryEntry *dir_table;         ///< RomFS directory entries table.
    u64 file_table_size;                            ///< RomFS file entries table size.
    RomFileSystemFileEntry *file_table;             ///< RomFS file entries table.
    u64 body_offset;                                ///< RomFS file data body offset (relative to the start of the RomFS).
    u32 cur_dir_offset;                             ///< Current RomFS directory offset (relative to the start of the directory entries table). Used for RomFS browsing.
} RomFileSystemContext;

typedef struct {
    bool use_old_format_patch;                      ///< Old format patch flag.
    NcaHierarchicalSha256Patch old_format_patch;    ///< Used with NCA0 RomFS sections.
    NcaHierarchicalIntegrityPatch cur_format_patch; ///< Used with NCA2/NCA3 RomFS sections.
} RomFileSystemFileEntryPatch;

typedef enum {
    RomFileSystemPathIllegalCharReplaceType_None               = 0,
    RomFileSystemPathIllegalCharReplaceType_IllegalFsChars     = 1,
    RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly = 2
} RomFileSystemPathIllegalCharReplaceType;

/// Initializes a RomFS context.
bool romfsInitializeContext(RomFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx);

/// Cleanups a previously initialized RomFS context.
NX_INLINE void romfsFreeContext(RomFileSystemContext *ctx)
{
    if (!ctx) return;
    if (ctx->dir_table) free(ctx->dir_table);
    if (ctx->file_table) free(ctx->file_table);
    memset(ctx, 0, sizeof(RomFileSystemContext));
}

/// Reads raw filesystem data using a RomFS context.
/// Input offset must be relative to the start of the RomFS.
bool romfsReadFileSystemData(RomFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved RomFileSystemFileEntry using a RomFS context.
/// Input offset must be relative to the start of the RomFS file entry data.
bool romfsReadFileEntryData(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset);

/// Calculates the extracted RomFS size.
bool romfsGetTotalDataSize(RomFileSystemContext *ctx, u64 *out_size);

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

/// Generates HierarchicalSha256 (NCA0) / HierarchicalIntegrity (NCA2/NCA3) FS section patch data using a RomFS context + file entry, which can be used to seamlessly replace NCA data.
/// Input offset must be relative to the start of the RomFS file entry data.
/// This function shares the same limitations as ncaGenerateHierarchicalSha256Patch() / ncaGenerateHierarchicalIntegrityPatch().
bool romfsGenerateFileEntryPatch(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, const void *data, u64 data_size, u64 data_offset, RomFileSystemFileEntryPatch *out);

/// Cleanups a previously generated RomFS file entry patch.
NX_INLINE void romfsFreeFileEntryPatch(RomFileSystemFileEntryPatch *patch)
{
    if (!patch) return;
    ncaFreeHierarchicalSha256Patch(&(patch->old_format_patch));
    ncaFreeHierarchicalIntegrityPatch(&(patch->cur_format_patch));
    memset(patch, 0, sizeof(RomFileSystemFileEntryPatch));
}

/// Miscellaneous functions.

NX_INLINE RomFileSystemDirectoryEntry *romfsGetDirectoryEntryByOffset(RomFileSystemContext *ctx, u32 dir_entry_offset)
{
    if (!ctx || !ctx->dir_table || (dir_entry_offset + sizeof(RomFileSystemDirectoryEntry)) > ctx->dir_table_size) return NULL;
    return (RomFileSystemDirectoryEntry*)((u8*)ctx->dir_table + dir_entry_offset);
}

NX_INLINE RomFileSystemFileEntry *romfsGetFileEntryByOffset(RomFileSystemContext *ctx, u32 file_entry_offset)
{
    if (!ctx || !ctx->file_table || (file_entry_offset + sizeof(RomFileSystemFileEntry)) > ctx->file_table_size) return NULL;
    return (RomFileSystemFileEntry*)((u8*)ctx->file_table + file_entry_offset);
}

#endif /* __ROMFS_H__ */
