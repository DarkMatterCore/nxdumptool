/*
 * npdm.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

typedef enum {
    NpdmProcessAddressSpace_AddressSpace32Bit           = 0,
    NpdmProcessAddressSpace_AddressSpace64BitOld        = 1,
    NpdmProcessAddressSpace_AddressSpace32BitNoReserved = 2,
    NpdmProcessAddressSpace_AddressSpace64Bit           = 3
} NpdmProcessAddressSpace;

typedef struct {
    u8 is_64bit_instruction               : 1;
    u8 process_address_space              : 3;  ///< NpdmProcessAddressSpace.
    u8 optimize_memory_allocation         : 1;
    u8 disable_device_address_space_merge : 1;
    u8 reserved                           : 2;
} NpdmMetaFlags;

NXDT_ASSERT(NpdmMetaFlags, 0x1);

/// This is the start of every NPDM file.
/// This is followed by ACID and ACI0 sections, both with variable offsets and sizes.
typedef struct {
    u32 magic;                          ///< "NPDM".
    u8 acid_signature_key_generation;
    u8 reserved_1[0x7];
    NpdmMetaFlags flags;
    u8 reserved_2;
    u8 main_thread_priority;            ///< Must not exceed NPDM_MAIN_THREAD_MAX_PRIORITY.
    u8 main_thread_core_number;         ///< Must not exceed NPDM_MAIN_THREAD_MAX_CORE_NUMBER.
    u8 reserved_3[0x4];
    u32 system_resource_size;           ///< Must not exceed NPDM_SYSTEM_RESOURCE_MAX_SIZE.
    VersionType1 version;
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
    NpdmMemoryRegion_SystemSecure    = 2,
    NpdmMemoryRegion_SystemNonSecure = 3,
    
    /// Old.
    NpdmMemoryRegion_NonSecure       = NpdmMemoryRegion_Application,
    NpdmMemoryRegion_Secure          = NpdmMemoryRegion_Applet
} NpdmMemoryRegion;

typedef struct {
    u32 production           : 1;
    u32 unqualified_approval : 1;
    u32 memory_region        : 2;   ///< NpdmMemoryRegion.
    u32 reserved             : 28;
} NpdmAcidFlags;

NXDT_ASSERT(NpdmAcidFlags, 0x4);

/// This is the start of an ACID section.
/// This is followed by FsAccessControl, SrvAccessControl and KernelCapability descriptors, each one aligned to a 0x10 byte boundary using zero padding (if needed).
typedef struct {
    u8 signature[0x100];            ///< RSA-2048-PSS with SHA-256 signature over the rest of the ACID section, using the value from the 'size' member.
    u8 public_key[0x100];           ///< RSA public key used to verify the ACID signature from the Program NCA header.
    u32 magic;                      ///< "ACID".
    u32 size;                       ///< Must be equal to ACID section size from the META header minus 0x100 (ACID signature size).
    u8 reserved_1[0x4];
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
    NpdmFsAccessControlFlags_ApplicationInfo             = BIT_LONG(0),
    NpdmFsAccessControlFlags_BootModeControl             = BIT_LONG(1),
    NpdmFsAccessControlFlags_Calibration                 = BIT_LONG(2),
    NpdmFsAccessControlFlags_SystemSaveData              = BIT_LONG(3),
    NpdmFsAccessControlFlags_GameCard                    = BIT_LONG(4),
    NpdmFsAccessControlFlags_SaveDataBackUp              = BIT_LONG(5),
    NpdmFsAccessControlFlags_SaveDataManagement          = BIT_LONG(6),
    NpdmFsAccessControlFlags_BisAllRaw                   = BIT_LONG(7),
    NpdmFsAccessControlFlags_GameCardRaw                 = BIT_LONG(8),
    NpdmFsAccessControlFlags_GameCardPrivate             = BIT_LONG(9),
    NpdmFsAccessControlFlags_SetTime                     = BIT_LONG(10),
    NpdmFsAccessControlFlags_ContentManager              = BIT_LONG(11),
    NpdmFsAccessControlFlags_ImageManager                = BIT_LONG(12),
    NpdmFsAccessControlFlags_CreateSaveData              = BIT_LONG(13),
    NpdmFsAccessControlFlags_SystemSaveDataManagement    = BIT_LONG(14),
    NpdmFsAccessControlFlags_BisFileSystem               = BIT_LONG(15),
    NpdmFsAccessControlFlags_SystemUpdate                = BIT_LONG(16),
    NpdmFsAccessControlFlags_SaveDataMeta                = BIT_LONG(17),
    NpdmFsAccessControlFlags_DeviceSaveData              = BIT_LONG(18),
    NpdmFsAccessControlFlags_SettingsControl             = BIT_LONG(19),
    NpdmFsAccessControlFlags_SystemData                  = BIT_LONG(20),
    NpdmFsAccessControlFlags_SdCard                      = BIT_LONG(21),
    NpdmFsAccessControlFlags_Host                        = BIT_LONG(22),
    NpdmFsAccessControlFlags_FillBis                     = BIT_LONG(23),
    NpdmFsAccessControlFlags_CorruptSaveData             = BIT_LONG(24),
    NpdmFsAccessControlFlags_SaveDataForDebug            = BIT_LONG(25),
    NpdmFsAccessControlFlags_FormatSdCard                = BIT_LONG(26),
    NpdmFsAccessControlFlags_GetRightsId                 = BIT_LONG(27),
    NpdmFsAccessControlFlags_RegisterExternalKey         = BIT_LONG(28),
    NpdmFsAccessControlFlags_RegisterUpdatePartition     = BIT_LONG(29),
    NpdmFsAccessControlFlags_SaveDataTransfer            = BIT_LONG(30),
    NpdmFsAccessControlFlags_DeviceDetection             = BIT_LONG(31),
    NpdmFsAccessControlFlags_AccessFailureResolution     = BIT_LONG(32),
    NpdmFsAccessControlFlags_SaveDataTransferVersion2    = BIT_LONG(33),
    NpdmFsAccessControlFlags_RegisterProgramIndexMapInfo = BIT_LONG(34),
    NpdmFsAccessControlFlags_CreateOwnSaveData           = BIT_LONG(35),
    NpdmFsAccessControlFlags_MoveCacheStorage            = BIT_LONG(36),
    NpdmFsAccessControlFlags_Debug                       = BIT_LONG(62),
    NpdmFsAccessControlFlags_FullPermission              = BIT_LONG(63)
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
    u64 flags;
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
    u64 flags;
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
    u64 content_owner_id[];     ///< 'content_owner_id_count' content owned IDs.
} NpdmFsAccessControlDataContentOwnerBlock;
#pragma pack(pop)

NXDT_ASSERT(NpdmFsAccessControlDataContentOwnerBlock, 0x4);

typedef enum {
    NpdmAccessibility_Read  = BIT(0),
    NpdmAccessibility_Write = BIT(1)
} NpdmAccessibility;

/// Placed after NpdmFsAccessControlData / NpdmFsAccessControlDataContentOwnerBlock if the 'content_owner_info_size' member from NpdmFsAccessControlData is greater than zero.
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

typedef enum {
    NpdmKernelCapabilityEntryNumber_ThreadInfo        = 3,
    NpdmKernelCapabilityEntryNumber_EnableSystemCalls = 4,
    NpdmKernelCapabilityEntryNumber_MemoryMap         = 6,
    NpdmKernelCapabilityEntryNumber_IoMemoryMap       = 7,
    NpdmKernelCapabilityEntryNumber_MemoryRegionMap   = 10,
    NpdmKernelCapabilityEntryNumber_EnableInterrupts  = 11,
    NpdmKernelCapabilityEntryNumber_MiscParams        = 13,
    NpdmKernelCapabilityEntryNumber_KernelVersion     = 14,
    NpdmKernelCapabilityEntryNumber_HandleTableSize   = 15,
    NpdmKernelCapabilityEntryNumber_MiscFlags         = 16
} NpdmKernelCapabilityEntryNumber;

typedef enum {
    NpdmKernelCapabilityEntryValue_ThreadInfo        = BIT(NpdmKernelCapabilityEntryNumber_ThreadInfo)        - 1,
    NpdmKernelCapabilityEntryValue_EnableSystemCalls = BIT(NpdmKernelCapabilityEntryNumber_EnableSystemCalls) - 1,
    NpdmKernelCapabilityEntryValue_MemoryMap         = BIT(NpdmKernelCapabilityEntryNumber_MemoryMap)         - 1,
    NpdmKernelCapabilityEntryValue_IoMemoryMap       = BIT(NpdmKernelCapabilityEntryNumber_IoMemoryMap)       - 1,
    NpdmKernelCapabilityEntryValue_MemoryRegionMap   = BIT(NpdmKernelCapabilityEntryNumber_MemoryRegionMap)   - 1,
    NpdmKernelCapabilityEntryValue_EnableInterrupts  = BIT(NpdmKernelCapabilityEntryNumber_EnableInterrupts)  - 1,
    NpdmKernelCapabilityEntryValue_MiscParams        = BIT(NpdmKernelCapabilityEntryNumber_MiscParams)        - 1,
    NpdmKernelCapabilityEntryValue_KernelVersion     = BIT(NpdmKernelCapabilityEntryNumber_KernelVersion)     - 1,
    NpdmKernelCapabilityEntryValue_HandleTableSize   = BIT(NpdmKernelCapabilityEntryNumber_HandleTableSize)   - 1,
    NpdmKernelCapabilityEntryValue_MiscFlags         = BIT(NpdmKernelCapabilityEntryNumber_MiscFlags)         - 1
} NpdmKernelCapabilityEntryValue;

/// ThreadInfo entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_value      : NpdmKernelCapabilityEntryNumber_ThreadInfo;  ///< Always set to NpdmKernelCapabilityEntryValue_ThreadInfo.
    u32 padding          : 1;                                           ///< Always set to zero.
    u32 lowest_priority  : 6;
    u32 highest_priority : 6;
    u32 min_core_number  : 8;
    u32 max_core_number  : 8;
} NpdmThreadInfo;

NXDT_ASSERT(NpdmThreadInfo, 0x4);

/// System call table.
typedef enum {
    ///< System calls for index 0.
    NpdmSystemCallId_Reserved1                      = BIT(0),
    NpdmSystemCallId_SetHeapSize                    = BIT(1),
    NpdmSystemCallId_SetMemoryPermission            = BIT(2),
    NpdmSystemCallId_SetMemoryAttribute             = BIT(3),
    NpdmSystemCallId_MapMemory                      = BIT(4),
    NpdmSystemCallId_UnmapMemory                    = BIT(5),
    NpdmSystemCallId_QueryMemory                    = BIT(6),
    NpdmSystemCallId_ExitProcess                    = BIT(7),
    NpdmSystemCallId_CreateThread                   = BIT(8),
    NpdmSystemCallId_StartThread                    = BIT(9),
    NpdmSystemCallId_ExitThread                     = BIT(10),
    NpdmSystemCallId_SleepThread                    = BIT(11),
    NpdmSystemCallId_GetThreadPriority              = BIT(12),
    NpdmSystemCallId_SetThreadPriority              = BIT(13),
    NpdmSystemCallId_GetThreadCoreMask              = BIT(14),
    NpdmSystemCallId_SetThreadCoreMask              = BIT(15),
    NpdmSystemCallId_GetCurrentProcessorNumber      = BIT(16),
    NpdmSystemCallId_SignalEvent                    = BIT(17),
    NpdmSystemCallId_ClearEvent                     = BIT(18),
    NpdmSystemCallId_MapSharedMemory                = BIT(19),
    NpdmSystemCallId_UnmapSharedMemory              = BIT(20),
    NpdmSystemCallId_CreateTransferMemory           = BIT(21),
    NpdmSystemCallId_CloseHandle                    = BIT(22),
    NpdmSystemCallId_ResetSignal                    = BIT(23),
    
    ///< System calls for index 1.
    NpdmSystemCallId_WaitSynchronization            = BIT(0),
    NpdmSystemCallId_CancelSynchronization          = BIT(1),
    NpdmSystemCallId_ArbitrateLock                  = BIT(2),
    NpdmSystemCallId_ArbitrateUnlock                = BIT(3),
    NpdmSystemCallId_WaitProcessWideKeyAtomic       = BIT(4),
    NpdmSystemCallId_SignalProcessWideKey           = BIT(5),
    NpdmSystemCallId_GetSystemTick                  = BIT(6),
    NpdmSystemCallId_ConnectToNamedPort             = BIT(7),
    NpdmSystemCallId_SendSyncRequestLight           = BIT(8),
    NpdmSystemCallId_SendSyncRequest                = BIT(9),
    NpdmSystemCallId_SendSyncRequestWithUserBuffer  = BIT(10),
    NpdmSystemCallId_SendAsyncRequestWithUserBuffer = BIT(11),
    NpdmSystemCallId_GetProcessId                   = BIT(12),
    NpdmSystemCallId_GetThreadId                    = BIT(13),
    NpdmSystemCallId_Break                          = BIT(14),
    NpdmSystemCallId_OutputDebugString              = BIT(15),
    NpdmSystemCallId_ReturnFromException            = BIT(16),
    NpdmSystemCallId_GetInfo                        = BIT(17),
    NpdmSystemCallId_FlushEntireDataCache           = BIT(18),
    NpdmSystemCallId_FlushDataCache                 = BIT(19),
    NpdmSystemCallId_MapPhysicalMemory              = BIT(20),
    NpdmSystemCallId_UnmapPhysicalMemory            = BIT(21),
    NpdmSystemCallId_GetDebugFutureThreadInfo       = BIT(22),  ///< Old: SystemCallId_GetFutureThreadInfo.
    NpdmSystemCallId_GetLastThreadInfo              = BIT(23),
    
    ///< System calls for index 2.
    NpdmSystemCallId_GetResourceLimitLimitValue     = BIT(0),
    NpdmSystemCallId_GetResourceLimitCurrentValue   = BIT(1),
    NpdmSystemCallId_SetThreadActivity              = BIT(2),
    NpdmSystemCallId_GetThreadContext3              = BIT(3),
    NpdmSystemCallId_WaitForAddress                 = BIT(4),
    NpdmSystemCallId_SignalToAddress                = BIT(5),
    NpdmSystemCallId_SynchronizePreemptionState     = BIT(6),
    NpdmSystemCallId_Reserved2                      = BIT(7),
    NpdmSystemCallId_Reserved3                      = BIT(8),
    NpdmSystemCallId_Reserved4                      = BIT(9),
    NpdmSystemCallId_Reserved5                      = BIT(10),
    NpdmSystemCallId_Reserved6                      = BIT(11),
    NpdmSystemCallId_KernelDebug                    = BIT(12),
    NpdmSystemCallId_ChangeKernelTraceState         = BIT(13),
    NpdmSystemCallId_Reserved7                      = BIT(14),
    NpdmSystemCallId_Reserved8                      = BIT(15),
    NpdmSystemCallId_CreateSession                  = BIT(16),
    NpdmSystemCallId_AcceptSession                  = BIT(17),
    NpdmSystemCallId_ReplyAndReceiveLight           = BIT(18),
    NpdmSystemCallId_ReplyAndReceive                = BIT(19),
    NpdmSystemCallId_ReplyAndReceiveWithUserBuffer  = BIT(20),
    NpdmSystemCallId_CreateEvent                    = BIT(21),
    NpdmSystemCallId_Reserved9                      = BIT(22),
    NpdmSystemCallId_Reserved10                     = BIT(23),
    
    ///< System calls for index 3.
    NpdmSystemCallId_MapPhysicalMemoryUnsafe        = BIT(0),
    NpdmSystemCallId_UnmapPhysicalMemoryUnsafe      = BIT(1),
    NpdmSystemCallId_SetUnsafeLimit                 = BIT(2),
    NpdmSystemCallId_CreateCodeMemory               = BIT(3),
    NpdmSystemCallId_ControlCodeMemory              = BIT(4),
    NpdmSystemCallId_SleepSystem                    = BIT(5),
    NpdmSystemCallId_ReadWriteRegister              = BIT(6),
    NpdmSystemCallId_SetProcessActivity             = BIT(7),
    NpdmSystemCallId_CreateSharedMemory             = BIT(8),
    NpdmSystemCallId_MapTransferMemory              = BIT(9),
    NpdmSystemCallId_UnmapTransferMemory            = BIT(10),
    NpdmSystemCallId_CreateInterruptEvent           = BIT(11),
    NpdmSystemCallId_QueryPhysicalAddress           = BIT(12),
    NpdmSystemCallId_QueryIoMapping                 = BIT(13),
    NpdmSystemCallId_CreateDeviceAddressSpace       = BIT(14),
    NpdmSystemCallId_AttachDeviceAddressSpace       = BIT(15),
    NpdmSystemCallId_DetachDeviceAddressSpace       = BIT(16),
    NpdmSystemCallId_MapDeviceAddressSpaceByForce   = BIT(17),
    NpdmSystemCallId_MapDeviceAddressSpaceAligned   = BIT(18),
    NpdmSystemCallId_MapDeviceAddressSpace          = BIT(19),
    NpdmSystemCallId_UnmapDeviceAddressSpace        = BIT(20),
    NpdmSystemCallId_InvalidateProcessDataCache     = BIT(21),
    NpdmSystemCallId_StoreProcessDataCache          = BIT(22),
    NpdmSystemCallId_FlushProcessDataCache          = BIT(23),
    
    ///< System calls for index 4.
    NpdmSystemCallId_DebugActiveProcess             = BIT(0),
    NpdmSystemCallId_BreakDebugProcess              = BIT(1),
    NpdmSystemCallId_TerminateDebugProcess          = BIT(2),
    NpdmSystemCallId_GetDebugEvent                  = BIT(3),
    NpdmSystemCallId_ContinueDebugEvent             = BIT(4),
    NpdmSystemCallId_GetProcessList                 = BIT(5),
    NpdmSystemCallId_GetThreadList                  = BIT(6),
    NpdmSystemCallId_GetDebugThreadContext          = BIT(7),
    NpdmSystemCallId_SetDebugThreadContext          = BIT(8),
    NpdmSystemCallId_QueryDebugProcessMemory        = BIT(9),
    NpdmSystemCallId_ReadDebugProcessMemory         = BIT(10),
    NpdmSystemCallId_WriteDebugProcessMemory        = BIT(11),
    NpdmSystemCallId_SetHardwareBreakPoint          = BIT(12),
    NpdmSystemCallId_GetDebugThreadParam            = BIT(13),
    NpdmSystemCallId_Reserved11                     = BIT(14),
    NpdmSystemCallId_GetSystemInfo                  = BIT(15),
    NpdmSystemCallId_CreatePort                     = BIT(16),
    NpdmSystemCallId_ManageNamedPort                = BIT(17),
    NpdmSystemCallId_ConnectToPort                  = BIT(18),
    NpdmSystemCallId_SetProcessMemoryPermission     = BIT(19),
    NpdmSystemCallId_MapProcessMemory               = BIT(20),
    NpdmSystemCallId_UnmapProcessMemory             = BIT(21),
    NpdmSystemCallId_QueryProcessMemory             = BIT(22),
    NpdmSystemCallId_MapProcessCodeMemory           = BIT(23),
    
    ///< System calls for index 5.
    NpdmSystemCallId_UnmapProcessCodeMemory         = BIT(0),
    NpdmSystemCallId_CreateProcess                  = BIT(1),
    NpdmSystemCallId_StartProcess                   = BIT(2),
    NpdmSystemCallId_TerminateProcess               = BIT(3),
    NpdmSystemCallId_GetProcessInfo                 = BIT(4),
    NpdmSystemCallId_CreateResourceLimit            = BIT(5),
    NpdmSystemCallId_SetResourceLimitLimitValue     = BIT(6),
    NpdmSystemCallId_CallSecureMonitor              = BIT(7),
    NpdmSystemCallId_Count                          = 0x80     ///< Total values supported by this enum.
} NpdmSystemCallId;

/// EnableSystemCalls entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_value     : NpdmKernelCapabilityEntryNumber_EnableSystemCalls;    ///< Always set to NpdmKernelCapabilityEntryValue_EnableSystemCalls.
    u32 padding         : 1;                                                    ///< Always set to zero.
    u32 system_call_ids : 24;                                                   ///< NpdmSystemCallId.
    u32 index           : 3;                                                    ///< System calls index.
} NpdmEnableSystemCalls;

NXDT_ASSERT(NpdmEnableSystemCalls, 0x4);

typedef enum {
    NpdmPermissionType_RW = 0,
    NpdmPermissionType_RO = 1
} NpdmPermissionType;

typedef enum {
    NpdmMappingType_Io     = 0,
    NpdmMappingType_Static = 1
} NpdmMappingType;

typedef struct {
    u32 entry_value     : NpdmKernelCapabilityEntryNumber_MemoryMap;    ///< Always set to NpdmKernelCapabilityEntryValue_MemoryMap.
    u32 padding         : 1;                                            ///< Always set to zero.
    u32 begin_address   : 24;                                           ///< begin_address << 12.
    u32 permission_type : 1;                                            ///< NpdmPermissionType.
} NpdmMemoryMapType1;

NXDT_ASSERT(NpdmMemoryMapType1, 0x4);

typedef struct {
    u32 entry_value  : NpdmKernelCapabilityEntryNumber_MemoryMap;   ///< Always set to NpdmKernelCapabilityEntryValue_MemoryMap.
    u32 padding      : 1;                                           ///< Always set to zero.
    u32 size         : 20;                                          ///< size << 12.
    u32 reserved     : 4;
    u32 mapping_type : 1;                                           ///< NpdmMappingType.
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
    u32 entry_value   : NpdmKernelCapabilityEntryNumber_IoMemoryMap;    ///< Always set to NpdmKernelCapabilityEntryValue_IoMemoryMap.
    u32 padding       : 1;                                              ///< Always set to zero.
    u32 begin_address : 24;                                             ///< begin_address << 12.
} NpdmIoMemoryMap;

NXDT_ASSERT(NpdmIoMemoryMap, 0x4);

typedef enum {
    NpdmRegionType_NoMapping         = 0,
    NpdmRegionType_KernelTraceBuffer = 1,
    NpdmRegionType_OnMemoryBootImage = 2,
    NpdmRegionType_DTB               = 3
} NpdmRegionType;

/// MemoryRegionMap entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_value       : NpdmKernelCapabilityEntryNumber_MemoryRegionMap;    ///< Always set to NpdmKernelCapabilityEntryValue_MemoryRegionMap.
    u32 padding           : 1;                                                  ///< Always set to zero.
    u32 region_type_1     : 6;                                                  ///< NpdmRegionType.
    u32 permission_type_1 : 1;                                                  ///< NpdmPermissionType.
    u32 region_type_2     : 6;                                                  ///< NpdmRegionType.
    u32 permission_type_2 : 1;                                                  ///< NpdmPermissionType.
    u32 region_type_3     : 6;                                                  ///< NpdmRegionType.
    u32 permission_type_3 : 1;                                                  ///< NpdmPermissionType.
} NpdmMemoryRegionMap;

NXDT_ASSERT(NpdmMemoryRegionMap, 0x4);

/// EnableInterrupts entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_value        : NpdmKernelCapabilityEntryNumber_EnableInterrupts;  ///< Always set to NpdmKernelCapabilityEntryValue_EnableInterrupts.
    u32 padding            : 1;                                                 ///< Always set to zero.
    u32 interrupt_number_1 : 10;                                                ///< 0x3FF means empty.
    u32 interrupt_number_2 : 10;                                                ///< 0x3FF means empty.
} NpdmEnableInterrupts;

NXDT_ASSERT(NpdmEnableInterrupts, 0x4);

typedef enum {
    NpdmProgramType_System      = 0,
    NpdmProgramType_Application = 1,
    NpdmProgramType_Applet      = 2
} NpdmProgramType;

/// MiscParams entry for the KernelCapability descriptor.
/// Defaults to 0 if this entry doesn't exist.
typedef struct {
    u32 entry_value  : NpdmKernelCapabilityEntryNumber_MiscParams;  ///< Always set to NpdmKernelCapabilityEntryValue_MiscParams.
    u32 padding      : 1;                                           ///< Always set to zero.
    u32 program_type : 3;                                           ///< NpdmProgramType.
    u32 reserved     : 15;
} NpdmMiscParams;

NXDT_ASSERT(NpdmMiscParams, 0x4);

/// KernelVersion entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_value   : NpdmKernelCapabilityEntryNumber_KernelVersion;  ///< Always set to NpdmKernelCapabilityEntryValue_KernelVersion.
    u32 padding       : 1;                                              ///< Always set to zero.
    u32 minor_version : 4;
    u32 major_version : 13;
} NpdmKernelVersion;

NXDT_ASSERT(NpdmKernelVersion, 0x4);

/// HandleTableSize entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_value       : NpdmKernelCapabilityEntryNumber_HandleTableSize;    ///< Always set to NpdmKernelCapabilityEntryValue_HandleTableSize.
    u32 padding           : 1;                                                  ///< Always set to zero.
    u32 handle_table_size : 10;
    u32 reserved          : 6;
} NpdmHandleTableSize;

NXDT_ASSERT(NpdmHandleTableSize, 0x4);

/// MiscFlags entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_value  : NpdmKernelCapabilityEntryNumber_MiscFlags;   ///< Always set to NpdmKernelCapabilityEntryValue_MiscFlags.
    u32 padding      : 1;                                           ///< Always set to zero.
    u32 enable_debug : 1;
    u32 force_debug  : 1;
    u32 reserved     : 13;
} NpdmMiscFlags;

NXDT_ASSERT(NpdmMiscFlags, 0x4);

/// KernelCapability descriptor. Part of the ACID and ACI0 section bodies.
/// This descriptor is composed of a variable number of u32 entries. Thus, the entry count can be calculated by dividing the KernelCapability descriptor size by 4.
/// The entry type is identified by a pattern of "01...11" (zero followed by ones) in the low u16, counting from the LSB. The variable number of ones must never exceed 16 (entirety of the low u16).
typedef struct {
    u32 value;
} NpdmKernelCapabilityDescriptorEntry;

NXDT_ASSERT(NpdmKernelCapabilityDescriptorEntry, 0x4);

typedef struct {
    NcaContext *nca_ctx;                                        ///< Pointer to the NCA context for the Program NCA from which NPDM data is retrieved.
    PartitionFileSystemContext *pfs_ctx;                        ///< PartitionFileSystemContext for the Program NCA FS section #0, which is where the NPDM is stored.
    PartitionFileSystemEntry *pfs_entry;                        ///< PartitionFileSystemEntry for the NPDM in the Program NCA FS section #0. Used to generate a NcaHierarchicalSha256Patch if needed.
    NcaHierarchicalSha256Patch nca_patch;                       ///< NcaHierarchicalSha256Patch generated if NPDM modifications are needed. Used to seamlessly replace Program NCA data while writing it.
                                                                ///< Bear in mind that generating a patch modifies the NCA context.
    u8 *raw_data;                                               ///< Pointer to a dynamically allocated buffer that holds the raw NPDM.
    u64 raw_data_size;                                          ///< Raw NPDM size. Kept here for convenience - this is part of 'pfs_entry'.
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

/// Changes the ACID public key from the NPDM in the input NpdmContext, updates the ACID signature from the NCA header in the underlying NCA context and generates a Partition FS entry patch.
bool npdmGenerateNcaPatch(NpdmContext *npdm_ctx);

/// Writes data from the Partition FS entry patch in the input NpdmContext to the provided buffer.
void npdmWriteNcaPatch(NpdmContext *npdm_ctx, void *buf, u64 buf_size, u64 buf_offset);

/// Helper inline functions.

NX_INLINE void npdmFreeContext(NpdmContext *npdm_ctx)
{
    if (!npdm_ctx) return;
    pfsFreeEntryPatch(&(npdm_ctx->nca_patch));
    if (npdm_ctx->raw_data) free(npdm_ctx->raw_data);
    memset(npdm_ctx, 0, sizeof(NpdmContext));
}

NX_INLINE bool npdmIsValidContext(NpdmContext *npdm_ctx)
{
    return (npdm_ctx && npdm_ctx->nca_ctx && npdm_ctx->pfs_ctx && npdm_ctx->pfs_entry && npdm_ctx->raw_data && npdm_ctx->raw_data_size && npdm_ctx->meta_header && npdm_ctx->acid_header && \
            npdm_ctx->acid_fac_descriptor && \
            ((npdm_ctx->acid_header->srv_access_control_size && npdm_ctx->acid_sac_descriptor) || (!npdm_ctx->acid_header->srv_access_control_size && !npdm_ctx->acid_sac_descriptor)) && \
            ((npdm_ctx->acid_header->kernel_capability_size && npdm_ctx->acid_kc_descriptor) || (!npdm_ctx->acid_header->kernel_capability_size && !npdm_ctx->acid_kc_descriptor)) && \
            npdm_ctx->aci_header && npdm_ctx->aci_fac_data && \
            ((npdm_ctx->aci_header->srv_access_control_size && npdm_ctx->aci_sac_descriptor) || (!npdm_ctx->aci_header->srv_access_control_size && !npdm_ctx->aci_sac_descriptor)) && \
            ((npdm_ctx->aci_header->kernel_capability_size && npdm_ctx->aci_kc_descriptor) || (!npdm_ctx->aci_header->kernel_capability_size && !npdm_ctx->aci_kc_descriptor)));
}

NX_INLINE u32 npdmGetKernelCapabilityDescriptorEntryValue(NpdmKernelCapabilityDescriptorEntry *entry)
{
    return (entry ? (((entry->value + 1) & ~entry->value) - 1) : 0);
}

#ifdef __cplusplus
}
#endif

#endif /* __NPDM_H__ */
