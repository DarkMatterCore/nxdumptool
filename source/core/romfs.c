/*
 * romfs.c
 *
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
#include "romfs.h"

/* Function prototypes. */

static RomFileSystemDirectoryEntry *romfsGetChildDirectoryEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name);
static RomFileSystemFileEntry *romfsGetChildFileEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name);

bool romfsInitializeContext(RomFileSystemContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *patch_nca_fs_ctx)
{
    u64 dir_table_offset = 0, file_table_offset = 0;
    NcaContext *base_nca_ctx = NULL, *patch_nca_ctx = NULL;
    bool dump_fs_header = false, success = false;

    /* Check if the base RomFS is missing (e.g. Fortnite, World of Tanks Blitz, etc.). */
    bool missing_base_romfs = (base_nca_fs_ctx && (!base_nca_fs_ctx->enabled || (base_nca_fs_ctx->section_type != NcaFsSectionType_RomFs && \
                               base_nca_fs_ctx->section_type != NcaFsSectionType_Nca0RomFs)));

    if (!out || !base_nca_fs_ctx || (!patch_nca_fs_ctx && (missing_base_romfs || base_nca_fs_ctx->has_sparse_layer)) || \
        (!missing_base_romfs && (!(base_nca_ctx = base_nca_fs_ctx->nca_ctx) || (base_nca_ctx->format_version == NcaVersion_Nca0 && \
        (base_nca_fs_ctx->section_type != NcaFsSectionType_Nca0RomFs || base_nca_fs_ctx->hash_type != NcaHashType_HierarchicalSha256)) || \
        (base_nca_ctx->format_version != NcaVersion_Nca0 && (base_nca_fs_ctx->section_type != NcaFsSectionType_RomFs || \
        (base_nca_fs_ctx->hash_type != NcaHashType_HierarchicalIntegrity && base_nca_fs_ctx->hash_type != NcaHashType_HierarchicalIntegritySha3))) || \
        (base_nca_ctx->rights_id_available && !base_nca_ctx->titlekey_retrieved))) || (patch_nca_fs_ctx && (!patch_nca_fs_ctx->enabled || \
        !(patch_nca_ctx = patch_nca_fs_ctx->nca_ctx) || (!missing_base_romfs && patch_nca_ctx->format_version != base_nca_ctx->format_version) || \
        patch_nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || (patch_nca_ctx->rights_id_available && !patch_nca_ctx->titlekey_retrieved))))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Free output context beforehand. */
    romfsFreeContext(out);

    NcaStorageContext *base_storage_ctx = &(out->storage_ctx[0]), *patch_storage_ctx = &(out->storage_ctx[1]);
    bool is_nca0_romfs = (base_nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs);

    /* Initialize base NCA storage context. */
    if (!missing_base_romfs && !ncaStorageInitializeContext(base_storage_ctx, base_nca_fs_ctx))
    {
        LOG_MSG_ERROR("Failed to initialize base NCA storage context!");
        goto end;
    }

    if (patch_nca_fs_ctx)
    {
        /* Initialize base NCA storage context. */
        if (!ncaStorageInitializeContext(patch_storage_ctx, patch_nca_fs_ctx))
        {
            LOG_MSG_ERROR("Failed to initialize patch NCA storage context!");
            goto end;
        }

        /* Set patch NCA storage original substorage, if available. */
        if (!missing_base_romfs && !ncaStorageSetPatchOriginalSubStorage(patch_storage_ctx, base_storage_ctx))
        {
            LOG_MSG_ERROR("Failed to set patch NCA storage context's original substorage!");
            goto end;
        }

        /* Set default NCA FS storage context. */
        out->is_patch = true;
        out->default_storage_ctx = patch_storage_ctx;
    } else {
        /* Set default NCA FS storage context. */
        out->is_patch = false;
        out->default_storage_ctx = base_storage_ctx;
    }

    /* Get RomFS offset and size. */
    if (!ncaStorageGetHashTargetExtents(out->default_storage_ctx, &(out->offset), &(out->size)))
    {
        LOG_MSG_ERROR("Failed to get target hash layer extents!");
        goto end;
    }

    /* Read RomFS header. */
    if (!ncaStorageRead(out->default_storage_ctx, &(out->header), sizeof(RomFileSystemHeader), out->offset))
    {
        LOG_MSG_ERROR("Failed to read RomFS header!");
        goto end;
    }

    if ((is_nca0_romfs && out->header.old_format.header_size != ROMFS_OLD_HEADER_SIZE) || (!is_nca0_romfs && out->header.cur_format.header_size != ROMFS_HEADER_SIZE))
    {
        LOG_MSG_ERROR("Invalid RomFS header size!");
        dump_fs_header = true;
        goto end;
    }

    /* Read directory entries table. */
    dir_table_offset = (is_nca0_romfs ? (u64)out->header.old_format.directory_entry_offset : out->header.cur_format.directory_entry_offset);
    out->dir_table_size = (is_nca0_romfs ? (u64)out->header.old_format.directory_entry_size : out->header.cur_format.directory_entry_size);

    if (!out->dir_table_size || (dir_table_offset + out->dir_table_size) > out->size)
    {
        LOG_MSG_ERROR("Invalid RomFS directory entries table!");
        dump_fs_header = true;
        goto end;
    }

    out->dir_table = malloc(out->dir_table_size);
    if (!out->dir_table)
    {
        LOG_MSG_ERROR("Unable to allocate memory for RomFS directory entries table!");
        goto end;
    }

    if (!ncaStorageRead(out->default_storage_ctx, out->dir_table, out->dir_table_size, out->offset + dir_table_offset))
    {
        LOG_MSG_ERROR("Failed to read RomFS directory entries table!");
        goto end;
    }

    /* Read file entries table. */
    file_table_offset = (is_nca0_romfs ? (u64)out->header.old_format.file_entry_offset : out->header.cur_format.file_entry_offset);
    out->file_table_size = (is_nca0_romfs ? (u64)out->header.old_format.file_entry_size : out->header.cur_format.file_entry_size);

    if (!out->file_table_size || (file_table_offset + out->file_table_size) > out->size)
    {
        LOG_MSG_ERROR("Invalid RomFS file entries table!");
        dump_fs_header = true;
        goto end;
    }

    out->file_table = malloc(out->file_table_size);
    if (!out->file_table)
    {
        LOG_MSG_ERROR("Unable to allocate memory for RomFS file entries table!");
        goto end;
    }

    if (!ncaStorageRead(out->default_storage_ctx, out->file_table, out->file_table_size, out->offset + file_table_offset))
    {
        LOG_MSG_ERROR("Failed to read RomFS file entries table!");
        goto end;
    }

    /* Get file data body offset. */
    out->body_offset = (is_nca0_romfs ? (u64)out->header.old_format.body_offset : out->header.cur_format.body_offset);
    if (out->body_offset >= out->size)
    {
        LOG_MSG_ERROR("Invalid RomFS file data body!");
        dump_fs_header = true;
        goto end;
    }

    /* Update flag. */
    success = true;

end:
    if (!success)
    {
        if (dump_fs_header) LOG_DATA_DEBUG(&(out->header), sizeof(RomFileSystemHeader), "RomFS header dump:");

        romfsFreeContext(out);
    }

    return success;
}

bool romfsReadFileSystemData(RomFileSystemContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!romfsIsValidContext(ctx) || !out || !read_size || (offset + read_size) > ctx->size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Read filesystem data. */
    if (!ncaStorageRead(ctx->default_storage_ctx, out, read_size, ctx->offset + offset))
    {
        LOG_MSG_ERROR("Failed to read RomFS data!");
        return false;
    }

    return true;
}

bool romfsReadFileEntryData(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset)
{
    if (!romfsIsValidContext(ctx) || !file_entry || !file_entry->size || (file_entry->offset + file_entry->size) > ctx->size || !out || !read_size || \
        (offset + read_size) > file_entry->size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Read entry data. */
    if (!romfsReadFileSystemData(ctx, out, read_size, ctx->body_offset + file_entry->offset + offset))
    {
        LOG_MSG_ERROR("Failed to read RomFS file entry data!");
        return false;
    }

    return true;
}

bool romfsGetTotalDataSize(RomFileSystemContext *ctx, bool only_updated, u64 *out_size)
{
    if (!romfsIsValidContext(ctx) || !out_size || (only_updated && (!ctx->is_patch || ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    RomFileSystemFileEntry *file_entry = NULL;
    u64 total_size = 0;
    bool success = false;

    /* Reset current file table offset. */
    romfsResetFileTableOffset(ctx);

    /* Loop through all file entries. */
    while(romfsCanMoveToNextFileEntry(ctx))
    {
        bool updated = false;

        /* Get current file entry. */
        if (!(file_entry = romfsGetCurrentFileEntry(ctx)))
        {
            LOG_MSG_ERROR("Failed to retrieve current file entry! (0x%lX, 0x%lX).", ctx->cur_file_offset, ctx->file_table_size);
            goto end;
        }

        /* Update total data size, taking into account the only_updated flag. */
        if (only_updated && !romfsIsFileEntryUpdated(ctx, file_entry, &updated))
        {
            LOG_MSG_ERROR("Failed to determine if file entry is updated or not! (0x%lX, 0x%lX).", ctx->cur_file_offset, ctx->file_table_size);
            goto end;
        }

        if (!only_updated || (only_updated && updated)) total_size += file_entry->size;

        /* Move to the next file entry. */
        if (!romfsMoveToNextFileEntry(ctx))
        {
            LOG_MSG_ERROR("Failed to move to the next file entry! (0x%lX, 0x%lX).", ctx->cur_file_offset, ctx->file_table_size);
            goto end;
        }
    }

    /* Update output values. */
    *out_size = total_size;
    success = true;

end:
    /* Reset current file table offset. */
    romfsResetFileTableOffset(ctx);

    return success;
}

bool romfsGetDirectoryDataSize(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, u64 *out_size)
{
    if (!romfsIsValidContext(ctx) || !dir_entry || !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Short-circuit: check if we're dealing with an empty directory. */
    if (dir_entry->file_offset == ROMFS_VOID_ENTRY && dir_entry->directory_offset == ROMFS_VOID_ENTRY)
    {
        *out_size = 0;
        return true;
    }

    RomFileSystemFileEntry *cur_file_entry = NULL;
    RomFileSystemDirectoryEntry *cur_dir_entry = NULL;
    u64 total_size = 0, cur_entry_offset = 0, child_dir_size = 0;
    bool success = false;

    /* Loop through the child file entries' linked list. */
    cur_entry_offset = dir_entry->file_offset;
    while(cur_entry_offset != ROMFS_VOID_ENTRY)
    {
        /* Get current file entry. */
        if (!(cur_file_entry = romfsGetFileEntryByOffset(ctx, cur_entry_offset)))
        {
            LOG_MSG_ERROR("Failed to retrieve file entry! (0x%lX, 0x%lX).", cur_entry_offset, ctx->file_table_size);
            goto end;
        }

        /* Update total data size. */
        total_size += cur_file_entry->size;

        /* Update current file entry offset. */
        cur_entry_offset = cur_file_entry->next_offset;
    }

    /* Loop through the child directory entries' linked list. */
    cur_entry_offset = dir_entry->directory_offset;
    while(cur_entry_offset != ROMFS_VOID_ENTRY)
    {
        /* Get current directory entry. */
        if (!(cur_dir_entry = romfsGetDirectoryEntryByOffset(ctx, cur_entry_offset)))
        {
            LOG_MSG_ERROR("Failed to retrieve directory entry! (0x%lX, 0x%lX).", cur_entry_offset, ctx->dir_table_size);
            goto end;
        }

        /* Calculate directory size. */
        if (!romfsGetDirectoryDataSize(ctx, cur_dir_entry, &child_dir_size))
        {
            LOG_MSG_ERROR("Failed to get size for directory entry! (0x%lX, 0x%lX).", cur_entry_offset, ctx->dir_table_size);
            goto end;
        }

        /* Update total data size. */
        total_size += child_dir_size;

        /* Update current directory entry offset. */
        cur_entry_offset = cur_dir_entry->next_offset;
    }

    /* Update output values. */
    *out_size = total_size;
    success = true;

end:
    return success;
}

RomFileSystemDirectoryEntry *romfsGetDirectoryEntryByPath(RomFileSystemContext *ctx, const char *path)
{
    size_t path_len = 0;
    char *path_dup = NULL, *pch = NULL, *state = NULL;
    RomFileSystemDirectoryEntry *dir_entry = NULL;

    if (!romfsIsValidContext(ctx) || !path || *path != '/' || !(dir_entry = romfsGetDirectoryEntryByOffset(ctx, 0)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    /* Retrieve path length. */
    path_len = strlen(path);

    /* Short-circuit: check if the root directory was requested. */
    if (path_len == 1) return dir_entry;

    /* Duplicate path to avoid problems with strtok_r(). */
    if (!(path_dup = strdup(path)))
    {
        LOG_MSG_ERROR("Unable to duplicate input path! (\"%s\").", path);
        dir_entry = NULL;
        goto end;
    }

    /* Tokenize duplicated path using path separators. */
    pch = strtok_r(path_dup, "/", &state);
    if (!pch)
    {
        LOG_MSG_ERROR("Failed to tokenize input path! (\"%s\").", path);
        dir_entry = NULL;
        goto end;
    }

    /* Loop through all path elements. */
    while(pch)
    {
        /* Get child directory entry using the current token. */
        if (!(dir_entry = romfsGetChildDirectoryEntryByName(ctx, dir_entry, pch)))
        {
            LOG_MSG_ERROR("Failed to retrieve directory entry by name for \"%s\"! (\"%s\").", pch, path);
            break;
        }

        /* Move onto the next token. */
        pch = strtok_r(NULL, "/", &state);
    }

end:
    if (path_dup) free(path_dup);

    return dir_entry;
}

RomFileSystemFileEntry *romfsGetFileEntryByPath(RomFileSystemContext *ctx, const char *path)
{
    size_t path_len = 0;
    char *path_dup = NULL, *filename = NULL;
    RomFileSystemFileEntry *file_entry = NULL;
    RomFileSystemDirectoryEntry *dir_entry = NULL;

    if (!romfsIsValidContext(ctx) || !path || *path != '/')
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    /* Retrieve path length. */
    path_len = strlen(path);

    /* Duplicate path. */
    if (!(path_dup = strdup(path)))
    {
        LOG_MSG_ERROR("Unable to duplicate input path! (\"%s\").", path);
        goto end;
    }

    /* Remove any trailing slashes. */
    while(path_dup[path_len - 1] == '/')
    {
        path_dup[path_len - 1] = '\0';
        path_len--;
    }

    /* Safety check. */
    if (!path_len || !(filename = strrchr(path_dup, '/')))
    {
        LOG_MSG_ERROR("Invalid input path! (\"%s\").", path);
        goto end;
    }

    /* Remove leading slash and adjust filename string pointer. */
    *filename++ = '\0';

    /* Retrieve directory entry. */
    /* If the first character is NULL, then just retrieve the root directory entry. */
    if (!(dir_entry = (*path_dup ? romfsGetDirectoryEntryByPath(ctx, path_dup) : romfsGetDirectoryEntryByOffset(ctx, 0))))
    {
        LOG_MSG_ERROR("Failed to retrieve directory entry for \"%s\"! (\"%s\").", *path_dup ? path_dup : "/", path);
        goto end;
    }

    /* Retrieve file entry. */
    if (!(file_entry = romfsGetChildFileEntryByName(ctx, dir_entry, filename))) LOG_MSG_ERROR("Failed to retrieve file entry by name for \"%s\"! (\"%s\").", filename, path);

end:
    if (path_dup) free(path_dup);

    return file_entry;
}

bool romfsGeneratePathFromDirectoryEntry(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    size_t path_len = 0;
    u64 dir_offset = ROMFS_VOID_ENTRY;
    u32 dir_entries_count = 0;
    RomFileSystemDirectoryEntry **dir_entries = NULL, **tmp_dir_entries = NULL;
    bool success = false;

    if (!romfsIsValidContext(ctx) || !dir_entry || (!dir_entry->name_length && dir_entry->parent_offset) || !out_path || out_path_size < 2 || \
        illegal_char_replace_type > RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Short-circuit: check if we're dealing with the root directory entry. */
    if (!dir_entry->name_length)
    {
        sprintf(out_path, "/");
        return true;
    }

    /* Allocate memory for our directory entries pointer array. */
    dir_entries = calloc(1, sizeof(RomFileSystemDirectoryEntry*));
    if (!dir_entries)
    {
        LOG_MSG_ERROR("Unable to allocate memory for directory entries!");
        goto end;
    }

    /* Update stats. */
    path_len = (1 + dir_entry->name_length);
    *dir_entries = dir_entry;
    dir_entries_count++;

    while(true)
    {
        /* Get parent directory offset. Break out of the loop if we reached the root directory. */
        dir_offset = dir_entries[dir_entries_count - 1]->parent_offset;
        if (!dir_offset) break;

        /* Reallocate directory entries pointer array. */
        if (!(tmp_dir_entries = realloc(dir_entries, (dir_entries_count + 1) * sizeof(RomFileSystemDirectoryEntry*))))
        {
            LOG_MSG_ERROR("Unable to reallocate directory entries buffer!");
            goto end;
        }

        dir_entries = tmp_dir_entries;
        tmp_dir_entries = NULL;

        /* Retrieve parent directory entry using the offset we got earlier. */
        RomFileSystemDirectoryEntry **cur_dir_entry = &(dir_entries[dir_entries_count]);
        if (!(*cur_dir_entry = romfsGetDirectoryEntryByOffset(ctx, dir_offset)) || !(*cur_dir_entry)->name_length)
        {
            LOG_MSG_ERROR("Failed to retrieve directory entry!");
            goto end;
        }

        /* Update stats. */
        path_len += (1 + (*cur_dir_entry)->name_length);
        dir_entries_count++;
    }

    /* Make sure the output buffer is big enough to hold the full path + NULL terminator. */
    if (path_len >= out_path_size)
    {
        LOG_MSG_ERROR("Output path length exceeds output buffer size! (%lu >= %lu).", path_len, out_path_size);
        goto end;
    }

    /* Generate output path, looping through our directory entries pointer array in reverse order. */
    *out_path = '\0';
    path_len = 0;

    for(u32 i = dir_entries_count; i > 0; i--)
    {
        /* Get current directory entry. */
        RomFileSystemDirectoryEntry *cur_dir_entry = dir_entries[i - 1];

        /* Concatenate path separator and current directory name to the output buffer. */
        strcat(out_path, "/");
        strncat(out_path, cur_dir_entry->name, cur_dir_entry->name_length);
        path_len++;

        if (illegal_char_replace_type)
        {
            /* Replace illegal characters within this directory name, then update the full path length. */
            utilsReplaceIllegalCharacters(out_path + path_len, illegal_char_replace_type == RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly);
            path_len += strlen(out_path + path_len);
        } else {
            /* Update full path length. */
            path_len += cur_dir_entry->name_length;
        }
    }

    /* Update return value. */
    success = true;

end:
    if (dir_entries) free(dir_entries);

    return success;
}

bool romfsGeneratePathFromFileEntry(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    size_t path_len = 0;
    RomFileSystemDirectoryEntry *dir_entry = NULL;
    bool success = false;

    if (!romfsIsValidContext(ctx) || !file_entry || !file_entry->name_length || !out_path || out_path_size < 2 || \
        !(dir_entry = romfsGetDirectoryEntryByOffset(ctx, file_entry->parent_offset)) || illegal_char_replace_type > RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Retrieve full RomFS path up to the file entry name. */
    if (!romfsGeneratePathFromDirectoryEntry(ctx, dir_entry, out_path, out_path_size, illegal_char_replace_type))
    {
        LOG_MSG_ERROR("Failed to retrieve RomFS directory path!");
        goto end;
    }

    /* Make sure the output buffer is big enough to hold the full path + NULL terminator. */
    path_len = strlen(out_path);
    if ((path_len + 1 + file_entry->name_length) >= out_path_size)
    {
        LOG_MSG_ERROR("Output path length exceeds output buffer size! (%lu >= %lu).", path_len + 1 + file_entry->name_length, out_path_size);
        goto end;
    }

    /* Concatenate path separator if our parent directory isn't the root directory. */
    if (file_entry->parent_offset)
    {
        strcat(out_path, "/");
        path_len++;
    }

    /* Concatenate file entry name. */
    strncat(out_path, file_entry->name, file_entry->name_length);

    /* Replace illegal characters within the file name, if needed. */
    if (illegal_char_replace_type) utilsReplaceIllegalCharacters(out_path + path_len, illegal_char_replace_type == RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly);

    /* Update return value. */
    success = true;

end:
    return success;
}

bool romfsIsFileEntryUpdated(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, bool *out)
{
    if (!romfsIsValidContext(ctx) || !ctx->is_patch || ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || \
        !file_entry || !file_entry->size || (file_entry->offset + file_entry->size) > ctx->size || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u64 file_offset = (ctx->offset + ctx->body_offset + file_entry->offset);
    bool success = false;

    /* Short-circuit: check if we're dealing with a Patch RomFS with a missing base RomFS. */
    if (!ncaStorageIsValidContext(&(ctx->storage_ctx[0])))
    {
        *out = success = true;
        goto end;
    }

    /* Check if any sections from this block belong to the Patch storage. */
    if (!ncaStorageIsBlockWithinPatchStorageRange(ctx->default_storage_ctx, file_offset, file_entry->size, out))
    {
        LOG_MSG_ERROR("Failed to determine if file entry is within Patch storage range!");
        goto end;
    }

    /* Update return value. */
    success = true;

end:
    return success;
}

bool romfsGenerateFileEntryPatch(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, const void *data, u64 data_size, u64 data_offset, RomFileSystemFileEntryPatch *out)
{
    if (!romfsIsValidContext(ctx) || ctx->is_patch || ctx->default_storage_ctx->base_storage_type != NcaStorageBaseStorageType_Regular || \
        (ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_Nca0RomFs && ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_RomFs) || \
        !file_entry || !file_entry->size || (file_entry->offset + file_entry->size) > ctx->size || !data || !data_size || (data_offset + data_size) > file_entry->size || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    NcaFsSectionContext *nca_fs_ctx = ctx->default_storage_ctx->nca_fs_ctx;
    u64 fs_offset = (ctx->body_offset + file_entry->offset + data_offset);
    bool success = false;

    if (nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs)
    {
        out->use_old_format_patch = true;
        success = ncaGenerateHierarchicalSha256Patch(nca_fs_ctx, data, data_size, fs_offset, &(out->old_format_patch));
    } else {
        out->use_old_format_patch = false;
        success = ncaGenerateHierarchicalIntegrityPatch(nca_fs_ctx, data, data_size, fs_offset, &(out->cur_format_patch));
    }

    out->written = false;

    if (!success) LOG_MSG_ERROR("Failed to generate 0x%lX bytes Hierarchical%s patch at offset 0x%lX for RomFS file entry!", data_size, \
                                nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? "Sha256" : "Integrity", fs_offset);

    return success;
}

static RomFileSystemDirectoryEntry *romfsGetChildDirectoryEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name)
{
    u64 dir_offset = 0;
    size_t name_len = 0;
    RomFileSystemDirectoryEntry *child_dir_entry = NULL;

    if (!dir_entry || (dir_offset = dir_entry->directory_offset) == ROMFS_VOID_ENTRY || !name || !(name_len = strlen(name)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    /* Loop through the child directory entries' linked list. */
    while(dir_offset != ROMFS_VOID_ENTRY)
    {
        /* Get current directory entry. */
        if (!(child_dir_entry = romfsGetDirectoryEntryByOffset(ctx, dir_offset)))
        {
            LOG_MSG_ERROR("Failed to retrieve directory entry! (0x%lX, 0x%lX).", dir_offset, ctx->dir_table_size);
            break;
        }

        /* Check if we found the right child directory entry. */
        /* strncmp() is used here instead of strcmp() because names stored in RomFS sections are not always NULL terminated. */
        /* If the name ends at a 4-byte boundary, the next entry starts immediately. */
        if (child_dir_entry->name_length == name_len && !strncmp(child_dir_entry->name, name, name_len)) return child_dir_entry;

        /* Update current directory entry offset. */
        dir_offset = child_dir_entry->next_offset;
    }

    return NULL;
}

static RomFileSystemFileEntry *romfsGetChildFileEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name)
{
    u64 file_offset = 0;
    size_t name_len = 0;
    RomFileSystemFileEntry *child_file_entry = NULL;

    if (!dir_entry || (file_offset = dir_entry->file_offset) == ROMFS_VOID_ENTRY || !name || !(name_len = strlen(name)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    /* Loop through the child file entries' linked list. */
    while(file_offset != ROMFS_VOID_ENTRY)
    {
        /* Get current file entry. */
        if (!(child_file_entry = romfsGetFileEntryByOffset(ctx, file_offset)))
        {
            LOG_MSG_ERROR("Failed to retrieve file entry! (0x%lX, 0x%lX).", file_offset, ctx->file_table_size);
            break;
        }

        /* Check if we found the right child file entry. */
        /* strncmp() is used here instead of strcmp() because names stored in RomFS sections are not always NULL terminated. */
        /* If the name ends at a 4-byte boundary, the next entry starts immediately. */
        if (child_file_entry->name_length == name_len && !strncmp(child_file_entry->name, name, name_len)) return child_file_entry;

        file_offset = child_file_entry->next_offset;
    }

    return NULL;
}
