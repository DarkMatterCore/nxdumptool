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

#include "bktr.h"



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
    
    Ticket base_tik = {0}, update_tik = {0};
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    NcmContentStorage ncm_storage = {0};
    
    BktrContext bktr_ctx = {0};
    RomFileSystemFileEntry *bktr_file_entry = NULL;
    
    Result rc = 0;
    
    mkdir("sdmc:/nxdt_test", 0744);
    
    // Untitled Goose Game's Base Program NCA
    NcmContentInfo base_program_content_info = {
        .content_id = {
            .c = { 0x8E, 0xF9, 0x20, 0xD4, 0x5E, 0xE1, 0x9E, 0xD1, 0xD2, 0x04, 0xC4, 0xC8, 0x22, 0x50, 0x79, 0xE8 }
        },
        .size = {
            0x00, 0x40, 0xAD, 0x31, 0x00, 0x00
        },
        .content_type = NcmContentType_Program,
        .id_offset = 0
    };
    
    // Untitled Goose Game's Update Program NCA
    NcmContentInfo update_program_content_info = {
        .content_id = {
            .c = { 0xDB, 0xEE, 0x62, 0x0E, 0x8F, 0x64, 0x37, 0xE4, 0x8A, 0x5C, 0x63, 0x61, 0xE8, 0xD2, 0x32, 0x6A }
        },
        .size = {
            0x00, 0xF4, 0xA0, 0x0E, 0x00, 0x00
        },
        .content_type = NcmContentType_Program,
        .id_offset = 0
    };
    
    buf = malloc(0x400000);
    if (!buf)
    {
        printf("read buf failed\n");
        goto out2;
    }
    
    printf("read buf succeeded\n");
    consoleUpdate(NULL);
    
    base_nca_ctx = calloc(1, sizeof(NcaContext));
    if (!base_nca_ctx)
    {
        printf("base nca ctx buf failed\n");
        goto out2;
    }
    
    printf("base nca ctx buf succeeded\n");
    consoleUpdate(NULL);
    
    update_nca_ctx = calloc(1, sizeof(NcaContext));
    if (!update_nca_ctx)
    {
        printf("update nca ctx buf failed\n");
        goto out2;
    }
    
    printf("update nca ctx buf succeeded\n");
    consoleUpdate(NULL);
    
    rc = ncmOpenContentStorage(&ncm_storage, NcmStorageId_SdCard);
    if (R_FAILED(rc))
    {
        printf("ncm open storage failed\n");
        goto out2;
    }
    
    printf("ncm open storage succeeded\n");
    consoleUpdate(NULL);
    
    if (!ncaInitializeContext(base_nca_ctx, NcmStorageId_SdCard, &ncm_storage, 0, &base_program_content_info, &base_tik))
    {
        printf("base nca initialize ctx failed\n");
        goto out2;
    }
    
    tmp_file = fopen("sdmc:/nxdt_test/base_nca_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(base_nca_ctx, 1, sizeof(NcaContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("base nca ctx saved\n");
    } else {
        printf("base nca ctx not saved\n");
    }
    
    consoleUpdate(NULL);
    
    if (!ncaInitializeContext(update_nca_ctx, NcmStorageId_SdCard, &ncm_storage, 0, &update_program_content_info, &update_tik))
    {
        printf("update nca initialize ctx failed\n");
        goto out2;
    }
    
    tmp_file = fopen("sdmc:/nxdt_test/update_nca_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(update_nca_ctx, 1, sizeof(NcaContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("update nca ctx saved\n");
    } else {
        printf("update nca ctx not saved\n");
    }
    
    consoleUpdate(NULL);
    
    if (!bktrInitializeContext(&bktr_ctx, &(base_nca_ctx->fs_contexts[1]), &(update_nca_ctx->fs_contexts[1])))
    {
        printf("bktr initialize ctx failed\n");
        goto out2;
    }
    
    printf("bktr initialize ctx succeeded\n");
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/bktr_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(&bktr_ctx, 1, sizeof(BktrContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("bktr ctx saved\n");
    } else {
        printf("bktr ctx not saved\n");
    }
    
    consoleUpdate(NULL);
    
    /*bktr_file_entry = bktrGetFileEntryByPath(&bktr_ctx, "/Data/resources.assets");
    if (!bktr_file_entry)
    {
        printf("bktr get file entry by path failed\n");
        goto out2;
    }
    
    printf("bktr get file entry by path success: %.*s | 0x%lX\n", bktr_file_entry->name_length, bktr_file_entry->name, bktr_file_entry->size);
    consoleUpdate(NULL);*/
    
    /*tmp_file = fopen("sdmc:/nxdt_test/resources.assets", "wb");
    if (tmp_file)
    {
        u64 curpos = 0, blksize = 0x400000;
        for(curpos = 0; curpos < bktr_file_entry->size; curpos += blksize)
        {
            if (blksize > (bktr_file_entry->size - curpos)) blksize = (bktr_file_entry->size - curpos);
            
            if (!bktrReadFileEntryData(&bktr_ctx, bktr_file_entry, buf, blksize, curpos)) break;
            
            fwrite(buf, 1, blksize, tmp_file);
        }
        
        fclose(tmp_file);
        tmp_file = NULL;
        
        if (curpos < bktr_file_entry->size)
        {
            printf("resources.assets read error\n");
        } else {
            printf("resources.assets saved\n");
        }
    } else {
        printf("resources.assets not saved\n");
    }
    
    consoleUpdate(NULL);*/
    
    /*tmp_file = fopen("sdmc:/nxdt_test/romfs.bin", "wb");
    if (tmp_file)
    {
        u64 curpos = 0, blksize = 0x400000;
        for(curpos = 0; curpos < bktr_ctx.size; curpos += blksize)
        {
            if (blksize > (bktr_ctx.size - curpos)) blksize = (bktr_ctx.size - curpos);
            
            if (!bktrReadFileSystemData(&bktr_ctx, buf, blksize, curpos)) break;
            
            fwrite(buf, 1, blksize, tmp_file);
        }
        
        fclose(tmp_file);
        tmp_file = NULL;
        
        if (curpos < bktr_ctx.size)
        {
            printf("romfs read error\n");
        } else {
            printf("romfs saved\n");
        }
    } else {
        printf("romfs not saved\n");
    }
    
    consoleUpdate(NULL);*/
    
    printf("updated file list:\n");
    consoleUpdate(NULL);
    
    u64 offset = 0;
    bool updated = false;
    char bktr_path[FS_MAX_PATH] = {0};
    
    while(offset < bktr_ctx.patch_romfs_ctx.file_table_size)
    {
        if (!(bktr_file_entry = bktrGetFileEntryByOffset(&bktr_ctx, offset)))
        {
            printf("Failed to retrieve file entry!\n");
            goto out2;
        }
        
        if (bktrIsFileEntryUpdated(&bktr_ctx, bktr_file_entry, &updated) && updated && bktrGeneratePathFromFileEntry(&bktr_ctx, bktr_file_entry, bktr_path, FS_MAX_PATH))
        {
            printf("%s\n", bktr_path);
            consoleUpdate(NULL);
        }
        
        offset += ALIGN_UP(sizeof(RomFileSystemFileEntry) + bktr_file_entry->name_length, 4);
    }
    
    
    
    
    
    
    
    
    
    
out2:
    consoleUpdate(NULL);
    utilsWaitForButtonPress();
    
    if (tmp_file) fclose(tmp_file);
    
    bktrFreeContext(&bktr_ctx);
    
    if (serviceIsActive(&(ncm_storage.s))) ncmContentStorageClose(&ncm_storage);
    
    if (update_nca_ctx) free(update_nca_ctx);
    
    if (base_nca_ctx) free(base_nca_ctx);
    
    if (buf) free(buf);
    
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
