/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __USB_H__
#define __USB_H__

#include <switch.h>

#define USB_TRANSFER_ALIGNMENT      0x1000      /* 4 KiB */
#define USB_TRANSFER_BUFFER_SIZE    0x800000    /* 8 MiB */

/// Initializes the USB interface, input and output endpoints and allocates an internal transfer buffer.
bool usbInitialize(void);

/// Closes the USB interface, input and output endpoints and frees the transfer buffer.
void usbExit(void);

/// Checks if the console is currently connected to a host device.
bool usbIsHostAvailable(void);

/// Performs a handshake with the host device. Returns true if the host device replies with valid data.
bool usbPerformHandshake(void);

/// Sends file properties to the host device before starting a file data transfer. Must be called before usbSendFileData().
bool usbSendFileProperties(u64 file_size, const char *filename);

/// Performs a file data transfer. Must be called after usbSendFileProperties().
/// Data chunk size must not exceed USB_TRANSFER_BUFFER_SIZE.
bool usbSendFileData(void *data, u32 data_size);

#endif /* __USB_H__ */
