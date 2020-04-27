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
#include "romfs.h"
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
    
    char romfs_path[FS_MAX_PATH] = {0};
    u64 romfs_size = 0;
    RomFileSystemDirectoryEntry *romfs_dir_entry = NULL;
    RomFileSystemFileEntry *romfs_file_entry = NULL;
    RomFileSystemContext romfs_ctx = {0};
    
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
    
    if (!romfsInitializeContext(&romfs_ctx, &(nca_ctx->fs_contexts[1])))
    {
        printf("romfs initialize ctx failed\n");
        goto out2;
    }
    
    printf("romfs initialize ctx succeeded\n");
    consoleUpdate(NULL);
    
    if (romfsGetTotalDataSize(&romfs_ctx, &romfs_size))
    {
        printf("romfs size succeeded: 0x%lX\n", romfs_size);
    } else {
        printf("romfs size failed\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/romfs_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(&romfs_ctx, 1, sizeof(RomFileSystemContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("romfs ctx saved\n");
    } else {
        printf("romfs ctx not saved\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/dir_table.bin", "wb");
    if (tmp_file)
    {
        fwrite(romfs_ctx.dir_table, 1, romfs_ctx.dir_table_size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("dir table saved\n");
    } else {
        printf("dir table not saved\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/file_table.bin", "wb");
    if (tmp_file)
    {
        fwrite(romfs_ctx.file_table, 1, romfs_ctx.file_table_size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("file table saved\n");
    } else {
        printf("file table not saved\n");
    }
    
    consoleUpdate(NULL);
    
    romfs_dir_entry = romfsGetDirectoryEntryByOffset(&romfs_ctx, 0x74); // "Resources"
    if (!romfs_dir_entry)
    {
        printf("romfs dir entry failed\n");
        goto out2;
    }
    
    printf("romfs dir entry success: %s | %p\n", romfs_dir_entry->name, romfs_dir_entry);
    consoleUpdate(NULL);
    
    if (romfsGetDirectoryDataSize(&romfs_ctx, romfs_dir_entry, &romfs_size))
    {
        printf("romfs dir size succeeded: 0x%lX\n", romfs_size);
    } else {
        printf("romfs dir size failed\n");
    }
    
    consoleUpdate(NULL);
    
    romfs_file_entry = romfsGetFileEntryByOffset(&romfs_ctx, romfs_dir_entry->file_offset); // "mscorlib.dll-resources.dat"
    if (!romfs_file_entry)
    {
        printf("romfs file entry failed\n");
        goto out2;
    }
    
    printf("romfs file entry success: %s | %p\n", romfs_file_entry->name, romfs_file_entry);
    consoleUpdate(NULL);
    
    if (!romfsGeneratePathFromDirectoryEntry(&romfs_ctx, romfs_dir_entry, romfs_path, FS_MAX_PATH))
    {
        printf("romfs generate dir path failed\n");
        goto out2;
    }
    
    printf("romfs generate dir path success: %s\n", romfs_path);
    consoleUpdate(NULL);
    
    romfs_dir_entry = romfsGetDirectoryEntryByPath(&romfs_ctx, romfs_path);
    if (!romfs_dir_entry)
    {
        printf("romfs get dir entry by path failed\n");
        goto out2;
    }
    
    printf("romfs get dir entry by path success: %s | %p\n", romfs_dir_entry->name, romfs_dir_entry);
    consoleUpdate(NULL);
    
    if (!romfsGeneratePathFromFileEntry(&romfs_ctx, romfs_file_entry, romfs_path, FS_MAX_PATH))
    {
        printf("romfs generate file path failed\n");
        goto out2;
    }
    
    printf("romfs generate file path success: %s\n", romfs_path);
    consoleUpdate(NULL);
    
    romfs_file_entry = romfsGetFileEntryByPath(&romfs_ctx, romfs_path);
    if (!romfs_file_entry)
    {
        printf("romfs get file entry by path failed\n");
        goto out2;
    }
    
    printf("romfs get file entry by path success: %s | %p\n", romfs_file_entry->name, romfs_file_entry);
    consoleUpdate(NULL);
    
    if (romfsReadFileEntryData(&romfs_ctx, romfs_file_entry, buf, romfs_file_entry->size, 0))
    {
        printf("romfs read file entry success\n");
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/nxdt_test/mscorlib.dll-resources.dat", "wb");
        if (tmp_file)
        {
            fwrite(buf, 1, romfs_file_entry->size, tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("romfs file entry data saved\n");
        } else {
            printf("romfs file entry data not saved\n");
        }
    } else {
        printf("romfs read file entry failed\n");
    }
    
    
    
    
    
    
    
    
out2:
    while(appletMainLoop())
    {
        consoleUpdate(NULL);
        hidScanInput();
        if (utilsHidKeysAllDown() & KEY_A) break;
    }
    
    if (tmp_file) fclose(tmp_file);
    
    romfsFreeContext(&romfs_ctx);
    
    if (serviceIsActive(&(ncm_storage.s))) ncmContentStorageClose(&ncm_storage);
    
    if (nca_ctx) free(nca_ctx);
    
    if (buf) free(buf);
    
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
