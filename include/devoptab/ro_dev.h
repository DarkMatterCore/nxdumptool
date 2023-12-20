/*
 * ro_dev.h
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

#pragma once

#ifndef __RO_DEV__
#define __RO_DEV__

#ifdef __cplusplus
extern "C" {
#endif

/* Bogus functions for devoptab write operations -- all of these set errno to ENOSYS and return -1. */
/* We don't provide support for relative directories, so chdir is discarded as well. */

ssize_t rodev_write(struct _reent *r, void *fd, const char *ptr, size_t len);
int     rodev_link(struct _reent *r, const char *existing, const char *newLink);
int     rodev_unlink(struct _reent *r, const char *name);
int     rodev_chdir(struct _reent *r, const char *name);
int     rodev_rename(struct _reent *r, const char *oldName, const char *newName);
int     rodev_mkdir(struct _reent *r, const char *path, int mode);
int     rodev_ftruncate(struct _reent *r, void *fd, off_t len);
int     rodev_fsync(struct _reent *r, void *fd);
int     rodev_chmod(struct _reent *r, const char *path, mode_t mode);
int     rodev_fchmod(struct _reent *r, void *fd, mode_t mode);
int     rodev_rmdir(struct _reent *r, const char *name);
int     rodev_utimes(struct _reent *r, const char *filename, const struct timeval times[2]);
long    rodev_fpathconf(struct _reent *r, void *fd, int name);
long    rodev_pathconf(struct _reent *r, const char *path, int name);
int     rodev_symlink(struct _reent *r, const char *target, const char *linkpath);
ssize_t rodev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz);

#ifdef __cplusplus
}
#endif

#endif /* __RO_DEV__ */
