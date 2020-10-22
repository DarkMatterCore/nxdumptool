/*
 * common.h
 *
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

#pragma once

#ifndef __COMMON_H__
#define __COMMON_H__

#define FS_SYSMODULE_TID        (u64)0x0100000000000000
#define BOOT_SYSMODULE_TID      (u64)0x0100000000000005
#define SPL_SYSMODULE_TID       (u64)0x0100000000000028
#define ES_SYSMODULE_TID        (u64)0x0100000000000033
#define SYSTEM_UPDATE_TID       (u64)0x0100000000000816

#define FAT32_FILESIZE_LIMIT    (u64)0xFFFFFFFF         /* 4 GiB - 1 (4294967295 bytes). */

/// Used to store version numbers expressed in dot notation: "{major}.{minor}.{micro}-{major_relstep}.{minor_relstep}".
/// Referenced by multiple header files.
typedef struct {
    union {
        struct {
            u32 minor_relstep : 8;
            u32 major_relstep : 8;
            u32 micro         : 4;
            u32 minor         : 6;
            u32 major         : 6;
        };
        u32 value;
    };
} VersionType1;

/// Used to store version numbers expressed in dot notation: "{major}.{minor}.{micro}-{relstep}".
/// Only used by GameCardFwMode and NcaSdkAddOnVersion.
typedef struct {
    union {
        struct {
            u32 relstep : 8;
            u32 micro   : 8;
            u32 minor   : 8;
            u32 major   : 8;
        };
        u32 value;
    };
} VersionType2;

#endif /* __COMMON_H__ */
