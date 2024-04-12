/*
 * ro_dev.c
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

#include "nxdt_utils.h"
#include "nxdt_devoptab.h"

ssize_t rodev_write(struct _reent *r, void *fd, const char *ptr, size_t len)
{
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(ptr);
    NX_IGNORE_ARG(len);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_link(struct _reent *r, const char *existing, const char *newLink)
{
    NX_IGNORE_ARG(existing);
    NX_IGNORE_ARG(newLink);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_unlink(struct _reent *r, const char *name)
{
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_chdir(struct _reent *r, const char *name)
{
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_rename(struct _reent *r, const char *oldName, const char *newName)
{
    NX_IGNORE_ARG(oldName);
    NX_IGNORE_ARG(newName);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_mkdir(struct _reent *r, const char *path, int mode)
{
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(mode);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_ftruncate(struct _reent *r, void *fd, off_t len)
{
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(len);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_fsync(struct _reent *r, void *fd)
{
    NX_IGNORE_ARG(fd);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_chmod(struct _reent *r, const char *path, mode_t mode)
{
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(mode);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_fchmod(struct _reent *r, void *fd, mode_t mode)
{
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(mode);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_rmdir(struct _reent *r, const char *name)
{
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_utimes(struct _reent *r, const char *filename, const struct timeval times[2])
{
    NX_IGNORE_ARG(filename);
    NX_IGNORE_ARG(times);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

long rodev_fpathconf(struct _reent *r, void *fd, int name)
{
    NX_IGNORE_ARG(fd);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

long rodev_pathconf(struct _reent *r, const char *path, int name)
{
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(name);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

int rodev_symlink(struct _reent *r, const char *target, const char *linkpath)
{
    NX_IGNORE_ARG(target);
    NX_IGNORE_ARG(linkpath);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}

ssize_t rodev_readlink(struct _reent *r, const char *path, char *buf, size_t bufsiz)
{
    NX_IGNORE_ARG(path);
    NX_IGNORE_ARG(buf);
    NX_IGNORE_ARG(bufsiz);

    DEVOPTAB_RETURN_UNSUPPORTED_OP;
}
