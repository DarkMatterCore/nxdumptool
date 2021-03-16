/*
 * usb.c
 *
 * Heavily based in usb_comms from libnx.
 *
 * Copyright (c) 2018-2020, Switchbrew and libnx contributors.
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include "utils.h"
#include "usb.h"

#define USB_ABI_VERSION             1

#define USB_CMD_HEADER_MAGIC        0x4E584454                  /* "NXDT". */

#define USB_TRANSFER_ALIGNMENT      0x1000                      /* 4 KiB. */
#define USB_TRANSFER_TIMEOUT        5                           /* 5 seconds. */

#define USB_DEV_VID                 0x057E                      /* VID officially used by Nintendo in usb:ds. */
#define USB_DEV_PID                 0x3000                      /* PID officially used by Nintendo in usb:ds. */
#define USB_DEV_BCD_REL             0x0100                      /* Device release number. Always 1.0. */

#define USB_FS_BCD_REVISION         0x0110                      /* USB 1.1. */
#define USB_FS_EP0_MAX_PACKET_SIZE  0x40                        /* 64 bytes. */
#define USB_FS_EP_MAX_PACKET_SIZE   0x40                        /* 64 bytes. */

#define USB_HS_BCD_REVISION         0x0200                      /* USB 2.0. */
#define USB_HS_EP0_MAX_PACKET_SIZE  USB_FS_EP0_MAX_PACKET_SIZE  /* 64 bytes. */
#define USB_HS_EP_MAX_PACKET_SIZE   0x200                       /* 512 bytes. */

#define USB_SS_BCD_REVISION         0x0300                      /* USB 3.0. */
#define USB_SS_EP_MAX_PACKET_SIZE   0x400                       /* 1024 bytes. */
#define USB_SS_EP0_MAX_PACKET_SIZE  9                           /* 512 bytes (1 << 9). */

#define USB_BOS_SIZE                0x16                        /* usb_bos_descriptor + usb_2_0_extension_descriptor + usb_ss_usb_device_capability_descriptor. */

#define USB_LANGID_ENUS             0x0409

/* Type definitions. */

typedef struct {
    RwLock lock, lock_in, lock_out;
    bool initialized;
    UsbDsInterface *interface;
    UsbDsEndpoint *endpoint_in, *endpoint_out;
} UsbDeviceInterface;

typedef enum {
    UsbCommandType_StartSession       = 0,
    UsbCommandType_SendFileProperties = 1,
    UsbCommandType_CancelFileTransfer = 2,
    UsbCommandType_SendNspHeader      = 3,
    UsbCommandType_EndSession         = 4
} UsbCommandType;

typedef struct {
    u32 magic;
    u32 cmd;
    u32 cmd_block_size;
    u8 reserved[0x4];
} UsbCommandHeader;

typedef struct {
    u8 app_ver_major;
    u8 app_ver_minor;
    u8 app_ver_micro;
    u8 abi_version;
    char git_commit[8];
    u8 reserved[0x4];
} UsbCommandStartSession;

typedef struct {
    u64 file_size;
    u32 filename_length;
    u32 nsp_header_size;
    char filename[FS_MAX_PATH];
    u8 reserved_2[0xF];
} UsbCommandSendFileProperties;

typedef enum {
    ///< Expected response code.
    UsbStatusType_Success               = 0,
    
    ///< Internal usage.
    UsbStatusType_InvalidCommandSize    = 1,
    UsbStatusType_WriteCommandFailed    = 2,
    UsbStatusType_ReadStatusFailed      = 3,
    
    ///< These can be returned by the host device.
    UsbStatusType_InvalidMagicWord      = 4,
    UsbStatusType_UnsupportedCommand    = 5,
    UsbStatusType_UnsupportedAbiVersion = 6,
    UsbStatusType_MalformedCommand      = 7,
    UsbStatusType_HostIoError           = 8
} UsbStatusType;

typedef struct {
    u32 magic;
    u32 status;             ///< UsbStatusType.
    u16 max_packet_size;    ///< USB host endpoint max packet size.
    u8 reserved[0x6];
} UsbStatus;

/// Imported from libusb, with some adjustments.
enum usb_bos_type {
    USB_BT_WIRELESS_USB_DEVICE_CAPABILITY = 1,
    USB_BT_USB_2_0_EXTENSION              = 2,
    USB_BT_SS_USB_DEVICE_CAPABILITY       = 3,
    USB_BT_CONTAINER_ID                   = 4
};

/// Imported from libusb, with some adjustments.
enum usb_2_0_extension_attributes {
    USB_BM_LPM_SUPPORT = 2
};

/// Imported from libusb, with some adjustments.
enum usb_ss_usb_device_capability_attributes {
    USB_BM_LTM_SUPPORT = 2
};

/// Imported from libusb, with some adjustments.
enum usb_supported_speed {
    USB_LOW_SPEED_OPERATION   = BIT(0),
    USB_FULL_SPEED_OPERATION  = BIT(1),
    USB_HIGH_SPEED_OPERATION  = BIT(2),
    USB_SUPER_SPEED_OPERATION = BIT(3)
};

/// Imported from libusb, with some adjustments.
struct usb_bos_descriptor {
    u8 bLength;
    u8 bDescriptorType; ///< Must match USB_DT_BOS.
    u16 wTotalLength;   ///< Length of this descriptor and all of its sub descriptors.
    u8 bNumDeviceCaps;  ///< The number of separate device capability descriptors in the BOS.
} PACKED;

/// Imported from libusb, with some adjustments.
struct usb_2_0_extension_descriptor {
    u8 bLength;
    u8 bDescriptorType;     ///< Must match USB_DT_DEVICE_CAPABILITY.
    u8 bDevCapabilityType;  ///< Must match USB_BT_USB_2_0_EXTENSION.
    u32 bmAttributes;       ///< usb_2_0_extension_attributes.
} PACKED;

/// Imported from libusb, with some adjustments.
struct usb_ss_usb_device_capability_descriptor {
    u8 bLength;
    u8 bDescriptorType;         ///< Must match USB_DT_DEVICE_CAPABILITY.
    u8 bDevCapabilityType;      ///< Must match USB_BT_SS_USB_DEVICE_CAPABILITY.
    u8 bmAttributes;            ///< usb_ss_usb_device_capability_attributes.
    u16 wSpeedsSupported;       ///< usb_supported_speed.
    u8 bFunctionalitySupport;   ///< The lowest speed at which all the functionality that the device supports is available to the user.
    u8 bU1DevExitLat;           ///< U1 Device Exit Latency.
    u16 bU2DevExitLat;          ///< U2 Device Exit Latency.
} PACKED;

/* Global variables. */

static RwLock g_usbDeviceLock = {0};
static UsbDeviceInterface g_usbDeviceInterface = {0};
static bool g_usbDeviceInterfaceInit = false;

static Event *g_usbStateChangeEvent = NULL;
static Thread g_usbDetectionThread = {0};
static UEvent g_usbDetectionThreadExitEvent = {0}, g_usbTimeoutEvent = {0};
static bool g_usbHostAvailable = false, g_usbSessionStarted = false, g_usbDetectionThreadExitFlag = false, g_nspTransferMode = false;
static atomic_bool g_usbDetectionThreadCreated = false;

static u8 *g_usbTransferBuffer = NULL;
static u64 g_usbTransferRemainingSize = 0, g_usbTransferWrittenSize = 0;
static u16 g_usbEndpointMaxPacketSize = 0;

/* Function prototypes. */

static bool usbCreateDetectionThread(void);
static void usbDestroyDetectionThread(void);
static void usbDetectionThreadFunc(void *arg);

static bool usbStartSession(void);
static void usbEndSession(void);

NX_INLINE void usbPrepareCommandHeader(u32 cmd, u32 cmd_block_size);
static bool usbSendCommand(void);
static void usbLogStatusDetail(u32 status);

NX_INLINE bool usbAllocateTransferBuffer(void);
NX_INLINE void usbFreeTransferBuffer(void);

static bool usbInitializeComms(void);
static void usbCloseComms(void);

static void usbFreeDeviceInterface(void);

NX_INLINE bool usbInitializeDeviceInterface(void);
static bool usbInitializeDeviceInterface5x(void);
static bool usbInitializeDeviceInterface1x(void);

NX_INLINE bool usbIsHostAvailable(void);

NX_INLINE void usbSetZltPacket(bool enable);

NX_INLINE bool usbRead(void *buf, size_t size);
NX_INLINE bool usbWrite(void *buf, size_t size);
static bool usbTransferData(void *buf, size_t size, UsbDsEndpoint *endpoint);

bool usbInitialize(void)
{
    bool ret = false;
    
    rwlockWriteLock(&g_usbDeviceLock);
    
    /* Allocate USB transfer buffer. */
    if (!usbAllocateTransferBuffer())
    {
        LOG_MSG("Failed to allocate memory for the USB transfer buffer!");
        goto end;
    }
    
    /* Initialize USB device interface. */
    if (!usbInitializeComms())
    {
        LOG_MSG("Failed to initialize USB device interface!");
        goto end;
    }
    
    /* Retrieve USB state change kernel event. */
    g_usbStateChangeEvent = usbDsGetStateChangeEvent();
    if (!g_usbStateChangeEvent)
    {
        LOG_MSG("Failed to retrieve USB state change kernel event!");
        goto end;
    }
    
    /* Create user-mode exit event. */
    ueventCreate(&g_usbDetectionThreadExitEvent, true);
    
    /* Create user-mode USB timeout event. */
    ueventCreate(&g_usbTimeoutEvent, true);
    
    /* Create USB detection thread. */
    atomic_store(&g_usbDetectionThreadCreated, usbCreateDetectionThread());
    if (!atomic_load(&g_usbDetectionThreadCreated)) goto end;
    
    ret = true;
    
end:
    rwlockWriteUnlock(&g_usbDeviceLock);
    
    return ret;
}

void usbExit(void)
{
    /* Destroy USB detection thread before attempting to lock. */
    if (atomic_load(&g_usbDetectionThreadCreated))
    {
        usbDestroyDetectionThread();
        atomic_store(&g_usbDetectionThreadCreated, false);
    }
    
    /* Now we can safely lock. */
    rwlockWriteLock(&g_usbDeviceLock);
    
    /* Clear USB state change kernel event. */
    g_usbStateChangeEvent = NULL;
    
    /* Close USB device interface. */
    usbCloseComms();
    
    /* Free USB transfer buffer. */
    usbFreeTransferBuffer();
    
    rwlockWriteUnlock(&g_usbDeviceLock);
}

void *usbAllocatePageAlignedBuffer(size_t size)
{
    if (!size) return NULL;
    return memalign(USB_TRANSFER_ALIGNMENT, size);
}

bool usbIsReady(void)
{
    rwlockWriteLock(&g_usbDeviceLock);
    rwlockWriteLock(&(g_usbDeviceInterface.lock));
    bool ret = (g_usbHostAvailable && g_usbSessionStarted);
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
    rwlockWriteUnlock(&g_usbDeviceLock);
    return ret;
}

bool usbSendFileProperties(u64 file_size, const char *filename, u32 nsp_header_size)
{
    rwlockWriteLock(&g_usbDeviceLock);
    rwlockWriteLock(&(g_usbDeviceInterface.lock));
    
    bool ret = false;
    UsbCommandSendFileProperties *cmd_block = NULL;
    size_t filename_length = 0;
    
    if (!g_usbTransferBuffer || !g_usbDeviceInterfaceInit || !g_usbDeviceInterface.initialized || !g_usbHostAvailable || !g_usbSessionStarted || !filename || \
        !(filename_length = strlen(filename)) || filename_length >= FS_MAX_PATH || (!g_nspTransferMode && ((file_size && nsp_header_size >= file_size) || g_usbTransferRemainingSize)) || \
        (g_nspTransferMode && nsp_header_size))
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    /* Prepare command data. */
    usbPrepareCommandHeader(UsbCommandType_SendFileProperties, (u32)sizeof(UsbCommandSendFileProperties));
    
    cmd_block = (UsbCommandSendFileProperties*)(g_usbTransferBuffer + sizeof(UsbCommandHeader));
    memset(cmd_block, 0, sizeof(UsbCommandSendFileProperties));
    
    cmd_block->file_size = file_size;
    cmd_block->filename_length = (u32)filename_length;
    cmd_block->nsp_header_size = nsp_header_size;
    snprintf(cmd_block->filename, sizeof(cmd_block->filename), "%s", filename);
    
    /* Send command. */
    ret = usbSendCommand();
    if (!ret) goto end;
    
    /* Update variables. */
    g_usbTransferRemainingSize = file_size;
    g_usbTransferWrittenSize = 0;
    if (!g_nspTransferMode) g_nspTransferMode = (file_size && nsp_header_size);
    
end:
    if (!ret && g_nspTransferMode) g_nspTransferMode = false;
    
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
    rwlockWriteUnlock(&g_usbDeviceLock);
    
    return ret;
}

bool usbSendFileData(void *data, u64 data_size)
{
    rwlockWriteLock(&g_usbDeviceLock);
    rwlockWriteLock(&(g_usbDeviceInterface.lock));
    
    void *buf = NULL;
    UsbStatus *cmd_status = NULL;
    bool ret = false, zlt_required = false;
    
    if (!g_usbTransferBuffer || !g_usbDeviceInterfaceInit || !g_usbDeviceInterface.initialized || !g_usbHostAvailable || !g_usbSessionStarted || !g_usbTransferRemainingSize || !data || \
        !data_size || data_size > USB_TRANSFER_BUFFER_SIZE || data_size > g_usbTransferRemainingSize)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    /* Optimization for buffers that already are page aligned. */
    if (IS_ALIGNED((u64)data, USB_TRANSFER_ALIGNMENT))
    {
        buf = data;
    } else {
        buf = g_usbTransferBuffer;
        memcpy(buf, data, data_size);
    }
    
    /* Determine if we'll need to set a Zero Length Termination (ZLT) packet. */
    /* This is automatically handled by usbDsEndpoint_PostBufferAsync(), depending on the ZLT setting from the input (write) endpoint. */
    /* First, check if this is the last data chunk for this file. */
    if ((g_usbTransferRemainingSize - data_size) == 0)
    {
        /* Enable ZLT if the last chunk size is aligned to the USB endpoint max packet size. */
        if (IS_ALIGNED(data_size, g_usbEndpointMaxPacketSize))
        {
            zlt_required = true;
            usbSetZltPacket(true);
            //LOG_MSG("ZLT enabled. Last chunk size: 0x%lX bytes.", data_size);
        }
    } else {
        /* Disable ZLT if this is the first of multiple data chunks. */
        if (!g_usbTransferWrittenSize)
        {
            usbSetZltPacket(false);
            //LOG_MSG("ZLT disabled (first chunk).");
        }
    }
    
    /* Send data chunk. */
    if (!usbWrite(buf, data_size))
    {
        LOG_MSG("Failed to write 0x%lX bytes long file data chunk from offset 0x%lX! (total size: 0x%lX).", data_size, g_usbTransferWrittenSize, g_usbTransferRemainingSize + g_usbTransferWrittenSize);
        goto end;
    }
    
    ret = true;
    g_usbTransferRemainingSize -= data_size;
    g_usbTransferWrittenSize += data_size;
    
    /* Check if this is the last chunk. */
    if (!g_usbTransferRemainingSize)
    {
        /* Check response from host device. */
        if (!usbRead(g_usbTransferBuffer, sizeof(UsbStatus)))
        {
            LOG_MSG("Failed to read 0x%lX bytes long status block!", sizeof(UsbStatus));
            ret = false;
            goto end;
        }
        
        cmd_status = (UsbStatus*)g_usbTransferBuffer;
        
        if (cmd_status->magic != __builtin_bswap32(USB_CMD_HEADER_MAGIC))
        {
            LOG_MSG("Invalid status block magic word!");
            ret = false;
            goto end;
        }
        
        ret = (cmd_status->status == UsbStatusType_Success);
        if (!ret) usbLogStatusDetail(cmd_status->status);
    }
    
end:
    /* Disable ZLT if it was previously enabled. */
    if (zlt_required) usbSetZltPacket(false);
    
    /* Reset variables in case of errors. */
    if (!ret)
    {
        g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
        g_nspTransferMode = false;
    }
    
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
    rwlockWriteUnlock(&g_usbDeviceLock);
    
    return ret;
}

void usbCancelFileTransfer(void)
{
    rwlockWriteLock(&g_usbDeviceLock);
    rwlockWriteLock(&(g_usbDeviceInterface.lock));
    
    if (!g_usbTransferBuffer || !g_usbDeviceInterfaceInit || !g_usbDeviceInterface.initialized || !g_usbHostAvailable || !g_usbSessionStarted || (!g_usbTransferRemainingSize && \
        !g_nspTransferMode)) goto end;
    
    /* Reset variables right away. */
    g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
    g_nspTransferMode = false;
    
    /* Prepare command data. */
    usbPrepareCommandHeader(UsbCommandType_CancelFileTransfer, 0);
    
    /* Send command. We don't care about the result here. */
    usbSendCommand();
    
end:
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
    rwlockWriteUnlock(&g_usbDeviceLock);
}

bool usbSendNspHeader(void *nsp_header, u32 nsp_header_size)
{
    rwlockWriteLock(&g_usbDeviceLock);
    rwlockWriteLock(&(g_usbDeviceInterface.lock));
    
    bool ret = false;
    
    if (!g_usbTransferBuffer || !g_usbDeviceInterfaceInit || !g_usbDeviceInterface.initialized || !g_usbHostAvailable || !g_usbSessionStarted || g_usbTransferRemainingSize || \
        !g_nspTransferMode || !nsp_header || !nsp_header_size || nsp_header_size > (USB_TRANSFER_BUFFER_SIZE - sizeof(UsbCommandHeader)))
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    /* Disable NSP transfer mode right away. */
    g_nspTransferMode = false;
    
    /* Prepare command data. */
    usbPrepareCommandHeader(UsbCommandType_SendNspHeader, nsp_header_size);
    memcpy(g_usbTransferBuffer + sizeof(UsbCommandHeader), nsp_header, nsp_header_size);
    
    /* Send command. */
    ret = usbSendCommand();
    
end:
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
    rwlockWriteUnlock(&g_usbDeviceLock);
    
    return ret;
}

static bool usbCreateDetectionThread(void)
{
    if (!utilsCreateThread(&g_usbDetectionThread, usbDetectionThreadFunc, NULL, 1))
    {
        LOG_MSG("Failed to create USB detection thread!");
        return false;
    }
    
    return true;
}

static void usbDestroyDetectionThread(void)
{
    /* Signal the exit event to terminate the USB detection thread */
    ueventSignal(&g_usbDetectionThreadExitEvent);
    
    /* Wait for the USB detection thread to exit. */
    utilsJoinThread(&g_usbDetectionThread);
}

static void usbDetectionThreadFunc(void *arg)
{
    (void)arg;
    
    Result rc = 0;
    int idx = 0;
    
    Waiter usb_change_event_waiter = waiterForEvent(g_usbStateChangeEvent);
    Waiter usb_timeout_event_waiter = waiterForUEvent(&g_usbTimeoutEvent);
    Waiter exit_event_waiter = waiterForUEvent(&g_usbDetectionThreadExitEvent);
    
    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, usb_change_event_waiter, usb_timeout_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;
        
        rwlockWriteLock(&g_usbDeviceLock);
        rwlockWriteLock(&(g_usbDeviceInterface.lock));
        
        /* Exit event triggered. */
        if (idx == 2) break;
        
        /* Retrieve current USB connection status. */
        /* Only proceed if we're dealing with a status change. */
        g_usbHostAvailable = usbIsHostAvailable();
        g_usbSessionStarted = false;
        g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
        g_usbEndpointMaxPacketSize = 0;
        
        /* Start an USB session if we're connected to a host device. */
        /* This will essentially hang this thread and all other threads that call USB-related functions until: */
        /* a) A session is successfully established. */
        /* b) The console is disconnected from the USB host. */
        /* c) The thread exit event is triggered. */
        if (g_usbHostAvailable)
        {
            /* Wait until a session is established. */
            g_usbSessionStarted = usbStartSession();
            if (g_usbSessionStarted)
            {
                LOG_MSG("USB session successfully established. Endpoint max packet size: 0x%04X.", g_usbEndpointMaxPacketSize);
            } else {
                /* Check if the exit event was triggered while waiting for a session to be established. */
                if (g_usbDetectionThreadExitFlag) break;
            }
        }
        
        rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
        rwlockWriteUnlock(&g_usbDeviceLock);
    }
    
    /* Close USB session if needed. */
    if (g_usbHostAvailable && g_usbSessionStarted) usbEndSession();
    g_usbHostAvailable = g_usbSessionStarted = g_usbDetectionThreadExitFlag = false;
    g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
    g_usbEndpointMaxPacketSize = 0;
    
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
    rwlockWriteUnlock(&g_usbDeviceLock);
    
    threadExit();
}

static bool usbStartSession(void)
{
    UsbCommandStartSession *cmd_block = NULL;
    bool ret = false;
    
    if (!g_usbTransferBuffer || !g_usbDeviceInterfaceInit || !g_usbDeviceInterface.initialized)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    usbPrepareCommandHeader(UsbCommandType_StartSession, (u32)sizeof(UsbCommandStartSession));
    
    cmd_block = (UsbCommandStartSession*)(g_usbTransferBuffer + sizeof(UsbCommandHeader));
    memset(cmd_block, 0, sizeof(UsbCommandStartSession));
    
    cmd_block->app_ver_major = VERSION_MAJOR;
    cmd_block->app_ver_minor = VERSION_MINOR;
    cmd_block->app_ver_micro = VERSION_MICRO;
    cmd_block->abi_version = USB_ABI_VERSION;
    snprintf(cmd_block->git_commit, sizeof(cmd_block->git_commit), GIT_COMMIT);
    
    ret = usbSendCommand();
    if (ret)
    {
        /* Get the endpoint max packet size from the response sent by the USB host. */
        /* This is done to accurately know when and where to enable Zero Length Termination (ZLT) packets during bulk transfers. */
        /* As much as I'd like to avoid this, usb:ds doesn't disclose information such as the exact device descriptor and/or speed used by the USB host. */
        UsbStatus *cmd_status = (UsbStatus*)g_usbTransferBuffer;
        g_usbEndpointMaxPacketSize = cmd_status->max_packet_size;
        if (g_usbEndpointMaxPacketSize != USB_FS_EP_MAX_PACKET_SIZE && g_usbEndpointMaxPacketSize != USB_HS_EP_MAX_PACKET_SIZE && g_usbEndpointMaxPacketSize != USB_SS_EP_MAX_PACKET_SIZE)
        {
            LOG_MSG("Invalid endpoint max packet size value received from USB host: 0x%04X.", g_usbEndpointMaxPacketSize);
            
            /* Reset flags. */
            ret = false;
            g_usbEndpointMaxPacketSize = 0;
        }
    }
    
end:
    return ret;
}

static void usbEndSession(void)
{
    if (!g_usbTransferBuffer || !g_usbDeviceInterfaceInit || !g_usbDeviceInterface.initialized || !g_usbHostAvailable || !g_usbSessionStarted)
    {
        LOG_MSG("Invalid parameters!");
        return;
    }
    
    /* Prepare command data. */
    usbPrepareCommandHeader(UsbCommandType_EndSession, 0);
    
    /* Send command. We don't care about the result here. */
    usbSendCommand();
}

NX_INLINE void usbPrepareCommandHeader(u32 cmd, u32 cmd_block_size)
{
    if (cmd > UsbCommandType_EndSession) return;
    UsbCommandHeader *cmd_header = (UsbCommandHeader*)g_usbTransferBuffer;
    memset(cmd_header, 0, sizeof(UsbCommandHeader));
    cmd_header->magic = __builtin_bswap32(USB_CMD_HEADER_MAGIC);
    cmd_header->cmd = cmd;
    cmd_header->cmd_block_size = cmd_block_size;
}

static bool usbSendCommand(void)
{
    UsbCommandHeader *cmd_header = (UsbCommandHeader*)g_usbTransferBuffer;
    u32 cmd = cmd_header->cmd, cmd_block_size = cmd_header->cmd_block_size;
    
    UsbStatus *cmd_status = (UsbStatus*)g_usbTransferBuffer;
    u32 status = UsbStatusType_Success;
    
    bool ret = false, zlt_required = false, cmd_block_written = false;
    
    if ((sizeof(UsbCommandHeader) + cmd_block_size) > USB_TRANSFER_BUFFER_SIZE)
    {
        LOG_MSG("Invalid command size!");
        status = UsbStatusType_InvalidCommandSize;
        goto end;
    }
    
    /* Write command header first. */
    if (!usbWrite(cmd_header, sizeof(UsbCommandHeader)))
    {
        LOG_MSG("Failed to write header for type 0x%X command!", cmd);
        status = UsbStatusType_WriteCommandFailed;
        goto end;
    }
    
    /* Check if we need to transfer a command block. */
    if (cmd_block_size)
    {
        /* Move command block data within the transfer buffer to guarantee we'll work with proper alignment. */
        memmove(g_usbTransferBuffer, g_usbTransferBuffer + sizeof(UsbCommandHeader), cmd_block_size);
        
        /* Determine if we'll need to set a Zero Length Termination (ZLT) packet after sending the command block. */
        zlt_required = IS_ALIGNED(cmd_block_size, g_usbEndpointMaxPacketSize);
        if (zlt_required) usbSetZltPacket(true);
        
        /* Write command block. */
        cmd_block_written = usbWrite(g_usbTransferBuffer, cmd_block_size);
        if (!cmd_block_written)
        {
            LOG_MSG("Failed to write command block for type 0x%X command!", cmd);
            status = UsbStatusType_WriteCommandFailed;
        }
        
        /* Disable ZLT if it was previously enabled. */
        if (zlt_required) usbSetZltPacket(false);
        
        /* Bail out if we failed to write the command block. */
        if (!cmd_block_written) goto end;
    }
    
    /* Read status block. */
    if (!usbRead(cmd_status, sizeof(UsbStatus)))
    {
        LOG_MSG("Failed to read 0x%lX bytes long status block for type 0x%X command!", sizeof(UsbStatus), cmd);
        status = UsbStatusType_ReadStatusFailed;
        goto end;
    }
    
    /* Verify magic word in status block. */
    if (cmd_status->magic != __builtin_bswap32(USB_CMD_HEADER_MAGIC))
    {
        status = UsbStatusType_InvalidMagicWord;
        goto end;
    }
    
    /* Update return value. */
    ret = ((status = cmd_status->status) == UsbStatusType_Success);
    
end:
    if (!ret) usbLogStatusDetail(status);
    
    return ret;
}

static void usbLogStatusDetail(u32 status)
{
    switch(status)
    {
        case UsbStatusType_Success:
        case UsbStatusType_InvalidCommandSize:
        case UsbStatusType_WriteCommandFailed:
        case UsbStatusType_ReadStatusFailed:
            break;
        case UsbStatusType_InvalidMagicWord:
            LOG_MSG("Host replied with Invalid Magic Word status code.");
            break;
        case UsbStatusType_UnsupportedCommand:
            LOG_MSG("Host replied with Unsupported Command status code.");
            break;
        case UsbStatusType_UnsupportedAbiVersion:
            LOG_MSG("Host replied with Unsupported ABI Version status code.");
            break;
        case UsbStatusType_MalformedCommand:
            LOG_MSG("Host replied with Malformed Command status code.");
            break;
        case UsbStatusType_HostIoError:
            LOG_MSG("Host replied with I/O Error status code.");
            break;
        default:
            LOG_MSG("Unknown status code: 0x%X.", status);
            break;
    }
}

NX_INLINE bool usbAllocateTransferBuffer(void)
{
    if (g_usbTransferBuffer) return true;
    g_usbTransferBuffer = memalign(USB_TRANSFER_ALIGNMENT, USB_TRANSFER_BUFFER_SIZE);
    return (g_usbTransferBuffer != NULL);
}

NX_INLINE void usbFreeTransferBuffer(void)
{
    if (!g_usbTransferBuffer) return;
    free(g_usbTransferBuffer);
    g_usbTransferBuffer = NULL;
}

static bool usbInitializeComms(void)
{
    Result rc = 0;
    
    /* Used on HOS >= 5.0.0. */
    struct usb_device_descriptor device_descriptor = {
        .bLength = USB_DT_DEVICE_SIZE,
        .bDescriptorType = USB_DT_DEVICE,
        .bcdUSB = USB_FS_BCD_REVISION,                  /* USB 1.1. Updated before setting new device descriptors for USB 2.0 and 3.0. */
        .bDeviceClass = 0,                              /* Defined at interface level. */
        .bDeviceSubClass = 0,                           /* Defined at interface level. */
        .bDeviceProtocol = 0,                           /* Defined at interface level. */
        .bMaxPacketSize0 = USB_FS_EP0_MAX_PACKET_SIZE,  /* Updated before setting the USB 3.0 device descriptor. */
        .idVendor = USB_DEV_VID,
        .idProduct = USB_DEV_PID,
        .bcdDevice = USB_DEV_BCD_REL,
        .iManufacturer = 0,                             /* Filled at a later time. */
        .iProduct = 0,                                  /* Filled at a later time. */
        .iSerialNumber = 0,                             /* Filled at a later time. */
        .bNumConfigurations = 1
    };
    
    static const u16 supported_langs[] = { USB_LANGID_ENUS };
    static const u16 num_supported_langs = (u16)MAX_ELEMENTS(supported_langs);
    
    u8 bos[USB_BOS_SIZE] = {0};
    
    struct usb_bos_descriptor *bos_desc = (struct usb_bos_descriptor*)bos;
    struct usb_2_0_extension_descriptor *usb2_ext_desc = (struct usb_2_0_extension_descriptor*)(bos + sizeof(struct usb_bos_descriptor));
    struct usb_ss_usb_device_capability_descriptor *usb3_devcap_desc = (struct usb_ss_usb_device_capability_descriptor*)((u8*)usb2_ext_desc + sizeof(struct usb_2_0_extension_descriptor));
    
    bos_desc->bLength = sizeof(struct usb_bos_descriptor);
    bos_desc->bDescriptorType = USB_DT_BOS;
    bos_desc->wTotalLength = USB_BOS_SIZE;
    bos_desc->bNumDeviceCaps = 2;   /* USB 2.0 + USB 3.0. No extra capabilities for USB 1.x. */
    
    usb2_ext_desc->bLength = sizeof(struct usb_2_0_extension_descriptor);
    usb2_ext_desc->bDescriptorType = USB_DT_DEVICE_CAPABILITY;
    usb2_ext_desc->bDevCapabilityType = USB_BT_USB_2_0_EXTENSION;
    usb2_ext_desc->bmAttributes = USB_BM_LPM_SUPPORT;
    
    usb3_devcap_desc->bLength = sizeof(struct usb_ss_usb_device_capability_descriptor);
    usb3_devcap_desc->bDescriptorType = USB_DT_DEVICE_CAPABILITY;
    usb3_devcap_desc->bDevCapabilityType = USB_BT_SS_USB_DEVICE_CAPABILITY;
    usb3_devcap_desc->bmAttributes = 0;
    usb3_devcap_desc->wSpeedsSupported = (USB_SUPER_SPEED_OPERATION | USB_HIGH_SPEED_OPERATION | USB_FULL_SPEED_OPERATION);
    usb3_devcap_desc->bFunctionalitySupport = 1;    /* We can fully work under USB 1.x. */
    usb3_devcap_desc->bU1DevExitLat = 0;
    usb3_devcap_desc->bU2DevExitLat = 0;
    
    /* Used on HOS < 5.0.0. */
    static const UsbDsDeviceInfo device_info = {
        .idVendor = USB_DEV_VID,
        .idProduct = USB_DEV_PID,
        .bcdDevice = USB_DEV_BCD_REL,
        .Manufacturer = APP_AUTHOR,
        .Product = APP_TITLE,
        .SerialNumber = APP_VERSION
    };
    
    bool ret = (g_usbDeviceInterfaceInit && g_usbDeviceInterface.initialized);
    if (ret) goto end;
    
    rc = usbDsInitialize();
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInitialize failed! (0x%08X).", rc);
        goto end;
    }
    
    if (hosversionAtLeast(5, 0, 0))
    {
        /* Set language string descriptor. */
        rc = usbDsAddUsbLanguageStringDescriptor(NULL, supported_langs, num_supported_langs);
        if (R_FAILED(rc)) LOG_MSG("usbDsAddUsbLanguageStringDescriptor failed! (0x%08X).", rc);
        
        /* Set manufacturer string descriptor. */
        if (R_SUCCEEDED(rc))
        {
            rc = usbDsAddUsbStringDescriptor(&(device_descriptor.iManufacturer), APP_AUTHOR);
            if (R_FAILED(rc)) LOG_MSG("usbDsAddUsbStringDescriptor failed! (0x%08X) (manufacturer).", rc);
        }
        
        /* Set product string descriptor. */
        if (R_SUCCEEDED(rc))
        {
            rc = usbDsAddUsbStringDescriptor(&(device_descriptor.iProduct), APP_TITLE);
            if (R_FAILED(rc)) LOG_MSG("usbDsAddUsbStringDescriptor failed! (0x%08X) (product).", rc);
        }
        
        /* Set serial number string descriptor. */
        if (R_SUCCEEDED(rc))
        {
            rc = usbDsAddUsbStringDescriptor(&(device_descriptor.iSerialNumber), APP_VERSION);
            if (R_FAILED(rc)) LOG_MSG("usbDsAddUsbStringDescriptor failed! (0x%08X) (serial number).", rc);
        }
        
        /* Set device descriptors. */
        
        if (R_SUCCEEDED(rc))
        {
            rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &device_descriptor);  /* Full Speed is USB 1.1. */
            if (R_FAILED(rc)) LOG_MSG("usbDsSetUsbDeviceDescriptor failed! (0x%08X) (USB 1.1).", rc);
        }
        
        if (R_SUCCEEDED(rc))
        {
            /* Update USB revision before proceeding. */
            device_descriptor.bcdUSB = USB_HS_BCD_REVISION;
            
            rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &device_descriptor);  /* High Speed is USB 2.0. */
            if (R_FAILED(rc)) LOG_MSG("usbDsSetUsbDeviceDescriptor failed! (0x%08X) (USB 2.0).", rc);
        }
        
        if (R_SUCCEEDED(rc))
        {
            /* Update USB revision and upgrade control endpoint packet size before proceeding. */
            device_descriptor.bcdUSB = USB_SS_BCD_REVISION;
            device_descriptor.bMaxPacketSize0 = USB_SS_EP0_MAX_PACKET_SIZE;
            
            rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &device_descriptor); /* Super Speed is USB 3.0. */
            if (R_FAILED(rc)) LOG_MSG("usbDsSetUsbDeviceDescriptor failed! (0x%08X) (USB 3.0).", rc);
        }
        
        /* Set Binary Object Store. */
        if (R_SUCCEEDED(rc))
        {
            rc = usbDsSetBinaryObjectStore(bos, USB_BOS_SIZE);
            if (R_FAILED(rc)) LOG_MSG("usbDsSetBinaryObjectStore failed! (0x%08X).", rc);
        }
    } else {
        /* Set VID, PID and BCD. */
        rc = usbDsSetVidPidBcd(&device_info);
        if (R_FAILED(rc)) LOG_MSG("usbDsSetVidPidBcd failed! (0x%08X).", rc);
    }
    
    if (R_FAILED(rc)) goto end;
    
    /* Initialize USB device interface. */
    rwlockWriteLock(&(g_usbDeviceInterface.lock));
    rwlockWriteLock(&(g_usbDeviceInterface.lock_in));
    rwlockWriteLock(&(g_usbDeviceInterface.lock_out));
    
    bool dev_iface_init = usbInitializeDeviceInterface();
    
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock_out));
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock_in));
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
    
    if (!dev_iface_init)
    {
        LOG_MSG("Failed to initialize USB device interface!");
        goto end;
    }
    
    if (hosversionAtLeast(5, 0, 0))
    {
        rc = usbDsEnable();
        if (R_FAILED(rc))
        {
            LOG_MSG("usbDsEnable failed! (0x%08X).", rc);
            goto end;
        }
    }
    
    ret = g_usbDeviceInterfaceInit = true;
    
end:
    if (!ret) usbCloseComms();
    
    return ret;
}

static void usbCloseComms(void)
{
    usbDsExit();
    g_usbDeviceInterfaceInit = false;
    usbFreeDeviceInterface();
}

static void usbFreeDeviceInterface(void)
{
    rwlockWriteLock(&(g_usbDeviceInterface.lock));
    
    if (!g_usbDeviceInterface.initialized) {
        rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
        return;
    }
    
    rwlockWriteLock(&(g_usbDeviceInterface.lock_in));
    rwlockWriteLock(&(g_usbDeviceInterface.lock_out));
    
    g_usbDeviceInterface.initialized = false;
    
    g_usbDeviceInterface.interface = NULL;
    g_usbDeviceInterface.endpoint_in = NULL;
    g_usbDeviceInterface.endpoint_out = NULL;
    
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock_out));
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock_in));
    
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock));
}

NX_INLINE bool usbInitializeDeviceInterface(void)
{
    return (hosversionAtLeast(5, 0, 0) ? usbInitializeDeviceInterface5x() : usbInitializeDeviceInterface1x());
}

static bool usbInitializeDeviceInterface5x(void)
{
    Result rc = 0;
    
    struct usb_interface_descriptor interface_descriptor = {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = USBDS_DEFAULT_InterfaceNumber,
        .bAlternateSetting = 0,
        .bNumEndpoints = 2,
        .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
        .bInterfaceSubClass = USB_CLASS_VENDOR_SPEC,
        .bInterfaceProtocol = USB_CLASS_VENDOR_SPEC,
        .iInterface = 0
    };
    
    struct usb_endpoint_descriptor endpoint_descriptor_in = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = USB_FS_EP_MAX_PACKET_SIZE,    /* Updated before setting new device descriptors for USB 2.0 and 3.0. */
        .bInterval = 0
    };
    
    struct usb_endpoint_descriptor endpoint_descriptor_out = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_OUT,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = USB_FS_EP_MAX_PACKET_SIZE,    /* Updated before setting new device descriptors for USB 2.0 and 3.0. */
        .bInterval = 0
    };
    
    struct usb_ss_endpoint_companion_descriptor endpoint_companion = {
        .bLength = sizeof(struct usb_ss_endpoint_companion_descriptor),
        .bDescriptorType = USB_DT_SS_ENDPOINT_COMPANION,
        .bMaxBurst = 0x0F,
        .bmAttributes = 0,
        .wBytesPerInterval = 0
    };
    
    /* Enable device interface. */
    g_usbDeviceInterface.initialized = true;
    
    /* Setup interface. */
    rc = usbDsRegisterInterface(&(g_usbDeviceInterface.interface));
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsRegisterInterface failed! (0x%08X).", rc);
        return false;
    }
    
    interface_descriptor.bInterfaceNumber = g_usbDeviceInterface.interface->interface_index;
    endpoint_descriptor_in.bEndpointAddress += (interface_descriptor.bInterfaceNumber + 1);
    endpoint_descriptor_out.bEndpointAddress += (interface_descriptor.bInterfaceNumber + 1);
    
    /* Full Speed config (USB 1.1). */
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Full, &interface_descriptor, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 1.1) (interface).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Full, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 1.1) (in endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Full, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 1.1) (out endpoint).", rc);
        return false;
    }
    
    /* High Speed config (USB 2.0). */
    endpoint_descriptor_in.wMaxPacketSize = USB_HS_EP_MAX_PACKET_SIZE;
    endpoint_descriptor_out.wMaxPacketSize = USB_HS_EP_MAX_PACKET_SIZE;
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_High, &interface_descriptor, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 2.0) (interface).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_High, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 2.0) (in endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_High, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 2.0) (out endpoint).", rc);
        return false;
    }
    
    /* Super Speed config (USB 3.0). */
    endpoint_descriptor_in.wMaxPacketSize = USB_SS_EP_MAX_PACKET_SIZE;
    endpoint_descriptor_out.wMaxPacketSize = USB_SS_EP_MAX_PACKET_SIZE;
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Super, &interface_descriptor, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 3.0) (interface).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Super, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 3.0) (in endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Super, &endpoint_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 3.0) (in endpoint companion).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Super, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 3.0) (out endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_AppendConfigurationData(g_usbDeviceInterface.interface, UsbDeviceSpeed_Super, &endpoint_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_AppendConfigurationData failed! (0x%08X) (USB 3.0) (out endpoint companion).", rc);
        return false;
    }
    
    /* Setup endpoints. */
    rc = usbDsInterface_RegisterEndpoint(g_usbDeviceInterface.interface, &(g_usbDeviceInterface.endpoint_in), endpoint_descriptor_in.bEndpointAddress);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_RegisterEndpoint failed! (0x%08X) (in endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_RegisterEndpoint(g_usbDeviceInterface.interface, &(g_usbDeviceInterface.endpoint_out), endpoint_descriptor_out.bEndpointAddress);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_RegisterEndpoint failed! (0x%08X) (out endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_EnableInterface(g_usbDeviceInterface.interface);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_EnableInterface failed! (0x%08X).", rc);
        return false;
    }
    
    return true;
}

static bool usbInitializeDeviceInterface1x(void)
{
    Result rc = 0;
    
    struct usb_interface_descriptor interface_descriptor = {
        .bLength = USB_DT_INTERFACE_SIZE,
        .bDescriptorType = USB_DT_INTERFACE,
        .bInterfaceNumber = 0,
        .bAlternateSetting = 0,
        .bInterfaceClass = USB_CLASS_VENDOR_SPEC,
        .bInterfaceSubClass = USB_CLASS_VENDOR_SPEC,
        .bInterfaceProtocol = USB_CLASS_VENDOR_SPEC,
        .iInterface = 0
    };
    
    struct usb_endpoint_descriptor endpoint_descriptor_in = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_IN,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = USB_HS_EP_MAX_PACKET_SIZE,
        .bInterval = 0
    };
    
    struct usb_endpoint_descriptor endpoint_descriptor_out = {
        .bLength = USB_DT_ENDPOINT_SIZE,
        .bDescriptorType = USB_DT_ENDPOINT,
        .bEndpointAddress = USB_ENDPOINT_OUT,
        .bmAttributes = USB_TRANSFER_TYPE_BULK,
        .wMaxPacketSize = USB_HS_EP_MAX_PACKET_SIZE,
        .bInterval = 0
    };
    
    /* Enable device interface. */
    g_usbDeviceInterface.initialized = true;
    
    /* Setup interface. */
    rc = usbDsGetDsInterface(&(g_usbDeviceInterface.interface), &interface_descriptor, "usb");
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsGetDsInterface failed! (0x%08X).", rc);
        return false;
    }
    
    /* Setup endpoints. */
    rc = usbDsInterface_GetDsEndpoint(g_usbDeviceInterface.interface, &(g_usbDeviceInterface.endpoint_in), &endpoint_descriptor_in);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_GetDsEndpoint failed! (0x%08X) (in endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_GetDsEndpoint(g_usbDeviceInterface.interface, &(g_usbDeviceInterface.endpoint_out), &endpoint_descriptor_out);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_GetDsEndpoint failed! (0x%08X) (out endpoint).", rc);
        return false;
    }
    
    rc = usbDsInterface_EnableInterface(g_usbDeviceInterface.interface);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsInterface_EnableInterface failed! (0x%08X).", rc);
        return false;
    }
    
    return true;
}

NX_INLINE bool usbIsHostAvailable(void)
{
    UsbState state = UsbState_Detached;
    Result rc = usbDsGetState(&state);
    return (R_SUCCEEDED(rc) && state == UsbState_Configured);
}

NX_INLINE void usbSetZltPacket(bool enable)
{
    rwlockWriteLock(&(g_usbDeviceInterface.lock_in));
    usbDsEndpoint_SetZlt(g_usbDeviceInterface.endpoint_in, enable);
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock_in));
}

NX_INLINE bool usbRead(void *buf, u64 size)
{
    rwlockWriteLock(&(g_usbDeviceInterface.lock_out));
    bool ret = usbTransferData(buf, size, g_usbDeviceInterface.endpoint_out);
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock_out));
    return ret;
}

NX_INLINE bool usbWrite(void *buf, u64 size)
{
    rwlockWriteLock(&(g_usbDeviceInterface.lock_in));
    bool ret = usbTransferData(buf, size, g_usbDeviceInterface.endpoint_in);
    rwlockWriteUnlock(&(g_usbDeviceInterface.lock_in));
    return ret;
}

static bool usbTransferData(void *buf, u64 size, UsbDsEndpoint *endpoint)
{
    if (!buf || !IS_ALIGNED((u64)buf, USB_TRANSFER_ALIGNMENT) || !size || !endpoint)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    if (!usbIsHostAvailable())
    {
        LOG_MSG("USB host unavailable!");
        return false;
    }
    
    Result rc = 0;
    UsbDsReportData report_data = {0};
    u32 urb_id = 0, transferred_size = 0;
    bool thread_exit = false;
    
    /* Start an USB transfer using the provided endpoint. */
    rc = usbDsEndpoint_PostBufferAsync(endpoint, buf, size, &urb_id);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsEndpoint_PostBufferAsync failed! (0x%08X) (URB ID %u).", rc, urb_id);
        return false;
    }
    
    /* Wait for the transfer to finish. */
    if (g_usbSessionStarted)
    {
        /* If the USB transfer session has already been started, then use a regular timeout value. */
        rc = eventWait(&(endpoint->CompletionEvent), USB_TRANSFER_TIMEOUT * (u64)1000000000);
    } else {
        /* If we're starting an USB transfer session, wait indefinitely inside a loop to let the user start the companion app. */
        int idx = 0;
        Waiter completion_event_waiter = waiterForEvent(&(endpoint->CompletionEvent));
        Waiter exit_event_waiter = waiterForUEvent(&g_usbDetectionThreadExitEvent);
        
        rc = waitMulti(&idx, -1, completion_event_waiter, exit_event_waiter);
        if (R_SUCCEEDED(rc) && idx == 1)
        {
            /* Exit event triggered. */
            rc = MAKERESULT(Module_Kernel, KernelError_TimedOut);
            g_usbDetectionThreadExitFlag = thread_exit = true;
        }
    }
    
    /* Clear the endpoint completion event. */
    if (!thread_exit) eventClear(&(endpoint->CompletionEvent));
    
    if (R_FAILED(rc))
    {
        /* Cancel transfer. */
        usbDsEndpoint_Cancel(endpoint);
        
        /* Safety measure: wait until the completion event is triggered again before proceeding. */
        eventWait(&(endpoint->CompletionEvent), UINT64_MAX);
        eventClear(&(endpoint->CompletionEvent));
        
        /* Signal user-mode USB timeout event if needed. */
        /* This will "reset" the USB connection by making the background thread wait until a new session is established. */
        if (g_usbSessionStarted) ueventSignal(&g_usbTimeoutEvent);
        
        if (!thread_exit) LOG_MSG("eventWait failed! (0x%08X) (URB ID %u).", rc, urb_id);
        return false;
    }
    
    rc = usbDsEndpoint_GetReportData(endpoint, &report_data);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsEndpoint_GetReportData failed! (0x%08X) (URB ID %u).", rc, urb_id);
        return false;
    }
    
    rc = usbDsParseReportData(&report_data, urb_id, NULL, &transferred_size);
    if (R_FAILED(rc))
    {
        LOG_MSG("usbDsParseReportData failed! (0x%08X) (URB ID %u).", rc, urb_id);
        return false;
    }
    
    if (transferred_size != size)
    {
        LOG_MSG("USB transfer failed! Expected 0x%lX bytes, got 0x%X bytes (URB ID %u).", size, transferred_size, urb_id);
        return false;
    }
    
    return true;
}
