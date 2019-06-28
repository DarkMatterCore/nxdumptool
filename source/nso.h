#pragma once

#ifndef __NSO_H__
#define __NSO_H__

#include <switch.h>

#define NSO_MAGIC       (u32)0x4E534F30     // "NSO0"
#define MOD_MAGIC       (u32)0x4D4F4430     // "MOD0"

#define DT_STRTAB       0x05
#define DT_SYMTAB       0x06
#define DT_STRSZ        0x0A

#define ST_OBJECT       0x01

typedef struct {
    u32 file_offset;
    u32 memory_offset;
    u32 decompressed_size;
} PACKED segment_header_t;

typedef struct {
    u32 region_offset;
    u32 region_size;
} PACKED rodata_extent_t;

typedef struct {
    u32 magic;
    u32 version;
    u32 reserved1;
    u32 flags;
    segment_header_t text_segment_header;
    u32 module_offset;
    segment_header_t rodata_segment_header;
    u32 module_file_size;
    segment_header_t data_segment_header;
    u32 bss_size;
    u8 elf_note_build_id[0x20];
    u32 text_compressed_size;
    u32 rodata_compressed_size;
    u32 data_compressed_size;
    u8 reserved2[0x1C];
    rodata_extent_t rodata_api_info;
    rodata_extent_t rodata_dynstr;
    rodata_extent_t rodata_dynsym;
    u8 text_decompressed_hash[0x20];
    u8 rodata_decompressed_hash[0x20];
    u8 data_decompressed_hash[0x20];
} PACKED nso_header_t;

// Retrieves the middleware list from a NSO stored in a partition from a NCA file
bool retrieveMiddlewareListFromNso(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, Aes128CtrContext *aes_ctx, const char *nso_filename, u64 nso_base_offset, nso_header_t *nsoHeader, char *programInfoXml);

// Retrieves the symbols list from a NSO stored in a partition from a NCA file
bool retrieveSymbolsListFromNso(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, Aes128CtrContext *aes_ctx, const char *nso_filename, u64 nso_base_offset, nso_header_t *nsoHeader, char *programInfoXml);

#endif
