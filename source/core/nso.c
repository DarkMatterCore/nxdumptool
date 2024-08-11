/*
 * nso.c
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

#include <core/nxdt_utils.h>
#include <core/nso.h>

/* Type definitions. */

typedef enum {
    NsoSegmentType_Text   = 0,
    NsoSegmentType_RoData = 1,
    NsoSegmentType_Data   = 2,
    NsoSegmentType_Count  = 3   ///< Total values supported by this enum.
} NsoSegmentType;

typedef struct {
    u8 type;                ///< NsoSegmentType.
    const char *name;       ///< Pointer to a string that holds the segment name.
    NsoSegmentInfo info;    ///< Copied from the NSO header.
    u8 *data;               ///< Dynamically allocated buffer for the decompressed segment data.
} NsoSegment;

/* Global variables. */

static const char *g_nsoSegmentTypeNames[NsoSegmentType_Count] = {
    [NsoSegmentType_Text]   = ".text",
    [NsoSegmentType_RoData] = ".rodata",
    [NsoSegmentType_Data]   = ".data",
};

/* Function prototypes. */

static bool nsoGetModuleName(NsoContext *nso_ctx);

static bool nsoGetSegment(NsoContext *nso_ctx, NsoSegment *out, u8 type);
NX_INLINE void nsoFreeSegment(NsoSegment *segment);

NX_INLINE bool nsoIsNnSdkVersionWithinSegment(const NsoModStart *mod_start, const NsoSegment *segment, u32 nnsdk_version_memory_offset);
static bool nsoGetNnSdkVersion(NsoContext *nso_ctx, const NsoModStart *mod_start, const NsoSegment *segment, u32 nnsdk_version_memory_offset);

static bool nsoGetModulePath(NsoContext *nso_ctx, const NsoSegment *segment);

static bool nsoGetSectionFromRoDataSegment(NsoContext *nso_ctx, const NsoSectionInfo *section_info, const NsoSegment *segment, u8 **out_ptr);

bool nsoInitializeContext(NsoContext *out, PartitionFileSystemContext *pfs_ctx, PartitionFileSystemEntry *pfs_entry)
{
    NsoModStart mod_start = {0};
    NsoSegment segment = {0};
    u32 nnsdk_version_memory_offset = 0;
    bool success = false, dump_nso_header = false, read_nnsdk_version = false;

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

    dump_nso_header = true;

    /* Verify NSO header. */
    if (__builtin_bswap32(out->nso_header.magic) != NSO_HEADER_MAGIC)
    {
        LOG_MSG_ERROR("Invalid NSO \"%s\" header magic word! (0x%08X != 0x%08X).", out->nso_filename, __builtin_bswap32(out->nso_header.magic), __builtin_bswap32(NSO_HEADER_MAGIC));
        goto end;
    }

#define NSO_VERIFY_SEGMENT_INFO(name, flag) \
    if (out->nso_header.name##_segment_info.file_offset < sizeof(NsoHeader) || !out->nso_header.name##_segment_info.size || \
        ((out->nso_header.flags & NsoFlags_##flag##Compress) && (!out->nso_header.name##_file_size || out->nso_header.name##_file_size > out->nso_header.name##_segment_info.size)) || \
        (!(out->nso_header.flags & NsoFlags_##flag##Compress) && out->nso_header.name##_file_size != out->nso_header.name##_segment_info.size) || \
        (out->nso_header.name##_segment_info.file_offset + out->nso_header.name##_file_size) > pfs_entry->size) { \
        LOG_MSG_ERROR("Invalid ." #name " segment offset/size for NSO \"%s\"! (0x%X, 0x%X, 0x%X).", out->nso_filename, out->nso_header.name##_segment_info.file_offset, \
                      out->nso_header.name##_file_size, out->nso_header.name##_segment_info.size); \
        goto end; \
    }

#define NSO_VERIFY_RODATA_SECTION_INFO(name) \
    if (out->nso_header.name##_section_info.size && (out->nso_header.name##_section_info.offset + out->nso_header.name##_section_info.size) > out->nso_header.rodata_segment_info.size) { \
        LOG_MSG_ERROR("Invalid ." #name " section offset/size for NSO \"%s\"! (0x%X, 0x%X).", out->nso_filename, out->nso_header.name##_section_info.offset, out->nso_header.name##_section_info.size); \
        goto end; \
    }

#define NSO_GET_RODATA_SECTION(name) \
    do { \
        if (!nsoGetSectionFromRoDataSegment(out, &(out->nso_header.name##_section_info), &segment, (u8**)&(out->rodata_##name##_section))) goto end; \
        out->rodata_##name##_section_size = out->nso_header.name##_section_info.size; \
    } while(0)

    /* Verify NSO segment info. */
    NSO_VERIFY_SEGMENT_INFO(text, Text);
    NSO_VERIFY_SEGMENT_INFO(rodata, Ro);
    NSO_VERIFY_SEGMENT_INFO(data, Data);

    /* Verify NSO module name properties. */
    if (out->nso_header.module_name_size > 1 && (out->nso_header.module_name_offset < sizeof(NsoHeader) || (out->nso_header.module_name_offset + out->nso_header.module_name_size) > pfs_entry->size))
    {
        LOG_MSG_ERROR("Invalid module name offset/size for NSO \"%s\"! (0x%X, 0x%X).", out->nso_filename, out->nso_header.module_name_offset, out->nso_header.module_name_size);
        goto end;
    }

    /* Verify section info blocks for the .rodata segment. */
    NSO_VERIFY_RODATA_SECTION_INFO(api_info);
    NSO_VERIFY_RODATA_SECTION_INFO(dynstr);
    NSO_VERIFY_RODATA_SECTION_INFO(dynsym);

    /* Get module name. */
    if (!nsoGetModuleName(out)) goto end;

    /* Get .text segment. */
    if (!nsoGetSegment(out, &segment, NsoSegmentType_Text)) goto end;

    /* Get NsoModStart block. */
    memcpy(&mod_start, segment.data, sizeof(NsoModStart));

    /* Check if a NsoNnSdkVersion block exists within this NRO. */
    read_nnsdk_version = ((mod_start.version & 1) != 0 && mod_start.nnsdk_version_offset >= (s32)sizeof(NsoModStart));
    if (read_nnsdk_version)
    {
        /* Calculate memory offset for the NsoNnSdkVersion block. */
        nnsdk_version_memory_offset = (segment.info.memory_offset + (u32)mod_start.nnsdk_version_offset);

        /* Check if the NsoNnSdkVersion block is located within the .text segment. */
        /* If so, we'll retrieve it immediately. */
        if (nsoIsNnSdkVersionWithinSegment(&mod_start, &segment, nnsdk_version_memory_offset) && !nsoGetNnSdkVersion(out, &mod_start, &segment, nnsdk_version_memory_offset)) goto end;
    }

    /* Get .rodata segment. */
    if (!nsoGetSegment(out, &segment, NsoSegmentType_RoData)) goto end;

    /* Check if we didn't read the NsoNnSdkVersion block from the .text segment. */
    if (read_nnsdk_version && !out->nnsdk_version)
    {
        /* Check if the NsoNnSdkVersion block is located within the .rodata segment. */
        if (!nsoIsNnSdkVersionWithinSegment(&mod_start, &segment, nnsdk_version_memory_offset))
        {
            LOG_MSG_ERROR("nnSdk version struct not located within .text or .rodata segments in NSO \"%s\".", out->nso_filename);
            goto end;
        }

        /* Retrieve NsoNnSdkVersion block from the .rodata segment. */
        if (!nsoGetNnSdkVersion(out, &mod_start, &segment, nnsdk_version_memory_offset)) goto end;
    }

    /* Get module path from the .rodata segment. */
    if (!nsoGetModulePath(out, &segment)) goto end;

    /* Get sections from the .rodata segment. */
    NSO_GET_RODATA_SECTION(api_info);
    NSO_GET_RODATA_SECTION(dynstr);
    NSO_GET_RODATA_SECTION(dynsym);

    success = true;

#undef NSO_GET_RODATA_SECTION
#undef NSO_VERIFY_RODATA_SECTION_INFO
#undef NSO_VERIFY_SEGMENT_INFO

end:
    nsoFreeSegment(&segment);

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

    /* Allocate memory for the module name. */
    nso_ctx->module_name = calloc(nso_ctx->nso_header.module_name_size + 1, sizeof(char));
    if (!nso_ctx->module_name)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NSO \"%s\" module name!", nso_ctx->nso_filename);
        return false;
    }

    /* Read module name string. */
    if (!pfsReadEntryData(nso_ctx->pfs_ctx, nso_ctx->pfs_entry, nso_ctx->module_name, nso_ctx->nso_header.module_name_size, nso_ctx->nso_header.module_name_offset))
    {
        LOG_MSG_ERROR("Failed to read NSO \"%s\" module name string!", nso_ctx->nso_filename);
        return false;
    }

    return true;
}

static bool nsoGetSegment(NsoContext *nso_ctx, NsoSegment *out, u8 type)
{
    if (!nso_ctx || !out || type >= NsoSegmentType_Count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    const char *segment_name = g_nsoSegmentTypeNames[type];

    const NsoSegmentInfo *segment_info = (type == NsoSegmentType_Text ? &(nso_ctx->nso_header.text_segment_info) : \
                                         (type == NsoSegmentType_RoData ? &(nso_ctx->nso_header.rodata_segment_info) : &(nso_ctx->nso_header.data_segment_info)));

    u32 segment_file_size = (type == NsoSegmentType_Text ? nso_ctx->nso_header.text_file_size : \
                            (type == NsoSegmentType_RoData ? nso_ctx->nso_header.rodata_file_size : nso_ctx->nso_header.data_file_size));

    const u8 *segment_hash = (type == NsoSegmentType_Text ? nso_ctx->nso_header.text_segment_hash : \
                             (type == NsoSegmentType_RoData ? nso_ctx->nso_header.rodata_segment_hash : nso_ctx->nso_header.data_segment_hash));

    int lz4_res = 0;
    bool compressed = (nso_ctx->nso_header.flags & BIT(type)), verify = (nso_ctx->nso_header.flags & BIT(type + 3));

    u8 *buf = NULL;
    u32 buf_size = (compressed ? LZ4_DECOMPRESS_INPLACE_BUFFER_SIZE(segment_info->size) : segment_info->size);

    u8 *read_ptr = NULL;
    u32 read_size = (compressed ? segment_file_size : segment_info->size);

    u8 hash[SHA256_HASH_SIZE] = {0};

    bool success = false;

    /* Clear output struct. */
    nsoFreeSegment(out);

    /* Allocate memory for the segment buffer. */
    if (!(buf = calloc(buf_size, sizeof(u8))))
    {
        LOG_MSG_ERROR("Failed to allocate 0x%X bytes for the %s segment in NSO \"%s\"!", buf_size, segment_name, nso_ctx->nso_filename);
        return NULL;
    }

    read_ptr = (compressed ? (buf + (buf_size - segment_file_size)) : buf);

    /* Read segment data. */
    if (!pfsReadEntryData(nso_ctx->pfs_ctx, nso_ctx->pfs_entry, read_ptr, read_size, segment_info->file_offset))
    {
        LOG_MSG_ERROR("Failed to read %s segment in NSO \"%s\"!", segment_name, nso_ctx->nso_filename);
        goto end;
    }

    /* Decompress segment data in-place. */
    if (compressed && (lz4_res = LZ4_decompress_safe((char*)read_ptr, (char*)buf, (int)segment_file_size, (int)buf_size)) != (int)segment_info->size)
    {
        LOG_MSG_ERROR("LZ4 decompression failed for %s segment in NSO \"%s\"! (%d).", segment_name, nso_ctx->nso_filename, lz4_res);
        goto end;
    }

    if (verify)
    {
        /* Verify segment data hash. */
        sha256CalculateHash(hash, buf, segment_info->size);
        if (memcmp(hash, segment_hash, SHA256_HASH_SIZE) != 0)
        {
            LOG_MSG_ERROR("%s segment checksum mismatch for NSO \"%s\"!", segment_name, nso_ctx->nso_filename);
            goto end;
        }
    }

    /* Fill output struct. */
    out->type = type;
    out->name = segment_name;
    memcpy(&(out->info), segment_info, sizeof(NsoSegmentInfo));
    out->data = buf;

    success = true;

end:
    if (!success && buf) free(buf);

    return success;
}

NX_INLINE void nsoFreeSegment(NsoSegment *segment)
{
    if (!segment) return;
    if (segment->data) free(segment->data);
    memset(segment, 0, sizeof(NsoSegment));
}

NX_INLINE bool nsoIsNnSdkVersionWithinSegment(const NsoModStart *mod_start, const NsoSegment *segment, u32 nnsdk_version_memory_offset)
{
    return (mod_start && segment && nnsdk_version_memory_offset >= segment->info.memory_offset && \
            (nnsdk_version_memory_offset + sizeof(NsoNnSdkVersion)) <= (segment->info.memory_offset + segment->info.size));
}

static bool nsoGetNnSdkVersion(NsoContext *nso_ctx, const NsoModStart *mod_start, const NsoSegment *segment, u32 nnsdk_version_memory_offset)
{
    if (!nso_ctx || !mod_start || !segment || !segment->data)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Return immediately if the NsoNnSdkVersion block has already been retrieved. */
    if (nso_ctx->nnsdk_version) return true;

    /* Make sure we're targetting the right NSO segment. */
    if (!nsoIsNnSdkVersionWithinSegment(mod_start, segment, nnsdk_version_memory_offset))
    {
        LOG_MSG_ERROR("nnSdk version struct isn't located within %s segment in NSO \"%s\"! ([0x%X, 0x%X] not within [0x%X, 0x%X]).", segment->name, nso_ctx->nso_filename, \
                      nnsdk_version_memory_offset, nnsdk_version_memory_offset + (u32)sizeof(NsoNnSdkVersion), segment->info.memory_offset, segment->info.memory_offset + segment->info.size);
        return false;
    }

    /* Allocate memory for the NsoNnSdkVersion block. */
    nso_ctx->nnsdk_version = malloc(sizeof(NsoNnSdkVersion));
    if (!nso_ctx->nnsdk_version)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NSO \"%s\" nnSdk version struct!", nso_ctx->nso_filename);
        return false;
    }

    /* Calculate segment-relative offset for the NsoNnSdkVersion block and copy its data. */
    u32 nnsdk_version_segment_offset = (nnsdk_version_memory_offset - segment->info.memory_offset);
    memcpy(nso_ctx->nnsdk_version, segment->data + nnsdk_version_segment_offset, sizeof(NsoNnSdkVersion));

    LOG_MSG_DEBUG("nnSdk version (NSO \"%s\", %s segment, virtual offset 0x%X, physical offset 0x%X): %u.%u.%u.", nso_ctx->nso_filename, segment->name, \
                  nnsdk_version_memory_offset, nnsdk_version_segment_offset, nso_ctx->nnsdk_version->major, nso_ctx->nnsdk_version->minor, nso_ctx->nnsdk_version->micro);

    return true;
}

static bool nsoGetModulePath(NsoContext *nso_ctx, const NsoSegment *segment)
{
    if (!nso_ctx || !segment || segment->type != NsoSegmentType_RoData || !segment->data)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Get data from the start of the .rodata segment. */
    const NsoRoDataStart *rodata_start = (const NsoRoDataStart*)segment->data;

    /* Perform sanity checks. */
    if ((nso_ctx->nso_header.text_segment_info.memory_offset + rodata_start->data_segment_offset) == nso_ctx->nso_header.data_segment_info.memory_offset || \
        rodata_start->module_path.zero != 0 || !rodata_start->module_path.path_length || !rodata_start->module_path.path[0]) return true;

    /* Allocate memory for the module path string. */
    nso_ctx->module_path = calloc(rodata_start->module_path.path_length + 1, sizeof(char));
    if (!nso_ctx->module_path)
    {
        LOG_MSG_ERROR("Failed to allocate memory for NSO \"%s\" module path!", nso_ctx->nso_filename);
        return false;
    }

    /* Copy module path string. */
    sprintf(nso_ctx->module_path, "%.*s", (int)rodata_start->module_path.path_length, rodata_start->module_path.path);
    LOG_MSG_DEBUG("Module path (NSO \"%s\"): \"%s\".", nso_ctx->nso_filename, nso_ctx->module_path);

    return true;
}

static bool nsoGetSectionFromRoDataSegment(NsoContext *nso_ctx, const NsoSectionInfo *section_info, const NsoSegment *segment, u8 **out_ptr)
{
    if (!nso_ctx || !section_info || !segment || segment->type != NsoSegmentType_RoData || !segment->data || !out_ptr)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Return immediately if the desired section is not within the .rodata segment. */
    if (!section_info->size || (section_info->offset + section_info->size) > segment->info.size) return true;

    /* Allocate memory for the desired .rodata section. */
    if (!(*out_ptr = malloc(section_info->size)))
    {
        LOG_MSG_ERROR("Failed to allocate 0x%X bytes for section at .rodata segment offset 0x%X in NSO \"%s\"!", section_info->size, section_info->offset, nso_ctx->nso_filename);
        return false;
    }

    /* Copy .rodata section data. */
    memcpy(*out_ptr, segment->data + section_info->offset, section_info->size);

    return true;
}
