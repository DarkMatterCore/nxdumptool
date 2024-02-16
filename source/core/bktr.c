/*
 * bktr.c
 *
 * Copyright (c) 2018-2020, SciresM.
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

#include "nxdt_utils.h"
#include "bktr.h"
#include "aes.h"

/* Type definitions. */

typedef struct {
    u64 offset;
    u32 stride;
} BucketTreeStorageNodeOffset;

typedef struct {
    BucketTreeStorageNodeOffset start;
    u32 count;
    u32 index;
} BucketTreeStorageNode;

typedef struct {
    BucketTreeNodeHeader header;
    u64 start;
} BucketTreeEntrySetHeader;

NXDT_ASSERT(BucketTreeEntrySetHeader, BKTR_NODE_HEADER_SIZE + 0x8);

typedef struct {
    BucketTreeContext *bktr_ctx;
    BucketTreeEntrySetHeader entry_set;
    u32 entry_index;
    void *entry;
} BucketTreeVisitor;

typedef struct {
    void *buffer;
    u64 offset;
    u64 size;
    u64 virtual_offset;
    u32 ctr_val;
    bool aes_ctr_ex_crypt;
    u8 parent_storage_type; ///< BucketTreeStorageType.
} BucketTreeSubStorageReadParams;

/* Global variables. */

#if LOG_LEVEL <= LOG_LEVEL_ERROR
static const char *g_bktrStorageTypeNames[] = {
    [BucketTreeStorageType_Indirect]   = "Indirect",
    [BucketTreeStorageType_AesCtrEx]   = "AesCtrEx",
    [BucketTreeStorageType_Compressed] = "Compressed",
    [BucketTreeStorageType_Sparse]     = "Sparse"
};
#endif

/* Function prototypes. */

#if LOG_LEVEL <= LOG_LEVEL_ERROR
static const char *bktrGetStorageTypeName(u8 storage_type);
#endif

static bool bktrInitializeIndirectStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, bool is_sparse);
static bool bktrGetIndirectStorageEntryExtents(BucketTreeVisitor *visitor, u64 offset, BucketTreeIndirectStorageEntry *out_cur_entry, u64 *out_next_entry_offset);
static bool bktrReadIndirectStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset);

static bool bktrInitializeAesCtrExStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx);
static bool bktrGetAesCtrExStorageEntryExtents(BucketTreeVisitor *visitor, u64 offset, BucketTreeAesCtrExStorageEntry *out_cur_entry, u64 *out_next_entry_offset);
static bool bktrReadAesCtrExStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset);

static bool bktrGetCompressedStorageEntryExtents(BucketTreeVisitor *visitor, u64 offset, BucketTreeCompressedStorageEntry *out_cur_entry, u64 *out_next_entry_offset);
static bool bktrReadCompressedStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset);

static bool bktrReadSubStorage(BucketTreeSubStorage *substorage, BucketTreeSubStorageReadParams *params);
NX_INLINE void bktrInitializeSubStorageReadParams(BucketTreeSubStorageReadParams *out, void *buffer, u64 offset, u64 size, u64 virtual_offset, u32 ctr_val, bool aes_ctr_ex_crypt, u8 parent_storage_type);

static bool bktrVerifyBucketInfo(NcaBucketInfo *bucket, u64 node_size, u64 entry_size, u64 *out_node_storage_size, u64 *out_entry_storage_size);
static bool bktrValidateTableOffsetNode(const BucketTreeTable *table, u64 node_size, u64 entry_size, u32 entry_count, u64 *out_start_offset, u64 *out_end_offset);
NX_INLINE bool bktrVerifyNodeHeader(const BucketTreeNodeHeader *node_header, u32 node_index, u64 node_size, u64 entry_size);

static u64 bktrQueryNodeStorageSize(u64 node_size, u64 entry_size, u32 entry_count);
static u64 bktrQueryEntryStorageSize(u64 node_size, u64 entry_size, u32 entry_count);

NX_INLINE u32 bktrGetEntryCount(u64 node_size, u64 entry_size);
NX_INLINE u32 bktrGetOffsetCount(u64 node_size);
NX_INLINE u32 bktrGetEntrySetCount(u64 node_size, u64 entry_size, u32 entry_count);
NX_INLINE u32 bktrGetNodeL2Count(u64 node_size, u64 entry_size, u32 entry_count);

NX_INLINE const void *bktrGetNodeArray(const BucketTreeNodeHeader *node_header);
NX_INLINE const u64 *bktrGetOffsetNodeArray(const BucketTreeOffsetNode *offset_node);
NX_INLINE const u64 *bktrGetOffsetNodeBegin(const BucketTreeOffsetNode *offset_node);
NX_INLINE const u64 *bktrGetOffsetNodeEnd(const BucketTreeOffsetNode *offset_node);

static bool bktrFindStorageEntry(BucketTreeContext *ctx, u64 virtual_offset, BucketTreeVisitor *out_visitor);
static bool bktrGetTreeNodeEntryIndex(const u64 *start_ptr, const u64 *end_ptr, u64 virtual_offset, u32 *out_index);
static bool bktrGetEntryNodeEntryIndex(const BucketTreeNodeHeader *node_header, u64 entry_size, u64 virtual_offset, u32 *out_index);

static bool bktrFindEntrySet(BucketTreeContext *ctx, u32 *out_index, u64 virtual_offset, u32 node_index);
static const BucketTreeNodeHeader *bktrGetTreeNodeHeader(BucketTreeContext *ctx, u32 node_index);
NX_INLINE u32 bktrGetEntrySetIndex(BucketTreeContext *ctx, u32 node_index, u32 offset_index);

static bool bktrFindEntry(BucketTreeContext *ctx, BucketTreeVisitor *out_visitor, u64 virtual_offset, u32 entry_set_index);
static const BucketTreeNodeHeader *bktrGetEntryNodeHeader(BucketTreeContext *ctx, u32 entry_set_index);

NX_INLINE u64 bktrGetEntryNodeEntryOffset(u64 entry_set_offset, u64 entry_size, u32 entry_index);
NX_INLINE u64 bktrGetEntryNodeEntryOffsetByIndex(u32 entry_set_index, u64 node_size, u64 entry_size, u32 entry_index);

NX_INLINE bool bktrIsExistL2(BucketTreeContext *ctx);
NX_INLINE bool bktrIsExistOffsetL2OnL1(BucketTreeContext *ctx);

static void bktrInitializeStorageNode(BucketTreeStorageNode *out, u64 entry_size, u32 entry_count);
static void bktrStorageNodeFind(BucketTreeStorageNode *storage_node, const BucketTreeNodeHeader *node_header, u64 virtual_offset);
NX_INLINE BucketTreeStorageNodeOffset bktrStorageNodeOffsetAdd(BucketTreeStorageNodeOffset *ofs, u64 value);
NX_INLINE const u64 bktrStorageNodeOffsetGetEntryVirtualOffset(const BucketTreeNodeHeader *node_header, const BucketTreeStorageNodeOffset *ofs);

NX_INLINE bool bktrVisitorIsValid(BucketTreeVisitor *visitor);
NX_INLINE bool bktrVisitorCanMoveNext(BucketTreeVisitor *visitor);
static bool bktrVisitorMoveNext(BucketTreeVisitor *visitor);

bool bktrInitializeContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, u8 storage_type)
{
    if (!out || !nca_fs_ctx || !nca_fs_ctx->enabled || nca_fs_ctx->section_type >= NcaFsSectionType_Invalid || !nca_fs_ctx->nca_ctx || \
        (nca_fs_ctx->nca_ctx->rights_id_available && !nca_fs_ctx->nca_ctx->titlekey_retrieved) || storage_type == BucketTreeStorageType_Compressed || \
        storage_type >= BucketTreeStorageType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = false;

    /* Free output context beforehand. */
    bktrFreeContext(out);

    /* Initialize the desired storage type. */
    switch(storage_type)
    {
        case BucketTreeStorageType_Indirect:
        case BucketTreeStorageType_Sparse:
            success = bktrInitializeIndirectStorageContext(out, nca_fs_ctx, storage_type == BucketTreeStorageType_Sparse);
            break;
        case BucketTreeStorageType_AesCtrEx:
            success = bktrInitializeAesCtrExStorageContext(out, nca_fs_ctx);
            break;
        default:
            break;
    }

    if (!success) LOG_MSG_ERROR("Failed to initialize Bucket Tree %s storage for FS section #%u in \"%s\".", bktrGetStorageTypeName(storage_type), nca_fs_ctx->section_idx, \
                                nca_fs_ctx->nca_ctx->content_id_str);

    return success;
}

bool bktrInitializeCompressedStorageContext(BucketTreeContext *out, BucketTreeSubStorage *substorage)
{
    if (!out || !bktrIsValidSubStorage(substorage) || substorage->index != 0 || !substorage->nca_fs_ctx->enabled || !substorage->nca_fs_ctx->has_compression_layer || \
        substorage->nca_fs_ctx->section_type >= NcaFsSectionType_Invalid || !substorage->nca_fs_ctx->nca_ctx || \
        (substorage->nca_fs_ctx->nca_ctx->rights_id_available && !substorage->nca_fs_ctx->nca_ctx->titlekey_retrieved) || \
        substorage->type == BucketTreeSubStorageType_AesCtrEx || substorage->type == BucketTreeSubStorageType_Compressed || substorage->type >= BucketTreeSubStorageType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Free output context beforehand. */
    bktrFreeContext(out);

    NcaFsSectionContext *nca_fs_ctx = substorage->nca_fs_ctx;
    NcaBucketInfo *compressed_bucket = &(nca_fs_ctx->header.compression_info.bucket);
    BucketTreeTable *compressed_table = NULL;
    u64 node_storage_size = 0, entry_storage_size = 0;
    BucketTreeSubStorageReadParams params = {0};
    bool dump_table = false, success = false;

    /* Verify bucket info. */
    if (!bktrVerifyBucketInfo(compressed_bucket, BKTR_NODE_SIZE, BKTR_COMPRESSED_ENTRY_SIZE, &node_storage_size, &entry_storage_size))
    {
        LOG_MSG_ERROR("Compressed Storage BucketInfo verification failed!");
        goto end;
    }

    /* Allocate memory for the full Compressed table. */
    compressed_table = calloc(1, compressed_bucket->size);
    if (!compressed_table)
    {
        LOG_MSG_ERROR("Unable to allocate memory for the Compressed Storage Table!");
        goto end;
    }

    /* Read Compressed storage table data. */
    const u64 compression_table_offset = (nca_fs_ctx->hash_region.size + compressed_bucket->offset);
    bktrInitializeSubStorageReadParams(&params, compressed_table, compression_table_offset, compressed_bucket->size, 0, 0, false, BucketTreeSubStorageType_Compressed);

    if (!bktrReadSubStorage(substorage, &params))
    {
        LOG_MSG_ERROR("Failed to read Compressed Storage Table data!");
        goto end;
    }

    dump_table = true;

    /* Validate table offset node. */
    u64 start_offset = 0, end_offset = 0;
    if (!bktrValidateTableOffsetNode(compressed_table, BKTR_NODE_SIZE, BKTR_COMPRESSED_ENTRY_SIZE, compressed_bucket->header.entry_count, &start_offset, &end_offset))
    {
        LOG_MSG_ERROR("Compressed Storage Table Offset Node validation failed!");
        goto end;
    }

    /* Update output context. */
    out->nca_fs_ctx = nca_fs_ctx;
    out->storage_type = BucketTreeStorageType_Compressed;
    out->storage_table = compressed_table;
    out->node_size = BKTR_NODE_SIZE;
    out->entry_size = BKTR_COMPRESSED_ENTRY_SIZE;
    out->offset_count = bktrGetOffsetCount(BKTR_NODE_SIZE);
    out->entry_set_count = bktrGetEntrySetCount(BKTR_NODE_SIZE, BKTR_COMPRESSED_ENTRY_SIZE, compressed_bucket->header.entry_count);
    out->node_storage_size = node_storage_size;
    out->entry_storage_size = entry_storage_size;
    out->start_offset = start_offset;
    out->end_offset = end_offset;

    memcpy(&(out->substorages[0]), substorage, sizeof(BucketTreeSubStorage));

    /* Update return value. */
    success = true;

end:
    if (!success)
    {
        LOG_DATA_DEBUG(compressed_bucket, sizeof(NcaBucketInfo), "Compressed Storage BucketInfo dump:");

        if (compressed_table)
        {
            if (dump_table) LOG_DATA_DEBUG(compressed_table, compressed_bucket->size, "Compressed Storage Table dump:");
            free(compressed_table);
        }
    }

    return success;
}

bool bktrSetRegularSubStorage(BucketTreeContext *ctx, NcaFsSectionContext *nca_fs_ctx)
{
    if (!bktrIsValidContext(ctx) || !nca_fs_ctx || !nca_fs_ctx->enabled || nca_fs_ctx->section_type >= NcaFsSectionType_Invalid || \
        !nca_fs_ctx->nca_ctx || (nca_fs_ctx->nca_ctx->rights_id_available && !nca_fs_ctx->nca_ctx->titlekey_retrieved) || \
        ctx->storage_type == BucketTreeStorageType_Compressed || ctx->storage_type >= BucketTreeStorageType_Count || \
        (ctx->storage_type == BucketTreeStorageType_Indirect && ctx->nca_fs_ctx == nca_fs_ctx) || \
        ((ctx->storage_type == BucketTreeStorageType_AesCtrEx || ctx->storage_type == BucketTreeStorageType_Sparse) && ctx->nca_fs_ctx != nca_fs_ctx))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Update the substorage. */
    BucketTreeSubStorage *substorage = &(ctx->substorages[0]);
    memset(substorage, 0, sizeof(BucketTreeSubStorage));

    substorage->index = 0;
    substorage->nca_fs_ctx = nca_fs_ctx;
    substorage->type = BucketTreeSubStorageType_Regular;
    substorage->bktr_ctx = NULL;

    return true;
}

bool bktrSetBucketTreeSubStorage(BucketTreeContext *parent_ctx, BucketTreeContext *child_ctx, u8 substorage_index)
{
    if (!bktrIsValidContext(parent_ctx) || !bktrIsValidContext(child_ctx) || substorage_index >= BKTR_MAX_SUBSTORAGE_COUNT || \
        parent_ctx->storage_type != BucketTreeStorageType_Indirect || child_ctx->storage_type < BucketTreeStorageType_AesCtrEx || \
        child_ctx->storage_type > BucketTreeStorageType_Sparse || (child_ctx->storage_type == BucketTreeStorageType_AesCtrEx && (substorage_index != 1 || \
        parent_ctx->nca_fs_ctx != child_ctx->nca_fs_ctx)) || ((child_ctx->storage_type == BucketTreeStorageType_Compressed || \
        child_ctx->storage_type == BucketTreeStorageType_Sparse) && (substorage_index != 0 || parent_ctx->nca_fs_ctx == child_ctx->nca_fs_ctx)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Update the substorage. */
    BucketTreeSubStorage *substorage = &(parent_ctx->substorages[substorage_index]);
    memset(substorage, 0, sizeof(BucketTreeSubStorage));

    substorage->index = substorage_index;
    substorage->nca_fs_ctx = child_ctx->nca_fs_ctx;
    substorage->type = (child_ctx->storage_type + 1);   /* Convert to BucketTreeSubStorageType value. */
    substorage->bktr_ctx = child_ctx;

    return true;
}

bool bktrReadStorage(BucketTreeContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!bktrIsBlockWithinStorageRange(ctx, read_size, offset) || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeVisitor visitor = {0};
    bool success = false;

    /* Find storage entry. */
    if (!bktrFindStorageEntry(ctx, offset, &visitor))
    {
        LOG_MSG_ERROR("Unable to find %s storage entry for offset 0x%lX!", bktrGetStorageTypeName(ctx->storage_type), offset);
        goto end;
    }

    /* Process storage entry according to the storage type. */
    switch(ctx->storage_type)
    {
        case BucketTreeStorageType_Indirect:
        case BucketTreeStorageType_Sparse:
            success = bktrReadIndirectStorage(&visitor, out, read_size, offset);
            break;
        case BucketTreeStorageType_AesCtrEx:
            success = bktrReadAesCtrExStorage(&visitor, out, read_size, offset);
            break;
        case BucketTreeStorageType_Compressed:
            success = bktrReadCompressedStorage(&visitor, out, read_size, offset);
            break;
        default:
            break;
    }

    if (!success) LOG_MSG_ERROR("Failed to read 0x%lX-byte long block at offset 0x%lX from %s storage!", read_size, offset, bktrGetStorageTypeName(ctx->storage_type));

end:
    return success;
}

bool bktrIsBlockWithinIndirectStorageRange(BucketTreeContext *ctx, u64 offset, u64 size, bool *out)
{
    if (!bktrIsBlockWithinStorageRange(ctx, size, offset) || (ctx->storage_type != BucketTreeStorageType_Indirect && ctx->storage_type != BucketTreeStorageType_Compressed) || \
        (ctx->storage_type == BucketTreeStorageType_Compressed && ctx->substorages[0].type != BucketTreeSubStorageType_Indirect) || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeVisitor visitor = {0};
    bool updated = false, success = false;

    /* Find storage entry. */
    if (!bktrFindStorageEntry(ctx, offset, &visitor))
    {
        LOG_MSG_ERROR("Unable to find %s storage entry for offset 0x%lX!", bktrGetStorageTypeName(ctx->storage_type), offset);
        goto end;
    }

    /* Check if we're dealing with a Compressed storage. */
    if (ctx->storage_type == BucketTreeStorageType_Compressed)
    {
        BucketTreeContext *indirect_storage = ctx->substorages[0].bktr_ctx;
        const u64 compressed_storage_base_offset = ctx->nca_fs_ctx->hash_region.size;
        BucketTreeCompressedStorageEntry *start_entry = NULL, *end_entry = NULL;

        /* Validate start entry node. */
        start_entry = end_entry = (BucketTreeCompressedStorageEntry*)visitor.entry;
        if (!bktrIsOffsetWithinStorageRange(ctx, (u64)start_entry->virtual_offset) || (u64)start_entry->virtual_offset > offset)
        {
            LOG_MSG_ERROR("Invalid Compressed Storage entry! (0x%lX) (#1).", start_entry->virtual_offset);
            goto end;
        }

        /* Loop until we reach the upper bound of the requested block or find a match. */
        do {
            u64 cur_entry_offset = 0;

            /* Check if we can move any further. */
            if (bktrVisitorCanMoveNext(&visitor))
            {
                BucketTreeCompressedStorageEntry *tmp = end_entry;

                /* Retrieve next entry node. */
                if (!bktrVisitorMoveNext(&visitor))
                {
                    LOG_MSG_ERROR("Failed to retrieve next Compressed Storage entry!");
                    goto end;
                }

                /* Validate next entry node. */
                end_entry = (BucketTreeCompressedStorageEntry*)visitor.entry;
                if (!bktrIsOffsetWithinStorageRange(ctx, (u64)end_entry->virtual_offset) || (u64)end_entry->virtual_offset <= (u64)tmp->virtual_offset)
                {
                    LOG_MSG_ERROR("Invalid Indirect Storage entry! (0x%lX) (#2).", (u64)end_entry->virtual_offset);
                    goto end;
                }

                /* Update current entry offset. */
                cur_entry_offset = (u64)end_entry->virtual_offset;

                /* Update start entry node. */
                start_entry = tmp;
            } else {
                /* Update current entry offset. */
                cur_entry_offset = ctx->end_offset;

                /* Update entry nodes. */
                start_entry = end_entry;
                end_entry = NULL;
            }

            /* Calculate indirect block extents. */
            u64 indirect_block_offset = compressed_storage_base_offset;
            u64 indirect_block_size = (cur_entry_offset - (u64)start_entry->virtual_offset);

            if ((u64)start_entry->virtual_offset <= offset)
            {
                indirect_block_offset += ((offset - (u64)start_entry->virtual_offset) + (u64)start_entry->physical_offset);
                indirect_block_size -= (offset - (u64)start_entry->virtual_offset);
            } else {
                indirect_block_offset += (u64)start_entry->physical_offset;
            }

            if ((offset + size) <= cur_entry_offset)
            {
                indirect_block_size -= (cur_entry_offset - (offset + size));
                end_entry = NULL;   /* Don't proceed any further, we have found our upper bound. */
            }

            /* Check if the current Compressed Storage entry node points to one or more Indirect Storage entry nodes with Patch storage index. */
            if (!bktrIsBlockWithinIndirectStorageRange(indirect_storage, indirect_block_offset, indirect_block_size, &updated))
            {
                LOG_MSG_ERROR("Failed to determine if 0x%lX-byte long Compressed storage block at offset 0x%lX is within Indirect Storage!", indirect_block_offset, indirect_block_size);
                goto end;
            }
        } while(!updated && end_entry && (u64)end_entry->virtual_offset < (offset + size));

        /* Update output values. */
        *out = updated;
        success = true;

        goto end;
    }

    /* Check the Indirect Storage. */
    BucketTreeIndirectStorageEntry *start_entry = NULL, *end_entry = NULL;

    /* Validate start entry node. */
    start_entry = end_entry = (BucketTreeIndirectStorageEntry*)visitor.entry;
    if (!bktrIsOffsetWithinStorageRange(ctx, start_entry->virtual_offset) || start_entry->virtual_offset > offset)
    {
        LOG_MSG_ERROR("Invalid Indirect Storage entry! (0x%lX) (#1).", start_entry->virtual_offset);
        goto end;
    }

    /* Loop through adjacent Indirect Storage entry nodes and check if at least one of them uses the Patch storage index. */
    do {
        /* Break out of the loop immediately if the current entry node's storage index matches Patch. */
        if (end_entry->storage_index == BucketTreeIndirectStorageIndex_Patch)
        {
            updated = true;
            break;
        }

        /* Don't proceed if we can't move any further. */
        if (!bktrVisitorCanMoveNext(&visitor)) break;

        /* Retrieve the next entry node. */
        if (!bktrVisitorMoveNext(&visitor))
        {
            LOG_MSG_ERROR("Failed to retrieve next Indirect Storage entry!");
            goto end;
        }

        /* Validate current entry node. */
        end_entry = (BucketTreeIndirectStorageEntry*)visitor.entry;
        if (!bktrIsOffsetWithinStorageRange(ctx, end_entry->virtual_offset) || end_entry->virtual_offset <= start_entry->virtual_offset)
        {
            LOG_MSG_ERROR("Invalid Indirect Storage entry! (0x%lX) (#2).", end_entry->virtual_offset);
            goto end;
        }
    } while(end_entry->virtual_offset < (offset + size));

    /* Update output values. */
    *out = updated;
    success = true;

end:
    return success;
}

#if LOG_LEVEL <= LOG_LEVEL_ERROR
static const char *bktrGetStorageTypeName(u8 storage_type)
{
    return (storage_type < BucketTreeStorageType_Count ? g_bktrStorageTypeNames[storage_type] : NULL);
}
#endif

static bool bktrInitializeIndirectStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, bool is_sparse)
{
    if ((!is_sparse && nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs) || (is_sparse && !nca_fs_ctx->has_sparse_layer))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    NcaContext *nca_ctx = nca_fs_ctx->nca_ctx;
    NcaBucketInfo *indirect_bucket = (is_sparse ? &(nca_fs_ctx->header.sparse_info.bucket) : &(nca_fs_ctx->header.patch_info.indirect_bucket));
    BucketTreeTable *indirect_table = NULL;
    u64 node_storage_size = 0, entry_storage_size = 0;
    bool dump_table = false, success = false;

    /* Verify bucket info. */
    if (!bktrVerifyBucketInfo(indirect_bucket, BKTR_NODE_SIZE, BKTR_INDIRECT_ENTRY_SIZE, &node_storage_size, &entry_storage_size))
    {
        LOG_MSG_ERROR("Indirect Storage BucketInfo verification failed! (%s).", is_sparse ? "sparse" : "patch");
        goto end;
    }

    /* Allocate memory for the full indirect table. */
    indirect_table = calloc(1, indirect_bucket->size);
    if (!indirect_table)
    {
        LOG_MSG_ERROR("Unable to allocate memory for the Indirect Storage Table! (%s).", is_sparse ? "sparse" : "patch");
        goto end;
    }

    /* Read indirect storage table data. */
    if ((!is_sparse && !ncaReadFsSection(nca_fs_ctx, indirect_table, indirect_bucket->size, indirect_bucket->offset)) || \
        (is_sparse && !ncaReadContentFile(nca_ctx, indirect_table, indirect_bucket->size, nca_fs_ctx->sparse_table_offset)))
    {
        LOG_MSG_ERROR("Failed to read Indirect Storage Table data! (%s).", is_sparse ? "sparse" : "patch");
        goto end;
    }

    /* Decrypt indirect storage table, if needed. */
    if (is_sparse)
    {
        NcaAesCtrUpperIv sparse_upper_iv = {0};
        u8 sparse_ctr[AES_BLOCK_SIZE] = {0};
        const u8 *sparse_ctr_key = NULL;
        Aes128CtrContext sparse_ctr_ctx = {0};

        /* Generate upper CTR IV. */
        memcpy(sparse_upper_iv.value, nca_fs_ctx->header.aes_ctr_upper_iv.value, sizeof(sparse_upper_iv.value));
        sparse_upper_iv.generation = ((u32)(nca_fs_ctx->header.sparse_info.generation) << 16);

        /* Initialize partial AES CTR. */
        aes128CtrInitializePartialCtr(sparse_ctr, sparse_upper_iv.value, nca_fs_ctx->sparse_table_offset);

        /* Create AES CTR context. */
        sparse_ctr_key = (nca_ctx->rights_id_available ? nca_ctx->titlekey : nca_ctx->decrypted_key_area.aes_ctr);
        aes128CtrContextCreate(&sparse_ctr_ctx, sparse_ctr_key, sparse_ctr);

        /* Decrypt indirect storage table in-place. */
        aes128CtrCrypt(&sparse_ctr_ctx, indirect_table, indirect_table, indirect_bucket->size);
    }

    dump_table = true;

    /* Validate table offset node. */
    u64 start_offset = 0, end_offset = 0;
    if (!bktrValidateTableOffsetNode(indirect_table, BKTR_NODE_SIZE, BKTR_INDIRECT_ENTRY_SIZE, indirect_bucket->header.entry_count, &start_offset, &end_offset))
    {
        LOG_MSG_ERROR("Indirect Storage Table Offset Node validation failed! (%s).", is_sparse ? "sparse" : "patch");
        goto end;
    }

    /* Update output context. */
    out->nca_fs_ctx = nca_fs_ctx;
    out->storage_type = (is_sparse ? BucketTreeStorageType_Sparse : BucketTreeStorageType_Indirect);
    out->storage_table = indirect_table;
    out->node_size = BKTR_NODE_SIZE;
    out->entry_size = BKTR_INDIRECT_ENTRY_SIZE;
    out->offset_count = bktrGetOffsetCount(BKTR_NODE_SIZE);
    out->entry_set_count = bktrGetEntrySetCount(BKTR_NODE_SIZE, BKTR_INDIRECT_ENTRY_SIZE, indirect_bucket->header.entry_count);
    out->node_storage_size = node_storage_size;
    out->entry_storage_size = entry_storage_size;
    out->start_offset = start_offset;
    out->end_offset = end_offset;

    /* Update return value. */
    success = true;

end:
    if (!success)
    {
        LOG_DATA_DEBUG(indirect_bucket, sizeof(NcaBucketInfo), "Indirect Storage BucketInfo dump (%s):", is_sparse ? "sparse" : "patch");

        if (indirect_table)
        {
            if (dump_table) LOG_DATA_DEBUG(indirect_table, indirect_bucket->size, "Indirect Storage Table dump (%s):", is_sparse ? "sparse" : "patch");
            free(indirect_table);
        }
    }

    return success;
}

static bool bktrGetIndirectStorageEntryExtents(BucketTreeVisitor *visitor, u64 offset, BucketTreeIndirectStorageEntry *out_cur_entry, u64 *out_next_entry_offset)
{
    if (!visitor || !out_cur_entry || !out_next_entry_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeContext *ctx = visitor->bktr_ctx;
    BucketTreeIndirectStorageEntry cur_entry = {0};
    u64 cur_entry_offset = 0, next_entry_offset = 0;
    bool success = false;

    /* Copy current Indirect Storage entry -- we'll move onto the next one, so we'll lose track of it. */
    memcpy(&cur_entry, visitor->entry, sizeof(BucketTreeIndirectStorageEntry));

    /* Validate Indirect Storage entry. */
    if (!bktrIsOffsetWithinStorageRange(ctx, cur_entry.virtual_offset) || cur_entry.virtual_offset > offset || cur_entry.storage_index > BucketTreeIndirectStorageIndex_Patch)
    {
        LOG_MSG_ERROR("Invalid Indirect Storage entry! (0x%lX) (#1).", cur_entry.virtual_offset);
        goto end;
    }

    cur_entry_offset = cur_entry.virtual_offset;

    /* Check if we can retrieve the next entry. */
    if (bktrVisitorCanMoveNext(visitor))
    {
        /* Retrieve the next entry. */
        if (!bktrVisitorMoveNext(visitor))
        {
            LOG_MSG_ERROR("Failed to retrieve next Indirect Storage entry!");
            goto end;
        }

        /* Validate Indirect Storage entry. */
        BucketTreeIndirectStorageEntry *next_entry = (BucketTreeIndirectStorageEntry*)visitor->entry;
        if (!bktrIsOffsetWithinStorageRange(ctx, next_entry->virtual_offset) || next_entry->storage_index > BucketTreeIndirectStorageIndex_Patch)
        {
            LOG_MSG_ERROR("Invalid Indirect Storage entry! (0x%lX) (#2).", next_entry->virtual_offset);
            goto end;
        }

        /* Store next entry's virtual offset. */
        next_entry_offset = next_entry->virtual_offset;
    } else {
        /* Set the next entry offset to the storage's end. */
        next_entry_offset = ctx->end_offset;
    }

    /* Verify next entry offset. */
    if (next_entry_offset <= cur_entry_offset || offset >= next_entry_offset)
    {
        LOG_MSG_ERROR("Invalid virtual offset for the Indirect Storage's next entry! (0x%lX).", next_entry_offset);
        goto end;
    }

    /* Update variables. */
    memcpy(out_cur_entry, &cur_entry, sizeof(BucketTreeIndirectStorageEntry));
    *out_next_entry_offset = next_entry_offset;
    success = true;

end:
    return success;
}

static bool bktrReadIndirectStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset)
{
    BucketTreeContext *ctx = visitor->bktr_ctx;
    bool is_sparse = (ctx->storage_type == BucketTreeStorageType_Sparse);
    bool missing_original_storage = !bktrIsValidSubStorage(&(ctx->substorages[0]));

    BucketTreeIndirectStorageEntry cur_entry = {0};
    BucketTreeSubStorageReadParams params = {0};
    u64 cur_entry_offset = 0, next_entry_offset = 0, accum = 0;

    bool success = false;

    if (!out || (is_sparse && (missing_original_storage || ctx->substorages[0].type != BucketTreeSubStorageType_Regular)) || \
        (!is_sparse && (!bktrIsValidSubStorage(&(ctx->substorages[1])) || ctx->substorages[1].type != BucketTreeSubStorageType_AesCtrEx || \
        (!missing_original_storage && (ctx->substorages[0].type == BucketTreeSubStorageType_Indirect || ctx->substorages[0].type == BucketTreeSubStorageType_AesCtrEx || \
        ctx->substorages[0].type >= BucketTreeSubStorageType_Count)))) || (offset + read_size) > ctx->end_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Perform Indirect Storage reads until we reach the requested size. */
    while(accum < read_size)
    {
        u8 *out_ptr = ((u8*)out + accum);
        const u64 indirect_block_offset = (offset + accum);
        u64 indirect_block_size = 0, indirect_block_read_size = 0, indirect_block_read_offset = 0, read_size_diff = 0;

        /* Get current Indirect Storage entry and the start offset for the next one. */
        if (!bktrGetIndirectStorageEntryExtents(visitor, indirect_block_offset, &cur_entry, &next_entry_offset))
        {
            LOG_MSG_ERROR("Failed to get Indirect Storage entry extents for offset 0x%lX!", indirect_block_offset);
            goto end;
        }

        /* Calculate Indirect Storage block size. */
        cur_entry_offset = cur_entry.virtual_offset;
        indirect_block_size = (!accum ? (next_entry_offset - offset) : (next_entry_offset - cur_entry_offset));

        /* Calculate Indirect Storage block read size and offset. */
        read_size_diff = (read_size - accum);
        indirect_block_read_size = (read_size_diff > indirect_block_size ? indirect_block_size : read_size_diff);
        indirect_block_read_offset = (indirect_block_offset - cur_entry_offset + cur_entry.physical_offset);

        /* Perform read operation within the current Indirect Storage entry. */
        bktrInitializeSubStorageReadParams(&params, out_ptr, indirect_block_read_offset, indirect_block_read_size, indirect_block_offset, 0, false, ctx->storage_type);

        if (cur_entry.storage_index == BucketTreeIndirectStorageIndex_Original)
        {
            if (!missing_original_storage)
            {
                /* Retrieve data from the original data storage. */
                /* This must either be a Regular/Sparse/Compressed storage from the base NCA (Indirect) or a Regular storage from this very same NCA (Sparse). */
                if (!bktrReadSubStorage(&(ctx->substorages[0]), &params))
                {
                    LOG_MSG_ERROR("Failed to read 0x%lX-byte long chunk from offset 0x%lX in original data storage!", indirect_block_read_size, indirect_block_read_offset);
                    goto end;
                }
            } else {
                LOG_MSG_ERROR("Error: attempting to read 0x%lX-byte long chunk from missing original data storage at offset 0x%lX!", indirect_block_read_size, indirect_block_read_offset);
                goto end;
            }
        } else {
            if (!is_sparse)
            {
                /* Retrieve data from the Indirect data storage. */
                /* This must always be the AesCtrEx storage within this very same NCA (Indirect). */
                if (!bktrReadSubStorage(&(ctx->substorages[1]), &params))
                {
                    LOG_MSG_ERROR("Failed to read 0x%lX-byte long chunk from offset 0x%lX in AesCtrEx storage!", indirect_block_read_size, indirect_block_read_offset);
                    goto end;
                }
            } else {
                /* Fill output buffer with zeroes (SparseStorage's ZeroStorage). */
                memset(out_ptr, 0, indirect_block_read_size);
            }
        }

        /* Update accumulator. */
        accum += indirect_block_read_size;
    }

    /* Update flag. */
    success = true;

end:
    return success;
}

static bool bktrInitializeAesCtrExStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    if (nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || !nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket.size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    NcaBucketInfo *aes_ctr_ex_bucket = &(nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket);
    BucketTreeTable *aes_ctr_ex_table = NULL;
    u64 node_storage_size = 0, entry_storage_size = 0;
    bool dump_table = false, success = false;

    /* Verify bucket info. */
    if (!bktrVerifyBucketInfo(aes_ctr_ex_bucket, BKTR_NODE_SIZE, BKTR_AES_CTR_EX_ENTRY_SIZE, &node_storage_size, &entry_storage_size))
    {
        LOG_MSG_ERROR("AesCtrEx Storage BucketInfo verification failed!");
        goto end;
    }

    /* Allocate memory for the full AesCtrEx table. */
    aes_ctr_ex_table = calloc(1, aes_ctr_ex_bucket->size);
    if (!aes_ctr_ex_table)
    {
        LOG_MSG_ERROR("Unable to allocate memory for the AesCtrEx Storage Table!");
        goto end;
    }

    /* Read AesCtrEx storage table data. */
    if (!ncaReadFsSection(nca_fs_ctx, aes_ctr_ex_table, aes_ctr_ex_bucket->size, aes_ctr_ex_bucket->offset))
    {
        LOG_MSG_ERROR("Failed to read AesCtrEx Storage Table data!");
        goto end;
    }

    dump_table = true;

    /* Validate table offset node. */
    u64 start_offset = 0, end_offset = 0;
    if (!bktrValidateTableOffsetNode(aes_ctr_ex_table, BKTR_NODE_SIZE, BKTR_AES_CTR_EX_ENTRY_SIZE, aes_ctr_ex_bucket->header.entry_count, &start_offset, &end_offset))
    {
        LOG_MSG_ERROR("AesCtrEx Storage Table Offset Node validation failed!");
        goto end;
    }

    /* Update output context. */
    out->nca_fs_ctx = nca_fs_ctx;
    out->storage_type = BucketTreeStorageType_AesCtrEx;
    out->storage_table = aes_ctr_ex_table;
    out->node_size = BKTR_NODE_SIZE;
    out->entry_size = BKTR_AES_CTR_EX_ENTRY_SIZE;
    out->offset_count = bktrGetOffsetCount(BKTR_NODE_SIZE);
    out->entry_set_count = bktrGetEntrySetCount(BKTR_NODE_SIZE, BKTR_AES_CTR_EX_ENTRY_SIZE, aes_ctr_ex_bucket->header.entry_count);
    out->node_storage_size = node_storage_size;
    out->entry_storage_size = entry_storage_size;
    out->start_offset = start_offset;
    out->end_offset = end_offset;

    /* Update return value. */
    success = true;

end:
    if (!success)
    {
        LOG_DATA_DEBUG(aes_ctr_ex_bucket, sizeof(NcaBucketInfo), "AesCtrEx Storage BucketInfo dump:");

        if (aes_ctr_ex_table)
        {
            if (dump_table) LOG_DATA_DEBUG(aes_ctr_ex_table, aes_ctr_ex_bucket->size, "AesCtrEx Storage Table dump:");
            free(aes_ctr_ex_table);
        }
    }

    return success;
}

static bool bktrGetAesCtrExStorageEntryExtents(BucketTreeVisitor *visitor, u64 offset, BucketTreeAesCtrExStorageEntry *out_cur_entry, u64 *out_next_entry_offset)
{
    if (!visitor || !out_cur_entry || !out_next_entry_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeContext *ctx = visitor->bktr_ctx;
    BucketTreeAesCtrExStorageEntry cur_entry = {0};
    u64 cur_entry_offset = 0, next_entry_offset = 0;
    bool success = false;

    /* Copy current AesCtrEx Storage entry -- we'll move onto the next one, so we'll lose track of it. */
    memcpy(&cur_entry, visitor->entry, sizeof(BucketTreeAesCtrExStorageEntry));

    /* Validate AesCtrEx Storage entry. */
    if (!bktrIsOffsetWithinStorageRange(ctx, cur_entry.offset) || cur_entry.offset > offset || !IS_ALIGNED(cur_entry.offset, AES_BLOCK_SIZE))
    {
        LOG_MSG_ERROR("Invalid AesCtrEx Storage entry! (0x%lX) (#1).", cur_entry.offset);
        goto end;
    }

    cur_entry_offset = cur_entry.offset;

    /* Check if we can retrieve the next entry. */
    if (bktrVisitorCanMoveNext(visitor))
    {
        /* Retrieve the next entry. */
        if (!bktrVisitorMoveNext(visitor))
        {
            LOG_MSG_ERROR("Failed to retrieve next AesCtrEx Storage entry!");
            goto end;
        }

        /* Validate AesCtrEx Storage entry. */
        BucketTreeAesCtrExStorageEntry *next_entry = (BucketTreeAesCtrExStorageEntry*)visitor->entry;
        if (!bktrIsOffsetWithinStorageRange(ctx, next_entry->offset))
        {
            LOG_MSG_ERROR("Invalid AesCtrEx Storage entry! (0x%lX) (#2).", next_entry->offset);
            goto end;
        }

        /* Store next entry's virtual offset. */
        next_entry_offset = next_entry->offset;
    } else {
        /* Set the next entry offset to the storage's end. */
        next_entry_offset = ctx->end_offset;
    }

    /* Verify next entry offset. */
    if (!IS_ALIGNED(next_entry_offset, AES_BLOCK_SIZE) || next_entry_offset <= cur_entry_offset || offset >= next_entry_offset)
    {
        LOG_MSG_ERROR("Invalid offset for the AesCtrEx Storage's next entry! (0x%lX).", next_entry_offset);
        goto end;
    }

    /* Update variables. */
    memcpy(out_cur_entry, &cur_entry, sizeof(BucketTreeAesCtrExStorageEntry));
    *out_next_entry_offset = next_entry_offset;
    success = true;

end:
    return success;
}

static bool bktrReadAesCtrExStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset)
{
    BucketTreeContext *ctx = visitor->bktr_ctx;

    BucketTreeAesCtrExStorageEntry cur_entry = {0};
    BucketTreeSubStorageReadParams params = {0};
    u64 cur_entry_offset = 0, next_entry_offset = 0, accum = 0;

    bool success = false;

    if (!out || !bktrIsValidSubStorage(&(ctx->substorages[0])) || ctx->substorages[0].type != BucketTreeSubStorageType_Regular || (offset + read_size) > ctx->end_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Perform AesCtrEx Storage reads until we reach the requested size. */
    while(accum < read_size)
    {
        u8 *out_ptr = ((u8*)out + accum);
        const u64 aes_ctr_ex_block_offset = (offset + accum);
        u64 aes_ctr_ex_block_size = 0, aes_ctr_ex_block_read_size = 0, read_size_diff = 0;

        /* Get current AesCtrEx Storage entry and the start offset for the next one. */
        if (!bktrGetAesCtrExStorageEntryExtents(visitor, aes_ctr_ex_block_offset, &cur_entry, &next_entry_offset))
        {
            LOG_MSG_ERROR("Failed to get AesCtrEx Storage entry extents for offset 0x%lX!", aes_ctr_ex_block_offset);
            goto end;
        }

        /* Calculate AesCtrEx Storage block size. */
        cur_entry_offset = cur_entry.offset;
        aes_ctr_ex_block_size = (!accum ? (next_entry_offset - offset) : (next_entry_offset - cur_entry_offset));

        /* Calculate AesCtrEx Storage block read size and offset. */
        read_size_diff = (read_size - accum);
        aes_ctr_ex_block_read_size = (read_size_diff > aes_ctr_ex_block_size ? aes_ctr_ex_block_size : read_size_diff);

        /* Perform read operation within the current AesCtrEx Storage entry. */
        bool aes_ctr_ex_crypt = (cur_entry.encryption == BucketTreeAesCtrExStorageEncryption_Enabled);
        bktrInitializeSubStorageReadParams(&params, out_ptr, aes_ctr_ex_block_offset, aes_ctr_ex_block_read_size, 0, cur_entry.generation, aes_ctr_ex_crypt, ctx->storage_type);

        if (!bktrReadSubStorage(&(ctx->substorages[0]), &params))
        {
            LOG_MSG_ERROR("Failed to read 0x%lX-byte long chunk at offset 0x%lX from AesCtrEx storage!", aes_ctr_ex_block_read_size, aes_ctr_ex_block_offset);
            goto end;
        }

        /* Update accumulator. */
        accum += aes_ctr_ex_block_read_size;
    }

    /* Update flag. */
    success = true;

end:
    return success;
}

static bool bktrGetCompressedStorageEntryExtents(BucketTreeVisitor *visitor, u64 offset, BucketTreeCompressedStorageEntry *out_cur_entry, u64 *out_next_entry_offset)
{
    if (!visitor || !out_cur_entry || !out_next_entry_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeContext *ctx = visitor->bktr_ctx;
    BucketTreeCompressedStorageEntry cur_entry = {0};
    u64 cur_entry_offset = 0, next_entry_offset = 0;
    bool success = false;

    /* Copy current Compressed Storage entry -- we'll move onto the next one, so we'll lose track of it. */
    memcpy(&cur_entry, visitor->entry, sizeof(BucketTreeCompressedStorageEntry));

    /* Validate Compressed Storage entry. */
    if (!bktrIsOffsetWithinStorageRange(ctx, (u64)cur_entry.virtual_offset) || (u64)cur_entry.virtual_offset > offset || cur_entry.compression_type == BucketTreeCompressedStorageCompressionType_2 || \
        cur_entry.compression_type > BucketTreeCompressedStorageCompressionType_LZ4 || (cur_entry.compression_type != BucketTreeCompressedStorageCompressionType_LZ4 && \
        cur_entry.compression_level != 0) || (cur_entry.compression_type == BucketTreeCompressedStorageCompressionType_None && cur_entry.physical_size != BKTR_COMPRESSION_INVALID_PHYS_SIZE) || \
        (cur_entry.compression_type != BucketTreeCompressedStorageCompressionType_None && cur_entry.physical_size == BKTR_COMPRESSION_INVALID_PHYS_SIZE) || \
        (cur_entry.compression_type == BucketTreeCompressedStorageCompressionType_LZ4 && (cur_entry.compression_level < BKTR_COMPRESSION_LEVEL_MIN || \
        cur_entry.compression_level > BKTR_COMPRESSION_LEVEL_MAX || !IS_ALIGNED(cur_entry.physical_offset, BKTR_COMPRESSION_PHYS_ALIGNMENT))))
    {
        LOG_DATA_ERROR(&cur_entry, sizeof(BucketTreeCompressedStorageEntry), "Invalid Compressed Storage entry! (#1). Entry dump:");
        goto end;
    }

    cur_entry_offset = (u64)cur_entry.virtual_offset;

    /* Check if we can retrieve the next entry. */
    if (bktrVisitorCanMoveNext(visitor))
    {
        /* Retrieve the next entry. */
        if (!bktrVisitorMoveNext(visitor))
        {
            LOG_MSG_ERROR("Failed to retrieve next Compressed Storage entry!");
            goto end;
        }

        /* Validate Compressed Storage entry. */
        BucketTreeCompressedStorageEntry *next_entry = (BucketTreeCompressedStorageEntry*)visitor->entry;
        if (!bktrIsOffsetWithinStorageRange(ctx, (u64)next_entry->virtual_offset) || next_entry->compression_type == BucketTreeCompressedStorageCompressionType_2 || \
            next_entry->compression_type > BucketTreeCompressedStorageCompressionType_LZ4 || \
            (next_entry->compression_type != BucketTreeCompressedStorageCompressionType_LZ4 && next_entry->compression_level != 0) || \
            (next_entry->compression_type == BucketTreeCompressedStorageCompressionType_None && next_entry->physical_size != BKTR_COMPRESSION_INVALID_PHYS_SIZE) || \
            (next_entry->compression_type != BucketTreeCompressedStorageCompressionType_None && next_entry->physical_size == BKTR_COMPRESSION_INVALID_PHYS_SIZE) || \
            (next_entry->compression_type == BucketTreeCompressedStorageCompressionType_LZ4 && (next_entry->compression_level < BKTR_COMPRESSION_LEVEL_MIN || \
            next_entry->compression_level > BKTR_COMPRESSION_LEVEL_MAX || !IS_ALIGNED(next_entry->physical_offset, BKTR_COMPRESSION_PHYS_ALIGNMENT))))
        {
            LOG_DATA_ERROR(next_entry, sizeof(BucketTreeCompressedStorageEntry), "Invalid Compressed Storage entry! (#2). Entry dump:");
            goto end;
        }

        /* Store next entry's virtual offset. */
        next_entry_offset = (u64)next_entry->virtual_offset;
    } else {
        /* Set the next entry offset to the storage's end. */
        next_entry_offset = ctx->end_offset;
    }

    /* Verify next entry offset. */
    if (next_entry_offset <= cur_entry_offset || offset >= next_entry_offset)
    {
        LOG_MSG_ERROR("Invalid virtual offset for the Compressed Storage's next entry! (0x%lX).", next_entry_offset);
        goto end;
    }

    /* Update variables. */
    memcpy(out_cur_entry, &cur_entry, sizeof(BucketTreeCompressedStorageEntry));
    *out_next_entry_offset = next_entry_offset;
    success = true;

end:
    return success;
}

static bool bktrReadCompressedStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset)
{
    BucketTreeContext *ctx = visitor->bktr_ctx;
    NcaFsSectionContext *nca_fs_ctx = ctx->nca_fs_ctx;
    u64 compressed_storage_base_offset = nca_fs_ctx->hash_region.size;

    BucketTreeCompressedStorageEntry cur_entry = {0};
    BucketTreeSubStorageReadParams params = {0};
    u64 cur_entry_offset = 0, next_entry_offset = 0, accum = 0;

    bool success = false;

    if (!out || !bktrIsValidSubStorage(&(ctx->substorages[0])) || ctx->substorages[0].type == BucketTreeSubStorageType_AesCtrEx || \
        ctx->substorages[0].type == BucketTreeSubStorageType_Compressed || ctx->substorages[0].type >= BucketTreeSubStorageType_Count || (offset + read_size) > ctx->end_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Perform Compressed Storage reads until we reach the requested size. */
    while(accum < read_size)
    {
        u8 *out_ptr = ((u8*)out + accum);
        const u64 compressed_block_offset = (offset + accum);
        u64 compressed_block_size = 0, compressed_block_read_size = 0, compressed_block_read_offset = 0, read_size_diff = 0;

        /* Get current Compressed Storage entry and the start offset for the next one. */
        if (!bktrGetCompressedStorageEntryExtents(visitor, compressed_block_offset, &cur_entry, &next_entry_offset))
        {
            LOG_MSG_ERROR("Failed to get Compressed Storage entry extents for offset 0x%lX!", compressed_block_offset);
            goto end;
        }

        /* Calculate Compressed Storage block size. */
        cur_entry_offset = cur_entry.virtual_offset;
        compressed_block_size = (!accum ? (next_entry_offset - offset) : (next_entry_offset - cur_entry_offset));

        /* Calculate Compressed Storage block read size. */
        read_size_diff = (read_size - accum);
        compressed_block_read_size = (read_size_diff > compressed_block_size ? compressed_block_size : read_size_diff);

        /* Perform read operation within the current Compressed Storage entry. */
        switch(cur_entry.compression_type)
        {
            case BucketTreeCompressedStorageCompressionType_None:
            {
                /* We can randomly access data that's not compressed. */
                /* Let's just read what we need. */
                compressed_block_read_offset = (compressed_storage_base_offset + (compressed_block_offset - cur_entry_offset + (u64)cur_entry.physical_offset));
                bktrInitializeSubStorageReadParams(&params, out_ptr, compressed_block_read_offset, compressed_block_read_size, 0, 0, false, ctx->storage_type);

                if (!bktrReadSubStorage(&(ctx->substorages[0]), &params))
                {
                    LOG_MSG_ERROR("Failed to read 0x%lX-byte long chunk from offset 0x%lX in non-compressed entry!", compressed_block_read_size, compressed_block_read_offset);
                    goto end;
                }

                break;
            }
            case BucketTreeCompressedStorageCompressionType_Zero:
            {
                /* Fill output buffer with zeroes. */
                memset(out_ptr, 0, compressed_block_read_size);
                break;
            }
            case BucketTreeCompressedStorageCompressionType_LZ4:
            {
                /* We can't randomly access data that's compressed. */
                /* Let's be lazy and allocate memory for the full entry, read it and then decompress it. */
                compressed_block_read_offset = (compressed_storage_base_offset + (u64)cur_entry.physical_offset);

                const u64 compressed_data_size = (u64)cur_entry.physical_size;
                const u64 decompressed_data_size = (next_entry_offset - cur_entry_offset);
                const u64 buffer_size = LZ4_DECOMPRESS_INPLACE_BUFFER_SIZE(decompressed_data_size);

                u8 *buffer = NULL, *read_ptr = NULL;

                buffer = calloc(1, buffer_size);
                if (!buffer)
                {
                    LOG_MSG_ERROR("Failed to allocate 0x%lX-byte long buffer for data decompression! (0x%lX).", buffer_size, decompressed_data_size);
                    goto end;
                }

                /* Adjust read pointer. This will let us use the same buffer for storing read data and decompressing it. */
                read_ptr = (buffer + (buffer_size - compressed_data_size));
                bktrInitializeSubStorageReadParams(&params, read_ptr, compressed_block_read_offset, compressed_data_size, 0, 0, false, ctx->storage_type);

                /* Read compressed LZ4 block. */
                if (!bktrReadSubStorage(&(ctx->substorages[0]), &params))
                {
                    LOG_MSG_ERROR("Failed to read 0x%lX-byte long compressed block from offset 0x%lX!", compressed_data_size, compressed_block_read_offset);
                    free(buffer);
                    goto end;
                }

                /* Decompress LZ4 block. */
                int lz4_res = LZ4_decompress_safe((char*)read_ptr, (char*)buffer, (int)compressed_data_size, (int)buffer_size);
                if (lz4_res != (int)decompressed_data_size)
                {
                    LOG_MSG_ERROR("Failed to decompress 0x%lX-byte long compressed block! (%d).", compressed_data_size, lz4_res);
                    free(buffer);
                    goto end;
                }

                /* Copy the data we need. */
                memcpy(out_ptr, buffer + (compressed_block_offset - cur_entry_offset), compressed_block_read_size);

                /* Free allocated buffer. */
                free(buffer);

                break;
            }
            default:
                break;
        }

        /* Update accumulator. */
        accum += compressed_block_read_size;
    }

    /* Update flag. */
    success = true;

end:
    return success;
}

static bool bktrReadSubStorage(BucketTreeSubStorage *substorage, BucketTreeSubStorageReadParams *params)
{
    if (!bktrIsValidSubStorage(substorage) || !params || !params->buffer || !params->size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = false;

    if (substorage->type == BucketTreeSubStorageType_Regular)
    {
        NcaFsSectionContext *nca_fs_ctx = substorage->nca_fs_ctx;

        if (params->parent_storage_type == BucketTreeStorageType_AesCtrEx)
        {
            /* Perform a read on the target NCA using AesCtrEx crypto. */
            success = ncaReadAesCtrExStorage(nca_fs_ctx, params->buffer, params->size, params->offset, params->ctr_val, params->aes_ctr_ex_crypt);
        } else {
            /* Make sure to handle Sparse virtual offsets if we need to. */
            if (params->parent_storage_type == BucketTreeStorageType_Sparse && params->virtual_offset) nca_fs_ctx->cur_sparse_virtual_offset = params->virtual_offset;

            /* Perform a read on the target NCA. */
            success = ncaReadFsSection(nca_fs_ctx, params->buffer, params->size, params->offset);
        }
    } else {
        /* Perform a read on the target BucketTree storage. */
        success = bktrReadStorage(substorage->bktr_ctx, params->buffer, params->size, params->offset);
    }

    if (!success) LOG_MSG_ERROR("Failed to read 0x%lX-byte long chunk from offset 0x%lX!", params->size, params->offset);

    return success;
}

NX_INLINE void bktrInitializeSubStorageReadParams(BucketTreeSubStorageReadParams *out, void *buffer, u64 offset, u64 size, u64 virtual_offset, u32 ctr_val, bool aes_ctr_ex_crypt, u8 parent_storage_type)
{
    out->buffer = buffer;
    out->offset = offset;
    out->size = size;
    out->virtual_offset = ((virtual_offset && parent_storage_type == BucketTreeStorageType_Sparse) ? virtual_offset : 0);
    out->ctr_val = ((ctr_val && parent_storage_type == BucketTreeStorageType_AesCtrEx) ? ctr_val : 0);
    out->aes_ctr_ex_crypt = ((aes_ctr_ex_crypt && parent_storage_type == BucketTreeStorageType_AesCtrEx) ? true : false);
    out->parent_storage_type = parent_storage_type;
}

static bool bktrVerifyBucketInfo(NcaBucketInfo *bucket, u64 node_size, u64 entry_size, u64 *out_node_storage_size, u64 *out_entry_storage_size)
{
    /* Verify bucket info properties. */
    if (!ncaVerifyBucketInfo(bucket)) return false;

    /* Validate table size. */
    u64 node_storage_size = bktrQueryNodeStorageSize(node_size, entry_size, bucket->header.entry_count);
    u64 entry_storage_size = bktrQueryEntryStorageSize(node_size, entry_size, bucket->header.entry_count);
    u64 calc_table_size = (node_storage_size + entry_storage_size);

    bool success = (bucket->size >= calc_table_size);
    if (success)
    {
        if (out_node_storage_size) *out_node_storage_size = node_storage_size;
        if (out_entry_storage_size) *out_entry_storage_size = entry_storage_size;
    } else {
        LOG_MSG_ERROR("Calculated table size exceeds the provided bucket's table size! (0x%lX > 0x%lX).", calc_table_size, bucket->size);
    }

    return success;
}

static bool bktrValidateTableOffsetNode(const BucketTreeTable *table, u64 node_size, u64 entry_size, u32 entry_count, u64 *out_start_offset, u64 *out_end_offset)
{
    const BucketTreeOffsetNode *offset_node = &(table->offset_node);
    const BucketTreeNodeHeader *node_header = &(offset_node->header);

    /* Verify offset node header. */
    if (!bktrVerifyNodeHeader(node_header, 0, node_size, sizeof(u64)))
    {
        LOG_MSG_ERROR("Bucket Tree Offset Node header verification failed!");
        return false;
    }

    /* Validate offsets. */
    u32 offset_count = bktrGetOffsetCount(node_size);
    u32 entry_set_count = bktrGetEntrySetCount(node_size, entry_size, entry_count);

    u64 node_start_offset = *bktrGetOffsetNodeBegin(offset_node);

    u64 start_offset = ((offset_count < entry_set_count && node_header->count < offset_count) ? *bktrGetOffsetNodeEnd(offset_node) : node_start_offset);
    u64 end_offset = node_header->offset;

    if (start_offset > node_start_offset || start_offset >= end_offset || node_header->count != entry_set_count)
    {
        LOG_MSG_ERROR("Invalid Bucket Tree Offset Node!");
        return false;
    }

    /* Update output offsets. */
    if (out_start_offset) *out_start_offset = start_offset;
    if (out_end_offset) *out_end_offset = end_offset;

    return true;
}

NX_INLINE bool bktrVerifyNodeHeader(const BucketTreeNodeHeader *node_header, u32 node_index, u64 node_size, u64 entry_size)
{
    return (node_header && node_header->index == node_index && entry_size > 0 && node_size >= (entry_size + BKTR_NODE_HEADER_SIZE) && \
            node_header->count > 0 && node_header->count <= ((node_size - BKTR_NODE_HEADER_SIZE) / entry_size));
}

static u64 bktrQueryNodeStorageSize(u64 node_size, u64 entry_size, u32 entry_count)
{
    if (entry_size < sizeof(u64) || node_size < (entry_size + BKTR_NODE_HEADER_SIZE) || node_size < BKTR_NODE_SIZE_MIN || node_size > BKTR_NODE_SIZE_MAX || \
        !IS_POWER_OF_TWO(node_size) || !entry_count) return 0;

    return ((1 + bktrGetNodeL2Count(node_size, entry_size, entry_count)) * node_size);
}

static u64 bktrQueryEntryStorageSize(u64 node_size, u64 entry_size, u32 entry_count)
{
    if (entry_size < sizeof(u64) || node_size < (entry_size + BKTR_NODE_HEADER_SIZE) || node_size < BKTR_NODE_SIZE_MIN || node_size > BKTR_NODE_SIZE_MAX || \
        !IS_POWER_OF_TWO(node_size) || !entry_count) return 0;

    return ((u64)bktrGetEntrySetCount(node_size, entry_size, entry_count) * node_size);
}

NX_INLINE u32 bktrGetEntryCount(u64 node_size, u64 entry_size)
{
    return (u32)((node_size - BKTR_NODE_HEADER_SIZE) / entry_size);
}

NX_INLINE u32 bktrGetOffsetCount(u64 node_size)
{
    return (u32)((node_size - BKTR_NODE_HEADER_SIZE) / sizeof(u64));
}

NX_INLINE u32 bktrGetEntrySetCount(u64 node_size, u64 entry_size, u32 entry_count)
{
    u32 entry_count_per_node = bktrGetEntryCount(node_size, entry_size);
    return DIVIDE_UP(entry_count, entry_count_per_node);
}

NX_INLINE u32 bktrGetNodeL2Count(u64 node_size, u64 entry_size, u32 entry_count)
{
    u32 offset_count_per_node = bktrGetOffsetCount(node_size);
    u32 entry_set_count = bktrGetEntrySetCount(node_size, entry_size, entry_count);

    if (entry_set_count <= offset_count_per_node) return 0;

    u32 node_l2_count = DIVIDE_UP(entry_set_count, offset_count_per_node);
    if (node_l2_count > offset_count_per_node) return 0;

    return DIVIDE_UP(entry_set_count - (offset_count_per_node - (node_l2_count - 1)), offset_count_per_node);
}

NX_INLINE const void *bktrGetNodeArray(const BucketTreeNodeHeader *node_header)
{
    return ((const u8*)node_header + BKTR_NODE_HEADER_SIZE);
}

NX_INLINE const u64 *bktrGetOffsetNodeArray(const BucketTreeOffsetNode *offset_node)
{
    return (const u64*)bktrGetNodeArray(&(offset_node->header));
}

NX_INLINE const u64 *bktrGetOffsetNodeBegin(const BucketTreeOffsetNode *offset_node)
{
    return bktrGetOffsetNodeArray(offset_node);
}

NX_INLINE const u64 *bktrGetOffsetNodeEnd(const BucketTreeOffsetNode *offset_node)
{
    return (bktrGetOffsetNodeArray(offset_node) + offset_node->header.count);
}

static bool bktrFindStorageEntry(BucketTreeContext *ctx, u64 virtual_offset, BucketTreeVisitor *out_visitor)
{
    if (!ctx || virtual_offset >= ctx->storage_table->offset_node.header.offset || !out_visitor)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Get the node. */
    const BucketTreeOffsetNode *offset_node = &(ctx->storage_table->offset_node);

    /* Get the entry node index. */
    u32 entry_set_index = 0;
    const u64 *node_start_ptr = bktrGetOffsetNodeBegin(offset_node), *node_end_ptr = bktrGetOffsetNodeEnd(offset_node);
    const u64 *start_ptr = NULL, *end_ptr = NULL;
    bool success = false;

    if (bktrIsExistOffsetL2OnL1(ctx) && virtual_offset < *node_start_ptr)
    {
        start_ptr = node_end_ptr;
        end_ptr = (node_start_ptr + ctx->offset_count);

        if (!bktrGetTreeNodeEntryIndex(start_ptr, end_ptr, virtual_offset, &entry_set_index))
        {
            LOG_MSG_ERROR("Failed to retrieve Bucket Tree Node entry index for virtual offset 0x%lX! (#1).", virtual_offset);
            goto end;
        }
    } else {
        start_ptr = node_start_ptr;
        end_ptr = node_end_ptr;

        if (!bktrGetTreeNodeEntryIndex(start_ptr, end_ptr, virtual_offset, &entry_set_index))
        {
            LOG_MSG_ERROR("Failed to retrieve Bucket Tree Node entry index for virtual offset 0x%lX! (#2).", virtual_offset);
            goto end;
        }

        if (bktrIsExistL2(ctx))
        {
            u32 node_index = entry_set_index;
            if (node_index >= ctx->offset_count || !bktrFindEntrySet(ctx, &entry_set_index, virtual_offset, node_index))
            {
                LOG_MSG_ERROR("Invalid L2 Bucket Tree Node index!");
                goto end;
            }
        }
    }

    /* Validate the entry set index. */
    if (entry_set_index >= ctx->entry_set_count)
    {
        LOG_MSG_ERROR("Invalid Bucket Tree Node offset!");
        goto end;
    }

    /* Find the entry. */
    success = bktrFindEntry(ctx, out_visitor, virtual_offset, entry_set_index);
    if (!success) LOG_MSG_ERROR("Failed to retrieve storage entry!");

end:
    return success;
}

static bool bktrGetTreeNodeEntryIndex(const u64 *start_ptr, const u64 *end_ptr, u64 virtual_offset, u32 *out_index)
{
    if (!start_ptr || !end_ptr || start_ptr >= end_ptr || !out_index)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Perform a binary search. */
    u32 offset_count = (u32)(end_ptr - start_ptr), low = 0, high = (offset_count - 1);
    bool ret = false;

    while(low <= high)
    {
        /* Get the index to the middle offset within our current lookup range. */
        u32 half = ((low + high) / 2);

        /* Check middle offset value. */
        const u64 *ptr = (start_ptr + half);
        if (*ptr > virtual_offset)
        {
            /* Update our upper limit. */
            high = (half - 1);
        } else {
            /* Check for success. */
            if (half == (offset_count - 1) || *(ptr + 1) > virtual_offset)
            {
                /* Update output. */
                *out_index = half;
                ret = true;
                break;
            }

            /* Update our lower limit. */
            low = (half + 1);
        }
    }

    return ret;
}

static bool bktrGetEntryNodeEntryIndex(const BucketTreeNodeHeader *node_header, u64 entry_size, u64 virtual_offset, u32 *out_index)
{
    if (!node_header || !out_index)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Initialize storage node and find the index for our virtual offset. */
    BucketTreeStorageNode storage_node = {0};
    bktrInitializeStorageNode(&storage_node, entry_size, node_header->count);
    bktrStorageNodeFind(&storage_node, node_header, virtual_offset);

    /* Validate index. */
    if (storage_node.index == UINT32_MAX)
    {
        LOG_MSG_ERROR("Unable to find index for virtual offset 0x%lX!", virtual_offset);
        return false;
    }

    /* Update output index. */
    *out_index = storage_node.index;

    return true;
}

static bool bktrFindEntrySet(BucketTreeContext *ctx, u32 *out_index, u64 virtual_offset, u32 node_index)
{
    /* Get offset node header. */
    const BucketTreeNodeHeader *node_header = bktrGetTreeNodeHeader(ctx, node_index);
    if (!node_header)
    {
        LOG_MSG_ERROR("Failed to retrieve offset node header at index 0x%X!", node_index);
        return false;
    }

    /* Get offset node entry index. */
    u32 offset_index = 0;
    if (!bktrGetEntryNodeEntryIndex(node_header, sizeof(u64), virtual_offset, &offset_index))
    {
        LOG_MSG_ERROR("Failed to get offset node entry index!");
        return false;
    }

    /* Update output index. */
    *out_index = bktrGetEntrySetIndex(ctx, node_header->index, offset_index);

    return true;
}

static const BucketTreeNodeHeader *bktrGetTreeNodeHeader(BucketTreeContext *ctx, u32 node_index)
{
    /* Calculate offset node extents. */
    const u64 node_size = ctx->node_size;
    const u64 node_offset = ((node_index + 1) * node_size);

    if ((node_offset + BKTR_NODE_HEADER_SIZE) > ctx->node_storage_size)
    {
        LOG_MSG_ERROR("Invalid Bucket Tree Offset Node offset!");
        return NULL;
    }

    /* Get offset node header. */
    const BucketTreeNodeHeader *node_header = (const BucketTreeNodeHeader*)((u8*)ctx->storage_table + node_offset);

    /* Validate offset node header. */
    if (!bktrVerifyNodeHeader(node_header, node_index, node_size, sizeof(u64)))
    {
        LOG_MSG_ERROR("Bucket Tree Offset Node header verification failed!");
        return NULL;
    }

    return node_header;
}

NX_INLINE u32 bktrGetEntrySetIndex(BucketTreeContext *ctx, u32 node_index, u32 offset_index)
{
    return (u32)((ctx->offset_count - ctx->storage_table->offset_node.header.count) + (ctx->offset_count * node_index) + offset_index);
}

static bool bktrFindEntry(BucketTreeContext *ctx, BucketTreeVisitor *out_visitor, u64 virtual_offset, u32 entry_set_index)
{
    /* Get entry node header. */
    const BucketTreeNodeHeader *entry_set_header = bktrGetEntryNodeHeader(ctx, entry_set_index);
    if (!entry_set_header)
    {
        LOG_MSG_ERROR("Failed to retrieve entry node header at index 0x%X!", entry_set_index);
        return false;
    }

    /* Calculate entry node extents. */
    const u64 entry_size = ctx->entry_size;
    const u64 entry_set_size = ctx->node_size;
    const u64 entry_set_offset = (ctx->node_storage_size + (entry_set_index * entry_set_size));

    /* Get entry node entry index. */
    u32 entry_index = 0;
    if (!bktrGetEntryNodeEntryIndex(entry_set_header, entry_size, virtual_offset, &entry_index))
    {
        LOG_MSG_ERROR("Failed to get entry node entry index!");
        return false;
    }

    /* Get entry node entry offset and validate it. */
    u64 entry_offset = bktrGetEntryNodeEntryOffset(entry_set_offset, entry_size, entry_index);
    if ((entry_offset + entry_size) > (ctx->node_storage_size + ctx->entry_storage_size))
    {
        LOG_MSG_ERROR("Invalid Bucket Tree Entry Node entry offset!");
        return false;
    }

    /* Update output visitor. */
    memset(out_visitor, 0, sizeof(BucketTreeVisitor));

    out_visitor->bktr_ctx = ctx;
    memcpy(&(out_visitor->entry_set), entry_set_header, sizeof(BucketTreeEntrySetHeader));
    out_visitor->entry_index = entry_index;
    out_visitor->entry = ((u8*)ctx->storage_table + entry_offset);

    return true;
}

static const BucketTreeNodeHeader *bktrGetEntryNodeHeader(BucketTreeContext *ctx, u32 entry_set_index)
{
    /* Calculate entry node extents. */
    const u64 entry_size = ctx->entry_size;
    const u64 entry_set_size = ctx->node_size;
    const u64 entry_set_offset = (ctx->node_storage_size + (entry_set_index * entry_set_size));

    if ((entry_set_offset + BKTR_NODE_HEADER_SIZE) > (ctx->node_storage_size + ctx->entry_storage_size))
    {
        LOG_MSG_ERROR("Invalid Bucket Tree Entry Node offset!");
        return NULL;
    }

    /* Get entry node header. */
    const BucketTreeNodeHeader *entry_set_header = (const BucketTreeNodeHeader*)((u8*)ctx->storage_table + entry_set_offset);

    /* Validate entry node header. */
    if (!bktrVerifyNodeHeader(entry_set_header, entry_set_index, entry_set_size, entry_size))
    {
        LOG_MSG_ERROR("Bucket Tree Entry Node header verification failed!");
        return NULL;
    }

    return entry_set_header;
}

NX_INLINE u64 bktrGetEntryNodeEntryOffset(u64 entry_set_offset, u64 entry_size, u32 entry_index)
{
    return (entry_set_offset + BKTR_NODE_HEADER_SIZE + ((u64)entry_index * entry_size));
}

NX_INLINE u64 bktrGetEntryNodeEntryOffsetByIndex(u32 entry_set_index, u64 node_size, u64 entry_size, u32 entry_index)
{
    return bktrGetEntryNodeEntryOffset((u64)entry_set_index * node_size, entry_size, entry_index);
}

NX_INLINE bool bktrIsExistL2(BucketTreeContext *ctx)
{
    return (ctx->offset_count < ctx->entry_set_count);
}

NX_INLINE bool bktrIsExistOffsetL2OnL1(BucketTreeContext *ctx)
{
    return (bktrIsExistL2(ctx) && ctx->storage_table->offset_node.header.count < ctx->offset_count);
}

static void bktrInitializeStorageNode(BucketTreeStorageNode *out, u64 entry_size, u32 entry_count)
{
    out->start.offset = BKTR_NODE_HEADER_SIZE;
    out->start.stride = (u32)entry_size;
    out->count = entry_count;
    out->index = UINT32_MAX;
}

static void bktrStorageNodeFind(BucketTreeStorageNode *storage_node, const BucketTreeNodeHeader *node_header, u64 virtual_offset)
{
    /* Check for edge case, short circuit. */
    if (storage_node->count == 1)
    {
        storage_node->index = 0;
        return;
    }

    /* Perform a binary search. */
    u32 entry_count = storage_node->count, low = 0, high = (entry_count - 1);
    BucketTreeStorageNodeOffset *start = &(storage_node->start);

    while(low <= high)
    {
        /* Get the offset to the middle entry within our current lookup range. */
        u32 half = ((low + high) / 2);
        BucketTreeStorageNodeOffset mid = bktrStorageNodeOffsetAdd(start, half);

        /* Check middle entry's virtual offset. */
        if (bktrStorageNodeOffsetGetEntryVirtualOffset(node_header, &mid) > virtual_offset)
        {
            /* Update our upper limit. */
            high = (half - 1);
        } else {
            /* Check for success. */
            BucketTreeStorageNodeOffset pos = bktrStorageNodeOffsetAdd(&mid, 1);
            if (half == (entry_count - 1) || bktrStorageNodeOffsetGetEntryVirtualOffset(node_header, &pos) > virtual_offset)
            {
                storage_node->index = half;
                break;
            }

            /* Update our lower limit. */
            low = (half + 1);
        }
    }
}

NX_INLINE BucketTreeStorageNodeOffset bktrStorageNodeOffsetAdd(BucketTreeStorageNodeOffset *ofs, u64 value)
{
    BucketTreeStorageNodeOffset out = { ofs->offset + (value * (u64)ofs->stride), ofs->stride };
    return out;
}

NX_INLINE const u64 bktrStorageNodeOffsetGetEntryVirtualOffset(const BucketTreeNodeHeader *node_header, const BucketTreeStorageNodeOffset *ofs)
{
    return *((const u64*)((const u8*)node_header + ofs->offset));
}

NX_INLINE bool bktrVisitorIsValid(BucketTreeVisitor *visitor)
{
    return (visitor && visitor->bktr_ctx && visitor->entry_index != UINT32_MAX);
}

NX_INLINE bool bktrVisitorCanMoveNext(BucketTreeVisitor *visitor)
{
    return (bktrVisitorIsValid(visitor) && ((visitor->entry_index + 1) < visitor->entry_set.header.count || (visitor->entry_set.header.index + 1) < visitor->bktr_ctx->entry_set_count));
}

static bool bktrVisitorMoveNext(BucketTreeVisitor *visitor)
{
    if (!bktrVisitorIsValid(visitor))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    BucketTreeContext *ctx = visitor->bktr_ctx;
    BucketTreeEntrySetHeader *entry_set = &(visitor->entry_set);
    u32 entry_index = (visitor->entry_index + 1);
    bool success = false;

    /* Invalidate index. */
    visitor->entry_index = UINT32_MAX;

    if (entry_index == entry_set->header.count)
    {
        /* We have reached the end of this entry node. Let's try to retrieve the first entry from the next one. */
        const u32 entry_set_index = (entry_set->header.index + 1);
        if (entry_set_index >= ctx->entry_set_count)
        {
            LOG_MSG_ERROR("Error: attempting to move visitor into non-existing Bucket Tree Entry Node!");
            goto end;
        }

        /* Read next entry set header. */
        const u64 end_offset = entry_set->header.offset;
        const u64 entry_set_size = ctx->node_size;
        const u64 entry_set_offset = (ctx->node_storage_size + (entry_set_index * entry_set_size));

        if ((entry_set_offset + sizeof(BucketTreeEntrySetHeader)) > (ctx->node_storage_size + ctx->entry_storage_size))
        {
            LOG_MSG_ERROR("Invalid Bucket Tree Entry Node offset!");
            goto end;
        }

        memcpy(entry_set, (u8*)ctx->storage_table + entry_set_offset, sizeof(BucketTreeEntrySetHeader));

        /* Validate next entry set header. */
        if (!bktrVerifyNodeHeader(&(entry_set->header), entry_set_index, entry_set_size, ctx->entry_size) || entry_set->start != end_offset || \
            entry_set->start >= entry_set->header.offset)
        {
            LOG_MSG_ERROR("Bucket Tree Entry Node header verification failed!");
            goto end;
        }

        /* Update entry index. */
        entry_index = 0;
    }

    /* Get the new entry. */
    const u64 entry_size = ctx->entry_size;
    const u64 entry_offset = (ctx->node_storage_size + bktrGetEntryNodeEntryOffsetByIndex(entry_set->header.index, ctx->node_size, entry_size, entry_index));

    if ((entry_offset + entry_size) > (ctx->node_storage_size + ctx->entry_storage_size))
    {
        LOG_MSG_ERROR("Invalid Bucket Tree Entry Node entry offset!");
        goto end;
    }

    /* Update visitor. */
    visitor->entry_index = entry_index;
    visitor->entry = ((u8*)ctx->storage_table + entry_offset);

    /* Update return value. */
    success = true;

end:
    return success;
}
