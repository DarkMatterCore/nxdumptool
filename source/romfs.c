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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "romfs.h"
#include "utils.h"

/* Function prototypes. */

static RomFileSystemDirectoryEntry *romfsGetChildDirectoryEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name);
static RomFileSystemFileEntry *romfsGetChildFileEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name);

bool romfsInitializeContext(RomFileSystemContext *out, NcaFsSectionContext *nca_fs_ctx)
{
    NcaContext *nca_ctx = NULL;
    u64 dir_table_offset = 0, file_table_offset = 0;
    
    if (!out || !nca_fs_ctx || nca_fs_ctx->section_type != NcaFsSectionType_RomFs || !nca_fs_ctx->header || !(nca_ctx = (NcaContext*)nca_fs_ctx->nca_ctx) || \
        (nca_ctx->format_version == NcaVersion_Nca0 && (nca_fs_ctx->section_type != NcaFsSectionType_Nca0RomFs || nca_fs_ctx->header->hash_type != NcaHashType_HierarchicalSha256)) || \
        (nca_ctx->format_version != NcaVersion_Nca0 && (nca_fs_ctx->section_type != NcaFsSectionType_RomFs || nca_fs_ctx->header->hash_type != NcaHashType_HierarchicalIntegrity)))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Fill context */
    out->nca_fs_ctx = nca_fs_ctx;
    out->offset = 0;
    out->size = 0;
    out->dir_table_size = 0;
    out->dir_table = NULL;
    out->file_table_size = 0;
    out->file_table = NULL;
    out->body_offset = 0;
    
    if (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs)
    {
        out->sha256_hash_info = &(nca_fs_ctx->header->hash_info.hierarchical_sha256);
        out->integrity_hash_info = NULL;
        
        if (!ncaValidateHierarchicalSha256Offsets(out->sha256_hash_info, nca_fs_ctx->section_size))
        {
            LOGFILE("Invalid HierarchicalSha256 block!");
            return false;
        }
        
        out->offset = out->sha256_hash_info->hash_target_layer_info.offset;
        out->size = out->sha256_hash_info->hash_target_layer_info.size;
    } else {
        out->sha256_hash_info = NULL;
        out->integrity_hash_info = &(nca_fs_ctx->header->hash_info.hierarchical_integrity);
        
        if (!ncaValidateHierarchicalIntegrityOffsets(out->integrity_hash_info, nca_fs_ctx->section_size))
        {
            LOGFILE("Invalid HierarchicalIntegrity block!");
            return false;
        }
        
        out->offset = out->integrity_hash_info->hash_target_layer_info.offset;
        out->size = out->integrity_hash_info->hash_target_layer_info.size;
    }
    
    /* Read RomFS header */
    if (!ncaReadFsSection(nca_fs_ctx, &(out->header), sizeof(RomFileSystemHeader), out->offset))
    {
        LOGFILE("Failed to read RomFS header!");
        return false;
    }
    
    if ((nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs && out->header.old_format.header_size != ROMFS_OLD_HEADER_SIZE) || \
        (nca_fs_ctx->section_type == NcaFsSectionType_RomFs && out->header.cur_format.header_size != ROMFS_HEADER_SIZE))
    {
        LOGFILE("Invalid RomFS header size!");
        return false;
    }
    
    /* Read directory entries table */
    dir_table_offset = (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.directory_entry_offset : out->header.cur_format.directory_entry_offset);
    out->dir_table_size = (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.directory_entry_size : out->header.cur_format.directory_entry_size);
    
    if (dir_table_offset >= out->size || !out->dir_table_size || (dir_table_offset + out->dir_table_size) > out->size)
    {
        LOGFILE("Invalid RomFS directory entries table!");
        return false;
    }
    
    out->dir_table = malloc(out->dir_table_size);
    if (!out->dir_table)
    {
        LOGFILE("Unable to allocate memory for RomFS directory entries table!");
        return false;
    }
    
    if (!ncaReadFsSection(nca_fs_ctx, out->dir_table, out->dir_table_size, out->offset + dir_table_offset))
    {
        LOGFILE("Failed to read RomFS directory entries table!");
        return false;
    }
    
    /* Read file entries table */
    file_table_offset = (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.file_entry_offset : out->header.cur_format.file_entry_offset);
    out->file_table_size = (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.file_entry_size : out->header.cur_format.file_entry_size);
    
    if (file_table_offset >= out->size || !out->file_table_size || (file_table_offset + out->file_table_size) > out->size)
    {
        LOGFILE("Invalid RomFS file entries table!");
        return false;
    }
    
    out->file_table = malloc(out->file_table_size);
    if (!out->file_table)
    {
        LOGFILE("Unable to allocate memory for RomFS file entries table!");
        return false;
    }
    
    if (!ncaReadFsSection(nca_fs_ctx, out->file_table, out->file_table_size, out->offset + file_table_offset))
    {
        LOGFILE("Failed to read RomFS file entries table!");
        return false;
    }
    
    /* Get file data body offset */
    out->body_offset = (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.body_offset : out->header.cur_format.body_offset);
    if (out->body_offset >= out->size)
    {
        LOGFILE("Invalid RomFS file data body!");
        return false;
    }
    
    return true;
}

bool romfsReadFileSystemData(RomFileSystemContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->nca_fs_ctx || !ctx->size || !out || !read_size || offset >= ctx->size || (offset + read_size) > ctx->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Read filesystem data */
    if (!ncaReadFsSection(ctx->nca_fs_ctx, out, read_size, ctx->offset + offset))
    {
        LOGFILE("Failed to read RomFS data!");
        return false;
    }
    
    return true;
}

bool romfsReadFileEntryData(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->body_offset || !file_entry || !file_entry->size || file_entry->offset >= ctx->size || (file_entry->offset + file_entry->size) > ctx->size || \
        !out || !read_size || offset >= file_entry->size || (offset + read_size) > file_entry->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Read entry data */
    if (!romfsReadFileSystemData(ctx, out, read_size, ctx->body_offset + file_entry->offset + offset))
    {
        LOGFILE("Failed to read RomFS file entry data!");
        return false;
    }
    
    return true;
}

bool romfsGetTotalDataSize(RomFileSystemContext *ctx, u64 *out_size)
{
    if (!ctx || !ctx->file_table_size || !ctx->file_table || !out_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u64 offset = 0, total_size = 0;
    RomFileSystemFileEntry *file_entry = NULL;
    
    while(offset < ctx->file_table_size)
    {
        if (!(file_entry = romfsGetFileEntryByOffset(ctx, offset)))
        {
            LOGFILE("Failed to retrieve file entry!");
            return false;
        }
        
        total_size += file_entry->size;
        offset += ALIGN_UP(sizeof(RomFileSystemFileEntry) + file_entry->name_length, 4);
    }
    
    *out_size = total_size;
    
    return true;
}

bool romfsGetDirectoryDataSize(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, u64 *out_size)
{
    if (!ctx || !ctx->dir_table_size || !ctx->dir_table || !ctx->file_table_size || !ctx->file_table || !dir_entry || (dir_entry->file_offset == ROMFS_VOID_ENTRY && \
        dir_entry->directory_offset == ROMFS_VOID_ENTRY) || !out_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u64 total_size = 0, child_dir_size = 0;
    u32 cur_file_offset = 0, cur_dir_offset = 0;
    RomFileSystemFileEntry *cur_file_entry = NULL;
    RomFileSystemDirectoryEntry *cur_dir_entry = NULL;
    
    cur_file_offset = dir_entry->file_offset;
    while(cur_file_offset != ROMFS_VOID_ENTRY)
    {
        if (!(cur_file_entry = romfsGetFileEntryByOffset(ctx, cur_file_offset)))
        {
            LOGFILE("Failed to retrieve file entry!");
            return false;
        }
        
        total_size += cur_file_entry->size;
        cur_file_offset = cur_file_entry->next_offset;
    }
    
    cur_dir_offset = dir_entry->directory_offset;
    while(cur_dir_offset != ROMFS_VOID_ENTRY)
    {
        if (!(cur_dir_entry = romfsGetDirectoryEntryByOffset(ctx, cur_dir_offset)) || !romfsGetDirectoryDataSize(ctx, cur_dir_entry, &child_dir_size))
        {
            LOGFILE("Failed to retrieve directory entry/size!");
            return false;
        }
        
        total_size += child_dir_size;
        cur_dir_offset = cur_dir_entry->next_offset;
    }
    
    *out_size = total_size;
    
    return true;
}

RomFileSystemDirectoryEntry *romfsGetDirectoryEntryByPath(RomFileSystemContext *ctx, const char *path)
{
    size_t path_len = 0;
    char *path_dup = NULL, *pch = NULL;
    RomFileSystemDirectoryEntry *dir_entry = NULL;
    
    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !path || *path != '/' || !(path_len = strlen(path)) || !(dir_entry = romfsGetDirectoryEntryByOffset(ctx, 0)))
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    /* Check if the root directory was requested */
    if (path_len == 1) return dir_entry;
    
    /* Duplicate path to avoid problems with strtok() */
    if (!(path_dup = strdup(path)))
    {
        LOGFILE("Unable to duplicate input path!");
        return NULL;
    }
    
    pch = strtok(path_dup, "/");
    if (!pch)
    {
        LOGFILE("Failed to tokenize input path!");
        dir_entry = NULL;
        goto out;
    }
    
    while(pch)
    {
        if (!(dir_entry = romfsGetChildDirectoryEntryByName(ctx, dir_entry, pch)))
        {
            LOGFILE("Failed to retrieve directory entry by name!");
            break;
        }
        
        pch = strtok(NULL, "/");
    }
    
out:
    if (path_dup) free(path_dup);
    
    return dir_entry;
}

RomFileSystemFileEntry *romfsGetFileEntryByPath(RomFileSystemContext *ctx, const char *path)
{
    size_t path_len = 0;
    char *path_dup = NULL, *filename = NULL;
    RomFileSystemFileEntry *file_entry = NULL;
    RomFileSystemDirectoryEntry *dir_entry = NULL;
    
    if (!ctx || !ctx->file_table || !ctx->file_table_size || !path || *path != '/' || (path_len = strlen(path)) <= 1)
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    /* Duplicate path */
    if (!(path_dup = strdup(path)))
    {
        LOGFILE("Unable to duplicate input path!");
        return NULL;
    }
    
    /* Remove any trailing slashes */
    while(path_dup[path_len - 1] == '/')
    {
        path_dup[path_len - 1] = '\0';
        path_len--;
    }
    
    /* Safety check */
    if (!path_len || !(filename = strrchr(path_dup, '/')))
    {
        LOGFILE("Invalid input path!");
        goto out;
    }
    
    /* Remove leading slash and adjust filename string pointer */
    *filename++ = '\0';
    
    /* Retrieve directory entry */
    /* If the first character is NULL, then just retrieve the root directory entry */
    if (!(dir_entry = (*path_dup ? romfsGetDirectoryEntryByPath(ctx, path_dup) : romfsGetDirectoryEntryByOffset(ctx, 0))))
    {
        LOGFILE("Failed to retrieve directory entry!");
        goto out;
    }
    
    /* Retrieve file entry */
    if (!(file_entry = romfsGetChildFileEntryByName(ctx, dir_entry, filename))) LOGFILE("Failed to retrieve file entry by name!");
    
out:
    if (path_dup) free(path_dup);
    
    return file_entry;
}

bool romfsGeneratePathFromDirectoryEntry(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, char *out_path, size_t out_path_size)
{
    size_t path_len = 0;
    u32 dir_offset = ROMFS_VOID_ENTRY, dir_entries_count = 0;
    RomFileSystemDirectoryEntry **dir_entries = NULL, **tmp_dir_entries = NULL;
    bool success = false;
    
    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !dir_entry || (!dir_entry->name_length && dir_entry->parent_offset) || !out_path || out_path_size < 2)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Check if we're dealing with the root directory entry */
    if (!dir_entry->name_length)
    {
        sprintf(out_path, "/");
        return true;
    }
    
    /* Allocate memory for our directory entries */
    dir_entries = calloc(1, sizeof(RomFileSystemDirectoryEntry*));
    if (!dir_entries)
    {
        LOGFILE("Unable to allocate memory for directory entries!");
        return false;
    }
    
    path_len = (1 + dir_entry->name_length);
    *dir_entries = dir_entry;
    dir_entries_count++;
    
    while(true)
    {
        dir_offset = dir_entries[dir_entries_count - 1]->parent_offset;
        if (!dir_offset) break;
        
        /* Reallocate directory entries */
        if (!(tmp_dir_entries = realloc(dir_entries, (dir_entries_count + 1) * sizeof(RomFileSystemDirectoryEntry*))))
        {
            LOGFILE("Unable to reallocate directory entries buffer!");
            goto out;
        }
        
        dir_entries = tmp_dir_entries;
        tmp_dir_entries = NULL;
        
        if (!(dir_entries[dir_entries_count] = romfsGetDirectoryEntryByOffset(ctx, dir_offset)) || !dir_entries[dir_entries_count]->name_length)
        {
            LOGFILE("Failed to retrieve directory entry!");
            goto out;
        }
        
        path_len += (1 + dir_entries[dir_entries_count]->name_length);
        dir_entries_count++;
    }
    
    if (path_len >= out_path_size)
    {
        LOGFILE("Output path length exceeds output buffer size!");
        goto out;
    }
    
    /* Generate output path */
    *out_path = '\0';
    for(u32 i = dir_entries_count; i > 0; i--)
    {
        strcat(out_path, "/");
        strncat(out_path, dir_entries[i - 1]->name, dir_entries[i - 1]->name_length);
    }
    
    success = true;
    
out:
    if (dir_entries) free(dir_entries);
    
    return success;
}

bool romfsGeneratePathFromFileEntry(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, char *out_path, size_t out_path_size)
{
    size_t path_len = 0;
    RomFileSystemDirectoryEntry *dir_entry = NULL;
    
    if (!ctx || !ctx->file_table || !ctx->file_table_size || !file_entry || !file_entry->name_length || !out_path || out_path_size < 2 || \
        !(dir_entry = romfsGetDirectoryEntryByOffset(ctx, file_entry->parent_offset)))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Retrieve full RomFS path up to the file entry name */
    if (!romfsGeneratePathFromDirectoryEntry(ctx, dir_entry, out_path, out_path_size))
    {
        LOGFILE("Failed to retrieve RomFS directory path!");
        return false;
    }
    
    /* Check path length */
    path_len = strlen(out_path);
    if ((1 + file_entry->name_length) >= (out_path_size - path_len))
    {
        LOGFILE("Output path length exceeds output buffer size!");
        return false;
    }
    
    /* Concatenate file entry name */
    strcat(out_path, "/");
    strncat(out_path, file_entry->name, file_entry->name_length);
    
    return true;
}













static RomFileSystemDirectoryEntry *romfsGetChildDirectoryEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name)
{
    u64 dir_offset = 0;
    size_t name_len = 0;
    RomFileSystemDirectoryEntry *child_dir_entry = NULL;
    
    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !dir_entry || (dir_offset = dir_entry->directory_offset) == ROMFS_VOID_ENTRY || !name || !(name_len = strlen(name))) return NULL;
    
    while(dir_offset != ROMFS_VOID_ENTRY)
    {
        if (!(child_dir_entry = romfsGetDirectoryEntryByOffset(ctx, dir_offset)) || !child_dir_entry->name_length) return NULL;
        if (!strncmp(child_dir_entry->name, name, name_len)) return child_dir_entry;
        dir_offset = child_dir_entry->next_offset;
    }
    
    return NULL;
}

static RomFileSystemFileEntry *romfsGetChildFileEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name)
{
    u64 file_offset = 0;
    size_t name_len = 0;
    RomFileSystemFileEntry *child_file_entry = NULL;
    
    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !ctx->file_table || !ctx->file_table_size || !dir_entry || (file_offset = dir_entry->file_offset) == ROMFS_VOID_ENTRY || !name || \
        !(name_len = strlen(name))) return NULL;
    
    while(file_offset != ROMFS_VOID_ENTRY)
    {
        if (!(child_file_entry = romfsGetFileEntryByOffset(ctx, file_offset)) || !child_file_entry->name_length) return NULL;
        if (!strncmp(child_file_entry->name, name, name_len)) return child_file_entry;
        file_offset = child_file_entry->next_offset;
    }
    
    return NULL;
}
