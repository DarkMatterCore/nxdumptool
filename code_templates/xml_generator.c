/*
 * main.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include "gamecard.h"
#include "title.h"
#include "cnmt.h"
#include "program_info.h"
#include "nacp.h"
#include "legal_info.h"

int g_argc = 0;
char **g_argv = NULL;
const char *g_appLaunchPath = NULL;

static void consolePrint(const char *text, ...)
{
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    consoleUpdate(NULL);
}

static void writeFile(void *buf, size_t buf_size, const char *path)
{
    FILE *fd = fopen(path, "wb");
    if (fd)
    {
        fwrite(buf, 1, buf_size, fd);
        fclose(fd);
        utilsCommitSdCardFileSystemChanges();
    }
}

int main(int argc, char *argv[])
{
    g_argc = argc;
    g_argv = argv;
    
    int ret = 0;
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    consoleInit(NULL);
    
    u32 app_count = 0;
    TitleApplicationMetadata **app_metadata = NULL;
    TitleUserApplicationData user_app_data = {0};
    
    u32 selected_idx = 0, page_size = 30, scroll = 0;
    bool exit_prompt = true;
    
    NcaContext *nca_ctx = NULL;
    Ticket tik = {0};
    
    u32 meta_idx = 0;
    ContentMetaContext cnmt_ctx = {0};
    
    u32 program_count = 0, program_idx = 0;
    ProgramInfoContext *program_info_ctx = NULL;
    
    u32 control_count = 0, control_idx = 0;
    NacpContext *nacp_ctx = NULL;
    
    u32 legal_info_count = 0, legal_info_idx = 0;
    LegalInfoContext *legal_info_ctx = NULL;
    
    char path[FS_MAX_PATH] = {0};
    
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
        printf("select a user application to generate xmls for.\npress b to exit.\n\n");
        printf("title: %u / %u\n", selected_idx + 1, app_count);
        printf("selected title: %016lX - %s\n\n", app_metadata[selected_idx]->title_id, app_metadata[selected_idx]->lang_entry.name);
        
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
            utilsScanPads();
            btn_down = utilsGetButtonsDown();
            btn_held = utilsGetButtonsHeld();
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
        
        if (btn_down & HidNpadButton_A)
        {
            if (!titleGetUserApplicationData(app_metadata[selected_idx]->title_id, &user_app_data) || !user_app_data.app_info)
            {
                consolePrint("\nthe selected title doesn't have available base content.\n");
                utilsSleep(3);
                continue;
            }
            
            break;
        } else
        if ((btn_down & HidNpadButton_Down) || (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown)))
        {
            selected_idx++;
            
            if (selected_idx >= app_count)
            {
                if (btn_down & HidNpadButton_Down)
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
        if ((btn_down & HidNpadButton_Up) || (btn_held & (HidNpadButton_StickLUp | HidNpadButton_StickRUp)))
        {
            selected_idx--;
            
            if (selected_idx == UINT32_MAX)
            {
                if (btn_down & HidNpadButton_Up)
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
        if (btn_down & HidNpadButton_B)
        {
            exit_prompt = false;
            goto out2;
        }
        
        if (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) svcSleepThread(50000000); // 50 ms
    }
    
    consoleClear();
    consolePrint("selected title:\n%s (%016lX)\n\n", app_metadata[selected_idx]->lang_entry.name, app_metadata[selected_idx]->title_id);
    
    nca_ctx = calloc(user_app_data.app_info->content_count, sizeof(NcaContext));
    if (!nca_ctx)
    {
        consolePrint("nca ctx calloc failed\n");
        goto out2;
    }
    
    consolePrint("nca ctx calloc succeeded\n");
    
    meta_idx = (user_app_data.app_info->content_count - 1);
    
    program_count = titleGetContentCountByType(user_app_data.app_info, NcmContentType_Program);
    if (program_count && !(program_info_ctx = calloc(program_count, sizeof(ProgramInfoContext))))
    {
        consolePrint("program info ctx calloc failed\n");
        goto out2;
    }
    
    control_count = titleGetContentCountByType(user_app_data.app_info, NcmContentType_Control);
    if (control_count && !(nacp_ctx = calloc(control_count, sizeof(NacpContext))))
    {
        consolePrint("nacp ctx calloc failed\n");
        goto out2;
    }
    
    legal_info_count = titleGetContentCountByType(user_app_data.app_info, NcmContentType_LegalInformation);
    if (legal_info_count && !(legal_info_ctx = calloc(legal_info_count, sizeof(LegalInfoContext))))
    {
        consolePrint("legal info ctx calloc failed\n");
        goto out2;
    }
    
    for(u32 i = 0, j = 0; i < user_app_data.app_info->content_count; i++)
    {
        // set meta nca as the last nca
        NcmContentInfo *content_info = &(user_app_data.app_info->content_infos[i]);
        if (content_info->content_type == NcmContentType_Meta) continue;
        
        if (!ncaInitializeContext(&(nca_ctx[j]), user_app_data.app_info->storage_id, (user_app_data.app_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
            content_info, &tik))
        {
            consolePrint("%s #%u initialize nca ctx failed\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
            goto out2;
        }
        
        consolePrint("%s #%u initialize nca ctx succeeded\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
        
        switch(content_info->content_type)
        {
            case NcmContentType_Program:
                if (!programInfoInitializeContext(&(program_info_ctx[program_idx]), &(nca_ctx[j])))
                {
                    consolePrint("initialize program info ctx failed (%s)\n", nca_ctx[j].content_id_str);
                    goto out2;
                }
                
                nca_ctx[j].content_type_ctx = &(program_info_ctx[program_idx++]);
                
                break;
            case NcmContentType_Control:
                if (!nacpInitializeContext(&(nacp_ctx[control_idx]), &(nca_ctx[j])))
                {
                    consolePrint("initialize nacp ctx failed (%s)\n", nca_ctx[j].content_id_str);
                    goto out2;
                }
                
                nca_ctx[j].content_type_ctx = &(nacp_ctx[control_idx++]);
                
                break;
            case NcmContentType_LegalInformation:
                if (!legalInfoInitializeContext(&(legal_info_ctx[legal_info_idx]), &(nca_ctx[j])))
                {
                    consolePrint("initialize legal info ctx failed (%s)\n", nca_ctx[j].content_id_str);
                    goto out2;
                }
                
                nca_ctx[j].content_type_ctx = &(legal_info_ctx[legal_info_idx++]);
                
                break;
            default:
                break;
        }
        
        j++;
    }
    
    if (!ncaInitializeContext(&(nca_ctx[meta_idx]), user_app_data.app_info->storage_id, (user_app_data.app_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(user_app_data.app_info, NcmContentType_Meta, 0), &tik))
    {
        consolePrint("Meta nca initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("Meta nca initialize ctx succeeded\n");
    
    if (!cnmtInitializeContext(&cnmt_ctx, &(nca_ctx[meta_idx])))
    {
        consolePrint("cnmt initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("cnmt initialize ctx succeeded\n");
    
    sprintf(path, "sdmc:/at_xml/%016lX", app_metadata[selected_idx]->title_id);
    utilsCreateDirectoryTree(path, true);
    
    if (cnmtGenerateAuthoringToolXml(&cnmt_ctx, nca_ctx, user_app_data.app_info->content_count))
    {
        consolePrint("cnmt xml succeeded\n");
        
        //sprintf(path, "sdmc:/at_xml/%016lX/%s.cnmt", app_metadata[selected_idx]->title_id, cnmt_ctx.nca_ctx->content_id_str);
        //writeFile(cnmt_ctx.raw_data, cnmt_ctx.raw_data_size, path);
        
        sprintf(path, "sdmc:/at_xml/%016lX/%s.cnmt.xml", app_metadata[selected_idx]->title_id, cnmt_ctx.nca_ctx->content_id_str);
        writeFile(cnmt_ctx.authoring_tool_xml, cnmt_ctx.authoring_tool_xml_size, path);
    } else {
        consolePrint("cnmt xml failed\n");
    }
    
    for(u32 i = 0; i < user_app_data.app_info->content_count; i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        
        if (!cur_nca_ctx->content_type_ctx || cur_nca_ctx->content_type == NcmContentType_Meta) continue;
        
        switch(cur_nca_ctx->content_type)
        {
            case NcmContentType_Program:
            {
                ProgramInfoContext *cur_program_info_ctx = (ProgramInfoContext*)cur_nca_ctx->content_type_ctx;
                if (!programInfoGenerateAuthoringToolXml(cur_program_info_ctx))
                {
                    consolePrint("program info xml failed (%s | id offset #%u)\n", cur_nca_ctx->content_id_str, cur_nca_ctx->id_offset);
                    goto out2;
                }
                
                consolePrint("program info xml succeeded (%s | id offset #%u)\n", cur_nca_ctx->content_id_str, cur_nca_ctx->id_offset);
                
                sprintf(path, "sdmc:/at_xml/%016lX/%s.programinfo.xml", app_metadata[selected_idx]->title_id, cur_nca_ctx->content_id_str);
                writeFile(cur_program_info_ctx->authoring_tool_xml, cur_program_info_ctx->authoring_tool_xml_size, path);
                
                break;
            }
            case NcmContentType_Control:
            {
                NacpContext *cur_nacp_ctx = (NacpContext*)cur_nca_ctx->content_type_ctx;
                if (!nacpGenerateAuthoringToolXml(cur_nacp_ctx, user_app_data.app_info->version.value, cnmtGetRequiredTitleVersion(&cnmt_ctx)))
                {
                    consolePrint("nacp xml failed (%s | id offset #%u)\n", cur_nca_ctx->content_id_str, cur_nca_ctx->id_offset);
                    goto out2;
                }
                
                consolePrint("nacp xml succeeded (%s | id offset #%u)\n", cur_nca_ctx->content_id_str, cur_nca_ctx->id_offset);
                
                //sprintf(path, "sdmc:/at_xml/%016lX/%s.nacp", app_metadata[selected_idx]->title_id, cur_nca_ctx->content_id_str);
                //writeFile(cur_nacp_ctx->data, sizeof(_NacpStruct), path);
                
                sprintf(path, "sdmc:/at_xml/%016lX/%s.nacp.xml", app_metadata[selected_idx]->title_id, cur_nca_ctx->content_id_str);
                writeFile(cur_nacp_ctx->authoring_tool_xml, cur_nacp_ctx->authoring_tool_xml_size, path);
                
                for(u8 j = 0; j < cur_nacp_ctx->icon_count; j++)
                {
                    NacpIconContext *icon_ctx = &(cur_nacp_ctx->icon_ctx[j]);
                    sprintf(path, "sdmc:/at_xml/%016lX/%s.nx.%s.jpg", app_metadata[selected_idx]->title_id, cur_nca_ctx->content_id_str, nacpGetLanguageString(icon_ctx->language));
                    writeFile(icon_ctx->icon_data, icon_ctx->icon_size, path);
                }
                
                break;
            }
            case NcmContentType_LegalInformation:
            {
                LegalInfoContext *cur_legal_info_ctx = (LegalInfoContext*)cur_nca_ctx->content_type_ctx;
                
                sprintf(path, "sdmc:/at_xml/%016lX/%s.legalinfo.xml", app_metadata[selected_idx]->title_id, cur_nca_ctx->content_id_str);
                writeFile(cur_legal_info_ctx->authoring_tool_xml, cur_legal_info_ctx->authoring_tool_xml_size, path);
                
                consolePrint("legal info xml succeeded (%s | id offset #%u)\n", cur_nca_ctx->content_id_str, cur_nca_ctx->id_offset);
                
                break;
            }
            default:
                break;
        }
    }
    
out2:
    if (exit_prompt)
    {
        consolePrint("press any button to exit\n");
        utilsWaitForButtonPress(0);
    }
    
    if (legal_info_ctx)
    {
        for(u32 i = 0; i < legal_info_count; i++) legalInfoFreeContext(&(legal_info_ctx[i]));
        free(legal_info_ctx);
    }
    
    if (nacp_ctx)
    {
        for(u32 i = 0; i < control_count; i++) nacpFreeContext(&(nacp_ctx[i]));
        free(nacp_ctx);
    }
    
    if (program_info_ctx)
    {
        for(u32 i = 0; i < program_count; i++) programInfoFreeContext(&(program_info_ctx[i]));
        free(program_info_ctx);
    }
    
    cnmtFreeContext(&cnmt_ctx);
    
    if (nca_ctx) free(nca_ctx);
    
    if (app_metadata) free(app_metadata);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
