/*
 * nxdt_includes.h
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

#ifndef __NXDT_INCLUDES_H__
#define __NXDT_INCLUDES_H__

/* C headers. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/param.h>
#include <dirent.h>
#include <assert.h>
#include <unistd.h>

#ifndef __cplusplus
#include <stdatomic.h>
#else
#include <atomic>
#define _Atomic(X) std::atomic< X >
#endif

/* libnx header. */
#include <switch.h>

/* Internet operations. */
#include <arpa/inet.h>

/* Global defines. */
#include "../defines.h"

/* File/socket based logger. */
#include "nxdt_log.h"

/* Configuration handler. */
#include "config.h"

/* HTTP requests handler. */
#include "http.h"

/* USB Mass Storage support. */
#include "ums.h"

/* SHA3 checksum calculator. */
#include "sha3.h"

/* LZ4 (dec)compression. */
#define LZ4_STATIC_LINKING_ONLY /* Required by LZ4 to enable in-place decompression. */
#include "lz4.h"

/// Used to store version numbers expressed in dot notation: "{major}.{minor}.{micro}-{major_relstep}.{minor_relstep}".
/// Used by system version fields. 16-bit long relstep values were used by system version fields prior to HOS 3.0.0.
typedef struct {
    union {
        u32 value;
        struct {
            union {
                u16 relstep;
                struct {
                    u16 minor_relstep : 8;
                    u16 major_relstep : 8;
                };
            };
            u16 micro                 : 4;
            u16 minor                 : 6;
            u16 major                 : 6;
        };
    };
} SystemVersion;

NXDT_ASSERT(SystemVersion, 0x4);

/// Used to store version numbers expressed in dot notation: "{release}.{private}".
/// Used by application version fields.
typedef struct {
    union {
        u32 value;
        struct {
            u32 private_ver : 16;
            u32 release_ver : 16;
        };
    };
} ApplicationVersion;

NXDT_ASSERT(ApplicationVersion, 0x4);

/// Used to store version numbers expressed in dot notation: "{major}.{minor}.{micro}-{relstep}".
/// Used by SDK version fields.
typedef struct {
    union {
        u32 value;
        struct {
            u32 relstep : 8;
            u32 micro   : 8;
            u32 minor   : 8;
            u32 major   : 8;
        };
    };
} SdkAddOnVersion;

NXDT_ASSERT(SdkAddOnVersion, 0x4);

/// Convenient wrapper for all version structs.
typedef struct {
    union {
        u32 value;
        SystemVersion system_version;
        ApplicationVersion application_version;
        SdkAddOnVersion sdk_addon_version;
    };
} Version;

NXDT_ASSERT(Version, 0x4);

#endif /* __NXDT_INCLUDES_H__ */
