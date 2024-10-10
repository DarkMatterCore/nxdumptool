/*
 * npdm.h
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

#ifndef __NPDM_H__
#define __NPDM_H__

#include "pfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NPDM_META_MAGIC                         0x4D455441  /* "META". */
#define NPDM_ACID_MAGIC                         0x41434944  /* "ACID". */
#define NPDM_ACI0_MAGIC                         0x41434930  /* "ACI0". */

#define NPDM_MAIN_THREAD_MAX_PRIORITY           0x3F
#define NPDM_MAIN_THREAD_MAX_CORE_NUMBER        3
#define NPDM_SYSTEM_RESOURCE_MAX_SIZE           0x1FE00000
#define NPDM_MAIN_THREAD_STACK_SIZE_ALIGNMENT   0x1000

/// 'NpdmSignatureKeyGeneration_Current' will always point to the last known key generation value.
/// TODO: update on signature keygen changes.
typedef enum {
    NpdmSignatureKeyGeneration_Since100NUP = 0,                                         ///< 1.0.0 - 8.1.1.
    NpdmSignatureKeyGeneration_Since900NUP = 1,                                         ///< 9.0.0+.
    NpdmSignatureKeyGeneration_Current     = NpdmSignatureKeyGeneration_Since900NUP,
    NpdmSignatureKeyGeneration_Max         = (NpdmSignatureKeyGeneration_Current + 1)
} NpdmSignatureKeyGeneration;

typedef enum {
    NpdmProcessAddressSpace_AddressSpace32Bit           = 0,
    NpdmProcessAddressSpace_AddressSpace64BitOld        = 1,
    NpdmProcessAddressSpace_AddressSpace32BitNoReserved = 2,
    NpdmProcessAddressSpace_AddressSpace64Bit           = 3,
    NpdmProcessAddressSpace_Count                       = 4     ///< Total values supported by this enum.
} NpdmProcessAddressSpace;

typedef struct {
    u8 is_64bit_instruction               : 1;
    u8 process_address_space              : 3;  ///< NpdmProcessAddressSpace.
    u8 optimize_memory_allocation         : 1;
    u8 disable_device_address_space_merge : 1;
    u8 enable_alias_region_extra_size     : 1;
    u8 prevent_code_reads                 : 1;
} NpdmMetaFlags;

NXDT_ASSERT(NpdmMetaFlags, 0x1);

/// This is the start of every NPDM file.
/// This is followed by ACID and ACI0 sections, both with variable offsets and sizes.
typedef struct {
    u32 magic;                          ///< "NPDM".
    u8 acid_signature_key_generation;   ///< NpdmSignatureKeyGeneration.
    u8 reserved_1[0x7];
    NpdmMetaFlags flags;
    u8 reserved_2;
    u8 main_thread_priority;            ///< Must not exceed NPDM_MAIN_THREAD_MAX_PRIORITY.
    u8 main_thread_core_number;         ///< Must not exceed NPDM_MAIN_THREAD_MAX_CORE_NUMBER.
    u8 reserved_3[0x4];
    u32 system_resource_size;           ///< Must not exceed NPDM_SYSTEM_RESOURCE_MAX_SIZE.
    Version version;
    u32 main_thread_stack_size;         ///< Must be aligned to NPDM_MAIN_THREAD_STACK_SIZE_ALIGNMENT.
    char name[0x10];                    ///< Usually set to "Application".
    char product_code[0x10];            ///< Usually zeroed out.
    u8 reserved_4[0x30];
    u32 aci_offset;                     ///< Offset value relative to the start of this header.
    u32 aci_size;
    u32 acid_offset;                    ///< Offset value relative to the start of this header.
    u32 acid_size;
} NpdmMetaHeader;

NXDT_ASSERT(NpdmMetaHeader, 0x80);

typedef enum {
    NpdmMemoryRegion_Application     = 0,
    NpdmMemoryRegion_Applet          = 1,
    NpdmMemoryRegion_SecureSystem    = 2,
    NpdmMemoryRegion_NonSecureSystem = 3,
    NpdmMemoryRegion_Count           = 4,                               ///< Total values supported by this enum.

    /// Old.
    NpdmMemoryRegion_NonSecure       = NpdmMemoryRegion_Application,
    NpdmMemoryRegion_Secure          = NpdmMemoryRegion_Applet
} NpdmMemoryRegion;

typedef struct {
    u32 production           : 1;
    u32 unqualified_approval : 1;
    u32 memory_region        : 4;   ///< NpdmMemoryRegion.
    u32 reserved             : 26;
} NpdmAcidFlags;

NXDT_ASSERT(NpdmAcidFlags, 0x4);

/// This is the start of an ACID section.
/// This is followed by FsAccessControl, SrvAccessControl and KernelCapability descriptors, each one aligned to a 0x10 byte boundary using zero padding (if needed).
typedef struct {
    u8 signature[0x100];            ///< RSA-2048-PSS with SHA-256 signature over the rest of the ACID section, using the value from the 'size' member.
    u8 public_key[0x100];           ///< RSA public key used to verify the ACID signature from the Program NCA header.
    u32 magic;                      ///< "ACID".
    u32 size;                       ///< Must be equal to ACID section size from the META header minus 0x100 (ACID signature size).
    u8 version;                     ///< 9.0.0+.
    u8 unknown;                     ///< 14.0.0+.
    u8 reserved_1[0x2];
    NpdmAcidFlags flags;
    u64 program_id_min;
    u64 program_id_max;
    u32 fs_access_control_offset;   ///< Offset value relative to the start of this header.
    u32 fs_access_control_size;
    u32 srv_access_control_offset;  ///< Offset value relative to the start of this header.
    u32 srv_access_control_size;
    u32 kernel_capability_offset;   ///< Offset value relative to the start of this header.
    u32 kernel_capability_size;
    u8 reserved_2[0x8];
} NpdmAcidHeader;

NXDT_ASSERT(NpdmAcidHeader, 0x240);

/// This is the start of an ACI0 section.
/// This is followed by a FsAccessControl data block, as well as SrvAccessControl and KernelCapability descriptors, each one aligned to a 0x10 byte boundary using zero padding (if needed).
typedef struct {
    u32 magic;
    u8 reserved_1[0xC];
    u64 program_id;
    u8 reserved_2[0x8];
    u32 fs_access_control_offset;   ///< Offset value relative to the start of this header.
    u32 fs_access_control_size;
    u32 srv_access_control_offset;  ///< Offset value relative to the start of this header.
    u32 srv_access_control_size;
    u32 kernel_capability_offset;   ///< Offset value relative to the start of this header.
    u32 kernel_capability_size;
    u8 reserved_3[0x8];
} NpdmAciHeader;

NXDT_ASSERT(NpdmAciHeader, 0x40);

typedef enum {
    NpdmFsAccessControlFlags_None                           = 0,
    NpdmFsAccessControlFlags_ApplicationInfo                = BITL(0),
    NpdmFsAccessControlFlags_BootModeControl                = BITL(1),
    NpdmFsAccessControlFlags_Calibration                    = BITL(2),
    NpdmFsAccessControlFlags_SystemSaveData                 = BITL(3),
    NpdmFsAccessControlFlags_GameCard                       = BITL(4),
    NpdmFsAccessControlFlags_SaveDataBackUp                 = BITL(5),
    NpdmFsAccessControlFlags_SaveDataManagement             = BITL(6),
    NpdmFsAccessControlFlags_BisAllRaw                      = BITL(7),
    NpdmFsAccessControlFlags_GameCardRaw                    = BITL(8),
    NpdmFsAccessControlFlags_GameCardPrivate                = BITL(9),
    NpdmFsAccessControlFlags_SetTime                        = BITL(10),
    NpdmFsAccessControlFlags_ContentManager                 = BITL(11),
    NpdmFsAccessControlFlags_ImageManager                   = BITL(12),
    NpdmFsAccessControlFlags_CreateSaveData                 = BITL(13),
    NpdmFsAccessControlFlags_SystemSaveDataManagement       = BITL(14),
    NpdmFsAccessControlFlags_BisFileSystem                  = BITL(15),
    NpdmFsAccessControlFlags_SystemUpdate                   = BITL(16),
    NpdmFsAccessControlFlags_SaveDataMeta                   = BITL(17),
    NpdmFsAccessControlFlags_DeviceSaveData                 = BITL(18),
    NpdmFsAccessControlFlags_SettingsControl                = BITL(19),
    NpdmFsAccessControlFlags_SystemData                     = BITL(20),
    NpdmFsAccessControlFlags_SdCard                         = BITL(21),
    NpdmFsAccessControlFlags_Host                           = BITL(22),
    NpdmFsAccessControlFlags_FillBis                        = BITL(23),
    NpdmFsAccessControlFlags_CorruptSaveData                = BITL(24),
    NpdmFsAccessControlFlags_SaveDataForDebug               = BITL(25),
    NpdmFsAccessControlFlags_FormatSdCard                   = BITL(26),
    NpdmFsAccessControlFlags_GetRightsId                    = BITL(27),
    NpdmFsAccessControlFlags_RegisterExternalKey            = BITL(28),
    NpdmFsAccessControlFlags_RegisterUpdatePartition        = BITL(29),
    NpdmFsAccessControlFlags_SaveDataTransfer               = BITL(30),
    NpdmFsAccessControlFlags_DeviceDetection                = BITL(31),
    NpdmFsAccessControlFlags_AccessFailureResolution        = BITL(32),
    NpdmFsAccessControlFlags_SaveDataTransferVersion2       = BITL(33),
    NpdmFsAccessControlFlags_RegisterProgramIndexMapInfo    = BITL(34),
    NpdmFsAccessControlFlags_CreateOwnSaveData              = BITL(35),
    NpdmFsAccessControlFlags_MoveCacheStorage               = BITL(36),
    NpdmFsAccessControlFlags_DeviceTreeBlob                 = BITL(37),
    NpdmFsAccessControlFlags_NotifyErrorContextServiceReady = BITL(38),
    NpdmFsAccessControlFlags_Debug                          = BITL(62),
    NpdmFsAccessControlFlags_FullPermission                 = BITL(63),
    NpdmFsAccessControlFlags_Count                          = 64        ///< Total values supported by this enum.
} NpdmFsAccessControlFlags;

/// FsAccessControl descriptor. Part of the ACID section body.
/// This is followed by:
///     * 'content_owner_id_count' content owner IDs.
///     * 'save_data_owner_id_count' save data owner IDs.
#pragma pack(push, 1)
typedef struct {
    u8 version;                     ///< Always non-zero. Usually set to 1.
    u8 content_owner_id_count;
    u8 save_data_owner_id_count;
    u8 reserved;
    u64 flags;                      ///< NpdmFsAccessControlFlags.
    u64 content_owner_id_min;
    u64 content_owner_id_max;
    u64 save_data_owner_id_min;
    u64 save_data_owner_id_max;
} NpdmFsAccessControlDescriptor;
#pragma pack(pop)

NXDT_ASSERT(NpdmFsAccessControlDescriptor, 0x2C);

/// FsAccessControl data. Part of the ACI0 section body.
/// This is followed by:
///     * A NpdmFsAccessControlDataContentOwnerBlock if 'content_owner_info_size' is greater than zero.
///     * A NpdmFsAccessControlDataSaveDataOwnerBlock if 'save_data_owner_info_size' is greater than zero.
///         * If available, this block is padded to a 0x4-byte boundary and followed by 'save_data_owner_id_count' save data owner IDs.
#pragma pack(push, 1)
typedef struct {
    u8 version;
    u8 reserved_1[0x3];
    u64 flags;                          ///< NpdmFsAccessControlFlags.
    u32 content_owner_info_offset;      ///< Relative to the start of this block. Only valid if 'content_owner_info_size' is greater than 0.
    u32 content_owner_info_size;
    u32 save_data_owner_info_offset;    ///< Relative to the start of this block. Only valid if 'save_data_owner_info_size' is greater than 0.
    u32 save_data_owner_info_size;
} NpdmFsAccessControlData;
#pragma pack(pop)

NXDT_ASSERT(NpdmFsAccessControlData, 0x1C);

/// Placed after NpdmFsAccessControlData if its 'content_owner_info_size' member is greater than zero.
#pragma pack(push, 1)
typedef struct {
    u32 content_owner_id_count;
    u64 content_owner_id[];     ///< 'content_owner_id_count' content owner IDs.
} NpdmFsAccessControlDataContentOwnerBlock;
#pragma pack(pop)

NXDT_ASSERT(NpdmFsAccessControlDataContentOwnerBlock, 0x4);

typedef enum {
    NpdmAccessibility_None      = 0,
    NpdmAccessibility_Read      = BIT(0),
    NpdmAccessibility_Write     = BIT(1),
    NpdmAccessibility_ReadWrite = (NpdmAccessibility_Read | NpdmAccessibility_Write),
    NpdmAccessibility_Count     = 3                                                     ///< Total values supported by this enum.
} NpdmAccessibility;

/// Placed after NpdmFsAccessControlData / NpdmFsAccessControlDataContentOwnerBlock if the 'save_data_owner_info_size' member from NpdmFsAccessControlData is greater than zero.
/// If available, this block is padded to a 0x4-byte boundary and followed by 'save_data_owner_id_count' save data owner IDs.
typedef struct {
    u32 save_data_owner_id_count;
    u8 accessibility[];             ///< 'save_data_owner_id_count' NpdmAccessibility fields.
} NpdmFsAccessControlDataSaveDataOwnerBlock;

NXDT_ASSERT(NpdmFsAccessControlDataSaveDataOwnerBlock, 0x4);

/// SrvAccessControl descriptor. Part of the ACID and ACI0 section bodies.
/// This descriptor is composed of a variable number of NpdmSrvAccessControlDescriptorEntry elements, each one with a variable size.
/// Since the total number of services isn't stored anywhere, this descriptor must be parsed until its total size is reached.
typedef struct {
    u8 name_length : 3; ///< Service name length minus 1.
    u8 reserved    : 4;
    u8 is_server   : 1; ///< Indicates if the service is allowed to be registered.
    char name[];        ///< Service name, stored without a NULL terminator. Supports the "*" wildcard character.
} NpdmSrvAccessControlDescriptorEntry;

NXDT_ASSERT(NpdmSrvAccessControlDescriptorEntry, 0x1);

/// KernelCapability descriptor. Part of the ACID and ACI0 section bodies.
/// This descriptor is composed of a variable number of u32 entries. Thus, the entry count can be calculated by dividing the KernelCapability descriptor size by 4.
/// The entry type is identified by a pattern of "01...11" (zero followed by ones) in the low u16, counting from the LSB. The variable number of ones must never exceed 16 (entirety of the low u16).
typedef struct {
    u32 value;
} NpdmKernelCapabilityDescriptorEntry;

NXDT_ASSERT(NpdmKernelCapabilityDescriptorEntry, 0x4);

typedef enum {
    NpdmKernelCapabilityEntryBitmaskSize_ThreadInfo        = 3,
    NpdmKernelCapabilityEntryBitmaskSize_EnableSystemCalls = 4,
    NpdmKernelCapabilityEntryBitmaskSize_MemoryMap         = 6,
    NpdmKernelCapabilityEntryBitmaskSize_IoMemoryMap       = 7,
    NpdmKernelCapabilityEntryBitmaskSize_MemoryRegionMap   = 10,
    NpdmKernelCapabilityEntryBitmaskSize_EnableInterrupts  = 11,
    NpdmKernelCapabilityEntryBitmaskSize_MiscParams        = 13,
    NpdmKernelCapabilityEntryBitmaskSize_KernelVersion     = 14,
    NpdmKernelCapabilityEntryBitmaskSize_HandleTableSize   = 15,
    NpdmKernelCapabilityEntryBitmaskSize_MiscFlags         = 16
} NpdmKernelCapabilityEntryBitmaskSize;

typedef enum {
    NpdmKernelCapabilityEntryBitmaskPattern_ThreadInfo        = BIT(NpdmKernelCapabilityEntryBitmaskSize_ThreadInfo)        - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_EnableSystemCalls = BIT(NpdmKernelCapabilityEntryBitmaskSize_EnableSystemCalls) - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_MemoryMap         = BIT(NpdmKernelCapabilityEntryBitmaskSize_MemoryMap)         - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_IoMemoryMap       = BIT(NpdmKernelCapabilityEntryBitmaskSize_IoMemoryMap)       - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_MemoryRegionMap   = BIT(NpdmKernelCapabilityEntryBitmaskSize_MemoryRegionMap)   - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_EnableInterrupts  = BIT(NpdmKernelCapabilityEntryBitmaskSize_EnableInterrupts)  - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_MiscParams        = BIT(NpdmKernelCapabilityEntryBitmaskSize_MiscParams)        - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_KernelVersion     = BIT(NpdmKernelCapabilityEntryBitmaskSize_KernelVersion)     - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_HandleTableSize   = BIT(NpdmKernelCapabilityEntryBitmaskSize_HandleTableSize)   - 1,
    NpdmKernelCapabilityEntryBitmaskPattern_MiscFlags         = BIT(NpdmKernelCapabilityEntryBitmaskSize_MiscFlags)         - 1
} NpdmKernelCapabilityEntryBitmaskPattern;

/// ThreadInfo entry for the KernelCapability descriptor.
typedef struct {
    u32 bitmask          : NpdmKernelCapabilityEntryBitmaskSize_ThreadInfo + 1; ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_ThreadInfo.
    u32 lowest_priority  : 6;
    u32 highest_priority : 6;
    u32 min_core_number  : 8;
    u32 max_core_number  : 8;
} NpdmThreadInfo;

NXDT_ASSERT(NpdmThreadInfo, 0x4);

/// System call table.
typedef enum {
    NpdmSystemCallId_None                           = 0,

    ///< System calls for index 0.
    NpdmSystemCallId_Reserved1                      = BIT(0),   ///< SVC 0x00.
    NpdmSystemCallId_SetHeapSize                    = BIT(1),   ///< SVC 0x01.
    NpdmSystemCallId_SetMemoryPermission            = BIT(2),   ///< SVC 0x02.
    NpdmSystemCallId_SetMemoryAttribute             = BIT(3),   ///< SVC 0x03.
    NpdmSystemCallId_MapMemory                      = BIT(4),   ///< SVC 0x04.
    NpdmSystemCallId_UnmapMemory                    = BIT(5),   ///< SVC 0x05.
    NpdmSystemCallId_QueryMemory                    = BIT(6),   ///< SVC 0x06.
    NpdmSystemCallId_ExitProcess                    = BIT(7),   ///< SVC 0x07.
    NpdmSystemCallId_CreateThread                   = BIT(8),   ///< SVC 0x08.
    NpdmSystemCallId_StartThread                    = BIT(9),   ///< SVC 0x09.
    NpdmSystemCallId_ExitThread                     = BIT(10),  ///< SVC 0x0A.
    NpdmSystemCallId_SleepThread                    = BIT(11),  ///< SVC 0x0B.
    NpdmSystemCallId_GetThreadPriority              = BIT(12),  ///< SVC 0x0C.
    NpdmSystemCallId_SetThreadPriority              = BIT(13),  ///< SVC 0x0D.
    NpdmSystemCallId_GetThreadCoreMask              = BIT(14),  ///< SVC 0x0E.
    NpdmSystemCallId_SetThreadCoreMask              = BIT(15),  ///< SVC 0x0F.
    NpdmSystemCallId_GetCurrentProcessorNumber      = BIT(16),  ///< SVC 0x10.
    NpdmSystemCallId_SignalEvent                    = BIT(17),  ///< SVC 0x11.
    NpdmSystemCallId_ClearEvent                     = BIT(18),  ///< SVC 0x12.
    NpdmSystemCallId_MapSharedMemory                = BIT(19),  ///< SVC 0x13.
    NpdmSystemCallId_UnmapSharedMemory              = BIT(20),  ///< SVC 0x14.
    NpdmSystemCallId_CreateTransferMemory           = BIT(21),  ///< SVC 0x15.
    NpdmSystemCallId_CloseHandle                    = BIT(22),  ///< SVC 0x16.
    NpdmSystemCallId_ResetSignal                    = BIT(23),  ///< SVC 0x17.

    ///< System calls for index 1.
    NpdmSystemCallId_WaitSynchronization            = BIT(0),   ///< SVC 0x18.
    NpdmSystemCallId_CancelSynchronization          = BIT(1),   ///< SVC 0x19.
    NpdmSystemCallId_ArbitrateLock                  = BIT(2),   ///< SVC 0x1A.
    NpdmSystemCallId_ArbitrateUnlock                = BIT(3),   ///< SVC 0x1B.
    NpdmSystemCallId_WaitProcessWideKeyAtomic       = BIT(4),   ///< SVC 0x1C.
    NpdmSystemCallId_SignalProcessWideKey           = BIT(5),   ///< SVC 0x1D.
    NpdmSystemCallId_GetSystemTick                  = BIT(6),   ///< SVC 0x1E.
    NpdmSystemCallId_ConnectToNamedPort             = BIT(7),   ///< SVC 0x1F.
    NpdmSystemCallId_SendSyncRequestLight           = BIT(8),   ///< SVC 0x20.
    NpdmSystemCallId_SendSyncRequest                = BIT(9),   ///< SVC 0x21.
    NpdmSystemCallId_SendSyncRequestWithUserBuffer  = BIT(10),  ///< SVC 0x22.
    NpdmSystemCallId_SendAsyncRequestWithUserBuffer = BIT(11),  ///< SVC 0x23.
    NpdmSystemCallId_GetProcessId                   = BIT(12),  ///< SVC 0x24.
    NpdmSystemCallId_GetThreadId                    = BIT(13),  ///< SVC 0x25.
    NpdmSystemCallId_Break                          = BIT(14),  ///< SVC 0x26.
    NpdmSystemCallId_OutputDebugString              = BIT(15),  ///< SVC 0x27.
    NpdmSystemCallId_ReturnFromException            = BIT(16),  ///< SVC 0x28.
    NpdmSystemCallId_GetInfo                        = BIT(17),  ///< SVC 0x29.
    NpdmSystemCallId_FlushEntireDataCache           = BIT(18),  ///< SVC 0x2A.
    NpdmSystemCallId_FlushDataCache                 = BIT(19),  ///< SVC 0x2B.
    NpdmSystemCallId_MapPhysicalMemory              = BIT(20),  ///< SVC 0x2C (3.0.0+).
    NpdmSystemCallId_UnmapPhysicalMemory            = BIT(21),  ///< SVC 0x2D (3.0.0+).
    NpdmSystemCallId_GetDebugFutureThreadInfo       = BIT(22),  ///< SVC 0x2E (6.0.0+). Old: NpdmSystemCallId_GetFutureThreadInfo (5.0.0 - 5.1.0).
    NpdmSystemCallId_GetLastThreadInfo              = BIT(23),  ///< SVC 0x2F.

    ///< System calls for index 2.
    NpdmSystemCallId_GetResourceLimitLimitValue     = BIT(0),   ///< SVC 0x30.
    NpdmSystemCallId_GetResourceLimitCurrentValue   = BIT(1),   ///< SVC 0x31.
    NpdmSystemCallId_SetThreadActivity              = BIT(2),   ///< SVC 0x32.
    NpdmSystemCallId_GetThreadContext3              = BIT(3),   ///< SVC 0x33.
    NpdmSystemCallId_WaitForAddress                 = BIT(4),   ///< SVC 0x34 (4.0.0+).
    NpdmSystemCallId_SignalToAddress                = BIT(5),   ///< SVC 0x35 (4.0.0+).
    NpdmSystemCallId_SynchronizePreemptionState     = BIT(6),   ///< SVC 0x36 (8.0.0+).
    NpdmSystemCallId_GetResourceLimitPeakValue      = BIT(7),   ///< SVC 0x37 (11.0.0+).
    NpdmSystemCallId_Reserved2                      = BIT(8),   ///< SVC 0x38.
    NpdmSystemCallId_CreateIoPool                   = BIT(9),   ///< SVC 0x39 (13.0.0+).
    NpdmSystemCallId_CreateIoRegion                 = BIT(10),  ///< SVC 0x3A (13.0.0+).
    NpdmSystemCallId_Reserved3                      = BIT(11),  ///< SVC 0x3B.
    NpdmSystemCallId_KernelDebug                    = BIT(12),  ///< SVC 0x3C (4.0.0+). Old: NpdmSystemCallId_DumpInfo (1.0.0 - 3.0.2).
    NpdmSystemCallId_ChangeKernelTraceState         = BIT(13),  ///< SVC 0x3D (4.0.0+).
    NpdmSystemCallId_Reserved4                      = BIT(14),  ///< SVC 0x3E.
    NpdmSystemCallId_Reserved5                      = BIT(15),  ///< SVC 0x3F.
    NpdmSystemCallId_CreateSession                  = BIT(16),  ///< SVC 0x40.
    NpdmSystemCallId_AcceptSession                  = BIT(17),  ///< SVC 0x41.
    NpdmSystemCallId_ReplyAndReceiveLight           = BIT(18),  ///< SVC 0x42.
    NpdmSystemCallId_ReplyAndReceive                = BIT(19),  ///< SVC 0x43.
    NpdmSystemCallId_ReplyAndReceiveWithUserBuffer  = BIT(20),  ///< SVC 0x44.
    NpdmSystemCallId_CreateEvent                    = BIT(21),  ///< SVC 0x45.
    NpdmSystemCallId_MapIoRegion                    = BIT(22),  ///< SVC 0x46 (13.0.0+).
    NpdmSystemCallId_UnmapIoRegion                  = BIT(23),  ///< SVC 0x47 (13.0.0+).

    ///< System calls for index 3.
    NpdmSystemCallId_MapPhysicalMemoryUnsafe        = BIT(0),   ///< SVC 0x48 (5.0.0+).
    NpdmSystemCallId_UnmapPhysicalMemoryUnsafe      = BIT(1),   ///< SVC 0x49 (5.0.0+).
    NpdmSystemCallId_SetUnsafeLimit                 = BIT(2),   ///< SVC 0x4A (5.0.0+).
    NpdmSystemCallId_CreateCodeMemory               = BIT(3),   ///< SVC 0x4B (4.0.0+).
    NpdmSystemCallId_ControlCodeMemory              = BIT(4),   ///< SVC 0x4C (4.0.0+).
    NpdmSystemCallId_SleepSystem                    = BIT(5),   ///< SVC 0x4D.
    NpdmSystemCallId_ReadWriteRegister              = BIT(6),   ///< SVC 0x4E.
    NpdmSystemCallId_SetProcessActivity             = BIT(7),   ///< SVC 0x4F.
    NpdmSystemCallId_CreateSharedMemory             = BIT(8),   ///< SVC 0x50.
    NpdmSystemCallId_MapTransferMemory              = BIT(9),   ///< SVC 0x51.
    NpdmSystemCallId_UnmapTransferMemory            = BIT(10),  ///< SVC 0x52.
    NpdmSystemCallId_CreateInterruptEvent           = BIT(11),  ///< SVC 0x53.
    NpdmSystemCallId_QueryPhysicalAddress           = BIT(12),  ///< SVC 0x54.
    NpdmSystemCallId_QueryIoMapping                 = BIT(13),  ///< SVC 0x55.
    NpdmSystemCallId_CreateDeviceAddressSpace       = BIT(14),  ///< SVC 0x56.
    NpdmSystemCallId_AttachDeviceAddressSpace       = BIT(15),  ///< SVC 0x57.
    NpdmSystemCallId_DetachDeviceAddressSpace       = BIT(16),  ///< SVC 0x58.
    NpdmSystemCallId_MapDeviceAddressSpaceByForce   = BIT(17),  ///< SVC 0x59.
    NpdmSystemCallId_MapDeviceAddressSpaceAligned   = BIT(18),  ///< SVC 0x5A.
    NpdmSystemCallId_MapDeviceAddressSpace          = BIT(19),  ///< SVC 0x5B (1.0.0 - 12.1.0).
    NpdmSystemCallId_UnmapDeviceAddressSpace        = BIT(20),  ///< SVC 0x5C.
    NpdmSystemCallId_InvalidateProcessDataCache     = BIT(21),  ///< SVC 0x5D.
    NpdmSystemCallId_StoreProcessDataCache          = BIT(22),  ///< SVC 0x5E.
    NpdmSystemCallId_FlushProcessDataCache          = BIT(23),  ///< SVC 0x5F.

    ///< System calls for index 4.
    NpdmSystemCallId_DebugActiveProcess             = BIT(0),   ///< SVC 0x60.
    NpdmSystemCallId_BreakDebugProcess              = BIT(1),   ///< SVC 0x61.
    NpdmSystemCallId_TerminateDebugProcess          = BIT(2),   ///< SVC 0x62.
    NpdmSystemCallId_GetDebugEvent                  = BIT(3),   ///< SVC 0x63.
    NpdmSystemCallId_ContinueDebugEvent             = BIT(4),   ///< SVC 0x64.
    NpdmSystemCallId_GetProcessList                 = BIT(5),   ///< SVC 0x65.
    NpdmSystemCallId_GetThreadList                  = BIT(6),   ///< SVC 0x66.
    NpdmSystemCallId_GetDebugThreadContext          = BIT(7),   ///< SVC 0x67.
    NpdmSystemCallId_SetDebugThreadContext          = BIT(8),   ///< SVC 0x68.
    NpdmSystemCallId_QueryDebugProcessMemory        = BIT(9),   ///< SVC 0x69.
    NpdmSystemCallId_ReadDebugProcessMemory         = BIT(10),  ///< SVC 0x6A.
    NpdmSystemCallId_WriteDebugProcessMemory        = BIT(11),  ///< SVC 0x6B.
    NpdmSystemCallId_SetHardwareBreakPoint          = BIT(12),  ///< SVC 0x6C.
    NpdmSystemCallId_GetDebugThreadParam            = BIT(13),  ///< SVC 0x6D.
    NpdmSystemCallId_Reserved6                      = BIT(14),  ///< SVC 0x6E.
    NpdmSystemCallId_GetSystemInfo                  = BIT(15),  ///< SVC 0x6F (5.0.0+).
    NpdmSystemCallId_CreatePort                     = BIT(16),  ///< SVC 0x70.
    NpdmSystemCallId_ManageNamedPort                = BIT(17),  ///< SVC 0x71.
    NpdmSystemCallId_ConnectToPort                  = BIT(18),  ///< SVC 0x72.
    NpdmSystemCallId_SetProcessMemoryPermission     = BIT(19),  ///< SVC 0x73.
    NpdmSystemCallId_MapProcessMemory               = BIT(20),  ///< SVC 0x74.
    NpdmSystemCallId_UnmapProcessMemory             = BIT(21),  ///< SVC 0x75.
    NpdmSystemCallId_QueryProcessMemory             = BIT(22),  ///< SVC 0x76.
    NpdmSystemCallId_MapProcessCodeMemory           = BIT(23),  ///< SVC 0x77.

    ///< System calls for index 5.
    NpdmSystemCallId_UnmapProcessCodeMemory         = BIT(0),   ///< SVC 0x78.
    NpdmSystemCallId_CreateProcess                  = BIT(1),   ///< SVC 0x79.
    NpdmSystemCallId_StartProcess                   = BIT(2),   ///< SVC 0x7A.
    NpdmSystemCallId_TerminateProcess               = BIT(3),   ///< SVC 0x7B.
    NpdmSystemCallId_GetProcessInfo                 = BIT(4),   ///< SVC 0x7C.
    NpdmSystemCallId_CreateResourceLimit            = BIT(5),   ///< SVC 0x7D.
    NpdmSystemCallId_SetResourceLimitLimitValue     = BIT(6),   ///< SVC 0x7E.
    NpdmSystemCallId_CallSecureMonitor              = BIT(7),   ///< SVC 0x7F.
    NpdmSystemCallId_Reserved7                      = BIT(8),   ///< SVC 0x80.
    NpdmSystemCallId_Reserved8                      = BIT(9),   ///< SVC 0x81.
    NpdmSystemCallId_Reserved9                      = BIT(10),  ///< SVC 0x82.
    NpdmSystemCallId_Reserved10                     = BIT(11),  ///< SVC 0x83.
    NpdmSystemCallId_Reserved11                     = BIT(12),  ///< SVC 0x84.
    NpdmSystemCallId_Reserved12                     = BIT(13),  ///< SVC 0x85.
    NpdmSystemCallId_Reserved13                     = BIT(14),  ///< SVC 0x86.
    NpdmSystemCallId_Reserved14                     = BIT(15),  ///< SVC 0x87.
    NpdmSystemCallId_Reserved15                     = BIT(16),  ///< SVC 0x88.
    NpdmSystemCallId_Reserved16                     = BIT(17),  ///< SVC 0x89.
    NpdmSystemCallId_Reserved17                     = BIT(18),  ///< SVC 0x8A.
    NpdmSystemCallId_Reserved18                     = BIT(19),  ///< SVC 0x8B.
    NpdmSystemCallId_Reserved19                     = BIT(20),  ///< SVC 0x8C.
    NpdmSystemCallId_Reserved20                     = BIT(21),  ///< SVC 0x8D.
    NpdmSystemCallId_Reserved21                     = BIT(22),  ///< SVC 0x8E.
    NpdmSystemCallId_Reserved22                     = BIT(23),  ///< SVC 0x8F.

    ///< System calls for index 6.
    NpdmSystemCallId_MapInsecureMemory              = BIT(0),   ///< SVC 0x90 (15.0.0+).
    NpdmSystemCallId_UnmapInsecureMemory            = BIT(1),   ///< SVC 0x91 (15.0.0+).
    NpdmSystemCallId_Reserved23                     = BIT(2),   ///< SVC 0x92.
    NpdmSystemCallId_Reserved24                     = BIT(3),   ///< SVC 0x93.
    NpdmSystemCallId_Reserved25                     = BIT(4),   ///< SVC 0x94.
    NpdmSystemCallId_Reserved26                     = BIT(5),   ///< SVC 0x95.
    NpdmSystemCallId_Reserved27                     = BIT(6),   ///< SVC 0x96.
    NpdmSystemCallId_Reserved28                     = BIT(7),   ///< SVC 0x97.
    NpdmSystemCallId_Reserved29                     = BIT(8),   ///< SVC 0x98.
    NpdmSystemCallId_Reserved30                     = BIT(9),   ///< SVC 0x99.
    NpdmSystemCallId_Reserved31                     = BIT(10),  ///< SVC 0x9A.
    NpdmSystemCallId_Reserved32                     = BIT(11),  ///< SVC 0x9B.
    NpdmSystemCallId_Reserved33                     = BIT(12),  ///< SVC 0x9C.
    NpdmSystemCallId_Reserved34                     = BIT(13),  ///< SVC 0x9D.
    NpdmSystemCallId_Reserved35                     = BIT(14),  ///< SVC 0x9E.
    NpdmSystemCallId_Reserved36                     = BIT(15),  ///< SVC 0x9F.
    NpdmSystemCallId_Reserved37                     = BIT(16),  ///< SVC 0xA0.
    NpdmSystemCallId_Reserved38                     = BIT(17),  ///< SVC 0xA1.
    NpdmSystemCallId_Reserved39                     = BIT(18),  ///< SVC 0xA2.
    NpdmSystemCallId_Reserved40                     = BIT(19),  ///< SVC 0xA3.
    NpdmSystemCallId_Reserved41                     = BIT(20),  ///< SVC 0xA4.
    NpdmSystemCallId_Reserved42                     = BIT(21),  ///< SVC 0xA5.
    NpdmSystemCallId_Reserved43                     = BIT(22),  ///< SVC 0xA6.
    NpdmSystemCallId_Reserved44                     = BIT(23),  ///< SVC 0xA7.

    ///< System calls for index 7.
    NpdmSystemCallId_Reserved45                     = BIT(0),   ///< SVC 0xA8.
    NpdmSystemCallId_Reserved46                     = BIT(1),   ///< SVC 0xA9.
    NpdmSystemCallId_Reserved47                     = BIT(2),   ///< SVC 0xAA.
    NpdmSystemCallId_Reserved48                     = BIT(3),   ///< SVC 0xAB.
    NpdmSystemCallId_Reserved49                     = BIT(4),   ///< SVC 0xAC.
    NpdmSystemCallId_Reserved50                     = BIT(5),   ///< SVC 0xAD.
    NpdmSystemCallId_Reserved51                     = BIT(6),   ///< SVC 0xAE.
    NpdmSystemCallId_Reserved52                     = BIT(7),   ///< SVC 0xAF.
    NpdmSystemCallId_Reserved53                     = BIT(8),   ///< SVC 0xB0.
    NpdmSystemCallId_Reserved54                     = BIT(9),   ///< SVC 0xB1.
    NpdmSystemCallId_Reserved55                     = BIT(10),  ///< SVC 0xB2.
    NpdmSystemCallId_Reserved56                     = BIT(11),  ///< SVC 0xB3.
    NpdmSystemCallId_Reserved57                     = BIT(12),  ///< SVC 0xB4.
    NpdmSystemCallId_Reserved58                     = BIT(13),  ///< SVC 0xB5.
    NpdmSystemCallId_Reserved59                     = BIT(14),  ///< SVC 0xB6.
    NpdmSystemCallId_Reserved60                     = BIT(15),  ///< SVC 0xB7.
    NpdmSystemCallId_Reserved61                     = BIT(16),  ///< SVC 0xB8.
    NpdmSystemCallId_Reserved62                     = BIT(17),  ///< SVC 0xB9.
    NpdmSystemCallId_Reserved63                     = BIT(18),  ///< SVC 0xBA.
    NpdmSystemCallId_Reserved64                     = BIT(19),  ///< SVC 0xBB.
    NpdmSystemCallId_Reserved65                     = BIT(20),  ///< SVC 0xBC.
    NpdmSystemCallId_Reserved66                     = BIT(21),  ///< SVC 0xBD.
    NpdmSystemCallId_Reserved67                     = BIT(22),  ///< SVC 0xBE.
    NpdmSystemCallId_Reserved68                     = BIT(23),  ///< SVC 0xBF.

    NpdmSystemCallId_Count                          = 0xC0     ///< Total values supported by this enum.
} NpdmSystemCallId;

/// EnableSystemCalls entry for the KernelCapability descriptor.
typedef struct {
    u32 bitmask         : NpdmKernelCapabilityEntryBitmaskSize_EnableSystemCalls + 1;   ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_EnableSystemCalls.
    u32 system_call_ids : 24;                                                           ///< NpdmSystemCallId.
    u32 index           : 3;                                                            ///< System calls index.
} NpdmEnableSystemCalls;

NXDT_ASSERT(NpdmEnableSystemCalls, 0x4);

typedef enum {
    NpdmPermissionType_RW    = 0,
    NpdmPermissionType_RO    = 1,
    NpdmPermissionType_Count = 2    ///< Total values supported by this enum.
} NpdmPermissionType;

typedef struct {
    u32 bitmask         : NpdmKernelCapabilityEntryBitmaskSize_MemoryMap + 1;   ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_MemoryMap.
    u32 begin_address   : 24;                                                   ///< begin_address << 12.
    u32 permission_type : 1;                                                    ///< NpdmPermissionType.
} NpdmMemoryMapType1;

NXDT_ASSERT(NpdmMemoryMapType1, 0x4);

typedef enum {
    NpdmMappingType_Io     = 0,
    NpdmMappingType_Static = 1,
    NpdmMappingType_Count  = 2  ///< Total values supported by this enum.
} NpdmMappingType;

typedef struct {
    u32 bitmask      : NpdmKernelCapabilityEntryBitmaskSize_MemoryMap + 1;  ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_MemoryMap.
    u32 size         : 20;                                                  ///< size << 12.
    u32 reserved     : 4;
    u32 mapping_type : 1;                                                   ///< NpdmMappingType.
} NpdmMemoryMapType2;

NXDT_ASSERT(NpdmMemoryMapType2, 0x4);

/// MemoryMap entry for the KernelCapability descriptor.
/// These are always stored in pairs of MemoryMapType1 + MemoryMapType2 entries.
typedef struct {
    union {
        NpdmMemoryMapType1 type1;
        NpdmMemoryMapType2 type2;
    };
} NpdmMemoryMap;

NXDT_ASSERT(NpdmMemoryMap, 0x4);

/// IoMemoryMap entry for the KernelCapability descriptor.
typedef struct {
    u32 bitmask       : NpdmKernelCapabilityEntryBitmaskSize_IoMemoryMap + 1;   ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_IoMemoryMap.
    u32 begin_address : 24;                                                     ///< begin_address << 12.
} NpdmIoMemoryMap;

NXDT_ASSERT(NpdmIoMemoryMap, 0x4);

typedef enum {
    NpdmRegionType_NoMapping         = 0,
    NpdmRegionType_KernelTraceBuffer = 1,
    NpdmRegionType_OnMemoryBootImage = 2,
    NpdmRegionType_DTB               = 3,
    NpdmRegionType_Count             = 4    ///< Total values supported by this enum.
} NpdmRegionType;

/// MemoryRegionMap entry for the KernelCapability descriptor.
typedef struct {
    u32 bitmask           : NpdmKernelCapabilityEntryBitmaskSize_MemoryRegionMap + 1;   ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_MemoryRegionMap.
    u32 region_type_0     : 6;                                                          ///< NpdmRegionType.
    u32 permission_type_0 : 1;                                                          ///< NpdmPermissionType.
    u32 region_type_1     : 6;                                                          ///< NpdmRegionType.
    u32 permission_type_1 : 1;                                                          ///< NpdmPermissionType.
    u32 region_type_2     : 6;                                                          ///< NpdmRegionType.
    u32 permission_type_2 : 1;                                                          ///< NpdmPermissionType.
} NpdmMemoryRegionMap;

NXDT_ASSERT(NpdmMemoryRegionMap, 0x4);

/// EnableInterrupts entry for the KernelCapability descriptor.
typedef struct {
    u32 bitmask            : NpdmKernelCapabilityEntryBitmaskSize_EnableInterrupts + 1; ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_EnableInterrupts.
    u32 interrupt_number_0 : 10;                                                        ///< 0x3FF means empty.
    u32 interrupt_number_1 : 10;                                                        ///< 0x3FF means empty.
} NpdmEnableInterrupts;

NXDT_ASSERT(NpdmEnableInterrupts, 0x4);

typedef enum {
    NpdmProgramType_System      = 0,
    NpdmProgramType_Application = 1,
    NpdmProgramType_Applet      = 2,
    NpdmProgramType_Count       = 3     ///< Total values supported by this enum.
} NpdmProgramType;

/// MiscParams entry for the KernelCapability descriptor.
/// Defaults to 0 if this entry doesn't exist.
typedef struct {
    u32 bitmask      : NpdmKernelCapabilityEntryBitmaskSize_MiscParams + 1; ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_MiscParams.
    u32 program_type : 3;                                                   ///< NpdmProgramType.
    u32 reserved     : 15;
} NpdmMiscParams;

NXDT_ASSERT(NpdmMiscParams, 0x4);

/// KernelVersion entry for the KernelCapability descriptor.
/// This is derived from/equivalent to SDK version.
typedef struct {
    u32 bitmask       : NpdmKernelCapabilityEntryBitmaskSize_KernelVersion + 1; ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_KernelVersion.
    u32 minor_version : 4;                                                      ///< SDK minor version.
    u32 major_version : 13;                                                     ///< SDK major version + 4.
} NpdmKernelVersion;

NXDT_ASSERT(NpdmKernelVersion, 0x4);

/// HandleTableSize entry for the KernelCapability descriptor.
typedef struct {
    u32 bitmask           : NpdmKernelCapabilityEntryBitmaskSize_HandleTableSize + 1;   ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_HandleTableSize.
    u32 handle_table_size : 10;
    u32 reserved          : 6;
} NpdmHandleTableSize;

NXDT_ASSERT(NpdmHandleTableSize, 0x4);

/// MiscFlags entry for the KernelCapability descriptor.
typedef struct {
    u32 bitmask          : NpdmKernelCapabilityEntryBitmaskSize_MiscFlags + 1;  ///< Always set to NpdmKernelCapabilityEntryBitmaskPattern_MiscFlags.
    u32 enable_debug     : 1;
    u32 force_debug_prod : 1;
    u32 force_debug      : 1;
    u32 reserved         : 12;
} NpdmMiscFlags;

NXDT_ASSERT(NpdmMiscFlags, 0x4);

typedef struct {
    u8 *raw_data;                                               ///< Pointer to a dynamically allocated buffer that holds the raw NPDM.
    u64 raw_data_size;                                          ///< Raw NPDM size.
    NpdmMetaHeader *meta_header;                                ///< Pointer to the NpdmMetaHeader within 'raw_data'.
    NpdmAcidHeader *acid_header;                                ///< Pointer to the NpdmAcidHeader within 'raw_data'.
    NpdmFsAccessControlDescriptor *acid_fac_descriptor;         ///< Pointer to the NpdmFsAccessControlDescriptor within the NPDM ACID section.
    NpdmSrvAccessControlDescriptorEntry *acid_sac_descriptor;   ///< Pointer to the first NpdmSrvAccessControlDescriptorEntry within the NPDM ACID section, if available.
    NpdmKernelCapabilityDescriptorEntry *acid_kc_descriptor;    ///< Pointer to the first NpdmKernelCapabilityDescriptorEntry within the NPDM ACID section, if available.
    NpdmAciHeader *aci_header;                                  ///< Pointer to the NpdmAciHeader within 'raw_data'.
    NpdmFsAccessControlData *aci_fac_data;                      ///< Pointer to the NpdmFsAccessControlData within the NPDM ACI0 section.
    NpdmSrvAccessControlDescriptorEntry *aci_sac_descriptor;    ///< Pointer to the first NpdmSrvAccessControlDescriptorEntry within the NPDM ACI0 section, if available.
    NpdmKernelCapabilityDescriptorEntry *aci_kc_descriptor;     ///< Pointer to the first NpdmKernelCapabilityDescriptorEntry within the NPDM ACI0 section, if available.
} NpdmContext;

/// Initializes a NpdmContext using a previously initialized PartitionFileSystemContext (which must belong to the ExeFS from a Program NCA).
bool npdmInitializeContext(NpdmContext *out, PartitionFileSystemContext *pfs_ctx);

/// Helper inline functions.

NX_INLINE void npdmFreeContext(NpdmContext *npdm_ctx)
{
    if (!npdm_ctx) return;
    if (npdm_ctx->raw_data) free(npdm_ctx->raw_data);
    memset(npdm_ctx, 0, sizeof(NpdmContext));
}

NX_INLINE bool npdmIsValidContext(NpdmContext *npdm_ctx)
{
    return (npdm_ctx && npdm_ctx->raw_data && npdm_ctx->raw_data_size && npdm_ctx->meta_header && npdm_ctx->acid_header && npdm_ctx->acid_fac_descriptor && \
            ((npdm_ctx->acid_header->srv_access_control_size && npdm_ctx->acid_sac_descriptor) || (!npdm_ctx->acid_header->srv_access_control_size && !npdm_ctx->acid_sac_descriptor)) && \
            ((npdm_ctx->acid_header->kernel_capability_size && npdm_ctx->acid_kc_descriptor) || (!npdm_ctx->acid_header->kernel_capability_size && !npdm_ctx->acid_kc_descriptor)) && \
            npdm_ctx->aci_header && npdm_ctx->aci_fac_data && \
            ((npdm_ctx->aci_header->srv_access_control_size && npdm_ctx->aci_sac_descriptor) || (!npdm_ctx->aci_header->srv_access_control_size && !npdm_ctx->aci_sac_descriptor)) && \
            ((npdm_ctx->aci_header->kernel_capability_size && npdm_ctx->aci_kc_descriptor) || (!npdm_ctx->aci_header->kernel_capability_size && !npdm_ctx->aci_kc_descriptor)));
}

/// Returns a value that can be loooked up in the NpdmKernelCapabilityEntryBitmaskPattern enum.
NX_INLINE u32 npdmGetKernelCapabilityDescriptorEntryBitmaskPattern(NpdmKernelCapabilityDescriptorEntry *entry)
{
    return (entry ? (((entry->value + 1) & ~entry->value) - 1) : 0);
}

#ifdef __cplusplus
}
#endif

#endif /* __NPDM_H__ */
