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
    
    u8 *buf = NULL;
    
    // ACNH 0x01006F8002326000 | Smash 0x01006A800016E000 | Dark Souls 0x01004AB00A260000 | BotW 0x01007EF00011E000 | Untitled Goose Game 0x010082400BCC6000 | SMO 0x0100000000010000
    u64 base_tid = (u64)0x0100000000010000;
    u64 update_tid = titleGetPatchIdByApplicationId(base_tid);
    
    TitleInfo *base_title_info = NULL, *update_title_info = NULL;
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    Ticket base_tik = {0}, update_tik = {0};
    
    BktrContext bktr_ctx = {0};
    
    ThreadSharedData shared_data = {0};
    thrd_t read_thread, write_thread;
    
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
    
    base_title_info = titleGetInfoFromStorageByTitleId(NcmStorageId_Any, base_tid);
    update_title_info = titleGetInfoFromStorageByTitleId(NcmStorageId_Any, update_tid);
    
    if (!base_title_info || !update_title_info)
    {
        consolePrint("title info failed\n");
        goto out2;
    }
    
    consolePrint("title info succeeded\n");
    
    if (!ncaInitializeContext(base_nca_ctx, base_title_info->storage_id, (base_title_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(base_title_info, NcmContentType_Program, 0), &base_tik))
    {
        consolePrint("nca initialize base ctx failed\n");
        goto out2;
    }
    
    if (!ncaInitializeContext(update_nca_ctx, update_title_info->storage_id, (update_title_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(update_title_info, NcmContentType_Program, 0), &update_tik))
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
    
    consolePrint("hold b to cancel\n\n");
    
    start = time(NULL);
    
    while(shared_data.data_written < shared_data.total_size)
    {
        if (shared_data.read_error || shared_data.write_error) break;
        
        time_t now = time(NULL);
        struct tm *ts = localtime(&now);
        size_t size = shared_data.data_written;
        
        btn_cancel_cur_state = (utilsReadInput(UtilsInputType_Down) & KEY_B);
        
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
    consolePrint("press any button to exit\n");
    utilsWaitForButtonPress(KEY_NONE);
    
    bktrFreeContext(&bktr_ctx);
    
    if (update_nca_ctx) free(update_nca_ctx);
    
    if (base_nca_ctx) free(base_nca_ctx);
    
    if (buf) free(buf);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
