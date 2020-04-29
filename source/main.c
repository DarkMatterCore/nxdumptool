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
    NcmPackagedContentInfo content_info = {
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
    };
    
    // Untitled Goose Game's Control NCA
    /*NcmPackagedContentInfo content_info = {
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
    };*/
    
    PartitionFileSystemEntry *pfs_entry = NULL;
    PartitionFileSystemContext pfs_ctx = {0};
    NcaHierarchicalSha256Patch pfs_patch = {0};
    
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
    
    if (!pfsInitializeContext(&pfs_ctx, &(nca_ctx->fs_contexts[0])))
    {
        printf("pfs initialize ctx failed\n");
        goto out2;
    }
    
    printf("pfs initialize ctx succeeded\n");
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
    
    pfs_entry = pfsGetEntryByName(&pfs_ctx, "main.npdm");
    if (!pfs_entry)
    {
        printf("pfs get entry by name failed\n");
        goto out2;
    }
    
    printf("pfs get entry by name succeeded\n");
    consoleUpdate(NULL);
    
    if (!pfsReadEntryData(&pfs_ctx, pfs_entry, buf, pfs_entry->size, 0))
    {
        printf("pfs read entry data failed\n");
        goto out2;
    }
    
    printf("pfs read entry data succeeded\n");
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/main.npdm", "wb");
    if (tmp_file)
    {
        fwrite(buf, 1, pfs_entry->size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("main.npdm saved\n");
    } else {
        printf("main.npdm not saved\n");
    }
    
    consoleUpdate(NULL);
    
    u32 acid_offset = 0;
    memcpy(&acid_offset, buf + 0x78, sizeof(u32));
    memcpy(buf + acid_offset + RSA2048_SIGNATURE_SIZE, rsa2048GetCustomAcidPublicKey(), RSA2048_SIGNATURE_SIZE);
    
    tmp_file = fopen("sdmc:/nxdt_test/main_mod.npdm", "wb");
    if (tmp_file)
    {
        fwrite(buf, 1, pfs_entry->size, tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("main_mod.npdm saved\n");
    } else {
        printf("main_mod.npdm not saved\n");
    }
    
    consoleUpdate(NULL);
    
    if (!pfsGenerateEntryPatch(&pfs_ctx, pfs_entry, buf + acid_offset + RSA2048_SIGNATURE_SIZE, RSA2048_SIGNATURE_SIZE, acid_offset + RSA2048_SIGNATURE_SIZE, &pfs_patch))
    {
        printf("pfs entry patch failed\n");
        goto out2;
    }
    
    printf("pfs entry patch succeeded\n");
    consoleUpdate(NULL);
    
    tmp_file = fopen("sdmc:/nxdt_test/pfs_patch.bin", "wb");
    if (tmp_file)
    {
        fwrite(&pfs_patch, 1, sizeof(NcaHierarchicalSha256Patch), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        printf("pfs patch saved\n");
    } else {
        printf("pfs patch not saved\n");
    }
    
    for(u8 i = 0; i < 2; i++)
    {
        NcaHashInfoLayerPatch *layer_patch = (i == 0 ? &(pfs_patch.hash_data_layer_patch) : &(pfs_patch.hash_target_layer_patch));
        if (!layer_patch->size || !layer_patch->data) continue;
        
        char path[64];
        sprintf(path, "sdmc:/nxdt_test/pfs_patch_l%u.bin", i);
        
        tmp_file = fopen(path, "wb");
        if (tmp_file)
        {
            fwrite(layer_patch->data, 1, layer_patch->size, tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("pfs patch #%u saved\n", i);
        } else {
            printf("pfs patch #%u not saved\n", i);
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
    
    ncaFreeHierarchicalSha256Patch(&pfs_patch);
    
    pfsFreeContext(&pfs_ctx);
    
    if (serviceIsActive(&(ncm_storage.s))) ncmContentStorageClose(&ncm_storage);
    
    if (nca_ctx) free(nca_ctx);
    
    if (buf) free(buf);
    
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
