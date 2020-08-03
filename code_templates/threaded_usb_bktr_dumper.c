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
#include "bktr.h"
#include "gamecard.h"
#include "usb.h"
#include "title.h"

#define TEST_BUF_SIZE   0x800000

static Mutex g_fileMutex = 0;
static CondVar g_readCondvar = 0, g_writeCondvar = 0;

typedef struct
{
    //FILE *fileobj;
    BktrContext *bktr_ctx;
    void *data;
    size_t data_size;
    size_t data_written;
    size_t total_size;
    bool read_error;
    bool write_error;
    bool transfer_cancelled;
} ThreadSharedData;

static void consolePrint(const char *text, ...)
{
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    consoleUpdate(NULL);
}

static int read_thread_func(void *arg)
{
    ThreadSharedData *shared_data = (ThreadSharedData*)arg;
    if (!shared_data || !shared_data->data || !shared_data->total_size)
    {
        shared_data->read_error = true;
        return -1;
    }
    
    u8 *buf = malloc(TEST_BUF_SIZE);
    if (!buf)
    {
        shared_data->read_error = true;
        return -2;
    }
    
    u64 file_table_offset = 0;
    RomFileSystemFileEntry *file_entry = NULL;
    char path[FS_MAX_PATH] = {0};
    
    while(file_table_offset < shared_data->bktr_ctx->patch_romfs_ctx.file_table_size)
    {
        /* Check if the transfer has been cancelled by the user */
        if (shared_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }
        
        /* Retrieve RomFS file entry information */
        shared_data->read_error = (!(file_entry = bktrGetFileEntryByOffset(shared_data->bktr_ctx, file_table_offset)) || \
                                   !bktrGeneratePathFromFileEntry(shared_data->bktr_ctx, file_entry, path, FS_MAX_PATH, RomFileSystemPathIllegalCharReplaceType_IllegalFsChars));
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
        shared_data->read_error = !usbSendFileProperties(file_entry->size, path);
        if (shared_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }
        
        for(u64 offset = 0, blksize = TEST_BUF_SIZE; offset < file_entry->size; offset += blksize)
        {
            if (blksize > (file_entry->size - offset)) blksize = (file_entry->size - offset);
            
            /* Check if the transfer has been cancelled by the user */
            if (shared_data->transfer_cancelled)
            {
                condvarWakeAll(&g_writeCondvar);
                break;
            }
            
            /* Read current file data chunk */
            shared_data->read_error = !bktrReadFileEntryData(shared_data->bktr_ctx, file_entry, buf, blksize, offset);
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
        
        file_table_offset += ALIGN_UP(sizeof(RomFileSystemFileEntry) + file_entry->name_length, 4);
    }
    
    free(buf);
    
    return (shared_data->read_error ? -3 : 0);
}

static int write_thread_func(void *arg)
{
    ThreadSharedData *shared_data = (ThreadSharedData*)arg;
    if (!shared_data || !shared_data->data)
    {
        shared_data->write_error = true;
        return -1;
    }
    
    while(shared_data->data_written < shared_data->total_size)
    {
        /* Wait until the current file data chunk has been read */
        mutexLock(&g_fileMutex);
        
        if (!shared_data->data_size && !shared_data->read_error) condvarWait(&g_writeCondvar, &g_fileMutex);
        
        if (shared_data->read_error || shared_data->transfer_cancelled)
        {
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
    
    return 0;
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
    
    u32 selected_idx = 0, page_size = 30, cur_page = 0;
    bool exit_prompt = true;
    
    u8 *buf = NULL;
    
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    Ticket base_tik = {0}, update_tik = {0};
    
    BktrContext bktr_ctx = {0};
    
    ThreadSharedData shared_data = {0};
    thrd_t read_thread, write_thread;
    
    app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
    if (!app_metadata || !app_count)
    {
        consolePrint("app metadata failed\n");
        goto out2;
    }
    
    consolePrint("app metadata succeeded\n");
    
    buf = usbAllocatePageAlignedBuffer(TEST_BUF_SIZE);
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
    
    while(true)
    {
        consoleClear();
        printf("select a base title with an available update.\nthe updated romfs will be dumped via usb.\npress b to exit.\n\n");
        
        for(u32 i = cur_page; i < app_count; i++)
        {
            if (i >= (cur_page + page_size)) break;
            printf("%s%s (%016lX)\n", i == selected_idx ? " -> " : "    ", app_metadata[i]->lang_entry.name, app_metadata[i]->title_id);
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
                
                selected_idx = cur_page = 0;
                break;
            }
        }
        
        if (btn_down & KEY_A)
        {
            if (!titleGetUserApplicationData(app_metadata[selected_idx]->title_id, &user_app_data) || !user_app_data.app_info || !user_app_data.patch_info)
            {
                consolePrint("\nthe selected title either doesn't have available base content or update content.\n");
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
                    selected_idx = cur_page = 0;
                } else {
                    selected_idx = (app_count - 1);
                }
            } else
            if (selected_idx >= (cur_page + page_size))
            {
                cur_page += page_size;
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
                    cur_page = (app_count - (app_count % page_size));
                } else {
                    selected_idx = 0;
                }
            } else
            if (selected_idx < cur_page)
            {
                cur_page -= page_size;
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
    
    if (!ncaInitializeContext(base_nca_ctx, user_app_data.app_info->storage_id, (user_app_data.app_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(user_app_data.app_info, NcmContentType_Program, 0), &base_tik))
    {
        consolePrint("nca initialize base ctx failed\n");
        goto out2;
    }
    
    if (!ncaInitializeContext(update_nca_ctx, user_app_data.patch_info->storage_id, (user_app_data.patch_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(user_app_data.patch_info, NcmContentType_Program, 0), &update_tik))
    {
        consolePrint("nca initialize update ctx failed\n");
        goto out2;
    }
    
    if (!bktrInitializeContext(&bktr_ctx, &(base_nca_ctx->fs_contexts[1]), &(update_nca_ctx->fs_contexts[1])))
    {
        consolePrint("bktr initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("bktr initialize ctx succeeded\n");
    
    shared_data.bktr_ctx = &bktr_ctx;
    shared_data.data = buf;
    shared_data.data_size = 0;
    shared_data.data_written = 0;
    bktrGetTotalDataSize(&bktr_ctx, &(shared_data.total_size));
    
    consolePrint("waiting for usb connection... ");
    
    time_t start = time(NULL);
    bool usb_conn = false;
    
    while(true)
    {
        time_t now = time(NULL);
        if ((now - start) >= 10) break;
        consolePrint("%lu ", now - start);
        
        if ((usb_conn = usbIsReady())) break;
        utilsSleep(1);
    }
    
    consolePrint("\n");
    
    if (!usb_conn)
    {
        consolePrint("usb connection failed\n");
        goto out2;
    }
    
    consolePrint("creating threads\n");
    thrd_create(&read_thread, read_thread_func, &shared_data);
    thrd_create(&write_thread, write_thread_func, &shared_data);
    
    u8 prev_time = 0;
    u64 prev_size = 0;
    u8 percent = 0;
    
    time_t btn_cancel_start_tmr = 0, btn_cancel_end_tmr = 0;
    bool btn_cancel_cur_state = false, btn_cancel_prev_state = false;
    
    utilsChangeHomeButtonBlockStatus(true);
    
    consolePrint("hold b to cancel\n\n");
    
    start = time(NULL);
    
    while(shared_data.data_written < shared_data.total_size)
    {
        if (shared_data.read_error || shared_data.write_error) break;
        
        time_t now = time(NULL);
        struct tm *ts = localtime(&now);
        size_t size = shared_data.data_written;
        
        hidScanInput();
        btn_cancel_cur_state = (utilsHidKeysAllDown() & KEY_B);
        
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
        
        if (prev_time == ts->tm_sec || prev_size == size) continue;
        
        percent = (u8)((size * 100) / shared_data.total_size);
        
        prev_time = ts->tm_sec;
        prev_size = size;
        
        printf("%lu / %lu (%u%%) | Time elapsed: %lu\n", size, shared_data.total_size, percent, (now - start));
        consoleUpdate(NULL);
    }
    
    start = (time(NULL) - start);
    
    consolePrint("\nwaiting for threads to join\n");
    thrd_join(read_thread, NULL);
    consolePrint("read_thread done: %lu\n", time(NULL));
    thrd_join(write_thread, NULL);
    consolePrint("write_thread done: %lu\n", time(NULL));
    
    utilsChangeHomeButtonBlockStatus(false);
    
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
        utilsWaitForButtonPress(KEY_NONE);
    }
    
    bktrFreeContext(&bktr_ctx);
    
    if (update_nca_ctx) free(update_nca_ctx);
    
    if (base_nca_ctx) free(base_nca_ctx);
    
    if (buf) free(buf);
    
    if (app_metadata) free(app_metadata);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
