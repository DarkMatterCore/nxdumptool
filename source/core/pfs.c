/*
 * pfs.c
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include <core/nxdt_utils.h>
#include <core/pfs.h>
#include <core/npdm.h>

#define PFS_HEADER_PADDING_ALIGNMENT    0x20

bool pfsInitializeContext(PartitionFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    u32 magic = 0;

    PartitionFileSystemHeader pfs_header = {0};
    PartitionFileSystemEntry *main_npdm_entry = NULL;

    bool success = false, dump_fs_header = false;

    if (!out || !nca_fs_ctx || !nca_fs_ctx->enabled || nca_fs_ctx->has_sparse_layer || nca_fs_ctx->section_type != NcaFsSectionType_PartitionFs || \
        (nca_fs_ctx->hash_type != NcaHashType_HierarchicalSha256 && nca_fs_ctx->hash_type != NcaHashType_HierarchicalSha3256) || !nca_fs_ctx->nca_ctx || \
        (nca_fs_ctx->nca_ctx->rights_id_available && !nca_fs_ctx->nca_ctx->titlekey_retrieved))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Free output context beforehand. */
    pfsFreeContext(out);

    /* Initialize NCA storage context. */
    NcaStorageContext *storage_ctx = &(out->storage_ctx);
    if (!ncaStorageInitializeContext(storage_ctx, nca_fs_ctx, NULL))
    {
        LOG_MSG_ERROR("Failed to initialize NCA storage context!");
        goto end;
    }

    out->nca_fs_ctx = storage_ctx->nca_fs_ctx;

    /* Get Partition FS offset and size. */
    if (!ncaStorageGetHashTargetExtents(storage_ctx, &(out->offset), &(out->size)))
    {
        LOG_MSG_ERROR("Failed to get target hash layer extents!");
        goto end;
    }

    /* Read partial Partition FS header. */
    if (!ncaStorageRead(storage_ctx, &pfs_header, sizeof(PartitionFileSystemHeader), out->offset))
    {
        LOG_MSG_ERROR("Failed to read partial Partition FS header!");
        goto end;
    }

    magic = __builtin_bswap32(pfs_header.magic);
    if (magic != PFS0_MAGIC)
    {
        LOG_MSG_ERROR("Invalid Partition FS magic word! (0x%08X).", magic);
        dump_fs_header = true;
        goto end;
    }

    if (!pfs_header.entry_count || !pfs_header.name_table_size)
    {
        LOG_MSG_ERROR("Invalid Partition FS entry count / name table size!");
        dump_fs_header = true;
        goto end;
    }

    /* Calculate full Partition FS header size. */
    out->header_size = (sizeof(PartitionFileSystemHeader) + (pfs_header.entry_count * sizeof(PartitionFileSystemEntry)) + pfs_header.name_table_size);

    /* Allocate memory for the full Partition FS header. */
    out->header = calloc(out->header_size, sizeof(u8));
    if (!out->header)
    {
        LOG_MSG_ERROR("Unable to allocate 0x%lX bytes buffer for the full Partition FS header!", out->header_size);
        goto end;
    }

    /* Read full Partition FS header. */
    if (!ncaStorageRead(storage_ctx, out->header, out->header_size, out->offset))
    {
        LOG_MSG_ERROR("Failed to read full Partition FS header!");
        goto end;
    }

    /* Check if we're dealing with an ExeFS section. */
    if ((main_npdm_entry = pfsGetEntryByName(out, "main.npdm")) != NULL && pfsReadEntryData(out, main_npdm_entry, &magic, sizeof(u32), 0) && \
        __builtin_bswap32(magic) == NPDM_META_MAGIC) out->is_exefs = true;

    /* Update flag. */
    success = true;

end:
    if (!success)
    {
        if (dump_fs_header) LOG_DATA_DEBUG(&pfs_header, sizeof(PartitionFileSystemHeader), "Partition FS header dump:");

        pfsFreeContext(out);
    }

    return success;
}

bool pfsReadPartitionData(PartitionFileSystemContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!pfsIsValidContext(ctx) || !out || !read_size || (offset + read_size) > ctx->size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Read partition data. */
    if (!ncaStorageRead(&(ctx->storage_ctx), out, read_size, ctx->offset + offset))
    {
        LOG_MSG_ERROR("Failed to read Partition FS data!");
        return false;
    }

    return true;
}

bool pfsReadEntryData(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !fs_entry || !fs_entry->size || (fs_entry->offset + fs_entry->size) > ctx->size || !out || !read_size || (offset + read_size) > fs_entry->size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Read entry data. */
    if (!pfsReadPartitionData(ctx, out, read_size, ctx->header_size + fs_entry->offset + offset))
    {
        LOG_MSG_ERROR("Failed to read Partition FS entry data!");
        return false;
    }

    return true;
}

bool pfsGetEntryIndexByName(PartitionFileSystemContext *ctx, const char *name, u32 *out_idx)
{
    PartitionFileSystemEntry *fs_entry = NULL;
    u32 entry_count = pfsGetEntryCount(ctx), name_table_size = 0;
    char *name_table = pfsGetNameTable(ctx);

    if (!entry_count || !name_table || !name || !*name || !out_idx)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    name_table_size = ((PartitionFileSystemHeader*)ctx->header)->name_table_size;

    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = pfsGetEntryByIndex(ctx, i)))
        {
            LOG_MSG_ERROR("Failed to retrieve Partition FS entry #%u!", i);
            return false;
        }

        if (fs_entry->name_offset >= name_table_size)
        {
            LOG_MSG_ERROR("Name offset from Partition FS entry #%u exceeds name table size!", i);
            return false;
        }

        if (!strcmp(name_table + fs_entry->name_offset, name))
        {
            *out_idx = i;
            return true;
        }
    }

    if (strcmp(name, "main.npdm") != 0) LOG_MSG_ERROR("Unable to find Partition FS entry \"%s\"!", name);

    return false;
}

bool pfsGetTotalDataSize(PartitionFileSystemContext *ctx, u64 *out_size)
{
    u64 total_size = 0;
    u32 entry_count = pfsGetEntryCount(ctx);
    PartitionFileSystemEntry *fs_entry = NULL;

    if (!entry_count || !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = pfsGetEntryByIndex(ctx, i)))
        {
            LOG_MSG_ERROR("Failed to retrieve Partition FS entry #%u!", i);
            return false;
        }

        total_size += fs_entry->size;
    }

    *out_size = total_size;

    return true;
}

bool pfsGenerateEntryPatch(PartitionFileSystemContext *ctx, PartitionFileSystemEntry *fs_entry, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out)
{
    if (!pfsIsValidContext(ctx) || !fs_entry || !fs_entry->size || (fs_entry->offset + fs_entry->size) > ctx->size || !data || !data_size || \
        (data_offset + data_size) > fs_entry->size || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u64 partition_offset = (ctx->header_size + fs_entry->offset + data_offset);

    if (!ncaGenerateHierarchicalSha256Patch(ctx->nca_fs_ctx, data, data_size, partition_offset, out))
    {
        LOG_MSG_ERROR("Failed to generate 0x%lX bytes HierarchicalSha256 patch at offset 0x%lX for Partition FS entry!", data_size, partition_offset);
        return false;
    }

    return true;
}

bool pfsAddEntryInformationToImageContext(PartitionFileSystemImageContext *ctx, const char *entry_name, u64 entry_size, u32 *out_entry_idx)
{
    if (!ctx || !entry_name || !*entry_name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    PartitionFileSystemHeader *header = &(ctx->header);

    PartitionFileSystemEntry *tmp_pfs_entries = NULL, *cur_pfs_entry = NULL, *prev_pfs_entry = NULL;
    u64 tmp_pfs_entries_size = ((header->entry_count + 1) * sizeof(PartitionFileSystemEntry));

    char *tmp_name_table = NULL;
    u32 tmp_name_table_size = (header->name_table_size + strlen(entry_name) + 1);

    /* Reallocate Partition FS entries. */
    if (!(tmp_pfs_entries = realloc(ctx->entries, tmp_pfs_entries_size)))
    {
        LOG_MSG_ERROR("Failed to reallocate Partition FS entries!");
        return false;
    }

    ctx->entries = tmp_pfs_entries;
    tmp_pfs_entries = NULL;

    /* Update Partition FS entry information. */
    cur_pfs_entry = &(ctx->entries[header->entry_count]);
    prev_pfs_entry = (header->entry_count ? &(ctx->entries[header->entry_count - 1]) : NULL);

    memset(cur_pfs_entry, 0, sizeof(PartitionFileSystemEntry));

    cur_pfs_entry->offset = (prev_pfs_entry ? (prev_pfs_entry->offset + prev_pfs_entry->size) : 0);
    cur_pfs_entry->size = entry_size;
    cur_pfs_entry->name_offset = header->name_table_size;

    /* Reallocate Partition FS name table. */
    if (!(tmp_name_table = realloc(ctx->name_table, tmp_name_table_size)))
    {
        LOG_MSG_ERROR("Failed to reallocate Partition FS name table!");
        return false;
    }

    ctx->name_table = tmp_name_table;
    tmp_name_table = NULL;

    /* Update Partition FS name table. */
    sprintf(ctx->name_table + header->name_table_size, "%s", entry_name);
    header->name_table_size = tmp_name_table_size;

    /* Update output entry index. */
    if (out_entry_idx) *out_entry_idx = header->entry_count;

    /* Update Partition FS entry count, name table size and data size. */
    header->entry_count++;
    ctx->fs_size += entry_size;

    return true;
}

bool pfsUpdateEntryNameFromImageContext(PartitionFileSystemImageContext *ctx, u32 entry_idx, const char *new_entry_name)
{
    if (!ctx || !ctx->header.entry_count || !ctx->header.name_table_size || !ctx->entries || !ctx->name_table || entry_idx >= ctx->header.entry_count || !new_entry_name || !*new_entry_name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    PartitionFileSystemEntry *pfs_entry = &(ctx->entries[entry_idx]);

    char *name_table_entry = (ctx->name_table + pfs_entry->name_offset);
    size_t new_entry_name_len = strlen(new_entry_name);
    size_t cur_entry_name_len = strlen(name_table_entry);

    if (new_entry_name_len > cur_entry_name_len)
    {
        LOG_MSG_ERROR("New entry name length exceeds previous entry name length! (0x%lX > 0x%lX).", new_entry_name_len, cur_entry_name_len);
        return false;
    }

    memcpy(name_table_entry, new_entry_name, new_entry_name_len);

    return true;
}

bool pfsWriteImageContextHeaderToMemoryBuffer(PartitionFileSystemImageContext *ctx, void *buf, u64 buf_size, u64 *out_header_size)
{
    if (!ctx || !ctx->header.entry_count || !ctx->header.name_table_size || !ctx->entries || !ctx->name_table || !buf || !out_header_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    PartitionFileSystemHeader *header = &(ctx->header);
    u8 *buf_u8 = (u8*)buf;
    u64 header_size = 0, padded_header_size = 0, block_offset = 0, block_size = 0;
    u32 padding_size = 0;

    /* Calculate header size. */
    header_size = (sizeof(PartitionFileSystemHeader) + (header->entry_count * sizeof(PartitionFileSystemEntry)) + header->name_table_size);

    /* Calculate padded header size and padding size. */
    padded_header_size = (IS_ALIGNED(header_size, PFS_HEADER_PADDING_ALIGNMENT) ? (header_size + PFS_HEADER_PADDING_ALIGNMENT) : ALIGN_UP(header_size, PFS_HEADER_PADDING_ALIGNMENT));
    padding_size = (u32)(padded_header_size - header_size);

    /* Check buffer size. */
    if (buf_size < padded_header_size)
    {
        LOG_MSG_ERROR("Not enough space available in input buffer to write full Partition FS header! (got 0x%lX, need 0x%lX).", buf_size, padded_header_size);
        return false;
    }

    /* Write full header. */
    header->name_table_size += padding_size;
    block_size = sizeof(PartitionFileSystemHeader);
    memcpy(buf_u8 + block_offset, header, block_size);
    block_offset += block_size;
    header->name_table_size -= padding_size;

    block_size = (header->entry_count * sizeof(PartitionFileSystemEntry));
    memcpy(buf_u8 + block_offset, ctx->entries, block_size);
    block_offset += block_size;

    block_size = header->name_table_size;
    memcpy(buf_u8 + block_offset, ctx->name_table, block_size);
    block_offset += block_size;

    memset(buf_u8 + block_offset, 0, padding_size);

    /* Update output header size. */
    *out_header_size = padded_header_size;

    return true;
}
