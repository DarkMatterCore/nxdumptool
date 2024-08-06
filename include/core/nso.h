/*
 * nso.h
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

#ifndef __NSO_H__
#define __NSO_H__

#include "pfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NSO_HEADER_MAGIC    0x4E534F30  /* "NSO0". */
#define NSO_MOD_MAGIC       0x4D4F4430  /* "MOD0". */

typedef enum {
    NsoFlags_None         = 0,
    NsoFlags_TextCompress = BIT(0), ///< Determines if .text segment is LZ4-compressed.
    NsoFlags_RoCompress   = BIT(1), ///< Determines if .rodata segment is LZ4-compressed.
    NsoFlags_DataCompress = BIT(2), ///< Determines if .data segment is LZ4-compressed.
    NsoFlags_TextHash     = BIT(3), ///< Determines if .text segment hash must be checked during load.
    NsoFlags_RoHash       = BIT(4), ///< Determines if .rodata segment hash must be checked during load.
    NsoFlags_DataHash     = BIT(5), ///< Determines if .data segment hash must be checked during load.
    NsoFlags_Count        = 6       ///< Total values supported by this enum.
} NsoFlags;

typedef struct {
    u32 file_offset;    ///< NSO segment offset.
    u32 memory_offset;  ///< Memory segment offset.
    u32 size;           ///< Decompressed segment size.
} NsoSegmentInfo;

NXDT_ASSERT(NsoSegmentInfo, 0xC);

typedef struct {
    u32 offset; ///< Relative to the .rodata segment start.
    u32 size;
} NsoSectionInfo;

NXDT_ASSERT(NsoSectionInfo, 0x8);

/// This is the start of every NSO.
/// This can be optionally followed by the NSO module name.
/// If available, the 'module_name_size' member is greater than 1, and the 'module_name_offset' member will usually be set to 0x100 (the size of this header).
typedef struct {
    u32 magic;                                  ///< "NSO0".
    u32 version;                                ///< Always set to 0.
    u8 reserved_1[0x4];
    u32 flags;                                  ///< NsoFlags.
    NsoSegmentInfo text_segment_info;
    u32 module_name_offset;                     ///< NSO module name offset.
    NsoSegmentInfo rodata_segment_info;
    u32 module_name_size;                       ///< NSO module name size.
    NsoSegmentInfo data_segment_info;
    u32 bss_size;
    u8 module_id[0x20];                         ///< Also known as build ID.
    u32 text_file_size;                         ///< .text segment compressed size (if NsoFlags_TextCompress is enabled).
    u32 rodata_file_size;                       ///< .rodata segment compressed size (if NsoFlags_RoCompress is enabled).
    u32 data_file_size;                         ///< .data segment compressed size (if NsoFlags_DataCompress is enabled).
    u8 reserved_2[0x1C];
    NsoSectionInfo api_info_section_info;
    NsoSectionInfo dynstr_section_info;
    NsoSectionInfo dynsym_section_info;
    u8 text_segment_hash[SHA256_HASH_SIZE];     ///< Decompressed .text segment SHA-256 checksum.
    u8 rodata_segment_hash[SHA256_HASH_SIZE];   ///< Decompressed .rodata segment SHA-256 checksum.
    u8 data_segment_hash[SHA256_HASH_SIZE];     ///< Decompressed .data segment SHA-256 checksum.
} NsoHeader;

NXDT_ASSERT(NsoHeader, 0x100);

/// Placed at the very start of the decompressed .text segment.
typedef struct {
    u32 version;        ///< Usually set to 0 or a branch instruction (0x14000002). Set to 1 or 0x14000003 if a NsoNnSdkVersion block is available.
    s32 mod_offset;     ///< NsoModHeader block offset (relative to the start of this header). Almost always set to 0x8 (the size of this struct).
} NsoModStart;

NXDT_ASSERT(NsoModStart, 0x8);

/// This is essentially a replacement for the PT_DYNAMIC program header available in ELF binaries.
/// All offsets are signed 32-bit values relative to the start of this header.
/// This is usually placed at the start of the decompressed .text segment, right after a NsoModStart block.
/// However, in some NSOs, it can instead be placed at the start of the decompressed .rodata segment, right after its NsoModuleInfo block.
/// In these cases, the 'mod_offset' value from the NsoModStart block will point to an offset within the .rodata segment.
typedef struct  {
    u32 magic;                      ///< "MOD0".
    s32 dynamic_offset;
    s32 bss_start_offset;
    s32 bss_end_offset;
    s32 eh_frame_hdr_start_offset;
    s32 eh_frame_hdr_end_offset;
    s32 module_object_offset;       ///< Typically equal to bss_start_offset.
} NsoModHeader;

NXDT_ASSERT(NsoModHeader, 0x1C);

/// Only available in 17.0.0+ binaries. Holds the nnSdk version used to build this NRO.
/// This is usually placed right after the NsoModHeader block.
typedef struct {
    u32 major;
    u32 minor;
    u32 micro;
} NsoNnSdkVersion;

NXDT_ASSERT(NsoNnSdkVersion, 0xC);

/// Placed at the start of the decompressed .rodata segment + 0x4.
/// If the 'name_length' element is greater than 0, 'name' will hold the module name.
typedef struct {
    u32 name_length;
    char name[];
} NsoModuleInfo;

NXDT_ASSERT(NsoModuleInfo, 0x4);

typedef struct {
    PartitionFileSystemContext *pfs_ctx;    ///< PartitionFileSystemContext for the Program NCA FS section #0, which is where this NSO is stored.
    PartitionFileSystemEntry *pfs_entry;    ///< PartitionFileSystemEntry for this NSO in the Program NCA FS section #0. Used to read NSO data.
    char *nso_filename;                     ///< Pointer to the NSO filename in the Program NCA FS section #0.
    NsoHeader nso_header;                   ///< NSO header.
    char *module_name;                      ///< Pointer to a dynamically allocated buffer that holds the NSO module name, if available. Otherwise, this is set to NULL.
    NsoNnSdkVersion *nnsdk_version;         ///< Pointer to a dynamically allocated buffer that holds the nnSdk version info, if available. Otherwise, this is set to NULL.
    char *module_info_name;                 ///< Pointer to a dynamically allocated buffer that holds the .rodata module info module name, if available. Otherwise, this is set to NULL.
    char *rodata_api_info_section;          ///< Pointer to a dynamically allocated buffer that holds the .rodata API info section data, if available. Otherwise, this is set to NULL.
                                            ///< Middleware and GuidelineApi entries are retrieved from this section.
    u64 rodata_api_info_section_size;       ///< .rodata API info section size, if available. Otherwise, this is set to 0. Kept here for convenience - this is part of 'nso_header'.
    char *rodata_dynstr_section;            ///< Pointer to a dynamically allocated buffer that holds the .rodata dynamic string section data. UnresolvedApi data is retrieved from this section.
    u64 rodata_dynstr_section_size;         ///< .rodata dynamic string section size. Kept here for convenience - this is part of 'nso_header'.
    u8 *rodata_dynsym_section;              ///< Pointer to a dynamically allocated buffer that holds the .rodata dynamic symbol section data. Used to retrieve pointers to symbol strings within dynstr.
    u64 rodata_dynsym_section_size;         ///< .rodata dynamic symbol section size. Kept here for convenience - this is part of 'nso_header'.
} NsoContext;

/// Initializes a NsoContext using a previously initialized PartitionFileSystemContext (which must belong to the ExeFS from a Program NCA) and a PartitionFileSystemEntry belonging to an underlying NSO.
bool nsoInitializeContext(NsoContext *out, PartitionFileSystemContext *pfs_ctx, PartitionFileSystemEntry *pfs_entry);

/// Helper inline functions.

NX_INLINE void nsoFreeContext(NsoContext *nso_ctx)
{
    if (!nso_ctx) return;
    if (nso_ctx->module_name) free(nso_ctx->module_name);
    if (nso_ctx->nnsdk_version) free(nso_ctx->nnsdk_version);
    if (nso_ctx->module_info_name) free(nso_ctx->module_info_name);
    if (nso_ctx->rodata_api_info_section) free(nso_ctx->rodata_api_info_section);
    if (nso_ctx->rodata_dynstr_section) free(nso_ctx->rodata_dynstr_section);
    if (nso_ctx->rodata_dynsym_section) free(nso_ctx->rodata_dynsym_section);
    memset(nso_ctx, 0, sizeof(NsoContext));
}

#ifdef __cplusplus
}
#endif

#endif /* __NSO_H__ */
