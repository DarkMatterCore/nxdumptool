/*
 * hfs.c
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

#include "nxdt_utils.h"
#include "gamecard.h"

bool hfsReadPartitionData(HashFileSystemContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->size || !out || !read_size || (offset + read_size) > ctx->size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Read partition data. */
    if (!gamecardReadStorage(out, read_size, ctx->offset + offset))
    {
        LOG_MSG_ERROR("Failed to read Hash FS partition data!");
        return false;
    }

    return true;
}

bool hfsReadEntryData(HashFileSystemContext *ctx, HashFileSystemEntry *fs_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !fs_entry || !fs_entry->size || (fs_entry->offset + fs_entry->size) > ctx->size || !out || !read_size || (offset + read_size) > fs_entry->size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Read entry data. */
    if (!hfsReadPartitionData(ctx, out, read_size, ctx->header_size + fs_entry->offset + offset))
    {
        LOG_MSG_ERROR("Failed to read Partition FS entry data!");
        return false;
    }

    return true;
}

bool hfsGetTotalDataSize(HashFileSystemContext *ctx, u64 *out_size)
{
    u64 total_size = 0;
    u32 entry_count = hfsGetEntryCount(ctx);
    HashFileSystemEntry *fs_entry = NULL;

    if (!entry_count || !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = hfsGetEntryByIndex(ctx, i)))
        {
            LOG_MSG_ERROR("Failed to retrieve Hash FS entry #%u!", i);
            return false;
        }

        total_size += fs_entry->size;
    }

    *out_size = total_size;

    return true;
}

bool hfsGetEntryIndexByName(HashFileSystemContext *ctx, const char *name, u32 *out_idx)
{
    HashFileSystemEntry *fs_entry = NULL;
    u32 entry_count = hfsGetEntryCount(ctx), name_table_size = 0;
    char *name_table = hfsGetNameTable(ctx);

    if (!entry_count || !name_table || !name || !*name || !out_idx)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    name_table_size = ((HashFileSystemHeader*)ctx->header)->name_table_size;

    for(u32 i = 0; i < entry_count; i++)
    {
        if (!(fs_entry = hfsGetEntryByIndex(ctx, i)))
        {
            LOG_MSG_ERROR("Failed to retrieve Hash FS entry #%u!", i);
            break;
        }

        if (fs_entry->name_offset >= name_table_size)
        {
            LOG_MSG_ERROR("Name offset from Hash FS entry #%u exceeds name table size!", i);
            break;
        }

        if (!strcmp(name_table + fs_entry->name_offset, name))
        {
            *out_idx = i;
            return true;
        }
    }

    return false;
}
