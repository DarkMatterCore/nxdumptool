/*
 * bktr.c
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

#include "nxdt_utils.h"
#include "bktr.h"
#include "aes.h"
#include "lz4.h"

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

static const char *g_bktrStorageTypeNames[] = {
    [BucketTreeStorageType_Indirect]   = "Indirect",
    [BucketTreeStorageType_AesCtrEx]   = "AesCtrEx",
    [BucketTreeStorageType_Compressed] = "Compressed",
    [BucketTreeStorageType_Sparse]     = "Sparse"
};

/* Function prototypes. */

static const char *bktrGetStorageTypeName(u8 storage_type);

static bool bktrInitializeIndirectStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, bool is_sparse);
static bool bktrReadIndirectStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset);

static bool bktrInitializeAesCtrExStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx);
static bool bktrReadAesCtrExStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset);

static bool bktrInitializeCompressedStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx);
static bool bktrReadCompressedStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset);

static bool bktrReadSubStorage(BucketTreeSubStorage *substorage, BucketTreeSubStorageReadParams *params);
NX_INLINE void bktrBucketInitializeSubStorageReadParams(BucketTreeSubStorageReadParams *out, void *buffer, u64 offset, u64 size, u64 virtual_offset, u32 ctr_val, bool aes_ctr_ex_crypt, u8 parent_storage_type);

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

static bool bktrFindEntrySet(u32 *out_index, u64 virtual_offset, u32 node_index);
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
NX_INLINE u64 bktrStorageNodeOffsetSubstract(BucketTreeStorageNodeOffset *ofs1, BucketTreeStorageNodeOffset *ofs2);

NX_INLINE bool bktrVisitorIsValid(BucketTreeVisitor *visitor);
NX_INLINE bool bktrVisitorCanMoveNext(BucketTreeVisitor *visitor);
NX_INLINE bool bktrVisitorCanMovePrevious(BucketTreeVisitor *visitor);

static bool bktrVisitorMoveNext(BucketTreeVisitor *visitor);
static bool bktrVisitorMovePrevious(BucketTreeVisitor *visitor);

bool bktrInitializeContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, u8 storage_type)
{
    NcaContext *nca_ctx = NULL;
    
    if (!out || storage_type >= BucketTreeStorageType_Count || !nca_fs_ctx || !nca_fs_ctx->enabled || nca_fs_ctx->section_type >= NcaFsSectionType_Invalid || \
        !(nca_ctx = (NcaContext*)nca_fs_ctx->nca_ctx) || (nca_ctx->rights_id_available && !nca_ctx->titlekey_retrieved))
    {
        LOG_MSG("Invalid parameters!");
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
        case BucketTreeStorageType_Compressed:
            success = bktrInitializeCompressedStorageContext(out, nca_fs_ctx);
            break;
        default:
            break;
    }
    
    if (!success) LOG_MSG("Failed to initialize Bucket Tree %s storage for FS section #%u in \"%s\".", bktrGetStorageTypeName(storage_type), nca_fs_ctx->section_idx, \
                          nca_ctx->content_id_str);
    
    return success;
}

bool bktrSetRegularSubStorage(BucketTreeContext *ctx, NcaFsSectionContext *nca_fs_ctx)
{
    NcaContext *nca_ctx = NULL;
    
    if (!bktrIsValidContext(ctx) || !nca_fs_ctx || !nca_fs_ctx->enabled || nca_fs_ctx->section_type >= NcaFsSectionType_Invalid || \
        !(nca_ctx = (NcaContext*)nca_fs_ctx->nca_ctx) || (nca_ctx->rights_id_available && !nca_ctx->titlekey_retrieved) || \
        (ctx->storage_type >= BucketTreeStorageType_AesCtrEx && ctx->storage_type <= BucketTreeStorageType_Sparse && ctx->nca_fs_ctx != nca_fs_ctx))
    {
        LOG_MSG("Invalid parameters!");
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
        (parent_ctx->storage_type != BucketTreeStorageType_Indirect && substorage_index != 0) || \
        (parent_ctx->storage_type == BucketTreeStorageType_Indirect && child_ctx->storage_type != BucketTreeStorageType_Compressed && child_ctx->storage_type != BucketTreeStorageType_AesCtrEx) || \
        (parent_ctx->storage_type == BucketTreeStorageType_Indirect && child_ctx->storage_type == BucketTreeStorageType_Compressed && (substorage_index != 0 || parent_ctx->nca_fs_ctx == child_ctx->nca_fs_ctx)) || \
        (parent_ctx->storage_type == BucketTreeStorageType_Indirect && child_ctx->storage_type == BucketTreeStorageType_AesCtrEx && (substorage_index != 1 || parent_ctx->nca_fs_ctx != child_ctx->nca_fs_ctx)) || \
        parent_ctx->storage_type == BucketTreeStorageType_AesCtrEx || parent_ctx->storage_type == BucketTreeStorageType_Sparse || \
        (parent_ctx->storage_type == BucketTreeStorageType_Compressed && child_ctx->storage_type != BucketTreeStorageType_Indirect && child_ctx->storage_type != BucketTreeStorageType_Sparse) || \
        (parent_ctx->storage_type == BucketTreeStorageType_Compressed && parent_ctx->nca_fs_ctx != child_ctx->nca_fs_ctx))
    {
        LOG_MSG("Invalid parameters!");
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
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    BucketTreeVisitor visitor = {0};
    bool success = false;
    
    /* Find storage entry. */
    if (!bktrFindStorageEntry(ctx, offset, &visitor))
    {
        LOG_MSG("Unable to find %s storage entry for offset 0x%lX!", bktrGetStorageTypeName(ctx->storage_type), offset);
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
    
    if (!success) LOG_MSG("Failed to read 0x%lX-byte long block at offset 0x%lX from %s storage!", read_size, offset, bktrGetStorageTypeName(ctx->storage_type));
    
end:
    return success;
}








































































static const char *bktrGetStorageTypeName(u8 storage_type)
{
    return (storage_type < BucketTreeStorageType_Count ? g_bktrStorageTypeNames[storage_type] : NULL);
}

static bool bktrInitializeIndirectStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx, bool is_sparse)
{
    if ((!is_sparse && nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs) || (is_sparse && !nca_fs_ctx->has_sparse_layer))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    NcaContext *nca_ctx = (NcaContext*)nca_fs_ctx->nca_ctx;
    NcaBucketInfo *indirect_bucket = (is_sparse ? &(nca_fs_ctx->header.sparse_info.bucket) : &(nca_fs_ctx->header.patch_info.indirect_bucket));
    BucketTreeTable *indirect_table = NULL;
    u64 node_storage_size = 0, entry_storage_size = 0;
    bool success = false;
    
    /* Verify bucket info. */
    if (!bktrVerifyBucketInfo(indirect_bucket, BKTR_NODE_SIZE, BKTR_INDIRECT_ENTRY_SIZE, &node_storage_size, &entry_storage_size))
    {
        LOG_MSG("Indirect Storage BucketInfo verification failed! (%s).", is_sparse ? "sparse" : "patch");
        goto end;
    }
    
    /* Allocate memory for the full indirect table. */
    indirect_table = calloc(1, indirect_bucket->size);
    if (!indirect_table)
    {
        LOG_MSG("Unable to allocate memory for the Indirect Storage Table! (%s).", is_sparse ? "sparse" : "patch");
        goto end;
    }
    
    /* Read indirect storage table data. */
    if ((!is_sparse && !ncaReadFsSection(nca_fs_ctx, indirect_table, indirect_bucket->size, indirect_bucket->offset)) || \
        (is_sparse && !ncaReadContentFile((NcaContext*)nca_fs_ctx->nca_ctx, indirect_table, indirect_bucket->size, nca_fs_ctx->sparse_table_offset)))
    {
        LOG_MSG("Failed to read Indirect Storage Table data! (%s).", is_sparse ? "sparse" : "patch");
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
    
    /* Validate table offset node. */
    u64 start_offset = 0, end_offset = 0;
    if (!bktrValidateTableOffsetNode(indirect_table, BKTR_NODE_SIZE, BKTR_INDIRECT_ENTRY_SIZE, indirect_bucket->header.entry_count, &start_offset, &end_offset))
    {
        LOG_MSG("Indirect Storage Table Offset Node validation failed! (%s).", is_sparse ? "sparse" : "patch");
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
    if (!success && indirect_table) free(indirect_table);
    
    return success;
}

static bool bktrReadIndirectStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset)
{
    BucketTreeContext *ctx = visitor->bktr_ctx;
    NcaFsSectionContext *nca_fs_ctx = ctx->nca_fs_ctx;
    bool is_sparse = (ctx->storage_type == BucketTreeStorageType_Sparse);
    
    if (!out || !bktrIsValidSubstorage(&(ctx->substorages[0])) || (!is_sparse && !bktrIsValidSubstorage(&(ctx->substorages[1]))) || \
        (!is_sparse && ((ctx->substorages[0].type != BucketTreeSubStorageType_Regular && ctx->substorages[0].type != BucketTreeStorageType_Compressed) || ctx->substorages[1].type != BucketTreeSubStorageType_AesCtrEx)) || \
        (is_sparse && ctx->substorages[0].type != BucketTreeSubStorageType_Regular))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Validate Indirect Storage entry. */
    BucketTreeIndirectStorageEntry cur_entry = {0};
    memcpy(&cur_entry, visitor->entry, sizeof(BucketTreeIndirectStorageEntry));
    
    if (!bktrIsOffsetWithinStorageRange(ctx, cur_entry.virtual_offset) || cur_entry.virtual_offset > offset || cur_entry.storage_index > BucketTreeIndirectStorageIndex_Patch)
    {
        LOG_MSG("Invalid Indirect Storage entry! (0x%lX) (#1).", cur_entry.virtual_offset);
        return false;
    }
    
    u64 cur_entry_offset = cur_entry.virtual_offset, next_entry_offset = 0;
    bool moved = false, success = false;
    
    /* Check if we can retrieve the next entry. */
    if (bktrVisitorCanMoveNext(visitor))
    {
        /* Retrieve the next entry. */
        if (!bktrVisitorMoveNext(visitor))
        {
            LOG_MSG("Failed to retrieve next Indirect Storage entry!");
            goto end;
        }
        
        /* Validate Indirect Storage entry. */
        BucketTreeIndirectStorageEntry *next_entry = (BucketTreeIndirectStorageEntry*)visitor->entry;
        if (!bktrIsOffsetWithinStorageRange(ctx, next_entry->virtual_offset) || next_entry->storage_index > BucketTreeIndirectStorageIndex_Patch)
        {
            LOG_MSG("Invalid Indirect Storage entry! (0x%lX) (#2).", next_entry->virtual_offset);
            goto end;
        }
        
        /* Store next entry's virtual offset. */
        next_entry_offset = next_entry->virtual_offset;
        
        /* Update variable. */
        moved = true;
    } else {
        /* Set the next entry offset to the storage's end. */
        next_entry_offset = ctx->end_offset;
    }
    
    /* Verify next entry offset. */
    if (next_entry_offset <= cur_entry_offset || offset >= next_entry_offset)
    {
        LOG_MSG("Invalid virtual offset for the Indirect Storage's next entry! (0x%lX).", next_entry_offset);
        goto end;
    }
    
    /* Verify read area size. */
    if ((offset + read_size) > ctx->end_offset)
    {
        LOG_MSG("Error: read area exceeds Indirect Storage size!");
        goto end;
    }
    
    /* Perform read operation. */
    if ((offset + read_size) <= next_entry_offset)
    {
        /* Read only within the current indirect storage entry. */
        BucketTreeSubStorageReadParams params = {0};
        const u64 data_offset = (offset - cur_entry_offset + cur_entry.physical_offset);
        bktrBucketInitializeSubStorageReadParams(&params, out, data_offset, read_size, offset, 0, false, ctx->storage_type);
        
        if (cur_entry.storage_index == BucketTreeIndirectStorageIndex_Original)
        {
            /* Retrieve data from the original data storage. */
            /* This may either be a Regular/Compressed storage from the base NCA (Indirect) or a Regular storage from this very same NCA (Sparse). */
            success = bktrReadSubStorage(&(ctx->substorages[0]), &params);
            if (!success) LOG_MSG("Failed to read 0x%lX-byte long chunk from offset 0x%lX in original data storage!", read_size, data_offset);
        } else {
            if (!is_sparse)
            {
                /* Retrieve data from the indirect data storage. */
                /* This must always be the AesCtrEx storage within this very same NCA (Indirect). */
                success = bktrReadSubStorage(&(ctx->substorages[1]), &params);
                if (!success) LOG_MSG("Failed to read 0x%lX-byte long chunk from offset 0x%lX in AesCtrEx storage!", read_size, data_offset);
            } else {
                /* Fill output buffer with zeroes (SparseStorage's ZeroStorage). */
                memset(0, out, read_size);
                success = true;
            }
        }
    } else {
        /* Handle reads that span multiple indirect storage entries. */
        if (moved) bktrVisitorMovePrevious(visitor);
        
        const u64 indirect_block_size = (next_entry_offset - offset);
        
        success = (bktrReadIndirectStorage(visitor, out, indirect_block_size, offset) && \
                   bktrReadIndirectStorage(visitor, (u8*)out + indirect_block_size, read_size - indirect_block_size, offset + indirect_block_size));
        
        if (!success) LOG_MSG("Failed to read 0x%lX bytes block from multiple Indirect Storage entries at offset 0x%lX!", read_size, offset);
    }
    
end:
    return success;
}

static bool bktrInitializeAesCtrExStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    if (nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || !nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket.size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    NcaContext *nca_ctx = (NcaContext*)nca_fs_ctx->nca_ctx;
    NcaBucketInfo *aes_ctr_ex_bucket = &(nca_fs_ctx->header.patch_info.aes_ctr_ex_bucket);
    BucketTreeTable *aes_ctr_ex_table = NULL;
    u64 node_storage_size = 0, entry_storage_size = 0;
    bool success = false;
    
    /* Verify bucket info. */
    if (!bktrVerifyBucketInfo(aes_ctr_ex_bucket, BKTR_NODE_SIZE, BKTR_AES_CTR_EX_ENTRY_SIZE, &node_storage_size, &entry_storage_size))
    {
        LOG_MSG("AesCtrEx Storage BucketInfo verification failed!");
        goto end;
    }
    
    /* Allocate memory for the full AesCtrEx table. */
    aes_ctr_ex_table = calloc(1, aes_ctr_ex_bucket->size);
    if (!aes_ctr_ex_table)
    {
        LOG_MSG("Unable to allocate memory for the AesCtrEx Storage Table!");
        goto end;
    }
    
    /* Read AesCtrEx storage table data. */
    if (!ncaReadFsSection(nca_fs_ctx, aes_ctr_ex_table, aes_ctr_ex_bucket->size, aes_ctr_ex_bucket->offset))
    {
        LOG_MSG("Failed to read AesCtrEx Storage Table data!");
        goto end;
    }
    
    /* Validate table offset node. */
    u64 start_offset = 0, end_offset = 0;
    if (!bktrValidateTableOffsetNode(aes_ctr_ex_table, BKTR_NODE_SIZE, BKTR_AES_CTR_EX_ENTRY_SIZE, aes_ctr_ex_bucket->header.entry_count, &start_offset, &end_offset))
    {
        LOG_MSG("AesCtrEx Storage Table Offset Node validation failed!");
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
    if (!success && aes_ctr_ex_table) free(aes_ctr_ex_table);
    
    return success;
}

static bool bktrReadAesCtrExStorage(BucketTreeVisitor *visitor, void *out, u64 read_size, u64 offset)
{
    BucketTreeContext *ctx = visitor->bktr_ctx;
    NcaFsSectionContext *nca_fs_ctx = ctx->nca_fs_ctx;
    
    if (!out || !bktrIsValidSubstorage(&(ctx->substorages[0])) || ctx->substorages[0].type != BucketTreeSubStorageType_Regular)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Validate AesCtrEx Storage entry. */
    BucketTreeAesCtrExStorageEntry cur_entry = {0};
    memcpy(&cur_entry, visitor->entry, sizeof(BucketTreeAesCtrExStorageEntry));
    
    if (!bktrIsOffsetWithinStorageRange(ctx, cur_entry.offset) || cur_entry.offset > offset || !IS_ALIGNED(cur_entry.offset, AES_BLOCK_SIZE))
    {
        LOG_MSG("Invalid AesCtrEx Storage entry! (0x%lX) (#1).", cur_entry.offset);
        return false;
    }
    
    u64 cur_entry_offset = cur_entry.offset, next_entry_offset = 0;
    bool moved = false, success = false;
    
    /* Check if we can retrieve the next entry. */
    if (bktrVisitorCanMoveNext(visitor))
    {
        /* Retrieve the next entry. */
        if (!bktrVisitorMoveNext(visitor))
        {
            LOG_MSG("Failed to retrieve next AesCtrEx Storage entry!");
            goto end;
        }
        
        /* Validate AesCtrEx Storage entry. */
        BucketTreeAesCtrExStorageEntry *next_entry = (BucketTreeAesCtrExStorageEntry*)visitor->entry;
        if (!bktrIsOffsetWithinStorageRange(ctx, next_entry->offset))
        {
            LOG_MSG("Invalid AesCtrEx Storage entry! (0x%lX) (#2).", next_entry->offset);
            goto end;
        }
        
        /* Store next entry's virtual offset. */
        next_entry_offset = next_entry->offset;
        
        /* Update variable. */
        moved = true;
    } else {
        /* Set the next entry offset to the storage's end. */
        next_entry_offset = ctx->end_offset;
    }
    
    /* Verify next entry offset. */
    if (!IS_ALIGNED(next_entry_offset, AES_BLOCK_SIZE) || next_entry_offset <= cur_entry_offset || offset >= next_entry_offset)
    {
        LOG_MSG("Invalid offset for the AesCtrEx Storage's next entry! (0x%lX).", next_entry_offset);
        goto end;
    }
    
    /* Verify read area size. */
    if ((offset + read_size) > ctx->end_offset)
    {
        LOG_MSG("Error: read area exceeds AesCtrEx Storage size!");
        goto end;
    }
    
    /* Perform read operation. */
    if ((offset + read_size) <= next_entry_offset)
    {
        /* Read only within the current AesCtrEx storage entry. */
        BucketTreeSubStorageReadParams params = {0};
        bktrBucketInitializeSubStorageReadParams(&params, out, offset, read_size, 0, cur_entry.generation, cur_entry.encryption == BucketTreeAesCtrExStorageEncryption_Enabled, ctx->storage_type);
        
        success = bktrReadSubStorage(&(ctx->substorages[0]), &params);
        if (!success) LOG_MSG("Failed to read 0x%lX-byte long chunk at offset 0x%lX from AesCtrEx storage!", read_size, offset);
    } else {
        /* Handle reads that span multiple AesCtrEx storage entries. */
        if (moved) bktrVisitorMovePrevious(visitor);
        
        const u64 aes_ctr_ex_block_size = (next_entry_offset - offset);
        
        success = (bktrReadAesCtrExStorage(visitor, out, aes_ctr_ex_block_size, offset) && \
                   bktrReadAesCtrExStorage(visitor, (u8*)out + aes_ctr_ex_block_size, read_size - aes_ctr_ex_block_size, offset + aes_ctr_ex_block_size));
        
        if (!success) LOG_MSG("Failed to read 0x%lX bytes block from multiple AesCtrEx Storage entries at offset 0x%lX!", read_size, offset);
    }
    
end:
    return success;
}

static bool bktrInitializeCompressedStorageContext(BucketTreeContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    if (!nca_fs_ctx->has_compression_layer)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    NcaContext *nca_ctx = (NcaContext*)nca_fs_ctx->nca_ctx;
    NcaBucketInfo *compressed_bucket = &(nca_fs_ctx->header.compression_info.bucket);
    BucketTreeTable *compressed_table = NULL;
    u64 node_storage_size = 0, entry_storage_size = 0;
    bool success = false;
    
    /* Verify bucket info. */
    if (!bktrVerifyBucketInfo(compressed_bucket, BKTR_NODE_SIZE, BKTR_COMPRESSED_ENTRY_SIZE, &node_storage_size, &entry_storage_size))
    {
        LOG_MSG("Compressed Storage BucketInfo verification failed!");
        goto end;
    }
    
    /* Allocate memory for the full Compressed table. */
    compressed_table = calloc(1, compressed_bucket->size);
    if (!compressed_table)
    {
        LOG_MSG("Unable to allocate memory for the Compressed Storage Table!");
        goto end;
    }
    
    /* Read Compressed storage table data. */
    if (!ncaReadFsSection(nca_fs_ctx, compressed_table, compressed_bucket->size, nca_fs_ctx->compression_table_offset))
    {
        LOG_MSG("Failed to read Compressed Storage Table data!");
        goto end;
    }
    
    /* Validate table offset node. */
    u64 start_offset = 0, end_offset = 0;
    if (!bktrValidateTableOffsetNode(compressed_table, BKTR_NODE_SIZE, BKTR_COMPRESSED_ENTRY_SIZE, compressed_bucket->header.entry_count, &start_offset, &end_offset))
    {
        LOG_MSG("Compressed Storage Table Offset Node validation failed!");
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
    
    /* Update return value. */
    success = true;
    
end:
    if (!success && compressed_table) free(compressed_table);
    
    return success;
}

static bool bktrReadSubStorage(BucketTreeSubStorage *substorage, BucketTreeSubStorageReadParams *params)
{
    if (!bktrIsValidSubstorage(substorage) || !params || !params->buffer || !params->size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    BucketTreeContext *ctx = (BucketTreeContext*)substorage->bktr_ctx;
    NcaFsSectionContext *nca_fs_ctx = substorage->nca_fs_ctx;
    bool success = false;
    
    if (substorage->type == BucketTreeSubStorageType_Regular)
    {
        if (params->parent_storage_type == BucketTreeStorageType_AesCtrEx)
        {
            /* Perform a read on the target NCA using AesCtrEx crypto. */
            success = ncaReadAesCtrExStorageFromBktrSection(nca_fs_ctx, params->buffer, params->size, params->offset, params->ctr_val, params->aes_ctr_ex_crypt);
        } else {
            /* Make sure to handle Sparse virtual offsets if we need to. */
            if (params->parent_storage_type == BucketTreeStorageType_Sparse && params->virtual_offset) nca_fs_ctx->cur_sparse_virtual_offset = params->virtual_offset;
            
            /* Perform a read on the target NCA. */
            success = ncaReadFsSection(nca_fs_ctx, params->buffer, params->size, params->offset);
        }
    } else {
        /* Perform a read on the target BucketTree storage. */
        success = bktrReadStorage(ctx, params->buffer, params->size, params->offset);
    }
    
    if (!success) LOG_MSG("Failed to read 0x%lX-byte long chunk from offset 0x%lX!", params->size, params->offset);
    
    return success;
}

NX_INLINE void bktrBucketInitializeSubStorageReadParams(BucketTreeSubStorageReadParams *out, void *buffer, u64 offset, u64 size, u64 virtual_offset, u32 ctr_val, bool aes_ctr_ex_crypt, u8 parent_storage_type)
{
    out->buffer = buffer;
    out->offset = offset;
    out->size = size;
    out->virtual_offset = ((virtual_offset && parent_storage_type == BucketTreeStorageType_Sparse) ? virtual_offset : 0);
    out->ctr_val = ((ctr_val && parent_storage_type == BucketTreeStorageType_AesCtrEx) ? ctr_val : 0);
    out->aes_ctr_ex_crypt = ((aes_ctr_ex_crypt && parent_storage_type == BucketTreeStorageType_AesCtrEx) true : false);
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
        LOG_MSG("Bucket Tree Offset Node header verification failed!");
        return false;
    }
    
    /* Validate offsets. */
    u32 offset_count = bktrGetOffsetCount(node_size);
    u32 entry_set_count = bktrGetEntrySetCount(node_size, entry_size, entry_count);
    
    const u64 start_offset = ((offset_count < entry_set_count && node_header->count < offset_count) ? *bktrGetOffsetNodeEnd(offset_node) : *bktrGetOffsetNodeBegin(offset_node));
    u64 end_offset = node_header->offset;
    
    if (start_offset > *bktrGetOffsetNodeBegin(offset_node) || start_offset >= end_offset || node_header->count != entry_set_count)
    {
        LOG_MSG("Invalid Bucket Tree Offset Node!");
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
    u32 entry_set_count = bktrGetEntrySetCount(node_size, entry_size, node_count);
    
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
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Get the node. */
    const BucketTreeOffsetNode *offset_node = &(ctx->storage_table->offset_node);
    
    /* Get the entry node index. */
    u32 entry_set_index = 0;
    const u64 *start_ptr = NULL, *end_ptr = NULL, *pos = NULL;
    bool success = false;
    
    if (bktrIsExistOffsetL2OnL1(ctx) && virtual_offset < *bktrGetOffsetNodeBegin(offset_node))
    {
        start_ptr = bktrGetOffsetNodeEnd(offset_node);
        end_ptr = (bktrGetOffsetNodeBegin(offset_node) + ctx->offset_count);
        
        if (!bktrGetTreeNodeEntryIndex(start_ptr, end_ptr, virtual_offset, &entry_set_index))
        {
            LOG_MSG("Failed to retrieve Bucket Tree Node entry index for virtual offset 0x%lX! (#1).", virtual_offset);
            goto end;
        }
    } else {
        start_ptr = bktrGetOffsetNodeBegin(offset_node);
        end_ptr = bktrGetOffsetNodeEnd(offset_node);
        
        if (!bktrGetTreeNodeEntryIndex(start_ptr, end_ptr, virtual_offset, &entry_set_index))
        {
            LOG_MSG("Failed to retrieve Bucket Tree Node entry index for virtual offset 0x%lX! (#2).", virtual_offset);
            goto end;
        }
        
        if (bktrIsExistL2(ctx))
        {
            u32 node_index = entry_set_index;
            if (node_index >= ctx->offset_count || !bktrFindEntrySet(&entry_set_index, virtual_offset, node_index))
            {
                LOG_MSG("Invalid L2 Bucket Tree Node index!");
                goto end;
            }
        }
    }
    
    /* Validate the entry set index. */
    if (entry_set_index >= ctx->entry_set_count)
    {
        LOG_MSG("Invalid Bucket Tree Node offset!");
        goto end;
    }
    
    /* Find the entry. */
    success = bktrFindEntry(ctx, out_visitor, virtual_offset, entry_set_index);
    if (!success) LOG_MSG("Failed to retrieve storage entry!");
    
end:
    return success;
}

static bool bktrGetTreeNodeEntryIndex(const u64 *start_ptr, const u64 *end_ptr, u64 virtual_offset, u32 *out_index)
{
    if (!start_ptr || !end_ptr || start_ptr >= end_ptr || !out_index)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    u64 *pos = (u64*)start_ptr;
    bool found = false;
    
    while(pos < end_ptr)
    {
        if (start_ptr < pos && *pos > virtual_offset)
        {
            *out_index = ((u32)(pos - start_ptr) - 1);
            found = true;
            break;
        }
        
        pos++;
    }
    
    return found;
}

static bool bktrGetEntryNodeEntryIndex(const BucketTreeNodeHeader *node_header, u64 entry_size, u64 virtual_offset, u32 *out_index)
{
    if (!node_header || !out_index)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Initialize storage node and find the index for our virtual offset. */
    BucketTreeStorageNode storage_node = {0};
    bktrInitializeStorageNode(&storage_node, entry_size, node_header->count);
    bktrStorageNodeFind(&storage_node, node_header, virtual_offset);
    
    /* Validate index. */
    if (storage_node.index == UINT32_MAX)
    {
        LOG_MSG("Unable to find index for virtual offset 0x%lX!", virtual_offset);
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
        LOG_MSG("Failed to retrieve offset node header at index 0x%X!", node_index);
        return false;
    }
    
    /* Get offset node entry index. */
    u32 offset_index = 0;
    if (!bktrGetEntryNodeEntryIndex(node_header, sizeof(u64), virtual_offset, &offset_index))
    {
        LOG_MSG("Failed to get offset node entry index!");
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
        LOG_MSG("Invalid Bucket Tree Offset Node offset!");
        return NULL;
    }
    
    /* Get offset node header. */
    const BucketTreeNodeHeader *node_header = (const BucketTreeNodeHeader*)((u8*)ctx->storage_table + node_offset);
    
    /* Validate offset node header. */
    if (!bktrVerifyNodeHeader(node_header, node_index, node_size, sizeof(u64)))
    {
        LOG_MSG("Bucket Tree Offset Node header verification failed!");
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
        LOG_MSG("Failed to retrieve entry node header at index 0x%X!", entry_set_index);
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
        LOG_MSG("Failed to get entry node entry index!");
        return false;
    }
    
    /* Get entry node entry offset and validate it. */
    u64 entry_offset = bktrGetEntryNodeEntryOffset(entry_set_offset, entry_size, entry_index);
    if ((entry_offset + entry_size) > (ctx->node_storage_size + ctx->entry_storage_size))
    {
        LOG_MSG("Invalid Bucket Tree Entry Node entry offset!");
        return false;
    }
    
    /* Update output visitor. */
    memset(out, 0, sizeof(BucketTreeVisitor));
    
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
        LOG_MSG("Invalid Bucket Tree Entry Node offset!");
        return NULL;
    }
    
    /* Get entry node header. */
    const BucketTreeNodeHeader *entry_set_header = (const BucketTreeNodeHeader*)((u8*)ctx->storage_table + entry_set_offset);
    
    /* Validate entry node header. */
    if (!bktrVerifyNodeHeader(entry_set_header, entry_set_index, entry_set_size, entry_size))
    {
        LOG_MSG("Bucket Tree Entry Node header verification failed!");
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
    u32 end = storage_node->count;
    BucketTreeStorageNodeOffset pos = storage_node->start;
    
    while(end > 0)
    {
        u32 half = (end / 2);
        BucketTreeStorageNodeOffset mid = bktrStorageNodeOffsetAdd(&pos, half);
        
        const u64 offset = *((const u64*)((const u8*)node_header + mid.offset));
        if (offset <= virtual_offset)
        {
            pos = bktrStorageNodeOffsetAdd(&mid, 1);
            end -= (half + 1);
        } else {
            end = half;
        }
    }
    
    storage_node->index = ((u32)bktrStorageNodeOffsetSubstract(&pos, &(storage_node->start)) - 1);
}

NX_INLINE BucketTreeStorageNodeOffset bktrStorageNodeOffsetAdd(BucketTreeStorageNodeOffset *ofs, u64 value)
{
    BucketTreeStorageNodeOffset out = { ofs->offset + (value * (u64)ofs->stride), ofs->stride };
    return out;
}

NX_INLINE u64 bktrStorageNodeOffsetSubstract(BucketTreeStorageNodeOffset *ofs1, BucketTreeStorageNodeOffset *ofs2)
{
    return (u64)((ofs1->offset - ofs2->offset) / ofs1->stride);
}

NX_INLINE bool bktrVisitorIsValid(BucketTreeVisitor *visitor)
{
    return (visitor && visitor->bktr_ctx && visitor->entry_index != UINT32_MAX);
}

NX_INLINE bool bktrVisitorCanMoveNext(BucketTreeVisitor *visitor)
{
    return (bktrVisitorIsValid(visitor) && ((visitor->entry_index + 1) < visitor->entry_set.header.count || (visitor->entry_set.header.index + 1) < visitor->bktr_ctx->entry_set_count));
}

NX_INLINE bool bktrVisitorCanMovePrevious(BucketTreeVisitor *visitor)
{
    return (bktrVisitorIsValid(visitor) && (visitor->entry_index > 0 || visitor->entry_set.header.index > 0));
}

static bool bktrVisitorMoveNext(BucketTreeVisitor *visitor)
{
    if (!bktrVisitorIsValid(visitor))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    BucketTreeContext *ctx = visitor->bktr_ctx;
    BucketTreeEntrySetHeader *entry_set = &(visitor->entry_set);
    bool success = false;
    
    /* Invalidate index. */
    visitor->entry_index = UINT32_MAX;
    
    u32 entry_index = (visitor->entry_index + 1);
    if (entry_index == entry_set->header.count)
    {
        /* We have reached the end of this entry node. Let's try to retrieve the first entry from the next one. */
        const u32 entry_set_index = (entry_set->header.index + 1);
        if (entry_set_index >= ctx->entry_set_count)
        {
            LOG_MSG("Error: attempting to move visitor into non-existing Bucket Tree Entry Node!");
            goto end;
        }
        
        /* Read next entry set header. */
        const u64 end_offset = entry_set->header.offset;
        const u64 entry_set_size = ctx->node_size;
        const u64 entry_set_offset = (ctx->node_storage_size + (entry_set_index * entry_set_size));
        
        if ((entry_set_offset + sizeof(BucketTreeEntrySetHeader)) > (ctx->node_storage_size + ctx->entry_storage_size))
        {
            LOG_MSG("Invalid Bucket Tree Entry Node offset!");
            goto end;
        }
        
        memcpy(entry_set, (u8*)ctx->storage_table + entry_set_offset, sizeof(BucketTreeEntrySetHeader));
        
        /* Validate next entry set header. */
        if (!bktrVerifyNodeHeader(&(entry_set->header), entry_set_index, entry_set_size, ctx->entry_size) || entry_set->start != end_offset || \
            entry_set->start >= entry_set->header.offset)
        {
            LOG_MSG("Bucket Tree Entry Node header verification failed!");
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
        LOG_MSG("Invalid Bucket Tree Entry Node entry offset!");
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

static bool bktrVisitorMovePrevious(BucketTreeVisitor *visitor)
{
    if (!bktrVisitorIsValid(visitor))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    BucketTreeContext *ctx = visitor->bktr_ctx;
    BucketTreeEntrySetHeader *entry_set = &(visitor->entry_set);
    bool success = false;
    
    /* Invalidate index. */
    visitor->entry_index = UINT32_MAX;
    
    u32 entry_index = visitor->entry_index;
    if (entry_index == 0)
    {
        /* We have reached the start of this entry node. Let's try to retrieve the last entry from the previous one. */
        if (!entry_set->header.index)
        {
            LOG_MSG("Error: attempting to move visitor into non-existing Bucket Tree Entry Node!");
            goto end;
        }
        
        /* Read previous entry set header. */
        const u64 start_offset = entry_set->start;
        const u64 entry_set_size = ctx->node_size;
        const u32 entry_set_index = (entry_set->header.index - 1);
        const u64 entry_set_offset = (ctx->node_storage_size + (entry_set_index * entry_set_size));
        
        if ((entry_set_offset + sizeof(BucketTreeEntrySetHeader)) > (ctx->node_storage_size + ctx->entry_storage_size))
        {
            LOG_MSG("Invalid Bucket Tree Entry Node offset!");
            goto end;
        }
        
        memcpy(entry_set, (u8*)ctx->storage_table + entry_set_offset, sizeof(BucketTreeEntrySetHeader));
        
        /* Validate next entry set header. */
        if (!bktrVerifyNodeHeader(&(entry_set->header), entry_set_index, entry_set_size, ctx->entry_size) || entry_set->header.offset != start_offset || \
            entry_set->start >= entry_set->header.offset)
        {
            LOG_MSG("Bucket Tree Entry Node header verification failed!");
            goto end;
        }
        
        /* Update entry index. */
        entry_index = entry_set->header.count;
    }
    
    entry_index--;
    
    /* Get the new entry. */
    const u64 entry_size = ctx->entry_size;
    const u64 entry_offset = (ctx->node_storage_size + bktrGetEntryNodeEntryOffsetByIndex(entry_set->header.index, ctx->node_size, entry_size, entry_index));
    
    if ((entry_offset + entry_size) > (ctx->node_storage_size + ctx->entry_storage_size))
    {
        LOG_MSG("Invalid Bucket Tree Entry Node entry offset!");
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
