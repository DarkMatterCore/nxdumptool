/*
 * npdm.h
 *
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

#ifndef __NPDM_H__
#define __NPDM_H__

#define NPDM_META_MAGIC 0x4D455441  /* "META". */
#define NPDM_ACID_MAGIC 0x41434944  /* "ACID". */
#define NPDM_ACI0_MAGIC 0x41434930  /* "ACI0". */

typedef enum {
    NpdmProcessAddressSpace_AddressSpace32Bit           = 0,
    NpdmProcessAddressSpace_AddressSpace64BitOld        = 1,
    NpdmProcessAddressSpace_AddressSpace32BitNoReserved = 2,
    NpdmProcessAddressSpace_AddressSpace64Bit           = 3
} NpdmProcessAddressSpace;

typedef struct {
    u8 is_64bit_instruction       : 1;
    u8 process_address_space      : 3;  ///< NpdmProcessAddressSpace.
    u8 optimize_memory_allocation : 1;
    u8 reserved                   : 3;
} NpdmMetaFlags;

typedef struct {
    u32 offset;
    u32 size;
} NpdmSectionHeader;

/// This is the start of every NPDM file.
/// This is followed by ACID and ACI0 sections, both with variable offsets and sizes.
typedef struct {
    u32 magic;                              ///< "NPDM".
    u8 acid_signature_key_generation;
    u8 reserved_1[0x7];
    NpdmMetaFlags flags;
    u8 reserved_2;
    u8 main_thread_priority;                ///< Ranges from 0x00 to 0x3F.
    u8 main_thread_core_number;             ///< CPU ID. Ranges from 0 to 3.
    u8 reserved_3[0x4];
    u32 system_resource_size;               ///< Must not exceed 0x1FE00000.
    VersionType1 version;
    u32 main_thread_stack_size;             ///< Must be aligned to 0x1000.
    char name[0x10];                        ///< Usually set to "Application".
    char product_code[0x10];                ///< Usually zeroed out.
    u8 reserved_4[0x30];
    NpdmSectionHeader aci_section_header;   ///< Offset value relative to the start of this header.
    NpdmSectionHeader acid_section_header;  ///< Offset value relative to the start of this header.
} NpdmMetaHeader;

typedef enum {
    NpdmMemoryRegion_Application     = 0,
    NpdmMemoryRegion_Applet          = 1,
    NpdmMemoryRegion_SecureSystem    = 2,
    NpdmMemoryRegion_NonSecureSystem = 3
} NpdmMemoryRegion;

typedef struct {
    u32 production           : 1;
    u32 unqualified_approval : 1;
    u32 memory_region        : 2;   ///< NpdmMemoryRegion.
    u32 reserved             : 28;
} NpdmAcidFlags;

/// This is the start of an ACID section.
/// This is followed by FsAccessControl (ACID), SrvAccessControl and KernelCapability descriptors, each one aligned to a 0x10 byte boundary using zero padding (if needed).
typedef struct {
    u8 signature[0x100];                                    ///< RSA-2048-PSS with SHA-256 signature over the rest of the ACID section, using the value from the 'size' member.
    u8 public_key[0x100];                                   ///< RSA public key used to verify the ACID signature from the Program NCA header.
    u32 magic;                                              ///< "ACID".
    u32 size;                                               ///< Must be equal to ACID section size from the META header minus 0x100 (ACID signature size).
    u8 reserved_1[0x4];
    NpdmAcidFlags flags;
    u64 program_id_min;
    u64 program_id_max;
    NpdmSectionHeader fs_access_control_section_header;     ///< Offset value relative to the start of this header.
    NpdmSectionHeader srv_access_control_section_header;    ///< Offset value relative to the start of this header.
    NpdmSectionHeader kernel_capability_section_header;     ///< Offset value relative to the start of this header.
    u8 reserved_2[0x8];
} NpdmAcidHeader;

/// This is the start of an ACI0 section.
/// This is followed by FsAccessControl (ACI0), SrvAccessControl and KernelCapability descriptors, each one aligned to a 0x10 byte boundary using zero padding (if needed).
typedef struct {
    u32 magic;
    u8 reserved_1[0xC];
    u64 program_id;
    u8 reserved_2[0x8];
    NpdmSectionHeader fs_access_control_section_header;     ///< Offset value relative to the start of this header.
    NpdmSectionHeader srv_access_control_section_header;    ///< Offset value relative to the start of this header.
    NpdmSectionHeader kernel_capability_section_header;     ///< Offset value relative to the start of this header.
    u8 reserved_3[0x8];
} NpdmAciHeader;

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

/// AcidFsAccessControl descriptor. Part of the ACID section body.
/// This is followed by:
///     * 'content_owner_id_count' content owner IDs.
///     * 'save_data_owner_id_count' save data owner IDs.
#pragma pack(push, 1)
typedef struct {
    u8 version;                                             ///< Always non-zero. Usually set to 1.
    u8 content_owner_id_count;
    u8 save_data_owner_id_count;
    u8 reserved;
    u64 flags;
    u64 content_owner_id_min;
    u64 content_owner_id_max;
    u64 save_data_owner_id_min;
    u64 save_data_owner_id_max;
} NpdmAcidFsAccessControlDescriptor;
#pragma pack(pop)

/// AciFsAccessControl descriptor. Part of the ACI0 section body.
/// This is followed by:
///     * A NpdmAciFsAccessControlDescriptorContentOwnerBlock if 'content_owner_info_size' is greater than zero.
///     * A NpdmAciFsAccessControlDescriptorSaveDataOwnerBlock if 'save_data_owner_info_size' is greater than zero.
///         * If available, this block is padded to a 0x4-byte boundary and followed by 'save_data_owner_id_count' save data owner IDs.
#pragma pack(push, 1)
typedef struct {
    u8 version;
    u8 reserved_1[0x3];
    u64 flags;
    u32 content_owner_info_offset;
    u32 content_owner_info_size;
    u32 save_data_owner_info_offset;
    u32 save_data_owner_info_size;
} NpdmAciFsAccessControlDescriptor;
#pragma pack(pop)

/// Placed after NpdmAciFsAccessControlDescriptor if its 'content_owner_info_size' member is greater than zero.
typedef struct {
    u32 content_owner_id_count;
    u64 content_owner_id[];     ///< 'content_owner_id_count' content owned IDs.
} NpdmAciFsAccessControlDescriptorContentOwnerBlock;

typedef enum {
    NpdmAccessibility_Read      = 1,
    NpdmAccessibility_Write     = 2,
    NpdmAccessibility_ReadWrite = 3
} NpdmAccessibility;

/// Placed after NpdmAciFsAccessControlDescriptor / NpdmAciFsAccessControlDescriptorContentOwnerBlock if the 'content_owner_info_size' member from NpdmAciFsAccessControlDescriptor is greater than zero.
/// If available, this block is padded to a 0x4-byte boundary and followed by 'save_data_owner_id_count' save data owner IDs.
typedef struct {
    u32 save_data_owner_id_count;
    u8 accessibility[];             ///< 'save_data_owner_id_count' NpdmAccessibility fields.
} NpdmAciFsAccessControlDescriptorSaveDataOwnerBlock;

typedef struct {
    u8 name_length : 3; ///< Service name length minus 1.
    u8 reserved    : 4;
    u8 is_server   : 1; ///< Indicates if the service is allowed to be registered.
} NpdmSrvAccessControlEntryDescriptor;

/// SrvAccessControl descriptor. Part of the ACID and ACI0 section bodies.
/// This descriptor is composed of a variable number of NpdmSrvAccessControlEntry elements, each one with a variable size.
/// Since the total number of services isn't stored anywhere, this descriptor must be parsed until its total size is reached.
typedef struct {
    NpdmSrvAccessControlEntryDescriptor descriptor;
    char name[];                                    ///< Service name, stored without a NULL terminator. Supports the "*" wildcard character.
} NpdmSrvAccessControlEntry;

/// ThreadInfo entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number     : 3;   ///< All bits set to one.
    u32 reserved         : 1;   ///< Always set to zero.
    u32 lowest_priority  : 6;
    u32 highest_priority : 6;
    u32 min_core_number  : 8;
    u32 max_core_number  : 8;
} NpdmThreadInfo;

/// System call table.
typedef enum {
    ///< System calls for index 0.
    NpdmSystemCallIds_Unknown1                       = BIT(0),
    NpdmSystemCallIds_SetHeapSize                    = BIT(1),
    NpdmSystemCallIds_SetMemoryPermission            = BIT(2),
    NpdmSystemCallIds_SetMemoryAttribute             = BIT(3),
    NpdmSystemCallIds_MapMemory                      = BIT(4),
    NpdmSystemCallIds_UnmapMemory                    = BIT(5),
    NpdmSystemCallIds_QueryMemory                    = BIT(6),
    NpdmSystemCallIds_ExitProcess                    = BIT(7),
    NpdmSystemCallIds_CreateThread                   = BIT(8),
    NpdmSystemCallIds_StartThread                    = BIT(9),
    NpdmSystemCallIds_ExitThread                     = BIT(10),
    NpdmSystemCallIds_SleepThread                    = BIT(11),
    NpdmSystemCallIds_GetThreadPriority              = BIT(12),
    NpdmSystemCallIds_SetThreadPriority              = BIT(13),
    NpdmSystemCallIds_GetThreadCoreMask              = BIT(14),
    NpdmSystemCallIds_SetThreadCoreMask              = BIT(15),
    NpdmSystemCallIds_GetCurrentProcessorNumber      = BIT(16),
    NpdmSystemCallIds_SignalEvent                    = BIT(17),
    NpdmSystemCallIds_ClearEvent                     = BIT(18),
    NpdmSystemCallIds_MapSharedMemory                = BIT(19),
    NpdmSystemCallIds_UnmapSharedMemory              = BIT(20),
    NpdmSystemCallIds_CreateTransferMemory           = BIT(21),
    NpdmSystemCallIds_CloseHandle                    = BIT(22),
    NpdmSystemCallIds_ResetSignal                    = BIT(23),
    
    ///< System calls for index 1.
    NpdmSystemCallIds_WaitSynchronization            = BIT(0),
    NpdmSystemCallIds_CancelSynchronization          = BIT(1),
    NpdmSystemCallIds_ArbitrateLock                  = BIT(2),
    NpdmSystemCallIds_ArbitrateUnlock                = BIT(3),
    NpdmSystemCallIds_WaitProcessWideKeyAtomic       = BIT(4),
    NpdmSystemCallIds_SignalProcessWideKey           = BIT(5),
    NpdmSystemCallIds_GetSystemTick                  = BIT(6),
    NpdmSystemCallIds_ConnectToNamedPort             = BIT(7),
    NpdmSystemCallIds_SendSyncRequestLight           = BIT(8),
    NpdmSystemCallIds_SendSyncRequest                = BIT(9),
    NpdmSystemCallIds_SendSyncRequestWithUserBuffer  = BIT(10),
    NpdmSystemCallIds_SendAsyncRequestWithUserBuffer = BIT(11),
    NpdmSystemCallIds_GetProcessId                   = BIT(12),
    NpdmSystemCallIds_GetThreadId                    = BIT(13),
    NpdmSystemCallIds_Break                          = BIT(14),
    NpdmSystemCallIds_OutputDebugString              = BIT(15),
    NpdmSystemCallIds_ReturnFromException            = BIT(16),
    NpdmSystemCallIds_GetInfo                        = BIT(17),
    NpdmSystemCallIds_FlushEntireDataCache           = BIT(18),
    NpdmSystemCallIds_FlushDataCache                 = BIT(19),
    NpdmSystemCallIds_MapPhysicalMemory              = BIT(20),
    NpdmSystemCallIds_UnmapPhysicalMemory            = BIT(21),
    NpdmSystemCallIds_GetDebugFutureThreadInfo       = BIT(22),
    NpdmSystemCallIds_GetLastThreadInfo              = BIT(23),
    
    ///< System calls for index 2.
    NpdmSystemCallIds_GetResourceLimitLimitValue     = BIT(0),
    NpdmSystemCallIds_GetResourceLimitCurrentValue   = BIT(1),
    NpdmSystemCallIds_SetThreadActivity              = BIT(2),
    NpdmSystemCallIds_GetThreadContext3              = BIT(3),
    NpdmSystemCallIds_WaitForAddress                 = BIT(4),
    NpdmSystemCallIds_SignalToAddress                = BIT(5),
    NpdmSystemCallIds_SynchronizePreemptionState     = BIT(6),
    NpdmSystemCallIds_Unknown2                       = BIT(7),
    NpdmSystemCallIds_Unknown3                       = BIT(8),
    NpdmSystemCallIds_Unknown4                       = BIT(9),
    NpdmSystemCallIds_Unknown5                       = BIT(10),
    NpdmSystemCallIds_Unknown6                       = BIT(11),
    NpdmSystemCallIds_KernelDebug                    = BIT(12),
    NpdmSystemCallIds_ChangeKernelTraceState         = BIT(13),
    NpdmSystemCallIds_Unknown7                       = BIT(14),
    NpdmSystemCallIds_Unknown8                       = BIT(15),
    NpdmSystemCallIds_CreateSession                  = BIT(16),
    NpdmSystemCallIds_AcceptSession                  = BIT(17),
    NpdmSystemCallIds_ReplyAndReceiveLight           = BIT(18),
    NpdmSystemCallIds_ReplyAndReceive                = BIT(19),
    NpdmSystemCallIds_ReplyAndReceiveWithUserBuffer  = BIT(20),
    NpdmSystemCallIds_CreateEvent                    = BIT(21),
    NpdmSystemCallIds_Unknown9                       = BIT(22),
    NpdmSystemCallIds_Unknown10                      = BIT(23),
    
    ///< System calls for index 3.
    NpdmSystemCallIds_MapPhysicalMemoryUnsafe        = BIT(0),
    NpdmSystemCallIds_UnmapPhysicalMemoryUnsafe      = BIT(1),
    NpdmSystemCallIds_SetUnsafeLimit                 = BIT(2),
    NpdmSystemCallIds_CreateCodeMemory               = BIT(3),
    NpdmSystemCallIds_ControlCodeMemory              = BIT(4),
    NpdmSystemCallIds_SleepSystem                    = BIT(5),
    NpdmSystemCallIds_ReadWriteRegister              = BIT(6),
    NpdmSystemCallIds_SetProcessActivity             = BIT(7),
    NpdmSystemCallIds_CreateSharedMemory             = BIT(8),
    NpdmSystemCallIds_MapTransferMemory              = BIT(9),
    NpdmSystemCallIds_UnmapTransferMemory            = BIT(10),
    NpdmSystemCallIds_CreateInterruptEvent           = BIT(11),
    NpdmSystemCallIds_QueryPhysicalAddress           = BIT(12),
    NpdmSystemCallIds_QueryIoMapping                 = BIT(13),
    NpdmSystemCallIds_CreateDeviceAddressSpace       = BIT(14),
    NpdmSystemCallIds_AttachDeviceAddressSpace       = BIT(15),
    NpdmSystemCallIds_DetachDeviceAddressSpace       = BIT(16),
    NpdmSystemCallIds_MapDeviceAddressSpaceByForce   = BIT(17),
    NpdmSystemCallIds_MapDeviceAddressSpaceAligned   = BIT(18),
    NpdmSystemCallIds_MapDeviceAddressSpace          = BIT(19),
    NpdmSystemCallIds_UnmapDeviceAddressSpace        = BIT(20),
    NpdmSystemCallIds_InvalidateProcessDataCache     = BIT(21),
    NpdmSystemCallIds_StoreProcessDataCache          = BIT(22),
    NpdmSystemCallIds_FlushProcessDataCache          = BIT(23),
    
    ///< System calls for index 4.
    NpdmSystemCallIds_DebugActiveProcess             = BIT(0),
    NpdmSystemCallIds_BreakDebugProcess              = BIT(1),
    NpdmSystemCallIds_TerminateDebugProcess          = BIT(2),
    NpdmSystemCallIds_GetDebugEvent                  = BIT(3),
    NpdmSystemCallIds_ContinueDebugEvent             = BIT(4),
    NpdmSystemCallIds_GetProcessList                 = BIT(5),
    NpdmSystemCallIds_GetThreadList                  = BIT(6),
    NpdmSystemCallIds_GetDebugThreadContext          = BIT(7),
    NpdmSystemCallIds_SetDebugThreadContext          = BIT(8),
    NpdmSystemCallIds_QueryDebugProcessMemory        = BIT(9),
    NpdmSystemCallIds_ReadDebugProcessMemory         = BIT(10),
    NpdmSystemCallIds_WriteDebugProcessMemory        = BIT(11),
    NpdmSystemCallIds_SetHardwareBreakPoint          = BIT(12),
    NpdmSystemCallIds_GetDebugThreadParam            = BIT(13),
    NpdmSystemCallIds_Unknown11                      = BIT(14),
    NpdmSystemCallIds_GetSystemInfo                  = BIT(15),
    NpdmSystemCallIds_CreatePort                     = BIT(16),
    NpdmSystemCallIds_ManageNamedPort                = BIT(17),
    NpdmSystemCallIds_ConnectToPort                  = BIT(18),
    NpdmSystemCallIds_SetProcessMemoryPermission     = BIT(19),
    NpdmSystemCallIds_MapProcessMemory               = BIT(20),
    NpdmSystemCallIds_UnmapProcessMemory             = BIT(21),
    NpdmSystemCallIds_QueryProcessMemory             = BIT(22),
    NpdmSystemCallIds_MapProcessCodeMemory           = BIT(23),
    
    ///< System calls for index 5.
    NpdmSystemCallIds_UnmapProcessCodeMemory         = BIT(0),
    NpdmSystemCallIds_CreateProcess                  = BIT(1),
    NpdmSystemCallIds_StartProcess                   = BIT(2),
    NpdmSystemCallIds_TerminateProcess               = BIT(3),
    NpdmSystemCallIds_GetProcessInfo                 = BIT(4),
    NpdmSystemCallIds_CreateResourceLimit            = BIT(5),
    NpdmSystemCallIds_SetResourceLimitLimitValue     = BIT(6),
    NpdmSystemCallIds_CallSecureMonitor              = BIT(7),
    NpdmSystemCallIds_Count                          = 0x80     ///< Total values supported by this enum.
} NpdmSystemCallIds;

/// EnableSystemCalls entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number                                        : 4;    ///< All bits set to one.
    u32 reserved                                            : 1;    ///< Always set to zero.
    u32 system_call_ids                                     : 24;   ///< NpdmSystemCallIds.
    u32 index                                               : 3;    ///< System calls index.
} NpdmEnableSystemCalls;

typedef enum {
    NpdmPermissionType_RW = 0,
    NpdmPermissionType_RO = 1
} NpdmPermissionType;

typedef enum {
    NpdmMapType_Io     = 0,
    NpdmMapType_Static = 1
} NpdmMapType;

/// MemoryMapType1 entry for the KernelCapability descriptor.
/// Always followed by a MemoryMapType2 entry.
typedef struct {
    u32 entry_number    : 6;    ///< All bits set to one.
    u32 reserved        : 1;    ///< Always set to zero.
    u32 begin_address   : 24;   ///< begin_address << 12.
    u32 permission_type : 1;    ///< NpdmPermissionType.
} NpdmMemoryMapType1;

/// MemoryMapType2 entry for the KernelCapability descriptor.
/// Always preceded by a MemoryMapType1 entry.
typedef struct {
    u32 entry_number : 6;   ///< All bits set to one.
    u32 reserved_1   : 1;   ///< Always set to zero.
    u32 size         : 20;  ///< size << 12.
    u32 reserved_2   : 4;
    u32 map_type     : 1;   ///< NpdmMapType.
} NpdmMemoryMapType2;

/// IoMemoryMap entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number  : 7;  ///< All bits set to one.
    u32 reserved      : 1;  ///< Always set to zero.
    u32 begin_address : 24; ///< begin_address << 12.
} NpdmIoMemoryMap;

typedef enum {
    NpdmRegionType_NoMapping         = 0,
    NpdmRegionType_KernelTraceBuffer = 1,
    NpdmRegionType_OnMemoryBootImage = 2,
    NpdmRegionType_DTB               = 3
} NpdmRegionType;

/// MemoryRegionMap entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number      : 10; ///< All bits set to one.
    u32 reserved          : 1;  ///< Always set to zero.
    u32 region_type_1     : 6;  ///< NpdmRegionType.
    u32 permission_type_1 : 1;  ///< NpdmPermissionType.
    u32 region_type_2     : 6;  ///< NpdmRegionType.
    u32 permission_type_2 : 1;  ///< NpdmPermissionType.
    u32 region_type_3     : 6;  ///< NpdmRegionType.
    u32 permission_type_3 : 1;  ///< NpdmPermissionType.
} NpdmMemoryRegionMap;

/// EnableInterrupts entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number       : 11;    ///< All bits set to one.
    u32 reserved           : 1;     ///< Always set to zero.
    u32 interrupt_number_1 : 10;    ///< 0x3FF means empty.
    u32 interrupt_number_2 : 10;    ///< 0x3FF means empty.
} NpdmEnableInterrupts;

typedef enum {
    NpdmProgramType_System      = 0,
    NpdmProgramType_Application = 1,
    NpdmProgramType_Applet      = 2
} NpdmProgramType;

/// MiscParams entry for the KernelCapability descriptor.
/// Defaults to 0 if this entry doesn't exist.
typedef struct {
    u32 entry_number : 13;  ///< All bits set to one.
    u32 reserved_1   : 1;   ///< Always set to zero.
    u32 program_type : 3;   ///< NpdmProgramType.
    u32 reserved_2   : 15;
} NpdmMiscParams;

/// KernelVersion entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number  : 14; ///< All bits set to one.
    u32 reserved      : 1;  ///< Always set to zero.
    u32 minor_version : 4;
    u32 major_version : 13;
} NpdmKernelVersion;

/// HandleTableSize entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number      : 15; ///< All bits set to one.
    u32 reserved_1        : 1;  ///< Always set to zero.
    u32 handle_table_size : 10;
    u32 reserved_2        : 6;
} NpdmHandleTableSize;

/// MiscFlags entry for the KernelCapability descriptor.
typedef struct {
    u32 entry_number : 16;  ///< All bits set to one.
    u32 reserved_1   : 1;   ///< Always set to zero.
    u32 enable_debug : 1;
    u32 force_debug  : 1;
    u32 reserved_2   : 13;
} NpdmMiscFlags;

/// KernelCapability descriptor. Part of the ACID and ACI0 section bodies.
/// This descriptor is composed of a variable number of u32 entries. Thus, the entry count can be calculated by dividing the KernelCapability descriptor size by 4.
/// The entry type is identified by a pattern of "01...11" (zero followed by ones) in the low u16, counting from the LSB. The variable number of ones must never exceed 16 (entirety of the low u16).
typedef struct {
    u32 value;
} NpdmKernelCapabilityEntry;

#endif /* __NPDM_H__ */
