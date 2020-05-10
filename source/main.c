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

#include <dirent.h>
#include <threads.h>
#include <stdarg.h>

#include "utils.h"
#include "bktr.h"
#include "gamecard.h"
#include "usb.h"

#define TEST_BUF_SIZE   0x800000

alignas(16) u8 __nx_exception_stack[0x1000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump *ctx)
{
    LOGFILE("Exception triggered!");
    
    FILE *logfile = fopen(LOGFILE_PATH, "a+");
    if (!logfile) return;
    
    fprintf(logfile, "\r\n    error_desc: 0x%x ", ctx->error_desc);
    
    switch(ctx->error_desc)
    {
        case ThreadExceptionDesc_InstructionAbort:
            fprintf(logfile, "(InstructionAbort)");
            break;
        case ThreadExceptionDesc_MisalignedPC:
            fprintf(logfile, "(MisalignedPC)");
            break;
        case ThreadExceptionDesc_MisalignedSP:
            fprintf(logfile, "(MisalignedSP)");
            break;
        case ThreadExceptionDesc_SError:
            fprintf(logfile, "(SError)");
            break;
        case ThreadExceptionDesc_BadSVC:
            fprintf(logfile, "(BadSVC)");
            break;
        case ThreadExceptionDesc_Trap:
            fprintf(logfile, "(Trap)");
            break;
        case ThreadExceptionDesc_Other:
            fprintf(logfile, "(Other)");
            break;
        default:
            fprintf(logfile, "(Unknown)");
            break;
    }
    
    fprintf(logfile, "\r\n\r\n");
    
    if (threadExceptionIsAArch64(ctx))
    {
        for(u32 i = 0; i < 29; i++) fprintf(logfile, "    [X%d]: 0x%lx\r\n", i, ctx->cpu_gprs[i].x);
        fprintf(logfile, "\r\n");
        
        fprintf(logfile, "    fp:  0x%lx\r\n", ctx->fp.x);
        fprintf(logfile, "    lr:  0x%lx\r\n", ctx->lr.x);
        fprintf(logfile, "    sp:  0x%lx\r\n", ctx->sp.x);
        fprintf(logfile, "    pc:  0x%lx\r\n", ctx->pc.x);
        fprintf(logfile, "    far: 0x%lx\r\n", ctx->far.x);
    } else {
        for(u32 i = 0; i < 29; i++) fprintf(logfile, "    [X%d]: 0x%x\r\n", i, ctx->cpu_gprs[i].r);
        fprintf(logfile, "\r\n");
        
        fprintf(logfile, "    fp:  0x%x\r\n", ctx->fp.r);
        fprintf(logfile, "    lr:  0x%x\r\n", ctx->lr.r);
        fprintf(logfile, "    sp:  0x%x\r\n", ctx->sp.r);
        fprintf(logfile, "    pc:  0x%x\r\n", ctx->pc.r);
        fprintf(logfile, "    far: 0x%x\r\n", ctx->far.r);
    }
    
    fprintf(logfile, "\r\n");
    
    fprintf(logfile, "    pstate: 0x%x\r\n", ctx->pstate);
    fprintf(logfile, "    afsr0:  0x%x\r\n", ctx->afsr0);
    fprintf(logfile, "    afsr1:  0x%x\r\n", ctx->afsr1);
    fprintf(logfile, "    esr:    0x%x\r\n\r\n", ctx->esr);
    
    fclose(logfile);
}













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
    
    while(file_table_offset < shared_data->romfs_ctx->file_table_size)
    {
        /* Check if the transfer has been cancelled by the user */
        if (shared_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }
        
        /* Retrieve RomFS file entry information */
        shared_data->read_error = (!(file_entry = romfsGetFileEntryByOffset(shared_data->romfs_ctx, file_table_offset)) || \
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
    
    LOGFILE("nxdumptool starting.");
    
    consoleInit(NULL);
    
    consolePrint("initializing...\n");
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    u8 *buf = NULL;
    
    Ticket tik = {0};
    NcaContext *nca_ctx = NULL;
    NcmContentStorage ncm_storage = {0};
    
    Result rc = 0;
    
    ThreadSharedData shared_data = {0};
    thrd_t read_thread, write_thread;
    
    char path[FS_MAX_PATH] = {0};
    LrLocationResolver resolver = {0};
    NcmContentInfo content_info = {0};
    
    RomFileSystemContext romfs_ctx = {0};
    
    //mkdir("sdmc:/nxdt_test", 0744);
    
    buf = usbAllocatePageAlignedBuffer(TEST_BUF_SIZE);
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
    
    rc = ncmOpenContentStorage(&ncm_storage, NcmStorageId_SdCard);
    if (R_FAILED(rc))
    {
        consolePrint("ncm open storage failed\n");
        goto out2;
    }
    
    consolePrint("ncm open storage succeeded\n");
    
    rc = lrInitialize();
    if (R_FAILED(rc))
    {
        consolePrint("lrInitialize failed\n");
        goto out2;
    }
    
    consolePrint("lrInitialize succeeded\n");
    
    rc = lrOpenLocationResolver(NcmStorageId_SdCard, &resolver);
    if (R_FAILED(rc))
    {
        consolePrint("lrOpenLocationResolver failed\n");
        goto out2;
    }
    
    consolePrint("lrOpenLocationResolver succeeded\n");
    
    rc = lrLrResolveProgramPath(&resolver, (u64)0x01006F8002326000, path); // ACNH 0x01006F8002326000 | Smash 0x01006A800016E000 | Dark Souls 0x01004AB00A260000
    if (R_FAILED(rc))
    {
        consolePrint("lrLrResolveProgramPath failed\n");
        goto out2;
    }
    
    consolePrint("lrLrResolveProgramPath succeeded\n");
    
    memmove(path, strrchr(path, '/') + 1, SHA256_HASH_SIZE + 4);
    path[SHA256_HASH_SIZE + 4] = '\0';
    
    consolePrint("Program NCA: %s\n", path);
    
    for(u32 i = 0; i < SHA256_HASH_SIZE; i++)
    {
        char val = (('a' <= path[i] && path[i] <= 'f') ? (path[i] - 'a' + 0xA) : (path[i] - '0'));
        if ((i & 1) == 0) val <<= 4;
        content_info.content_id.c[i >> 1] |= val;
    }
    
    content_info.content_type = NcmContentType_Program;
    
    u64 content_size = 0;
    rc = ncmContentStorageGetSizeFromContentId(&ncm_storage, (s64*)&content_size, &(content_info.content_id));
    if (R_FAILED(rc))
    {
        consolePrint("ncmContentStorageGetSizeFromContentId failed\n");
        goto out2;
    }
    
    consolePrint("ncmContentStorageGetSizeFromContentId succeeded\n");
    
    memcpy(&(content_info.size), &content_size, 6);
    
    if (!ncaInitializeContext(nca_ctx, NcmStorageId_SdCard, &ncm_storage, 0, &content_info, &tik))
    {
        consolePrint("nca initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("nca initialize ctx succeeded\n");
    
    if (!romfsInitializeContext(&romfs_ctx, &(nca_ctx->fs_contexts[1])))
    {
        consolePrint("romfs initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("romfs initialize ctx succeeded\n");
    
    shared_data.romfs_ctx = &romfs_ctx;
    shared_data.data = buf;
    shared_data.data_size = 0;
    shared_data.data_written = 0;
    romfsGetTotalDataSize(&romfs_ctx, &(shared_data.total_size));
    
    consolePrint("waiting for usb connection... ");
    
    if (!usbStartSession())
    {
        consolePrint("failed\n");
        goto out2;
    }
    
    consolePrint("success\n");
    
    consolePrint("creating threads\n");
    thrd_create(&read_thread, read_thread_func, &shared_data);
    thrd_create(&write_thread, write_thread_func, &shared_data);
    
    u8 prev_time = 0;
    u64 prev_size = 0;
    u8 percent = 0;
    
    time_t start = time(NULL);
    
    time_t btn_cancel_start_tmr = 0, btn_cancel_end_tmr = 0;
    bool btn_cancel_cur_state = false, btn_cancel_prev_state = false;
    
    consolePrint("hold b to cancel\n\n");
    
    while(shared_data.data_written < shared_data.total_size)
    {
        if (shared_data.read_error || shared_data.write_error) break;
        
        time_t now = time(NULL);
        struct tm *ts = localtime(&now);
        size_t size = shared_data.data_written;
        
        hidScanInput();
        btn_cancel_cur_state = (utilsHidKeysAllHeld() & KEY_B);
        
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
    thrd_join(write_thread, NULL);
    
    usbEndSession();
    
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
    utilsWaitForButtonPress();
    
    lrExit();
    
    if (serviceIsActive(&(ncm_storage.s))) ncmContentStorageClose(&ncm_storage);
    
    if (nca_ctx) free(nca_ctx);
    
    if (buf) free(buf);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
