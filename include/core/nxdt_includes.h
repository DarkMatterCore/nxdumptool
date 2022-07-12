/*
 * nxdt_includes.h
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

/// Used to store version numbers expressed in dot notation:
///     * System version: "{major}.{minor}.{micro}-{major_relstep}.{minor_relstep}".
///     * Application version: "{release}.{private}".
/// Referenced by multiple header files.
typedef struct {
    union {
        u32 value;
        struct {
            u32 minor_relstep : 8;
            u32 major_relstep : 8;
            u32 micro         : 4;
            u32 minor         : 6;
            u32 major         : 6;
        } system_version;
        struct {
            u32 private_ver   : 16;
            u32 release_ver   : 16;
        } application_version;
    };
} Version;

NXDT_ASSERT(Version, 0x4);

/// Used to store version numbers expressed in dot notation: "{major}.{minor}.{micro}-{relstep}".
/// Only used by GameCardFwMode and NcaSdkAddOnVersion.
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

#endif /* __NXDT_INCLUDES_H__ */
