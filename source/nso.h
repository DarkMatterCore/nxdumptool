/*
 * nso.h
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

#ifndef __NSO_H__
#define __NSO_H__

#include "pfs.h"

#define NSO_HEADER_MAGIC    0x4E534F30  /* "NSO0". */
#define NSO_MOD_MAGIC       0x4D4F4430  /* "MOD0". */

typedef enum {
    NsoFlags_TextCompress = BIT(0), ///< Determines if .text segment is LZ4-compressed.
    NsoFlags_RoCompress   = BIT(1), ///< Determines if .rodata segment is LZ4-compressed.
    NsoFlags_DataCompress = BIT(2), ///< Determines if .data segment is LZ4-compressed.
    NsoFlags_TextHash     = BIT(3), ///< Determines if .text segment hash must be checked during load.
    NsoFlags_RoHash       = BIT(4), ///< Determines if .rodata segment hash must be checked during load.
    NsoFlags_DataHash     = BIT(5)  ///< Determines if .data segment hash must be checked during load.
} NsoFlags;

typedef struct {
    u32 file_offset;    ///< NSO segment offset.
    u32 memory_offset;  ///< Memory segment offset.
    u32 size;           ///< Decompressed segment size.
} NsoSegmentInfo;

typedef struct {
    u32 offset; ///< Relative to the .rodata segment start.
    u32 size;
} NsoSectionInfo;

/// This is the start of every NSO.
/// This is always followed by a NsoModuleName block.
typedef struct {
    u32 magic;                                  ///< "NSO0".
    u32 version;                                ///< Always set to 0.
    u8 reserved_1[0x4];
    u32 flags;                                  ///< NsoFlags.
    NsoSegmentInfo text_segment_info;
    u32 module_name_offset;                     ///< NsoModuleName block offset.
    NsoSegmentInfo rodata_segment_info;
    u32 module_name_size;                       ///< NsoModuleName block size.
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
    u8 text_segment_hash[0x20];                 ///< Decompressed .text segment SHA-256 checksum.
    u8 rodata_segment_hash[0x20];               ///< Decompressed .rodata segment SHA-256 checksum.
    u8 data_segment_hash[0x20];                 ///< Decompressed .data segment SHA-256 checksum.
} NsoHeader;

/// Usually placed right after NsoHeader, but it's actual offset may vary.
/// If the 'module_name_size' member from NsoHeader is greater than 1 and the 'name_length' element from NsoModuleName is greater than 0, 'name' will hold the module name.
typedef struct {
    u8 name_length;
    char name[];
} NsoModuleName;

/// Placed at the very start of the decompressed .text segment.
typedef struct {
    u32 entry_point;
    u32 mod_offset;     ///< NsoModHeader block offset (relative to the start of this header). Almost always set to 0x8 (the size of this struct).
} NsoModStart;

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

/// Placed at the start of the decompressed .rodata segment + 0x4.
/// If the 'name_length' element is greater than 0, 'name' will hold the module name.
typedef struct {
    u32 name_length;
    char name[];
} NsoModuleInfo;

typedef struct {
    PartitionFileSystemContext *pfs_ctx;    ///< PartitionFileSystemContext for the Program NCA FS section #0, which is where this NSO is stored.
    PartitionFileSystemEntry *pfs_entry;    ///< PartitionFileSystemEntry for this NSO in the Program NCA FS section #0. Used to read NSO data.
    char *nso_filename;                     ///< Pointer to the NSO filename in the Program NCA FS section #0.
    NsoHeader nso_header;                   ///< NSO header.
    char *module_name;                      ///< Pointer to a dynamically allocated buffer that holds the NSO module name, if available. Otherwise, this is set to NULL.
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
    if (nso_ctx->module_info_name) free(nso_ctx->module_info_name);
    if (nso_ctx->rodata_api_info_section) free(nso_ctx->rodata_api_info_section);
    if (nso_ctx->rodata_dynstr_section) free(nso_ctx->rodata_dynstr_section);
    if (nso_ctx->rodata_dynsym_section) free(nso_ctx->rodata_dynsym_section);
    memset(nso_ctx, 0, sizeof(NsoContext));
}

#endif /* __NSO_H__ */
