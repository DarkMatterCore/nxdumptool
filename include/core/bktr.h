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

#define BKTR_MAX_SUBSTORAGE_COUNT           2

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
    BucketTreeStorageType_Indirect   = 0,   ///< Uses two substorages: index 0 (points to the base NCA) and index 1 (AesCtrEx storage).
                                            ///< All reads within storage index 0 use the calculated physical offsets for data decryption.
    BucketTreeStorageType_AesCtrEx   = 1,   ///< Used as storage index 1 for BucketTreeStorageType_Indirect.
    BucketTreeStorageType_Compressed = 2,   ///< Uses LZ4-compressed sections.
    BucketTreeStorageType_Sparse     = 3,   ///< BucketTreeStorageType_Indirect with a twist. Storage index 0 points to the same NCA, and uses virtual offsets for data decryption.
                                            ///< Zero-filled output is used for any reads within storage index 1.
    BucketTreeStorageType_Count      = 4    ///< Total values supported by this enum.
} BucketTreeStorageType;

typedef enum {
    BucketTreeSubStorageType_Regular    = 0,    ///< Body storage with None, XTS or CTR crypto. Most common substorage type, used in all title types.
                                                ///< May be used as substorage for all BucketTreeStorage types.
    BucketTreeSubStorageType_Indirect   = 1,    ///< Indirect storage from patch NCAs. May be used as substorage for BucketTreeStorageType_Compressed only.
    BucketTreeSubStorageType_AesCtrEx   = 2,    ///< AesCtrEx storage from patch NCAs. May be used as substorage for BucketTreeStorageType_Indirect only.
    BucketTreeSubStorageType_Sparse     = 3,    ///< Sparse storage with CTR crypto, using virtual offsets as lower CTR IVs. Used in base applications only.
                                                ///< May be used as substorage for BucketTreeStorageType_Compressed only.
    BucketTreeSubStorageType_Compressed = 4,    ///< Compressed storage. If available, this is always the outmost storage type for any NCA. May be used by all title types.
                                                ///< May be used as substorage for BucketTreeStorageType_Indirect only.
    BucketTreeSubStorageType_Count      = 5     ///< Total values supported by this enum.
} BucketTreeSubStorageType;

typedef struct {
    u8 index;                           ///< Substorage index.
    NcaFsSectionContext *nca_fs_ctx;    ///< NCA FS section context. Used to perform operations on the target NCA.
    u8 type;                 ///< BucketTreeSubStorageType.
    void *bktr_ctx;                     ///< BucketTreeContext related to this storage. Only used if type > BucketTreeSubStorageType_Regular.
} BucketTreeSubStorage;

typedef struct {
    NcaFsSectionContext *nca_fs_ctx;                                ///< NCA FS section context. Used to perform operations on the target NCA.
    u8 storage_type;                                                ///< BucketTreeStorageType.
    BucketTreeTable *storage_table;                                 ///< Pointer to the dynamically allocated Bucket Tree Table for this storage.
    u64 node_size;                                                  ///< Node size for this type of Bucket Tree storage.
    u64 entry_size;                                                 ///< Size of each individual entry within BucketTreeEntryNode.
    u32 offset_count;                                               ///< Number of offsets available within each BucketTreeOffsetNode for this storage.
    u32 entry_set_count;                                            ///< Number of BucketTreeEntryNode elements available in this storage.
    u64 node_storage_size;                                          ///< Offset node segment size within 'storage_table'.
    u64 entry_storage_size;                                         ///< Entry node segment size within 'storage_table'.
    u64 start_offset;                                               ///< Virtual storage start offset.
    u64 end_offset;                                                 ///< Virtual storage end offset.
    BucketTreeSubStorage substorages[BKTR_MAX_SUBSTORAGE_COUNT];    ///< Substorages required for this BucketTree storage. May be set after initializing this context.
} BucketTreeContext;

/// Initializes a Bucket Tree context using the provided NCA FS section context and a storage type.
bool bktrInitializeContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, u8 storage_type);

/// Sets a BucketTreeSubStorageType_Regular substorage at index 0.
bool bktrSetRegularSubStorage(BucketTreeContext *ctx, NcaFsSectionContext *nca_fs_ctx);

/// Sets a substorage with type >= BucketTreeSubStorageType_Indirect and <= BucketTreeSubStorageType_Compressed at the provided index.
bool bktrSetBucketTreeSubStorage(BucketTreeContext *parent_ctx, BucketTreeContext *child_ctx, u8 substorage_index);

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
            ctx->entry_set_count && ctx->node_storage_size && ctx->entry_storage_size && ctx->end_offset > ctx->start_offset);
}

NX_INLINE bool bktrIsOffsetWithinStorageRange(BucketTreeContext *ctx, u64 offset)
{
    return (bktrIsValidContext(ctx) && ctx->start_offset <= offset && offset < ctx->end_offset);
}

NX_INLINE bool bktrIsBlockWithinStorageRange(BucketTreeContext *ctx, u64 size, u64 offset)
{
    return (bktrIsValidContext(ctx) && size > 0 && ctx->start_offset <= offset && size <= (ctx->end_offset - offset));
}

NX_INLINE bool bktrIsValidSubstorage(BucketTreeSubStorage *substorage)
{
    return (substorage && substorage->index < BKTR_MAX_SUBSTORAGE_COUNT && substorage->nca_fs_ctx && substorage->type < BucketTreeSubStorageType_Count && \
            ((substorage->type == BucketTreeSubStorageType_Regular && substorage->index == 0 && !substorage->bktr_ctx) || \
            (substorage->type > BucketTreeSubStorageType_Regular && substorage->bktr_ctx)));
}

#ifdef __cplusplus
}
#endif

#endif /* __BKTR_H__ */
