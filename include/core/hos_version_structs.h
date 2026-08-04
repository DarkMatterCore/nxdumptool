/*
 * hos_version_structs.h
 *
 * Copyright (c) 2020-2026, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __HOS_VERSION_STRUCTS_H__
#define __HOS_VERSION_STRUCTS_H__

#ifdef __cplusplus
extern "C" {
#endif

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

/// Used by the SystemVersion title file (0100000000000809).
typedef struct {
    u8 major;
    u8 minor;
    u8 micro;
    u8 reserved_1;
    u8 rev_major;
    u8 rev_minor;
    u8 reserved_2[0x2];
    char platform[0x20];        ///< e.g. "NX".
    char hash[0x40];            ///< e.g. "52971eebbba7ab9e6e23d73753aa63e0c3794b16".
    char display_version[0x18]; ///< e.g. "19.0.0".
    char display_title[0x80];   ///< e.g. "NintendoSDK Firmware for NX 19.0.0-4.0".
} SystemVersionFile;

NXDT_ASSERT(SystemVersionFile, 0x100);

#ifdef __cplusplus
}
#endif

#endif  /* __HOS_VERSION_STRUCTS_H__ */
