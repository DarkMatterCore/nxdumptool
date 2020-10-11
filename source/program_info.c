/*
 * program_info.c
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

#include <mbedtls/base64.h>

#include "utils.h"
#include "program_info.h"
#include "elf_symbol.h"

bool programInfoInitializeContext(ProgramInfoContext *out, NcaContext *nca_ctx)
{
    if (!out || !nca_ctx || !strlen(nca_ctx->content_id_str) || nca_ctx->content_type != NcmContentType_Program || nca_ctx->content_size < NCA_FULL_HEADER_LENGTH || \
        (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        nca_ctx->header.content_type != NcaContentType_Program || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 i = 0, pfs_entry_count = 0, magic = 0;
    NsoContext *tmp_nso_ctx = NULL;
    
    bool success = false;
    
    /* Free output context beforehand. */
    programInfoFreeContext(out);
    
    /* Initialize Partition FS context. */
    if (!pfsInitializeContext(&(out->pfs_ctx), &(nca_ctx->fs_contexts[0])))
    {
        LOGFILE("Failed to initialize Partition FS context!");
        goto end;
    }
    
    /* Check if we're indeed dealing with an ExeFS. */
    if (!out->pfs_ctx.is_exefs)
    {
        LOGFILE("Initialized Partition FS is not an ExeFS!");
        goto end;
    }
    
    /* Get ExeFS entry count. Edge case, we should never trigger this. */
    if (!(pfs_entry_count = pfsGetEntryCount(&(out->pfs_ctx))))
    {
        LOGFILE("ExeFS has no file entries!");
        goto end;
    }
    
    /* Initialize NPDM context. */
    if (!npdmInitializeContext(&(out->npdm_ctx), &(out->pfs_ctx)))
    {
        LOGFILE("Failed to initialize NPDM context!");
        goto end;
    }
    
    /* Initialize NSO contexts. */
    for(i = 0; i < pfs_entry_count; i++)
    {
        /* Skip the main.npdm entry, as well as any other entries without a NSO header. */
        PartitionFileSystemEntry *pfs_entry = pfsGetEntryByIndex(&(out->pfs_ctx), i);
        char *pfs_entry_name = pfsGetEntryName(&(out->pfs_ctx), pfs_entry);
        if (!pfs_entry || !pfs_entry_name || !strncmp(pfs_entry_name, "main.npdm", 9) || !pfsReadEntryData(&(out->pfs_ctx), pfs_entry, &magic, sizeof(u32), 0) || \
            __builtin_bswap32(magic) != NSO_HEADER_MAGIC) continue;
        
        /* Reallocate NSO context buffer. */
        if (!(tmp_nso_ctx = realloc(out->nso_ctx, (out->nso_count + 1) * sizeof(NsoContext))))
        {
            LOGFILE("Failed to reallocate NSO context buffer for NSO \"%s\"! (entry #%u).", pfs_entry_name, i);
            goto end;
        }
        
        out->nso_ctx = tmp_nso_ctx;
        tmp_nso_ctx = NULL;
        
        memset(&(out->nso_ctx[out->nso_count]), 0, sizeof(NsoContext));
        
        /* Initialize NSO context. */
        if (!nsoInitializeContext(&(out->nso_ctx[out->nso_count]), &(out->pfs_ctx), pfs_entry))
        {
            LOGFILE("Failed to initialize context for NSO \"%s\"! (entry #%u).", pfs_entry_name, i);
            goto end;
        }
        
        /* Update NSO count. */
        out->nso_count++;
    }
    
    success = true;
    
end:
    if (!success) programInfoFreeContext(out);
    
    return success;
}
