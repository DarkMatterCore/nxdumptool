/*
 * bis_storage.h
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

#ifndef __BIS_STORAGE_H__
#define __BIS_STORAGE_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Mounts the eMMC partitions with IDs `CalibrationFile` (28) through `System` (31) and makes it possible to perform read-only FS operations with them.
/// The mount name for each partition can be retrieved via bisStorageGetMountNameByBisPartitionId().
bool bisStorageInitialize(void);

/// Unmounts all previously mounted eMMC partitions.
void bisStorageExit(void);

/// Returns a pointer to a string that holds the GPT partition name for the provided eMMC BIS partition ID (e.g. FsBisPartitionId_CalibrationFile -> "PRODINFOF").
/// Only eMMC BIS partition IDs `CalibrationFile` (28) through `System` (31) are supported.
/// Returns NULL if the eMMC BIS storage interface hasn't been initialized yet, or if an unsupported eMMC BIS partition ID is provided.
const char *bisStorageGetGptPartitionNameByBisPartitionId(u8 bis_partition_id);

/// Returns a pointer to a string that holds the SystemInitializer partition name for the provided eMMC BIS partition ID (e.g. FsBisPartitionId_CalibrationFile -> "CalibrationFile").
/// Only eMMC BIS partition IDs `CalibrationFile` (28) through `System` (31) are supported.
/// Returns NULL if the eMMC BIS storage interface hasn't been initialized yet, or if an unsupported eMMC BIS partition ID is provided.
const char *bisStorageGetSystemInitializerPartitionNameByBisPartitionId(u8 bis_partition_id);

/// Returns a pointer to a string that holds the mount name for the provided eMMC BIS partition ID (e.g. FsBisPartitionId_CalibrationFile -> "bisprodinfof").
/// This can be used to perform read-only FS operations on a specific partition.
/// Only eMMC BIS partition IDs `CalibrationFile` (28) through `System` (31) are supported.
/// Returns NULL if the eMMC BIS storage interface hasn't been initialized yet, or if an unsupported eMMC BIS partition ID is provided.
const char *bisStorageGetMountNameByBisPartitionId(u8 bis_partition_id);

/// Returns a pointer to a FsStorage object that matches the provided FatFs drive number, or NULL if it hasn't been mounted.
/// Only used by FatFs's diskio operations.
FsStorage *bisStorageGetFsStorageByFatFsDriveNumber(u8 drive_number);

/// (Un)locks the BIS storage mutex. Can be used to block other threads and prevent them from altering the internal status of this interface.
/// Use with caution.
void bisStorageControlMutex(bool lock);

#ifdef __cplusplus
}
#endif

#endif /* __BIS_STORAGE_H__ */
