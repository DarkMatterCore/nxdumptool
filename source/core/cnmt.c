/*
 * cnmt.c
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

#include "utils.h"
#include "cnmt.h"
#include "title.h"

#define CNMT_MINIMUM_FILENAME_LENGTH    23  /* Content Meta Type + "_" + Title ID + ".cnmt". */

/* Global variables. */

static const char *g_cnmtAttributeStrings[ContentMetaAttribute_Count] = {
    "IncludesExFatDriver",
    "Rebootless",
    "Compacted"
};

/* Function prototypes. */

static bool cnmtGetContentMetaTypeAndTitleIdFromFileName(const char *cnmt_filename, size_t cnmt_filename_len, u8 *out_content_meta_type, u64 *out_title_id);

static const char *cnmtGetRequiredTitleVersionString(u8 content_meta_type);
static const char *cnmtGetRequiredTitleTypeString(u8 content_meta_type);

bool cnmtInitializeContext(ContentMetaContext *out, NcaContext *nca_ctx)
{
    if (!out || !nca_ctx || !*(nca_ctx->content_id_str) || nca_ctx->content_type != NcmContentType_Meta || nca_ctx->content_size < NCA_FULL_HEADER_LENGTH || \
        (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        nca_ctx->header.content_type != NcaContentType_Meta || nca_ctx->content_type_ctx || !out)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    u32 i = 0, pfs_entry_count = 0;
    size_t cnmt_filename_len = 0;
    
    u8 content_meta_type = 0;
    u64 title_id = 0, cur_offset = 0;
    
    bool success = false, invalid_ext_header_size = false, invalid_ext_data_size = false, dump_packaged_header = false;
    
    /* Free output context beforehand. */
    cnmtFreeContext(out);
    
    /* Initialize Partition FS context. */
    if (!pfsInitializeContext(&(out->pfs_ctx), &(nca_ctx->fs_ctx[0])))
    {
        LOG_MSG("Failed to initialize Partition FS context!");
        goto end;
    }
    
    /* Get Partition FS entry count. Edge case, we should never trigger this. */
    if (!(pfs_entry_count = pfsGetEntryCount(&(out->pfs_ctx))))
    {
        LOG_MSG("Partition FS has no file entries!");
        goto end;
    }
    
    /* Look for the '.cnmt' file entry index. */
    for(i = 0; i < pfs_entry_count; i++)
    {
        if ((out->cnmt_filename = pfsGetEntryNameByIndex(&(out->pfs_ctx), i)) && (cnmt_filename_len = strlen(out->cnmt_filename)) >= CNMT_MINIMUM_FILENAME_LENGTH && \
            !strncasecmp(out->cnmt_filename + cnmt_filename_len - 5, ".cnmt", 5)) break;
    }
    
    if (i >= pfs_entry_count)
    {
        LOG_MSG("'.cnmt' entry unavailable in Partition FS!");
        goto end;
    }
    
    //LOG_MSG("Found '.cnmt' entry \"%s\" in Meta NCA \"%s\".", out->cnmt_filename, nca_ctx->content_id_str);
    
    /* Retrieve content meta type and title ID from the '.cnmt' filename. */
    if (!cnmtGetContentMetaTypeAndTitleIdFromFileName(out->cnmt_filename, cnmt_filename_len, &content_meta_type, &title_id)) goto end;
    
    /* Get '.cnmt' file entry. */
    if (!(out->pfs_entry = pfsGetEntryByIndex(&(out->pfs_ctx), i)))
    {
        LOG_MSG("Failed to get '.cnmt' entry from Partition FS!");
        goto end;
    }
    
    /* Check raw CNMT size. */
    if (!out->pfs_entry->size)
    {
        LOG_DATA(out->pfs_entry, sizeof(PartitionFileSystemEntry), "Invalid raw CNMT size! Partition FS entry dump:");
        goto end;
    }
    
    /* Allocate memory for the raw CNMT data. */
    out->raw_data_size = out->pfs_entry->size;
    if (!(out->raw_data = malloc(out->raw_data_size)))
    {
        LOG_MSG("Failed to allocate memory for the raw CNMT data!");
        goto end;
    }
    
    /* Read raw CNMT data into memory buffer. */
    if (!pfsReadEntryData(&(out->pfs_ctx), out->pfs_entry, out->raw_data, out->raw_data_size, 0))
    {
        LOG_MSG("Failed to read raw CNMT data!");
        goto end;
    }
    
    /* Calculate SHA-256 checksum for the whole raw CNMT. */
    sha256CalculateHash(out->raw_data_hash, out->raw_data, out->raw_data_size);
    
    /* Verify packaged header. */
    out->packaged_header = (ContentMetaPackagedContentMetaHeader*)out->raw_data;
    cur_offset += sizeof(ContentMetaPackagedContentMetaHeader);
    
    if (out->packaged_header->title_id != title_id)
    {
        LOG_MSG("CNMT title ID mismatch! (%016lX != %016lX).", out->packaged_header->title_id, title_id);
        dump_packaged_header = true;
        goto end;
    }
    
    if (out->packaged_header->content_meta_type != content_meta_type)
    {
        LOG_MSG("CNMT content meta type mismatch! (0x%02X != 0x%02X).", out->packaged_header->content_meta_type, content_meta_type);
        dump_packaged_header = true;
        goto end;
    }
    
    if (!out->packaged_header->content_count && out->packaged_header->content_meta_type != NcmContentMetaType_SystemUpdate)
    {
        LOG_MSG("Invalid content count!");
        dump_packaged_header = true;
        goto end;
    }
    
    if ((out->packaged_header->content_meta_type == NcmContentMetaType_SystemUpdate && !out->packaged_header->content_meta_count) || \
        (out->packaged_header->content_meta_type != NcmContentMetaType_SystemUpdate && out->packaged_header->content_meta_count))
    {
        LOG_MSG("Invalid content meta count!");
        dump_packaged_header = true;
        goto end;
    }
    
    /* Save pointer to extended header. */
    if (out->packaged_header->extended_header_size)
    {
        out->extended_header = (out->raw_data + cur_offset);
        cur_offset += out->packaged_header->extended_header_size;
        
        switch(out->packaged_header->content_meta_type)
        {
            case NcmContentMetaType_SystemUpdate:
                invalid_ext_header_size = (out->packaged_header->extended_header_size != (u16)sizeof(ContentMetaSystemUpdateMetaExtendedHeader));
                out->extended_data_size = (!invalid_ext_header_size ? ((ContentMetaSystemUpdateMetaExtendedHeader*)out->extended_header)->extended_data_size : 0);
                invalid_ext_data_size = (out->extended_data_size <= sizeof(ContentMetaSystemUpdateMetaExtendedDataHeader));
                break;
            case NcmContentMetaType_Application:
                invalid_ext_header_size = (out->packaged_header->extended_header_size != (u16)sizeof(ContentMetaApplicationMetaExtendedHeader));
                break;
            case NcmContentMetaType_Patch:
                invalid_ext_header_size = (out->packaged_header->extended_header_size != (u16)sizeof(ContentMetaPatchMetaExtendedHeader));
                out->extended_data_size = (!invalid_ext_header_size ? ((ContentMetaPatchMetaExtendedHeader*)out->extended_header)->extended_data_size : 0);
                invalid_ext_data_size = (out->extended_data_size <= sizeof(ContentMetaPatchMetaExtendedDataHeader));
                break;
            case NcmContentMetaType_AddOnContent:
                invalid_ext_header_size = (out->packaged_header->extended_header_size != (u16)sizeof(ContentMetaAddOnContentMetaExtendedHeader));
                break;
            case NcmContentMetaType_Delta:
                invalid_ext_header_size = (out->packaged_header->extended_header_size != (u16)sizeof(ContentMetaDeltaMetaExtendedHeader));
                out->extended_data_size = (!invalid_ext_header_size ? ((ContentMetaDeltaMetaExtendedHeader*)out->extended_header)->extended_data_size : 0);
                invalid_ext_data_size = (out->extended_data_size <= sizeof(ContentMetaDeltaMetaExtendedDataHeader));
                break;
            default:
                invalid_ext_header_size = (out->packaged_header->extended_header_size > 0);
                break;
        }
        
        if (invalid_ext_header_size)
        {
            LOG_MSG("Invalid extended header size!");
            dump_packaged_header = true;
            goto end;
        }
        
        if (invalid_ext_data_size)
        {
            LOG_DATA(out->extended_header, out->packaged_header->extended_header_size, "Invalid extended data size! CNMT Extended Header dump:");
            dump_packaged_header = true;
            goto end;
        }
    }
    
    /* Save pointer to packaged content infos. */
    if (out->packaged_header->content_count)
    {
        out->packaged_content_info = (NcmPackagedContentInfo*)(out->raw_data + cur_offset);
        cur_offset += (out->packaged_header->content_count * sizeof(NcmPackagedContentInfo));
    }
    
    /* Save pointer to content meta infos. */
    if (out->packaged_header->content_meta_count)
    {
        out->content_meta_info = (NcmContentMetaInfo*)(out->raw_data + cur_offset);
        cur_offset += (out->packaged_header->content_meta_count * sizeof(NcmContentMetaInfo));
    }
    
    /* Save pointer to the extended data block. */
    if (out->extended_data_size)
    {
        out->extended_data = (out->raw_data + cur_offset);
        cur_offset += out->extended_data_size;
    }
    
    /* Save pointer to digest. */
    out->digest = (out->raw_data + cur_offset);
    cur_offset += CNMT_DIGEST_SIZE;
    
    /* Safety check: verify raw CNMT size. */
    if (cur_offset != out->raw_data_size)
    {
        LOG_MSG("Raw CNMT size mismatch! (0x%lX != 0x%lX).", cur_offset, out->raw_data_size);
        goto end;
    }
    
    /* Update output context. */
    out->nca_ctx = nca_ctx;
    
    /* Update content type context info in NCA context. */
    nca_ctx->content_type_ctx = out;
    nca_ctx->content_type_ctx_patch = false;
    
    success = true;
    
end:
    if (!success)
    {
        if (dump_packaged_header) LOG_DATA(out->packaged_header, sizeof(ContentMetaPackagedContentMetaHeader), "CNMT Packaged Header dump:");
        
        cnmtFreeContext(out);
    }
    
    return success;
}

bool cnmtUpdateContentInfo(ContentMetaContext *cnmt_ctx, NcaContext *nca_ctx)
{
    if (!cnmtIsValidContext(cnmt_ctx) || !nca_ctx || !*(nca_ctx->content_id_str) || !*(nca_ctx->hash_str) || nca_ctx->content_type > NcmContentType_DeltaFragment || !nca_ctx->content_size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Return right away if we're dealing with a Meta NCA. */
    if (nca_ctx->content_type == NcmContentType_Meta) return true;
    
    bool success = false;
    
    for(u16 i = 0; i < cnmt_ctx->packaged_header->content_count; i++)
    {
        NcmPackagedContentInfo *packaged_content_info = &(cnmt_ctx->packaged_content_info[i]);
        NcmContentInfo *content_info = &(packaged_content_info->info);
        u64 content_size = 0;
        
        titleConvertNcmContentSizeToU64(content_info->size, &content_size);
        
        if (content_size == nca_ctx->content_size && content_info->content_type == nca_ctx->content_type && content_info->id_offset == nca_ctx->id_offset)
        {
            /* Jackpot. Copy content ID and hash to our raw CNMT. */
            memcpy(packaged_content_info->hash, nca_ctx->hash, sizeof(nca_ctx->hash));
            memcpy(&(content_info->content_id), &(nca_ctx->content_id), sizeof(NcmContentId));
            LOG_MSG("Updated CNMT content record #%u (Title ID %016lX, size 0x%lX, type 0x%02X, ID offset 0x%02X).", i, cnmt_ctx->packaged_header->title_id, content_size, content_info->content_type, \
                    content_info->id_offset);
            success = true;
            break;
        }
    }
    
    if (!success) LOG_MSG("Unable to find CNMT content info entry for \"%s\" NCA! (Title ID %016lX, size 0x%lX, type 0x%02X, ID offset 0x%02X).", nca_ctx->content_id_str, \
                          cnmt_ctx->packaged_header->title_id, nca_ctx->content_size, nca_ctx->content_type, nca_ctx->id_offset);
    
    return success;
}

bool cnmtGenerateNcaPatch(ContentMetaContext *cnmt_ctx)
{
    if (!cnmtIsValidContext(cnmt_ctx))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    /* Check if we really need to generate this patch. */
    u8 cnmt_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(cnmt_hash, cnmt_ctx->raw_data, cnmt_ctx->raw_data_size);
    if (!memcmp(cnmt_hash, cnmt_ctx->raw_data_hash, sizeof(cnmt_hash)))
    {
        LOG_MSG("Skipping CNMT patching - no content records have been changed.");
        return true;
    }
    
    /* Generate Partition FS entry patch. */
    if (!pfsGenerateEntryPatch(&(cnmt_ctx->pfs_ctx), cnmt_ctx->pfs_entry, cnmt_ctx->raw_data, cnmt_ctx->raw_data_size, 0, &(cnmt_ctx->nca_patch)))
    {
        LOG_MSG("Failed to generate Partition FS entry patch!");
        return false;
    }
    
    /* Update NCA content type context patch status. */
    cnmt_ctx->nca_ctx->content_type_ctx_patch = true;
    
    return true;
}

void cnmtWriteNcaPatch(ContentMetaContext *cnmt_ctx, void *buf, u64 buf_size, u64 buf_offset)
{
    NcaContext *nca_ctx = NULL;
    NcaHierarchicalSha256Patch *nca_patch = (cnmt_ctx ? &(cnmt_ctx->nca_patch) : NULL);
    
    /* Using cnmtIsValidContext() here would probably take up precious CPU cycles. */
    if (!nca_patch || nca_patch->written || !(nca_ctx = cnmt_ctx->nca_ctx) || nca_ctx->content_type != NcmContentType_Meta || !nca_ctx->content_type_ctx_patch) return;
    
    /* Attempt to write Partition FS entry patch. */
    pfsWriteEntryPatchToMemoryBuffer(&(cnmt_ctx->pfs_ctx), nca_patch, buf, buf_size, buf_offset);
    
    /* Check if we need to update the NCA content type context patch status. */
    if (nca_patch->written)
    {
        nca_ctx->content_type_ctx_patch = false;
        LOG_MSG("CNMT Partition FS entry patch successfully written to NCA \"%s\"!", nca_ctx->content_id_str);
    }
}

bool cnmtGenerateAuthoringToolXml(ContentMetaContext *cnmt_ctx, NcaContext *nca_ctx, u32 nca_ctx_count)
{
    if (!cnmtIsValidContext(cnmt_ctx) || !nca_ctx || !nca_ctx_count || nca_ctx_count > ((u32)cnmt_ctx->packaged_header->content_count + 1))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    u32 i, j;
    char *xml_buf = NULL;
    u64 xml_buf_size = 0;
    char digest_str[0x41] = {0};
    u8 count = 0;
    bool success = false, invalid_nca = false;
    
    /* Free AuthoringTool-like XML data if needed. */
    if (cnmt_ctx->authoring_tool_xml) free(cnmt_ctx->authoring_tool_xml);
    cnmt_ctx->authoring_tool_xml = NULL;
    cnmt_ctx->authoring_tool_xml_size = 0;
    
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
                                            "<ContentMeta>\n" \
                                            "  <Type>%s</Type>\n" \
                                            "  <Id>0x%016lx</Id>\n" \
                                            "  <Version>%u</Version>\n" \
                                            "  <ReleaseVersion />\n" \
                                            "  <PrivateVersion />\n",
                                            titleGetNcmContentMetaTypeName(cnmt_ctx->packaged_header->content_meta_type), \
                                            cnmt_ctx->packaged_header->title_id, \
                                            cnmt_ctx->packaged_header->version.value)) goto end;
    
    /* ContentMetaAttribute. */
    for(i = 0; i < ContentMetaAttribute_Count; i++)
    {
        if (!(cnmt_ctx->packaged_header->content_meta_attribute & (u8)BIT(i))) continue;
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <ContentMetaAttribute>%s</ContentMetaAttribute>\n", g_cnmtAttributeStrings[i])) goto end;
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <ContentMetaAttribute />\n")) goto end;
    
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <RequiredDownloadSystemVersion>%u</RequiredDownloadSystemVersion>\n", \
                                            cnmt_ctx->packaged_header->required_download_system_version.value)) goto end;
    
    for(i = 0; i < nca_ctx_count; i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        
        /* Check if this NCA is really referenced by our CNMT. */
        if (cur_nca_ctx->content_type != NcmContentType_Meta)
        {
            /* Non-Meta NCAs: check if their content IDs are part of the packaged content info entries from the CNMT. */
            for(j = 0; j < cnmt_ctx->packaged_header->content_count; j++)
            {
                if (!memcmp(cnmt_ctx->packaged_content_info[j].info.content_id.c, cur_nca_ctx->content_id.c, 0x10)) break;
            }
            
            invalid_nca = (j >= cnmt_ctx->packaged_header->content_count);
        } else {
            /* Meta NCAs: quick and dirty pointer comparison because why not. */
            invalid_nca = (cnmt_ctx->nca_ctx != cur_nca_ctx);
        }
        
        if (invalid_nca)
        {
            LOG_MSG("NCA \"%s\" isn't referenced by this CNMT!", cur_nca_ctx->content_id_str);
            goto end;
        }
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <Content>\n" \
                                                "    <Type>%s</Type>\n" \
                                                "    <Id>%s</Id>\n" \
                                                "    <Size>%lu</Size>\n" \
                                                "    <Hash>%s</Hash>\n" \
                                                "    <KeyGeneration>%u</KeyGeneration>\n" \
                                                "    <IdOffset>%u</IdOffset>\n" \
                                                "  </Content>\n", \
                                                titleGetNcmContentTypeName(cur_nca_ctx->content_type), \
                                                cur_nca_ctx->content_id_str, \
                                                cur_nca_ctx->content_size, \
                                                cur_nca_ctx->hash_str, \
                                                cur_nca_ctx->key_generation, \
                                                cur_nca_ctx->id_offset)) goto end;
    }
    
    utilsGenerateHexStringFromData(digest_str, sizeof(digest_str), cnmt_ctx->digest, CNMT_DIGEST_SIZE);
    
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "  <ContentMeta />\n" \
                                            "  <Digest>%s</Digest>\n" \
                                            "  <KeyGenerationMin>%u</KeyGenerationMin>\n" \
                                            "  <KeepGeneration />\n" \
                                            "  <KeepGenerationSpecified />\n", \
                                            digest_str, \
                                            cnmt_ctx->nca_ctx->key_generation)) goto end;
    
    if (cnmt_ctx->packaged_header->content_meta_type == NcmContentMetaType_Application || cnmt_ctx->packaged_header->content_meta_type == NcmContentMetaType_Patch || \
        cnmt_ctx->packaged_header->content_meta_type == NcmContentMetaType_AddOnContent)
    {
        u32 required_title_version = cnmtGetRequiredTitleVersion(cnmt_ctx);
        const char *required_title_version_str = cnmtGetRequiredTitleVersionString(cnmt_ctx->packaged_header->content_meta_type);
        
        u64 required_title_id = cnmtGetRequiredTitleId(cnmt_ctx);
        const char *required_title_type_str = cnmtGetRequiredTitleTypeString(cnmt_ctx->packaged_header->content_meta_type);
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <%s>%u</%s>\n" \
                                                "  <%s>0x%016lx</%s>\n", \
                                                required_title_version_str, required_title_version, required_title_version_str, \
                                                required_title_type_str, required_title_id, required_title_type_str)) goto end;
    }
    
    if (cnmt_ctx->packaged_header->content_meta_type == NcmContentMetaType_Application)
    {
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <RequiredApplicationVersion>%u</RequiredApplicationVersion>\n", \
                                                ((ContentMetaApplicationMetaExtendedHeader*)cnmt_ctx->extended_header)->required_application_version.value)) goto end;
    }
    
    if (!(success = utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "</ContentMeta>"))) goto end;
    
    /* Update CNMT context. */
    cnmt_ctx->authoring_tool_xml = xml_buf;
    cnmt_ctx->authoring_tool_xml_size = strlen(xml_buf);
    
end:
    if (!success)
    {
        if (xml_buf) free(xml_buf);
        LOG_MSG("Failed to generate CNMT AuthoringTool XML!");
    }
    
    return success;
}

static bool cnmtGetContentMetaTypeAndTitleIdFromFileName(const char *cnmt_filename, size_t cnmt_filename_len, u8 *out_content_meta_type, u64 *out_title_id)
{
    if (!cnmt_filename || cnmt_filename_len < CNMT_MINIMUM_FILENAME_LENGTH || !out_content_meta_type || !out_title_id)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    u8 i = 0;
    const char *pch1 = NULL, *pch2 = NULL;
    size_t content_meta_type_str_len = 0;
    
    pch1 = (const char*)strstr(cnmt_filename, "_");
    pch2 = (cnmt_filename + cnmt_filename_len - 5);
    
    if (!pch1 || !(content_meta_type_str_len = (pch1 - cnmt_filename)) || (pch2 - ++pch1) != 16)
    {
        LOG_MSG("Invalid '.cnmt' filename in Partition FS! (\"%s\").", cnmt_filename);
        return false;
    }
    
    for(i = NcmContentMetaType_SystemProgram; i <= NcmContentMetaType_Delta; i++)
    {
        /* Dirty loop hack, but whatever. */
        if (i > NcmContentMetaType_BootImagePackageSafe && i < NcmContentMetaType_Application)
        {
            i = (NcmContentMetaType_Application - 1);
            continue;
        }
        
        if (!strncasecmp(cnmt_filename, titleGetNcmContentMetaTypeName(i), content_meta_type_str_len))
        {
            *out_content_meta_type = i;
            break;
        }
    }
    
    if (i > NcmContentMetaType_Delta)
    {
        LOG_MSG("Invalid content meta type \"%.*s\" in '.cnmt' filename! (\"%s\").", (int)content_meta_type_str_len, cnmt_filename, cnmt_filename);
        return false;
    }
    
    if (!(*out_title_id = strtoull(pch1, NULL, 16)))
    {
        LOG_MSG("Invalid title ID in '.cnmt' filename! (\"%s\").", cnmt_filename);
        return false;
    }
    
    return true;
}

static const char *cnmtGetRequiredTitleVersionString(u8 content_meta_type)
{
    const char *str = NULL;
    
    switch(content_meta_type)
    {
        case NcmContentMetaType_Application:
        case NcmContentMetaType_Patch:
            str = "RequiredSystemVersion";
            break;
        case NcmContentMetaType_AddOnContent:
            str = "RequiredApplicationVersion";
            break;
        default:
            str = "Unknown";
            break;
    }
    
    return str;
}

static const char *cnmtGetRequiredTitleTypeString(u8 content_meta_type)
{
    const char *str = NULL;
    
    switch(content_meta_type)
    {
        case NcmContentMetaType_Application:
            str = "PatchId";
            break;
        case NcmContentMetaType_Patch:
        case NcmContentMetaType_AddOnContent:
            str = "ApplicationId";
            break;
        default:
            str = "Unknown";
            break;
    }
    
    return str;
}
