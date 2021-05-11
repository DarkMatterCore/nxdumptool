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

#include "nxdt_utils.h"
#include "nca.h"
#include "title.h"
#include "pfs.h"
#include "romfs.h"

#define BLOCK_SIZE  0x800000
#define OUTPATH     "sdmc:/systitle_dumps"

int g_argc = 0;
char **g_argv = NULL;
const char *g_appLaunchPath = NULL;

static PadState g_padState = {0};

static u8 *buf = NULL;
static FILE *filefd = NULL;
static char path[FS_MAX_PATH * 2] = {0};

static void utilsScanPads(void)
{
    padUpdate(&g_padState);
}

static u64 utilsGetButtonsDown(void)
{
    return padGetButtonsDown(&g_padState);
}

static u64 utilsGetButtonsHeld(void)
{
    return padGetButtons(&g_padState);
}

static void utilsWaitForButtonPress(u64 flag)
{
    /* Don't consider stick movement as button inputs. */
    if (!flag) flag = ~(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | \
                        HidNpadButton_StickRUp | HidNpadButton_StickRDown);
    
    while(appletMainLoop())
    {
        utilsScanPads();
        if (utilsGetButtonsDown() & flag) break;
    }
}

static void consolePrint(const char *text, ...)
{
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    consoleUpdate(NULL);
}

static void dumpPartitionFs(TitleInfo *info, NcaFsSectionContext *nca_fs_ctx)
{
    if (!buf || !info || !nca_fs_ctx) return;
    
    u32 pfs_entry_count = 0;
    PartitionFileSystemContext pfs_ctx = {0};
    PartitionFileSystemEntry *pfs_entry = NULL;
    char *pfs_entry_name = NULL;
    
    size_t path_len = 0;
    *path = '\0';
    
    if (!pfsInitializeContext(&pfs_ctx, nca_fs_ctx))
    {
        consolePrint("pfs initialize ctx failed!\n");
        goto end;
    }
    
    if (!(pfs_entry_count = pfsGetEntryCount(&pfs_ctx)))
    {
        consolePrint("pfs entry count is zero!\n");
        goto end;
    }
    
    snprintf(path, sizeof(path), OUTPATH "/%016lX - %s/%s (%s)/Section %u (%s)", info->meta_key.id, info->app_metadata->lang_entry.name, ((NcaContext*)nca_fs_ctx->nca_ctx)->content_id_str, \
             titleGetNcmContentTypeName(((NcaContext*)nca_fs_ctx->nca_ctx)->content_type), nca_fs_ctx->section_num, ncaGetFsSectionTypeName(nca_fs_ctx));
    utilsCreateDirectoryTree(path, true);
    path_len = strlen(path);
    
    for(u32 i = 0; i < pfs_entry_count; i++)
    {
        if (!(pfs_entry = pfsGetEntryByIndex(&pfs_ctx, i)) || !(pfs_entry_name = pfsGetEntryNameByIndex(&pfs_ctx, i)) || !strlen(pfs_entry_name))
        {
            consolePrint("pfs get entry / get name #%u failed!\n", i);
            goto end;
        }
        
        path[path_len] = '\0';
        strcat(path, "/");
        strcat(path, pfs_entry_name);
        utilsReplaceIllegalCharacters(path + path_len + 1, true);
        
        filefd = fopen(path, "wb");
        if (!filefd)
        {
            consolePrint("failed to create \"%s\"!\n", path);
            goto end;
        }
        
        consolePrint("dumping \"%s\"...\n", pfs_entry_name);
        
        u64 blksize = BLOCK_SIZE;
        for(u64 j = 0; j < pfs_entry->size; j += blksize)
        {
            if (blksize > (pfs_entry->size - j)) blksize = (pfs_entry->size - j);
            
            if (!pfsReadEntryData(&pfs_ctx, pfs_entry, buf, blksize, j))
            {
                consolePrint("failed to read 0x%lX block from offset 0x%lX!\n", blksize, j);
                goto end;
            }
            
            fwrite(buf, 1, blksize, filefd);
        }
        
        fclose(filefd);
        filefd = NULL;
    }
    
    consolePrint("pfs dump complete\n");
    
end:
    if (filefd)
    {
        fclose(filefd);
        remove(path);
    }
    
    if (*path) utilsCommitSdCardFileSystemChanges();
    
    pfsFreeContext(&pfs_ctx);
}

static void dumpRomFs(TitleInfo *info, NcaFsSectionContext *nca_fs_ctx)
{
    if (!buf || !info || !nca_fs_ctx) return;
    
    u64 romfs_file_table_offset = 0;
    RomFileSystemContext romfs_ctx = {0};
    RomFileSystemFileEntry *romfs_file_entry = NULL;
    
    size_t path_len = 0;
    *path = '\0';
    
    if (!romfsInitializeContext(&romfs_ctx, nca_fs_ctx))
    {
        consolePrint("romfs initialize ctx failed!\n");
        goto end;
    }
    
    snprintf(path, sizeof(path), OUTPATH "/%016lX - %s/%s (%s)/Section %u (%s)", info->meta_key.id, info->app_metadata->lang_entry.name, ((NcaContext*)nca_fs_ctx->nca_ctx)->content_id_str, \
             titleGetNcmContentTypeName(((NcaContext*)nca_fs_ctx->nca_ctx)->content_type), nca_fs_ctx->section_num, ncaGetFsSectionTypeName(nca_fs_ctx));
    utilsCreateDirectoryTree(path, true);
    path_len = strlen(path);
    
    while(romfs_file_table_offset < romfs_ctx.file_table_size)
    {
        if (!(romfs_file_entry = romfsGetFileEntryByOffset(&romfs_ctx, romfs_file_table_offset)) || \
            !romfsGeneratePathFromFileEntry(&romfs_ctx, romfs_file_entry, path + path_len, sizeof(path) - path_len, RomFileSystemPathIllegalCharReplaceType_KeepAsciiCharsOnly))
        {
            consolePrint("romfs get entry / generate path failed for 0x%lX!\n", romfs_file_table_offset);
            goto end;
        }
        
        utilsCreateDirectoryTree(path, false);
        
        filefd = fopen(path, "wb");
        if (!filefd)
        {
            consolePrint("failed to create \"%s\"!\n", path);
            goto end;
        }
        
        consolePrint("dumping \"%s\"...\n", path + path_len);
        
        u64 blksize = BLOCK_SIZE;
        for(u64 j = 0; j < romfs_file_entry->size; j += blksize)
        {
            if (blksize > (romfs_file_entry->size - j)) blksize = (romfs_file_entry->size - j);
            
            if (!romfsReadFileEntryData(&romfs_ctx, romfs_file_entry, buf, blksize, j))
            {
                consolePrint("failed to read 0x%lX block from offset 0x%lX!\n", blksize, j);
                goto end;
            }
            
            fwrite(buf, 1, blksize, filefd);
        }
        
        fclose(filefd);
        filefd = NULL;
        
        romfs_file_table_offset += ALIGN_UP(sizeof(RomFileSystemFileEntry) + romfs_file_entry->name_length, 4);
    }
    
    consolePrint("romfs dump complete\n");
    
end:
    if (filefd)
    {
        fclose(filefd);
        remove(path);
    }
    
    if (*path) utilsCommitSdCardFileSystemChanges();
    
    romfsFreeContext(&romfs_ctx);
}

static void dumpFsSection(TitleInfo *info, NcaFsSectionContext *nca_fs_ctx)
{
    if (!buf || !info || !nca_fs_ctx) return;
    
    switch(nca_fs_ctx->section_type)
    {
        case NcaFsSectionType_PartitionFs:
            dumpPartitionFs(info, nca_fs_ctx);
            break;
        case NcaFsSectionType_RomFs:
        case NcaFsSectionType_Nca0RomFs:
            dumpRomFs(info, nca_fs_ctx);
            break;
        default:
            consolePrint("invalid section type!\n");
            break;
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
    
    /* Configure input. */
    /* Up to 8 different, full controller inputs. */
    /* Individual Joy-Cons not supported. */
    padConfigureInput(8, HidNpadStyleSet_NpadFullCtrl);
    padInitializeWithMask(&g_padState, 0x1000000FFUL);
    
    consoleInit(NULL);
    
    u32 app_count = 0;
    TitleApplicationMetadata **app_metadata = NULL;
    TitleInfo *cur_title_info = NULL;
    
    u32 selected_idx = 0, menu = 0, page_size = 30, scroll = 0;
    u32 title_idx = 0, title_scroll = 0, nca_idx = 0;
    char nca_id_str[0x21] = {0};
    
    NcaContext *nca_ctx = NULL;
    
    app_metadata = titleGetApplicationMetadataEntries(true, &app_count);
    if (!app_metadata || !app_count)
    {
        consolePrint("app metadata failed\n");
        goto out2;
    }
    
    consolePrint("app metadata succeeded\n");
    
    buf = malloc(BLOCK_SIZE);
    if (!buf)
    {
        consolePrint("buf failed\n");
        goto out2;
    }
    
    consolePrint("buf succeeded\n");
    
    nca_ctx = calloc(1, sizeof(NcaContext));
    if (!nca_ctx)
    {
        consolePrint("nca ctx buf failed\n");
        goto out2;
    }
    
    consolePrint("nca ctx buf succeeded\n");
    
    utilsSleep(1);
    
    while(true)
    {
        consoleClear();
        
        printf("select a %s.", menu == 0 ? "system title to view its contents" : (menu == 1 ? "content" : "fs section"));
        printf("\npress b to %s.\n\n", menu == 0 ? "exit" : "go back");
        
        if (menu == 0)
        {
            printf("title: %u / %u\n", selected_idx + 1, app_count);
            printf("selected title: %016lX - %s\n\n", app_metadata[selected_idx]->title_id, app_metadata[selected_idx]->lang_entry.name);
        }
        
        if (menu >= 1) printf("selected title: %016lX - %s\n\n", app_metadata[title_idx]->title_id, app_metadata[title_idx]->lang_entry.name);
        
        if (menu == 2) printf("selected content: %s (%s)\n\n", nca_id_str, titleGetNcmContentTypeName(cur_title_info->content_infos[nca_idx].content_type));
        
        u32 max_val = (menu == 0 ? app_count : (menu == 1 ? cur_title_info->content_count : NCA_FS_HEADER_COUNT));
        for(u32 i = scroll; i < max_val; i++)
        {
            if (i >= (scroll + page_size)) break;
            
            printf("%s", i == selected_idx ? " -> " : "    ");
            
            if (menu == 0)
            {
                printf("%016lX - %s\n", app_metadata[i]->title_id, app_metadata[i]->lang_entry.name);
            } else
            if (menu == 1)
            {
                utilsGenerateHexStringFromData(nca_id_str, sizeof(nca_id_str), cur_title_info->content_infos[i].content_id.c, sizeof(cur_title_info->content_infos[i].content_id.c), false);
                printf("%s (%s)\n", nca_id_str, titleGetNcmContentTypeName(cur_title_info->content_infos[i].content_type));
            } else
            if (menu == 2)
            {
                printf("fs section #%u (%s)\n", i + 1, ncaGetFsSectionTypeName(&(nca_ctx->fs_ctx[i])));
            }
        }
        
        printf("\n");
        
        consoleUpdate(NULL);
        
        u64 btn_down = 0, btn_held = 0;
        while(!btn_down && !btn_held)
        {
            utilsScanPads();
            btn_down = utilsGetButtonsDown();
            btn_held = utilsGetButtonsHeld();
        }
        
        if (btn_down & HidNpadButton_A)
        {
            bool error = false;
            
            if (menu == 0)
            {
                title_idx = selected_idx;
                title_scroll = scroll;
            } else
            if (menu == 1)
            {
                nca_idx = selected_idx;
                utilsGenerateHexStringFromData(nca_id_str, sizeof(nca_id_str), cur_title_info->content_infos[nca_idx].content_id.c, sizeof(cur_title_info->content_infos[nca_idx].content_id.c), false);
            }
            
            menu++;
            
            if (menu == 1)
            {
                cur_title_info = titleGetInfoFromStorageByTitleId(NcmStorageId_BuiltInSystem, app_metadata[title_idx]->title_id);
                if (!cur_title_info)
                {
                    consolePrint("failed to get title info\n");
                    error = true;
                }
            } else
            if (menu == 2)
            {
                if (!ncaInitializeContext(nca_ctx, cur_title_info->storage_id, 0, &(cur_title_info->content_infos[nca_idx]), NULL))
                {
                    consolePrint("nca initialize ctx failed\n");
                    error = true;
                }
            } else
            if (menu == 3)
            {
                consoleClear();
                utilsChangeHomeButtonBlockStatus(true);
                dumpFsSection(cur_title_info, &(nca_ctx->fs_ctx[selected_idx]));
                utilsChangeHomeButtonBlockStatus(false);
            }
            
            if (error || menu >= 3)
            {
                consolePrint("press any button to continue\n");
                utilsWaitForButtonPress(0);
                menu--;
            } else {
                selected_idx = scroll = 0;
            }
        } else
        if ((btn_down & HidNpadButton_Down) || (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown)))
        {
            selected_idx++;
            
            if (selected_idx >= max_val)
            {
                if (btn_down & HidNpadButton_Down)
                {
                    selected_idx = scroll = 0;
                } else {
                    selected_idx = (max_val - 1);
                }
            } else
            if (selected_idx >= (scroll + (page_size / 2)) && max_val > (scroll + page_size))
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
                    selected_idx = (max_val - 1);
                    scroll = (max_val >= page_size ? (max_val - page_size) : 0);
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
            menu--;
            
            if (menu == UINT32_MAX)
            {
                break;
            } else {
                selected_idx = (menu == 0 ? title_idx : nca_idx);
                scroll = (menu == 0 ? title_scroll : 0);
            }
        }
        
        if (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) svcSleepThread(50000000); // 50 ms
    }
    
out2:
    if (menu != UINT32_MAX)
    {
        consolePrint("press any button to exit\n");
        utilsWaitForButtonPress(0);
    }
    
    if (nca_ctx) free(nca_ctx);
    
    if (buf) free(buf);
    
    if (app_metadata) free(app_metadata);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
