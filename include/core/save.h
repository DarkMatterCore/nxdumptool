/*
 * save.h
 *
 * Copyright (c) 2019-2020, shchmue.
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

#ifndef __SAVE_H__
#define __SAVE_H__

#include "fatfs/ff.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IVFC_MAX_LEVEL                  6

#define SAVE_HEADER_SIZE                0x4000
#define SAVE_FAT_ENTRY_SIZE             8
#define SAVE_FS_LIST_MAX_NAME_LENGTH    0x40
#define SAVE_FS_LIST_ENTRY_SIZE         0x60

#define MAGIC_DISF                      0x46534944
#define MAGIC_DPFS                      0x53465044
#define MAGIC_JNGL                      0x4C474E4A
#define MAGIC_SAVE                      0x45564153
#define MAGIC_RMAP                      0x50414D52
#define MAGIC_IVFC                      0x43465649

#define ACTION_VERIFY                   (1<<2)

typedef enum {
    VALIDITY_UNCHECKED = 0,
    VALIDITY_INVALID,
    VALIDITY_VALID
} validity_t;

typedef struct save_ctx_t save_ctx_t;

typedef struct {
    u32 magic; /* "DISF". */
    u32 version;
    u8 hash[0x20];
    u64 file_map_entry_offset;
    u64 file_map_entry_size;
    u64 meta_map_entry_offset;
    u64 meta_map_entry_size;
    u64 file_map_data_offset;
    u64 file_map_data_size;
    u64 duplex_l1_offset_a;
    u64 duplex_l1_offset_b;
    u64 duplex_l1_size;
    u64 duplex_data_offset_a;
    u64 duplex_data_offset_b;
    u64 duplex_data_size;
    u64 journal_data_offset;
    u64 journal_data_size_a;
    u64 journal_data_size_b;
    u64 journal_size;
    u64 duplex_master_offset_a;
    u64 duplex_master_offset_b;
    u64 duplex_master_size;
    u64 ivfc_master_hash_offset_a;
    u64 ivfc_master_hash_offset_b;
    u64 ivfc_master_hash_size;
    u64 journal_map_table_offset;
    u64 journal_map_table_size;
    u64 journal_physical_bitmap_offset;
    u64 journal_physical_bitmap_size;
    u64 journal_virtual_bitmap_offset;
    u64 journal_virtual_bitmap_size;
    u64 journal_free_bitmap_offset;
    u64 journal_free_bitmap_size;
    u64 ivfc_l1_offset;
    u64 ivfc_l1_size;
    u64 ivfc_l2_offset;
    u64 ivfc_l2_size;
    u64 ivfc_l3_offset;
    u64 ivfc_l3_size;
    u64 fat_offset;
    u64 fat_size;
    u64 duplex_index;
    u64 fat_ivfc_master_hash_a;
    u64 fat_ivfc_master_hash_b;
    u64 fat_ivfc_l1_offset;
    u64 fat_ivfc_l1_size;
    u64 fat_ivfc_l2_offset;
    u64 fat_ivfc_l2_size;
    u8 _0x190[0x70];
} fs_layout_t;

NXDT_ASSERT(fs_layout_t, 0x200);

#pragma pack(push, 1)
typedef struct {
    u64 offset;
    u64 length;
    u32 block_size_power;
} duplex_info_t;
#pragma pack(pop)

NXDT_ASSERT(duplex_info_t, 0x14);

typedef struct {
    u32 magic; /* "DPFS". */
    u32 version;
    duplex_info_t layers[3];
} duplex_header_t;

NXDT_ASSERT(duplex_header_t, 0x44);

typedef struct {
    u32 version;
    u32 main_data_block_count;
    u32 journal_block_count;
    u32 _0x0C;
} journal_map_header_t;

NXDT_ASSERT(journal_map_header_t, 0x10);

typedef struct {
    u32 magic; /* "JNGL". */
    u32 version;
    u64 total_size;
    u64 journal_size;
    u64 block_size;
} journal_header_t;

NXDT_ASSERT(journal_header_t, 0x20);

typedef struct {
    u32 magic; /* "SAVE". */
    u32 version;
    u64 block_count;
    u64 block_size;
} save_fs_header_t;

NXDT_ASSERT(save_fs_header_t, 0x18);

typedef struct {
    u64 block_size;
    u64 allocation_table_offset;
    u32 allocation_table_block_count;
    u32 _0x14;
    u64 data_offset;
    u32 data_block_count;
    u32 _0x24;
    u32 directory_table_block;
    u32 file_table_block;
} fat_header_t;

NXDT_ASSERT(fat_header_t, 0x30);

typedef struct {
    u32 magic; /* "RMAP". */
    u32 version;
    u32 map_entry_count;
    u32 map_segment_count;
    u32 segment_bits;
    u8 _0x14[0x2C];
} remap_header_t;

NXDT_ASSERT(remap_header_t, 0x40);

typedef struct remap_segment_ctx_t remap_segment_ctx_t;
typedef struct remap_entry_ctx_t remap_entry_ctx_t;

#pragma pack(push, 1)
struct remap_entry_ctx_t {
    u64 virtual_offset;
    u64 physical_offset;
    u64 size;
    u32 alignment;
    u32 _0x1C;
    u64 virtual_offset_end;
    u64 physical_offset_end;
    remap_segment_ctx_t *segment;
    remap_entry_ctx_t *next;
};
#pragma pack(pop)

struct remap_segment_ctx_t{
    u64 offset;
    u64 length;
    remap_entry_ctx_t **entries;
    u64 entry_count;
};

typedef struct {
    u8 *data;
    u8 *bitmap;
} duplex_bitmap_t;

typedef struct {
    u32 block_size;
    u8 *bitmap_storage;
    u8 *data_a;
    u8 *data_b;
    duplex_bitmap_t bitmap;
    u64 _length;
} duplex_storage_ctx_t;

enum base_storage_type {
    STORAGE_BYTES = 0,
    STORAGE_DUPLEX = 1,
    STORAGE_REMAP = 2,
    STORAGE_JOURNAL = 3
};

typedef struct {
    remap_header_t *header;
    remap_entry_ctx_t *map_entries;
    remap_segment_ctx_t *segments;
    enum base_storage_type type;
    u64 base_storage_offset;
    duplex_storage_ctx_t *duplex;
    FIL *file;
} remap_storage_ctx_t;

typedef struct {
    u64 title_id;
    u8 user_id[0x10];
    u64 save_id;
    u8 save_data_type;
    u8 _0x21[0x1F];
    u64 save_owner_id;
    u64 timestamp;
    u64 _0x50;
    u64 data_size;
    u64 journal_size;
    u64 commit_id;
} extra_data_t;

NXDT_ASSERT(extra_data_t, 0x70);

typedef struct {
    u64 logical_offset;
    u64 hash_data_size;
    u32 block_size;
    u32 reserved;
} ivfc_level_hdr_t;

NXDT_ASSERT(ivfc_level_hdr_t, 0x18);

typedef struct {
    u32 magic;
    u32 id;
    u32 master_hash_size;
    u32 num_levels;
    ivfc_level_hdr_t level_headers[IVFC_MAX_LEVEL];
    u8 salt_source[0x20];
} ivfc_save_hdr_t;

NXDT_ASSERT(ivfc_save_hdr_t, 0xC0);

#pragma pack(push, 1)
typedef struct {
    u8 cmac[0x10];
    u8 _0x10[0xF0];
    fs_layout_t layout;
    duplex_header_t duplex_header;
    ivfc_save_hdr_t data_ivfc_header;
    u32 _0x404;
    journal_header_t journal_header;
    journal_map_header_t map_header;
    u8 _0x438[0x1D0];
    save_fs_header_t save_header;
    fat_header_t fat_header;
    remap_header_t main_remap_header, meta_remap_header;
    u64 _0x6D0;
    extra_data_t extra_data;
    u8 _0x748[0x390];
    ivfc_save_hdr_t fat_ivfc_header;
    u8 _0xB98[0x3468];
} save_header_t;
#pragma pack(pop)

NXDT_ASSERT(save_header_t, 0x4000);

typedef struct {
    duplex_storage_ctx_t layers[2];
    duplex_storage_ctx_t data_layer;
    u64 _length;
} hierarchical_duplex_storage_ctx_t;

typedef struct {
    u8 *data_a;
    u8 *data_b;
    duplex_info_t info;
} duplex_fs_layer_info_t;

typedef struct {
    u8 *map_storage;
    u8 *physical_block_bitmap;
    u8 *virtual_block_bitmap;
    u8 *free_block_bitmap;
} journal_map_params_t;

typedef struct {
    u32 physical_index;
    u32 virtual_index;
} journal_map_entry_t;

NXDT_ASSERT(journal_map_entry_t, 0x8);

typedef struct {
    journal_map_header_t *header;
    journal_map_entry_t *entries;
    u8 *map_storage;
} journal_map_ctx_t;

typedef struct {
    journal_map_ctx_t map;
    journal_header_t *header;
    u32 block_size;
    u64 journal_data_offset;
    u64 _length;
    FIL *file;
} journal_storage_ctx_t;

typedef struct {
    u64 data_offset;
    u64 data_size;
    u64 hash_offset;
    u32 hash_block_size;
    validity_t hash_validity;
    enum base_storage_type type;
    save_ctx_t *save_ctx;
} ivfc_level_save_ctx_t;

typedef struct {
    ivfc_level_save_ctx_t *data;
    u32 block_size;
    u8 salt[0x20];
} integrity_verification_info_ctx_t;

typedef struct integrity_verification_storage_ctx_t integrity_verification_storage_ctx_t;

struct integrity_verification_storage_ctx_t {
    ivfc_level_save_ctx_t *hash_storage;
    ivfc_level_save_ctx_t *base_storage;
    validity_t *block_validities;
    u8 salt[0x20];
    u32 sector_size;
    u32 sector_count;
    u64 _length;
    integrity_verification_storage_ctx_t *next_level;
};

typedef struct {
    ivfc_level_save_ctx_t levels[5];
    ivfc_level_save_ctx_t *data_level;
    validity_t **level_validities;
    u64 _length;
    integrity_verification_storage_ctx_t integrity_storages[4];
} hierarchical_integrity_verification_storage_ctx_t;

typedef struct {
    u32 prev;
    u32 next;
} allocation_table_entry_t;

typedef struct {
    u32 free_list_entry_index;
    void *base_storage;
    fat_header_t *header;
} allocation_table_ctx_t;

typedef struct {
    hierarchical_integrity_verification_storage_ctx_t *base_storage;
    u32 block_size;
    u32 initial_block;
    allocation_table_ctx_t *fat;
    u64 _length;
} allocation_table_storage_ctx_t;

typedef struct {
    allocation_table_ctx_t *fat;
    u32 virtual_block;
    u32 physical_block;
    u32 current_segment_size;
    u32 next_block;
    u32 prev_block;
} allocation_table_iterator_ctx_t;

typedef struct {
    char name[SAVE_FS_LIST_MAX_NAME_LENGTH];
    u32 parent;
} save_entry_key_t;

#pragma pack(push, 1)
typedef struct {
    u32 start_block;
    u64 length;
    u32 _0xC[2];
} save_file_info_t;
#pragma pack(pop)

NXDT_ASSERT(save_file_info_t, 0x14);

#pragma pack(push, 1)
typedef struct {
    u32 next_directory;
    u32 next_file;
    u32 _0x8[3];
} save_find_position_t;
#pragma pack(pop)

NXDT_ASSERT(save_find_position_t, 0x14);

#pragma pack(push, 1)
typedef struct {
    u32 next_sibling;
    union { /* Save table entry type. Size = 0x14. */
        save_file_info_t save_file_info;
        save_find_position_t save_find_position;
    };
} save_table_entry_t;
#pragma pack(pop)

NXDT_ASSERT(save_table_entry_t, 0x18);

#pragma pack(push, 1)
typedef struct {
    u32 parent;
    char name[SAVE_FS_LIST_MAX_NAME_LENGTH];
    save_table_entry_t value;
    u32 next;
} save_fs_list_entry_t;
#pragma pack(pop)

NXDT_ASSERT(save_fs_list_entry_t, 0x60);

typedef struct {
    u32 free_list_head_index;
    u32 used_list_head_index;
    allocation_table_storage_ctx_t storage;
    u32 capacity;
} save_filesystem_list_ctx_t;

typedef struct {
    save_filesystem_list_ctx_t file_table;
    save_filesystem_list_ctx_t directory_table;
} hierarchical_save_file_table_ctx_t;

typedef struct {
    hierarchical_integrity_verification_storage_ctx_t *base_storage;
    allocation_table_ctx_t allocation_table;
    save_fs_header_t *header;
    hierarchical_save_file_table_ctx_t file_table;
} save_filesystem_ctx_t;

struct save_ctx_t {
    save_header_t header;
    FIL *file;
    struct {
        FIL *file;
        u32 action;
    } tool_ctx;
    validity_t header_cmac_validity;
    validity_t header_hash_validity;
    u8 *data_ivfc_master;
    u8 *fat_ivfc_master;
    remap_storage_ctx_t data_remap_storage;
    remap_storage_ctx_t meta_remap_storage;
    duplex_fs_layer_info_t duplex_layers[3];
    hierarchical_duplex_storage_ctx_t duplex_storage;
    journal_storage_ctx_t journal_storage;
    journal_map_params_t journal_map_info;
    hierarchical_integrity_verification_storage_ctx_t core_data_ivfc_storage;
    hierarchical_integrity_verification_storage_ctx_t fat_ivfc_storage;
    u8 *fat_storage;
    save_filesystem_ctx_t save_filesystem_core;
    u8 save_mac_key[0x10];
};

static inline u32 allocation_table_entry_index_to_block(u32 entry_index)
{
    return (entry_index - 1);
}

static inline u32 allocation_table_block_to_entry_index(u32 block_index)
{
    return (block_index + 1);
}

static inline int allocation_table_is_list_end(allocation_table_entry_t *entry)
{
    return ((entry->next & 0x7FFFFFFF) == 0);
}

static inline int allocation_table_is_list_start(allocation_table_entry_t *entry)
{
    return (entry->prev == 0x80000000);
}

static inline int allocation_table_get_next(allocation_table_entry_t *entry)
{
    return (entry->next & 0x7FFFFFFF);
}

static inline int allocation_table_get_prev(allocation_table_entry_t *entry)
{
    return (entry->prev & 0x7FFFFFFF);
}

static inline allocation_table_entry_t *save_allocation_table_read_entry(allocation_table_ctx_t *ctx, u32 entry_index)
{
    return ((allocation_table_entry_t*)((u8*)ctx->base_storage + (entry_index * SAVE_FAT_ENTRY_SIZE)));
}

static inline u32 save_allocation_table_get_free_list_entry_index(allocation_table_ctx_t *ctx)
{
    return allocation_table_get_next(save_allocation_table_read_entry(ctx, ctx->free_list_entry_index));
}

static inline u32 save_allocation_table_get_free_list_block_index(allocation_table_ctx_t *ctx)
{
    return allocation_table_entry_index_to_block(save_allocation_table_get_free_list_entry_index(ctx));
}

bool save_process(save_ctx_t *ctx);
bool save_process_header(save_ctx_t *ctx);
void save_free_contexts(save_ctx_t *ctx);

bool save_open_fat_storage(save_filesystem_ctx_t *ctx, allocation_table_storage_ctx_t *storage_ctx, u32 block_index);
u32 save_allocation_table_storage_read(allocation_table_storage_ctx_t *ctx, void *buffer, u64 offset, size_t count);
bool save_fs_list_get_value(save_filesystem_list_ctx_t *ctx, u32 index, save_fs_list_entry_t *value);
u32 save_fs_list_get_index_from_key(save_filesystem_list_ctx_t *ctx, save_entry_key_t *key, u32 *prev_index);
bool save_hierarchical_file_table_find_path_recursive(hierarchical_save_file_table_ctx_t *ctx, save_entry_key_t *key, const char *path);
bool save_hierarchical_file_table_get_file_entry_by_path(hierarchical_save_file_table_ctx_t *ctx, const char *path, save_fs_list_entry_t *entry);

save_ctx_t *save_open_savefile(const char *path, u32 action);
void save_close_savefile(save_ctx_t *ctx);
bool save_get_fat_storage_from_file_entry_by_path(save_ctx_t *ctx, const char *path, allocation_table_storage_ctx_t *out_fat_storage, u64 *out_file_entry_size);

#ifdef __cplusplus
}
#endif

#endif /* __SAVE_H__ */
