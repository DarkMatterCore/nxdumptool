/*
 * ums.h
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __UMS_H__
#define __UMS_H__

#include <usbhsfs.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Initializes the USB Mass Storage interface.
bool umsInitialize(void);

/// Closes the USB Mass Storage interface.
void umsExit(void);

/// Returns true if USB Mass Storage device info has been updated.
bool umsIsDeviceInfoUpdated(void);

/// Returns a pointer to a dynamically allocated array of UsbHsFsDevice elements. The allocated buffer must be freed by the calling function.
/// Returns NULL if an error occurs.
UsbHsFsDevice *umsGetDevices(u32 *out_count);

/// Unmounts a USB Mass Storage device using a UsbHsFsDevice element.
/// If successful, USB Mass Storage device info will be automatically reloaded, and the next call to umsIsDeviceInfoUpdated() shall return true.
bool umsUnmountDevice(const UsbHsFsDevice *device);

#ifdef __cplusplus
}
#endif

#endif /* __UMS_H__ */
