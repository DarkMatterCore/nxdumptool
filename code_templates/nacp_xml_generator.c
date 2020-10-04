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
#include "title.h"
#include "nacp.h"

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
    
    LOGFILE(APP_TITLE " starting.");
    
    consoleInit(NULL);
    
    consolePrint("initializing...\n");
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    u32 app_count = 0;
    TitleApplicationMetadata **app_metadata = NULL;
    TitleUserApplicationData user_app_data = {0};
    
    u32 selected_idx = 0, page_size = 30, scroll = 0;
    bool exit_prompt = true;
    
    NcaContext *nca_ctx = NULL;
    Ticket tik = {0};
    NacpContext nacp_ctx = {0};
    
    app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
    if (!app_metadata || !app_count)
    {
        consolePrint("app metadata failed\n");
        goto out2;
    }
    
    consolePrint("app metadata succeeded\n");
    
    utilsSleep(1);
    
    while(true)
    {
        consoleClear();
        printf("select an user application to generate a nacp xml for.\npress b to exit.\n\n");
        printf("title: %u / %u\n\n", selected_idx + 1, app_count);
        
        for(u32 i = scroll; i < app_count; i++)
        {
            if (i >= (scroll + page_size)) break;
            printf("%s%016lX - %s\n", i == selected_idx ? " -> " : "    ", app_metadata[i]->title_id, app_metadata[i]->lang_entry.name);
        }
        
        printf("\n");
        
        consoleUpdate(NULL);
        
        u64 btn_down = 0, btn_held = 0;
        while(true)
        {
            hidScanInput();
            btn_down = utilsHidKeysAllDown();
            btn_held = utilsHidKeysAllHeld();
            if (btn_down || btn_held) break;
            
            if (titleIsGameCardInfoUpdated())
            {
                free(app_metadata);
                
                app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
                if (!app_metadata)
                {
                    consolePrint("\napp metadata failed\n");
                    goto out2;
                }
                
                selected_idx = scroll = 0;
                break;
            }
        }
        
        if (btn_down & KEY_A)
        {
            if (!titleGetUserApplicationData(app_metadata[selected_idx]->title_id, &user_app_data) || !user_app_data.app_info)
            {
                consolePrint("\nthe selected title doesn't have available base content.\n");
                utilsSleep(3);
                continue;
            }
            
            break;
        } else
        if ((btn_down & KEY_DDOWN) || (btn_held & (KEY_LSTICK_DOWN | KEY_RSTICK_DOWN)))
        {
            selected_idx++;
            
            if (selected_idx >= app_count)
            {
                if (btn_down & KEY_DDOWN)
                {
                    selected_idx = scroll = 0;
                } else {
                    selected_idx = (app_count - 1);
                }
            } else
            if (selected_idx >= (scroll + (page_size / 2)) && app_count > (scroll + page_size))
            {
                scroll++;
            }
        } else
        if ((btn_down & KEY_DUP) || (btn_held & (KEY_LSTICK_UP | KEY_RSTICK_UP)))
        {
            selected_idx--;
            
            if (selected_idx == UINT32_MAX)
            {
                if (btn_down & KEY_DUP)
                {
                    selected_idx = (app_count - 1);
                    scroll = (app_count >= page_size ? (app_count - page_size) : 0);
                } else {
                    selected_idx = 0;
                }
            } else
            if (selected_idx < (scroll + (page_size / 2)) && scroll > 0)
            {
                scroll--;
            }
        } else
        if (btn_down & KEY_B)
        {
            exit_prompt = false;
            goto out2;
        }
        
        if (btn_held & (KEY_LSTICK_DOWN | KEY_RSTICK_DOWN | KEY_LSTICK_UP | KEY_RSTICK_UP)) svcSleepThread(50000000); // 50 ms
    }
    
    consoleClear();
    consolePrint("selected title:\n%s (%016lX)\n\n", app_metadata[selected_idx]->lang_entry.name, app_metadata[selected_idx]->title_id);
    
    nca_ctx = calloc(1, sizeof(NcaContext));
    if (!nca_ctx)
    {
        consolePrint("nca ctx calloc failed\n");
        goto out2;
    }
    
    consolePrint("nca ctx calloc succeeded\n");
    
    if (!ncaInitializeContext(nca_ctx, user_app_data.app_info->storage_id, (user_app_data.app_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(user_app_data.app_info, NcmContentType_Control, 0), &tik))
    {
        consolePrint("Control nca initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("Control nca initialize ctx succeeded\n");
    
    if (!nacpInitializeContext(&nacp_ctx, nca_ctx))
    {
        consolePrint("nacp initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("nacp initialize ctx succeeded\n");
    
    if (nacpGenerateAuthoringToolXml(&nacp_ctx))
    {
        consolePrint("nacp xml succeeded\n");
        
        FILE *xml_fd = NULL;
        char path[FS_MAX_PATH] = {0};
        
        sprintf(path, "sdmc:/%s.nacp", nca_ctx->content_id_str);
        
        xml_fd = fopen(path, "wb");
        if (xml_fd)
        {
            fwrite(nacp_ctx.data, 1, sizeof(_NacpStruct), xml_fd);
            fclose(xml_fd);
            xml_fd = NULL;
        }
        
        sprintf(path, "sdmc:/%s.nacp.xml", nca_ctx->content_id_str);
        
        xml_fd = fopen(path, "wb");
        if (xml_fd)
        {
            fwrite(nacp_ctx.authoring_tool_xml, 1, nacp_ctx.authoring_tool_xml_size, xml_fd);
            fclose(xml_fd);
            xml_fd = NULL;
        }
        
        for(u8 i = 0; i < nacp_ctx.icon_count; i++)
        {
            NacpIconContext *icon_ctx = &(nacp_ctx.icon_ctx[i]);
            
            sprintf(path, "sdmc:/%s.nx.%s.jpg", nca_ctx->content_id_str, nacpGetLanguageString(icon_ctx->language));
            
            xml_fd = fopen(path, "wb");
            if (xml_fd)
            {
                fwrite(icon_ctx->icon_data, 1, icon_ctx->icon_size, xml_fd);
                fclose(xml_fd);
                xml_fd = NULL;
            }
        }
    } else {
        consolePrint("nacp xml failed\n");
    }
    
out2:
    if (exit_prompt)
    {
        consolePrint("press any button to exit\n");
        utilsWaitForButtonPress(KEY_NONE);
    }
    
    nacpFreeContext(&nacp_ctx);
    
    if (nca_ctx) free(nca_ctx);
    
    if (app_metadata) free(app_metadata);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
