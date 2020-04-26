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
#include <stdlib.h>
#include <string.h>
#include <switch.h>

//#include "lvgl_helper.h"
#include "utils.h"


#include <dirent.h>

#include "nca.h"
#include "pfs.h"
#include "rsa.h"



int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    int ret = 0;
    
    LOGFILE("nxdumptool starting.");
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    /*lv_test();
    
    while(appletMainLoop())
    {
        lv_task_handler();
        if (lvglHelperGetExitFlag()) break;
    }*/
    
    consoleInit(NULL);
    
    printf("initializing...\n");
    consoleUpdate(NULL);
    
    u8 *buf = NULL;
    FILE *tmp_file = NULL;
    
    Ticket tik = {0};
    NcaContext *nca_ctx = NULL;
    NcmContentStorage ncm_storage = {0};
    
    Result rc = 0;
    
    mkdir("sdmc:/nxdt_test", 0744);
    
    /*FsRightsId rights_id = {
        .c = { 0x01, 0x00, 0x82, 0x40, 0x0B, 0xCC, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 } // Untitled Goose Game
    };*/
    
    // Untitled Goose Game
    NcmPackagedContentInfo content_info = {
        .hash = {
            0x8E, 0xF9, 0x20, 0xD4, 0x5E, 0xE1, 0x9E, 0xD1, 0xD2, 0x04, 0xC4, 0xC8, 0x22, 0x50, 0x79, 0xE8,
            0x8E, 0xF9, 0x20, 0xD4, 0x5E, 0xE1, 0x9E, 0xD1, 0xD2, 0x04, 0xC4, 0xC8, 0x22, 0x50, 0x79, 0xE8
        },
        .info = {
            .content_id = {
                .c = { 0x8E, 0xF9, 0x20, 0xD4, 0x5E, 0xE1, 0x9E, 0xD1, 0xD2, 0x04, 0xC4, 0xC8, 0x22, 0x50, 0x79, 0xE8 }
            },
            .size = {
                0x00, 0x40, 0xAD, 0x31, 0x00, 0x00
            },
            .content_type = NcmContentType_Program,
            .id_offset = 0
        }
    };
    
    u64 pfs_size = 0;
    PartitionFileSystemContext pfs_ctx = {0};
    PartitionFileSystemEntry *pfs_entry = NULL;
    
    buf = malloc(0x400000);
    if (!buf)
    {
        printf("read buf failed\n");
        goto out2;
    }
    
    printf("read buf succeeded\n");
    consoleUpdate(NULL);
    
    nca_ctx = calloc(1, sizeof(NcaContext));
    if (!nca_ctx)
    {
        printf("nca ctx buf failed\n");
        goto out2;
    }
    
    printf("nca ctx buf succeeded\n");
    consoleUpdate(NULL);
    
    rc = ncmOpenContentStorage(&ncm_storage, NcmStorageId_SdCard);
    if (R_FAILED(rc))
    {
        printf("ncm open storage failed\n");
        goto out2;
    }
    
    printf("ncm open storage succeeded\n");
    consoleUpdate(NULL);
    
    if (!ncaInitializeContext(nca_ctx, NcmStorageId_SdCard, &ncm_storage, 0, &content_info, &tik))
    {
        printf("nca initialize ctx failed\n");
        goto out2;
    }
    
    tmp_file = fopen("sdmc:/nxdt_test/nca_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(nca_ctx, 1, sizeof(NcaContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("nca ctx saved\n");
    } else {
        printf("nca ctx not saved\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/section0.bin", "wb");
    if (tmp_file)
    {
        u64 blksize = 0x400000;
        u64 total = nca_ctx->fs_contexts[0].section_size;
        
        printf("nca section0 created: 0x%lX\n", total);
        consoleUpdate(NULL);
        
        for(u64 curpos = 0; curpos < total; curpos += blksize)
        {
            if (blksize > (total - curpos)) blksize = (total - curpos);
            
            if (!ncaReadFsSection(&(nca_ctx->fs_contexts[0]), buf, blksize, curpos))
            {
                printf("nca read section failed\n");
                goto out2;
            }
            
            fwrite(buf, 1, blksize, tmp_file);
        }
        
        fclose(tmp_file);
        tmp_file = NULL;
        
        printf("nca read section0 success\n");
    } else {
        printf("nca section0 not created\n");
    }
    
    consoleUpdate(NULL);
    
    if (!pfsInitializeContext(&pfs_ctx, &(nca_ctx->fs_contexts[0])))
    {
        printf("pfs initialize ctx failed\n");
        goto out2;
    }
    
    printf("pfs initialize ctx succeeded\n");
    consoleUpdate(NULL);
    
    if (pfsGetTotalDataSize(&pfs_ctx, &pfs_size))
    {
        printf("pfs size succeeded: 0x%lX\n", pfs_size);
    } else {
        printf("pfs size failed\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/pfs_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(&pfs_ctx, 1, sizeof(PartitionFileSystemContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("pfs ctx saved\n");
    } else {
        printf("pfs ctx not saved\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/pfs_header.bin", "wb");
    if (tmp_file)
    {
        fwrite(pfs_ctx.header, 1, pfs_ctx.header_size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("pfs header saved\n");
    } else {
        printf("pfs header not saved\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/pfs.bin", "wb");
    if (tmp_file)
    {
        u64 blksize = 0x400000;
        u64 total = pfs_ctx.size;
        
        printf("pfs created: 0x%lX\n", total);
        consoleUpdate(NULL);
        
        for(u64 curpos = 0; curpos < total; curpos += blksize)
        {
            if (blksize > (total - curpos)) blksize = (total - curpos);
            
            if (!pfsReadPartitionData(&pfs_ctx, buf, blksize, curpos))
            {
                printf("pfs read partition failed\n");
                goto out2;
            }
            
            fwrite(buf, 1, blksize, tmp_file);
        }
        
        fclose(tmp_file);
        tmp_file = NULL;
        
        printf("pfs read partition success\n");
    } else {
        printf("pfs not created\n");
    }
    
    consoleUpdate(NULL);
    
    pfs_entry = pfsGetEntryByName(&pfs_ctx, "main.npdm");
    if (!pfs_entry)
    {
        printf("pfs get entry by name failed\n");
        goto out2;
    }
    
    printf("pfs get entry by name succeeded\n");
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/main.npdm", "wb");
    if (tmp_file)
    {
        printf("main.npdm created. Target size -> 0x%lX\n", pfs_entry->size);
        consoleUpdate(NULL);
        
        if (!pfsReadEntryData(&pfs_ctx, pfs_entry, buf, pfs_entry->size, 0))
        {
            printf("pfs read entry data failed\n");
            goto out2;
        }
        
        fwrite(buf, 1, pfs_entry->size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        
        printf("pfs read main.npdm success\n");
    } else {
        printf("main.npdm not created\n");
    }
    
    consoleUpdate(NULL);
    
    u32 acid_offset = 0;
    memcpy(&acid_offset, buf + 0x78, sizeof(u32));
    
    PartitionFileSystemModifiedBlockInfo pfs_block_info = {0};
    
    if (pfsGenerateModifiedEntryData(&pfs_ctx, pfs_entry, rsa2048GetCustomAcidPublicKey(), RSA2048_SIGNATURE_SIZE, acid_offset + RSA2048_SIGNATURE_SIZE, &pfs_block_info))
    {
        printf("pfs mod data success | hbo: 0x%lX | hbs: 0x%lX | dbo: 0x%lX | dbs: 0x%lX\n", pfs_block_info.hash_block_offset, pfs_block_info.hash_block_size, pfs_block_info.data_block_offset, pfs_block_info.data_block_size);
        
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/nxdt_test/pfs_mod.bin", "wb");
        if (tmp_file)
        {
            fwrite(&pfs_block_info, 1, sizeof(PartitionFileSystemModifiedBlockInfo), tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("pfs mod data saved\n");
        } else {
            printf("pfs mod data not saved\n");
        }
        
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/nxdt_test/pfs_hash_mod.bin", "wb");
        if (tmp_file)
        {
            fwrite(pfs_block_info.hash_block, 1, pfs_block_info.hash_block_size, tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("pfs hash mod data saved\n");
        } else {
            printf("pfs hash mod data not saved\n");
        }
        
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/nxdt_test/pfs_data_mod.bin", "wb");
        if (tmp_file)
        {
            fwrite(pfs_block_info.data_block, 1, pfs_block_info.data_block_size, tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("pfs data mod data saved\n");
        } else {
            printf("pfs data mod data not saved\n");
        }
        
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/nxdt_test/new_nca_ctx.bin", "wb");
        if (tmp_file)
        {
            fwrite(nca_ctx, 1, sizeof(NcaContext), tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("nca ctx saved\n");
        } else {
            printf("nca ctx not saved\n");
        }
    } else {
        printf("pfs mod data failed\n");
    }
    
out2:
    while(appletMainLoop())
    {
        consoleUpdate(NULL);
        hidScanInput();
        if (utilsHidKeysAllDown() & KEY_A) break;
    }
    
    if (tmp_file) fclose(tmp_file);
    
    pfsFreeContext(&pfs_ctx);
    
    if (serviceIsActive(&(ncm_storage.s))) ncmContentStorageClose(&ncm_storage);
    
    if (nca_ctx) free(nca_ctx);
    
    if (buf) free(buf);
    
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
