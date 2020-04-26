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
    dir_table_offset = (out->offset + (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.directory_entry_offset : out->header.cur_format.directory_entry_offset));
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
    
    if (!ncaReadFsSection(nca_fs_ctx, out->dir_table, out->dir_table_size, dir_table_offset))
    {
        LOGFILE("Failed to read RomFS directory entries table!");
        return false;
    }
    
    /* Read file entries table */
    file_table_offset = (out->offset + (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.file_entry_offset : out->header.cur_format.file_entry_offset));
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
    
    if (!ncaReadFsSection(nca_fs_ctx, out->file_table, out->file_table_size, file_table_offset))
    {
        LOGFILE("Failed to read RomFS file entries table!");
        return false;
    }
    
    /* Calculate file data body offset */
    out->body_offset = (out->offset + (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? (u64)out->header.old_format.body_offset : out->header.cur_format.body_offset));
    if (out->body_offset >= out->size)
    {
        LOGFILE("Invalid RomFS file data body!");
        return false;
    }
    
    return true;
}

bool romfsReadFileEntryData(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->nca_fs_ctx || !ctx->size || !ctx->body_offset || !file_entry || !file_entry->size || file_entry->offset >= ctx->size || (file_entry->offset + file_entry->size) > ctx->size || \
        !out || !read_size || offset >= file_entry->size || (offset + read_size) > file_entry->size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Calculate offset relative to the start of the NCA FS section */
    u64 section_offset = (ctx->body_offset + file_entry->offset + offset);
    
    /* Read entry data */
    if (!ncaReadFsSection(ctx->nca_fs_ctx, out, read_size, section_offset))
    {
        LOGFILE("Failed to read RomFS file entry data!");
        return false;
    }
    
    return true;
}

bool romfsGetTotalDataSize(RomFileSystemContext *ctx, u64 *out_size)
{
    if (!ctx || !ctx->file_table_size || !ctx->file_table || !out_size) return false;
    
    u64 offset = 0, total_size = 0;
    RomFileSystemFileEntry *file_entry = NULL;
    
    while(offset < ctx->file_table_size)
    {
        if (!(file_entry = romfsGetFileEntry(ctx, offset))) return false;
        total_size += file_entry->size;
        offset += ALIGN_UP(sizeof(RomFileSystemFileEntry) + file_entry->name_length, 4);
    }
    
    *out_size = total_size;
    return true;
}

bool romfsGetDirectoryDataSize(RomFileSystemContext *ctx, u32 dir_entry_offset, u64 *out_size)
{
    u64 total_size = 0, child_dir_size = 0;
    RomFileSystemDirectoryEntry *dir_entry = NULL;
    RomFileSystemFileEntry *file_entry = NULL;
    
    if (!ctx || !ctx->dir_table_size || !ctx->dir_table || !ctx->file_table_size || !ctx->file_table || !out_size || !(dir_entry = romfsGetDirectoryEntry(ctx, dir_entry_offset)) || \
        (!dir_entry->name_length && dir_entry_offset > 0)) return false;
    
    if (dir_entry->file_offset != ROMFS_VOID_ENTRY)
    {
        if (!(file_entry = romfsGetFileEntry(ctx, dir_entry->file_offset))) return false;
        total_size += file_entry->size;
        
        while(file_entry->next_offset != ROMFS_VOID_ENTRY)
        {
            if (!(file_entry = romfsGetFileEntry(ctx, file_entry->next_offset))) return false;
            total_size += file_entry->size;
        }
    }
    
    if (dir_entry->directory_offset != ROMFS_VOID_ENTRY)
    {
        if (!romfsGetDirectoryDataSize(ctx, dir_entry->directory_offset, &child_dir_size)) return false;
        total_size += child_dir_size;
        
        while(dir_entry->next_offset != ROMFS_VOID_ENTRY)
        {
            if (!romfsGetDirectoryDataSize(ctx, dir_entry->next_offset, &child_dir_size)) return false;
            total_size += child_dir_size;
            if (!(dir_entry = romfsGetDirectoryEntry(ctx, dir_entry->next_offset))) return false;
        }
    }
    
    *out_size = total_size;
    
    return true;
}
