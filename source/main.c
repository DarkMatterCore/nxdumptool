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
    
    
    
    
    
    goto out;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    Result rc = 0;
    
    u8 *buf = NULL;
    
    u64 base_tid = (u64)0x010082400BCC6000; // ACNH 0x01006F8002326000 | Smash 0x01006A800016E000 | Dark Souls 0x01004AB00A260000 | BotW 0x01007EF00011E000 | Untitled Goose Game 0x010082400BCC6000
    u64 update_tid = (base_tid | 0x800);
    
    Ticket base_tik = {0}, update_tik = {0};
    NcaContext *base_nca_ctx = NULL, *update_nca_ctx = NULL;
    NcmContentStorage ncm_storage_sdcard = {0}, ncm_storage_emmc = {0};
    
    char path[FS_MAX_PATH] = {0};
    LrLocationResolver resolver_sdcard = {0}, resolver_emmc = {0};
    NcmContentInfo content_info = {0};
    
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
    
    rc = ncmOpenContentStorage(&ncm_storage_sdcard, NcmStorageId_SdCard);
    if (R_FAILED(rc))
    {
        consolePrint("ncm open storage sdcard failed\n");
        goto out2;
    }
    
    consolePrint("ncm open storage sdcard succeeded\n");
    
    rc = ncmOpenContentStorage(&ncm_storage_emmc, NcmStorageId_BuiltInUser);
    if (R_FAILED(rc))
    {
        consolePrint("ncm open storage emmc failed\n");
        goto out2;
    }
    
    consolePrint("ncm open storage emmc succeeded\n");
    
    rc = lrInitialize();
    if (R_FAILED(rc))
    {
        consolePrint("lrInitialize failed\n");
        goto out2;
    }
    
    consolePrint("lrInitialize succeeded\n");
    
    rc = lrOpenLocationResolver(NcmStorageId_SdCard, &resolver_sdcard);
    if (R_FAILED(rc))
    {
        consolePrint("lrOpenLocationResolver sdcard failed\n");
        goto out2;
    }
    
    consolePrint("lrOpenLocationResolver sdcard succeeded\n");
    
    rc = lrOpenLocationResolver(NcmStorageId_BuiltInUser, &resolver_emmc);
    if (R_FAILED(rc))
    {
        consolePrint("lrOpenLocationResolver emmc failed\n");
        goto out2;
    }
    
    consolePrint("lrOpenLocationResolver emmc succeeded\n");
    
    for(u32 i = 0; i < 2; i++)
    {
        for(u32 j = 0; j < 2; j++)
        {
            NcmContentStorage *ncm_storage = (j == 0 ? &ncm_storage_sdcard : &ncm_storage_emmc);
            LrLocationResolver *resolver = (j == 0 ? &resolver_sdcard : &resolver_emmc);
            NcaContext *nca_ctx = (i == 0 ? base_nca_ctx : update_nca_ctx);
            Ticket *tik = (i == 0 ? &base_tik : &update_tik);
            
            rc = lrLrResolveProgramPath(resolver, i == 0 ? base_tid : update_tid, path);
            if (R_FAILED(rc))
            {
                consolePrint("lrLrResolveProgramPath %s,%s failed\n", i == 0 ? "base" : "update", j == 0 ? "sdcard" : "emmc");
                if (j == 0) continue;
                goto out2;
            }
            
            consolePrint("lrLrResolveProgramPath %s,%s succeeded\n", i == 0 ? "base" : "update", j == 0 ? "sdcard" : "emmc");
            
            memset(&content_info, 0, sizeof(NcmContentInfo));
            
            memmove(path, strrchr(path, '/') + 1, SHA256_HASH_SIZE + 4);
            path[SHA256_HASH_SIZE + 4] = '\0';
            
            consolePrint("Program NCA (%s,%s): %s\n", i == 0 ? "base" : "update", j == 0 ? "sdcard" : "emmc", path);
            
            for(u32 i = 0; i < SHA256_HASH_SIZE; i++)
            {
                char val = (('a' <= path[i] && path[i] <= 'f') ? (path[i] - 'a' + 0xA) : (path[i] - '0'));
                if ((i & 1) == 0) val <<= 4;
                content_info.content_id.c[i >> 1] |= val;
            }
            
            content_info.content_type = NcmContentType_Program;
            
            u64 content_size = 0;
            rc = ncmContentStorageGetSizeFromContentId(ncm_storage, (s64*)&content_size, &(content_info.content_id));
            if (R_FAILED(rc))
            {
                consolePrint("ncmContentStorageGetSizeFromContentId %s,%s failed\n", i == 0 ? "base" : "update", j == 0 ? "sdcard" : "emmc");
                goto out2;
            }
            
            consolePrint("ncmContentStorageGetSizeFromContentId %s,%s succeeded\n", i == 0 ? "base" : "update", j == 0 ? "sdcard" : "emmc");
            
            memcpy(&(content_info.size), &content_size, 6);
            
            if (!ncaInitializeContext(nca_ctx, i == 0 ? NcmStorageId_SdCard : NcmStorageId_BuiltInUser, ncm_storage, 0, &content_info, tik))
            {
                consolePrint("nca initialize ctx %s,%s failed\n", i == 0 ? "base" : "update", j == 0 ? "sdcard" : "emmc");
                goto out2;
            }
            
            consolePrint("nca initialize ctx %s,%s succeeded\n", i == 0 ? "base" : "update", j == 0 ? "sdcard" : "emmc");
            
            break;
        }
    }
    
    if (!bktrInitializeContext(&bktr_ctx, &(base_nca_ctx->fs_contexts[1]), &(update_nca_ctx->fs_contexts[1])))
    {
        consolePrint("bktr initialize ctx failed\n");
        goto out2;
    }
    
    consolePrint("bktr initialize ctx succeeded\n");
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    FILE *tmp_file = NULL;
    RomFileSystemFileEntry *romfs_file_entry = NULL;
    RomFileSystemFileEntryPatch romfs_patch = {0};
    
    romfs_file_entry = romfsGetFileEntryByPath(&(bktr_ctx.base_romfs_ctx), "/Data/rawsettings");
    if (!romfs_file_entry)
    {
        consolePrint("romfs get file entry by path failed\n");
        goto out2;
    }
    
    consolePrint("romfs get file entry by path success: %s | 0x%lX | %p\n", romfs_file_entry->name, romfs_file_entry->size, romfs_file_entry);
    
    if (!romfsReadFileEntryData(&(bktr_ctx.base_romfs_ctx), romfs_file_entry, buf, romfs_file_entry->size, 0))
    {
        consolePrint("romfs read file entry failed\n");
        goto out2;
    }
    
    consolePrint("romfs read file entry success\n");
    
    memset(buf, 0xAA, romfs_file_entry->size);
    
    if (!romfsGenerateFileEntryPatch(&(bktr_ctx.base_romfs_ctx), romfs_file_entry, buf, romfs_file_entry->size, 0, &romfs_patch))
    {
        consolePrint("romfs file entry patch failed\n");
        goto out2;
    }
    
    consolePrint("romfs file entry patch success\n");
    
    if (!ncaEncryptHeader(base_nca_ctx))
    {
        consolePrint("nca header mod not encrypted\n");
        romfsFreeFileEntryPatch(&romfs_patch);
        goto out2;
    }
    
    consolePrint("nca header mod encrypted\n");
    
    tmp_file = fopen("sdmc:/program_nca_mod.bin", "wb");
    if (!tmp_file)
    {
        consolePrint("program nca mod not saved\n");
        romfsFreeFileEntryPatch(&romfs_patch);
        goto out2;
    }
    
    u64 block_size = TEST_BUF_SIZE;
    for(u64 i = 0; i < base_nca_ctx->content_size; i += block_size)
    {
        if (block_size > (base_nca_ctx->content_size - i)) block_size = (base_nca_ctx->content_size - i);
        
        if (!ncaReadContentFile(base_nca_ctx, buf, block_size, i))
        {
            consolePrint("failed to read 0x%lX chunk from offset 0x%lX\n", block_size, i);
            fclose(tmp_file);
            romfsFreeFileEntryPatch(&romfs_patch);
            goto out2;
        }
        
        if (i == 0)
        {
            memcpy(buf, &(base_nca_ctx->header), sizeof(NcaHeader));
            for(u64 j = 0; j < 4; j++) memcpy(buf + sizeof(NcaHeader) + (j * sizeof(NcaFsHeader)), &(base_nca_ctx->fs_contexts[j].header), sizeof(NcaFsHeader));
        }
        
        romfsWriteFileEntryPatchToMemoryBuffer(&(bktr_ctx.base_romfs_ctx), &romfs_patch, buf, block_size, i);
        
        fwrite(buf, 1, block_size, tmp_file);
        fflush(tmp_file);
        
        consolePrint("wrote 0x%lX bytes to offset 0x%lX\n", block_size, i);
    }
    
    fclose(tmp_file);
    romfsFreeFileEntryPatch(&romfs_patch);
    
    goto out2;
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
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
    
    if (serviceIsActive(&(resolver_emmc.s))) serviceClose(&(resolver_emmc.s));
    if (serviceIsActive(&(resolver_sdcard.s))) serviceClose(&(resolver_sdcard.s));
    
    lrExit();
    
    if (serviceIsActive(&(ncm_storage_emmc.s))) ncmContentStorageClose(&ncm_storage_emmc);
    
    if (serviceIsActive(&(ncm_storage_sdcard.s))) ncmContentStorageClose(&ncm_storage_sdcard);
    
    if (update_nca_ctx) free(update_nca_ctx);
    
    if (base_nca_ctx) free(base_nca_ctx);
    
    if (buf) free(buf);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
