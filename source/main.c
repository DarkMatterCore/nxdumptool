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
        printf("cert success\n");
        consoleUpdate(NULL);
        
        tmp_file = fopen("sdmc:/cert.bin", "wb");
        if (tmp_file)
        {
            fwrite(&cert, 1, sizeof(FsGameCardCertificate), tmp_file);
            fclose(tmp_file);
            tmp_file = NULL;
            printf("cert saved\n");
        } else {
            printf("cert not saved\n");
        }
    } else {
        printf("cert failed\n");
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
            printf("read succeeded\n");
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
        
        free(buf);
    } else {
        printf("buf failed\n");
    }
    
    consoleUpdate(NULL);
    
    SLEEP(3);
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
