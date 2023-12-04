/*
 * usb.c
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

#include "nxdt_utils.h"
#include "usb.h"

#define USB_ABI_VERSION_MAJOR       1
#define USB_ABI_VERSION_MINOR       2
#define USB_ABI_VERSION             ((USB_ABI_VERSION_MAJOR << 4) | USB_ABI_VERSION_MINOR)

#define USB_CMD_HEADER_MAGIC        0x4E584454                  /* "NXDT". */

#define USB_TRANSFER_ALIGNMENT      0x1000                      /* 4 KiB. */
#define USB_TRANSFER_TIMEOUT        10                          /* 10 seconds. */

#define USB_DEV_VID                 0x057E                      /* VID officially used by Nintendo in usb:ds. */
#define USB_DEV_PID                 0x3000                      /* PID officially used by Nintendo in usb:ds. */
#define USB_DEV_BCD_REL             0x0100                      /* Device release number. Always 1.0. */

#define USB_FS_BCD_REVISION         0x0110                      /* USB 1.1. */
#define USB_FS_EP0_MAX_PACKET_SIZE  0x40                        /* 64 bytes. */
#define USB_FS_EP_MAX_PACKET_SIZE   0x40                        /* 64 bytes. */

#define USB_HS_BCD_REVISION         0x0200                      /* USB 2.0. */
#define USB_HS_EP0_MAX_PACKET_SIZE  0x40                        /* 64 bytes. */
#define USB_HS_EP_MAX_PACKET_SIZE   0x200                       /* 512 bytes. */

#define USB_SS_BCD_REVISION         0x0300                      /* USB 3.0. */
#define USB_SS_EP0_MAX_PACKET_SIZE  9                           /* 512 bytes (1 << 9). */
#define USB_SS_EP_MAX_PACKET_SIZE   0x400                       /* 1024 bytes. */

#define USB_BOS_SIZE                0x16                        /* usb_bos_descriptor + usb_2_0_extension_descriptor + usb_ss_usb_device_capability_descriptor. */

#define USB_LANGID_ENUS             0x0409

/* Type definitions. */

typedef enum {
    UsbCommandType_StartSession         = 0,
    UsbCommandType_SendFileProperties   = 1,
    UsbCommandType_CancelFileTransfer   = 2,
    UsbCommandType_SendNspHeader        = 3,
    UsbCommandType_EndSession           = 4,
    UsbCommandType_StartExtractedFsDump = 5,
    UsbCommandType_EndExtractedFsDump   = 6,
    UsbCommandType_Count                = 7     ///< Total values supported by this enum.
} UsbCommandType;

typedef struct {
    u32 magic;
    u32 cmd;
    u32 cmd_block_size;
    u8 reserved[0x4];
} UsbCommandHeader;

NXDT_ASSERT(UsbCommandHeader, 0x10);

typedef struct {
    u8 app_ver_major;
    u8 app_ver_minor;
    u8 app_ver_micro;
    u8 abi_version;
    char git_commit[8];
    u8 reserved[0x4];
} UsbCommandStartSession;

NXDT_ASSERT(UsbCommandStartSession, 0x10);

typedef struct {
    u64 file_size;
    u32 filename_length;
    u32 nsp_header_size;
    char filename[FS_MAX_PATH];
    u8 reserved[0xF];
} UsbCommandSendFileProperties;

NXDT_ASSERT(UsbCommandSendFileProperties, 0x320);

typedef struct {
    u64 extracted_fs_size;
    char extracted_fs_root_path[FS_MAX_PATH];
    u8 reserved[0x6];
} UsbCommandStartExtractedFsDump;

NXDT_ASSERT(UsbCommandStartExtractedFsDump, 0x310);

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
    UsbStatusType_HostIoError           = 8,

    UsbStatusType_Count                 = 9         ///< Total values supported by this enum.
} UsbStatusType;

typedef struct {
    u32 magic;
    u32 status;             ///< UsbStatusType.
    u16 max_packet_size;    ///< USB host endpoint max packet size.
    u8 reserved[0x6];
} UsbStatus;

NXDT_ASSERT(UsbStatus, 0x10);

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
struct NX_PACKED usb_bos_descriptor {
    u8 bLength;
    u8 bDescriptorType; ///< Must match USB_DT_BOS.
    u16 wTotalLength;   ///< Length of this descriptor and all of its sub descriptors.
    u8 bNumDeviceCaps;  ///< The number of separate device capability descriptors in the BOS.
};

NXDT_ASSERT(struct usb_bos_descriptor, 0x5);

/// Imported from libusb, with some adjustments.
struct NX_PACKED usb_2_0_extension_descriptor {
    u8 bLength;
    u8 bDescriptorType;     ///< Must match USB_DT_DEVICE_CAPABILITY.
    u8 bDevCapabilityType;  ///< Must match USB_BT_USB_2_0_EXTENSION.
    u32 bmAttributes;       ///< usb_2_0_extension_attributes.
};

NXDT_ASSERT(struct usb_2_0_extension_descriptor, 0x7);

/// Imported from libusb, with some adjustments.
struct NX_PACKED usb_ss_usb_device_capability_descriptor {
    u8 bLength;
    u8 bDescriptorType;         ///< Must match USB_DT_DEVICE_CAPABILITY.
    u8 bDevCapabilityType;      ///< Must match USB_BT_SS_USB_DEVICE_CAPABILITY.
    u8 bmAttributes;            ///< usb_ss_usb_device_capability_attributes.
    u16 wSpeedsSupported;       ///< usb_supported_speed.
    u8 bFunctionalitySupport;   ///< The lowest speed at which all the functionality that the device supports is available to the user.
    u8 bU1DevExitLat;           ///< U1 Device Exit Latency.
    u16 bU2DevExitLat;          ///< U2 Device Exit Latency.
};

NXDT_ASSERT(struct usb_ss_usb_device_capability_descriptor, 0xA);

/* Global variables. */

static Mutex g_usbInterfaceMutex = 0;
static UsbDsInterface *g_usbInterface = NULL;
static UsbDsEndpoint *g_usbEndpointIn = NULL, *g_usbEndpointOut = NULL;
static bool g_usbInterfaceInit = false, g_usbHos5xEnabled = false;

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
#if LOG_LEVEL <= LOG_LEVEL_INFO
static void usbLogStatusDetail(u32 status);
#endif

NX_INLINE bool usbAllocateTransferBuffer(void);
NX_INLINE void usbFreeTransferBuffer(void);

static bool usbInitializeComms(void);
static bool usbInitializeComms5x(void);
static bool usbInitializeComms1x(void);
static void usbCloseComms(void);

static bool _usbSendFileProperties(u64 file_size, const char *filename, u32 nsp_header_size, bool enforce_nsp_mode);

NX_INLINE bool usbIsHostAvailable(void);

NX_INLINE void usbSetZltPacket(bool enable);

NX_INLINE bool usbRead(void *buf, size_t size);
NX_INLINE bool usbWrite(void *buf, size_t size);
static bool usbTransferData(void *buf, size_t size, UsbDsEndpoint *endpoint);

bool usbInitialize(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        ret = g_usbInterfaceInit;
        if (ret) break;

        /* Allocate USB transfer buffer. */
        if (!usbAllocateTransferBuffer())
        {
            LOG_MSG_ERROR("Failed to allocate memory for the USB transfer buffer!");
            break;
        }

        /* Initialize USB comms. */
        if (!usbInitializeComms())
        {
            LOG_MSG_ERROR("Failed to initialize USB comms!");
            break;
        }

        /* Retrieve USB state change kernel event. */
        g_usbStateChangeEvent = usbDsGetStateChangeEvent();
        if (!g_usbStateChangeEvent)
        {
            LOG_MSG_ERROR("Failed to retrieve USB state change kernel event!");
            break;
        }

        /* Create user-mode exit event. */
        ueventCreate(&g_usbDetectionThreadExitEvent, true);

        /* Create user-mode USB timeout event. */
        ueventCreate(&g_usbTimeoutEvent, true);

        /* Create USB detection thread. */
        atomic_store(&g_usbDetectionThreadCreated, usbCreateDetectionThread());
        if (!atomic_load(&g_usbDetectionThreadCreated)) break;

        /* Update flags. */
        ret = g_usbInterfaceInit = true;
    }

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
    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        /* Clear USB state change kernel event. */
        g_usbStateChangeEvent = NULL;

        /* Close USB device interface. */
        usbCloseComms();

        /* Free USB transfer buffer. */
        usbFreeTransferBuffer();

        /* Update flag. */
        g_usbInterfaceInit = false;
    }
}

void *usbAllocatePageAlignedBuffer(size_t size)
{
    if (!size) return NULL;
    return memalign(USB_TRANSFER_ALIGNMENT, size);
}

u8 usbIsReady(void)
{
    u8 ret = UsbHostSpeed_None;

    SCOPED_TRY_LOCK(&g_usbInterfaceMutex)
    {
        if (!g_usbHostAvailable || !g_usbSessionStarted) break;

        switch(g_usbEndpointMaxPacketSize)
        {
            case USB_FS_EP_MAX_PACKET_SIZE: /* USB 1.x. */
                ret = UsbHostSpeed_FullSpeed;
                break;
            case USB_HS_EP_MAX_PACKET_SIZE: /* USB 2.0. */
                ret = UsbHostSpeed_HighSpeed;
                break;
            case USB_SS_EP_MAX_PACKET_SIZE: /* USB 3.0. */
                ret = UsbHostSpeed_SuperSpeed;
                break;
            default:
                break;
        }
    }

    return ret;
}

bool usbSendFileProperties(u64 file_size, const char *filename)
{
    bool ret = false;
    SCOPED_LOCK(&g_usbInterfaceMutex) ret = _usbSendFileProperties(file_size, filename, 0, false);
    return ret;
}

bool usbSendNspProperties(u64 nsp_size, const char *filename, u32 nsp_header_size)
{
    bool ret = false;
    SCOPED_LOCK(&g_usbInterfaceMutex) ret = _usbSendFileProperties(nsp_size, filename, nsp_header_size, true);
    return ret;
}

bool usbSendFileData(void *data, u64 data_size)
{
    bool ret = false;

    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        void *buf = NULL;
        bool zlt_required = false;

        if (!g_usbTransferBuffer || !g_usbInterfaceInit || !g_usbHostAvailable || !g_usbSessionStarted || !g_usbTransferRemainingSize || !data || !data_size || \
            data_size > USB_TRANSFER_BUFFER_SIZE || data_size > g_usbTransferRemainingSize)
        {
            LOG_MSG_ERROR("Invalid parameters!");
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
                LOG_MSG_DEBUG("ZLT enabled. Last chunk size: 0x%lX bytes.", data_size);
            }
        } else {
            /* Disable ZLT if this is the first of multiple data chunks. */
            if (!g_usbTransferWrittenSize)
            {
                usbSetZltPacket(false);
                LOG_MSG_DEBUG("ZLT disabled (first chunk).");
            }
        }

        /* Send data chunk. */
        if (!(ret = usbWrite(buf, data_size)))
        {
            LOG_MSG_ERROR("Failed to write 0x%lX bytes long file data chunk from offset 0x%lX! (total size: 0x%lX).", data_size, g_usbTransferWrittenSize, \
                                                                                                                      g_usbTransferRemainingSize + g_usbTransferWrittenSize);
            goto end;
        }

        g_usbTransferRemainingSize -= data_size;
        g_usbTransferWrittenSize += data_size;

        /* Check if this is the last chunk. */
        if (!g_usbTransferRemainingSize)
        {
            /* Check response from host device. */
            if (!(ret = usbRead(g_usbTransferBuffer, sizeof(UsbStatus))))
            {
                LOG_MSG_ERROR("Failed to read 0x%lX bytes long status block!", sizeof(UsbStatus));
                goto end;
            }

            UsbStatus *cmd_status = (UsbStatus*)g_usbTransferBuffer;

            if (!(ret = (cmd_status->magic == __builtin_bswap32(USB_CMD_HEADER_MAGIC))))
            {
                LOG_MSG_ERROR("Invalid status block magic word! (0x%08X).", __builtin_bswap32(cmd_status->magic));
                goto end;
            }

            ret = (cmd_status->status == UsbStatusType_Success);
#if LOG_LEVEL <= LOG_LEVEL_INFO
            if (!ret) usbLogStatusDetail(cmd_status->status);
#endif
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
    }

    return ret;
}

void usbCancelFileTransfer(void)
{
    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        if (!g_usbInterfaceInit || !g_usbTransferBuffer || !g_usbHostAvailable || !g_usbSessionStarted || (!g_usbTransferRemainingSize && !g_nspTransferMode)) break;

        /* Reset variables right away. */
        g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
        g_nspTransferMode = false;

        /* Prepare command data. */
        usbPrepareCommandHeader(UsbCommandType_CancelFileTransfer, 0);

        /* Send command. We don't care about the result here. */
        usbSendCommand();
    }
}

bool usbSendNspHeader(void *nsp_header, u32 nsp_header_size)
{
    bool ret = false;

    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        if (!g_usbInterfaceInit || !g_usbTransferBuffer || !g_usbHostAvailable || !g_usbSessionStarted || g_usbTransferRemainingSize || !g_nspTransferMode || !nsp_header || \
            !nsp_header_size || nsp_header_size > (USB_TRANSFER_BUFFER_SIZE - sizeof(UsbCommandHeader)))
        {
            LOG_MSG_ERROR("Invalid parameters!");
            break;
        }

        /* Disable NSP transfer mode right away. */
        g_nspTransferMode = false;

        /* Prepare command data. */
        usbPrepareCommandHeader(UsbCommandType_SendNspHeader, nsp_header_size);
        memcpy(g_usbTransferBuffer + sizeof(UsbCommandHeader), nsp_header, nsp_header_size);

        /* Send command. */
        ret = usbSendCommand();
    }

    return ret;
}

bool usbStartExtractedFsDump(u64 extracted_fs_size, const char *extracted_fs_root_path)
{
    bool ret = false;

    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        if (!g_usbInterfaceInit || !g_usbTransferBuffer || !g_usbHostAvailable || !g_usbSessionStarted || g_usbTransferRemainingSize || g_nspTransferMode || !extracted_fs_size || \
            !extracted_fs_root_path || !*extracted_fs_root_path) break;

        /* Prepare command data. */
        usbPrepareCommandHeader(UsbCommandType_StartExtractedFsDump, (u32)sizeof(UsbCommandStartExtractedFsDump));

        UsbCommandStartExtractedFsDump *cmd_block = (UsbCommandStartExtractedFsDump*)(g_usbTransferBuffer + sizeof(UsbCommandHeader));
        memset(cmd_block, 0, sizeof(UsbCommandStartExtractedFsDump));

        cmd_block->extracted_fs_size = extracted_fs_size;
        snprintf(cmd_block->extracted_fs_root_path, sizeof(cmd_block->extracted_fs_root_path), "%s", extracted_fs_root_path);

        /* Send command. */
        ret = usbSendCommand();
    }

    return ret;
}

void usbEndExtractedFsDump(void)
{
    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        if (!g_usbInterfaceInit || !g_usbTransferBuffer || !g_usbHostAvailable || !g_usbSessionStarted || g_usbTransferRemainingSize || g_nspTransferMode) break;

        /* Prepare command data. */
        usbPrepareCommandHeader(UsbCommandType_EndExtractedFsDump, 0);

        /* Send command. We don't care about the result here. */
        usbSendCommand();
    }
}

static bool usbCreateDetectionThread(void)
{
    if (!utilsCreateThread(&g_usbDetectionThread, usbDetectionThreadFunc, NULL, 1))
    {
        LOG_MSG_ERROR("Failed to create USB detection thread!");
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

    bool exit_flag = false;

    while(true)
    {
        /* Wait until an event is triggered. */
        rc = waitMulti(&idx, -1, usb_change_event_waiter, usb_timeout_event_waiter, exit_event_waiter);
        if (R_FAILED(rc)) continue;

        /* Exit event triggered. */
        if (idx == 2) break;

        SCOPED_LOCK(&g_usbInterfaceMutex)
        {
            /* Retrieve current USB connection status. */
            /* Only proceed if we're dealing with a status change. */
            g_usbHostAvailable = usbIsHostAvailable();
            g_usbSessionStarted = false;
            g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
            g_usbEndpointMaxPacketSize = 0;

            /* Start a USB session if we're connected to a host device. */
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
                    LOG_MSG_INFO("USB session successfully established. Endpoint max packet size: 0x%04X.", g_usbEndpointMaxPacketSize);
                } else {
                    /* Update exit flag. */
                    exit_flag = g_usbDetectionThreadExitFlag;
                }
            }
        }

        /* Check if the exit event was triggered while waiting for a session to be established. */
        if (exit_flag) break;
    }

    SCOPED_LOCK(&g_usbInterfaceMutex)
    {
        /* Close USB session if needed. */
        if (g_usbHostAvailable && g_usbSessionStarted) usbEndSession();
        g_usbHostAvailable = g_usbSessionStarted = g_usbDetectionThreadExitFlag = false;
        g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
        g_usbEndpointMaxPacketSize = 0;
    }

    threadExit();
}

static bool usbStartSession(void)
{
    UsbCommandStartSession *cmd_block = NULL;
    bool ret = false;

    if (!g_usbInterfaceInit || !g_usbTransferBuffer)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        goto end;
    }

    usbPrepareCommandHeader(UsbCommandType_StartSession, (u32)sizeof(UsbCommandStartSession));

    cmd_block = (UsbCommandStartSession*)(g_usbTransferBuffer + sizeof(UsbCommandHeader));
    memset(cmd_block, 0, sizeof(UsbCommandStartSession));

    cmd_block->app_ver_major = VERSION_MAJOR;
    cmd_block->app_ver_minor = VERSION_MINOR;
    cmd_block->app_ver_micro = VERSION_MICRO;
    cmd_block->abi_version = USB_ABI_VERSION;
    snprintf(cmd_block->git_commit, sizeof(cmd_block->git_commit), "%s", GIT_COMMIT);

    ret = usbSendCommand();
    if (ret)
    {
        /* Get the endpoint max packet size from the response sent by the USB host. */
        /* This is done to accurately know when and where to enable Zero Length Termination (ZLT) packets during bulk transfers. */
        /* As much as I'd like to avoid this, the GetUsbDeviceSpeed cmd from usb:ds is only available in HOS 8.0.0+ -- and we definitely want to provide USB comms under older versions. */
        g_usbEndpointMaxPacketSize = ((UsbStatus*)g_usbTransferBuffer)->max_packet_size;
        if (g_usbEndpointMaxPacketSize != USB_FS_EP_MAX_PACKET_SIZE && g_usbEndpointMaxPacketSize != USB_HS_EP_MAX_PACKET_SIZE && g_usbEndpointMaxPacketSize != USB_SS_EP_MAX_PACKET_SIZE)
        {
            LOG_MSG_ERROR("Invalid endpoint max packet size value received from USB host: 0x%04X.", g_usbEndpointMaxPacketSize);

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
    if (!g_usbInterfaceInit || !g_usbTransferBuffer || !g_usbHostAvailable || !g_usbSessionStarted)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    /* Prepare command data. */
    usbPrepareCommandHeader(UsbCommandType_EndSession, 0);

    /* Send command. We don't care about the result here. */
    usbSendCommand();
}

NX_INLINE void usbPrepareCommandHeader(u32 cmd, u32 cmd_block_size)
{
    if (cmd >= UsbCommandType_Count) return;
    UsbCommandHeader *cmd_header = (UsbCommandHeader*)g_usbTransferBuffer;
    memset(cmd_header, 0, sizeof(UsbCommandHeader));
    cmd_header->magic = __builtin_bswap32(USB_CMD_HEADER_MAGIC);
    cmd_header->cmd = cmd;
    cmd_header->cmd_block_size = cmd_block_size;
}

static bool usbSendCommand(void)
{
    UsbCommandHeader *cmd_header = (UsbCommandHeader*)g_usbTransferBuffer;
    u32 cmd_block_size = cmd_header->cmd_block_size;

#if LOG_LEVEL <= LOG_LEVEL_ERROR
    u32 cmd = cmd_header->cmd;
#endif

#if LOG_LEVEL <= LOG_LEVEL_INFO
    UsbCommandHeader cmd_header_bkp = {0};
    memcpy(&cmd_header_bkp, cmd_header, sizeof(UsbCommandHeader));
#endif

    UsbStatus *cmd_status = (UsbStatus*)g_usbTransferBuffer;
    u32 status = UsbStatusType_Success;

    bool ret = false, zlt_required = false, cmd_block_written = false;

    if ((sizeof(UsbCommandHeader) + cmd_block_size) > USB_TRANSFER_BUFFER_SIZE)
    {
        LOG_MSG_ERROR("Invalid command size!");
        status = UsbStatusType_InvalidCommandSize;
        goto end;
    }

    /* Write command header first. */
    if (!usbWrite(cmd_header, sizeof(UsbCommandHeader)))
    {
        if (!g_usbDetectionThreadExitFlag) LOG_MSG_ERROR("Failed to write header for type 0x%X command!", cmd);
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
            LOG_MSG_ERROR("Failed to write command block for type 0x%X command!", cmd);
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
        LOG_MSG_ERROR("Failed to read 0x%lX bytes long status block for type 0x%X command!", sizeof(UsbStatus), cmd);
        status = UsbStatusType_ReadStatusFailed;
        goto end;
    }

    /* Verify magic word in status block. */
    if (cmd_status->magic != __builtin_bswap32(USB_CMD_HEADER_MAGIC))
    {
        LOG_MSG_ERROR("Invalid status block magic word! (0x%08X).", __builtin_bswap32(cmd_status->magic));
        status = UsbStatusType_InvalidMagicWord;
        goto end;
    }

    /* Update return value. */
    ret = ((status = cmd_status->status) == UsbStatusType_Success);

end:
#if LOG_LEVEL <= LOG_LEVEL_INFO
    if (!ret)
    {
        usbLogStatusDetail(status);

        if (status > UsbStatusType_ReadStatusFailed)
        {
            LOG_DATA_INFO(&cmd_header_bkp, sizeof(cmd_header_bkp), "USB command header dump:");
            if (cmd_block_size) LOG_DATA_INFO(g_usbTransferBuffer, cmd_block_size, "USB command block dump:");
        }
    }
#endif

    return ret;
}

#if LOG_LEVEL <= LOG_LEVEL_INFO
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
            LOG_MSG_INFO("Host replied with Invalid Magic Word status code.");
            break;
        case UsbStatusType_UnsupportedCommand:
            LOG_MSG_INFO("Host replied with Unsupported Command status code.");
            break;
        case UsbStatusType_UnsupportedAbiVersion:
            LOG_MSG_INFO("Host replied with Unsupported ABI Version status code.");
            break;
        case UsbStatusType_MalformedCommand:
            LOG_MSG_INFO("Host replied with Malformed Command status code.");
            break;
        case UsbStatusType_HostIoError:
            LOG_MSG_INFO("Host replied with I/O Error status code.");
            break;
        default:
            LOG_MSG_INFO("Unknown status code: 0x%X.", status);
            break;
    }
}
#endif

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
    bool ret = false, is_5x = hosversionAtLeast(5, 0, 0);

    /* Carry out USB comms initialization steps for this HOS version. */
    ret = (is_5x ? usbInitializeComms5x() : usbInitializeComms1x());
    if (!ret) goto end;

    /* Enable USB interface. */
    /* This is always needed regardless of the HOS version. */
    rc = usbDsInterface_EnableInterface(g_usbInterface);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_EnableInterface failed! (0x%X).", rc);
        goto end;
    }

    /* Additional step needed under HOS 5.0.0+. */
    if (is_5x)
    {
        rc = usbDsEnable();
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("usbDsEnable failed! (0x%X).", rc);
            goto end;
        }

        g_usbHos5xEnabled = true;
    }

    ret = true;

end:
    if (!ret) usbCloseComms();

    return ret;
}

static bool usbInitializeComms5x(void)
{
    Result rc = 0;
    bool ret = false;

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
    bos_desc->wTotalLength = sizeof(bos);
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

    /* Set language string descriptor. */
    rc = usbDsAddUsbLanguageStringDescriptor(NULL, supported_langs, num_supported_langs);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsAddUsbLanguageStringDescriptor failed! (0x%X).", rc);
        goto end;
    }

    /* Set manufacturer string descriptor. */
    rc = usbDsAddUsbStringDescriptor(&(device_descriptor.iManufacturer), APP_AUTHOR);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsAddUsbStringDescriptor failed! (0x%X) (manufacturer).", rc);
        goto end;
    }

    /* Set product string descriptor. */
    rc = usbDsAddUsbStringDescriptor(&(device_descriptor.iProduct), APP_TITLE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsAddUsbStringDescriptor failed! (0x%X) (product).", rc);
        goto end;
    }

    /* Set serial number string descriptor. */
    rc = usbDsAddUsbStringDescriptor(&(device_descriptor.iSerialNumber), APP_VERSION);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsAddUsbStringDescriptor failed! (0x%X) (serial number).", rc);
        goto end;
    }

    /* Set device descriptors. */
    rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Full, &device_descriptor);  /* Full Speed is USB 1.1. */
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsSetUsbDeviceDescriptor failed! (0x%X) (USB 1.1).", rc);
        goto end;
    }

    device_descriptor.bcdUSB = USB_HS_BCD_REVISION;
    rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_High, &device_descriptor);  /* High Speed is USB 2.0. */
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsSetUsbDeviceDescriptor failed! (0x%X) (USB 2.0).", rc);
        goto end;
    }

    device_descriptor.bcdUSB = USB_SS_BCD_REVISION;
    device_descriptor.bMaxPacketSize0 = USB_SS_EP0_MAX_PACKET_SIZE;
    rc = usbDsSetUsbDeviceDescriptor(UsbDeviceSpeed_Super, &device_descriptor); /* Super Speed is USB 3.0. */
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsSetUsbDeviceDescriptor failed! (0x%X) (USB 3.0).", rc);
        goto end;
    }

    /* Set Binary Object Store. */
    rc = usbDsSetBinaryObjectStore(bos, sizeof(bos));
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsSetBinaryObjectStore failed! (0x%X).", rc);
        goto end;
    }

    /* Setup interface. */
    rc = usbDsRegisterInterface(&g_usbInterface);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsRegisterInterface failed! (0x%X).", rc);
        goto end;
    }

    interface_descriptor.bInterfaceNumber = g_usbInterface->interface_index;
    endpoint_descriptor_in.bEndpointAddress += (interface_descriptor.bInterfaceNumber + 1);
    endpoint_descriptor_out.bEndpointAddress += (interface_descriptor.bInterfaceNumber + 1);

    /* Full Speed config (USB 1.1). */
    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Full, &interface_descriptor, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 1.1) (interface).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Full, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 1.1) (in endpoint).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Full, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 1.1) (out endpoint).", rc);
        goto end;
    }

    /* High Speed config (USB 2.0). */
    endpoint_descriptor_in.wMaxPacketSize = endpoint_descriptor_out.wMaxPacketSize = USB_HS_EP_MAX_PACKET_SIZE;

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_High, &interface_descriptor, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 2.0) (interface).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_High, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 2.0) (in endpoint).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_High, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 2.0) (out endpoint).", rc);
        goto end;
    }

    /* Super Speed config (USB 3.0). */
    endpoint_descriptor_in.wMaxPacketSize = endpoint_descriptor_out.wMaxPacketSize = USB_SS_EP_MAX_PACKET_SIZE;

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Super, &interface_descriptor, USB_DT_INTERFACE_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 3.0) (interface).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Super, &endpoint_descriptor_in, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 3.0) (in endpoint).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Super, &endpoint_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 3.0) (in endpoint companion).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Super, &endpoint_descriptor_out, USB_DT_ENDPOINT_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 3.0) (out endpoint).", rc);
        goto end;
    }

    rc = usbDsInterface_AppendConfigurationData(g_usbInterface, UsbDeviceSpeed_Super, &endpoint_companion, USB_DT_SS_ENDPOINT_COMPANION_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_AppendConfigurationData failed! (0x%X) (USB 3.0) (out endpoint companion).", rc);
        goto end;
    }

    /* Setup endpoints. */
    rc = usbDsInterface_RegisterEndpoint(g_usbInterface, &g_usbEndpointIn, endpoint_descriptor_in.bEndpointAddress);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_RegisterEndpoint failed! (0x%X) (in endpoint).", rc);
        goto end;
    }

    rc = usbDsInterface_RegisterEndpoint(g_usbInterface, &g_usbEndpointOut, endpoint_descriptor_out.bEndpointAddress);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_RegisterEndpoint failed! (0x%X) (out endpoint).", rc);
        goto end;
    }

    ret = true;

end:
    return ret;
}

static bool usbInitializeComms1x(void)
{
    Result rc = 0;
    bool ret = false;

    static const UsbDsDeviceInfo device_info = {
        .idVendor = USB_DEV_VID,
        .idProduct = USB_DEV_PID,
        .bcdDevice = USB_DEV_BCD_REL,
        .Manufacturer = APP_AUTHOR,
        .Product = APP_TITLE,
        .SerialNumber = APP_VERSION
    };

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

    /* Set VID, PID and BCD. */
    rc = usbDsSetVidPidBcd(&device_info);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsSetVidPidBcd failed! (0x%X).", rc);
        goto end;
    }

    /* Setup interface. */
    rc = usbDsGetDsInterface(&g_usbInterface, &interface_descriptor, "usb");
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsGetDsInterface failed! (0x%X).", rc);
        goto end;
    }

    /* Setup endpoints. */
    rc = usbDsInterface_GetDsEndpoint(g_usbInterface, &g_usbEndpointIn, &endpoint_descriptor_in);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_GetDsEndpoint failed! (0x%X) (in endpoint).", rc);
        goto end;
    }

    rc = usbDsInterface_GetDsEndpoint(g_usbInterface, &g_usbEndpointOut, &endpoint_descriptor_out);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsInterface_GetDsEndpoint failed! (0x%X) (out endpoint).", rc);
        goto end;
    }

    ret = true;

end:
    return ret;
}

static void usbCloseComms(void)
{
    bool is_5x = hosversionAtLeast(5, 0, 0);

    if (is_5x && g_usbHos5xEnabled)
    {
        usbDsDisable();
        g_usbHos5xEnabled = false;
    }

    if (g_usbEndpointOut)
    {
        usbDsEndpoint_Close(g_usbEndpointOut);
        g_usbEndpointOut = NULL;
    }

    if (g_usbEndpointIn)
    {
        usbDsEndpoint_Close(g_usbEndpointIn);
        g_usbEndpointIn = NULL;
    }

    if (g_usbInterface)
    {
        /* usbDsInterface_DisableInterface() is internally called here. */
        usbDsInterface_Close(g_usbInterface);
        g_usbInterface = NULL;
    }

    if (is_5x) usbDsClearDeviceData();
}

static bool _usbSendFileProperties(u64 file_size, const char *filename, u32 nsp_header_size, bool enforce_nsp_mode)
{
    bool ret = false;
    size_t filename_length = 0;

    /* Disallow sending new files if we're not in NSP transfer mode and the remaining transfer size isn't zero. */
    /* Allow empty files if we're not in NSP transfer mode. */
    /* Disallow sending new NSPs if we're already in NSP transfer mode. */
    if (!g_usbInterfaceInit || !g_usbTransferBuffer || !g_usbHostAvailable || !g_usbSessionStarted || (!g_nspTransferMode && g_usbTransferRemainingSize) || \
        !filename || !(filename_length = strlen(filename)) || filename_length >= FS_MAX_PATH || (!enforce_nsp_mode && nsp_header_size) || \
        (enforce_nsp_mode && (g_nspTransferMode || !file_size || !nsp_header_size || nsp_header_size >= file_size)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Prepare command data. */
    usbPrepareCommandHeader(UsbCommandType_SendFileProperties, (u32)sizeof(UsbCommandSendFileProperties));

    UsbCommandSendFileProperties *cmd_block = (UsbCommandSendFileProperties*)(g_usbTransferBuffer + sizeof(UsbCommandHeader));
    memset(cmd_block, 0, sizeof(UsbCommandSendFileProperties));

    cmd_block->file_size = file_size;
    cmd_block->filename_length = (u32)filename_length;
    cmd_block->nsp_header_size = nsp_header_size;
    snprintf(cmd_block->filename, sizeof(cmd_block->filename), "%s", filename);

    /* Send command. */
    ret = usbSendCommand();
    if (ret)
    {
        g_usbTransferRemainingSize = file_size;
        g_usbTransferWrittenSize = 0;
        if (!g_nspTransferMode && enforce_nsp_mode) g_nspTransferMode = true;
    } else {
        g_usbTransferRemainingSize = g_usbTransferWrittenSize = 0;
        g_nspTransferMode = false;
    }

    return ret;
}

NX_INLINE bool usbIsHostAvailable(void)
{
    UsbState state = UsbState_Detached;
    Result rc = usbDsGetState(&state);
    return (R_SUCCEEDED(rc) && state == UsbState_Configured);
}

NX_INLINE void usbSetZltPacket(bool enable)
{
    usbDsEndpoint_SetZlt(g_usbEndpointIn, enable);
}

NX_INLINE bool usbRead(void *buf, u64 size)
{
    return usbTransferData(buf, size, g_usbEndpointOut);
}

NX_INLINE bool usbWrite(void *buf, u64 size)
{
    return usbTransferData(buf, size, g_usbEndpointIn);
}

static bool usbTransferData(void *buf, u64 size, UsbDsEndpoint *endpoint)
{
    if (!buf || !IS_ALIGNED((u64)buf, USB_TRANSFER_ALIGNMENT) || !size || !endpoint)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    if (!usbIsHostAvailable())
    {
        LOG_MSG_ERROR("USB host unavailable!");
        return false;
    }

    Result rc = 0;
    UsbDsReportData report_data = {0};
    u32 urb_id = 0, transferred_size = 0;
    bool thread_exit = false;

    /* Start a USB transfer using the provided endpoint. */
    rc = usbDsEndpoint_PostBufferAsync(endpoint, buf, size, &urb_id);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsEndpoint_PostBufferAsync failed! (0x%X) (URB ID %u).", rc, urb_id);
        return false;
    }

    /* Wait for the transfer to finish. */
    if (g_usbSessionStarted)
    {
        /* If the USB session has already been established, then use a regular timeout value. */
        rc = eventWait(&(endpoint->CompletionEvent), USB_TRANSFER_TIMEOUT * (u64)1000000000);
    } else {
        /* If we're starting a USB session, wait indefinitely inside a loop to let the user start the host script. */
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

        if (!thread_exit) LOG_MSG_ERROR("eventWait failed! (0x%X) (URB ID %u).", rc, urb_id);

        return false;
    }

    rc = usbDsEndpoint_GetReportData(endpoint, &report_data);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsEndpoint_GetReportData failed! (0x%X) (URB ID %u).", rc, urb_id);
        return false;
    }

    rc = usbDsParseReportData(&report_data, urb_id, NULL, &transferred_size);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("usbDsParseReportData failed! (0x%X) (URB ID %u).", rc, urb_id);
        return false;
    }

    if (transferred_size != size)
    {
        LOG_MSG_ERROR("USB transfer failed! Expected 0x%lX bytes, got 0x%X bytes (URB ID %u).", size, transferred_size, urb_id);
        return false;
    }

    return true;
}
