# nxdumptool USB Application Binary Interface (ABI) Technical Specification

This Markdown document aims to explain the technical details behind the ABI used by nxdumptool to communicate with a USB host device connected to the console. As of this writing (May 11th, 2021), the current ABI version is `1`.

In order to avoid unnecessary clutter, this document assumes the reader is already familiar with homebrew launching on the Nintendo Switch, as well as USB concepts such as device/configuration/interface/endpoint descriptors and bulk mode transfers. Shall this not be the case, a small list of helpful resources is available at the end of this document.

## Table of contents

* [USB device interface details](#usb-device-interface-details).
* [USB driver](#usb-driver).
    * [Unix-like operating systems](#unix-like-operating-systems).
    * [Windows](#windows).
* [USB communication details](#usb-communication-details).
    * [Command header](#command-header).
        * [Command IDs](#command-ids).
    * [Command blocks](#command-blocks).
        * [StartSession](#startsession).
        * [SendFileProperties](#sendfileproperties).
            * [Zero Length Termination (ZLT)](#zero-length-termination-zlt).
        * [CancelFileTransfer](#cancelfiletransfer).
        * [SendNspHeader](#sendnspheader).
        * [EndSession](#endsession).
    * [Status response](#status-response).
        * [Status codes](#status-codes).
    * [NSP transfer mode](#nsp-transfer-mode).
        * [Why is there such thing as a 'NSP transfer mode'?](#why-is-there-such-thing-as-a-nsp-transfer-mode)
* [Additional resources](#additional-resources).

## USB device interface details

Right after launching nxdumptool on the target Nintendo Switch, the application configures the console's USB interface using the following information:

* Device descriptor:
    * Class / Subclass / Protocol: all set to `0x00` (defined at the interface level).
    * Vendor ID: `0x057E`.
    * Product ID: `0x3000`.
    * BCD release number: `0x0100`.
    * Product string: `nxdumptool`.
    * Manufacturer string: `DarkMatterCore`.
    * Multiple device descriptors are used to support USB 1.1, 2.0 and 3.0 speeds, each one with slightly different properties. The underlying USB stack from a USB host device usually takes care of automatically choosing one of these, depending on the capabilities of the USB host.
        * USB 3.0 support depends on the `usb30_force_enabled` setting from Horizon OS to be manually enabled before launching a custom firmware (CFW) on the target console. Otherwise, only 1.1 and 2.0 speeds will be made available to the USB host device.
* Configuration descriptor:
    * A single configuration descriptor is provided, regardless of the USB speed selected by the USB host.
* Interface descriptor:
    * A single interface descriptor with no alternate setting is provided as part of the configuration descriptor.
    * Class / Subclass / Protocol: all set to `0xFF` (vendor-specific).
* Endpoint descriptors:
    * Only two bulk endpoint descriptors are provided as part of the interface descriptor.
    * The max packet size varies depending on the USB speed selected by the USB host:
        * USB 1.1: 64 bytes.
        * USB 2.0: 512 bytes.
        * USB 3.0: 1024 bytes.
    * SuperSpeed endpoint companion descriptors are also provided if USB 3.0 is used.
* Binary Object Store (BOS) descriptor:
    * Holds a USB 2.0 extension descriptor for Link Power Management (LPM) support, as well as a SuperSpeed device capability descriptor to indicate the supported speeds.

Communication is performed through the bulk input and output endpoints using 5-second timeouts.

## USB driver

A USB driver is needed to actually communicate to the target console running nxdumptool.

### Unix-like operating systems

A package manager can be used to install [libusb](https://libusb.info), which in turn can be used by programs to enumerate and interact with the target console. Under some operating systems, this step may not even be needed.

### Windows

A tool such as [Zadig](https://zadig.akeo.ie) must be used to manually install a USB driver for the target console.

Even though it's possible to use the `WinUSB` driver, we suggest to use `libusbK` instead - the provided Python script in this directory depends on [PyUSB](https://github.com/pyusb/pyusb), which only provides a backend for `libusb` devices. If you intend to write your own `WinUSB`-based ABI host implementation for Windows based on this document, you may be able to use that driver.

Furthermore, even though it's possible for USB devices to work right out of the box using [Microsoft OS descriptors](https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors), the `usb:ds` API available to homebrew applications on the Nintendo Switch doesn't provide any way to set them. Thus, it's not possible to interact with the target console without installing a USB driver first.

## USB communication details

The USB host device essentially acts as a storage server for nxdumptool. This means all commands are initially issued by the target console, leading to data transfer stages for which status responses are expected to be sent by the USB host device.

This intends to minimize the overhead on the USB host device by letting nxdumptool take care of the full dump process - the host only needs to take care of storing the received data. This also heavily simplifies the work required to write ABI host implementations from scratch, regardless of the programming language being used.

Command handling can be broken down in three different transfer stages: command header (from nxdumptool), command block (from nxdumptool) and status response (from USB host). Certain commands may lead to an additional data transfer stage after the status response is received from the USB host device.

### Command header

| Offset | Size | Description                  |
|:------:|:----:|------------------------------|
|  0x00  | 0x04 | Magic word (`NXDT`).         |
|  0x04  | 0x04 | Command ID (LE u32).         |
|  0x08  | 0x04 | Command block size (LE u32). |
|  0x0C  | 0x04 | Reserved.                    |

While handling ABI commands, nxdumptool first issues the command header - this way, the USB host device knows both the command ID and the command block size before attempting to receive the command block.

Certain commands yield no command block at all, leading to a command block size of zero - this is considered defined behaviour. Nonetheless, a status response is still expected to be sent by the USB host.

#### Command IDs

| Value | Name                | Description                                                                                                                                                              |
|:-----:|---------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
|   0   | StartSession.       | Starts a USB session between the target console and the USB host device.                                                                                                 |
|   1   | SendFileProperties. | Sends file metadata and starts a data transfer process.                                                                                                                  |
|   2   | CancelFileTransfer. | Cancels an ongoing data transfer process started by a previously issued SendFileProperties command.                                                                      |
|   3   | SendNspHeader.      | Sends the `PFS0` header from a Nintendo Submission Package (NSP). Only issued under NSP transfer mode, and after the data for all NSP file entries has been transferred. |
|   4   | EndSession.         | Ends a previously stablished USB session between the target console and the USB host device.                                                                             |

### Command blocks

All commands, with the exception of `CancelFileTransfer` and `EndSession`, yield a command block. Each command block follows its own distinctive structure.

#### StartSession

| Offset | Size | Description                               |
|:------:|------|-------------------------------------------|
|  0x00  | 0x01 | nxdumptool version (major).               |
|  0x01  | 0x01 | nxdumptool version (minor).               |
|  0x02  | 0x01 | nxdumptool version (micro).               |
|  0x03  | 0x01 | nxdumptool USB ABI version.               |
|  0x04  | 0x08 | Git commit hash (NULL terminated string). |
|  0x0C  | 0x04 | Reserved.                                 |

This is the first USB command issued by nxdumptool upon connection to a USB host device. If it succeeds, further USB commands may be sent.

#### SendFileProperties

| Offset | Size  | Description                                      |
|:------:|:-----:|--------------------------------------------------|
|  0x000 | 0x08  | File size (LE u64).                              |
|  0x008 | 0x04  | Filename length (LE u32).                        |
|  0x00C | 0x04  | NSP header size (LE u32).                        |
|  0x010 | 0x301 | UTF-8 encoded filename (NULL terminated string). |
|  0x311 | 0x0F  | Reserved.                                        |

Sent right before starting a file transfer. If it succeeds, file data will be transferred using 8 MiB (0x800000) chunks. The last chunk will be truncated, if needed.

A status response is expected from the USB host right after receiving this command block, which is also right before starting the file data transfer stage. Furthermore, an additional status response is expected right after the last file data chunk has been sent.

In the case of extracted RomFS transfers, the `filename` field may actually hold an absolute filepath - this will always start with a `/`. The USB host is free to decide how to handle this (e.g. create full directory tree, etc.).

##### Zero Length Termination (ZLT)

As per USB bulk transfer specification, when a USB host/device receives a data packet smaller than the endpoint max packet size, it shall consider the transfer is complete and no more data packets are left. This is called a transaction completion mechanism.

However, if the last data chunk is aligned to the endpoint max packet size, an alternate completion mechanism is needed - this is where Zero Length Termination (ZLT) packets come into play. If this condition is met, the USB host device should expect a single ZLT packet from nxdumptool right after the last data chunk has been transferred.

If no ZLT packet were issued, the USB stack from the host device wouldn't be capable of knowing the ongoing transfer has been completed, making it expect further data to be sent by the target console - which in turn leads to a timeout error on the USB host side. Furthermore, if the ZLT packet is left unhandled by the USB host device, a timeout error will be raised on the target console's side.

Most USB backend implementations require the program to provide a bigger transfer length (+1 byte at least) if a ZLT packet is to be expected. This should be more than enough.

#### CancelFileTransfer

Yields no command block. Expects a status response, just like the rest of the commands.

This command can only be issued during the file data transfer stage from a [SendFileProperties](#sendfileproperties) command. It is used to gracefully cancel an ongoing file transfer while also keeping the USB session alive. It's up to the USB host to decide what to do with the incomplete data.

The easiest way to detect this command during a file transfer is by checking the length of the last received block and then parse it to see if it matches a `CancelFileTransfer` command header.

#### SendNspHeader

Variable length. The command block size from the command header represents the NSP header size, while the command block data represents the `PFS0` header from a NSP.

If the NSP header size is aligned to the endpoint max packet size, the USB host should expect a [ZLT packet](#zero-length-termination-zlt).

#### EndSession

Yields no command block. Expects a status response, just like the rest of the commands.

This command is only issued while exiting nxdumptool, as long as the target console is connected to a host device and a USB session has been successfully established.

### Status response

| Offset | Size | Description                        |
|:------:|:----:|------------------------------------|
|  0x00  | 0x04 | Magic word (`NXDT`).               |
|  0x04  | 0x04 | Status code (LE u32).              |
|  0x08  | 0x02 | Endpoint max packet size (LE u16). |
|  0x0A  | 0x06 | Reserved.                          |

Status responses are expected by nxdumptool at certain points throughout the command handling steps:

* Right after receiving a command header and/or command block.
* Right after receiving the last file data chunk from a [SendFileProperties](#sendfileproperties) command.

The endpoint max packet size must be sent back to the target console using status responses because the `usb:ds` API provides no way for homebrew applications to know which device descriptor and/or speed was selected by the USB host device.

#### Status codes

| Value | Description                                                      |
|:-----:|------------------------------------------------------------------|
|   0   | Success.                                                         |
|   1   | Invalid command size. Reserved for internal nxdumptool usage.    |
|   2   | Failed to write command. Reserved for internal nxdumptool usage. |
|   3   | Failed to read status. Reserved for internal nxdumptool usage.   |
|   4   | Invalid magic word.                                              |
|   5   | Unsupported command.                                             |
|   6   | Unsupported USB ABI version.                                     |
|   7   | Malformed command.                                               |
|   8   | USB host I/O error (write error, insufficient space, etc.).      |

### NSP transfer mode

If the NSP header size field from a [SendFileProperties](#sendfileproperties) command block is greater than zero, the USB host should enter NSP transfer mode. The file size field from this block represents, then, the full NSP size (including the NSP header).

In this mode, the USB host should immediately create the output file, write `NSP header size` bytes of padding to it, reply with a status response as usual and expect further [SendFileProperties](#sendfileproperties) commands. No file data is transferred for this very first [SendFileProperties](#sendfileproperties) command block.

Each further [SendFileProperties](#sendfileproperties) command block will hold the filename and size for a specific NSP file entry, and the NSP header size field will always be set to zero. The file data received for each one of these file entries must be written to the output file created during the first [SendFileProperties](#sendfileproperties) command. The sum of all file entry sizes should be equal to the full NSP size minus the NSP header size received during the first [SendFileProperties](#sendfileproperties) command.

Finally, the USB host will receive a [SendNspHeader](#sendnspheader) command with the NSP header data, which should be written at the start of the output file. The command block size in the command header should match the NSP header size received in the first [SendFileProperties](#sendfileproperties) command.

#### Why is there such thing as a 'NSP transfer mode'?

This is because the `PFS0` header from NSPs holds the filenames for all file entries written into the package, which are mostly [Nintendo Content Archives (NCA)](https://switchbrew.org/wiki/NCA).

NCA filenames represent the first half of the NCA SHA-256 checksum, in lowercase. This fact alone makes it impossible to send a NSP header right from the beginning - SHA-256 checksums are calculated by nxdumptool while dumping each NCA.

## Additional resources

* [USB in a NutShell](https://www.beyondlogic.org/usbnutshell/usb1.shtml).
* [USB Made Simple](https://www.usbmadesimple.co.uk).
