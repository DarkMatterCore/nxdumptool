/**
 * @file fspusb.h
 * @brief USB filesystem extension (fsp-usb) service IPC wrapper.
 * @author XorTroll
 * @copyright libnx Authors
 */
#pragma once

#ifndef __FSPUSB_H__
#define __FSPUSB_H__

#include <switch.h>

/// This is basically FATFS' file system types.
typedef enum {
    FspUsbFileSystemType_FAT12 = 1,
    FspUsbFileSystemType_FAT16 = 2,
    FspUsbFileSystemType_FAT32 = 3,
    FspUsbFileSystemType_exFAT = 4,
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
