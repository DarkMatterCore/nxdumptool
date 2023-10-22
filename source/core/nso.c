/*
 * nso.c
 *
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
#include "nso.h"

/* Function prototypes. */

static bool nsoGetModuleName(NsoContext *nso_ctx);
static u8 *nsoGetRodataSegment(NsoContext *nso_ctx);
static bool nsoGetModuleInfoName(NsoContext *nso_ctx, u8 *rodata_buf);
static bool nsoGetSectionFromRodataSegment(NsoContext *nso_ctx, u8 *rodata_buf, u8 **section_ptr, u64 section_offset, u64 section_size);

bool nsoInitializeContext(NsoContext *out, PartitionFileSystemContext *pfs_ctx, PartitionFileSystemEntry *pfs_entry)
{
    u8 *rodata_buf = NULL;
    bool success = false, dump_nso_header = false;

    if (!out || !pfs_ctx || !ncaStorageIsValidContext(&(pfs_ctx->storage_ctx)) || !pfs_ctx->nca_fs_ctx->nca_ctx || \
        pfs_ctx->nca_fs_ctx->nca_ctx->content_type != NcmContentType_Program || !pfs_ctx->offset || !pfs_ctx->size || !pfs_ctx->is_exefs || \
        pfs_ctx->header_size <= sizeof(PartitionFileSystemHeader) || !pfs_ctx->header || !pfs_entry)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Free output context beforehand. */
    nsoFreeContext(out);

    /* Update output context. */
    out->pfs_ctx = pfs_ctx;
    out->pfs_entry = pfs_entry;

    /* Get entry filename. */
    if (!(out->nso_filename = pfsGetEntryName(pfs_ctx, pfs_entry)) || !*(out->nso_filename))
    {
        LOG_MSG_ERROR("Invalid Partition FS entry filename!");
        goto end;
    }

    /* Read NSO header. */
    if (!pfsReadEntryData(pfs_ctx, pfs_entry, &(out->nso_header), sizeof(NsoHeader), 0))
    {
        LOG_MSG_ERROR("Failed to read NSO \"%s\" header!", out->nso_filename);;
        goto end;
    }

    /* Verify NSO header. */
    if (__builtin_bswap32(out->nso_header.magic) != NSO_HEADER_MAGIC)
    {
        LOG_MSG_ERROR("Invalid NSO \"%s\" header magic word! (0x%08X != 0x%08X).", out->nso_filename, __builtin_bswap32(out->nso_header.magic), __builtin_bswap32(NSO_HEADER_MAGIC));
        dump_nso_header = true;
        goto end;
    }

    if (out->nso_header.text_segment_info.file_offset < sizeof(NsoHeader) || !out->nso_header.text_segment_info.size || \
        ((out->nso_header.flags & NsoFlags_TextCompress) && (!out->nso_header.text_file_size || out->nso_header.text_file_size > out->nso_header.text_segment_info.size)) || \
        (!(out->nso_header.flags & NsoFlags_TextCompress) && out->nso_header.text_file_size != out->nso_header.text_segment_info.size) || \
        (out->nso_header.text_segment_info.file_offset + out->nso_header.text_file_size) > pfs_entry->size)
    {
        LOG_MSG_ERROR("Invalid .text segment offset/size for NSO \"%s\"! (0x%X, 0x%X, 0x%X).", out->nso_filename, out->nso_header.text_segment_info.file_offset, \
                      out->nso_header.text_file_size, out->nso_header.text_segment_info.size);
        dump_nso_header = true;
        goto end;
    }

    if (out->nso_header.rodata_segment_info.file_offset < sizeof(NsoHeader) || !out->nso_header.rodata_segment_info.size || \
        ((out->nso_header.flags & NsoFlags_RoCompress) && (!out->nso_header.rodata_file_size || out->nso_header.rodata_file_size > out->nso_header.rodata_segment_info.size)) || \
        (!(out->nso_header.flags & NsoFlags_RoCompress) && out->nso_header.rodata_file_size != out->nso_header.rodata_segment_info.size) || \
        (out->nso_header.rodata_segment_info.file_offset + out->nso_header.rodata_file_size) > pfs_entry->size)
    {
        LOG_MSG_ERROR("Invalid .rodata segment offset/size for NSO \"%s\"! (0x%X, 0x%X, 0x%X).", out->nso_filename, out->nso_header.rodata_segment_info.file_offset, \
                      out->nso_header.rodata_file_size, out->nso_header.rodata_segment_info.size);
        dump_nso_header = true;
        goto end;
    }

    if (out->nso_header.data_segment_info.file_offset < sizeof(NsoHeader) || !out->nso_header.data_segment_info.size || \
        ((out->nso_header.flags & NsoFlags_DataCompress) && (!out->nso_header.data_file_size || out->nso_header.data_file_size > out->nso_header.data_segment_info.size)) || \
        (!(out->nso_header.flags & NsoFlags_DataCompress) && out->nso_header.data_file_size != out->nso_header.data_segment_info.size) || \
        (out->nso_header.data_segment_info.file_offset + out->nso_header.data_file_size) > pfs_entry->size)
    {
        LOG_MSG_ERROR("Invalid .data segment offset/size for NSO \"%s\"! (0x%X, 0x%X, 0x%X).", out->nso_filename, out->nso_header.data_segment_info.file_offset, \
                      out->nso_header.data_file_size, out->nso_header.data_segment_info.size);
        dump_nso_header = true;
        goto end;
    }

    if (out->nso_header.module_name_size > 1 && (out->nso_header.module_name_offset < sizeof(NsoHeader) || (out->nso_header.module_name_offset + out->nso_header.module_name_size) > pfs_entry->size))
    {
        LOG_MSG_ERROR("Invalid module name offset/size for NSO \"%s\"! (0x%X, 0x%X).", out->nso_filename, out->nso_header.module_name_offset, out->nso_header.module_name_size);
        dump_nso_header = true;
        goto end;
    }

    if (out->nso_header.api_info_section_info.size && (out->nso_header.api_info_section_info.offset + out->nso_header.api_info_section_info.size) > out->nso_header.rodata_segment_info.size)
    {
        LOG_MSG_ERROR("Invalid .api_info section offset/size for NSO \"%s\"! (0x%X, 0x%X).", out->nso_filename, out->nso_header.api_info_section_info.offset, out->nso_header.api_info_section_info.size);
        dump_nso_header = true;
        goto end;
    }

    if (out->nso_header.dynstr_section_info.size && (out->nso_header.dynstr_section_info.offset + out->nso_header.dynstr_section_info.size) > out->nso_header.rodata_segment_info.size)
    {
        LOG_MSG_ERROR("Invalid .dynstr section offset/size for NSO \"%s\"! (0x%X, 0x%X).", out->nso_filename, out->nso_header.dynstr_section_info.offset, out->nso_header.dynstr_section_info.size);
        dump_nso_header = true;
        goto end;
    }

    if (out->nso_header.dynsym_section_info.size && (out->nso_header.dynsym_section_info.offset + out->nso_header.dynsym_section_info.size) > out->nso_header.rodata_segment_info.size)
    {
        LOG_MSG_ERROR("Invalid .dynsym section offset/size for NSO \"%s\"! (0x%X, 0x%X).", out->nso_filename, out->nso_header.dynsym_section_info.offset, out->nso_header.dynsym_section_info.size);
        dump_nso_header = true;
        goto end;
    }

    /* Get module name. */
    if (!nsoGetModuleName(out)) goto end;

    /* Get .rodata segment. */
    if (!(rodata_buf = nsoGetRodataSegment(out))) goto end;

    /* Get module info name. */
    if (!nsoGetModuleInfoName(out, rodata_buf)) goto end;

    /* Get .api_info section data. */
    if (!nsoGetSectionFromRodataSegment(out, rodata_buf, (u8**)&(out->rodata_api_info_section), out->nso_header.api_info_section_info.offset, out->nso_header.api_info_section_info.size)) goto end;
    out->rodata_api_info_section_size = out->nso_header.api_info_section_info.size;

    /* Get .dynstr section data. */
    if (!nsoGetSectionFromRodataSegment(out, rodata_buf, (u8**)&(out->rodata_dynstr_section), out->nso_header.dynstr_section_info.offset, out->nso_header.dynstr_section_info.size)) goto end;
    out->rodata_dynstr_section_size = out->nso_header.dynstr_section_info.size;

    /* Get .dynsym section data. */
    if (!nsoGetSectionFromRodataSegment(out, rodata_buf, &(out->rodata_dynsym_section), out->nso_header.dynsym_section_info.offset, out->nso_header.dynsym_section_info.size)) goto end;
    out->rodata_dynsym_section_size = out->nso_header.dynsym_section_info.size;

    success = true;

end:
    if (rodata_buf) free(rodata_buf);

    if (!success)
    {
        if (dump_nso_header) LOG_DATA_DEBUG(&(out->nso_header), sizeof(NsoHeader), "NSO header dump:");

        nsoFreeContext(out);
    }

    return success;
}

static bool nsoGetModuleName(NsoContext *nso_ctx)
{
    if (nso_ctx->nso_header.module_name_offset < sizeof(NsoHeader) || nso_ctx->nso_header.module_name_size <= 1) return true;

    NsoModuleName module_name = {0};

    /* Get module name. */
    if (!pfsReadEntryData(nso_ctx->pfs_ctx, nso_ctx->pfs_entry, &module_name, sizeof(NsoModuleName), nso_ctx->nso_header.module_name_offset))
    {
        LOG_MSG_ERROR("Failed to read NSO \"%s\" module name length!", nso_ctx->nso_filename);
        return false;
    }

    /* Verify module name length. */
    if (module_name.name_length != ((u8)nso_ctx->nso_header.module_name_size - 1))
    {
        LOG_MSG_ERROR("NSO \"%s\" module name length mismatch! (0x%02X != 0x%02X).", nso_ctx->nso_filename, module_name.name_length, (u8)nso_ctx->nso_header.module_name_size - 1);
        return false;
    }

    /* Allocate memory for the module name. */
    nso_ctx->module_name = calloc(nso_ctx->nso_header.module_name_size, sizeof(char));
    if (!nso_ctx->module_name)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NSO \"%s\" module name!", nso_ctx->nso_filename);
        return false;
    }

    /* Read module name string. */
    if (!pfsReadEntryData(nso_ctx->pfs_ctx, nso_ctx->pfs_entry, nso_ctx->module_name, module_name.name_length, nso_ctx->nso_header.module_name_offset + 1))
    {
        LOG_MSG_ERROR("Failed to read NSO \"%s\" module name string!", nso_ctx->nso_filename);
        return false;
    }

    return true;
}

static u8 *nsoGetRodataSegment(NsoContext *nso_ctx)
{
    int lz4_res = 0;
    bool compressed = (nso_ctx->nso_header.flags & NsoFlags_RoCompress), verify = (nso_ctx->nso_header.flags & NsoFlags_RoHash);

    u8 *rodata_buf = NULL;
    u64 rodata_buf_size = (compressed ? LZ4_DECOMPRESS_INPLACE_BUFFER_SIZE(nso_ctx->nso_header.rodata_segment_info.size) : nso_ctx->nso_header.rodata_segment_info.size);

    u8 *rodata_read_ptr = NULL;
    u64 rodata_read_size = (compressed ? nso_ctx->nso_header.rodata_file_size : nso_ctx->nso_header.rodata_segment_info.size);

    u8 rodata_hash[SHA256_HASH_SIZE] = {0};

    bool success = false;

    /* Allocate memory for the .rodata buffer. */
    if (!(rodata_buf = calloc(rodata_buf_size, sizeof(u8))))
    {
        LOG_MSG_ERROR("Failed to allocate 0x%lX bytes for the .rodata segment in NSO \"%s\"!", rodata_buf_size, nso_ctx->nso_filename);
        return NULL;
    }

    rodata_read_ptr = (compressed ? (rodata_buf + (rodata_buf_size - nso_ctx->nso_header.rodata_file_size)) : rodata_buf);

    /* Read .rodata segment data. */
    if (!pfsReadEntryData(nso_ctx->pfs_ctx, nso_ctx->pfs_entry, rodata_read_ptr, rodata_read_size, nso_ctx->nso_header.rodata_segment_info.file_offset))
    {
        LOG_MSG_ERROR("Failed to read .rodata segment in NSO \"%s\"!", nso_ctx->nso_filename);
        goto end;
    }

    if (compressed)
    {
        /* Decompress .rodata segment in-place. */
        if ((lz4_res = LZ4_decompress_safe((char*)rodata_read_ptr, (char*)rodata_buf, (int)nso_ctx->nso_header.rodata_file_size, (int)rodata_buf_size)) != \
            (int)nso_ctx->nso_header.rodata_segment_info.size)
        {
            LOG_MSG_ERROR("LZ4 decompression failed for NSO \"%s\"! (%d).", nso_ctx->nso_filename, lz4_res);
            goto end;
        }
    }

    if (verify)
    {
        /* Verify .rodata segment hash. */
        sha256CalculateHash(rodata_hash, rodata_buf, nso_ctx->nso_header.rodata_segment_info.size);
        if (memcmp(rodata_hash, nso_ctx->nso_header.rodata_segment_hash, SHA256_HASH_SIZE) != 0)
        {
            LOG_MSG_ERROR(".rodata segment checksum mismatch for NSO \"%s\"!", nso_ctx->nso_filename);
            goto end;
        }
    }

    success = true;

end:
    if (!success && rodata_buf)
    {
        free(rodata_buf);
        rodata_buf = NULL;
    }

    return rodata_buf;
}

static bool nsoGetModuleInfoName(NsoContext *nso_ctx, u8 *rodata_buf)
{
    NsoModuleInfo *module_info = (NsoModuleInfo*)(rodata_buf + 0x4);
    if (!module_info->name_length) return true;

    /* Allocate memory for the module info name. */
    nso_ctx->module_info_name = calloc(module_info->name_length + 1, sizeof(char));
    if (!nso_ctx->module_info_name)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NSO \"%s\" module info name!", nso_ctx->nso_filename);
        return false;
    }

    /* Copy module info name. */
    sprintf(nso_ctx->module_info_name, "%.*s", (int)module_info->name_length, module_info->name);

    return true;
}

static bool nsoGetSectionFromRodataSegment(NsoContext *nso_ctx, u8 *rodata_buf, u8 **section_ptr, u64 section_offset, u64 section_size)
{
    if (!section_size || (section_offset + section_size) > nso_ctx->nso_header.rodata_segment_info.size) return true;

    /* Allocate memory for the desired .rodata section. */
    if (!(*section_ptr = malloc(section_size)))
    {
        LOG_MSG_ERROR("Failed to allocate 0x%lX bytes for section at .rodata offset 0x%lX in NSO \"%s\"!", section_size, section_offset, nso_ctx->nso_filename);
        return false;
    }

    /* Copy .rodata section data. */
    memcpy(*section_ptr, rodata_buf + section_offset, section_size);

    return true;
}
