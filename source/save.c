/*
 * save.c
 *
 * Copyright (c) 2019-2020, shchmue.
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

#include "utils.h"
#include "save.h"

static inline void save_bitmap_set_bit(void *buffer, size_t bit_offset)
{
    *((u8*)buffer + (bit_offset >> 3)) |= 1 << (bit_offset & 7);
}

static inline void save_bitmap_clear_bit(void *buffer, size_t bit_offset)
{
    *((u8*)buffer + (bit_offset >> 3)) &= ~(u8)(1 << (bit_offset & 7));
}

static inline u8 save_bitmap_check_bit(const void *buffer, size_t bit_offset)
{
    return (*((u8*)buffer + (bit_offset >> 3)) & (1 << (bit_offset & 7)));
}

static bool save_duplex_storage_init(duplex_storage_ctx_t *ctx, duplex_fs_layer_info_t *layer, void *bitmap, u64 bitmap_size)
{
    if (!ctx || !layer || !layer->data_a || !layer->data_b || !layer->info.block_size_power || !bitmap || !bitmap_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    ctx->data_a = layer->data_a;
    ctx->data_b = layer->data_b;
    ctx->bitmap_storage = (u8*)bitmap;
    ctx->block_size = (1 << layer->info.block_size_power);
    ctx->bitmap.data = ctx->bitmap_storage;
    
    ctx->bitmap.bitmap = calloc(1, bitmap_size >> 3);
    if (!ctx->bitmap.bitmap)
    {
        LOGFILE("Failed to allocate memory for duplex bitmap!");
        return false;
    }
    
    u32 bits_remaining = bitmap_size;
    u32 bitmap_pos = 0;
    u32 *buffer_pos = (u32*)bitmap;
    
    while(bits_remaining)
    {
        u32 bits_to_read = (bits_remaining < 32 ? bits_remaining : 32);
        u32 val = *buffer_pos;
        
        for(u32 i = 0; i < bits_to_read; i++)
        {
            if (val & 0x80000000)
            {
                save_bitmap_set_bit(ctx->bitmap.bitmap, bitmap_pos);
            } else {
                save_bitmap_clear_bit(ctx->bitmap.bitmap, bitmap_pos);
            }
            
            bitmap_pos++;
            bits_remaining--;
            val <<= 1;
        }
        
        buffer_pos++;
    }
    
    return true;
}

static u32 save_duplex_storage_read(duplex_storage_ctx_t *ctx, void *buffer, u64 offset, size_t count)
{
    if (!ctx || !ctx->block_size || !ctx->bitmap.bitmap || !buffer || !count)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    u64 in_pos = offset;
    u32 out_pos = 0;
    u32 remaining = count;
    
    while(remaining)
    {
        u32 block_num = (u32)(in_pos / ctx->block_size);
        u32 block_pos = (u32)(in_pos % ctx->block_size);
        u32 bytes_to_read = ((ctx->block_size - block_pos) < remaining ? (ctx->block_size - block_pos) : remaining);
        
        u8 *data = (save_bitmap_check_bit(ctx->bitmap.bitmap, block_num) ? ctx->data_b : ctx->data_a);
        memcpy((u8*)buffer + out_pos, data + in_pos, bytes_to_read);
        
        out_pos += bytes_to_read;
        in_pos += bytes_to_read;
        remaining -= bytes_to_read;
    }
    
    return out_pos;
}

static remap_segment_ctx_t *save_remap_init_segments(remap_header_t *header, remap_entry_ctx_t *map_entries, u32 num_map_entries)
{
    if (!header || !header->map_segment_count || !map_entries || !num_map_entries)
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    remap_segment_ctx_t *segments = calloc(header->map_segment_count, sizeof(remap_segment_ctx_t));
    if (!segments)
    {
        LOGFILE("Failed to allocate initial memory for remap segments!");
        return NULL;
    }
    
    u32 i, entry_idx = 0;
    bool success = false;
    
    for(i = 0; i < header->map_segment_count; i++)
    {
        remap_segment_ctx_t *seg = &(segments[i]);
        
        seg->entry_count = 0;
        
        seg->entries = calloc(1, sizeof(remap_entry_ctx_t*));
        if (!seg->entries)
        {
            LOGFILE("Failed to allocate memory for remap segment entry #%u!", entry_idx);
            goto out;
        }
        
        seg->entries[seg->entry_count++] = &map_entries[entry_idx];
        seg->offset = map_entries[entry_idx].virtual_offset;
        map_entries[entry_idx++].segment = seg;
        
        while(entry_idx < num_map_entries && map_entries[entry_idx - 1].virtual_offset_end == map_entries[entry_idx].virtual_offset)
        {
            map_entries[entry_idx].segment = seg;
            map_entries[entry_idx - 1].next = &map_entries[entry_idx];
            
            remap_entry_ctx_t **ptr = calloc(sizeof(remap_entry_ctx_t*), seg->entry_count + 1);
            if (!ptr)
            {
                LOGFILE("Failed to allocate memory for remap segment entry #%u!", entry_idx);
                goto out;
            }
            
            memcpy(ptr, seg->entries, sizeof(remap_entry_ctx_t*) * seg->entry_count);
            free(seg->entries);
            seg->entries = ptr;
            seg->entries[seg->entry_count++] = &map_entries[entry_idx++];
        }
        
        seg->length = (seg->entries[seg->entry_count - 1]->virtual_offset_end - seg->entries[0]->virtual_offset);
    }
    
    success = true;
    
out:
    if (!success)
    {
        entry_idx = 0;
        
        for(u32 j = 0; j <= i; j++)
        {
            if (!map_entries[entry_idx].segment) break;
            
            if (map_entries[entry_idx].segment->entries)
            {
                free(map_entries[entry_idx].segment->entries);
                map_entries[entry_idx].segment->entries = NULL;
            }
            
            map_entries[entry_idx++].segment = NULL;
            
            while(entry_idx < num_map_entries && map_entries[entry_idx - 1].virtual_offset_end == map_entries[entry_idx].virtual_offset)
            {
                map_entries[entry_idx - 1].next = NULL;
                
                if (!map_entries[entry_idx].segment) break;
                
                if (map_entries[entry_idx].segment->entries)
                {
                    free(map_entries[entry_idx].segment->entries);
                    map_entries[entry_idx].segment->entries = NULL;
                }
                
                map_entries[entry_idx++].segment = NULL;
            }
        }
        
        free(segments);
        segments = NULL;
    }
    
    return segments;
}

static remap_entry_ctx_t *save_remap_get_map_entry(remap_storage_ctx_t *ctx, u64 offset)
{
    if (!ctx || !ctx->header || !ctx->segments)
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    u32 segment_idx = (u32)(offset >> (64 - ctx->header->segment_bits));
    
    if (segment_idx < ctx->header->map_segment_count)
    {
        for(u32 i = 0; i < ctx->segments[segment_idx].entry_count; i++)
        {
            if (ctx->segments[segment_idx].entries[i]->virtual_offset_end > offset) return ctx->segments[segment_idx].entries[i];
        }
    }
    
    LOGFILE("Unable to find map entry for offset 0x%lX!", offset);
    return NULL;
}

static u32 save_remap_read(remap_storage_ctx_t *ctx, void *buffer, u64 offset, size_t count)
{
    if (!ctx || (ctx->type == STORAGE_BYTES && !ctx->file) || (ctx->type == STORAGE_DUPLEX && !ctx->duplex) || (ctx->type != STORAGE_BYTES && ctx->type != STORAGE_DUPLEX) || !buffer || !count)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    remap_entry_ctx_t *entry = save_remap_get_map_entry(ctx, offset);
    if (!entry)
    {
        LOGFILE("Failed to retrieve map entry!");
        return 0;
    }
    
    u64 in_pos = offset;
    u32 out_pos = 0;
    u32 remaining = count;
    
    UINT br = 0;
    FRESULT fr;
    
    while(remaining)
    {
        u64 entry_pos = (in_pos - entry->virtual_offset);
        u32 bytes_to_read = ((entry->virtual_offset_end - in_pos) < remaining ? (u32)(entry->virtual_offset_end - in_pos) : remaining);
        
        switch (ctx->type)
        {
            case STORAGE_BYTES:
                fr = f_lseek(ctx->file, ctx->base_storage_offset + entry->physical_offset + entry_pos);
                if (fr || f_tell(ctx->file) != (ctx->base_storage_offset + entry->physical_offset + entry_pos))
                {
                    LOGFILE("Failed to seek to offset 0x%lX in savefile! (%u)", ctx->base_storage_offset + entry->physical_offset + entry_pos, fr);
                    return out_pos;
                }
                
                fr = f_read(ctx->file, (u8*)buffer + out_pos, bytes_to_read, &br);
                if (fr || br != bytes_to_read)
                {
                    LOGFILE("Failed to read %u bytes chunk from offset 0x%lX in savefile! (%u)", bytes_to_read, ctx->base_storage_offset + entry->physical_offset + entry_pos, fr);
                    return (out_pos + br);
                }
                
                break;
            case STORAGE_DUPLEX:
                br = save_duplex_storage_read(ctx->duplex, (u8*)buffer + out_pos, ctx->base_storage_offset + entry->physical_offset + entry_pos, bytes_to_read);
                if (br != bytes_to_read)
                {
                    LOGFILE("Failed to read remap data from duplex storage!");
                    return (out_pos + br);
                }
                break;
            default:
                break;
        }
        
        out_pos += bytes_to_read;
        in_pos += bytes_to_read;
        remaining -= bytes_to_read;
        
        if (in_pos >= entry->virtual_offset_end) entry = entry->next;
    }
    
    return out_pos;
}

static u32 save_journal_storage_read(journal_storage_ctx_t *ctx, remap_storage_ctx_t *remap, void *buffer, u64 offset, size_t count)
{
    if (!ctx || !ctx->block_size || !remap || !buffer || !count)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    u64 in_pos = offset;
    u32 out_pos = 0;
    u32 remaining = count;
    u32 br;
    
    while(remaining)
    {
        u32 block_num = (u32)(in_pos / ctx->block_size);
        u32 block_pos = (u32)(in_pos % ctx->block_size);
        u64 physical_offset = (ctx->map.entries[block_num].physical_index * ctx->block_size + block_pos);
        u32 bytes_to_read = ((ctx->block_size - block_pos) < remaining ? (ctx->block_size - block_pos) : remaining);
        
        br = save_remap_read(remap, (u8*)buffer + out_pos, ctx->journal_data_offset + physical_offset, bytes_to_read);
        if (br != bytes_to_read)
        {
            LOGFILE("Failed to read journal storage data!");
            return (out_pos + br);
        }
        
        out_pos += bytes_to_read;
        in_pos += bytes_to_read;
        remaining -= bytes_to_read;
    }
    
    return out_pos;
}

static bool save_ivfc_storage_init(hierarchical_integrity_verification_storage_ctx_t *ctx, u64 master_hash_offset, ivfc_save_hdr_t *ivfc)
{
    if (!ctx || !ctx->levels || !ivfc || !ivfc->num_levels)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    bool success = false;
    
    ivfc_level_save_ctx_t *levels = ctx->levels;
    levels[0].type = STORAGE_BYTES;
    levels[0].hash_offset = master_hash_offset;
    
    for(u32 i = 1; i < 4; i++)
    {
        ivfc_level_hdr_t *level = &ivfc->level_headers[i - 1];
        levels[i].type = STORAGE_REMAP;
        levels[i].data_offset = level->logical_offset;
        levels[i].data_size = level->hash_data_size;
    }
    
    if (ivfc->num_levels == 5)
    {
        ivfc_level_hdr_t *data_level = &ivfc->level_headers[ivfc->num_levels - 2];
        levels[ivfc->num_levels - 1].type = STORAGE_JOURNAL;
        levels[ivfc->num_levels - 1].data_offset = data_level->logical_offset;
        levels[ivfc->num_levels - 1].data_size = data_level->hash_data_size;
    }
    
    struct salt_source_t {
        char string[50];
        u32 length;
    };
    
    static const struct salt_source_t salt_sources[6] = {
        { "HierarchicalIntegrityVerificationStorage::Master", 48 },
        { "HierarchicalIntegrityVerificationStorage::L1", 44 },
        { "HierarchicalIntegrityVerificationStorage::L2", 44 },
        { "HierarchicalIntegrityVerificationStorage::L3", 44 },
        { "HierarchicalIntegrityVerificationStorage::L4", 44 },
        { "HierarchicalIntegrityVerificationStorage::L5", 44 }
    };
    
    integrity_verification_info_ctx_t init_info[ivfc->num_levels];
    
    init_info[0].data = &levels[0];
    init_info[0].block_size = 0;
    
    for(u32 i = 1; i < ivfc->num_levels; i++)
    {
        init_info[i].data = &levels[i];
        init_info[i].block_size = (1 << ivfc->level_headers[i - 1].block_size);
        hmacSha256CalculateMac(init_info[i].salt, salt_sources[i - 1].string, salt_sources[i - 1].length, ivfc->salt_source, 0x20);
    }
    
    ctx->integrity_storages[0].next_level = NULL;
    
    ctx->level_validities = calloc(sizeof(validity_t*), (ivfc->num_levels - 1));
    if (!ctx->level_validities)
    {
        LOGFILE("Failed to allocate memory for level validities!");
        goto out;
    }
    
    for(u32 i = 1; i < ivfc->num_levels; i++)
    {
        integrity_verification_storage_ctx_t *level_data = &ctx->integrity_storages[i - 1];
        level_data->hash_storage = &levels[i - 1];
        level_data->base_storage = &levels[i];
        level_data->sector_size = init_info[i].block_size;
        level_data->_length = init_info[i].data->data_size;
        level_data->sector_count = ((level_data->_length + level_data->sector_size - 1) / level_data->sector_size);
        memcpy(level_data->salt, init_info[i].salt, 0x20);
        
        level_data->block_validities = calloc(sizeof(validity_t), level_data->sector_count);
        if (!level_data->block_validities)
        {
            LOGFILE("Failed to allocate memory for block validities in IVFC level #%u!", i);
            goto out;
        }
        
        ctx->level_validities[i - 1] = level_data->block_validities;
        if (i > 1) level_data->next_level = &ctx->integrity_storages[i - 2];
    }
    
    ctx->data_level = &levels[ivfc->num_levels - 1];
    ctx->_length = ctx->integrity_storages[ivfc->num_levels - 2]._length;
    
    success = true;
    
out:
    if (!success && ctx->level_validities)
    {
        free(ctx->level_validities);
        ctx->level_validities = NULL;
        
        for(u32 i = 1; i < ivfc->num_levels; i++)
        {
            integrity_verification_storage_ctx_t *level_data = &ctx->integrity_storages[i - 1];
            
            if (level_data->block_validities)
            {
                free(level_data->block_validities);
                level_data->block_validities = NULL;
                ctx->level_validities[i - 1] = NULL;
            } else {
                break;
            }
        }
    }
    
    return success;
}

static size_t save_ivfc_level_fread(ivfc_level_save_ctx_t *ctx, void *buffer, u64 offset, size_t count)
{
    if (!ctx || (ctx->type == STORAGE_BYTES && !ctx->save_ctx->file) || (ctx->type != STORAGE_BYTES && ctx->type != STORAGE_REMAP && ctx->type != STORAGE_JOURNAL) || !buffer || !count)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    UINT br = 0;
    FRESULT fr;
    
    switch (ctx->type)
    {
        case STORAGE_BYTES:
            fr = f_lseek(ctx->save_ctx->file, ctx->hash_offset + offset);
            if (fr || f_tell(ctx->save_ctx->file) != (ctx->hash_offset + offset))
            {
                LOGFILE("Failed to seek to offset 0x%lX in savefile! (%u)", ctx->hash_offset + offset, fr);
                return (size_t)br;
            }
            
            fr = f_read(ctx->save_ctx->file, buffer, count, &br);
            if (fr || br != count)
            {
                LOGFILE("Failed to read IVFC level data from offset 0x%lX in savefile! (%u)", ctx->hash_offset + offset, fr);
                return (size_t)br;
            }
            
            break;
        case STORAGE_REMAP:
            br = save_remap_read(&ctx->save_ctx->meta_remap_storage, buffer, ctx->data_offset + offset, count);
            if (br != count)
            {
                LOGFILE("Failed to read IVFC level data from remap storage!");
                return (size_t)br;
            }
            
            break;
        case STORAGE_JOURNAL:
            br = save_journal_storage_read(&ctx->save_ctx->journal_storage, &ctx->save_ctx->data_remap_storage, buffer, ctx->data_offset + offset, count);
            if (br != count)
            {
                LOGFILE("Failed to read IVFC level data from journal storage!");
                return (size_t)br;
            }
            
            break;
        default:
            return 0;
    }
    
    return count;
}

static bool save_ivfc_storage_read(integrity_verification_storage_ctx_t *ctx, void *buffer, u64 offset, size_t count, u32 verify)
{
    if (!ctx || !ctx->sector_size || (!ctx->next_level && !ctx->hash_storage && !ctx->base_storage) || !buffer || !count)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    if (count > ctx->sector_size)
    {
        LOGFILE("IVFC read exceeds sector size!");
        return false;
    }
    
    u64 block_index = (offset / ctx->sector_size);
    
    if (ctx->block_validities[block_index] == VALIDITY_INVALID && verify)
    {
        LOGFILE("Hash error from previous check found at offset 0x%lX, count 0x%lX!", offset, count);
        return false;
    }
    
    u8 hash_buffer[0x20] = {0};
    u8 zeroes[0x20] = {0};
    u64 hash_pos = (block_index * 0x20);
    
    if (ctx->next_level)
    {
        if (!save_ivfc_storage_read(ctx->next_level, hash_buffer, hash_pos, 0x20, verify))
        {
            LOGFILE("Failed to read hash from next IVFC level!");
            return false;
        }
    } else {
        if (save_ivfc_level_fread(ctx->hash_storage, hash_buffer, hash_pos, 0x20) != 0x20)
        {
            LOGFILE("Failed to read hash from hash storage!");
            return false;
        }
    }
    
    if (!memcmp(hash_buffer, zeroes, 0x20))
    {
        memset(buffer, 0, count);
        ctx->block_validities[block_index] = VALIDITY_VALID;
        return true;
    }
    
    if (save_ivfc_level_fread(ctx->base_storage, buffer, offset, count) != count)
    {
        LOGFILE("Failed to read IVFC level from base storage!");
        return false;
    }
    
    if (!(verify && ctx->block_validities[block_index] == VALIDITY_UNCHECKED)) return true;
    
    u8 hash[0x20] = {0};
    
    u8 *data_buffer = calloc(1, ctx->sector_size + 0x20);
    if (!data_buffer)
    {
        LOGFILE("Failed to allocate memory for data buffer!");
        return false;
    }
    
    memcpy(data_buffer, ctx->salt, 0x20);
    memcpy(data_buffer + 0x20, buffer, ctx->sector_size);
    
    sha256CalculateHash(hash, data_buffer, ctx->sector_size + 0x20);
    hash[0x1F] |= 0x80;
    
    free(data_buffer);
    
    ctx->block_validities[block_index] = (!memcmp(hash_buffer, hash, 0x20) ? VALIDITY_VALID : VALIDITY_INVALID);
    
    if (ctx->block_validities[block_index] == VALIDITY_INVALID && verify)
    {
        LOGFILE("Hash error from current check found at offset 0x%lX, count 0x%lX!", offset, count);
        return false;
    }
    
    return true;
}

static u32 save_allocation_table_read_entry_with_length(allocation_table_ctx_t *ctx, allocation_table_entry_t *entry)
{
    if (!ctx || !ctx->base_storage || !entry)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    u32 length = 1;
    u32 entry_index = allocation_table_block_to_entry_index(entry->next);
    
    allocation_table_entry_t *entries = (allocation_table_entry_t*)((u8*)(ctx->base_storage) + (entry_index * SAVE_FAT_ENTRY_SIZE));
    
    if ((entries[0].next & 0x80000000) == 0)
    {
        if ((entries[0].prev & 0x80000000) && entries[0].prev != 0x80000000)
        {
            LOGFILE("Invalid range entry in allocation table!");
            return 0;
        }
    } else {
        length = (entries[1].next - entry_index + 1);
    }
    
    if (allocation_table_is_list_end(&entries[0]))
    {
        entry->next = 0xFFFFFFFF;
    } else {
        entry->next = allocation_table_entry_index_to_block(allocation_table_get_next(&entries[0]));
    }
    
    if (allocation_table_is_list_start(&entries[0]))
    {
        entry->prev = 0xFFFFFFFF;
    } else {
        entry->prev = allocation_table_entry_index_to_block(allocation_table_get_prev(&entries[0]));
    }
    
    return length;
}

static u32 save_allocation_table_get_list_length(allocation_table_ctx_t *ctx, u32 block_index)
{
    if (!ctx || !ctx->header->allocation_table_block_count)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    allocation_table_entry_t entry = {0};
    entry.next = block_index;
    u32 total_length = 0;
    u32 table_size = ctx->header->allocation_table_block_count;
    u32 nodes_iterated = 0;
    
    while(entry.next != 0xFFFFFFFF)
    {
        u32 entry_length = save_allocation_table_read_entry_with_length(ctx, &entry);
        if (!entry_length)
        {
            LOGFILE("Failed to retrieve FAT entry length!");
            return 0;
        }
        
        total_length += entry_length;
        nodes_iterated++;
        
        if (nodes_iterated > table_size)
        {
            LOGFILE("Cycle detected in allocation table!");
            return 0;
        }
    }
    
    return total_length;
}

static bool save_allocation_table_iterator_begin(allocation_table_iterator_ctx_t *ctx, allocation_table_ctx_t *table, u32 initial_block)
{
    if (!ctx || !table)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    ctx->fat = table;
    ctx->physical_block = initial_block;
    ctx->virtual_block = 0;
    
    allocation_table_entry_t entry = {0};
    entry.next = initial_block;
    
    ctx->current_segment_size = save_allocation_table_read_entry_with_length(ctx->fat, &entry);
    if (!ctx->current_segment_size)
    {
        LOGFILE("Failed to retrieve FAT entry length!");
        return false;
    }
    
    ctx->next_block = entry.next;
    ctx->prev_block = entry.prev;
    
    if (ctx->prev_block != 0xFFFFFFFF)
    {
        LOGFILE("Attempted to start FAT iteration from invalid block 0x%08X!", initial_block);
        return false;
    }
    
    return true;
}

static bool save_allocation_table_iterator_move_next(allocation_table_iterator_ctx_t *ctx)
{
    if (!ctx || ctx->next_block == 0xFFFFFFFF)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    ctx->virtual_block += ctx->current_segment_size;
    ctx->physical_block = ctx->next_block;
    
    allocation_table_entry_t entry = {0};
    entry.next = ctx->next_block;
    
    ctx->current_segment_size = save_allocation_table_read_entry_with_length(ctx->fat, &entry);
    if (!ctx->current_segment_size)
    {
        LOGFILE("Failed to retrieve current segment size!");
        return false;
    }
    
    ctx->next_block = entry.next;
    ctx->prev_block = entry.prev;
    
    return true;
}

static bool save_allocation_table_iterator_move_prev(allocation_table_iterator_ctx_t *ctx)
{
    if (!ctx || ctx->prev_block == 0xFFFFFFFF)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    ctx->physical_block = ctx->prev_block;
    
    allocation_table_entry_t entry = {0};
    entry.next = ctx->prev_block;
    
    ctx->current_segment_size = save_allocation_table_read_entry_with_length(ctx->fat, &entry);
    if (!ctx->current_segment_size)
    {
        LOGFILE("Failed to retrieve current segment size!");
        return false;
    }
    
    ctx->next_block = entry.next;
    ctx->prev_block = entry.prev;
    
    ctx->virtual_block -= ctx->current_segment_size;
    
    return true;
}

static bool save_allocation_table_iterator_seek(allocation_table_iterator_ctx_t *ctx, u32 block)
{
    while(true)
    {
        if (block < ctx->virtual_block)
        {
            if (!save_allocation_table_iterator_move_prev(ctx)) return false;
        } else
        if (block >= ctx->virtual_block + ctx->current_segment_size)
        {
            if (!save_allocation_table_iterator_move_next(ctx)) return false;
        } else {
            return true;
        }
    }
}

u32 save_allocation_table_storage_read(allocation_table_storage_ctx_t *ctx, void *buffer, u64 offset, size_t count)
{
    if (!ctx || !ctx->fat || !ctx->block_size || !buffer || !count)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    allocation_table_iterator_ctx_t iterator;
    if (!save_allocation_table_iterator_begin(&iterator, ctx->fat, ctx->initial_block))
    {
        LOGFILE("Failed to initialize FAT interator!");
        return 0;
    }
    
    u64 in_pos = offset;
    u32 out_pos = 0;
    u32 remaining = count;
    
    while(remaining)
    {
        u32 block_num = (u32)(in_pos / ctx->block_size);
        if (!save_allocation_table_iterator_seek(&iterator, block_num))
        {
            LOGFILE("Failed to seek to block #%u within offset 0x%lX!", block_num, offset);
            return out_pos;
        }
        
        u32 segment_pos = (u32)(in_pos - ((u64)iterator.virtual_block * ctx->block_size));
        u64 physical_offset = ((iterator.physical_block * ctx->block_size) + segment_pos);
        
        u32 remaining_in_segment = ((iterator.current_segment_size * ctx->block_size) - segment_pos);
        u32 bytes_to_read = (remaining < remaining_in_segment ? remaining : remaining_in_segment);
        
        u32 sector_size = ctx->base_storage->integrity_storages[3].sector_size;
        u32 chunk_remaining = bytes_to_read;
        
        for(u32 i = 0; i < bytes_to_read; i += sector_size)
        {
            u32 bytes_to_request = (chunk_remaining < sector_size ? chunk_remaining : sector_size);
            
            if (!save_ivfc_storage_read(&ctx->base_storage->integrity_storages[3], (u8*)buffer + out_pos + i, physical_offset + i, bytes_to_request, ctx->base_storage->data_level->save_ctx->tool_ctx.action & ACTION_VERIFY))
            {
                LOGFILE("Failed to read %u bytes chunk from IVFC storage at physical offset 0x%lX!", bytes_to_request, physical_offset + i);
                return (out_pos + bytes_to_read - chunk_remaining);
            }
            
            chunk_remaining -= bytes_to_request;
        }
        
        out_pos += bytes_to_read;
        in_pos += bytes_to_read;
        remaining -= bytes_to_read;
    }
    
    return out_pos;
}

static u32 save_fs_list_get_capacity(save_filesystem_list_ctx_t *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    if (!ctx->capacity)
    {
        if (save_allocation_table_storage_read(&ctx->storage, &ctx->capacity, 4, 4) != 4)
        {
            LOGFILE("Failed to read FS capacity from FAT storage!");
            return 0;
        }
    }
    
    return ctx->capacity;
}

static u32 save_fs_list_read_entry(save_filesystem_list_ctx_t *ctx, u32 index, save_fs_list_entry_t *entry)
{
    if (!ctx || !entry)
    {
        LOGFILE("Invalid parameters!");
        return 0;
    }
    
    u32 ret = save_allocation_table_storage_read(&ctx->storage, entry, index * SAVE_FS_LIST_ENTRY_SIZE, SAVE_FS_LIST_ENTRY_SIZE);
    if (ret != SAVE_FS_LIST_ENTRY_SIZE)
    {
        LOGFILE("Failed to read FS entry from FAT storage!");
        return 0;
    }
    
    return ret;
}

bool save_fs_list_get_value(save_filesystem_list_ctx_t *ctx, u32 index, save_fs_list_entry_t *value)
{
    if (!ctx || !value)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 capacity = save_fs_list_get_capacity(ctx);
    if (!capacity)
    {
        LOGFILE("Failed to retrieve FS capacity!");
        return false;
    }
    
    if (index >= capacity)
    {
        LOGFILE("Provided index exceeds FS capacity!");
        return false;
    }
    
    if (!save_fs_list_read_entry(ctx, index, value))
    {
        LOGFILE("Failed to read FS entry!");
        return false;
    }
    
    return true;
}

u32 save_fs_list_get_index_from_key(save_filesystem_list_ctx_t *ctx, save_entry_key_t *key, u32 *prev_index)
{
    u32 prev;
    if (!prev_index) prev_index = &prev;
    
    if (!ctx || !key)
    {
        LOGFILE("Invalid parameters!");
        goto out;
    }
    
    u32 capacity = save_fs_list_get_capacity(ctx);
    if (!capacity)
    {
        LOGFILE("Failed to retrieve FS capacity!");
        goto out;
    }
    
    save_fs_list_entry_t entry;
    if (!save_fs_list_read_entry(ctx, ctx->used_list_head_index, &entry))
    {
        LOGFILE("Failed to read FS entry for initial index %u!", ctx->used_list_head_index);
        goto out;
    }
    
    *prev_index = ctx->used_list_head_index;
    u32 index = entry.next;
    
    while(index)
    {
        if (index > capacity)
        {
            LOGFILE("Save entry index %d out of range!", index);
            break;
        }
        
        if (!save_fs_list_read_entry(ctx, index, &entry))
        {
            LOGFILE("Failed to read FS entry for index %u!", index);
            break;
        }
        
        if (entry.parent == key->parent && !strcmp(entry.name, key->name)) return index;
        
        *prev_index = index;
        index = entry.next;
    }
    
    if (!index) LOGFILE("Unable to find FS index from key!");
    
out:
    *prev_index = 0xFFFFFFFF;
    return 0xFFFFFFFF;
}

bool save_hierarchical_file_table_find_path_recursive(hierarchical_save_file_table_ctx_t *ctx, save_entry_key_t *key, const char *path)
{
    if (!ctx || !key || !path || !strlen(path))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    key->parent = 0;
    const char *pos = strchr(path, '/');
    
    while(pos)
    {
        memset(key->name, 0, SAVE_FS_LIST_MAX_NAME_LENGTH);
        
        const char *tmp = strchr(pos, '/');
        if (!tmp)
        {
            memcpy(key->name, pos, strlen(pos));
            break;
        }
        
        memcpy(key->name, pos, tmp - pos);
        
        key->parent = save_fs_list_get_index_from_key(&ctx->directory_table, key, NULL);
        if (key->parent == 0xFFFFFFFF) return false;
        
        pos = (tmp + 1);
    }
    
    return true;
}

bool save_hierarchical_file_table_get_file_entry_by_path(hierarchical_save_file_table_ctx_t *ctx, const char *path, save_fs_list_entry_t *entry)
{
    if (!ctx || !path || !strlen(path) || !entry)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    save_entry_key_t key;
    if (!save_hierarchical_file_table_find_path_recursive(ctx, &key, path))
    {
        LOGFILE("Unable to locate file \"%s\"!", path);
        return false;
    }
    
    u32 index = save_fs_list_get_index_from_key(&ctx->file_table, &key, NULL);
    if (index == 0xFFFFFFFF)
    {
        LOGFILE("Unable to get table index for file \"%s\"!", path);
        return false;
    }
    
    if (!save_fs_list_get_value(&ctx->file_table, index, entry))
    {
        LOGFILE("Unable to get file entry for \"%s\" from index!", path);
        return false;
    }
    
    return true;
}

bool save_open_fat_storage(save_filesystem_ctx_t *ctx, allocation_table_storage_ctx_t *storage_ctx, u32 block_index)
{
    if (!ctx || !ctx->base_storage || !storage_ctx)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    storage_ctx->base_storage = ctx->base_storage;
    storage_ctx->fat = &ctx->allocation_table;
    storage_ctx->block_size = (u32)ctx->header->block_size;
    storage_ctx->initial_block = block_index;
    
    if (block_index == 0xFFFFFFFF)
    {
        storage_ctx->_length = 0;
    } else {
        u32 fat_list_length = save_allocation_table_get_list_length(storage_ctx->fat, block_index);
        if (!fat_list_length)
        {
            LOGFILE("Failed to retrieve FAT list length!");
            return false;
        }
        
        storage_ctx->_length = (fat_list_length * storage_ctx->block_size);
    }
    
    return true;
}

static bool save_filesystem_init(save_filesystem_ctx_t *ctx, void *fat, save_fs_header_t *save_fs_header, fat_header_t *fat_header)
{
    if (!ctx || !fat || !save_fs_header || !fat_header)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    ctx->allocation_table.base_storage = fat;
    ctx->allocation_table.header = fat_header;
    ctx->allocation_table.free_list_entry_index = 0;
    ctx->header = save_fs_header;
    
    if (!save_open_fat_storage(ctx, &ctx->file_table.directory_table.storage, fat_header->directory_table_block))
    {
        LOGFILE("Failed to open FAT directory storage!");
        return false;
    }
    
    if (!save_open_fat_storage(ctx, &ctx->file_table.file_table.storage, fat_header->file_table_block))
    {
        LOGFILE("Failed to open FAT file storage!");
        return false;
    }
    
    ctx->file_table.file_table.free_list_head_index = 0;
    ctx->file_table.file_table.used_list_head_index = 1;
    ctx->file_table.directory_table.free_list_head_index = 0;
    ctx->file_table.directory_table.used_list_head_index = 1;
    
    return true;
}

static validity_t save_ivfc_validate(hierarchical_integrity_verification_storage_ctx_t *ctx, ivfc_save_hdr_t *ivfc)
{
    if (!ctx || !ivfc || !ivfc->num_levels)
    {
        LOGFILE("Invalid parameters!");
        return VALIDITY_INVALID;
    }
    
    validity_t result = VALIDITY_VALID;
    
    for(u32 i = 0; i < (ivfc->num_levels - 1) && result != VALIDITY_INVALID; i++)
    {
        integrity_verification_storage_ctx_t *storage = &ctx->integrity_storages[i];
        
        u64 block_size = storage->sector_size;
        u32 block_count = (u32)((storage->_length + block_size - 1) / block_size);
        
        u8 *buffer = calloc(1, block_size);
        if (!buffer)
        {
            LOGFILE("Failed to allocate memory for input buffer!");
            result = VALIDITY_INVALID;
            break;
        }
        
        for(u32 j = 0; j < block_count; j++)
        {
            if (ctx->level_validities[ivfc->num_levels - 2][j] == VALIDITY_UNCHECKED)
            {
                u32 to_read = ((storage->_length - (block_size * j)) < block_size ? (storage->_length - (block_size * j)) : block_size);
                if (!save_ivfc_storage_read(storage, buffer, block_size * j, to_read, 1))
                {
                    LOGFILE("Failed to read IVFC storage data!");
                    result = VALIDITY_INVALID;
                    break;
                }
            }
            
            if (ctx->level_validities[ivfc->num_levels - 2][j] == VALIDITY_INVALID)
            {
                result = VALIDITY_INVALID;
                break;
            }
        }
        
        free(buffer);
        
        if (result == VALIDITY_INVALID) break;
    }
    
    return result;
}

static bool save_ivfc_set_level_validities(hierarchical_integrity_verification_storage_ctx_t *ctx, ivfc_save_hdr_t *ivfc)
{
    if (!ctx || !ivfc || !ivfc->num_levels)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    bool success = true;
    
    for(u32 i = 0; i < (ivfc->num_levels - 1); i++)
    {
        validity_t level_validity = VALIDITY_VALID;
        
        for(u32 j = 0; j < ctx->integrity_storages[i].sector_count; j++)
        {
            if (ctx->level_validities[i][j] == VALIDITY_INVALID)
            {
                level_validity = VALIDITY_INVALID;
                break;
            }
            
            if (ctx->level_validities[i][j] == VALIDITY_UNCHECKED && level_validity != VALIDITY_INVALID) level_validity = VALIDITY_UNCHECKED;
        }
        
        ctx->levels[i].hash_validity = level_validity;
        
        if (success && level_validity == VALIDITY_INVALID) success = false;
    }
    
    if (!success) LOGFILE("Invalid IVFC level!");
    
    return success;
}

static validity_t save_filesystem_verify(save_ctx_t *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid parameters!");
        return VALIDITY_INVALID;
    }
    
    validity_t journal_validity = save_ivfc_validate(&ctx->core_data_ivfc_storage, &ctx->header.data_ivfc_header);
    if (journal_validity == VALIDITY_INVALID)
    {
        LOGFILE("Invalid core IVFC storage!");
        return journal_validity;
    }
    
    if (!save_ivfc_set_level_validities(&ctx->core_data_ivfc_storage, &ctx->header.data_ivfc_header))
    {
        LOGFILE("Invalid IVFC level in core IVFC storage!");
        journal_validity = VALIDITY_INVALID;
        return journal_validity;
    }
    
    if (!ctx->fat_ivfc_storage.levels[0].save_ctx) return journal_validity;
    
    validity_t fat_validity = save_ivfc_validate(&ctx->fat_ivfc_storage, &ctx->header.fat_ivfc_header);
    if (fat_validity == VALIDITY_INVALID)
    {
        LOGFILE("Invalid FAT IVFC storage!");
        return fat_validity;
    }
    
    if (!save_ivfc_set_level_validities(&ctx->fat_ivfc_storage, &ctx->header.fat_ivfc_header))
    {
        LOGFILE("Invalid IVFC level in FAT IVFC storage!");
        fat_validity = VALIDITY_INVALID;
        return fat_validity;
    }
    
    if (journal_validity != VALIDITY_VALID) return journal_validity;
    if (fat_validity != VALIDITY_VALID) return fat_validity;
    
    return journal_validity;
}

bool save_process(save_ctx_t *ctx)
{
    if (!ctx || !ctx->file)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    UINT br = 0;
    FRESULT fr;
    bool success = false;
    
    /* Try to parse Header A. */
    f_rewind(ctx->file);
    
    fr = f_read(ctx->file, &ctx->header, sizeof(ctx->header), &br);
    if (fr || br != sizeof(ctx->header))
    {
        LOGFILE("Failed to read savefile header A! (%u)", fr);
        return success;
    }
    
    if (!save_process_header(ctx) || ctx->header_hash_validity == VALIDITY_INVALID)
    {
        /* Try to parse Header B. */
        fr = f_lseek(ctx->file, 0x4000);
        if (fr || f_tell(ctx->file) != 0x4000)
        {
            LOGFILE("Failed to seek to offset 0x4000 in savefile! (%u)", fr);
            return success;
        }
        
        fr = f_read(ctx->file, &ctx->header, sizeof(ctx->header), &br);
        if (fr || br != sizeof(ctx->header))
        {
            LOGFILE("Failed to read savefile header B! (%u)", fr);
            return success;
        }
        
        if (!save_process_header(ctx) || ctx->header_hash_validity == VALIDITY_INVALID)
        {
            LOGFILE("Savefile header is invalid!");
            return success;
        }
    }
    
    u8 cmac[0x10] = {0};
    cmacAes128CalculateMac(cmac, ctx->save_mac_key, &ctx->header.layout, sizeof(ctx->header.layout));
    
    ctx->header_cmac_validity = (!memcmp(cmac, &ctx->header.cmac, 0x10) ? VALIDITY_VALID : VALIDITY_INVALID);
    
    /* Initialize remap storages. */
    ctx->data_remap_storage.type = STORAGE_BYTES;
    ctx->data_remap_storage.base_storage_offset = ctx->header.layout.file_map_data_offset;
    ctx->data_remap_storage.header = &ctx->header.main_remap_header;
    ctx->data_remap_storage.file = ctx->file;
    
    ctx->data_remap_storage.map_entries = calloc(sizeof(remap_entry_ctx_t), ctx->data_remap_storage.header->map_entry_count);
    if (!ctx->data_remap_storage.map_entries)
    {
        LOGFILE("Failed to allocate memory for data remap storage entries!");
        return success;
    }
    
    fr = f_lseek(ctx->file, ctx->header.layout.file_map_entry_offset);
    if (fr || f_tell(ctx->file) != ctx->header.layout.file_map_entry_offset)
    {
        LOGFILE("Failed to seek to file map entry offset 0x%lX in savefile! (%u)", ctx->header.layout.file_map_entry_offset, fr);
        return success;
    }
    
    for(u32 i = 0; i < ctx->data_remap_storage.header->map_entry_count; i++)
    {
        fr = f_read(ctx->file, &ctx->data_remap_storage.map_entries[i], 0x20, &br);
        if (fr || br != 0x20)
        {
            LOGFILE("Failed to read data remap storage entry #%u! (%u)", i, fr);
            goto out;
        }
        
        ctx->data_remap_storage.map_entries[i].physical_offset_end = (ctx->data_remap_storage.map_entries[i].physical_offset + ctx->data_remap_storage.map_entries[i].size);
        ctx->data_remap_storage.map_entries[i].virtual_offset_end = (ctx->data_remap_storage.map_entries[i].virtual_offset + ctx->data_remap_storage.map_entries[i].size);
    }
    
    /* Initialize data remap storage. */
    ctx->data_remap_storage.segments = save_remap_init_segments(ctx->data_remap_storage.header, ctx->data_remap_storage.map_entries, ctx->data_remap_storage.header->map_entry_count);
    if (!ctx->data_remap_storage.segments)
    {
        LOGFILE("Failed to retrieve data remap storage segments!");
        goto out;
    }
    
    /* Initialize duplex storage. */
    ctx->duplex_layers[0].data_a = ((u8*)&ctx->header + ctx->header.layout.duplex_master_offset_a);
    ctx->duplex_layers[0].data_b = ((u8*)&ctx->header + ctx->header.layout.duplex_master_offset_b);
    memcpy(&ctx->duplex_layers[0].info, &ctx->header.duplex_header.layers[0], sizeof(duplex_info_t));
    
    ctx->duplex_layers[1].data_a = calloc(1, ctx->header.layout.duplex_l1_size);
    if (!ctx->duplex_layers[1].data_a)
    {
        LOGFILE("Failed to allocate memory for data_a block in duplex layer #1!");
        goto out;
    }
    
    if (save_remap_read(&ctx->data_remap_storage, ctx->duplex_layers[1].data_a, ctx->header.layout.duplex_l1_offset_a, ctx->header.layout.duplex_l1_size) != ctx->header.layout.duplex_l1_size)
    {
        LOGFILE("Failed to read data_a block from duplex layer #1 in data remap storage!");
        goto out;
    }
    
    ctx->duplex_layers[1].data_b = calloc(1, ctx->header.layout.duplex_l1_size);
    if (!ctx->duplex_layers[1].data_b)
    {
        LOGFILE("Failed to allocate memory for data_b block in duplex layer #1!");
        goto out;
    }
    
    if (save_remap_read(&ctx->data_remap_storage, ctx->duplex_layers[1].data_b, ctx->header.layout.duplex_l1_offset_b, ctx->header.layout.duplex_l1_size) != ctx->header.layout.duplex_l1_size)
    {
        LOGFILE("Failed to read data_b block from duplex layer #1 in data remap storage!");
        goto out;
    }
    
    memcpy(&ctx->duplex_layers[1].info, &ctx->header.duplex_header.layers[1], sizeof(duplex_info_t));
    
    ctx->duplex_layers[2].data_a = calloc(1, ctx->header.layout.duplex_data_size);
    if (!ctx->duplex_layers[2].data_a)
    {
        LOGFILE("Failed to allocate memory for data_a block in duplex layer #2!");
        goto out;
    }
    
    if (save_remap_read(&ctx->data_remap_storage, ctx->duplex_layers[2].data_a, ctx->header.layout.duplex_data_offset_a, ctx->header.layout.duplex_data_size) != ctx->header.layout.duplex_data_size)
    {
        LOGFILE("Failed to read data_a block from duplex layer #2 in data remap storage!");
        goto out;
    }
    
    ctx->duplex_layers[2].data_b = calloc(1, ctx->header.layout.duplex_data_size);
    if (!ctx->duplex_layers[2].data_b)
    {
        LOGFILE("Failed to allocate memory for data_b block in duplex layer #2!");
        goto out;
    }
    
    if (save_remap_read(&ctx->data_remap_storage, ctx->duplex_layers[2].data_b, ctx->header.layout.duplex_data_offset_b, ctx->header.layout.duplex_data_size) != ctx->header.layout.duplex_data_size)
    {
        LOGFILE("Failed to read data_b block from duplex layer #2 in data remap storage!");
        goto out;
    }
    
    memcpy(&ctx->duplex_layers[2].info, &ctx->header.duplex_header.layers[2], sizeof(duplex_info_t));
    
    /* Initialize hierarchical duplex storage. */
    u8 *bitmap = (ctx->header.layout.duplex_index == 1 ? ctx->duplex_layers[0].data_b : ctx->duplex_layers[0].data_a);
    
    if (!save_duplex_storage_init(&ctx->duplex_storage.layers[0], &ctx->duplex_layers[1], bitmap, ctx->header.layout.duplex_master_size))
    {
        LOGFILE("Failed to initialize duplex storage layer #0!");
        goto out;
    }
    
    ctx->duplex_storage.layers[0]._length = ctx->header.layout.duplex_l1_size;
    
    bitmap = calloc(1, ctx->duplex_storage.layers[0]._length);
    if (!bitmap)
    {
        LOGFILE("Failed to allocate memory for duplex storage layer #0 bitmap!");
        goto out;
    }
    
    if (save_duplex_storage_read(&ctx->duplex_storage.layers[0], bitmap, 0, ctx->duplex_storage.layers[0]._length) != ctx->duplex_storage.layers[0]._length)
    {
        LOGFILE("Failed to read duplex storage layer #0 bitmap!");
        free(bitmap);
        goto out;
    }
    
    if (!save_duplex_storage_init(&ctx->duplex_storage.layers[1], &ctx->duplex_layers[2], bitmap, ctx->duplex_storage.layers[0]._length))
    {
        LOGFILE("Failed to initialize duplex storage layer #1!");
        goto out;
    }
    
    ctx->duplex_storage.layers[1]._length = ctx->header.layout.duplex_data_size;
    
    ctx->duplex_storage.data_layer = ctx->duplex_storage.layers[1];
    
    /* Initialize meta remap storage. */
    ctx->meta_remap_storage.type = STORAGE_DUPLEX;
    ctx->meta_remap_storage.duplex = &ctx->duplex_storage.data_layer;
    ctx->meta_remap_storage.header = &ctx->header.meta_remap_header;
    ctx->meta_remap_storage.file = ctx->file;
    
    ctx->meta_remap_storage.map_entries = calloc(sizeof(remap_entry_ctx_t), ctx->meta_remap_storage.header->map_entry_count);
    if (!ctx->meta_remap_storage.map_entries)
    {
        LOGFILE("Failed to allocate memory for meta remap storage entries!");
        goto out;
    }
    
    fr = f_lseek(ctx->file, ctx->header.layout.meta_map_entry_offset);
    if (fr || f_tell(ctx->file) != ctx->header.layout.meta_map_entry_offset)
    {
        LOGFILE("Failed to seek to meta map entry offset 0x%lX in savefile! (%u)", ctx->header.layout.meta_map_entry_offset, fr);
        goto out;
    }
    
    for(u32 i = 0; i < ctx->meta_remap_storage.header->map_entry_count; i++)
    {
        fr = f_read(ctx->file, &ctx->meta_remap_storage.map_entries[i], 0x20, &br);
        if (fr || br != 0x20)
        {
            LOGFILE("Failed to read meta remap storage entry #%u! (%u)", i, fr);
            goto out;
        }
        
        ctx->meta_remap_storage.map_entries[i].physical_offset_end = (ctx->meta_remap_storage.map_entries[i].physical_offset + ctx->meta_remap_storage.map_entries[i].size);
        ctx->meta_remap_storage.map_entries[i].virtual_offset_end = (ctx->meta_remap_storage.map_entries[i].virtual_offset + ctx->meta_remap_storage.map_entries[i].size);
    }
    
    ctx->meta_remap_storage.segments = save_remap_init_segments(ctx->meta_remap_storage.header, ctx->meta_remap_storage.map_entries, ctx->meta_remap_storage.header->map_entry_count);
    if (!ctx->meta_remap_storage.segments)
    {
        LOGFILE("Failed to retrieve meta remap storage segments!");
        goto out;
    }
    
    /* Initialize journal map. */
    ctx->journal_map_info.map_storage = calloc(1, ctx->header.layout.journal_map_table_size);
    if (!ctx->journal_map_info.map_storage)
    {
        LOGFILE("Failed to allocate memory for journal map info!");
        goto out;
    }
    
    if (save_remap_read(&ctx->meta_remap_storage, ctx->journal_map_info.map_storage, ctx->header.layout.journal_map_table_offset, ctx->header.layout.journal_map_table_size) != ctx->header.layout.journal_map_table_size)
    {
        LOGFILE("Failed to read map storage from journal map info in meta remap storage!");
        goto out;
    }
    
    /* Initialize journal storage. */
    ctx->journal_storage.header = &ctx->header.journal_header;
    ctx->journal_storage.journal_data_offset = ctx->header.layout.journal_data_offset;
    ctx->journal_storage._length = (ctx->journal_storage.header->total_size - ctx->journal_storage.header->journal_size);
    ctx->journal_storage.file = ctx->file;
    ctx->journal_storage.map.header = &ctx->header.map_header;
    ctx->journal_storage.map.map_storage = ctx->journal_map_info.map_storage;
    
    ctx->journal_storage.map.entries = calloc(sizeof(journal_map_entry_t), ctx->journal_storage.map.header->main_data_block_count);
    if (!ctx->journal_storage.map.entries)
    {
        LOGFILE("Failed to allocate memory for journal map storage entries!");
        goto out;
    }
    
    u32 *pos = (u32*)ctx->journal_storage.map.map_storage;
    
    for(u32 i = 0; i < ctx->journal_storage.map.header->main_data_block_count; i++)
    {
        ctx->journal_storage.map.entries[i].virtual_index = i;
        ctx->journal_storage.map.entries[i].physical_index = (*pos & 0x7FFFFFFF);
        pos += 2;
    }
    
    ctx->journal_storage.block_size = ctx->journal_storage.header->block_size;
    ctx->journal_storage._length = (ctx->journal_storage.header->total_size - ctx->journal_storage.header->journal_size);
    
    /* Initialize core IVFC storage. */
    for(u32 i = 0; i < 5; i++) ctx->core_data_ivfc_storage.levels[i].save_ctx = ctx;
    
    if (!save_ivfc_storage_init(&ctx->core_data_ivfc_storage, ctx->header.layout.ivfc_master_hash_offset_a, &ctx->header.data_ivfc_header))
    {
        LOGFILE("Failed to initialize core IVFC storage!");
        goto out;
    }
    
    /* Initialize FAT storage. */
    if (ctx->header.layout.version < 0x50000)
    {
        ctx->fat_storage = calloc(1, ctx->header.layout.fat_size);
        if (!ctx->fat_storage)
        {
            LOGFILE("Failed to allocate memory for FAT storage!");
            goto out;
        }
        
        if (save_remap_read(&ctx->meta_remap_storage, ctx->fat_storage, ctx->header.layout.fat_offset, ctx->header.layout.fat_size) != ctx->header.layout.fat_size)
        {
            LOGFILE("Failed to read FAT storage from meta remap storage!");
            goto out;
        }
    } else {
        for(u32 i = 0; i < 5; i++) ctx->fat_ivfc_storage.levels[i].save_ctx = ctx;
        
        if (!save_ivfc_storage_init(&ctx->fat_ivfc_storage, ctx->header.layout.fat_ivfc_master_hash_a, &ctx->header.fat_ivfc_header))
        {
            LOGFILE("Failed to initialize FAT storage (IVFC)!");
            goto out;
        }
        
        ctx->fat_storage = calloc(1, ctx->fat_ivfc_storage._length);
        if (!ctx->fat_storage)
        {
            LOGFILE("Failed to allocate memory for FAT storage (IVFC)!");
            goto out;
        }
        
        if (save_remap_read(&ctx->meta_remap_storage, ctx->fat_storage, ctx->header.fat_ivfc_header.level_headers[ctx->header.fat_ivfc_header.num_levels - 2].logical_offset, ctx->fat_ivfc_storage._length) != ctx->fat_ivfc_storage._length)
        {
            LOGFILE("Failed to read FAT storage from meta remap storage (IVFC)!");
            goto out;
        }
    }
    
    if (ctx->tool_ctx.action & ACTION_VERIFY)
    {
        if (save_filesystem_verify(ctx) == VALIDITY_INVALID)
        {
            LOGFILE("Savefile FS verification failed!");
            goto out;
        }
    }
    
    /* Initialize core save filesystem. */
    ctx->save_filesystem_core.base_storage = &ctx->core_data_ivfc_storage;
    if (!save_filesystem_init(&ctx->save_filesystem_core, ctx->fat_storage, &ctx->header.save_header, &ctx->header.fat_header))
    {
        LOGFILE("Failed to initialize savefile FS!");
        goto out;
    }
    
    success = true;
    
out:
    if (!success) save_free_contexts(ctx);
    
    return success;
}

bool save_process_header(save_ctx_t *ctx)
{
    if (!ctx)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    if (ctx->header.layout.magic != MAGIC_DISF || ctx->header.duplex_header.magic != MAGIC_DPFS || \
        ctx->header.data_ivfc_header.magic != MAGIC_IVFC || ctx->header.journal_header.magic != MAGIC_JNGL || \
        ctx->header.save_header.magic != MAGIC_SAVE || ctx->header.main_remap_header.magic != MAGIC_RMAP || \
        ctx->header.meta_remap_header.magic != MAGIC_RMAP)
    {
        LOGFILE("Save header is corrupt!");
        return false;
    }
    
    ctx->data_ivfc_master = ((u8*)&ctx->header + ctx->header.layout.ivfc_master_hash_offset_a);
    ctx->fat_ivfc_master = ((u8*)&ctx->header + ctx->header.layout.fat_ivfc_master_hash_a);
    
    u8 hash[SHA256_HASH_SIZE];
    sha256CalculateHash(hash, &ctx->header.duplex_header, 0x3D00);
    
    ctx->header_hash_validity = (memcmp(hash, ctx->header.layout.hash, SHA256_HASH_SIZE) == 0 ? VALIDITY_VALID : VALIDITY_INVALID);
    
    ctx->header.data_ivfc_header.num_levels = 5;
    
    if (ctx->header.layout.version >= 0x50000) ctx->header.fat_ivfc_header.num_levels = 4;
    
    return true;
}

void save_free_contexts(save_ctx_t *ctx)
{
    if (!ctx) return;
    
    if (ctx->data_remap_storage.segments)
    {
        if (ctx->data_remap_storage.header)
        {
            for(u32 i = 0; i < ctx->data_remap_storage.header->map_segment_count; i++)
            {
                if (ctx->data_remap_storage.segments[i].entries) free(ctx->data_remap_storage.segments[i].entries);
            }
        }
        
        free(ctx->data_remap_storage.segments);
        ctx->data_remap_storage.segments = NULL;
    }
    
    if (ctx->data_remap_storage.map_entries)
    {
        free(ctx->data_remap_storage.map_entries);
        ctx->data_remap_storage.map_entries = NULL;
    }
    
    if (ctx->meta_remap_storage.segments)
    {
        if (ctx->meta_remap_storage.header)
        {
            for(u32 i = 0; i < ctx->meta_remap_storage.header->map_segment_count; i++)
            {
                if (ctx->meta_remap_storage.segments[i].entries) free(ctx->meta_remap_storage.segments[i].entries);
            }
        }
        
        free(ctx->meta_remap_storage.segments);
        ctx->meta_remap_storage.segments = NULL;
    }
    
    if (ctx->meta_remap_storage.map_entries)
    {
        free(ctx->meta_remap_storage.map_entries);
        ctx->meta_remap_storage.map_entries = NULL;
    }
    
    if (ctx->duplex_storage.layers[0].bitmap.bitmap)
    {
        free(ctx->duplex_storage.layers[0].bitmap.bitmap);
        ctx->duplex_storage.layers[0].bitmap.bitmap = NULL;
    }
    
    if (ctx->duplex_storage.layers[1].bitmap.bitmap)
    {
        free(ctx->duplex_storage.layers[1].bitmap.bitmap);
        ctx->duplex_storage.layers[1].bitmap.bitmap = NULL;
    }
    
    if (ctx->duplex_storage.layers[1].bitmap_storage)
    {
        free(ctx->duplex_storage.layers[1].bitmap_storage);
        ctx->duplex_storage.layers[1].bitmap_storage = NULL;
    }
    
    for(u32 i = 1; i < 3; i++)
    {
        if (ctx->duplex_layers[i].data_a)
        {
            free(ctx->duplex_layers[i].data_a);
            ctx->duplex_layers[i].data_a = NULL;
        }
        
        if (ctx->duplex_layers[i].data_b)
        {
            free(ctx->duplex_layers[i].data_b);
            ctx->duplex_layers[i].data_b = NULL;
        }
    }
    
    if (ctx->journal_map_info.map_storage)
    {
        free(ctx->journal_map_info.map_storage);
        ctx->journal_map_info.map_storage = NULL;
    }
    
    if (ctx->journal_storage.map.entries)
    {
        free(ctx->journal_storage.map.entries);
        ctx->journal_storage.map.entries = NULL;
    }
    
    for(u32 i = 0; i < ctx->header.data_ivfc_header.num_levels - 1; i++)
    {
        if (ctx->core_data_ivfc_storage.integrity_storages[i].block_validities)
        {
            free(ctx->core_data_ivfc_storage.integrity_storages[i].block_validities);
            ctx->core_data_ivfc_storage.integrity_storages[i].block_validities = NULL;
        }
    }
    
    if (ctx->core_data_ivfc_storage.level_validities)
    {
        free(ctx->core_data_ivfc_storage.level_validities);
        ctx->core_data_ivfc_storage.level_validities = NULL;
    }
    
    if (ctx->header.layout.version >= 0x50000)
    {
        for(u32 i = 0; i < ctx->header.fat_ivfc_header.num_levels - 1; i++)
        {
            if (ctx->fat_ivfc_storage.integrity_storages[i].block_validities)
            {
                free(ctx->fat_ivfc_storage.integrity_storages[i].block_validities);
                ctx->fat_ivfc_storage.integrity_storages[i].block_validities = NULL;
            }
        }
    }
    
    if (ctx->fat_ivfc_storage.level_validities)
    {
        free(ctx->fat_ivfc_storage.level_validities);
        ctx->fat_ivfc_storage.level_validities = NULL;
    }
    
    if (ctx->fat_storage)
    {
        free(ctx->fat_storage);
        ctx->fat_storage = NULL;
    }
}

save_ctx_t *save_open_savefile(const char *path, u32 action)
{
    if (!path || !strlen(path))
    {
        LOGFILE("Invalid savefile path!");
        return NULL;
    }
    
    FRESULT fr = FR_OK;
    FIL *save_fd = NULL;
    save_ctx_t *save_ctx = NULL;
    bool open_savefile = false, success = false;
    
    save_fd = calloc(1, sizeof(FIL));
    if (!save_fd)
    {
        LOGFILE("Unable to allocate memory for FatFs file descriptor!");
        return NULL;
    }
    
    fr = f_open(save_fd, path, FA_READ | FA_OPEN_EXISTING);
    if (fr != FR_OK)
    {
        LOGFILE("Failed to open \"%s\" savefile from BIS System partition! (%u)", path, fr);
        goto out;
    }
    
    open_savefile = true;
    
    save_ctx = calloc(1, sizeof(save_ctx_t));
    if (!save_ctx)
    {
        LOGFILE("Unable to allocate memory for savefile \"%s\" context!", path);
        goto out;
    }
    
    save_ctx->file = save_fd;
    save_ctx->tool_ctx.action = action;
    
    success = save_process(save_ctx);
    if (!success) LOGFILE("Failed to process savefile \"%s\"!", path);
    
out:
    if (!success)
    {
        if (save_ctx)
        {
            free(save_ctx);
            save_ctx = NULL;
        }
        
        if (save_fd)
        {
            if (open_savefile) f_close(save_fd);
            free(save_fd);
        }
    }
    
    return save_ctx;
}

void save_close_savefile(save_ctx_t *ctx)
{
    if (!ctx) return;
    
    if (ctx->file)
    {
        f_close(ctx->file);
        free(ctx->file);
    }
    
    save_free_contexts(ctx);
    
    free(ctx);
}

bool save_get_fat_storage_from_file_entry_by_path(save_ctx_t *ctx, const char *path, allocation_table_storage_ctx_t *out_fat_storage, u64 *out_file_entry_size)
{
    if (!ctx || !path || !strlen(path) || !out_fat_storage || !out_file_entry_size)
    {
        LOGFILE("Invalid file entry path!");
        return false;
    }
    
    save_fs_list_entry_t entry = {0};
    
    if (!save_hierarchical_file_table_get_file_entry_by_path(&(ctx->save_filesystem_core.file_table), path, &entry))
    {
        LOGFILE("Failed to get file entry for \"%s\" in savefile!", path);
        return false;
    }
    
    if (!save_open_fat_storage(&(ctx->save_filesystem_core), out_fat_storage, entry.value.save_file_info.start_block))
    {
        LOGFILE("Failed to open FAT storage at block 0x%X for \"%s\" in savefile!", entry.value.save_file_info.start_block, path);
        return false;
    }
    
    *out_file_entry_size = entry.value.save_file_info.length;
    
    return true;
}
