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
        (!missing_base_romfs && (!(base_nca_ctx = (NcaContext*)base_nca_fs_ctx->nca_ctx) || (base_nca_ctx->format_version == NcaVersion_Nca0 && \
        (base_nca_fs_ctx->section_type != NcaFsSectionType_Nca0RomFs || base_nca_fs_ctx->hash_type != NcaHashType_HierarchicalSha256)) || \
        (base_nca_ctx->format_version != NcaVersion_Nca0 && (base_nca_fs_ctx->section_type != NcaFsSectionType_RomFs || \
        (base_nca_fs_ctx->hash_type != NcaHashType_HierarchicalIntegrity && base_nca_fs_ctx->hash_type != NcaHashType_HierarchicalIntegritySha3))) || \
        (base_nca_ctx->rights_id_available && !base_nca_ctx->titlekey_retrieved))) || (patch_nca_fs_ctx && (!patch_nca_fs_ctx->enabled || \
        !(patch_nca_ctx = (NcaContext*)patch_nca_fs_ctx->nca_ctx) || (!missing_base_romfs && patch_nca_ctx->format_version != base_nca_ctx->format_version) || \
        patch_nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || (patch_nca_ctx->rights_id_available && !patch_nca_ctx->titlekey_retrieved))))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    /* Free output context beforehand. */
    romfsFreeContext(out);

    NcaStorageContext *base_storage_ctx = &(out->storage_ctx[0]), *patch_storage_ctx = &(out->storage_ctx[1]);
    bool is_nca0_romfs = (base_nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs);

    /* Initialize base NCA storage context. */
    if (!missing_base_romfs && !ncaStorageInitializeContext(base_storage_ctx, base_nca_fs_ctx))
    {
        LOG_MSG("Failed to initialize base NCA storage context!");
        goto end;
    }

    if (patch_nca_fs_ctx)
    {
        /* Initialize base NCA storage context. */
        if (!ncaStorageInitializeContext(patch_storage_ctx, patch_nca_fs_ctx))
        {
            LOG_MSG("Failed to initialize patch NCA storage context!");
            goto end;
        }

        /* Set patch NCA storage original substorage, if available. */
        if (!missing_base_romfs && !ncaStorageSetPatchOriginalSubStorage(patch_storage_ctx, base_storage_ctx))
        {
            LOG_MSG("Failed to set patch NCA storage context's original substorage!");
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
        LOG_MSG("Failed to get target hash layer extents!");
        goto end;
    }

    /* Read RomFS header. */
    if (!ncaStorageRead(out->default_storage_ctx, &(out->header), sizeof(RomFileSystemHeader), out->offset))
    {
        LOG_MSG("Failed to read RomFS header!");
        goto end;
    }

    if ((is_nca0_romfs && out->header.old_format.header_size != ROMFS_OLD_HEADER_SIZE) || (!is_nca0_romfs && out->header.cur_format.header_size != ROMFS_HEADER_SIZE))
    {
        LOG_MSG("Invalid RomFS header size!");
        dump_fs_header = true;
        goto end;
    }

    /* Read directory entries table. */
    dir_table_offset = (is_nca0_romfs ? (u64)out->header.old_format.directory_entry_offset : out->header.cur_format.directory_entry_offset);
    out->dir_table_size = (is_nca0_romfs ? (u64)out->header.old_format.directory_entry_size : out->header.cur_format.directory_entry_size);

    if (!out->dir_table_size || (dir_table_offset + out->dir_table_size) > out->size)
    {
        LOG_MSG("Invalid RomFS directory entries table!");
        dump_fs_header = true;
        goto end;
    }

    out->dir_table = malloc(out->dir_table_size);
    if (!out->dir_table)
    {
        LOG_MSG("Unable to allocate memory for RomFS directory entries table!");
        goto end;
    }

    if (!ncaStorageRead(out->default_storage_ctx, out->dir_table, out->dir_table_size, out->offset + dir_table_offset))
    {
        LOG_MSG("Failed to read RomFS directory entries table!");
        goto end;
    }

    /* Read file entries table. */
    file_table_offset = (is_nca0_romfs ? (u64)out->header.old_format.file_entry_offset : out->header.cur_format.file_entry_offset);
    out->file_table_size = (is_nca0_romfs ? (u64)out->header.old_format.file_entry_size : out->header.cur_format.file_entry_size);

    if (!out->file_table_size || (file_table_offset + out->file_table_size) > out->size)
    {
        LOG_MSG("Invalid RomFS file entries table!");
        dump_fs_header = true;
        goto end;
    }

    out->file_table = malloc(out->file_table_size);
    if (!out->file_table)
    {
        LOG_MSG("Unable to allocate memory for RomFS file entries table!");
        goto end;
    }

    if (!ncaStorageRead(out->default_storage_ctx, out->file_table, out->file_table_size, out->offset + file_table_offset))
    {
        LOG_MSG("Failed to read RomFS file entries table!");
        goto end;
    }

    /* Get file data body offset. */
    out->body_offset = (is_nca0_romfs ? (u64)out->header.old_format.body_offset : out->header.cur_format.body_offset);
    if (out->body_offset >= out->size)
    {
        LOG_MSG("Invalid RomFS file data body!");
        dump_fs_header = true;
        goto end;
    }

    /* Update flag. */
    success = true;

end:
    if (!success)
    {
        if (dump_fs_header) LOG_DATA(&(out->header), sizeof(RomFileSystemHeader), "RomFS header dump:");

        romfsFreeContext(out);
    }

    return success;
}

bool romfsReadFileSystemData(RomFileSystemContext *ctx, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ncaStorageIsValidContext(ctx->default_storage_ctx) || !ctx->size || !out || !read_size || (offset + read_size) > ctx->size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    /* Read filesystem data. */
    if (!ncaStorageRead(ctx->default_storage_ctx, out, read_size, ctx->offset + offset))
    {
        LOG_MSG("Failed to read RomFS data!");
        return false;
    }

    return true;
}

bool romfsReadFileEntryData(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, void *out, u64 read_size, u64 offset)
{
    if (!ctx || !ctx->body_offset || !file_entry || !file_entry->size || (file_entry->offset + file_entry->size) > ctx->size || !out || !read_size || (offset + read_size) > file_entry->size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    /* Read entry data. */
    if (!romfsReadFileSystemData(ctx, out, read_size, ctx->body_offset + file_entry->offset + offset))
    {
        LOG_MSG("Failed to read RomFS file entry data!");
        return false;
    }

    return true;
}

bool romfsGetTotalDataSize(RomFileSystemContext *ctx, u64 *out_size)
{
    if (!ctx || !ctx->file_table_size || !ctx->file_table || !out_size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    u64 offset = 0, total_size = 0;
    RomFileSystemFileEntry *file_entry = NULL;

    while(offset < ctx->file_table_size)
    {
        if (!(file_entry = romfsGetFileEntryByOffset(ctx, offset)))
        {
            LOG_MSG("Failed to retrieve file entry!");
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
        LOG_MSG("Invalid parameters!");
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
            LOG_MSG("Failed to retrieve file entry!");
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
            LOG_MSG("Failed to retrieve directory entry/size!");
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
    char *path_dup = NULL, *pch = NULL, *state = NULL;
    RomFileSystemDirectoryEntry *dir_entry = NULL;

    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !path || *path != '/' || !(path_len = strlen(path)) || !(dir_entry = romfsGetDirectoryEntryByOffset(ctx, 0)))
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }

    /* Check if the root directory was requested. */
    if (path_len == 1) return dir_entry;

    /* Duplicate path to avoid problems with strtok_r(). */
    if (!(path_dup = strdup(path)))
    {
        LOG_MSG("Unable to duplicate input path! (\"%s\").", path);
        return NULL;
    }

    pch = strtok_r(path_dup, "/", &state);
    if (!pch)
    {
        LOG_MSG("Failed to tokenize input path! (\"%s\").", path);
        dir_entry = NULL;
        goto end;
    }

    while(pch)
    {
        if (!(dir_entry = romfsGetChildDirectoryEntryByName(ctx, dir_entry, pch)))
        {
            LOG_MSG("Failed to retrieve directory entry by name for \"%s\"! (\"%s\").", pch, path);
            break;
        }

        pch = strtok_r(NULL, "/", &state);
    }

end:
    if (path_dup) free(path_dup);

    return dir_entry;
}

RomFileSystemFileEntry *romfsGetFileEntryByPath(RomFileSystemContext *ctx, const char *path)
{
    size_t path_len = 0;
    u8 content_type = 0;
    char *path_dup = NULL, *filename = NULL;
    RomFileSystemFileEntry *file_entry = NULL;
    RomFileSystemDirectoryEntry *dir_entry = NULL;
    NcaContext *nca_ctx = NULL;

    if (!ctx || !ctx->file_table || !ctx->file_table_size || !ncaStorageIsValidContext(ctx->default_storage_ctx) || \
        !(nca_ctx = (NcaContext*)ctx->default_storage_ctx->nca_fs_ctx->nca_ctx) || !path || *path != '/' || (path_len = strlen(path)) <= 1)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }

    content_type = nca_ctx->content_type;

    /* Duplicate path. */
    if (!(path_dup = strdup(path)))
    {
        LOG_MSG("Unable to duplicate input path! (\"%s\").", path);
        return NULL;
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
        LOG_MSG("Invalid input path! (\"%s\").", path);
        goto end;
    }

    /* Remove leading slash and adjust filename string pointer. */
    *filename++ = '\0';

    /* Retrieve directory entry. */
    /* If the first character is NULL, then just retrieve the root directory entry. */
    if (!(dir_entry = (*path_dup ? romfsGetDirectoryEntryByPath(ctx, path_dup) : romfsGetDirectoryEntryByOffset(ctx, 0))))
    {
        LOG_MSG("Failed to retrieve directory entry for \"%s\"! (\"%s\").", *path_dup ? path_dup : "/", path);
        goto end;
    }

    /* Retrieve file entry. */
    if (!(file_entry = romfsGetChildFileEntryByName(ctx, dir_entry, filename)))
    {
        /* Only log error if we're not dealing with NACP icons or a LegalInformation XML. */
        bool skip_log = ((!strncmp(path, "/icon_", 6) && content_type == NcmContentType_Control) || (!strcmp(path, "/legalinfo.xml") && content_type == NcmContentType_LegalInformation));
        if (!skip_log) LOG_MSG("Failed to retrieve file entry by name for \"%s\"! (\"%s\").", filename, path);
    }

end:
    if (path_dup) free(path_dup);

    return file_entry;
}

bool romfsGeneratePathFromDirectoryEntry(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    size_t path_len = 0;
    u32 dir_offset = ROMFS_VOID_ENTRY, dir_entries_count = 0;
    RomFileSystemDirectoryEntry **dir_entries = NULL, **tmp_dir_entries = NULL;
    bool success = false;

    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !dir_entry || (!dir_entry->name_length && dir_entry->parent_offset) || !out_path || out_path_size < 2 || \
        illegal_char_replace_type > RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    /* Check if we're dealing with the root directory entry. */
    if (!dir_entry->name_length)
    {
        sprintf(out_path, "/");
        return true;
    }

    /* Allocate memory for our directory entries. */
    dir_entries = calloc(1, sizeof(RomFileSystemDirectoryEntry*));
    if (!dir_entries)
    {
        LOG_MSG("Unable to allocate memory for directory entries!");
        return false;
    }

    path_len = (1 + dir_entry->name_length);
    *dir_entries = dir_entry;
    dir_entries_count++;

    while(true)
    {
        dir_offset = dir_entries[dir_entries_count - 1]->parent_offset;
        if (!dir_offset) break;

        /* Reallocate directory entries. */
        if (!(tmp_dir_entries = realloc(dir_entries, (dir_entries_count + 1) * sizeof(RomFileSystemDirectoryEntry*))))
        {
            LOG_MSG("Unable to reallocate directory entries buffer!");
            goto end;
        }

        dir_entries = tmp_dir_entries;
        tmp_dir_entries = NULL;

        RomFileSystemDirectoryEntry **cur_dir_entry = &(dir_entries[dir_entries_count]);
        if (!(*cur_dir_entry = romfsGetDirectoryEntryByOffset(ctx, dir_offset)) || !(*cur_dir_entry)->name_length)
        {
            LOG_MSG("Failed to retrieve directory entry!");
            goto end;
        }

        path_len += (1 + (*cur_dir_entry)->name_length);
        dir_entries_count++;
    }

    if (path_len >= out_path_size)
    {
        LOG_MSG("Output path length exceeds output buffer size!");
        goto end;
    }

    /* Generate output path. */
    *out_path = '\0';
    path_len = 0;

    for(u32 i = dir_entries_count; i > 0; i--)
    {
        RomFileSystemDirectoryEntry **cur_dir_entry = &(dir_entries[i - 1]);

        strcat(out_path, "/");
        strncat(out_path, (*cur_dir_entry)->name, (*cur_dir_entry)->name_length);
        path_len++;

        if (illegal_char_replace_type)
        {
            utilsReplaceIllegalCharacters(out_path + path_len, illegal_char_replace_type == RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly);
            path_len += strlen(out_path + path_len);
        } else {
            path_len += (*cur_dir_entry)->name_length;
        }
    }

    success = true;

end:
    if (dir_entries) free(dir_entries);

    return success;
}

bool romfsGeneratePathFromFileEntry(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, char *out_path, size_t out_path_size, u8 illegal_char_replace_type)
{
    size_t path_len = 0;
    RomFileSystemDirectoryEntry *dir_entry = NULL;

    if (!ctx || !ctx->file_table || !ctx->file_table_size || !file_entry || !file_entry->name_length || !out_path || out_path_size < 2 || \
        !(dir_entry = romfsGetDirectoryEntryByOffset(ctx, file_entry->parent_offset)) || illegal_char_replace_type > RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }

    /* Retrieve full RomFS path up to the file entry name. */
    if (!romfsGeneratePathFromDirectoryEntry(ctx, dir_entry, out_path, out_path_size, illegal_char_replace_type))
    {
        LOG_MSG("Failed to retrieve RomFS directory path!");
        return false;
    }

    /* Check path length. */
    path_len = strlen(out_path);
    if ((1 + file_entry->name_length) >= (out_path_size - path_len))
    {
        LOG_MSG("Output path length exceeds output buffer size!");
        return false;
    }

    /* Concatenate file entry name. */
    if (file_entry->parent_offset)
    {
        strcat(out_path, "/");
        path_len++;
    }

    strncat(out_path, file_entry->name, file_entry->name_length);

    if (illegal_char_replace_type) utilsReplaceIllegalCharacters(out_path + path_len, illegal_char_replace_type == RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly);

    return true;
}

bool romfsGenerateFileEntryPatch(RomFileSystemContext *ctx, RomFileSystemFileEntry *file_entry, const void *data, u64 data_size, u64 data_offset, RomFileSystemFileEntryPatch *out)
{
    if (!ctx || ctx->is_patch || !ncaStorageIsValidContext(ctx->default_storage_ctx) || ctx->default_storage_ctx->base_storage_type != NcaStorageBaseStorageType_Regular || \
        !ctx->body_offset || (ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_Nca0RomFs && ctx->default_storage_ctx->nca_fs_ctx->section_type != NcaFsSectionType_RomFs) || \
        !file_entry || !file_entry->size || (file_entry->offset + file_entry->size) > ctx->size || !data || !data_size || (data_offset + data_size) > file_entry->size || !out)
    {
        LOG_MSG("Invalid parameters!");
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

    if (!success) LOG_MSG("Failed to generate 0x%lX bytes Hierarchical%s patch at offset 0x%lX for RomFS file entry!", data_size, \
                          nca_fs_ctx->section_type == NcaFsSectionType_Nca0RomFs ? "Sha256" : "Integrity", fs_offset);

    return success;
}

static RomFileSystemDirectoryEntry *romfsGetChildDirectoryEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name)
{
    u64 dir_offset = 0;
    size_t name_len = 0;
    RomFileSystemDirectoryEntry *child_dir_entry = NULL;

    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !dir_entry || (dir_offset = dir_entry->directory_offset) == ROMFS_VOID_ENTRY || !name || !(name_len = strlen(name)))
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }

    while(dir_offset != ROMFS_VOID_ENTRY)
    {
        if (!(child_dir_entry = romfsGetDirectoryEntryByOffset(ctx, dir_offset)))
        {
            LOG_MSG("Failed to retrieve directory entry at offset 0x%lX!", dir_offset);
            break;
        }

        /* strncmp() is used here instead of strcmp() because names stored in RomFS sections are not always NULL terminated. */
        /* If the name ends at a 4-byte boundary, the next entry starts immediately. */
        if (child_dir_entry->name_length == name_len && !strncmp(child_dir_entry->name, name, name_len)) return child_dir_entry;

        dir_offset = child_dir_entry->next_offset;
    }

    return NULL;
}

static RomFileSystemFileEntry *romfsGetChildFileEntryByName(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry, const char *name)
{
    u64 file_offset = 0;
    size_t name_len = 0;
    RomFileSystemFileEntry *child_file_entry = NULL;

    if (!ctx || !ctx->dir_table || !ctx->dir_table_size || !ctx->file_table || !ctx->file_table_size || !dir_entry || (file_offset = dir_entry->file_offset) == ROMFS_VOID_ENTRY || \
        !name || !(name_len = strlen(name)))
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }

    while(file_offset != ROMFS_VOID_ENTRY)
    {
        if (!(child_file_entry = romfsGetFileEntryByOffset(ctx, file_offset)))
        {
            LOG_MSG("Failed to retrieve file entry at offset 0x%lX!", file_offset);
            break;
        }

        /* strncmp() is used here instead of strcmp() because names stored in RomFS sections are not always NULL terminated. */
        /* If the name ends at a 4-byte boundary, the next entry starts immediately. */
        if (child_file_entry->name_length == name_len && !strncmp(child_file_entry->name, name, name_len)) return child_file_entry;

        file_offset = child_file_entry->next_offset;
    }

    return NULL;
}
