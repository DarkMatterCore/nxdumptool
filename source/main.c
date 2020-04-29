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
    
    // Untitled Goose Game's Program NCA
    /*NcmPackagedContentInfo content_info = {
        .hash = {
            0x8E, 0xF9, 0x20, 0xD4, 0x5E, 0xE1, 0x9E, 0xD1, 0xD2, 0x04, 0xC4, 0xC8, 0x22, 0x50, 0x79, 0xE8,
            0xD3, 0xE2, 0xE2, 0xA0, 0x66, 0xFD, 0x2B, 0xB6, 0x5C, 0x73, 0xF6, 0x89, 0xE2, 0x25, 0x0A, 0x82
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
    };*/
    
    // Untitled Goose Game's Control NCA
    NcmPackagedContentInfo content_info = {
        .hash = {
            0xCE, 0x6E, 0x17, 0x1F, 0x93, 0x2D, 0x29, 0x28, 0xC1, 0x62, 0x94, 0x5B, 0x86, 0x2C, 0x42, 0x93,
            0xAC, 0x2C, 0x0D, 0x3E, 0xD7, 0xCE, 0x07, 0xA2, 0x34, 0x33, 0x43, 0xD9, 0x21, 0x8A, 0xA3, 0xFE
        },
        .info = {
            .content_id = {
                .c = { 0xCE, 0x6E, 0x17, 0x1F, 0x93, 0x2D, 0x29, 0x28, 0xC1, 0x62, 0x94, 0x5B, 0x86, 0x2C, 0x42, 0x93 }
            },
            .size = {
                0x00, 0x74, 0x0A, 0x00, 0x00, 0x00
            },
            .content_type = NcmContentType_Control,
            .id_offset = 0
        }
    };
    
    u64 romfs_size = 0;
    RomFileSystemFileEntry *romfs_file_entry = NULL;
    RomFileSystemContext romfs_ctx = {0};
    RomFileSystemFileEntryPatch romfs_patch = {0};
    
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
    
    if (!romfsInitializeContext(&romfs_ctx, &(nca_ctx->fs_contexts[0])))
    {
        printf("romfs initialize ctx failed\n");
        goto out2;
    }
    
    printf("romfs initialize ctx succeeded\n");
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
        printf("romfs dir table saved\n");
    } else {
        printf("romfs dir table not saved\n");
    }
    
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/file_table.bin", "wb");
    if (tmp_file)
    {
        fwrite(romfs_ctx.file_table, 1, romfs_ctx.file_table_size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("romfs file table saved\n");
    } else {
        printf("romfs file table not saved\n");
    }
    
    consoleUpdate(NULL);
    
    if (romfsReadFileSystemData(&romfs_ctx, buf, romfs_ctx.size, 0))
    {
        printf("romfs read fs data success\n");
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/nxdt_test/romfs.bin", "wb");
        if (tmp_file)
        {
            fwrite(buf, 1, romfs_ctx.size, tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("romfs data saved\n");
        } else {
            printf("romfs data not saved\n");
        }
    } else {
        printf("romfs read fs data failed\n");
    }
    
    if (romfsGetTotalDataSize(&romfs_ctx, &romfs_size))
    {
        printf("romfs size succeeded: 0x%lX\n", romfs_size);
    } else {
        printf("romfs size failed\n");
    }
    
    consoleUpdate(NULL);
    
    romfs_file_entry = romfsGetFileEntryByPath(&romfs_ctx, "/control.nacp");
    if (!romfs_file_entry)
    {
        printf("romfs get file entry by path failed\n");
        goto out2;
    }
    
    printf("romfs get file entry by path success: %s | %p\n", romfs_file_entry->name, romfs_file_entry);
    consoleUpdate(NULL);
    
    if (!romfsReadFileEntryData(&romfs_ctx, romfs_file_entry, buf, romfs_file_entry->size, 0))
    {
        printf("romfs read file entry failed\n");
        goto out2;
    }
    
    printf("romfs read file entry success\n");
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/control.nacp", "wb");
    if (tmp_file)
    {
        fwrite(buf, 1, romfs_file_entry->size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("romfs file entry data saved\n");
    } else {
        printf("romfs file entry data not saved\n");
    }
    
    consoleUpdate(NULL);
    
    NacpStruct *nacp_data = (NacpStruct*)buf;
    memset(nacp_data->lang, 0, MEMBER_SIZE(NacpStruct, lang));
    for(u8 i = 0; i < 16; i++)
    {
        sprintf(nacp_data->lang[i].name, "nxdumptool");
        sprintf(nacp_data->lang[i].author, "DarkMatterCore");
    }
    
    tmp_file = fopen("sdmc:/nxdt_test/control_mod.nacp", "wb");
    if (tmp_file)
    {
        fwrite(buf, 1, romfs_file_entry->size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("romfs file entry mod data saved\n");
    } else {
        printf("romfs file entry mod data not saved\n");
    }
    
    consoleUpdate(NULL);
    
    if (!romfsGenerateFileEntryPatch(&romfs_ctx, romfs_file_entry, buf, MEMBER_SIZE(NacpStruct, lang), 0, &romfs_patch))
    {
        printf("romfs file entry patch failed\n");
        goto out2;
    }
    
    printf("romfs file entry patch success\n");
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/romfs_patch.bin", "wb");
    if (tmp_file)
    {
        fwrite(&romfs_patch, 1, sizeof(RomFileSystemFileEntryPatch), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("romfs patch saved\n");
    } else {
        printf("romfs patch not saved\n");
    }
    
    for(u8 i = 0; i < (NCA_IVFC_HASH_DATA_LAYER_COUNT + 1); i++)
    {
        NcaHashInfoLayerPatch *layer_patch = (i < NCA_IVFC_HASH_DATA_LAYER_COUNT ? &(romfs_patch.cur_format_patch.hash_data_layer_patch[i]) : &(romfs_patch.cur_format_patch.hash_target_layer_patch));
        if (!layer_patch->size || !layer_patch->data) continue;
        
        char path[64];
        sprintf(path, "sdmc:/nxdt_test/romfs_patch_l%u.bin", i);
        
        tmp_file = fopen(path, "wb");
        if (tmp_file)
        {
            fwrite(layer_patch->data, 1, layer_patch->size, tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("romfs patch #%u saved\n", i);
        } else {
            printf("romfs patch #%u not saved\n", i);
        }
        
        consoleUpdate(NULL);
    }
    
    if (!ncaEncryptHeader(nca_ctx))
    {
        printf("nca header mod not encrypted\n");
        goto out2;
    }
    
    printf("nca header mod encrypted\n");
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/nca_header_mod.bin", "wb");
    if (tmp_file)
    {
        fwrite(&(nca_ctx->header), 1, sizeof(NcaHeader), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("nca header mod saved\n");
    } else {
        printf("nca header mod not saved\n");
    }
    
    
    
    
    
    
    
    
    
    
    
    
out2:
    while(appletMainLoop())
    {
        consoleUpdate(NULL);
        hidScanInput();
        if (utilsHidKeysAllDown() & KEY_A) break;
    }
    
    if (tmp_file) fclose(tmp_file);
    
    romfsFreeFileEntryPatch(&romfs_patch);
    
    romfsFreeContext(&romfs_ctx);
    
    if (serviceIsActive(&(ncm_storage.s))) ncmContentStorageClose(&ncm_storage);
    
    if (nca_ctx) free(nca_ctx);
    
    if (buf) free(buf);
    
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
