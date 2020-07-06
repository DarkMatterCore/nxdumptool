/*
 * fspusb.h
 *
 * Copyright (c) 2019-2020, XorTroll.
 * Copyright (c) 2019-2020, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __FSPUSB_H__
#define __FSPUSB_H__

/// This is basically FatFs' file system types.
typedef enum {
    FspUsbFileSystemType_FAT12 = 1,
    FspUsbFileSystemType_FAT16 = 2,
    FspUsbFileSystemType_FAT32 = 3,
    FspUsbFileSystemType_exFAT = 4
} FspUsbFileSystemType;

/// Initialize fsp-usb.
Result fspusbInitialize(void);

/// Exit fsp-usb.
void fspusbExit(void);

/// Gets the Service object for the actual fsp-usb service session.
Service* fspusbGetServiceSession(void);

Result fspusbListMountedDrives(s32 *drives_buf, size_t drive_count, s32 *out_total);
Result fspusbGetDriveFileSystemType(s32 interface_id, FspUsbFileSystemType *out_type);
Result fspusbGetDriveLabel(s32 interface_id, char *out_label, size_t out_label_size);
Result fspusbSetDriveLabel(s32 interface_id, const char *label);
Result fspusbOpenDriveFileSystem(s32 interface_id, FsFileSystem *out_fs);

#endif /* __FSPUSB_H__ */
