/*
 * cnmt.c
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
        nca_ctx->header.content_type != NcaContentType_Meta || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 i = 0, pfs_entry_count = 0;
    size_t cnmt_filename_len = 0;
    
    u8 content_meta_type = 0;
    u64 title_id = 0, cur_offset = 0;
    
    bool success = false, invalid_ext_header_size = false, invalid_ext_data_size = false;
    
    /* Free output context beforehand. */
    cnmtFreeContext(out);
    
    /* Initialize Partition FS context. */
    if (!pfsInitializeContext(&(out->pfs_ctx), &(nca_ctx->fs_ctx[0])))
    {
        LOGFILE("Failed to initialize Partition FS context!");
        goto end;
    }
    
    /* Get Partition FS entry count. Edge case, we should never trigger this. */
    if (!(pfs_entry_count = pfsGetEntryCount(&(out->pfs_ctx))))
    {
        LOGFILE("Partition FS has no file entries!");
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
        LOGFILE("'.cnmt' entry unavailable in Partition FS!");
        goto end;
    }
    
    //LOGFILE("Found '.cnmt' entry \"%s\" in Meta NCA \"%s\".", out->cnmt_filename, nca_ctx->content_id_str);
    
    /* Retrieve content meta type and title ID from the '.cnmt' filename. */
    if (!cnmtGetContentMetaTypeAndTitleIdFromFileName(out->cnmt_filename, cnmt_filename_len, &content_meta_type, &title_id)) goto end;
    
    /* Get '.cnmt' file entry. */
    if (!(out->pfs_entry = pfsGetEntryByIndex(&(out->pfs_ctx), i)))
    {
        LOGFILE("Failed to get '.cnmt' entry from Partition FS!");
        goto end;
    }
    
    /* Check raw CNMT size. */
    if (!out->pfs_entry->size)
    {
        LOGFILE("Invalid raw CNMT size!");
        goto end;
    }
    
    /* Allocate memory for the raw CNMT data. */
    out->raw_data_size = out->pfs_entry->size;
    if (!(out->raw_data = malloc(out->raw_data_size)))
    {
        LOGFILE("Failed to allocate memory for the raw CNMT data!");
        goto end;
    }
    
    /* Read raw CNMT data into memory buffer. */
    if (!pfsReadEntryData(&(out->pfs_ctx), out->pfs_entry, out->raw_data, out->raw_data_size, 0))
    {
        LOGFILE("Failed to read raw CNMT data!");
        goto end;
    }
    
    /* Calculate SHA-256 checksum for the whole raw CNMT. */
    sha256CalculateHash(out->raw_data_hash, out->raw_data, out->raw_data_size);
    
    /* Save pointer to NCA context to the output CNMT context. */
    out->nca_ctx = nca_ctx;
    
    /* Verify packaged header. */
    out->packaged_header = (ContentMetaPackagedContentMetaHeader*)out->raw_data;
    cur_offset += sizeof(ContentMetaPackagedContentMetaHeader);
    
    if (out->packaged_header->title_id != title_id)
    {
        LOGFILE("CNMT title ID mismatch! (%016lX != %016lX).", out->packaged_header->title_id, title_id);
        goto end;
    }
    
    if (out->packaged_header->content_meta_type != content_meta_type)
    {
        LOGFILE("CNMT content meta type mismatch! (0x%02X != 0x%02X).", out->packaged_header->content_meta_type, content_meta_type);
        goto end;
    }
    
    if (!out->packaged_header->content_count && out->packaged_header->content_meta_type != NcmContentMetaType_SystemUpdate)
    {
        LOGFILE("Invalid content count!");
        goto end;
    }
    
    if ((out->packaged_header->content_meta_type == NcmContentMetaType_SystemUpdate && !out->packaged_header->content_meta_count) || \
        (out->packaged_header->content_meta_type != NcmContentMetaType_SystemUpdate && out->packaged_header->content_meta_count))
    {
        LOGFILE("Invalid content meta count!");
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
            LOGFILE("Invalid extended header size!");
            goto end;
        }
        
        if (invalid_ext_data_size)
        {
            LOGFILE("Invalid extended data size!");
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
        LOGFILE("Raw CNMT size mismatch! (0x%lX != 0x%lX).", cur_offset, out->raw_data_size);
        goto end;
    }
    
    success = true;
    
end:
    if (!success) cnmtFreeContext(out);
    
    return success;
}

bool cnmtGenerateAuthoringToolXml(ContentMetaContext *cnmt_ctx, NcaContext *nca_ctx, u32 nca_ctx_count)
{
    if (!cnmtIsValidContext(cnmt_ctx) || !nca_ctx || nca_ctx_count != ((u32)cnmt_ctx->packaged_header->content_count + 1))
    {
        LOGFILE("Invalid parameters!");
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
            LOGFILE("NCA \"%s\" isn't referenced by this CNMT!", cur_nca_ctx->content_id_str);
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
                                                ((VersionType1*)(cnmt_ctx->extended_header + sizeof(u64) + sizeof(u32)))->value)) goto end;
    }
    
    if (!(success = utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "</ContentMeta>"))) goto end;
    
    /* Update CNMT context. */
    cnmt_ctx->authoring_tool_xml = xml_buf;
    cnmt_ctx->authoring_tool_xml_size = strlen(xml_buf);
    
end:
    if (!success)
    {
        if (xml_buf) free(xml_buf);
        LOGFILE("Failed to generate CNMT AuthoringTool XML!");
    }
    
    return success;
}

static bool cnmtGetContentMetaTypeAndTitleIdFromFileName(const char *cnmt_filename, size_t cnmt_filename_len, u8 *out_content_meta_type, u64 *out_title_id)
{
    if (!cnmt_filename || cnmt_filename_len < CNMT_MINIMUM_FILENAME_LENGTH || !out_content_meta_type || !out_title_id)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u8 i = 0;
    const char *pch1 = NULL, *pch2 = NULL;
    size_t content_meta_type_str_len = 0;
    
    pch1 = (const char*)strstr(cnmt_filename, "_");
    pch2 = (cnmt_filename + cnmt_filename_len - 5);
    
    if (!pch1 || !(content_meta_type_str_len = (pch1 - cnmt_filename)) || (pch2 - ++pch1) != 16)
    {
        LOGFILE("Invalid '.cnmt' filename in Partition FS!");
        return false;
    }
    
    for(i = NcmContentMetaType_SystemProgram; i <= NcmContentMetaType_Delta; i++)
    {
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
        LOGFILE("Invalid content meta type \"%.*s\" in '.cnmt' filename!", (int)content_meta_type_str_len, cnmt_filename);
        return false;
    }
    
    if (!(*out_title_id = strtoull(pch1, NULL, 16)))
    {
        LOGFILE("Invalid title ID in '.cnmt' filename!");
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
