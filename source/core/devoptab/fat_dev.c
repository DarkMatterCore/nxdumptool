/*
 * fat_dev.c
 *
 * Loosely based on ff_dev.c from libusbhsfs.
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

#define FAT_DEV_INIT_FILE_VARS  DEVOPTAB_INIT_FILE_VARS(FFFIL)
#define FAT_DEV_INIT_DIR_VARS   DEVOPTAB_INIT_DIR_VARS(FFDIR)
#define FAT_DEV_INIT_FS_ACCESS  DEVOPTAB_DECL_FS_CTX(FATFS)

/* Function prototypes. */

static int       fatdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode);
static int       fatdev_close(struct _reent *r, void *fd);
static ssize_t   fatdev_read(struct _reent *r, void *fd, char *ptr, size_t len);
static off_t     fatdev_seek(struct _reent *r, void *fd, off_t pos, int dir);
static int       fatdev_stat(struct _reent *r, const char *file, struct stat *st);
static DIR_ITER* fatdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path);
static int       fatdev_dirreset(struct _reent *r, DIR_ITER *dirState);
static int       fatdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat);
static int       fatdev_dirclose(struct _reent *r, DIR_ITER *dirState);
static int       fatdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf);

static const char *fatdev_get_fixed_path(struct _reent *r, const char *path, FATFS *fatfs);

static void fatdev_fill_stat(struct stat *st, const FILINFO *info);

static void fatdev_time_fat2posix(const WORD fat_date, const WORD fat_time, time_t *out_posix_ts);

static int fatdev_translate_error(FRESULT res);

/* Global variables. */

__thread char g_fatDevicePathBuffer[FS_MAX_PATH] = {0};

static const devoptab_t fatdev_devoptab = {
    .name         = NULL,
    .structSize   = sizeof(FFFIL),
    .open_r       = fatdev_open,
    .close_r      = fatdev_close,
    .write_r      = rodev_write,        ///< Supported by FatFs, but disabled on purpose.
    .read_r       = fatdev_read,
    .seek_r       = fatdev_seek,
    .fstat_r      = rodev_fstat,        ///< Not supported by FatFs.
    .stat_r       = fatdev_stat,
    .link_r       = rodev_link,         ///< Supported by FatFs, but disabled on purpose.
    .unlink_r     = rodev_unlink,       ///< Supported by FatFs, but disabled on purpose.
    .chdir_r      = rodev_chdir,        ///< No need to deal with cwd shenanigans, so we won't support it.
    .rename_r     = rodev_rename,       ///< Supported by FatFs, but disabled on purpose.
    .mkdir_r      = rodev_mkdir,        ///< Supported by FatFs, but disabled on purpose.
    .dirStateSize = sizeof(FFDIR),
    .diropen_r    = fatdev_diropen,
    .dirreset_r   = fatdev_dirreset,
    .dirnext_r    = fatdev_dirnext,
    .dirclose_r   = fatdev_dirclose,
    .statvfs_r    = fatdev_statvfs,
    .ftruncate_r  = rodev_ftruncate,    ///< Supported by FatFs, but disabled on purpose.
    .fsync_r      = rodev_fsync,        ///< Supported by FatFs, but disabled on purpose.
    .deviceData   = NULL,
    .chmod_r      = rodev_chmod,        ///< Supported by FatFs, but disabled on purpose.
    .fchmod_r     = rodev_fchmod,       ///< Supported by FatFs, but disabled on purpose.
    .rmdir_r      = rodev_rmdir,        ///< Supported by FatFs, but disabled on purpose.
    .lstat_r      = fatdev_stat,        ///< Symlinks aren't supported, so we'll just alias lstat() to stat().
    .utimes_r     = rodev_utimes,       ///< Supported by FatFs, but disabled on purpose.
    .fpathconf_r  = rodev_fpathconf,    ///< Not supported by FatFs.
    .pathconf_r   = rodev_pathconf,     ///< Not supported by FatFs.
    .symlink_r    = rodev_symlink,      ///< Not supported by FatFs.
    .readlink_r   = rodev_readlink      ///< Not supported by FatFs.
};

const devoptab_t *fatdev_get_devoptab()
{
    return &fatdev_devoptab;
}

static int fatdev_open(struct _reent *r, void *fd, const char *path, int flags, int mode)
{
    NX_IGNORE_ARG(mode);

    BYTE fatdev_flags = (FA_READ | FA_OPEN_EXISTING);
    FRESULT res = FR_OK;

    FAT_DEV_INIT_FILE_VARS;
    FAT_DEV_INIT_FS_ACCESS;

    /* Validate input. */
    if ((flags & (O_WRONLY | O_RDWR | O_APPEND | O_CREAT | O_TRUNC | O_EXCL))) DEVOPTAB_SET_ERROR_AND_EXIT(EROFS);

    /* Get fixed path. */
    if (!(path = fatdev_get_fixed_path(r, path, fs_ctx))) DEVOPTAB_EXIT;

    //LOG_MSG_DEBUG("Opening \"%s\" with flags 0x%X (volume \"%s:\").", path, fatdev_flags, dev_ctx->name);

    /* Reset file descriptor. */
    memset(file, 0, sizeof(FFFIL));

    /* Open file. */
    res = f_open(file, path, fatdev_flags);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(fatdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int fatdev_close(struct _reent *r, void *fd)
{
    FRESULT res = FR_OK;

    FAT_DEV_INIT_FILE_VARS;

    //LOG_MSG_DEBUG("Closing file from \"%u:\" (volume \"%s:\").", file->obj.fs->pdrv, dev_ctx->name);

    /* Close file. */
    res = f_close(file);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(fatdev_translate_error(res));

    /* Reset file descriptor. */
    memset(file, 0, sizeof(FFFIL));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static ssize_t fatdev_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    UINT br = 0;
    FRESULT res = FR_OK;

    FAT_DEV_INIT_FILE_VARS;

    /* Sanity check. */
    if (!ptr || !len) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Check if the file was opened with read access. */
    if (!(file->flag & FA_READ)) DEVOPTAB_SET_ERROR_AND_EXIT(EBADF);

    //LOG_MSG_DEBUG("Reading 0x%lX byte(s) at offset 0x%lX from file in \"%u:\" (volume \"%s:\").", len, f_tell(file), ((FATFS*)dev_ctx->fs_ctx)->pdrv, dev_ctx->name);

    /* Read file data. */
    res = f_read(file, ptr, (UINT)len, &br);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(fatdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT((ssize_t)br);
}

static off_t fatdev_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    off_t offset = 0;
    FRESULT res = FR_OK;

    FAT_DEV_INIT_FILE_VARS;

    /* Find the offset to seek from. */
    switch(dir)
    {
        case SEEK_SET:  /* Set absolute position relative to zero (start offset). */
            break;
        case SEEK_CUR:  /* Set position relative to the current position. */
            offset = (off_t)f_tell(file);
            break;
        case SEEK_END:  /* Set position relative to EOF. */
            offset = (off_t)f_size(file);
            break;
        default:        /* Invalid option. */
            DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);
    }

    /* Don't allow negative seeks beyond the beginning of file. */
    if (pos < 0 && offset < -pos) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Calculate actual offset. */
    offset += pos;

    /* Don't allow positive seeks beyond the end of file. */
    if (offset > (off_t)f_size(file)) DEVOPTAB_SET_ERROR_AND_EXIT(EOVERFLOW);

    //LOG_MSG_DEBUG("Seeking to offset 0x%lX from file in \"%u:\" (volume \"%s:\").", offset, file->obj.fs->pdrv, dev_ctx->name);

    /* Perform file seek. */
    res = f_lseek(file, (FSIZE_t)offset);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(fatdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(offset);
}

static int fatdev_stat(struct _reent *r, const char *file, struct stat *st)
{
    FILINFO info = {0};
    FRESULT res = FR_OK;

    DEVOPTAB_INIT_VARS;
    FAT_DEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!st) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    /* Get fixed path. */
    if (!(file = fatdev_get_fixed_path(r, file, fs_ctx))) DEVOPTAB_EXIT;

    //LOG_MSG_DEBUG("Getting file stats for \"%s\" (volume \"%s:\").", file, dev_ctx->name);

    /* Get stats. */
    res = f_stat(file, &info);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(fatdev_translate_error(res));

    /* Fill stat info. */
    fatdev_fill_stat(st, &info);

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static DIR_ITER *fatdev_diropen(struct _reent *r, DIR_ITER *dirState, const char *path)
{
    FRESULT res = FR_OK;
    DIR_ITER *ret = NULL;

    FAT_DEV_INIT_DIR_VARS;
    FAT_DEV_INIT_FS_ACCESS;

    /* Get fixed path. */
    if (!(path = fatdev_get_fixed_path(r, path, fs_ctx))) DEVOPTAB_EXIT;

    //LOG_MSG_DEBUG("Opening directory \"%s\" (volume \"%s:\").", path, dev_ctx->name);

    /* Reset directory state. */
    memset(dir, 0, sizeof(FFDIR));

    /* Open directory. */
    res = f_opendir(dir, path);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(fatdev_translate_error(res));

    /* Update return value. */
    ret = dirState;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_PTR(ret);
}

static int fatdev_dirreset(struct _reent *r, DIR_ITER *dirState)
{
    FRESULT res = FR_OK;

    FAT_DEV_INIT_DIR_VARS;

    //LOG_MSG_DEBUG("Resetting state for directory in \"%u:\" (volume \"%s:\").", dir->obj.fs->pdrv, dev_ctx->name);

    /* Reset directory state. */
    res = f_rewinddir(dir);
    if (res != FR_OK) DEVOPTAB_SET_ERROR(fatdev_translate_error(res));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int fatdev_dirnext(struct _reent *r, DIR_ITER *dirState, char *filename, struct stat *filestat)
{
    FILINFO info = {0};
    FRESULT res = FR_OK;

    FAT_DEV_INIT_DIR_VARS;

    /* Sanity check. */
    if (!filename || !filestat) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    //LOG_MSG_DEBUG("Getting info from next directory entry in \"%u:\" (volume \"%s:\").", dir->obj.fs->pdrv, dev_ctx->name);

    /* Read directory. */
    res = f_readdir(dir, &info);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(fatdev_translate_error(res));

    /* Check if we haven't reached EOD. */
    /* FatFs returns an empty string if so. */
    if (info.fname[0])
    {
        /* Copy filename. */
        sprintf(filename, "%s", info.fname);

        /* Fill stat info. */
        fatdev_fill_stat(filestat, &info);
    } else {
        /* ENOENT signals EOD. */
        DEVOPTAB_SET_ERROR(ENOENT);
    }

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int fatdev_dirclose(struct _reent *r, DIR_ITER *dirState)
{
    FRESULT res = FR_OK;

    FAT_DEV_INIT_DIR_VARS;

    //LOG_MSG_DEBUG("Closing directory from \"%s:\" (volume \"%u:\").", dir->obj.fs->pdrv, dev_ctx->name);

    /* Close directory. */
    res = f_closedir(dir);
    if (res != FR_OK) DEVOPTAB_SET_ERROR_AND_EXIT(fatdev_translate_error(res));

    /* Reset directory state. */
    memset(dir, 0, sizeof(FFDIR));

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static int fatdev_statvfs(struct _reent *r, const char *path, struct statvfs *buf)
{
    NX_IGNORE_ARG(path);

    DEVOPTAB_INIT_VARS;
    FAT_DEV_INIT_FS_ACCESS;

    /* Sanity check. */
    if (!buf) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    //LOG_MSG_DEBUG("Getting filesystem stats for \"%u:\" (volume \"%s:\").", fs_ctx->pdrv, dev_ctx->name);

    /* Fill filesystem stats. */
    memset(buf, 0, sizeof(struct statvfs));

    buf->f_bsize = FF_MIN_SS;                                           /* Sector size. */
    buf->f_frsize = FF_MIN_SS;                                          /* Sector size. */
    buf->f_blocks = ((fs_ctx->n_fatent - 2) * (DWORD)fs_ctx->csize);    /* Total cluster count * cluster size in sectors. */
    buf->f_bfree = 0;
    buf->f_bavail = 0;
    buf->f_files = 0;
    buf->f_ffree = 0;
    buf->f_favail = 0;
    buf->f_fsid = 0;
    buf->f_flag = (ST_NOSUID | ST_RDONLY);
    buf->f_namemax = FF_LFN_BUF;

end:
    DEVOPTAB_DEINIT_VARS;
    DEVOPTAB_RETURN_INT(0);
}

static const char *fatdev_get_fixed_path(struct _reent *r, const char *path, FATFS *fatfs)
{
    DEVOPTAB_INIT_ERROR_STATE;

    const u8 *p = (const u8*)path;
    ssize_t units = 0;
    u32 code = 0;
    size_t len = 0;
    char name[DEVOPTAB_MOUNT_NAME_LENGTH] = {0};

    if (!r || !path || !*path || !fatfs) DEVOPTAB_SET_ERROR_AND_EXIT(EINVAL);

    //LOG_MSG_DEBUG("Input path: \"%s\".", path);

    /* Generate FatFs mount name ID. */
    snprintf(name, sizeof(name), "%u:", fatfs->pdrv);

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
    len = (strlen(name) + strlen(path));
    if (len >= sizeof(g_fatDevicePathBuffer)) DEVOPTAB_SET_ERROR_AND_EXIT(ENAMETOOLONG);

    /* Generate fixed path. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
    snprintf(g_fatDevicePathBuffer, MAX_ELEMENTS(g_fatDevicePathBuffer), "%s%s", name, path);
#pragma GCC diagnostic pop

    //LOG_MSG_DEBUG("Fixed path: \"%s\".", g_fatDevicePathBuffer);

end:
    DEVOPTAB_RETURN_PTR(g_fatDevicePathBuffer);
}

static void fatdev_fill_stat(struct stat *st, const FILINFO *info)
{
    /* Clear stat struct. */
    memset(st, 0, sizeof(struct stat));

    /* Fill stat struct. */
    st->st_nlink = 1;

    if (info->fattrib & AM_DIR)
    {
        /* We're dealing with a directory entry. */
        st->st_mode = (S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH);
    } else {
        /* We're dealing with a file entry. */
        st->st_size = (off_t)info->fsize;
        st->st_mode = (S_IFREG | S_IRUSR | S_IRGRP | S_IROTH);
    }

    /* Convert FAT timestamps into POSIX timestamps. */
    //fatdev_time_fat2posix(info->acdate, info->actime, &(st->st_atim.tv_sec));
    fatdev_time_fat2posix(info->fdate, info->ftime, &(st->st_mtim.tv_sec));
    fatdev_time_fat2posix(info->crdate, info->crtime, &(st->st_ctim.tv_sec));

    /* Store FAT-specific file flags. */
    st->st_spare4[0] = (long)info->fattrib;

    //LOG_MSG_DEBUG("DOS timestamp: 0x%04X%04X. Generated POSIX timestamp: %lu.", info->fdate, info->ftime, st->st_mtime);
}

static void fatdev_time_fat2posix(const WORD fat_date, const WORD fat_time, time_t *out_posix_ts)
{
    if (!out_posix_ts) return;

    struct tm timeinfo = {0};

    /* Convert date/time into an actual UTC POSIX timestamp using the system local time. */
    timeinfo.tm_year = (((fat_date >> 9) & 0x7F) + 80); /* DOS time: offset since 1980. POSIX time: offset since 1900. */
    timeinfo.tm_mon = (((fat_date >> 5) & 0xF) - 1);    /* DOS time: 1-12 range (inclusive). POSIX time: 0-11 range (inclusive). */
    timeinfo.tm_mday = (fat_date & 0x1F);
    timeinfo.tm_hour = ((fat_time >> 11) & 0x1F);
    timeinfo.tm_min = ((fat_time >> 5) & 0x3F);
    timeinfo.tm_sec = ((fat_time & 0x1F) << 1);         /* DOS time: 2-second intervals with a 0-29 range (inclusive, 58 seconds max). POSIX time: 0-59 range (inclusive). */

    *out_posix_ts = mktime(&timeinfo);

    /*LOG_MSG_DEBUG("Converted DOS timestamp 0x%04X%04X (%u-%02u-%02u %02u:%02u:%02u) into POSIX timestamp %lu.", fat_date, fat_time, timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, \
                                                                                                                timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, \
                                                                                                                *out_posix_ts);*/
}

static int fatdev_translate_error(FRESULT res)
{
    int ret;

    switch(res)
    {
        case FR_OK:
            ret = 0;
            break;
        case FR_DISK_ERR:
        case FR_NOT_READY:
            ret = EIO;
            break;
        case FR_INT_ERR:
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            ret = EINVAL;
            break;
        case FR_NO_FILE:
        case FR_NO_PATH:
            ret = ENOENT;
            break;
        case FR_DENIED:
            ret = EACCES;
            break;
        case FR_EXIST:
            ret = EEXIST;
            break;
        case FR_INVALID_OBJECT:
            ret = EFAULT;
            break;
        case FR_WRITE_PROTECTED:
            ret = EROFS;
            break;
        case FR_INVALID_DRIVE:
            ret = ENODEV;
            break;
        case FR_NOT_ENABLED:
            ret = ENOEXEC;
            break;
        case FR_NO_FILESYSTEM:
            ret = ENFILE;
            break;
        case FR_TIMEOUT:
            ret = EAGAIN;
            break;
        case FR_LOCKED:
            ret = EBUSY;
            break;
        case FR_NOT_ENOUGH_CORE:
            ret = ENOMEM;
            break;
        case FR_TOO_MANY_OPEN_FILES:
            ret = EMFILE;
            break;
        default:
            ret = EPERM;
            break;
    }

    //LOG_MSG_DEBUG("FRESULT: %u. Translated errno: %d.", res, ret);

    return ret;
}
