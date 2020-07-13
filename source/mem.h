/*
 * mem.h
 *
 * Copyright (c) 2019, shchmue.
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

#ifndef __MEM_H__
#define __MEM_H__

#define FS_SYSMODULE_TID    (u64)0x0100000000000000
#define BOOT_SYSMODULE_TID  (u64)0x0100000000000005
#define SPL_SYSMODULE_TID   (u64)0x0100000000000028

typedef enum {
    MemoryProgramSegmentType_Text   = BIT(0),
    MemoryProgramSegmentType_Rodata = BIT(1),
    MemoryProgramSegmentType_Data   = BIT(2)
} MemoryProgramSegmentType;

typedef struct {
    u64 program_id;
    u8 mask;        ///< MemoryProgramSegmentType. Used with memRetrieveProgramMemorySegment(). Ignored in memRetrieveFullProgramMemory().
    u8 *data;
    u64 data_size;
} MemoryLocation;

/// Retrieves memory segment (.text, .rodata, .data) data from a running program.
/// These are memory pages with read permission (Perm_R) enabled and type MemType_CodeStatic or MemType_CodeMutable.
bool memRetrieveProgramMemorySegment(MemoryLocation *location);

/// Retrieves full memory data from a running program.
/// These are any type of memory pages with read permission (Perm_R) enabled.
/// MemType_Unmapped, MemType_Io, MemType_ThreadLocal and MemType_Reserved memory pages are excluded, as well as memory pages with a populated MemoryAttribute value.
bool memRetrieveFullProgramMemory(MemoryLocation *location);

/// Frees a populated MemoryLocation element.
NX_INLINE void memFreeMemoryLocation(MemoryLocation *location)
{
    if (!location) return;
    if (location->data) free(location->data);
    location->data = NULL;
    location->data_size = 0;
}

#endif /* __MEM_H__ */
