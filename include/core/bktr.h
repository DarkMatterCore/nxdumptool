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

#define BKTR_NODE_HEADER_SIZE               0x10
#define BKTR_NODE_SIZE                      0x4000                      /* Currently shared by all Bucket Tree storage types. */
#define BKTR_NODE_SIZE_MIN                  0x400
#define BKTR_NODE_SIZE_MAX                  0x80000

#define BKTR_INDIRECT_ENTRY_SIZE            0x14
#define BKTR_AES_CTR_EX_ENTRY_SIZE          0x10
#define BKTR_COMPRESSED_ENTRY_SIZE          0x18

#define BKTR_COMPRESSION_PHYS_ALIGNMENT     0x10

#define BKTR_COMPRESSION_LEVEL_MIN          0
#define BKTR_COMPRESSION_LEVEL_MAX          16
#define BKTR_COMPRESSION_LEVEL_DEFAULT      BKTR_COMPRESSION_LEVEL_MIN

#define BKTR_COMPRESSION_INVALID_PHYS_SIZE  UINT32_MAX

/// Used as the header for both BucketTreeOffsetNode and BucketTreeEntryNode.
typedef struct {
    u32 index;  ///< BucketTreeOffsetNode / BucketTreeEntryNode index.
    u32 count;  ///< BucketTreeHeader: BucketTreeEntryNode count. BucketTreeEntryNode: entry count.
    u64 offset; ///< Usually represents a physical or virtual size.
} BucketTreeNodeHeader;

NXDT_ASSERT(BucketTreeNodeHeader, BKTR_NODE_HEADER_SIZE);

/// First segment of every BucketTreeTable.
typedef struct {
    BucketTreeNodeHeader header;
    u64 offsets[0x7FE];             ///< May represent virtual or physical offsets, depending on the storage type.
} BucketTreeOffsetNode;

NXDT_ASSERT(BucketTreeOffsetNode, BKTR_NODE_SIZE);

/// IndirectStorage-related elements.
typedef enum {
    BucketTreeIndirectStorageIndex_Original = 0,
    BucketTreeIndirectStorageIndex_Patch    = 1
} BucketTreeIndirectStorageIndex;

#pragma pack(push, 1)
typedef struct {
    u64 virtual_offset;
    u64 physical_offset;
    u32 storage_index;      ///< BucketTreeIndirectStorageIndex.
} BucketTreeIndirectStorageEntry;
#pragma pack(pop)

NXDT_ASSERT(BucketTreeIndirectStorageEntry, BKTR_INDIRECT_ENTRY_SIZE);

/// AesCtrExStorage-related elements.
typedef enum {
    BucketTreeAesCtrExStorageEncryption_Enabled  = 0,
    BucketTreeAesCtrExStorageEncryption_Disabled = 1
} BucketTreeAesCtrExStorageEncryption;

typedef struct {
    u64 offset;
    u8 encryption;      ///< BucketTreeAesCtrExStorageEncryption.
    u8 reserved[0x3];
    u32 generation;
} BucketTreeAesCtrExStorageEntry;

NXDT_ASSERT(BucketTreeAesCtrExStorageEntry, BKTR_AES_CTR_EX_ENTRY_SIZE);

/// CompressedStorage-related elements.
typedef enum {
    BucketTreeCompressedStorageCompressionType_None = 0,
    BucketTreeCompressedStorageCompressionType_Zero = 1,
    BucketTreeCompressedStorageCompressionType_2    = 2,
    BucketTreeCompressedStorageCompressionType_LZ4  = 3
} BucketTreeCompressedStorageCompressionType;

typedef struct {
    s64 virtual_offset;
    s64 physical_offset;    ///< Must be aligned to BKTR_COMPRESSION_PHYS_ALIGNMENT.
    u8 compression_type;    ///< BucketTreeCompressedStorageCompressionType.
    s8 compression_level;   ///< Must be within the range [BKTR_COMPRESSION_LEVEL_MIN, BKTR_COMPRESSION_LEVEL_MAX].
    u8 reserved[0x2];
    u32 physical_size;
} BucketTreeCompressedStorageEntry;

NXDT_ASSERT(BucketTreeCompressedStorageEntry, BKTR_COMPRESSED_ENTRY_SIZE);

/// Second segment of every BucketTreeTable. At least one entry node must be available.
typedef struct {
    BucketTreeNodeHeader header;
    union {
        struct {
            BucketTreeIndirectStorageEntry indirect_entries[0x332];
            u8 reserved[0x8];
        };
        BucketTreeAesCtrExStorageEntry aes_ctr_ex_entries[0x3FF];
        BucketTreeCompressedStorageEntry compressed_entries[0x2AA];
    };
} BucketTreeEntryNode;

NXDT_ASSERT(BucketTreeEntryNode, BKTR_NODE_SIZE);

typedef struct {
    BucketTreeOffsetNode offset_node;
    BucketTreeEntryNode entry_nodes[];  ///< Number of nodes can be retrieved from offset_node.header.count.
} BucketTreeTable;

NXDT_ASSERT(BucketTreeTable, BKTR_NODE_SIZE);

typedef enum {
    BucketTreeStorageType_Indirect   = 0,
    BucketTreeStorageType_AesCtrEx   = 1,
    BucketTreeStorageType_Compressed = 2,
    BucketTreeStorageType_Sparse     = 3,   ///< BucketTreeStorageType_Indirect, but uses virtual offsets for data decryption.
    BucketTreeStorageType_Count      = 4    ///< Total values supported by this enum.
} BucketTreeStorageType;

typedef struct {
    NcaFsSectionContext *nca_fs_ctx;    ///< NCA FS section context. Used to perform operations on the target NCA.
    NcaBucketInfo bucket;               ///< Bucket info used to initialize this context.
    u8 storage_type;                    ///< BucketTreeStorageType.
    BucketTreeTable *storage_table;     ///< Pointer to the dynamically allocated Bucket Tree Table for this storage.
    u64 node_size;                      ///< Node size for this type of Bucket Tree storage.
    u64 entry_size;                     ///< Size of each individual entry within BucketTreeEntryNode.
    u32 offset_count;                   ///< Number of offsets available within each BucketTreeOffsetNode for this storage.
    u32 entry_set_count;                ///< Number of BucketTreeEntryNode elements available in this storage.
    u64 start_offset;                   ///< Virtual storage start offset.
    u64 end_offset;                     ///< Virtual storage end offset.
    
    // TODO: add sub storage
    
} BucketTreeContext;

/// Initializes a Bucket Tree context using the provided NCA FS section context and a storage 
bool bktrInitializeContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, u8 storage_type);

/// Reads data from a Bucket Tree storage using a previously initialized BucketTreeContext.
bool bktrReadStorage(BucketTreeContext *ctx, void *out, u64 read_size, u64 offset);

/// Helper inline functions.

NX_INLINE void bktrFreeContext(BucketTreeContext *ctx)
{
    if (!ctx) return;
    if (ctx->storage_table) free(ctx->storage_table);
    memset(ctx, 0, sizeof(BucketTreeContext));
}

NX_INLINE bool bktrIsValidContext(BucketTreeContext *ctx)
{
    return (ctx && ctx->nca_fs_ctx && ctx->storage_type < BucketTreeStorageType_Count && ctx->storage_table && ctx->node_size && ctx->entry_size && ctx->offset_count && \
            ctx->entry_set_count && ctx->end_offset > ctx->start_offset);
}

NX_INLINE bool bktrIsOffsetWithinStorageRange(BucketTreeContext *ctx, u64 offset)
{
    return (bktrIsValidContext(ctx) && ctx->start_offset <= offset && offset < ctx->end_offset);
}

NX_INLINE bool bktrIsBlockWithinStorageRange(BucketTreeContext *ctx, u64 size, u64 offset)
{
    return (bktrIsValidContext(ctx) && size > 0 && ctx->start_offset <= offset && size <= (ctx->end_offset - offset));
}






















typedef struct {
    RomFileSystemContext base_romfs_ctx;            ///< Base NCA RomFS context.
    RomFileSystemContext patch_romfs_ctx;           ///< Update NCA RomFS context. Must be used with RomFS directory/file entry functions, because it holds the updated directory/file tables.
    u64 offset;                                     ///< Patched RomFS image offset (relative to the start of the update NCA FS section).
    u64 size;                                       ///< Patched RomFS image size.
    u64 body_offset;                                ///< Patched RomFS image file data body offset (relative to the start of the RomFS).
    BktrIndirectStorageBlock *indirect_block;       ///< BKTR Indirect Storage Block.
    BktrAesCtrExStorageBlock *aes_ctr_ex_block;     ///< BKTR AesCtrEx Storage Block.
    bool missing_base_romfs;                        ///< If true, only Patch RomFS data is used. Needed for games with base Program NCAs without a RomFS section (e.g. Fortnite, World of Tanks Blitz, etc.).
    BktrIndirectStorageBlock *base_indirect_block;  ///< Base NCA Indirect Storage Block (sparse layer), if available.
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
    if (ctx->base_indirect_block) free(ctx->base_indirect_block);
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
