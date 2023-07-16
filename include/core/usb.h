/*
 * usb.h
 *
 * Heavily based in usb_comms from libnx.
 *
 * Copyright (c) 2018-2020, Switchbrew and libnx contributors.
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __USB_H__
#define __USB_H__

#ifdef __cplusplus
extern "C" {
#endif

#define USB_TRANSFER_BUFFER_SIZE    0x800000    /* 8 MiB. */

/// Used to indicate the USB speed selected by the host device.
typedef enum {
    UsbHostSpeed_None       = 0,
    UsbHostSpeed_FullSpeed  = 1,    ///< USB 1.x.
    UsbHostSpeed_HighSpeed  = 2,    ///< USB 2.0.
    UsbHostSpeed_SuperSpeed = 3,    ///< USB 3.0.
    UsbHostSpeed_Count      = 4     ///< Total values supported by this enum.
} UsbHostSpeed;

/// Initializes the USB interface, input and output endpoints and allocates an internal transfer buffer.
bool usbInitialize(void);

/// Closes the USB interface, input and output endpoints and frees the transfer buffer.
void usbExit(void);

/// Returns a pointer to a dynamically allocated, page aligned memory buffer that's suitable for USB transfers.
void *usbAllocatePageAlignedBuffer(size_t size);

/// Used to check if the console has been connected to a USB host device and if a valid USB session has been established.
/// Returns a value from the UsbHostSpeed enum.
u8 usbIsReady(void);

/// Sends file properties to the host device before starting a file data transfer. If needed, it must be called before usbSendFileData().
/// 'file_size' may be zero if an empty file shall be created, in which case no file data transfer will be necessary.
/// Calling this function before finishing an ongoing file data transfer will result in an error.
/// Under NSP transfer mode, this function must be called right before transferring data from each NSP file entry to the host device, which should in turn write it all to the same output file.
bool usbSendFileProperties(u64 file_size, const char *filename);

/// Sends NSP properties to the host device and enables NSP transfer mode. If needed, it must be called before usbSendFileData().
/// Both 'nsp_size' and 'nsp_header_size' must be greater than zero. 'nsp_size' must also be greater than 'nsp_header_size'.
/// Calling this function after NSP transfer mode has already been enabled will result in an error.
/// The host device should immediately write 'nsp_header_size' padding at the start of the output file and start listening for further usbSendFileProperties() calls, or a usbSendNspHeader() call.
bool usbSendNspProperties(u64 nsp_size, const char *filename, u32 nsp_header_size);

/// Performs a file data transfer. Must be continuously called after usbSendFileProperties() / usbSendNspProperties() until all file data has been transferred.
/// Data chunk size must not exceed USB_TRANSFER_BUFFER_SIZE.
/// If the last file data chunk is aligned to the endpoint max packet size, the host device should expect a Zero Length Termination (ZLT) packet.
/// Calling this function if there's no remaining data to transfer will result in an error.
bool usbSendFileData(void *data, u64 data_size);

/// Used to gracefully cancel an ongoing file transfer. The current USB session is kept alive.
void usbCancelFileTransfer(void);

/// Sends NSP header data to the host device, making it rewind the NSP file pointer to write this data, essentially finishing the NSP transfer process.
/// Must be called after the data from all NSP file entries has been transferred using both usbSendNspProperties() and usbSendFileData() calls.
/// If the NSP header size is aligned to the endpoint max packet size, the host device should expect a Zero Length Termination (ZLT) packet.
bool usbSendNspHeader(void *nsp_header, u32 nsp_header_size);

#ifdef __cplusplus
}
#endif

#endif /* __USB_H__ */
