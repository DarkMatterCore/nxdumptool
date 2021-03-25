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
#include "usb.h"
#include "title.h"
#include "crc32_fast.h"

#define BLOCK_SIZE  USB_TRANSFER_BUFFER_SIZE

int g_argc = 0;
char **g_argv = NULL;
const char *g_appLaunchPath = NULL;

/* Type definitions. */

typedef void (*MenuElementOptionFunction)(u32 idx);

typedef struct {
    u32 selected;                           ///< Used to keep track of the selected option.
    MenuElementOptionFunction options_func; ///< Pointer to a function to be called each time a new option is selected. Should be set to NULL if not used.
    const char **options;                   ///< Pointer to multiple char pointers with strings representing options. Last element must be set to NULL.
} MenuElementOption;

typedef bool (*MenuElementFunction)(void);

typedef struct {
    const char *str;                    ///< Pointer to a string to be printed for this menu element.
    void *child_menu;                   ///< Pointer to a child Menu element. Must be set to NULL if task_func != NULL.
    MenuElementFunction task_func;      ///< Pointer to a function to be called by this element. Must be set to NULL if child_menu != NULL.
    MenuElementOption *element_options; ///< Options for this menu element. Should be set to NULL if not used.
} MenuElement;

typedef struct _Menu {
    struct _Menu *parent;   ///< Set to NULL in the root menu element.
    u32 selected, scroll;   ///< Used to keep track of the selected element and scroll values.
    MenuElement **elements; ///< Element info from this menu. Last element must be set to NULL.
} Menu;

typedef struct
{
    void *data;
    size_t data_size;
    size_t data_written;
    size_t total_size;
    bool read_error;
    bool write_error;
    bool transfer_cancelled;
    u32 xci_crc, full_xci_crc;
} ThreadSharedData;

/* Function prototypes. */

static void consolePrint(const char *text, ...);

static u32 menuGetElementCount(const Menu *menu);

static bool sendGameCardKeyAreaViaUsb(void);
static bool sendGameCardCertificateViaUsb(void);
static bool sendGameCardImageViaUsb(void);

static void changeKeyAreaOption(u32 idx);
static void changeCertificateOption(u32 idx);
static void changeTrimOption(u32 idx);
static void changeCrcOption(u32 idx);

static void read_thread_func(void *arg);
static void write_thread_func(void *arg);

/* Global variables. */

static bool g_appendKeyArea = false, g_keepCertificate = false, g_trimDump = false, g_calcCrc = false;

static const char *g_xciOptions[] = { "no", "yes", NULL };

static MenuElement *g_xciMenuElements[] = {
    &(MenuElement){
        .str = "start dump",
        .child_menu = NULL,
        .task_func = &sendGameCardImageViaUsb,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "append key area",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .options_func = &changeKeyAreaOption,
            .options = g_xciOptions
        }
    },
    &(MenuElement){
        .str = "keep certificate",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .options_func = &changeCertificateOption,
            .options = g_xciOptions
        }
    },
    &(MenuElement){
        .str = "trim dump",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .options_func = &changeTrimOption,
            .options = g_xciOptions
        }
    },
    &(MenuElement){
        .str = "calculate crc32",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .options_func = &changeCrcOption,
            .options = g_xciOptions
        }
    },
    NULL
};

static Menu g_xciMenu = {
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_xciMenuElements
};

static MenuElement *g_rootMenuElements[] = {
    &(MenuElement){
        .str = "dump key area",
        .child_menu = NULL,
        .task_func = &sendGameCardKeyAreaViaUsb,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "dump certificate",
        .child_menu = NULL,
        .task_func = &sendGameCardCertificateViaUsb,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "dump xci",
        .child_menu = &g_xciMenu,
        .task_func = NULL,
        .element_options = NULL
    },
    NULL
};

static Menu g_rootMenu = {
    .parent = NULL,
    .selected = 0,
    .scroll = 0,
    .elements = g_rootMenuElements
};

static Mutex g_fileMutex = 0;
static CondVar g_readCondvar = 0, g_writeCondvar = 0;

static char path[FS_MAX_PATH] = {0};

int main(int argc, char *argv[])
{
    g_argc = argc;
    g_argv = argv;
    
    int ret = 0;
    
    Menu *cur_menu = &g_rootMenu;
    u32 element_count = menuGetElementCount(cur_menu), page_size = 30;
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    consoleInit(NULL);
    
    while(appletMainLoop())
    {
        consoleClear();
        printf("\npress b to %s.\n\n", cur_menu->parent ? "go back" : "exit");
        
        u32 limit = (cur_menu->scroll + page_size);
        MenuElement *selected_element = cur_menu->elements[cur_menu->selected];
        MenuElementOption *selected_element_options = selected_element->element_options;
        
        for(u32 i = cur_menu->scroll; cur_menu->elements[i] && i < element_count; i++)
        {
            if (i >= limit) break;
            
            MenuElement *cur_element = cur_menu->elements[i];
            MenuElementOption *cur_options = cur_menu->elements[i]->element_options;
            
            printf("%s%s", i == cur_menu->selected ? " -> " : "    ", cur_element->str);
            
            if (cur_options)
            {
                printf(": ");
                if (cur_options->selected > 0) printf("< ");
                printf("%s", cur_options->options[cur_options->selected]);
                if (cur_options->options[cur_options->selected + 1]) printf(" >");
            }
            
            printf("\n");
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
            Menu *child_menu = (Menu*)selected_element->child_menu;
            
            if (child_menu)
            {
                child_menu->parent = cur_menu;
                cur_menu = child_menu;
                element_count = menuGetElementCount(cur_menu);
            } else
            if (selected_element->task_func)
            {
                selected_element->task_func();
            }
        } else
        if ((btn_down & HidNpadButton_Down) || (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown)))
        {
            cur_menu->selected++;
            
            if (!cur_menu->elements[cur_menu->selected])
            {
                if (btn_down & HidNpadButton_Down)
                {
                    cur_menu->selected = 0;
                    cur_menu->scroll = 0;
                } else {
                    cur_menu->selected--;
                }
            } else
            if (cur_menu->selected >= limit && cur_menu->elements[cur_menu->selected + 1])
            {
                cur_menu->scroll++;
            }
        } else
        if ((btn_down & HidNpadButton_Up) || (btn_held & (HidNpadButton_StickLUp | HidNpadButton_StickRUp)))
        {
            cur_menu->selected--;
            
            if (cur_menu->selected == UINT32_MAX)
            {
                if (btn_down & HidNpadButton_Up)
                {
                    cur_menu->selected = (element_count - 1);
                    cur_menu->scroll = (element_count > page_size ? (element_count - page_size) : 0);
                } else {
                    cur_menu->selected = 0;
                }
            } else
            if (cur_menu->selected < cur_menu->scroll && cur_menu->scroll > 0)
            {
                cur_menu->scroll--;
            }
        } else
        if ((btn_down & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_StickRRight)) && selected_element_options)
        {
            selected_element_options->selected++;
            if (!selected_element_options->options[selected_element_options->selected]) selected_element_options->selected--;
            if (selected_element_options->options_func) selected_element_options->options_func(selected_element_options->selected);
        } else
        if ((btn_down & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft)) && selected_element_options)
        {
            selected_element_options->selected--;
            if (selected_element_options->selected == UINT32_MAX) selected_element_options->selected = 0;
            if (selected_element_options->options_func) selected_element_options->options_func(selected_element_options->selected);
        } else
        if (btn_down & HidNpadButton_B)
        {
            if (!cur_menu->parent) break;
            
            cur_menu = cur_menu->parent;
            element_count = menuGetElementCount(cur_menu);
        }
        
        if (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) svcSleepThread(50000000); // 50 ms
    }
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}

static void consolePrint(const char *text, ...)
{
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    consoleUpdate(NULL);
}

static u32 menuGetElementCount(const Menu *menu)
{
    if (!menu || !menu->elements || !menu->elements[0]) return 0;
    
    u32 cnt;
    for(cnt = 0; menu->elements[cnt]; cnt++);
    return cnt;
}

static void waitForGameCardAndUsb(void)
{
    consoleClear();
    consolePrint("waiting for gamecard...\n");
    
    while(true)
    {
        if (gamecardGetStatus() == GameCardStatus_InsertedAndInfoLoaded) break;
    }
    
    titleIsGameCardInfoUpdated();
    
    consolePrint("waiting for usb session...\n");
    
    while(true)
    {
        if (usbIsReady()) break;
    }
}

static bool sendFileData(const char *path, void *data, size_t data_size)
{
    if (!path || !strlen(path) || !data || !data_size)
    {
        consolePrint("invalid parameters to send file data!\n");
        return false;
    }
    
    if (!usbSendFilePropertiesCommon(data_size, path))
    {
        consolePrint("failed to send file properties for \"%s\"!\n", path);
        return false;
    }
    
    if (!usbSendFileData(data, data_size))
    {
        consolePrint("failed to send file data for \"%s\"!\n", path);
        return false;
    }
    
    return true;
}

static bool dumpGameCardKeyArea(GameCardKeyArea *out)
{
    if (!out)
    {
        consolePrint("invalid parameters to dump key area!\n");
        return false;
    }
    
    if (!gamecardGetKeyArea(out))
    {
        consolePrint("failed to get gamecard key area\n");
        return false;
    }
    
    consolePrint("get gamecard key area ok\n");
    return true;
}

static bool sendGameCardKeyAreaViaUsb(void)
{
    waitForGameCardAndUsb();
    
    utilsChangeHomeButtonBlockStatus(false);
    
    GameCardKeyArea gc_key_area = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = titleGenerateGameCardFileName(TitleFileNameConvention_Full, TitleFileNameIllegalCharReplaceType_IllegalFsChars);
    
    if (!dumpGameCardKeyArea(&gc_key_area) || !filename) goto end;
    
    crc32FastCalculate(&gc_key_area, sizeof(GameCardKeyArea), &crc);
    snprintf(path, MAX_ELEMENTS(path), "%s (Key Area) (%08X).bin", filename, crc);
    
    if (!sendFileData(path, &gc_key_area, sizeof(GameCardKeyArea))) goto end;
    
    printf("successfully sent key area as \"%s\"\n", path);
    success = true;
    
end:
    if (filename) free(filename);
    
    utilsChangeHomeButtonBlockStatus(false);
    
    consolePrint("press any button to continue");
    utilsWaitForButtonPress(0);
    
    return success;
}

static bool sendGameCardCertificateViaUsb(void)
{
    waitForGameCardAndUsb();
    
    utilsChangeHomeButtonBlockStatus(true);
    
    FsGameCardCertificate gc_cert = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = titleGenerateGameCardFileName(TitleFileNameConvention_Full, TitleFileNameIllegalCharReplaceType_IllegalFsChars);
    
    if (!gamecardGetCertificate(&gc_cert) || !filename)
    {
        consolePrint("failed to get gamecard certificate\n");
        goto end;
    }
    
    consolePrint("get gamecard certificate ok\n");
    
    crc32FastCalculate(&gc_cert, sizeof(FsGameCardCertificate), &crc);
    snprintf(path, MAX_ELEMENTS(path), "%s (Certificate) (%08X).bin", filename, crc);
    
    if (!sendFileData(path, &gc_cert, sizeof(FsGameCardCertificate))) goto end;
    
    printf("successfully sent certificate as \"%s\"\n", path);
    success = true;
    
end:
    if (filename) free(filename);
    
    utilsChangeHomeButtonBlockStatus(false);
    
    consolePrint("press any button to continue");
    utilsWaitForButtonPress(0);
    
    return success;
}

static bool sendGameCardImageViaUsb(void)
{
    waitForGameCardAndUsb();
    
    utilsChangeHomeButtonBlockStatus(true);
    
    u64 gc_size = 0;
    u32 key_area_crc = 0;
    GameCardKeyArea gc_key_area = {0};
    
    ThreadSharedData shared_data = {0};
    Thread read_thread = {0}, write_thread = {0};
    
    char *filename = NULL;
    
    bool success = false;
    
    consolePrint("gamecard image dump\nappend key area: %s | keep certificate: %s | trim dump: %s\n\n", g_appendKeyArea ? "yes" : "no", g_keepCertificate ? "yes" : "no", g_trimDump ? "yes" : "no");
    
    filename = titleGenerateGameCardFileName(TitleFileNameConvention_Full, TitleFileNameIllegalCharReplaceType_IllegalFsChars);
    if (!filename)
    {
        consolePrint("failed to generate gamecard filename!\n");
        goto end;
    }
    
    shared_data.data = usbAllocatePageAlignedBuffer(BLOCK_SIZE);
    if (!shared_data.data)
    {
        consolePrint("failed to allocate memory for the dump procedure!\n");
        goto end;
    }
    
    if ((!g_trimDump && !gamecardGetTotalSize(&gc_size)) || (g_trimDump && !gamecardGetTrimmedSize(&gc_size)) || !gc_size)
    {
        consolePrint("failed to get gamecard size!\n");
        goto end;
    }
    
    shared_data.total_size = gc_size;
    
    consolePrint("gamecard size: 0x%lX\n", gc_size);
    
    if (g_appendKeyArea)
    {
        gc_size += sizeof(GameCardKeyArea);
        if (!dumpGameCardKeyArea(&gc_key_area)) goto end;
        if (g_calcCrc) crc32FastCalculate(&gc_key_area, sizeof(GameCardKeyArea), &key_area_crc);
        shared_data.full_xci_crc = key_area_crc;
        consolePrint("gamecard size (with key area): 0x%lX\n", gc_size);
    }
    
    snprintf(path, MAX_ELEMENTS(path), "%s (%s) (%s) (%s).xci", filename, g_appendKeyArea ? "keyarea" : "keyarealess", g_keepCertificate ? "cert" : "certless", g_trimDump ? "trimmed" : "untrimmed");
    if (!usbSendFilePropertiesCommon(gc_size, path))
    {
        consolePrint("failed to send file properties for \"%s\"!\n", path);
        goto end;
    }
    
    if (g_appendKeyArea && !usbSendFileData(&gc_key_area, sizeof(GameCardKeyArea)))
    {
        consolePrint("failed to send gamecard key area data!\n");
        goto end;
    }
    
    consolePrint("creating threads\n");
    utilsCreateThread(&read_thread, read_thread_func, &shared_data, 2);
    utilsCreateThread(&write_thread, write_thread_func, &shared_data, 2);
    
    u8 prev_time = 0;
    u64 prev_size = 0;
    u8 percent = 0;
    
    time_t start = 0, btn_cancel_start_tmr = 0, btn_cancel_end_tmr = 0;
    bool btn_cancel_cur_state = false, btn_cancel_prev_state = false;
    
    consolePrint("hold b to cancel\n\n");
    
    start = time(NULL);
    
    while(shared_data.data_written < shared_data.total_size)
    {
        if (shared_data.read_error || shared_data.write_error) break;
        
        time_t now = time(NULL);
        struct tm *ts = localtime(&now);
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
        
        if (prev_time == ts->tm_sec || prev_size == size) continue;
        
        percent = (u8)((size * 100) / shared_data.total_size);
        
        prev_time = ts->tm_sec;
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
    
    if (shared_data.read_error || shared_data.write_error)
    {
        consolePrint("usb transfer error\n");
        goto end;
    }
    
    if (shared_data.transfer_cancelled)
    {
        consolePrint("process cancelled\n");
        goto end;
    }
    
    printf("process completed in %lu seconds\n", start);
    success = true;
    
    if (g_calcCrc)
    {
        if (g_appendKeyArea) printf("key area crc: %08X | ", key_area_crc);
        printf("xci crc: %08X", shared_data.xci_crc);
        if (g_appendKeyArea) printf(" | xci crc (with key area): %08X", shared_data.full_xci_crc);
        printf("\n");
    }
    
end:
    if (shared_data.data) free(shared_data.data);
    
    if (filename) free(filename);
    
    utilsChangeHomeButtonBlockStatus(false);
    
    consolePrint("press any button to continue");
    utilsWaitForButtonPress(0);
    
    return success;
}

static void changeKeyAreaOption(u32 idx)
{
    g_appendKeyArea = (idx > 0);
}

static void changeCertificateOption(u32 idx)
{
    g_keepCertificate = (idx > 0);
}

static void changeTrimOption(u32 idx)
{
    g_trimDump = (idx > 0);
}

static void changeCrcOption(u32 idx)
{
    g_calcCrc = (idx > 0);
}

static void read_thread_func(void *arg)
{
    ThreadSharedData *shared_data = (ThreadSharedData*)arg;
    if (!shared_data || !shared_data->data || !shared_data->total_size)
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
    
    for(u64 offset = 0, blksize = BLOCK_SIZE; offset < shared_data->total_size; offset += blksize)
    {
        if (blksize > (shared_data->total_size - offset)) blksize = (shared_data->total_size - offset);
        
        /* Check if the transfer has been cancelled by the user */
        if (shared_data->transfer_cancelled)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }
        
        /* Read current data chunk */
        shared_data->read_error = !gamecardReadStorage(buf, blksize, offset);
        if (shared_data->read_error)
        {
            condvarWakeAll(&g_writeCondvar);
            break;
        }
        
        /* Remove certificate */
        if (!g_keepCertificate && offset == 0) memset(buf + GAMECARD_CERTIFICATE_OFFSET, 0xFF, sizeof(FsGameCardCertificate));
        
        /* Update checksum */
        if (g_calcCrc)
        {
            crc32FastCalculate(buf, blksize, &(shared_data->xci_crc));
            if (g_appendKeyArea) crc32FastCalculate(buf, blksize, &(shared_data->full_xci_crc));
        }
        
        /* Wait until the previous data chunk has been written */
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
