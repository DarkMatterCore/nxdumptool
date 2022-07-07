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
#include "romfs.h"
#include "gamecard.h"
#include "usb.h"
#include "title.h"

#define BLOCK_SIZE  USB_TRANSFER_BUFFER_SIZE

bool g_borealisInitialized = false;

static PadState g_padState = {0};

static Mutex g_fileMutex = 0;
static CondVar g_readCondvar = 0, g_writeCondvar = 0;

typedef struct
{
    //FILE *fileobj;
    RomFileSystemContext *romfs_ctx;
    void *data;
    size_t data_size;
    size_t data_written;
    size_t total_size;
    bool read_error;
    bool write_error;
    bool transfer_cancelled;
} ThreadSharedData;

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

static void read_thread_func(void *arg)
{
    ThreadSharedData *shared_data = (ThreadSharedData*)arg;
    if (!shared_data || !shared_data->data || !shared_data->total_size || !shared_data->romfs_ctx)
    {
        shared_data->read_error = true;
        goto end;
    }

    u8 *buf = malloc(BLOCK_SIZE);
    if (!buf)
    {
        shared_data->read_error = true;
        goto end;
    }

    RomFileSystemFileEntry *file_entry = NULL;
    char path[FS_MAX_PATH] = {0};

    /* Reset current file table offset. */
    romfsResetFileTableOffset(shared_data->romfs_ctx);

    /* Loop through all file entries. */
    while(shared_data->data_written < shared_data->total_size && romfsCanMoveToNextFileEntry(shared_data->romfs_ctx))
    {
        /* Check if the transfer has been cancelled by the user */
        if (shared_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Retrieve RomFS file entry information */
        /*shared_data->read_error = (!(file_entry = romfsGetCurrentFileEntry(shared_data->romfs_ctx)));
        if (shared_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        bool updated = false;
        if (!romfsIsFileEntryUpdated(shared_data->romfs_ctx, file_entry, &updated) || !updated)
        {
            shared_data->read_error = !romfsMoveToNextFileEntry(shared_data->romfs_ctx);
            if (shared_data->read_error)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            continue;
        }

        shared_data->read_error = !romfsGeneratePathFromFileEntry(shared_data->romfs_ctx, file_entry, path, FS_MAX_PATH, RomFileSystemPathIllegalCharReplaceType_IllegalFsChars);*/

        shared_data->read_error = (!(file_entry = romfsGetCurrentFileEntry(shared_data->romfs_ctx)) || \
                                    !romfsGeneratePathFromFileEntry(shared_data->romfs_ctx, file_entry, path, FS_MAX_PATH, RomFileSystemPathIllegalCharReplaceType_IllegalFsChars));
        if (shared_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        /* Wait until the previous file data chunk has been written */
        mutexLock(&g_fileMutex);
        if (shared_data->data_size && !shared_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);
        mutexUnlock(&g_fileMutex);
        if (shared_data->write_error) break;

        /* Send current file properties */
        shared_data->read_error = !usbSendFilePropertiesCommon(file_entry->size, path);
        if (shared_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }

        for(u64 offset = 0, blksize = BLOCK_SIZE; offset < file_entry->size; offset += blksize)
        {
            if (blksize > (file_entry->size - offset)) blksize = (file_entry->size - offset);

            /* Check if the transfer has been cancelled by the user */
            if (shared_data->transfer_cancelled)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Read current file data chunk */
            shared_data->read_error = !romfsReadFileEntryData(shared_data->romfs_ctx, file_entry, buf, blksize, offset);
            if (shared_data->read_error)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }

            /* Wait until the previous file data chunk has been written */
            mutexLock(&g_fileMutex);

            if (shared_data->data_size && !shared_data->write_error) condvarWait(&g_readCondvar, &g_fileMutex);

            if (shared_data->write_error)
            {
                mutexUnlock(&g_fileMutex);
                break;
            }

            /* Copy current file data chunk to the shared buffer */
            memcpy(shared_data->data, buf, blksize);
            shared_data->data_size = blksize;

            /* Wake up the write thread to continue writing data */
            mutexUnlock(&g_fileMutex);
            condvarWakeAll(&g_writeCondvar);
        }

        if (shared_data->read_error || shared_data->write_error || shared_data->transfer_cancelled) break;

        /* Move to the next file entry. */
        shared_data->read_error = !romfsMoveToNextFileEntry(shared_data->romfs_ctx);
        if (shared_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }
    }

    free(buf);

end:
    threadExit();
}

static void write_thread_func(void *arg)
{
    ThreadSharedData *shared_data = (ThreadSharedData*)arg;
    if (!shared_data || !shared_data->data)
    {
        shared_data->write_error = true;
        goto end;
    }

    while(shared_data->data_written < shared_data->total_size)
    {
        /* Wait until the current file data chunk has been read */
        mutexLock(&g_fileMutex);

        if (!shared_data->data_size && !shared_data->read_error) condvarWait(&g_writeCondvar, &g_fileMutex);

        if (shared_data->read_error || shared_data->transfer_cancelled)
        {
            if (shared_data->transfer_cancelled) usbCancelFileTransfer();
            mutexUnlock(&g_fileMutex);
            break;
        }

        //shared_data->write_error = (fwrite(shared_data->data, 1, shared_data->data_size, shared_data->fileobj) != shared_data->data_size);

        /* Write current file data chunk */
        shared_data->write_error = !usbSendFileData(shared_data->data, shared_data->data_size);
        if (!shared_data->write_error)
        {
            shared_data->data_written += shared_data->data_size;
            shared_data->data_size = 0;
        }

        /* Wake up the read thread to continue reading data */
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_readCondvar);

        if (shared_data->write_error) break;
    }

end:
    threadExit();
}

u8 get_program_id_offset(TitleInfo *info, u32 program_count)
{
    if (program_count <= 1) return 0;

    u8 id_offset = 0;
    u32 selected_idx = 0, page_size = 30, scroll = 0;
    char nca_id_str[0x21] = {0};
    bool applet_status = true;

    NcmContentInfo **content_infos = calloc(program_count, sizeof(NcmContentInfo*));
    if (!content_infos) return 0;

    for(u32 i = 0, j = 0; i < info->content_count && j < program_count; i++)
    {
        if (info->content_infos[i].content_type != NcmContentType_Program) continue;
        content_infos[j++] = &(info->content_infos[i]);
    }

    while((applet_status = appletMainLoop()))
    {
        consoleClear();
        printf("select a program nca to dump the romfs from.\n\n");

        for(u32 i = scroll; i < program_count; i++)
        {
            if (i >= (scroll + page_size)) break;
            utilsGenerateHexStringFromData(nca_id_str, sizeof(nca_id_str), content_infos[i]->content_id.c, sizeof(content_infos[i]->content_id.c), false);
            printf("%s%s.nca (ID offset #%u)\n", i == selected_idx ? " -> " : "    ", nca_id_str, content_infos[i]->id_offset);
        }

        printf("\n");

        consoleUpdate(NULL);

        u64 btn_down = 0, btn_held = 0;
        while((applet_status = appletMainLoop()))
        {
            utilsScanPads();
            btn_down = utilsGetButtonsDown();
            btn_held = utilsGetButtonsHeld();
            if (btn_down || btn_held) break;
        }

        if (!applet_status) break;

        if (btn_down & HidNpadButton_A)
        {
            id_offset = content_infos[selected_idx]->id_offset;
            break;
        } else
        if ((btn_down & HidNpadButton_Down) || (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown)))
        {
            selected_idx++;

            if (selected_idx >= program_count)
            {
                if (btn_down & HidNpadButton_Down)
                {
                    selected_idx = scroll = 0;
                } else {
                    selected_idx = (program_count - 1);
                }
            } else
            if (selected_idx >= (scroll + (page_size / 2)) && program_count > (scroll + page_size))
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
                    selected_idx = (program_count - 1);
                    scroll = (program_count >= page_size ? (program_count - page_size) : 0);
                } else {
                    selected_idx = 0;
                }
            } else
            if (selected_idx < (scroll + (page_size / 2)) && scroll > 0)
            {
                scroll--;
            }
        }

        if (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) svcSleepThread(50000000); // 50 ms
    }

    free(content_infos);

    return (applet_status ? id_offset : (u8)program_count);
}

static TitleInfo *get_latest_patch_info(TitleInfo *patch_info)
{
    if (!patch_info || patch_info->meta_key.type != NcmContentMetaType_Patch) return NULL;

    TitleInfo *output = patch_info, *tmp = patch_info;
    u32 highest_version = patch_info->version.value;

    while((tmp = tmp->next) != NULL)
    {
        if (tmp->version.value > highest_version)
        {
            output = tmp;
            highest_version = output->version.value;
        }
    }

    return output;
}

int main(int argc, char *argv[])
{
    int ret = 0;

    if (!utilsInitializeResources(argc, (const char**)argv))
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
    TitleUserApplicationData user_app_data = {0};

    u32 selected_idx = 0, page_size = 30, scroll = 0;
    bool applet_status = true, exit_prompt = true;

    u8 *buf = NULL;

    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;

    RomFileSystemContext romfs_ctx = {0};

    ThreadSharedData shared_data = {0};
    Thread read_thread = {0}, write_thread = {0};

    app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
    if (!app_metadata || !app_count)
    {
        consolePrint("app metadata failed\n");
        goto out2;
    }

    consolePrint("app metadata succeeded\n");

    buf = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    if (!buf)
    {
        consolePrint("buf failed\n");
        goto out2;
    }

    consolePrint("buf succeeded\n");

    base_nca_ctx = calloc(1, sizeof(NcaContext));
    if (!base_nca_ctx)
    {
        consolePrint("base nca ctx buf failed\n");
        goto out2;
    }

    consolePrint("base nca ctx buf succeeded\n");

    update_nca_ctx = calloc(1, sizeof(NcaContext));
    if (!update_nca_ctx)
    {
        consolePrint("update nca ctx buf failed\n");
        goto out2;
    }

    consolePrint("update nca ctx buf succeeded\n");

    utilsSleep(1);

    while((applet_status = appletMainLoop()))
    {
        consoleClear();
        printf("select a user application to dump its romfs.\nif an update is available, patch romfs data will be dumped instead.\ndata will be transferred via usb.\npress b to exit.\n\n");
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
        while((applet_status = appletMainLoop()))
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

        if (!applet_status) break;

        if (btn_down & HidNpadButton_A)
        {
            if (!titleGetUserApplicationData(app_metadata[selected_idx]->title_id, &user_app_data) || !user_app_data.app_info)
            {
                consolePrint("\nthe selected title doesn't have available base content.\n");
                utilsSleep(3);
                titleFreeUserApplicationData(&user_app_data);
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

    if (!applet_status)
    {
        exit_prompt = false;
        goto out2;
    }

    u32 program_count = titleGetContentCountByType(user_app_data.app_info, NcmContentType_Program);
    if (!program_count)
    {
        consolePrint("base app has no program ncas!\n");
        goto out2;
    }

    u8 program_id_offset = get_program_id_offset(user_app_data.app_info, program_count);
    if (program_id_offset >= program_count)
    {
        exit_prompt = false;
        goto out2;
    }

    consoleClear();
    consolePrint("selected title:\n%s (%016lX)\n\n", app_metadata[selected_idx]->lang_entry.name, app_metadata[selected_idx]->title_id + program_id_offset);

    if (!ncaInitializeContext(base_nca_ctx, user_app_data.app_info->storage_id, (user_app_data.app_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(user_app_data.app_info, NcmContentType_Program, program_id_offset), user_app_data.app_info->version.value, NULL))
    {
        consolePrint("nca initialize base ctx failed\n");
        goto out2;
    }

    TitleInfo *latest_patch = NULL;
    if (user_app_data.patch_info) latest_patch = get_latest_patch_info(user_app_data.patch_info);

    if (!latest_patch && (!base_nca_ctx->fs_ctx[1].enabled || (base_nca_ctx->fs_ctx[1].section_type != NcaFsSectionType_RomFs && base_nca_ctx->fs_ctx[1].section_type != NcaFsSectionType_Nca0RomFs)))
    {
        consolePrint("base app has no valid romfs and no updates could be found\n");
        goto out2;
    }

    if (base_nca_ctx->fs_ctx[1].has_sparse_layer && (!latest_patch || latest_patch->version.value < user_app_data.app_info->version.value))
    {
        consolePrint("base app is a sparse title and no v%u or greater update could be found\n", user_app_data.app_info->version.value);
        goto out2;
    }

    if (latest_patch)
    {
        consolePrint("using patch romfs with update v%u\n", latest_patch->version.value);

        if (!ncaInitializeContext(update_nca_ctx, latest_patch->storage_id, (latest_patch->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
            titleGetContentInfoByTypeAndIdOffset(latest_patch, NcmContentType_Program, program_id_offset), latest_patch->version.value, NULL))
        {
            consolePrint("nca initialize update ctx failed\n");
            goto out2;
        }

        if (!romfsInitializeContext(&romfs_ctx, &(base_nca_ctx->fs_ctx[1]), &(update_nca_ctx->fs_ctx[1])))
        {
            consolePrint("romfs initialize ctx failed (update)\n");
            goto out2;
        }
    } else {
        consolePrint("using base romfs only\n");

        if (!romfsInitializeContext(&romfs_ctx, &(base_nca_ctx->fs_ctx[1]), NULL))
        {
            consolePrint("romfs initialize ctx failed (base)\n");
            goto out2;
        }
    }

    shared_data.romfs_ctx = &romfs_ctx;
    //if (!romfsGetTotalDataSize(&romfs_ctx, true, &(shared_data.total_size)) || !shared_data.total_size)
    if (!romfsGetTotalDataSize(&romfs_ctx, false, &(shared_data.total_size)) || !shared_data.total_size)
    {
        consolePrint("failed to retrieve total romfs size\n");
        goto out2;
    }

    consolePrint("romfs initialize ctx succeeded\n");

    shared_data.data = buf;
    shared_data.data_size = 0;
    shared_data.data_written = 0;

    consolePrint("waiting for usb connection... ");

    time_t start = time(NULL);
    u8 usb_host_speed = UsbHostSpeed_None;

    while((applet_status = appletMainLoop()))
    {
        time_t now = time(NULL);
        if ((now - start) >= 10) break;
        consolePrint("%lu ", now - start);

        if ((usb_host_speed = usbIsReady())) break;
        utilsSleep(1);
    }

    if (!applet_status)
    {
        exit_prompt = false;
        goto out2;
    }

    consolePrint("\n");

    if (!usb_host_speed)
    {
        consolePrint("usb connection failed\n");
        goto out2;
    }

    consolePrint("creating threads\n");
    utilsCreateThread(&read_thread, read_thread_func, &shared_data, 2);
    utilsCreateThread(&write_thread, write_thread_func, &shared_data, 2);

    u8 prev_time = 0;
    u64 prev_size = 0;
    u8 percent = 0;

    time_t btn_cancel_start_tmr = 0, btn_cancel_end_tmr = 0;
    bool btn_cancel_cur_state = false, btn_cancel_prev_state = false;

    utilsSetLongRunningProcessState(true);

    consolePrint("hold b to cancel\n\n");

    start = time(NULL);

    while(shared_data.data_written < shared_data.total_size)
    {
        if (shared_data.read_error || shared_data.write_error) break;

        struct tm ts = {0};
        time_t now = time(NULL);
        localtime_r(&now, &ts);

        size_t size = shared_data.data_written;

        utilsScanPads();
        btn_cancel_cur_state = (utilsGetButtonsHeld() & HidNpadButton_B);

        if (btn_cancel_cur_state && btn_cancel_cur_state != btn_cancel_prev_state)
        {
            btn_cancel_start_tmr = now;
        } else
        if (btn_cancel_cur_state && btn_cancel_cur_state == btn_cancel_prev_state)
        {
            btn_cancel_end_tmr = now;
            if ((btn_cancel_end_tmr - btn_cancel_start_tmr) >= 3)
            {
                mutexLock(&g_fileMutex);
                shared_data.transfer_cancelled = true;
                mutexUnlock(&g_fileMutex);
                break;
            }
        } else {
            btn_cancel_start_tmr = btn_cancel_end_tmr = 0;
        }

        btn_cancel_prev_state = btn_cancel_cur_state;

        if (prev_time == ts.tm_sec || prev_size == size) continue;

        percent = (u8)((size * 100) / shared_data.total_size);

        prev_time = ts.tm_sec;
        prev_size = size;

        printf("%lu / %lu (%u%%) | Time elapsed: %lu\n", size, shared_data.total_size, percent, (now - start));
        consoleUpdate(NULL);
    }

    start = (time(NULL) - start);

    consolePrint("\nwaiting for threads to join\n");
    utilsJoinThread(&read_thread);
    consolePrint("read_thread done: %lu\n", time(NULL));
    utilsJoinThread(&write_thread);
    consolePrint("write_thread done: %lu\n", time(NULL));

    utilsSetLongRunningProcessState(false);

    if (shared_data.read_error || shared_data.write_error)
    {
        consolePrint("usb transfer error\n");
        goto out2;
    }

    if (shared_data.transfer_cancelled)
    {
        consolePrint("process cancelled\n");
        goto out2;
    }

    consolePrint("process completed in %lu seconds\n", start);

out2:
    if (exit_prompt)
    {
        consolePrint("press any button to exit\n");
        utilsWaitForButtonPress(0);
    }

    romfsFreeContext(&romfs_ctx);

    if (update_nca_ctx) free(update_nca_ctx);

    if (base_nca_ctx) free(base_nca_ctx);

    titleFreeUserApplicationData(&user_app_data);

    if (buf) free(buf);

    if (app_metadata) free(app_metadata);

out:
    utilsCloseResources();

    consoleExit(NULL);

    return ret;
}
