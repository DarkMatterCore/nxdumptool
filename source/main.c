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
#include <malloc.h>

//#include "lvgl_helper.h"

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
    FILE *fileobj;
    void *data;
    size_t data_size;
    size_t data_written;
    size_t total_size;
    bool read_error;
    bool write_error;
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
    if (!shared_data || !shared_data->data || !shared_data->total_size) return -1;
    
    u8 *buf = memalign(USB_TRANSFER_ALIGNMENT, TEST_BUF_SIZE);
    if (!buf) return -2;
    
    for(u64 offset = 0, blksize = TEST_BUF_SIZE; offset < shared_data->total_size; offset += blksize)
    {
        if (blksize > (shared_data->total_size - offset)) blksize = (shared_data->total_size - offset);
        
        shared_data->read_error = !gamecardReadStorage(buf, blksize, offset);
        if (shared_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }
        
        mutexLock(&g_fileMutex);
        
        if (shared_data->data_size) condvarWait(&g_readCondvar, &g_fileMutex);
        
        if (shared_data->write_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }
        
        memcpy(shared_data->data, buf, blksize);
        shared_data->data_size = blksize;
        
        mutexUnlock(&g_fileMutex);
        condvarWakeAll(&g_writeCondvar);
    }
    
    free(buf);
    
    return 0;
}

static int write_thread_func(void *arg)
{
    ThreadSharedData *shared_data = (ThreadSharedData*)arg;
    if (!shared_data || !shared_data->fileobj || !shared_data->data) return -1;
    
    while(shared_data->data_written < shared_data->total_size)
    {
        mutexLock(&g_fileMutex);
        
        if (!shared_data->data_size) condvarWait(&g_writeCondvar, &g_fileMutex);
        
        if (shared_data->read_error)
        {
            mutexUnlock(&g_fileMutex);
            break;
        }
        
        //shared_data->write_error = (fwrite(shared_data->data, 1, shared_data->data_size, shared_data->fileobj) != shared_data->data_size);
        
        shared_data->write_error = !usbSendFileData(shared_data->data, shared_data->data_size);
        if (!shared_data->write_error)
        {
            shared_data->data_written += shared_data->data_size;
            shared_data->data_size = 0;
        }
        
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
    
    /*lv_test();
    
    while(appletMainLoop())
    {
        lv_task_handler();
        if (lvglHelperGetExitFlag()) break;
    }*/
    
    u8 *buf = NULL;
    FILE *tmp_file = NULL;
    
    Ticket base_tik = {0}, update_tik = {0};
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    NcmContentStorage ncm_storage = {0};
    
    BktrContext bktr_ctx = {0};
    RomFileSystemFileEntry *bktr_file_entry = NULL;
    
    Result rc = 0;
    
    mkdir("sdmc:/nxdt_test", 0744);
    
    // SSBU's Base Program NCA
    NcmContentInfo base_program_content_info = {
        .content_id = {
            .c = { 0x48, 0xBB, 0xEA, 0xB6, 0x3E, 0x73, 0x88, 0x69, 0x8D, 0xE4, 0x74, 0x43, 0x49, 0x00, 0xE1, 0x04 }
        },
        .size = {
            0x00, 0x40, 0x24, 0x62, 0x03, 0x00
        },
        .content_type = NcmContentType_Program,
        .id_offset = 0
    };
    
    // SSBU's Update Program NCA
    NcmContentInfo update_program_content_info = {
        .content_id = {
            .c = { 0x83, 0x7A, 0xD9, 0x78, 0x3E, 0xB2, 0x3A, 0xFA, 0xB0, 0x4B, 0xF5, 0x71, 0x55, 0x43, 0x6E, 0x5D }
        },
        .size = {
            0x00, 0x6E, 0xE9, 0x84, 0x00, 0x00
        },
        .content_type = NcmContentType_Program,
        .id_offset = 0
    };
    
    buf = malloc(TEST_BUF_SIZE);
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
    
    rc = ncmOpenContentStorage(&ncm_storage, NcmStorageId_SdCard);
    if (R_FAILED(rc))
    {
        consolePrint("ncm open storage failed\n");
        goto out2;
    }
    
    consolePrint("ncm open storage succeeded\n");
    
    if (!ncaInitializeContext(base_nca_ctx, NcmStorageId_SdCard, &ncm_storage, 0, &base_program_content_info, &base_tik))
    {
        consolePrint("base nca initialize ctx failed\n");
        goto out2;
    }
    
    tmp_file = fopen("sdmc:/nxdt_test/base_nca_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(base_nca_ctx, 1, sizeof(NcaContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        consolePrint("base nca ctx saved\n");
    } else {
        consolePrint("base nca ctx not saved\n");
    }
    
    if (!ncaInitializeContext(update_nca_ctx, NcmStorageId_SdCard, &ncm_storage, 0, &update_program_content_info, &update_tik))
    {
        consolePrint("update nca initialize ctx failed\n");
        goto out2;
    }
    
    tmp_file = fopen("sdmc:/nxdt_test/update_nca_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(update_nca_ctx, 1, sizeof(NcaContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        consolePrint("update nca ctx saved\n");
    } else {
        consolePrint("update nca ctx not saved\n");
    }
    
    if (!bktrInitializeContext(&bktr_ctx, &(base_nca_ctx->fs_contexts[1]), &(update_nca_ctx->fs_contexts[1])))
    {
        consolePrint("bktr initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("bktr initialize ctx succeeded\n");
    
    tmp_file = fopen("sdmc:/nxdt_test/bktr_ctx.bin", "wb");
    if (tmp_file)
    {
        fwrite(&bktr_ctx, 1, sizeof(BktrContext), tmp_file);
        fclose(tmp_file);
        tmp_file = NULL;
        consolePrint("bktr ctx saved\n");
    } else {
        consolePrint("bktr ctx not saved\n");
    }
    
    bktr_file_entry = bktrGetFileEntryByPath(&bktr_ctx, "/data.arc");
    if (!bktr_file_entry)
    {
        consolePrint("bktr get file entry by path failed\n");
        goto out2;
    }
    
    consolePrint("bktr get file entry by path success: %.*s | 0x%lX\n", bktr_file_entry->name_length, bktr_file_entry->name, bktr_file_entry->size);
    
    
    
    
    
    
    /*if (!utilsCreateConcatenationFile("sdmc:/nxdt_test/gamecard.xci"))
    {
        consolePrint("create concatenationfile failed\n");
        goto out2;
    }
    
    consolePrint("create concatenationfile success\n");
    
    tmp_file = fopen("sdmc:/nxdt_test/gamecard.xci", "wb");
    if (!tmp_file)
    {
        consolePrint("open concatenationfile failed\n");
        goto out2;
    }
    
    consolePrint("open concatenationfile success\n");*/
    
    ThreadSharedData shared_data = {0};
    
    //shared_data.fileobj = tmp_file;
    shared_data.fileobj = NULL;
    shared_data.data = buf;
    shared_data.data_size = 0;
    shared_data.data_written = 0;
    gamecardGetTotalSize(&(shared_data.total_size));
    
    consolePrint("waiting for usb connection... ");
    
    u8 count = 0;
    
    while(appletMainLoop())
    {
        /* Avoid using usbIsHostAvailable() alone inside a loop, because it can hang up the system */
        consolePrint("%u ", count);
        if (usbIsHostAvailable()) break;
        utilsSleep(1);
        count++;
        if (count == 10) break;
    }
    
    if (count == 10)
    {
        consolePrint("\nusb connection not detected\n");
        goto out2;
    }
    
    consolePrint("\nusb connection detected\n");
    
    if (!usbSendFileProperties(shared_data.total_size, "gamecard.xci"))
    {
        consolePrint("usb send file properties failed\n");
        goto out2;
    }
    
    consolePrint("usb send file properties succeeded\n");
    
    thrd_t read_thread, write_thread;

    consolePrint("creating threads\n\n");
    thrd_create(&read_thread, read_thread_func, &shared_data);
    thrd_create(&write_thread, write_thread_func, &shared_data);
    
    u8 prev_time = 0;
    u64 prev_size = 0;
    u8 percent = 0;
    
    time_t start = time(NULL);
    
    while(shared_data.data_written < shared_data.total_size)
    {
        if (shared_data.read_error || shared_data.write_error) break;
        
        time_t now = time(NULL);
        struct tm *ts = localtime(&now);
        size_t size = shared_data.data_written;
        
        if (prev_time == ts->tm_sec || prev_size == size) continue;
        
        percent = (u8)((size * 100) / shared_data.total_size) + 1;
        
        prev_time = ts->tm_sec;
        prev_size = size;
        
        printf("%lu / %lu (%u%%) | Time elapsed: %lu\n", size, shared_data.total_size, percent, (time(NULL) - start));
        consoleUpdate(NULL);
    }
    
    //fclose(tmp_file);
    //tmp_file = NULL;
    
    start = (time(NULL) - start);
    
    consolePrint("\n\nwaiting for threads to join\n");
    thrd_join(read_thread, NULL);
    thrd_join(write_thread, NULL);
    
    if (shared_data.read_error || shared_data.write_error)
    {
        consolePrint("\n\nusb transfer error\n");
        goto out2;
    }
    
    consolePrint("\n\nprocess completed in %lu seconds\n", start);
    
    
    
    
    
    
    
    
    
    
    
    
    
out2:
    utilsWaitForButtonPress();
    
    if (tmp_file) fclose(tmp_file);
    
    bktrFreeContext(&bktr_ctx);
    
    if (serviceIsActive(&(ncm_storage.s))) ncmContentStorageClose(&ncm_storage);
    
    if (update_nca_ctx) free(update_nca_ctx);
    
    if (base_nca_ctx) free(base_nca_ctx);
    
    if (buf) free(buf);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
