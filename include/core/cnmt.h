/*
 * cnmt.h
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

#pragma once

#ifndef __CNMT_H__
#define __CNMT_H__

#include "pfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CNMT_DIGEST_SIZE    SHA256_HASH_SIZE

/// Equivalent to NcmContentMetaAttribute.
typedef enum {
    ContentMetaAttribute_IncludesExFatDriver = BIT(0),
    ContentMetaAttribute_Rebootless          = BIT(1),
    ContentMetaAttribute_Compacted           = BIT(2),  ///< One or more NCAs use SparseInfo data.
    ContentMetaAttribute_Count               = 3        ///< Total values supported by this enum.
} ContentMetaAttribute;

typedef enum {
    ContentMetaInstallState_Committed = BIT(0),
    ContentMetaInstallState_Count     = 1       ///< Total values supported by this enum.
} ContentMetaInstallState;

/// Extended variation of NcmContentMetaHeader. This is essentially the start of every CNMT file.
/// Depending on the content meta type value, this header may be followed by an additional extended header for that specific content meta type.
/// NcmPackagedContentInfo and/or NcmContentMetaInfo entries may follow afterwards.
/// If the extended data size field from the extended header (if available) is non-zero, this data is saved after the content info entries.
/// Finally, a 0x20 byte long digest is appended to the EOF.
typedef struct {
    u64 title_id;
    Version version;
    u8 content_meta_type;                                   ///< NcmContentMetaType.
    u8 reserved_1;
    u16 extended_header_size;                               ///< Must match the size from the extended header struct for this content meta type (SystemUpdate, Application, Patch, AddOnContent, Delta).
    u16 content_count;                                      ///< Determines how many NcmPackagedContentInfo entries are available after the extended header.
    u16 content_meta_count;                                 ///< Determines how many NcmContentMetaInfo entries are available after the NcmPackagedContentInfo entries. Only used for SystemUpdate.
    u8 content_meta_attribute;                              ///< ContentMetaAttribute.
    u8 storage_id;                                          ///< NcmStorageId.
    u8 content_install_type;                                ///< NcmContentInstallType.
    u8 install_state;                                       ///< ContentMetaInstallState.
    Version required_download_system_version;
    u8 reserved_2[0x4];
} ContentMetaPackagedContentMetaHeader;

NXDT_ASSERT(ContentMetaPackagedContentMetaHeader, 0x20);

/// Extended header for the SystemUpdate title.
/// Equivalent to NcmSystemUpdateMetaExtendedHeader.
typedef struct {
    u32 extended_data_size;
} ContentMetaSystemUpdateMetaExtendedHeader;

NXDT_ASSERT(ContentMetaSystemUpdateMetaExtendedHeader, 0x4);

/// Extended header for Application titles.
/// Equivalent to NcmApplicationMetaExtendedHeader, but using Version structs.
typedef struct {
    u64 patch_id;
    Version required_system_version;
    Version required_application_version;
} ContentMetaApplicationMetaExtendedHeader;

NXDT_ASSERT(ContentMetaApplicationMetaExtendedHeader, 0x10);

/// Extended header for Patch titles.
/// Equivalent to NcmPatchMetaExtendedHeader, but using a Version struct.
typedef struct {
    u64 application_id;
    Version required_system_version;
    u32 extended_data_size;
    u8 reserved[0x8];
} ContentMetaPatchMetaExtendedHeader;

NXDT_ASSERT(ContentMetaPatchMetaExtendedHeader, 0x18);

typedef enum {
    ContentMetaContentAccessibility_Individual = BIT(0),
    ContentMetaContentAccessibility_Count      = 1          ///< Total values supported by this enum.
} ContentMetaContentAccessibility;

/// Extended header for AddOnContent tiles (15.0.0+).
/// Equivalent to NcmAddOnContentMetaExtendedHeader, but using a Version struct.
typedef struct {
    u64 application_id;
    Version required_application_version;
    u8 content_accessibility;               ///< ContentMetaContentAccessibility.
    u8 reserved[0x3];
    u64 data_patch_id;
} ContentMetaAddOnContentMetaExtendedHeader;

NXDT_ASSERT(ContentMetaAddOnContentMetaExtendedHeader, 0x18);

/// Old extended header for AddOnContent titles (1.0.0 - 14.1.2).
/// Equivalent to NcmLegacyAddOnContentMetaExtendedHeader, but using a Version struct.
typedef struct {
    u64 application_id;
    Version required_application_version;
    u8 reserved[0x4];
} ContentMetaLegacyAddOnContentMetaExtendedHeader;

NXDT_ASSERT(ContentMetaLegacyAddOnContentMetaExtendedHeader, 0x10);

/// Extended header for Delta titles.
typedef struct {
    u64 application_id;
    u32 extended_data_size;
    u8 reserved[0x4];
} ContentMetaDeltaMetaExtendedHeader;

NXDT_ASSERT(ContentMetaDeltaMetaExtendedHeader, 0x10);

/// Extended header for DataPatch titles.
/// Equivalent to NcmDataPatchMetaExtendedHeader, but using a Version struct.
typedef struct {
    u64 data_id;
    u64 application_id;
    Version required_application_version;
    u32 extended_data_size;
    u8 reserved[0x8];
} ContentMetaDataPatchMetaExtendedHeader;

NXDT_ASSERT(ContentMetaDataPatchMetaExtendedHeader, 0x20);

typedef enum {
    ContentMetaFirmwareVariationVersion_Invalid = 0,
    ContentMetaFirmwareVariationVersion_V1      = 1,
    ContentMetaFirmwareVariationVersion_V2      = 2,
    ContentMetaFirmwareVariationVersion_Unknown = 3
} ContentMetaFirmwareVariationVersion;

/// Header for the extended data region in the SystemUpdate title, pointed to by the extended header.
/// If version is ContentMetaFirmwareVariationVersion_V1, this is followed by 'variation_count' ContentMetaFirmwareVariationInfoV1 entries.
/// Otherwise, if version is ContentMetaFirmwareVariationVersion_V2, this is followed by:
///     * 'variation_count' firmware variation IDs (4 bytes each).
///     * 'variation_count' ContentMetaFirmwareVariationInfoV2 entries.
///     * (Optionally) A variable number of NcmContentMetaInfo entries, which is the sum of all 'meta_count' values from ContentMetaFirmwareVariationInfoV2 entries where 'refer_to_base' is set to false.
typedef struct {
    u32 version;            ///< ContentMetaFirmwareVariationVersion.
    u32 variation_count;    ///< Determines how many firmware variation entries are available after this header.
} ContentMetaSystemUpdateMetaExtendedDataHeader;

NXDT_ASSERT(ContentMetaSystemUpdateMetaExtendedDataHeader, 0x8);

/// Used if the firmware variation version matches ContentMetaFirmwareVariationVersion_V1.
typedef struct {
    u32 firmware_variation_id;
    u8 reserved[0x1C];
} ContentMetaFirmwareVariationInfoV1;

NXDT_ASSERT(ContentMetaFirmwareVariationInfoV1, 0x20);

/// Used if the firmware variation version matches ContentMetaFirmwareVariationVersion_V2.
typedef struct {
    bool refer_to_base;
    u8 reserved_1[0x3];
    u32 meta_count;
    u8 reserved_2[0x18];
} ContentMetaFirmwareVariationInfoV2;

NXDT_ASSERT(ContentMetaFirmwareVariationInfoV2, 0x20);

/// Header for the extended data region in Patch titles, pointed to by the extended header.
/// This is followed by:
///     * 'history_count' ContentMetaPatchHistoryHeader entries.
///     * 'delta_history_count' ContentMetaPatchDeltaHistory entries.
///     * 'delta_count' ContentMetaPatchDeltaHeader entries.
///     * 'fragment_set_count' ContentMetaFragmentSet entries.
///     * 'history_content_count' NcmContentInfo entries.
///     * 'delta_content_count' NcmPackagedContentInfo entries.
///     * A variable number of ContentMetaFragmentIndicator entries, which is the sum of all 'fragment_count' values from ContentMetaFragmentSet entries.
typedef struct {
    u32 history_count;
    u32 delta_history_count;
    u32 delta_count;
    u32 fragment_set_count;
    u32 history_content_count;
    u32 delta_content_count;
    u8 reserved[0x4];
} ContentMetaPatchMetaExtendedDataHeader;

NXDT_ASSERT(ContentMetaPatchMetaExtendedDataHeader, 0x1C);

typedef struct {
    NcmContentMetaKey content_meta_key;
    u8 digest[CNMT_DIGEST_SIZE];
    u16 content_info_count;
    u8 reserved[0x6];
} ContentMetaPatchHistoryHeader;

NXDT_ASSERT(ContentMetaPatchHistoryHeader, 0x38);

typedef struct {
    u64 source_patch_id;
    u64 destination_patch_id;
    Version source_version;
    Version destination_version;
    u64 download_size;
    u8 reserved[0x8];
} ContentMetaPatchDeltaHistory;

NXDT_ASSERT(ContentMetaPatchDeltaHistory, 0x28);

typedef struct {
    u64 source_patch_id;
    u64 destination_patch_id;
    Version source_version;
    Version destination_version;
    u16 fragment_set_count;
    u8 reserved_1[0x6];
    u16 content_info_count;
    u8 reserved_2[0x6];
} ContentMetaPatchDeltaHeader;

NXDT_ASSERT(ContentMetaPatchDeltaHeader, 0x28);

typedef enum {
    ContentMetaUpdateType_ApplyAsDelta = 0,
    ContentMetaUpdateType_Overwrite    = 1,
    ContentMetaUpdateType_Create       = 2
} ContentMetaUpdateType;

#pragma pack(push, 1)
typedef struct {
    NcmContentId source_content_id;
    NcmContentId destination_content_id;
    u32 source_size_low;
    u16 source_size_high;
    u32 destination_size_low;
    u16 destination_size_high;
    u16 fragment_count;
    u8 fragment_target_content_type;                ///< NcmContentType.
    u8 update_type;                                 ///< ContentMetaUpdateType.
    u8 reserved[0x4];
} ContentMetaFragmentSet;
#pragma pack(pop)

NXDT_ASSERT(ContentMetaFragmentSet, 0x34);

typedef struct {
    u16 content_info_index;
    u16 fragment_index;
} ContentMetaFragmentIndicator;

NXDT_ASSERT(ContentMetaFragmentIndicator, 0x4);

/// Header for the extended data region in Delta titles, pointed to by the extended header.
/// This is followed by:
///     * 'fragment_set_count' ContentMetaFragmentSet entries.
///     * A variable number of ContentMetaFragmentIndicator entries, which is the sum of all 'fragment_count' values from ContentMetaFragmentSet entries.
typedef struct {
    u64 source_patch_id;
    u64 destination_patch_id;
    Version source_version;
    Version destination_version;
    u16 fragment_set_count;
    u8 reserved[0x6];
} ContentMetaDeltaMetaExtendedDataHeader;

NXDT_ASSERT(ContentMetaDeltaMetaExtendedDataHeader, 0x20);

typedef struct {
    NcaContext *nca_ctx;                                    ///< Pointer to the NCA context for the Meta NCA from which CNMT data is retrieved.
    PartitionFileSystemContext pfs_ctx;                     ///< PartitionFileSystemContext for the Meta NCA FS section #0, which is where the CNMT is stored.
    PartitionFileSystemEntry *pfs_entry;                    ///< PartitionFileSystemEntry for the CNMT in the Meta NCA FS section #0. Used to generate a NcaHierarchicalSha256Patch if needed.
    NcaHierarchicalSha256Patch nca_patch;                   ///< NcaHierarchicalSha256Patch generated if CNMT modifications are needed. Used to seamlessly replace Meta NCA data while writing it.
                                                            ///< Bear in mind that generating a patch modifies the NCA context.
    char *cnmt_filename;                                    ///< Pointer to the CNMT filename in the Meta NCA FS section #0.
    u8 *raw_data;                                           ///< Pointer to a dynamically allocated buffer that holds the raw CNMT.
    u64 raw_data_size;                                      ///< Raw CNMT size. Kept here for convenience - this is part of 'pfs_entry'.
    u8 raw_data_hash[SHA256_HASH_SIZE];                     ///< SHA-256 checksum calculated over the whole raw CNMT. Used to determine if NcaHierarchicalSha256Patch generation is truly needed.
    ContentMetaPackagedContentMetaHeader *packaged_header;  ///< Pointer to the ContentMetaPackagedContentMetaHeader within 'raw_data'.
    u8 *extended_header;                                    ///< Pointer to the extended header within 'raw_data', if available. May be casted to other types. Its size is stored in 'packaged_header'.
    NcmPackagedContentInfo *packaged_content_info;          ///< Pointer to the NcmPackagedContentInfo entries within 'raw_data', if available. The content count is stored in 'packaged_header'.
    NcmContentMetaInfo *content_meta_info;                  ///< Pointer to the NcmContentMetaInfo entries within 'raw_data', if available. The content meta count is stored in 'packaged_header'.
    u8 *extended_data;                                      ///< Pointer to the extended data block within 'raw_data', if available.
    u32 extended_data_size;                                 ///< Size of the extended data block within 'raw_data', if available. Kept here for convenience - this is part of the header in 'extended_data'.
    u8 *digest;                                             ///< Pointer to the digest within 'raw_data'.
    char *authoring_tool_xml;                               ///< Pointer to a dynamically allocated, NULL-terminated buffer that holds AuthoringTool-like XML data.
                                                            ///< This is always NULL unless cnmtGenerateAuthoringToolXml() is used on this ContentMetaContext.
    u64 authoring_tool_xml_size;                            ///< Size for the AuthoringTool-like XML. This is essentially the same as using strlen() on 'authoring_tool_xml'.
                                                            ///< This is always 0 unless cnmtGenerateAuthoringToolXml() is used on this ContentMetaContext.
} ContentMetaContext;

/// Initializes a ContentMetaContext using a previously initialized NcaContext (which must belong to a Meta NCA).
bool cnmtInitializeContext(ContentMetaContext *out, NcaContext *nca_ctx);

/// Updates NcmPackagedContentInfo data for the content entry with size, type and ID offset values that match the ones from the input NcaContext.
bool cnmtUpdateContentInfo(ContentMetaContext *cnmt_ctx, NcaContext *nca_ctx);

/// Generates a Partition FS entry patch for the NcaContext pointed to by the input ContentMetaContext, using its raw CNMT data.
bool cnmtGenerateNcaPatch(ContentMetaContext *cnmt_ctx);

/// Writes data from the Partition FS entry patch in the input ContentMetaContext to the provided buffer.
void cnmtWriteNcaPatch(ContentMetaContext *cnmt_ctx, void *buf, u64 buf_size, u64 buf_offset);

/// Generates an AuthoringTool-like XML using information from a previously initialized ContentMetaContext, as well as a pointer to 'nca_ctx_count' NcaContext with content information.
/// If the function succeeds, XML data and size will get saved to the 'authoring_tool_xml' and 'authoring_tool_xml_size' members from the ContentMetaContext.
bool cnmtGenerateAuthoringToolXml(ContentMetaContext *cnmt_ctx, NcaContext *nca_ctx, u32 nca_ctx_count);

/// Helper inline functions.

NX_INLINE void cnmtFreeContext(ContentMetaContext *cnmt_ctx)
{
    if (!cnmt_ctx) return;
    pfsFreeContext(&(cnmt_ctx->pfs_ctx));
    pfsFreeEntryPatch(&(cnmt_ctx->nca_patch));
    if (cnmt_ctx->raw_data) free(cnmt_ctx->raw_data);
    if (cnmt_ctx->authoring_tool_xml) free(cnmt_ctx->authoring_tool_xml);
    memset(cnmt_ctx, 0, sizeof(ContentMetaContext));
}

NX_INLINE bool cnmtIsValidContext(ContentMetaContext *cnmt_ctx)
{
    return (cnmt_ctx && cnmt_ctx->nca_ctx && cnmt_ctx->pfs_entry && cnmt_ctx->cnmt_filename && cnmt_ctx->raw_data && cnmt_ctx->raw_data_size && cnmt_ctx->packaged_header && \
            ((cnmt_ctx->packaged_header->extended_header_size && cnmt_ctx->extended_header) || (!cnmt_ctx->packaged_header->extended_header_size && !cnmt_ctx->extended_header)) && \
            ((cnmt_ctx->packaged_header->content_count && cnmt_ctx->packaged_content_info) || (!cnmt_ctx->packaged_header->content_count && !cnmt_ctx->packaged_content_info)) && \
            ((cnmt_ctx->packaged_header->content_meta_count && cnmt_ctx->content_meta_info) || (!cnmt_ctx->packaged_header->content_meta_count && !cnmt_ctx->content_meta_info)) && \
            ((cnmt_ctx->extended_data_size && cnmt_ctx->extended_data) || (!cnmt_ctx->extended_data_size && !cnmt_ctx->extended_data)) && cnmt_ctx->digest);
}

NX_INLINE u64 cnmtGetRequiredTitleId(ContentMetaContext *cnmt_ctx)
{
    if (!cnmtIsValidContext(cnmt_ctx)) return 0;

    u8 content_meta_type = cnmt_ctx->packaged_header->content_meta_type;

    if (content_meta_type == NcmContentMetaType_Application || content_meta_type == NcmContentMetaType_Patch || content_meta_type == NcmContentMetaType_AddOnContent)
    {
        return *((u64*)cnmt_ctx->extended_header);
    } else
    if (content_meta_type == NcmContentMetaType_DataPatch)
    {
        return ((ContentMetaDataPatchMetaExtendedHeader*)cnmt_ctx->extended_header)->application_id;
    }

    return 0;
}

NX_INLINE u32 cnmtGetRequiredTitleVersion(ContentMetaContext *cnmt_ctx)
{
    if (!cnmtIsValidContext(cnmt_ctx)) return 0;

    u8 content_meta_type = cnmt_ctx->packaged_header->content_meta_type;

    if (content_meta_type == NcmContentMetaType_Application || content_meta_type == NcmContentMetaType_Patch || content_meta_type == NcmContentMetaType_AddOnContent)
    {
        return ((Version*)(cnmt_ctx->extended_header + sizeof(u64)))->value;
    } else
    if (content_meta_type == NcmContentMetaType_DataPatch)
    {
        return ((ContentMetaDataPatchMetaExtendedHeader*)cnmt_ctx->extended_header)->required_application_version.value;
    }

    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __CNMT_H__ */
