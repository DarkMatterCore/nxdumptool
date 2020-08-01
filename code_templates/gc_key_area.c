/*
 * main.c
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
#include "gamecard.h"

static void consolePrint(const char *text, ...)
{
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    consoleUpdate(NULL);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    int ret = 0;
    
    GameCardKeyArea gc_key_area = {0};
    char path[FS_MAX_PATH] = {0};
    
    LOGFILE("nxdumptool starting.");
    
    consoleInit(NULL);
    
    consolePrint("initializing...\n");
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    consolePrint("waiting for gamecard. press b to cancel.\n");
    
    while(true)
    {
        hidScanInput();
        
        if (utilsHidKeysAllDown() == KEY_B)
        {
            consolePrint("process cancelled\n");
            goto out2;
        }
        
        u8 status = gamecardGetStatus();
        if (status == GameCardStatus_InsertedAndInfoLoaded) break;
        if (status == GameCardStatus_InsertedAndInfoNotLoaded) consolePrint("gamecard inserted but info couldn't be loaded from it. check nogc patch setting.\n");
    }
    
    consolePrint("gamecard detected.\n");
    
    if (!gamecardGetKeyArea(&gc_key_area))
    {
        consolePrint("failed to get gamecard key area\n");
        goto out2;
    }
    
    consolePrint("get gamecard key area ok\n");
    
    sprintf(path, "sdmc:/card_key_area_%016lX.bin", gc_key_area.initial_data.key_source.package_id);
    
    FILE *kafd = fopen(path, "wb");
    if (!kafd)
    {
        consolePrint("failed to open \"%s\" for writing\n", path);
        goto out2;
    }
    
    fwrite(&gc_key_area, 1, sizeof(GameCardKeyArea), kafd);
    fclose(kafd);
    utilsCommitSdCardFileSystemChanges();
    
    consolePrint("successfully saved key area to \"%s\"\n", path);
    
out2:
    consolePrint("press any button to exit\n");
    utilsWaitForButtonPress(KEY_NONE);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
