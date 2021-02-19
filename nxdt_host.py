#!/usr/bin/env python3

"""
 * nxdt_host.py
 *
 * Copyright (c) 2021, sigmaboy.
 * Copyright (c) 2021, DarkMatterCore <pabloacurielz@gmail.com>.
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
"""

# This script depends on PyUSB and tdqm.
# You can install both with `pip install pyusb tqdm`.

import os
import usb.core
import usb.util
import struct
import array
import sys
import time
import threading
import shutil
from tqdm import tqdm

# Script version.
SCRIPT_VERSION = '0.1'

# USB VID/PID pair.
USB_DEV_VID = 0x057E
USB_DEV_PID = 0x3000

# USB manufacturer and product strings.
USB_DEV_MANUFACTURER = 'DarkMatterCore'
USB_DEV_PRODUCT = 'nxdumptool'

# USB timeout (milliseconds).
USB_TRANSFER_TIMEOUT = 5000

# USB transfer block size.
USB_TRANSFER_BLOCK_SIZE = 0x800000

# USB command header/status magic word.
USB_MAGIC_WORD = b'NXDT'

# Supported USB ABI version.
USB_ABI_VERSION = 1

# USB command header size.
USB_CMD_HEADER_SIZE = 0x10

# USB command IDs.
USB_CMD_START_SESSION        = 0
USB_CMD_SEND_FILE_PROPERTIES = 1
USB_CMD_CANCEL_FILE_TRANSFER = 2
USB_CMD_SEND_NSP_HEADER      = 3
USB_CMD_END_SESSION          = 4

# USB command block sizes.
USB_CMD_BLOCK_SIZE_START_SESSION        = 0x10
USB_CMD_BLOCK_SIZE_SEND_FILE_PROPERTIES = 0x320

# Max filename length (file properties).
USB_FILE_PROPERTIES_MAX_NAME_LENGTH = 0x300

# USB status codes.
USB_STATUS_SUCCESS                 = 0
USB_STATUS_INVALID_MAGIC_WORD      = 4
USB_STATUS_UNSUPPORTED_CMD         = 5
USB_STATUS_UNSUPPORTED_ABI_VERSION = 6
USB_STATUS_MALFORMED_CMD           = 7
USB_STATUS_HOST_IO_ERROR           = 8

# Global variables.
g_usbEpIn = None
g_usbEpOut = None
g_usbEpMaxPacketSize = 0

g_nxdtVersionMajor = 0
g_nxdtVersionMinor = 0
g_nxdtVersionMicro = 0
g_nxdtAbiVersion   = 0

g_nspTransferMode = False
g_nspSize = 0
g_nspHeaderSize = 0
g_nspRemainingSize = 0
g_nspFile = None
g_nspFilePath = None

g_outputDir = None

def utilsIsValueAlignedToEndpointPacketSize(value):
    global g_usbEpMaxPacketSize
    return bool((value & (g_usbEpMaxPacketSize - 1)) == 0)

def utilsResetNspInfo():
    global g_nspTransferMode, g_nspSize, g_nspHeaderSize, g_nspRemainingSize, g_nspFile, g_nspFilePath
    
    # Reset NSP transfer mode info.
    g_nspTransferMode = False
    g_nspSize = 0
    g_nspHeaderSize = 0
    g_nspRemainingSize = 0
    g_nspFile = None
    g_nspFilePath = None

def utilsGetSizeUnitAndDivisor(size):
    size_suffixes = [ 'B', 'KiB', 'MiB', 'GiB' ]
    size_suffixes_count = len(size_suffixes)
    
    float_size = float(size)
    ret = None
    
    for i in range(size_suffixes_count):
        if (float_size < pow(1024, i + 1)) or ((i + 1) >= size_suffixes_count):
            ret = (size_suffixes[i], pow(1024, i))
            break
    
    return ret

def usbGetDeviceEndpoints():
    global g_usbEpIn, g_usbEpOut, g_usbEpMaxPacketSize
    
    prev_dev = cur_dev = None
    usb_ep_in_lambda = lambda ep: usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_IN
    usb_ep_out_lambda = lambda ep: usb.util.endpoint_direction(ep.bEndpointAddress) == usb.util.ENDPOINT_OUT
    usb_version = None
    
    print('Please connect a Nintendo Switch console running nxdumptool. Use Ctrl+C to abort.\n')
    
    while True:
        # Find a connected USB device with a matching VID/PID pair.
        cur_dev = usb.core.find(idVendor=USB_DEV_VID, idProduct=USB_DEV_PID)
        if (cur_dev is None) or ((prev_dev is not None) and (cur_dev.bus == prev_dev.bus) and (cur_dev.address == prev_dev.address)): # Using == here would also compare the backend.
            time.sleep(0.1)
            continue
        
        # Update previous device.
        prev_dev = cur_dev
        
        # Check if the product and manufacturer strings match the ones used by nxdumptool.
        #if (cur_dev.manufacturer != USB_DEV_MANUFACTURER) or (cur_dev.product != USB_DEV_PRODUCT):
        if cur_dev.manufacturer != USB_DEV_MANUFACTURER:
            print('Invalid manufacturer/product strings! (bus %u, address %u).' % (cur_dev.bus, cur_dev.address))
            time.sleep(0.1)
            continue
        
        # Reset device.
        cur_dev.reset()
        
        # Set default device configuration, then get the active configuration descriptor.
        cur_dev.set_configuration()
        cfg = cur_dev.get_active_configuration()
        
        # Get default interface descriptor.
        intf = cfg[(0,0)]
        
        # Retrieve endpoints.
        g_usbEpIn = usb.util.find_descriptor(intf, custom_match=usb_ep_in_lambda)
        g_usbEpOut = usb.util.find_descriptor(intf, custom_match=usb_ep_out_lambda)
        
        if (g_usbEpIn is None) or (g_usbEpOut is None):
            print('Invalid endpoint addresses! (bus %u, address %u).' % (cur_dev.bus, cur_dev.address))
            time.sleep(0.1)
            continue
        
        # Save endpoint max packet size and USB version.
        g_usbEpMaxPacketSize = g_usbEpIn.wMaxPacketSize
        usb_version = cur_dev.bcdUSB
        
        break
    
    print('Successfully retrieved USB endpoints! (bus %u, address %u).' % (cur_dev.bus, cur_dev.address))
    print('Max packet size: 0x%X (USB %u.%u).\n' % (g_usbEpMaxPacketSize, usb_version >> 8, (usb_version & 0xFF) >> 4))
    print('Exit nxdumptool or disconnect your console at any time to close this script.\n')

def usbRead(size, timeout=-1):
    global g_usbEpIn
    
    # Read data.
    rd = g_usbEpIn.read(size, timeout)
    if rd is not None:
        # Convert to a bytes object for easier handling.
        rd = bytes(rd)
    
    return rd

def usbWrite(data, timeout=-1):
    global g_usbEpOut
    return g_usbEpOut.write(data, timeout)

def usbSendStatus(code):
    global g_usbEpMaxPacketSize
    return usbWrite(struct.pack('<4sIH6p', USB_MAGIC_WORD, code, g_usbEpMaxPacketSize, b''), USB_TRANSFER_TIMEOUT) == 0x10

def usbHandleStartSession(cmd_block):
    global g_nxdtVersionMajor, g_nxdtVersionMinor, g_nxdtVersionMicro, g_nxdtAbiVersion
    
    print('Received StartSession (%02X) command.' % (USB_CMD_START_SESSION))
    
    # Parse command block.
    (g_nxdtVersionMajor, g_nxdtVersionMinor, g_nxdtVersionMicro, g_nxdtAbiVersion, padding) = struct.unpack('<BBBB12p', cmd_block)
    
    # Print client info.
    print('Client info: nxdumptool v%u.%u.%u - ABI v%u.' % (g_nxdtVersionMajor, g_nxdtVersionMinor, g_nxdtVersionMicro, g_nxdtAbiVersion))
    
    # Check if we support this ABI version.
    if g_nxdtAbiVersion != USB_ABI_VERSION:
        print('Unsupported ABI version!')
        return USB_STATUS_UNSUPPORTED_ABI_VERSION
    
    # Return status code
    return USB_STATUS_SUCCESS

def usbHandleSendFileProperties(cmd_block):
    global g_nspTransferMode, g_nspSize, g_nspHeaderSize, g_nspRemainingSize, g_nspFile, g_nspFilePath, g_outputDir
    
    print('Received SendFileProperties (%02X) command.' % (USB_CMD_SEND_FILE_PROPERTIES))
    
    # Parse command block.
    (file_size, filename_length, nsp_header_size, raw_filename, padding) = struct.unpack('<QII{}s16p'.format(USB_FILE_PROPERTIES_MAX_NAME_LENGTH), cmd_block)
    filename = raw_filename.decode('utf-8').strip('\x00')
    
    # Print info.
    print('File size: 0x%X | Filename length: 0x%X | NSP header size: 0x%X.' % (file_size, filename_length, nsp_header_size))
    print('Filename: "%s".' % (filename))
    
    # Perform integrity checks
    if (g_nspTransferMode == False) and (file_size > 0) and (nsp_header_size >= file_size):
        print('NSP header size must be smaller than the full NSP size!')
        return USB_STATUS_MALFORMED_CMD
    
    if (g_nspTransferMode == True) and (nsp_header_size > 0):
        print('Received non-zero NSP header size during NSP transfer mode!')
        return USB_STATUS_MALFORMED_CMD
    
    if (filename_length <= 0) or (filename_length > USB_FILE_PROPERTIES_MAX_NAME_LENGTH):
        print('Invalid filename length!')
        return USB_STATUS_MALFORMED_CMD
    
    # Enable NSP transfer mode (if needed).
    if (g_nspTransferMode == False) and (file_size > 0) and (nsp_header_size > 0):
        g_nspTransferMode = True
        g_nspSize = file_size
        g_nspRemainingSize = (file_size - nsp_header_size)
        g_nspHeaderSize = nsp_header_size
        g_nspFile = None
        g_nspFilePath = None
        print('NSP transfer mode enabled!')
    
    # Perform additional integrity checks and get a file object to work with.
    if (g_nspTransferMode == False) or ((g_nspTransferMode == True) and (g_nspFile is None)):
        # Check if we're dealing with an absolute path.
        if filename[0] == '/':
            filename = filename[1:]
            
            # Replace all slashes with backslashes if we're running under Windows.
            if os.name == 'nt':
                filename = filename.replace('/', '\\')
        
        # Generate full, absolute path to the destination file.
        fullpath = os.path.abspath(g_outputDir + os.path.sep + filename)
        
        # Get parent directory path.
        dirpath = os.path.dirname(fullpath)
        
        # Create full directory tree.
        os.makedirs(dirpath, exist_ok=True)
        
        # Make sure the output filepath doesn't point to an existing directory.
        if (os.path.exists(fullpath) == True) and (os.path.isfile(fullpath) == False):
            utilsResetNspInfo()
            print('Output filepath points to an existing directory! ("%s").' % (fullpath))
            return USB_STATUS_HOST_IO_ERROR
        
        # Make sure we have enough free space.
        (total_space, used_space, free_space) = shutil.disk_usage(dirpath)
        if free_space <= file_size:
            utilsResetNspInfo()
            print('Not enough free space available in output volume!')
            return USB_STATUS_HOST_IO_ERROR
        
        # Get file object.
        file = open(fullpath, "wb")
        
        if g_nspTransferMode == True:
            # Update NSP file object.
            g_nspFile = file
            
            # Update NSP file path.
            g_nspFilePath = fullpath
            
            # Write NSP header padding right away.
            file.write(b'\0' * g_nspHeaderSize)
    else:
        # Retrieve what we need using global variables.
        file = g_nspFile
        fullpath = g_nspFilePath
        dirpath = os.path.dirname(fullpath)
    
    # Check if we're dealing with an empty file or with the first SendFileProperties command from a NSP.
    if (file_size == 0) or ((g_nspTransferMode == True) and (file_size == g_nspSize)):
        # Close file (if needed).
        if g_nspTransferMode == False:
            file.close()
        
        # Let the command handler take care of sending the status response for us.
        return USB_STATUS_SUCCESS
    
    # Send status response before entering the data transfer stage.
    usbSendStatus(USB_STATUS_SUCCESS)
    
    # Start data transfer stage.
    if g_nspTransferMode == False:
        print('\nData transfer started. Saving file to: "%s".' % (fullpath))
    else:
        print('\nData transfer started. Saving NSP file entry to: "%s".' % (fullpath))
    
    offset = 0
    blksize = USB_TRANSFER_BLOCK_SIZE
    
    # Initialize progress bar.
    ascii = (False if (os.name != 'nt') else True)
    (unit, unit_divisor) = utilsGetSizeUnitAndDivisor(file_size)
    bar_format = '{percentage:3.0f}% |{bar}| {n:.2f}/{total:.2f} [{elapsed}<{remaining}, {rate_fmt}]'
    
    pbar = tqdm(total=(float(file_size) / unit_divisor), ascii=ascii, unit=unit, dynamic_ncols=True, bar_format=bar_format)
    
    while offset < file_size:
        # Update block size (if needed).
        diff = (file_size - offset)
        if blksize > diff:
            blksize = diff
        
        # Handle Zero-Length Termination packet (if needed).
        if ((offset + blksize) >= file_size) and (utilsIsValueAlignedToEndpointPacketSize(blksize) == True):
            rd_size = (blksize + 1)
        else:
            rd_size = blksize
        
        # Read current chunk.
        chunk = usbRead(rd_size, USB_TRANSFER_TIMEOUT)
        chunk_size = len(chunk)
        
        # Check if we're dealing with a CancelFileTransfer command.
        if chunk_size == USB_CMD_HEADER_SIZE:
            (magic, cmd_id, cmd_block_size, padding) = struct.unpack('<4sII4p', chunk)
            if (magic == USB_MAGIC_WORD) and (cmd_id == USB_CMD_CANCEL_FILE_TRANSFER):
                print('\n\nReceived CancelFileTransfer (%02X) command.' % (USB_CMD_CANCEL_FILE_TRANSFER))
                
                # Cancel file transfer.
                file.close()
                os.remove(fullpath)
                utilsResetNspInfo()
                pbar.close()
                
                # Let the command handler take care of sending the status response for us.
                return USB_STATUS_SUCCESS
        
        # Write current chunk.
        file.write(chunk)
        file.flush()
        
        # Update current offset.
        offset = (offset + chunk_size)
        
        # Update remaining NSP data size.
        if g_nspTransferMode == True:
            g_nspRemainingSize = (g_nspRemainingSize - chunk_size)
        
        # Update progress bar.
        pbar.update(float(chunk_size) / unit_divisor)
    
    # Close progress bar
    pbar.close()
    
    # Close file handle (if needed).
    if g_nspTransferMode == False:
        file.close()
    
    print('File transfer successfully completed!')
    
    return USB_STATUS_SUCCESS

def usbHandleSendNspHeader(cmd_block):
    global g_nspTransferMode, g_nspHeaderSize, g_nspRemainingSize, g_nspFile, g_nspFilePath
    
    nsp_header_size = len(cmd_block)
    
    print('Received SendNspHeader (%02X) command.' % (USB_CMD_SEND_NSP_HEADER))
    
    # Integrity checks.
    if g_nspTransferMode == False:
        print('Received NSP header out of NSP transfer mode!')
        return USB_STATUS_MALFORMED_CMD
    
    if g_nspRemainingSize > 0:
        print('Received NSP header before receiving all NSP data! (missing 0x%X byte[s]).' % (g_nspRemainingSize))
        return USB_STATUS_MALFORMED_CMD
    
    if nsp_header_size != g_nspHeaderSize:
        print('NSP header size mismatch! (0x%X != 0x%X).' % (nsp_header_size, g_nspHeaderSize))
        return USB_STATUS_MALFORMED_CMD
    
    # Write NSP header.
    g_nspFile.seek(0)
    g_nspFile.write(cmd_block)
    g_nspFile.close()
    
    print('Successfully wrote 0x%X byte-long NSP header to "%s".' % (nsp_header_size, g_nspFilePath))
    
    # Disable NSP transfer mode.
    utilsResetNspInfo()
    
    return USB_STATUS_SUCCESS

def usbHandleEndSession(cmd_block):
    print('Received EndSession (%02X) command.' % (USB_CMD_END_SESSION))
    return USB_STATUS_SUCCESS

def usbCommandHandler():
    # CancelFileTransfer is handled in usbHandleSendFileProperties().
    cmd_dict = {
        USB_CMD_START_SESSION:        usbHandleStartSession,
        USB_CMD_SEND_FILE_PROPERTIES: usbHandleSendFileProperties,
        USB_CMD_SEND_NSP_HEADER:      usbHandleSendNspHeader,
        USB_CMD_END_SESSION:          usbHandleEndSession
    }
    
    # Get device endpoints.
    usbGetDeviceEndpoints()
    
    while True:
        try:
            # Read command header.
            cmd_header = usbRead(USB_CMD_HEADER_SIZE)
        except usb.core.USBError:
            print('Nintendo Switch disconnected. Exiting.')
            return
        
        if (cmd_header is None) or (len(cmd_header) != USB_CMD_HEADER_SIZE):
            continue
        
        # Parse command header.
        (magic, cmd_id, cmd_block_size, padding) = struct.unpack('<4sII4p', cmd_header)
        
        # Read command block right away (if needed).
        # nxdumptool expects us to read it right after sending the command header.
        cmd_block = None
        if cmd_block_size > 0:
            # Handle Zero-Length Termination packet (if needed).
            if utilsIsValueAlignedToEndpointPacketSize(cmd_block_size) == True:
                rd_size = (cmd_block_size + 1)
            else:
                rd_size = cmd_block_size
            
            cmd_block = usbRead(rd_size, USB_TRANSFER_TIMEOUT)
            if (cmd_block is None) or (len(cmd_block) != cmd_block_size):
                print('Failed to read 0x%X byte(s) long command block for command ID %02X!\n' % (cmd_block_size, cmd_id))
                continue
        
        # Verify magic word.
        if magic != USB_MAGIC_WORD:
            print('Received command header with invalid magic word!\n')
            usbSendStatus(USB_STATUS_INVALID_MAGIC_WORD)
            continue
        
        # Get command handler function.
        cmd_func = cmd_dict.get(cmd_id, None)
        if cmd_func is None:
            print('Received command header with unsupported ID %02X.\n' % (cmd_id))
            usbSendStatus(USB_STATUS_UNSUPPORTED_CMD)
            continue
        
        # Verify command block size.
        if ((cmd_id == USB_CMD_START_SESSION) and (cmd_block_size != USB_CMD_BLOCK_SIZE_START_SESSION)) or \
           ((cmd_id == USB_CMD_SEND_FILE_PROPERTIES) and (cmd_block_size != USB_CMD_BLOCK_SIZE_SEND_FILE_PROPERTIES)) or \
           ((cmd_id == USB_CMD_SEND_NSP_HEADER) and (cmd_block_size == 0)):
            print('Invalid command block size for command ID %02X! (0x%X).\n' % (cmd_id, cmd_block_size))
            usbSendStatus(USB_STATUS_MALFORMED_COMMAND)
            continue
        
        # Run command handler function.
        status = cmd_func(cmd_block)
        print('')
        
        # Send status response
        usbSendStatus(status)
        
        # Bail out if requested.
        if cmd_id == USB_CMD_END_SESSION:
            break

def main():
    global g_outputDir
    
    print('nxdumptool companion script v%s.' % (SCRIPT_VERSION))
    
    # Check if the user provided an output directory.
    if len(sys.argv) >= 2:
        # Expand environment variables and user's home directory.
        g_outputDir = os.path.abspath(os.path.expanduser(os.path.expandvars(sys.argv[1])))
        
        # Check if the provided directory exists.
        if os.path.exists(g_outputDir) == True:
            # Make sure it's a directory.
            if os.path.isdir(g_outputDir) == False:
                print('The provided path points to an existing file!')
                return
        else:
            # Create directory.
            os.mkdir(g_outputDir)
    else:
        # Create 'nxdumptool' subdirectory in the directory where the script is located.
        g_outputDir = (sys.path[0] + os.path.sep + 'nxdumptool')
        if os.path.exists(g_outputDir) == False:
            os.mkdir(g_outputDir)
    
    print('Output directory set to "%s".\n' % (g_outputDir))
    
    # Start USB command handler.
    usbCommandHandler()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print('\nScript interrupted.')
        try:
            sys.exit(0)
        except SystemExit:
            os._exit(0)
