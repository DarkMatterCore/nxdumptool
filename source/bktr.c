/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "bktr.h"
#include "utils.h"

/* Function prototypes. */




bool bktrInitializeContext(BktrContext *out, NcaFsSectionContext *base_nca_fs_ctx, NcaFsSectionContext *update_nca_fs_ctx)
{
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    
    if (!out || !base_nca_fs_ctx || !(base_nca_ctx = (NcaContext*)base_nca_fs_ctx->nca_ctx) || base_nca_fs_ctx->section_type != NcaFsSectionType_RomFs || \
        base_nca_fs_ctx->encryption_type == NcaEncryptionType_AesCtrEx || !update_nca_fs_ctx || !(update_nca_ctx = (NcaContext*)update_nca_fs_ctx->nca_ctx) || \
        update_nca_fs_ctx->section_type != NcaFsSectionType_PatchRomFs || update_nca_fs_ctx->encryption_type != NcaEncryptionType_AesCtrEx || 
        base_nca_ctx->header.program_id != update_nca_ctx->header.program_id)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Initialize base NCA RomFS context */
    if (!romfsInitializeContext(&(out->base_romfs_ctx), base_nca_fs_ctx))
    {
        LOGFILE("Failed to initialize base NCA RomFS context!");
        return false;
    }
    
    /* Initialize update NCA RomFS context */
    if (!romfsInitializeContext(&(out->patch_romfs_ctx), update_nca_fs_ctx))
    {
        LOGFILE("Failed to initialize update NCA RomFS context!");
        romfsFreeContext(&(out->base_romfs_ctx));
        return false;
    }
    
    /* Fill context */
    bool success = false;
    out->patch_info = &(update_nca_fs_ctx->header->patch_info);
    out->size = out->patch_romfs_ctx.size;
    out->virtual_seek = out->base_seek = out->patch_seek = 0;
    
    
    
    
    
    
    
    success = true;
    
exit:
    if (!success) bktrFreeContext(out);
    
    return success;
}




