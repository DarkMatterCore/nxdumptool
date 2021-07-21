/*
 * config.h
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

#ifndef __CONFIG_H__
#define __CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ConfigDumpDestination_SdCard  = 0,
    ConfigDumpDestination_UsbHost = 1,
    ConfigDumpDestination_Count   = 2
} ConfigDumpDestination;

typedef enum {
    ConfigChecksumLookupMethod_None    = 0,
    ConfigChecksumLookupMethod_NSWDB   = 1,
    ConfigChecksumLookupMethod_NoIntro = 2,
    ConfigChecksumLookupMethod_Count   = 3
} ConfigChecksumLookupMethod;

/// Initializes the configuration interface.
bool configInitialize(void);

/// Closes the configuration interface.
void configExit(void);

/// Getters and setters for various data types.
/// Path elements must be separated using forward slashes.

bool configGetBoolean(const char *path);
void configSetBoolean(const char *path, bool value);

int configGetInteger(const char *path);
void configSetInteger(const char *path, int value);

#ifdef __cplusplus
}
#endif

#endif /* __CONFIG_H__ */
