/*
 * defines.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __DEFINES_H__
#define __DEFINES_H__

/* Broadly useful language defines. */

#define MEMBER_SIZE(type, member)       sizeof(((type*)NULL)->member)

#define MAX_ELEMENTS(x)                 ((sizeof((x))) / (sizeof((x)[0])))

#define BIT_LONG(n)                     (1UL << (n))

#define ALIGN_UP(x, y)                  (((x) + ((y) - 1)) & ~((y) - 1))
#define ALIGN_DOWN(x, y)                ((x) & ~((y) - 1))
#define IS_ALIGNED(x, y)                (((x) & ((y) - 1)) == 0)

#define IS_POWER_OF_TWO(x)              (((x) & ((x) - 1)) == 0)

#define CONCATENATE_IMPL(s1, s2)        s1##s2
#define CONCATENATE(s1, s2)             CONCATENATE_IMPL(s1, s2)

#define ANONYMOUS_VARIABLE(pref)        CONCATENATE(pref, __LINE__)

#define NON_COPYABLE(cls) \
    cls(const cls&) = delete; \
    cls& operator=(const cls&) = delete

#define ALWAYS_INLINE                   inline __attribute__((always_inline))
#define ALWAYS_INLINE_LAMBDA            __attribute__((always_inline))

#define CLEANUP(func)                   __attribute__((__cleanup__(func)))

#define NXDT_ASSERT(name, size)         static_assert(sizeof(name) == (size), "Bad size for " #name "! Expected " #size ".")

/* Global constants used throughout the application. */

#define FS_SYSMODULE_TID                (u64)0x0100000000000000
#define BOOT_SYSMODULE_TID              (u64)0x0100000000000005
#define SPL_SYSMODULE_TID               (u64)0x0100000000000028
#define ES_SYSMODULE_TID                (u64)0x0100000000000033
#define SYSTEM_UPDATE_TID               (u64)0x0100000000000816
#define QLAUNCH_TID                     (u64)0x0100000000001000

#define FAT32_FILESIZE_LIMIT            (u64)0xFFFFFFFF                                                                         /* 4 GiB - 1 (4294967295 bytes). */

#define UTF8_BOM                        "\xEF\xBB\xBF"
#define CRLF                            "\r\n"

#define HBMENU_BASE_PATH                "/switch/"
#define APP_BASE_PATH                   HBMENU_BASE_PATH APP_TITLE "/"

#define GAMECARD_PATH                   APP_BASE_PATH "Gamecard/"
#define CERT_PATH                       APP_BASE_PATH "Certificate/"
#define HFS_PATH                        APP_BASE_PATH "HFS/"
#define NSP_PATH                        APP_BASE_PATH "NSP/"
#define TICKET_PATH                     APP_BASE_PATH "Ticket/"
#define NCA_PATH                        APP_BASE_PATH "NCA/"
#define NCA_FS_PATH                     APP_BASE_PATH "NCA FS/"

#define CONFIG_PATH                     "sdmc:" APP_BASE_PATH "config.json"
#define DEFAULT_CONFIG_PATH             "romfs:/default_config.json"

#define NRO_NAME                        APP_TITLE ".nro"
#define NRO_PATH                        APP_BASE_PATH NRO_NAME

#define NSWDB_XML_PATH                  APP_BASE_PATH "NSWreleases.xml"

#define KEYS_FILE_PATH                  "sdmc:" HBMENU_BASE_PATH "prod.keys"                                                    /* Location used by Lockpick_RCM. */

#define LOG_FILE_NAME                   APP_TITLE ".log"
#define LOG_BUF_SIZE                    0x400000                                                                                /* 4 MiB. */
#define LOG_FORCE_FLUSH                 0                                                                                       /* Forces a log buffer flush each time the logfile is written to. */

#define NXLINK_TIMEOUT                  2000

#define BIS_SYSTEM_PARTITION_MOUNT_NAME "sys:"

#define GITHUB_REPOSITORY_URL           "https://github.com/DarkMatterCore/nxdumptool"
#define GITHUB_NEW_ISSUE_URL            GITHUB_REPOSITORY_URL "/issues/new/choose"

#define BOREALIS_URL                    "https://github.com/natinusala/borealis"
#define LIBUSBHSFS_URL                  "https://github.com/DarkMatterCore/libusbhsfs"
#define FATFS_URL                       "http://elm-chan.org/fsw/ff/00index_e.html"
#define LZ4_URL                         "https://github.com/lz4/lz4"

#define DISCORD_SERVER_URL              "https://discord.gg/SCbbcQx"

#define LOCKPICK_RCM_URL                "https://github.com/shchmue/Lockpick_RCM"

#endif  /* __DEFINES_H__ */
