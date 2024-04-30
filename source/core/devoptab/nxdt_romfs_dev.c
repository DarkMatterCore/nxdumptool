/*
 * nxdt_romfs_dev.c
 *
 * Loosely based on romfs_dev.c from libnx, et al.
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
#include <core/devoptab/nxdt_devoptab.h>
#include <core/devoptab/ro_dev.h>

/* Helper macros. */

#define ROMFS_DEV_INIT_VARS         DEVOPTAB_INIT_VARS(RomFileSystemContext)
#define ROMFS_DEV_INIT_FILE_VARS    DEVOPTAB_INIT_FILE_VARS(RomFileSystemContext, RomFileSystemFileState)
#define ROMFS_DEV_INIT_DIR_VARS     DEVOPTAB_INIT_DIR_VARS(RomFileSystemContext, RomFileSystemDirectoryState)
#define ROMFS_DEV_INIT_FS_ACCESS    DEVOPTAB_DECL_FS_CTX(RomFileSystemContext)

#define ROMFS_FILE_INODE(file)      ((u64)(file - fs_ctx->file_table) + (fs_ctx->dir_table_size / 4))
#define ROMFS_DIR_INODE(dir)        (u64)(dir - fs_ctx->dir_table)

/* Type definitions. */

typedef struct {
    RomFileSystemFileEntry *file_entry; ///< RomFS file entry metadata.
    u64 data_offset;                    ///< Current offset within RomFS file entry data.
} RomFileSystemFileState;

typedef struct {
    RomFileSystemDirectoryEntry *dir_entry; ///< RomFS directory entry metadata.
    u8 state;                               ///< 0: "." entry; 1: ".." entry; 2: actual RomFS entry.
    u64 cur_dir_offset;                     ///< Offset to current child directory entry within the RomFS directory table.
    u64 cur_file_offset;                    ///< Offset to current child file entry within the RomFS file table.
} RomFileSystemDirectoryState;

/* Function prototypes. */

static int       romfsdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode);
static int       romfsdev_close(struct _reent *r, void *fd);
static ssize_t   romfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len);
static off_t     romfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir);
static int       romfsdev_fstat(struct _reent *r, void *fd, struct stat *st);
static int       romfsdev_stat(struct _reent *r, const char *file, struct stat *st);
static DIR_ITER* romfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path);
static int       romfsdev_dirreset(struct _reent *r, DIR_ITER *dirState);
static int       romfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
static int       romfsdev_dirclose(struct _reent *r, DIR_ITER *dirState);
static int       romfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf);

static const char *romfsdev_get_truncated_path(struct _reent *r, const char *path);

static void romfsdev_fill_file_stat(struct stat *st, const RomFileSystemContext *fs_ctx, const RomFileSystemFileEntry *file_entry, time_t mount_time);
static void romfsdev_fill_dir_stat(struct stat *st, RomFileSystemContext *fs_ctx, RomFileSystemDirectoryEntry *dir_entry, time_t mount_time);

static nlink_t romfsdev_get_dir_nlink(RomFileSystemContext *ctx, RomFileSystemDirectoryEntry *dir_entry);

/* Global variables. */

static const devoptab_t romfsdev_devoptab = {
    .name         = NULL,
    .structSize   = sizeof(RomFileSystemFileState),
    .open_r       = romfsdev_open,
    .close_r      = romfsdev_close,
    .write_r      = rodev_write,                            ///< Not supported by RomFS sections.
    .read_r       = romfsdev_read,
    .seek_r       = romfsdev_seek,
    .fstat_r      = romfsdev_fstat,
    .stat_r       = romfsdev_stat,
    .link_r       = rodev_link,                             ///< Not supported by RomFS sections.
    .unlink_r     = rodev_unlink,                           ///< Not supported by RomFS sections.
    .chdir_r      = rodev_chdir,                            ///< No need to deal with cwd shenanigans, so we won't support it.
    .rename_r     = rodev_rename,                           ///< Not supported by RomFS sections.
    .mkdir_r      = rodev_mkdir,                            ///< Not supported by RomFS sections.
    .dirStateSize = sizeof(RomFileSystemDirectoryState),
    .diropen_r    = romfsdev_diropen,
    .dirreset_r   = romfsdev_dirreset,
    .dirnext_r    = romfsdev_dirnext,
    .dirclose_r   = romfsdev_dirclose,
    .statvfs_r    = romfsdev_statvfs,
    .ftruncate_r  = rodev_ftruncate,                        ///< Not supported by RomFS sections.
    .fsync_r      = rodev_fsync,                            ///< Not supported by RomFS sections.
    .deviceData   = NULL,
    .chmod_r      = rodev_chmod,                            ///< Not supported by RomFS sections.
    .fchmod_r     = rodev_fchmod,                           ///< Not supported by RomFS sections.
    .rmdir_r      = rodev_rmdir,                            ///< Not supported by RomFS sections.
    .lstat_r      = romfsdev_stat,                          ///< Symlinks aren't supported, so we'll just alias lstat() to stat().
    .utimes_r     = rodev_utimes,                           ///< Not supported by RomFS sections.
    .fpathconf_r  = rodev_fpathconf,                        ///< Not supported by RomFS sections.
    .pathconf_r   = rodev_pathconf,                         ///< Not supported by RomFS sections.
    .symlink_r    = rodev_symlink,                          ///< Not supported by RomFS sections.
    .readlink_r   = rodev_readlink                          ///< Not supported by RomFS sections.
};

const devoptab_t *romfsdev_get_devoptab()
{
    return &romfsdev_devoptab;
}

static int romfsdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    NX_IGNORE_ARG(mode);

    ROMFS_DEV_INIT_FILE_VARS;
    ROMFS_DEV_INIT_FS_ACCESS;

    /* Validate input. */
    if (!file || (flags & (O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC | O_EXCL))) DEVOPTAB_SET_ERROR_AND_EXIT(EROFS);

    /* Get truncated path. */
    if (!(path = romfsdev_get_truncated_path(r, path))) DEVOPTAB_EXIT;

    //LOG_MSG_DEBUG("Opening \"%s:%s\" with flags 0x%X.", dev_ctx->name, path, flags);

    /* Reset file descriptor. */
    memset(file, 0, sizeof(RomFileSystemFileState));

    /* Get information about the requested RomFS file entry. */
    if (!(file->file_entry = romfsGetFileEntryByPath(fs_ctx, path))) DEVOPTAB_SET_ERROR(ENOENT);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int romfsdev_close(struct _reent *r, void *fd)
{
    ROMFS_DEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    //LOG_MSG_DEBUG("Closing file \"%.*s\" from \"%s:\".", (int)file->file_entry->name_length, file->file_entry->name, dev_ctx->name);

    /* Reset file descriptor. */
    memset(file, 0, sizeof(RomFileSystemFileState));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static ssize_t romfsdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    ROMFS_DEV_INIT_FILE_VARS;
    ROMFS_DEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!file || !ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /*LOG_MSG_DEBUG("Reading 0x%lX byte(s) at offset 0x%lX from file \"%.*s\" in \"%s:\".", len, file->data_offset, (int)file->file_entry->name_length, file->file_entry->name, \
                                                                                          dev_ctx->name);*/

    /* Read file data. */
    if (!romfsReadFileEntryData(fs_ctx, file->file_entry, ptr, len, file->data_offset)) DEVOPTAB_SET_ERROR_AND_EXIT(EIO);

    /* Adjust offset. */
    file->data_offset += len;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)len);
}

static off_t romfsdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t offset = 0;

    ROMFS_DEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!file) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Find the offset to seek from. */
    switch(dir)
    {
        case SEEK_SET:  /* Set absolute position relative to zero (start offset). */
            break;
        case SEEK_CUR:  /* Set position relative to the current position. */
            offset = (off_t)file->data_offset;
            break;
        case SEEK_END:  /* Set position relative to EOF. */
            offset = (off_t)file->file_entry->size;
            break;
        default:        /* Invalid option. */
            DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
    }

    /* Don't allow negative seeks beyond the beginning of file. */
    if (pos < 0 && offset < -pos) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Calculate actual offset. */
    offset += pos;

    /* Don't allow positive seeks beyond the end of file. */
    if (offset > (off_t)file->file_entry->size) DEVOPTAB_SET_ERROR_AND_EXIT(EOVERFLOW);

    //LOG_MSG_DEBUG("Seeking to offset 0x%lX from file \"%.*s\" in \"%s:\".", offset, (int)file->file_entry->name_length, file->file_entry->name, dev_ctx->name);

    /* Adjust offset. */
    file->data_offset = (u64)offset;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(offset);
}

static int romfsdev_fstat(struct _reent *r, void *fd, struct stat *st)
{
    ROMFS_DEV_INIT_FILE_VARS;
    ROMFS_DEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!file || !st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    //LOG_MSG_DEBUG("Getting stats for file \"%.*s\" in \"%s:\".", (int)file->file_entry->name_length, file->file_entry->name, dev_ctx->name);

    /* Fill stat info. */
    romfsdev_fill_file_stat(st, fs_ctx, file->file_entry, dev_ctx->mount_time);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int romfsdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    RomFileSystemFileEntry *file_entry = NULL;

    ROMFS_DEV_INIT_VARS;
    ROMFS_DEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get truncated path. */
    if (!(file = romfsdev_get_truncated_path(r, file))) DEVOPTAB_EXIT;

    //LOG_MSG_DEBUG("Getting file stats for \"%s:%s\".", dev_ctx->name, file);

    /* Get information about the requested RomFS file entry. */
    if (!(file_entry = romfsGetFileEntryByPath(fs_ctx, file))) DEVOPTAB_SET_ERROR_AND_EXIT(ENOENT);

    /* Fill stat info. */
    romfsdev_fill_file_stat(st, fs_ctx, file_entry, dev_ctx->mount_time);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static DIR_ITER *romfsdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    DIR_ITER *ret = NULL;

    ROMFS_DEV_INIT_DIR_VARS;
    ROMFS_DEV_INIT_FS_ACCESS;

    /* Get truncated path. */
    if (!(path = romfsdev_get_truncated_path(r, path))) DEVOPTAB_EXIT;

    //LOG_MSG_DEBUG("Opening directory \"%s:%s\".", dev_ctx->name, path);

    /* Reset directory state. */
    memset(dir, 0, sizeof(RomFileSystemDirectoryState));

    /* Get information about the requested RomFS directory entry. */
    if (!(dir->dir_entry = romfsGetDirectoryEntryByPath(fs_ctx, path))) DEVOPTAB_SET_ERROR_AND_EXIT(ENOENT);

    dir->cur_dir_offset = dir->dir_entry->directory_offset;
    dir->cur_file_offset = dir->dir_entry->file_offset;

    /* Update return value. */
    ret = dirState;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_PTR(ret);
}

static int romfsdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    ROMFS_DEV_INIT_DIR_VARS;

    //LOG_MSG_DEBUG("Resetting state for directory \"%.*s\" in \"%s:\".", (int)dir->dir_entry->name_length, dir->dir_entry->name, dev_ctx->name);

    /* Reset directory state. */
    dir->state = 0;
    dir->cur_dir_offset = dir->dir_entry->directory_offset;
    dir->cur_file_offset = dir->dir_entry->file_offset;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int romfsdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    ROMFS_DEV_INIT_DIR_VARS;
    ROMFS_DEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!filename || !filestat) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /*LOG_MSG_DEBUG("Getting info for next entry from directory \"%.*s\" in \"%s:\" (state %u, cur_dir_offset 0x%lX, cur_file_offset 0x%lX).", \
                  (int)dir->dir_entry->name_length, dir->dir_entry->name, dev_ctx->name, dir->state, dir->cur_dir_offset, dir->cur_file_offset);*/

    if (dir->state < 2)
    {
        RomFileSystemDirectoryEntry *dir_entry = (dir->state == 0 ? dir->dir_entry : romfsGetDirectoryEntryByOffset(fs_ctx, dir->dir_entry->parent_offset));
        if (!dir_entry) DEVOPTAB_SET_ERROR_AND_EXIT(EFAULT);

        /* Fill directory entry. */
        romfsdev_fill_dir_stat(filestat, fs_ctx, dir_entry, dev_ctx->mount_time);
        strcpy(filename, dir->state == 0 ? "." : "..");

        /* Update state. */
        dir->state++;

        DEVOPTAB_EXIT;
    }

    if (dir->cur_dir_offset != ROMFS_VOID_ENTRY)
    {
        /* Get next directory entry. */
        RomFileSystemDirectoryEntry *dir_entry = romfsGetDirectoryEntryByOffset(fs_ctx, dir->cur_dir_offset);
        if (!dir_entry) DEVOPTAB_SET_ERROR_AND_EXIT(EFAULT);
        if (dir_entry->name_length > NAME_MAX) DEVOPTAB_SET_ERROR_AND_EXIT(ENAMETOOLONG);

        /* Fill directory entry. */
        romfsdev_fill_dir_stat(filestat, fs_ctx, dir_entry, dev_ctx->mount_time);
        snprintf(filename, NAME_MAX + 1, "%.*s", (int)dir_entry->name_length, dir_entry->name);

        /* Update child directory offset. */
        dir->cur_dir_offset = dir_entry->next_offset;

        DEVOPTAB_EXIT;
    }

    if (dir->cur_file_offset != ROMFS_VOID_ENTRY)
    {
        /* Get next file entry. */
        RomFileSystemFileEntry *file_entry = romfsGetFileEntryByOffset(fs_ctx, dir->cur_file_offset);
        if (!file_entry) DEVOPTAB_SET_ERROR_AND_EXIT(EFAULT);
        if (file_entry->name_length > NAME_MAX) DEVOPTAB_SET_ERROR_AND_EXIT(ENAMETOOLONG);

        /* Fill file entry. */
        romfsdev_fill_file_stat(filestat, fs_ctx, file_entry, dev_ctx->mount_time);
        snprintf(filename, NAME_MAX + 1, "%.*s", (int)file_entry->name_length, file_entry->name);

        /* Update child file offset. */
        dir->cur_file_offset = file_entry->next_offset;

        DEVOPTAB_EXIT;
    }

    /* We have reached EOD. */
    DEVOPTAB_SET_ERROR(ENOENT);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int romfsdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    ROMFS_DEV_INIT_DIR_VARS;

    //LOG_MSG_DEBUG("Closing directory \"%.*s\" in \"%s:\".", (int)dir->dir_entry->name_length, dir->dir_entry->name, dev_ctx->name);

    /* Reset directory state. */
    memset(dir, 0, sizeof(RomFileSystemDirectoryState));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int romfsdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    NX_IGNORE_ARG(path);

    u64 ext_fs_size = 0;

    ROMFS_DEV_INIT_VARS;
    ROMFS_DEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!buf) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    //LOG_MSG_DEBUG("Getting filesystem stats for \"%s:\"", dev_ctx->name);

    /* Get RomFS total data size. */
    if (!romfsGetTotalDataSize(fs_ctx, false, &ext_fs_size)) DEVOPTAB_SET_ERROR_AND_EXIT(EIO);

    /* Fill filesystem stats. */
    memset(buf, 0, sizeof(struct statvfs));

    buf->f_bsize = 1;
    buf->f_frsize = 1;
    buf->f_blocks = ext_fs_size;
    buf->f_bfree = 0;
    buf->f_bavail = 0;
    buf->f_files = 0;
    buf->f_ffree = 0;
    buf->f_favail = 0;
    buf->f_fsid = 0;
    buf->f_flag = ST_NOSUID;
    buf->f_namemax = FS_MAX_PATH;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static const char *romfsdev_get_truncated_path(struct _reent *r, const char *path)
{
    const u8 *p = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;
    size_t len = 0;

    DEVOPTAB_DECL_ERROR_STATE;

    if (!r || !path || !*path) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    //LOG_MSG_DEBUG("Input path: \"%s\".", path);

    /* Move the path pointer to the start of the actual path. */
    do {
        units = decode_utf8(&code, p);
        if (units < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EILSEQ);
        p += units;
    } while(code >= ' ' && code != ':');

    /* We found a colon; p points to the actual path. */
    if (code == ':') path = (const char*)p;

    /* Make sure the provided path starts with a slash. */
    if (path[0] != '/') DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Make sure there are no more colons and that the remainder of the string is valid UTF-8. */
    p = (const u8*)path;

    do {
        units = decode_utf8(&code, p);
        if (units < 0) DEVOPTAB_SET_ERROR_AND_EXIT(EILSEQ);
        if (code == ':') DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
        p += units;
    } while(code >= ' ');

    /* Verify fixed path length. */
    len = strlen(path);
    if (len >= FS_MAX_PATH) DEVOPTAB_SET_ERROR_AND_EXIT(ENAMETOOLONG);

    //LOG_MSG_DEBUG("Truncated path: \"%s\".", path);

end:
    DEVOPTAB_RETURN_PTR(path);
}

static void romfsdev_fill_file_stat(struct stat *st, const RomFileSystemContext *fs_ctx, const RomFileSystemFileEntry *file_entry, time_t mount_time)
{
    /* Clear stat struct. */
    memset(st, 0, sizeof(struct stat));

    /* Fill stat struct. */
    st->st_ino = ROMFS_FILE_INODE(file_entry);
    st->st_mode = (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH);
    st->st_nlink = 1;
    st->st_size = (off_t)file_entry->size;
    st->st_atime = st->st_mtime = st->st_ctime = mount_time;
}

static void romfsdev_fill_dir_stat(struct stat *st, RomFileSystemContext *fs_ctx, RomFileSystemDirectoryEntry *dir_entry, time_t mount_time)
{
    /* Clear stat struct. */
    memset(st, 0, sizeof(struct stat));

    /* Fill stat struct. */
    st->st_ino = ROMFS_DIR_INODE(dir_entry);
    st->st_mode = (S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH);
    st->st_nlink = romfsdev_get_dir_nlink(fs_ctx, dir_entry);
    st->st_size = ALIGN_UP(sizeof(RomFileSystemDirectoryEntry) + dir_entry->name_length, ROMFS_TABLE_ENTRY_ALIGNMENT);
    st->st_atime = st->st_mtime = st->st_ctime = mount_time;
}

static nlink_t romfsdev_get_dir_nlink(RomFileSystemContext *fs_ctx, RomFileSystemDirectoryEntry *dir_entry)
{
    u64 cur_entry_offset = 0;
    nlink_t count = 2; // One for self, one for parent.

    /* Short-circuit: check if we're dealing with an empty directory. */
    if (dir_entry->file_offset == ROMFS_VOID_ENTRY && dir_entry->directory_offset == ROMFS_VOID_ENTRY) return count;

    /* Loop through the child file entries' linked list. */
    cur_entry_offset = dir_entry->file_offset;
    while(cur_entry_offset != ROMFS_VOID_ENTRY)
    {
        /* Get current file entry. */
        RomFileSystemFileEntry *cur_file_entry = romfsGetFileEntryByOffset(fs_ctx, cur_entry_offset);
        if (!cur_file_entry) break;

        /* Update count. */
        count++;

        /* Update current file entry offset. */
        cur_entry_offset = cur_file_entry->next_offset;
    }

    /* Loop through the child directory entries' linked list. */
    cur_entry_offset = dir_entry->directory_offset;
    while(cur_entry_offset != ROMFS_VOID_ENTRY)
    {
        /* Get current directory entry. */
        RomFileSystemDirectoryEntry *cur_dir_entry = romfsGetDirectoryEntryByOffset(fs_ctx, cur_entry_offset);
        if (!cur_dir_entry) break;

        /* Update count. */
        count++;

        /* Update current directory entry offset. */
        cur_entry_offset = cur_dir_entry->next_offset;
    }

    return count;
}
