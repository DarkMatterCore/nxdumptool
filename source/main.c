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

#include "gamecard.h"
#include "nca.h"
#include "cert.h"

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
    
    printf("waiting...\n");
    consoleUpdate(NULL);
    
    while(appletMainLoop())
    {
        if (gamecardIsReady()) break;
    }
    
    FILE *tmp_file = NULL;
    GameCardHeader header = {0};
    FsGameCardCertificate cert = {0};
    u64 total_size = 0, trimmed_size = 0;
    u32 update_version = 0;
    u64 nca_offset = 0, nca_size = 0;
    
    if (gamecardGetHeader(&header))
    {
        printf("header success\n");
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/header.bin", "wb");
        if (tmp_file)
        {
            fwrite(&header, 1, sizeof(GameCardHeader), tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("header saved\n");
        } else {
            printf("header not saved\n");
        }
    } else {
        printf("header failed\n");
    }
    
    consoleUpdate(NULL);
    
    if (gamecardGetTotalSize(&total_size))
    {
        printf("total_size: 0x%lX\n", total_size);
    } else {
        printf("total_size failed\n");
    }
    
    consoleUpdate(NULL);
    
    if (gamecardGetTrimmedSize(&trimmed_size))
    {
        printf("trimmed_size: 0x%lX\n", trimmed_size);
    } else {
        printf("trimmed_size failed\n");
    }
    
    consoleUpdate(NULL);
    
    if (gamecardGetCertificate(&cert))
    {
        printf("gamecard cert success\n");
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/cert.bin", "wb");
        if (tmp_file)
        {
            fwrite(&cert, 1, sizeof(FsGameCardCertificate), tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("gamecard cert saved\n");
        } else {
            printf("gamecard cert not saved\n");
        }
    } else {
        printf("gamecard cert failed\n");
    }
    
    consoleUpdate(NULL);
    
    if (gamecardGetBundledFirmwareUpdateVersion(&update_version))
    {
        printf("update_version: %u\n", update_version);
    } else {
        printf("update_version failed\n");
    }
    
    consoleUpdate(NULL);
    
    u8 *buf = malloc((u64)0x400300); // 4 MiB + 512 bytes + 256 bytes
    if (buf)
    {
        printf("buf succeeded\n");
        consoleUpdate(NULL);
        
        if (gamecardRead(buf, (u64)0x400300, (u64)0x16F18100)) // force unaligned read that spans both storage areas
        {
            u32 crc = crc32Calculate(buf, (u64)0x400300);
            
            printf("read succeeded: %08X\n", crc);
            consoleUpdate(NULL);
            
            tmp_file = fopen("sdmc:/data.bin", "wb");
            if (tmp_file)
            {
                fwrite(buf, 1, (u64)0x400300, tmp_file);
                fclose(tmp_file);
                tmp_file = NULL;
                printf("data saved\n");
            } else {
                printf("data not saved\n");
            }
        } else {
            printf("read failed\n");
        }
    } else {
        printf("buf failed\n");
    }
    
    consoleUpdate(NULL);
    
    // Should match 0x1657F5E00
    if (gamecardGetOffsetAndSizeFromHashFileSystemPartitionEntryByName(GameCardHashFileSystemPartitionType_Secure, "7e86768383cfabb30f1b58d2373fed07.nca", &nca_offset, &nca_size))
    {
        printf("nca_offset: 0x%lX | nca_size: 0x%lX\n", nca_offset, nca_size);
    } else {
        printf("nca_offset + nca_size failed\n");
    }
    
    consoleUpdate(NULL);
    
    Ticket tik = {0};
    TikCommonBlock *tik_common_blk = NULL;
    
    u8 *cert_chain = NULL;
    u64 cert_chain_size = 0;
    
    FsRightsId rights_id = {
        .c = { 0x01, 0x00, 0x82, 0x40, 0x0B, 0xCC, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 } // Untitled Goose Game
    };
    
    if (tikRetrieveTicketByRightsId(&tik, &rights_id, false))
    {
        printf("tik succeeded\n");
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/tik.bin", "wb");
        if (tmp_file)
        {
            fwrite(&tik, 1, sizeof(Ticket), tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("tik saved\n");
        } else {
            printf("tik not saved\n");
        }
        
        consoleUpdate(NULL);
        
        /*tikConvertPersonalizedTicketToCommonTicket(&tik);
        
        printf("common tik generated\n");
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/common_tik.bin", "wb");
        if (tmp_file)
        {
            fwrite(&tik, 1, sizeof(Ticket), tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("common tik saved\n");
        } else {
            printf("common tik not saved\n");
        }
        
        consoleUpdate(NULL);*/
        
        tik_common_blk = tikGetCommonBlockFromTicket(&tik);
        
        if (tik_common_blk)
        {
            cert_chain = certGenerateRawCertificateChainBySignatureIssuer(tik_common_blk->issuer, &cert_chain_size);
            if (cert_chain)
            {
                printf("cert chain succeeded | size: 0x%lX\n", cert_chain_size);
                consoleUpdate(NULL);
                
                tmp_file = fopen("sdmc:/chain.bin", "wb");
                if (tmp_file)
                {
                    fwrite(cert_chain, 1, cert_chain_size, tmp_file);
                    fclose(tmp_file);
                    tmp_file = NULL;
                    printf("cert chain saved\n");
                } else {
                    printf("cert chain not saved\n");
                }
            } else {
                printf("cert chain failed\n");
            }
        }
    } else {
        printf("tik failed\n");
    }
    
    consoleUpdate(NULL);
    
    NcaContext *nca_ctx = calloc(1, sizeof(NcaContext));
    if (nca_ctx)
    {
        printf("nca ctx buf succeeded\n");
        consoleUpdate(NULL);
        
        NcmContentStorage ncm_storage = {0};
        if (R_SUCCEEDED(ncmOpenContentStorage(&ncm_storage, NcmStorageId_SdCard)))
        {
            printf("ncm open storage succeeded\n");
            consoleUpdate(NULL);
            
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
            
            if (ncaInitializeContext(nca_ctx, &tik, NcmStorageId_SdCard, &ncm_storage, 0, &content_info))
            {
                printf("nca initialize ctx succeeded\n");
                consoleUpdate(NULL);
                
                tmp_file = fopen("sdmc:/nca_ctx.bin", "wb");
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
                
                tmp_file = fopen("sdmc:/section0.bin", "wb");
                if (tmp_file)
                {
                    printf("nca section0 created\n");
                    consoleUpdate(NULL);
                    
                    u64 curpos = 0;
                    u64 blksize = (u64)0x400000;
                    u64 total = nca_ctx->fs_contexts[0].section_size;
                    
                    for(u64 curpos = 0; curpos < total; curpos += blksize)
                    {
                        if (blksize > (total - curpos)) blksize = (total - curpos);
                        
                        if (!ncaReadFsSection(&(nca_ctx->fs_contexts[0]), buf, blksize, curpos))
                        {
                            printf("nca read section failed\n");
                            break;
                        }
                        
                        fwrite(buf, 1, blksize, tmp_file);
                    }
                    
                    if (curpos >= total) printf("nca read section success\n");
                    
                    consoleUpdate(NULL);
                    
                    fclose(tmp_file);
                    tmp_file = NULL;
                } else {
                    printf("nca section0 not created\n");
                }
            } else {
                printf("nca initialize ctx failed\n");
            }
            
            consoleUpdate(NULL);
            
            ncmContentStorageClose(&ncm_storage);
        } else {
            printf("ncm open storage failed\n");
        }
        
        free(nca_ctx);
    } else {
        printf("nca ctx buf failed\n");
    }
    
    consoleUpdate(NULL);
    
    
    while(true)
    {
        hidScanInput();
        if (utilsHidKeysAllDown() & KEY_A) break;
    }
    
    
    if (buf) free(buf);
    
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
