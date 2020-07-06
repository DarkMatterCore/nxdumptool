/*
 * usb.h
 *
 * Heavily based in usb_comms from libnx.
 *
 * Copyright (c) 2018-2020, Switchbrew and libnx contributors.
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

#ifndef __USB_H__
#define __USB_H__

#define USB_TRANSFER_BUFFER_SIZE    0x800000    /* 8 MiB. */

/// Initializes the USB interface, input and output endpoints and allocates an internal transfer buffer.
bool usbInitialize(void);

/// Closes the USB interface, input and output endpoints and frees the transfer buffer.
void usbExit(void);

/// Returns a pointer to a heap-allocated, page-aligned memory buffer that's suitable for USB transfers.
void *usbAllocatePageAlignedBuffer(size_t size);

/// Used to check if the console has been connected to an USB host device and if a valid USB session has been established.
/// Bear in mind this call will block the calling thread if the console is connected to an USB host device but no USB session has been established.
/// If the console is disconnected during this block, the function will return false.
/// If the console isn't connected to an USB host device when this function is called, false will be returned right away.
bool usbIsReady(void);

/// Sends file properties to the host device before starting a file data transfer. Must be called before usbSendFileData().
bool usbSendFileProperties(u64 file_size, const char *filename);

/// Performs a file data transfer. Must be continuously called after usbSendFileProperties() until all file data has been transferred.
/// Data chunk size must not exceed USB_TRANSFER_BUFFER_SIZE.
bool usbSendFileData(void *data, u64 data_size);

#endif /* __USB_H__ */
