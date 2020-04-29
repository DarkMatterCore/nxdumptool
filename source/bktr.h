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
} BktrIndirectStorageIndex;

typedef struct {
    u64 virtual_offset;
    u64 physical_offset;
    u32 indirect_storage_index; ///< BktrIndirectStorageIndex.
} BktrIndirectStorageEntry;

typedef struct {
    u32 index;
    u32 entry_count;
    u64 end_offset;
    BktrIndirectStorageEntry indirect_storage_entries[0x3FF0 / sizeof(BktrIndirectStorageEntry)];
    u8 reserved[0x3FF0 % sizeof(BktrIndirectStorageEntry)];
} BktrIndirectStorageBucket;

typedef struct {
    u32 index;
    u32 bucket_count;
    u64 virtual_size;
    u64 virtual_offsets[0x3FF0 / sizeof(u64)];
    BktrIndirectStorageBucket indirect_storage_buckets[];
} BktrIndirectStorageBlock;

typedef struct {
    u64 offset;
    u32 size;
    u32 generation;
} BktrAesCtrExStorageEntry;

typedef struct {
    u32 index;
    u32 entry_count;
    u64 end_offset;
    BktrAesCtrExStorageEntry aes_ctr_ex_storage_entries[0x3FF];
} BktrAesCtrExStorageBucket;

typedef struct {
    u32 index;
    u32 bucket_count;
    u64 physical_size;
    u64 physical_offsets[0x3FF0 / sizeof(u64)];
    BktrAesCtrExStorageBucket aes_ctr_ex_storage_buckets[];
} BktrAesCtrExStorageBlock;

typedef struct {
    RomFileSystemContext base_romfs;            ///< Base NCA RomFS context.
    RomFileSystemContext patch_romfs;           ///< Update NCA RomFS context. Must be used with RomFS directory/file entry functions, because it holds the updated directory/file tables.
    NcaPatchInfo *patch_info;
    BktrIndirectStorageBlock *indirect_block;
    BktrAesCtrExStorageBlock *aes_ctr_ex_block;
    u64 virtual_seek;                           ///< Relative to the start of the NCA FS section.
    u64 base_seek;                              ///< Relative to the start of the NCA FS section (base NCA RomFS).
    u64 patch_seek;                             ///< Relative to the start of the NCA FS section (update NCA BKTR).
} BktrContext;

/// Initializes a BKTR context.
bool bktrInitializeContext(BktrContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *update_nca_fs_ctx);

/// Cleanups a previously initialized BKTR context.
NX_INLINE void bktrFreeContext(BktrContext *ctx)
{
    if (!ctx) return;
    romfsFreeContext(&(ctx->base_romfs));
    romfsFreeContext(&(ctx->update_romfs));
    if (ctx->indirect_block) free(ctx->indirect_block);
    if (ctx->aes_ctr_ex_block) free(ctx->aes_ctr_ex_block);
    memset(ctx, 0, sizeof(BktrContext));
}








/// Reads raw filesystem data using a RomFS context.
/// Input offset must be relative to the start of the RomFS.
//bool romfsReadFileSystemData(RomFileSystemContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads data from a previously retrieved RomFileSystemFileEntry using a RomFS context.
/// Input offset must be relative to the start of the RomFS file entry data.
//bool romfsReadFileEntryData(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset);







#endif /* __BKTR_H__ */
