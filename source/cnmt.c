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

/* Function prototypes. */

static bool cnmtGetContentMetaTypeAndTitleIdFromFileName(const char *cnmt_filename, size_t cnmt_filename_len, u8 *out_content_meta_type, u64 *out_title_id);

bool cnmtInitializeContext(ContentMetaContext *out, NcaContext *nca_ctx)
{
    if (!out || !nca_ctx || !strlen(nca_ctx->content_id_str) || nca_ctx->content_type != NcmContentType_Meta || nca_ctx->content_size < NCA_FULL_HEADER_LENGTH || \
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
    if (!pfsInitializeContext(&(out->pfs_ctx), &(nca_ctx->fs_contexts[0])))
    {
        LOGFILE("Failed to initialize Partition FS context!");
        goto end;
    }
    
    /* Get Partition FS entry count. */
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
    out->raw_data = malloc(out->raw_data_size);
    if (!out->raw_data)
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
    
    /* Save pointer to NCA context to the output CNMT context. */
    out->nca_ctx = nca_ctx;
    
    /* Verify packaged header. */
    out->packaged_header = (ContentMetaPackagedHeader*)out->raw_data;
    cur_offset += sizeof(ContentMetaPackagedHeader);
    
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
    
    if (!out->packaged_header->content_count)
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
    
    /* Save pointer to extended header */
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
    out->packaged_content_info = (NcmPackagedContentInfo*)(out->raw_data + cur_offset);
    cur_offset += (out->packaged_header->content_count * sizeof(NcmPackagedContentInfo));
    
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
        LOGFILE("Raw CNMT size mismatch! (0x%X != 0x%X).", cur_offset, out->raw_data_size);
        goto end;
    }
    
    success = true;
    
end:
    if (!success) cnmtFreeContext(out);
    
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
