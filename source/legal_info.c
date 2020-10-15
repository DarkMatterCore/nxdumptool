/*
 * legal_info.c
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
#include "legal_info.h"
#include "romfs.h"

bool legalInfoInitializeContext(LegalInfoContext *out, NcaContext *nca_ctx)
{
    if (!out || !nca_ctx || !*(nca_ctx->content_id_str) || nca_ctx->content_type != NcmContentType_LegalInformation || nca_ctx->content_size < NCA_FULL_HEADER_LENGTH || \
        (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        nca_ctx->header.content_type != NcaContentType_Manual || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    RomFileSystemContext romfs_ctx = {0};
    RomFileSystemFileEntry *xml_entry = NULL;
    
    bool success = false;
    
    /* Free output context beforehand. */
    legalInfoFreeContext(out);
    
    /* Initialize RomFS context. */
    if (!romfsInitializeContext(&romfs_ctx, &(nca_ctx->fs_ctx[0])))
    {
        LOGFILE("Failed to initialize RomFS context!");
        goto end;
    }
    
    /* Retrieve RomFS file entry for 'legalinfo.xml'. */
    if (!(xml_entry = romfsGetFileEntryByPath(&romfs_ctx, "/legalinfo.xml")))
    {
        LOGFILE("Failed to retrieve file entry for \"legalinfo.xml\" from RomFS!");
        goto end;
    }
    
    //LOGFILE("Found 'legalinfo.xml' entry in LegalInformation NCA \"%s\".", nca_ctx->content_id_str);
    
    /* Verify XML size. */
    if (!xml_entry->size)
    {
        LOGFILE("Invalid XML size!");
        goto end;
    }
    
    /* Allocate memory for the XML. */
    out->authoring_tool_xml_size = xml_entry->size;
    if (!(out->authoring_tool_xml = malloc(out->authoring_tool_xml_size)))
    {
        LOGFILE("Failed to allocate memory for the XML!");
        goto end;
    }
    
    /* Read NACP data into memory buffer. */
    if (!romfsReadFileEntryData(&romfs_ctx, xml_entry, out->authoring_tool_xml, out->authoring_tool_xml_size, 0))
    {
        LOGFILE("Failed to read XML!");
        goto end;
    }
    
    /* Update NCA context pointer in output context. */
    out->nca_ctx = nca_ctx;
    
    success = true;
    
end:
    romfsFreeContext(&romfs_ctx);
    
    if (!success) legalInfoFreeContext(out);
    
    return success;
}
