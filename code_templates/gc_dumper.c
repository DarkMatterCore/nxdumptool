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
#include "gamecard.h"
#include "usb.h"
#include "title.h"

#define BLOCK_SIZE  USB_TRANSFER_BUFFER_SIZE

bool g_borealisInitialized = false;

static PadState g_padState = {0};

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
    FILE *fp;
} ThreadSharedData;

/* Function prototypes. */

static void consolePrint(const char *text, ...);

static u32 menuGetElementCount(const Menu *menu);

static bool waitForGameCard(void);
static bool waitForUsb(void);

static void generateDumpTxt(void);
static bool saveDumpTxt(void);

static char *generateOutputFileName(const char *extension);

static bool saveGameCardSpecificData(void);
static bool saveGameCardCertificate(void);
static bool saveGameCardInitialData(void);
static bool saveGameCardIdSet(void);
static bool saveGameCardImage(void);
static bool saveConsoleLafwBlob(void);

static void changeStorageOption(u32 idx);
static void changeKeyAreaOption(u32 idx);
static void changeCertificateOption(u32 idx);
static void changeTrimOption(u32 idx);
static void changeCrcOption(u32 idx);

static void read_thread_func(void *arg);
static void write_thread_func(void *arg);

/* Global variables. */

static bool g_useUsbHost = false, g_appendKeyArea = true, g_keepCertificate = false, g_trimDump = false, g_calcCrc = false;

static const char *g_storageOptions[] = { "sd card", "usb host", NULL };
static const char *g_xciOptions[] = { "no", "yes", NULL };

static MenuElement *g_xciMenuElements[] = {
    &(MenuElement){
        .str = "start dump",
        .child_menu = NULL,
        .task_func = &saveGameCardImage,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "append key area",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 1,
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
        .str = "dump gamecard xci",
        .child_menu = &g_xciMenu,
        .task_func = NULL,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "dump gamecard certificate",
        .child_menu = NULL,
        .task_func = &saveGameCardCertificate,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "dump gamecard initial data",
        .child_menu = NULL,
        .task_func = &saveGameCardInitialData,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "dump gamecard id set",
        .child_menu = NULL,
        .task_func = &saveGameCardIdSet,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "dump gamecard specific data",
        .child_menu = NULL,
        .task_func = &saveGameCardSpecificData,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "dump console lafw blob",
        .child_menu = NULL,
        .task_func = &saveConsoleLafwBlob,
        .element_options = NULL
    },
    &(MenuElement){
        .str = "output storage",
        .child_menu = NULL,
        .task_func = NULL,
        .element_options = &(MenuElementOption){
            .selected = 0,
            .options_func = &changeStorageOption,
            .options = g_storageOptions
        }
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

static char path[FS_MAX_PATH] = {0}, txt_info[FS_MAX_PATH] = {0};

static bool g_appletStatus = true;

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

int main(int argc, char *argv[])
{
    int ret = 0;
    
    Menu *cur_menu = &g_rootMenu;
    u32 element_count = menuGetElementCount(cur_menu), page_size = 30;
    
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
    
    chdir(DEVOPTAB_SDMC_DEVICE GAMECARD_PATH);
    
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
        while((g_appletStatus = appletMainLoop()))
        {
            utilsScanPads();
            btn_down = utilsGetButtonsDown();
            btn_held = utilsGetButtonsHeld();
            if (btn_down || btn_held) break;
        }
        
        if (!g_appletStatus) break;
        
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
                /* Wait for gamecard. */
                if (!waitForGameCard())
                {
                    if (g_appletStatus) continue;
                    break;
                }
                
                /* Wait for USB session. */
                if (g_useUsbHost && !waitForUsb()) break;
                
                /* Generate dump text. */
                generateDumpTxt();
                
                /* Run task. */
                utilsSetLongRunningProcessState(true);
                if (selected_element->task_func()) saveDumpTxt();
                utilsSetLongRunningProcessState(false);
                
                /* Display prompt. */
                consolePrint("press any button to continue");
                utilsWaitForButtonPress(0);
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

static bool waitForGameCard(void)
{
    consoleClear();
    consolePrint("waiting for gamecard...\n");
    
    u8 status = GameCardStatus_NotInserted;
    
    while((g_appletStatus = appletMainLoop()))
    {
        status = gamecardGetStatus();
        if (status > GameCardStatus_Processing) break;
    }
    
    if (!g_appletStatus) return false;
    
    switch(status)
    {
        case GameCardStatus_NoGameCardPatchEnabled:
            consolePrint("\"nogc\" patch enabled, please disable it and reboot your console\n");
            break;
        case GameCardStatus_LotusAsicFirmwareUpdateRequired:
            consolePrint("gamecard controller firmware update required, please update your console\n");
            break;
        case GameCardStatus_InsertedAndInfoNotLoaded:
            consolePrint("unexpected I/O error occurred, please check the logfile\n");
            break;
        default:
            break;
    }
    
    if (status != GameCardStatus_InsertedAndInfoLoaded)
    {
        consolePrint("press any button\n");
        utilsWaitForButtonPress(0);
        return false;
    }
    
    return true;
}

static bool waitForUsb(void)
{
    if (usbIsReady()) return true;
    
    consolePrint("waiting for usb session...\n");
    
    while((g_appletStatus = appletMainLoop()))
    {
        if (usbIsReady()) break;
    }
    
    return g_appletStatus;
}

static void generateDumpTxt(void)
{
    *txt_info = '\0';
    
    struct tm ts = {0};
    time_t now = time(NULL);
    
    /* Get UTC time. */
    gmtime_r(&now, &ts);
    ts.tm_year += 1900;
    ts.tm_mon++;
    
    /* Generate dump text. */
    snprintf(txt_info, MAX_ELEMENTS(txt_info), "tool:       nxdumptool\r\n" \
                                               "version:    " APP_VERSION "\r\n" \
                                               "branch:     " GIT_BRANCH "\r\n" \
                                               "commit:     " GIT_COMMIT "\r\n" \
                                               "build date: " BUILD_TIMESTAMP "\r\n" \
                                               "dump date:  %d-%02d-%02d %02d:%02d:%02d UTC\r\n", \
                                               ts.tm_year, ts.tm_mon, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec);
}

static bool saveFileData(const char *path, void *data, size_t data_size)
{
    if (!path || !*path || !data || !data_size)
    {
        consolePrint("invalid parameters to save file data!\n");
        return false;
    }
    
    if (g_useUsbHost)
    {
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
    } else {
        FILE *fp = fopen(path, "wb");
        if (!fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", path);
            return false;
        }
        
        size_t ret = fwrite(data, 1, data_size, fp);
        fclose(fp);
        
        if (ret != data_size)
        {
            consolePrint("failed to write 0x%lX byte(s) to \"%s\"! (%d)\n", data_size, path, errno);
            remove(path);
        }
    }
    
    return true;
}

static bool saveDumpTxt(void)
{
    if (!*path || !*txt_info) return true;
    
    path[strlen(path) - 3] = '\0';
    strcat(path, "txt");
    
    return saveFileData(path, txt_info, strlen(txt_info));
}

static char *generateOutputFileName(const char *extension)
{
    char *filename = NULL, *output = NULL;
    
    if (!extension || !*extension || !(filename = titleGenerateGameCardFileName(TitleNamingConvention_Full, g_useUsbHost ? TitleFileNameIllegalCharReplaceType_IllegalFsChars : TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly)))
    {
        consolePrint("failed to get gamecard filename!\n");
        return NULL;
    }
    
    output = utilsGeneratePath(NULL, filename, extension);
    free(filename);
    
    if (output)
    {
        snprintf(path, MAX_ELEMENTS(path), "%s", output);
    } else {
        consolePrint("failed to generate output filename!\n");
    }
    
    return output;
}

static bool dumpGameCardSecurityInformation(GameCardSecurityInformation *out)
{
    if (!out)
    {
        consolePrint("invalid parameters to dump gamecard security information!\n");
        return false;
    }
    
    if (!gamecardGetSecurityInformation(out))
    {
        consolePrint("failed to get gamecard security information\n");
        return false;
    }
    
    consolePrint("get gamecard security information ok\n");
    return true;
}

static bool saveGameCardSpecificData(void)
{
    GameCardSecurityInformation gc_security_information = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;
    
    if (!dumpGameCardSecurityInformation(&gc_security_information)) goto end;
    
    crc = crc32Calculate(&(gc_security_information.specific_data), sizeof(GameCardSpecificData));
    snprintf(path, MAX_ELEMENTS(path), " (Specific Data) (%08X).bin", crc);
    
    filename = generateOutputFileName(path);
    if (!filename) goto end;
    
    if (!saveFileData(filename, &(gc_security_information.specific_data), sizeof(GameCardSpecificData))) goto end;
    
    printf("successfully saved specific data as \"%s\"\n", filename);
    success = true;
    
end:
    if (filename) free(filename);
    
    return success;
}

static bool saveGameCardCertificate(void)
{
    FsGameCardCertificate gc_cert = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;
    
    if (!gamecardGetCertificate(&gc_cert))
    {
        consolePrint("failed to get gamecard certificate\n");
        goto end;
    }
    
    consolePrint("get gamecard certificate ok\n");
    
    crc = crc32Calculate(&gc_cert, sizeof(FsGameCardCertificate));
    snprintf(path, MAX_ELEMENTS(path), " (Certificate) (%08X).bin", crc);
    
    filename = generateOutputFileName(path);
    if (!filename) goto end;
    
    if (!saveFileData(filename, &gc_cert, sizeof(FsGameCardCertificate))) goto end;
    
    printf("successfully saved certificate as \"%s\"\n", filename);
    success = true;
    
end:
    if (filename) free(filename);
    
    return success;
}

static bool saveGameCardInitialData(void)
{
    GameCardSecurityInformation gc_security_information = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;
    
    if (!dumpGameCardSecurityInformation(&gc_security_information)) goto end;
    
    crc = crc32Calculate(&(gc_security_information.initial_data), sizeof(GameCardInitialData));
    snprintf(path, MAX_ELEMENTS(path), " (Initial Data) (%08X).bin", crc);
    
    filename = generateOutputFileName(path);
    if (!filename) goto end;
    
    if (!saveFileData(filename, &(gc_security_information.initial_data), sizeof(GameCardInitialData))) goto end;
    
    printf("successfully saved initial data as \"%s\"\n", filename);
    success = true;
    
end:
    if (filename) free(filename);
    
    return success;
}

static bool saveGameCardIdSet(void)
{
    FsGameCardIdSet id_set = {0};
    bool success = false;
    u32 crc = 0;
    char *filename = NULL;
    
    if (!gamecardGetIdSet(&id_set)) goto end;
    
    crc = crc32Calculate(&id_set, sizeof(FsGameCardIdSet));
    snprintf(path, MAX_ELEMENTS(path), " (Card ID Set) (%08X).bin", crc);
    
    filename = generateOutputFileName(path);
    if (!filename) goto end;
    
    if (!saveFileData(filename, &id_set, sizeof(FsGameCardIdSet))) goto end;
    
    printf("successfully saved gamecard id set as \"%s\"\n", filename);
    success = true;
    
end:
    if (filename) free(filename);
    
    return success;
}

static bool saveGameCardImage(void)
{
    u64 gc_size = 0;
    
    u32 key_area_crc = 0;
    GameCardKeyArea gc_key_area = {0};
    GameCardSecurityInformation gc_security_information = {0};
    
    ThreadSharedData shared_data = {0};
    Thread read_thread = {0}, write_thread = {0};
    
    char *filename = NULL;
    
    bool success = false;
    
    consolePrint("gamecard image dump\nappend key area: %s | keep certificate: %s | trim dump: %s | calculate crc32: %s\n\n", g_appendKeyArea ? "yes" : "no", g_keepCertificate ? "yes" : "no", g_trimDump ? "yes" : "no", g_calcCrc ? "yes" : "no");
    
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
        
        if (!dumpGameCardSecurityInformation(&gc_security_information)) goto end;
        
        memcpy(&(gc_key_area.initial_data), &(gc_security_information.initial_data), sizeof(GameCardInitialData));
        
        if (g_calcCrc)
        {
            key_area_crc = crc32Calculate(&gc_key_area, sizeof(GameCardKeyArea));
            shared_data.full_xci_crc = key_area_crc;
        }
        
        consolePrint("gamecard size (with key area): 0x%lX\n", gc_size);
    }
    
    snprintf(path, MAX_ELEMENTS(path), " (%s) (%s) (%s).xci", g_appendKeyArea ? "keyarea" : "keyarealess", g_keepCertificate ? "cert" : "certless", g_trimDump ? "trimmed" : "untrimmed");
    filename = generateOutputFileName(path);
    if (!filename) goto end;
    
    if (g_useUsbHost)
    {
        if (!usbSendFilePropertiesCommon(gc_size, filename))
        {
            consolePrint("failed to send file properties for \"%s\"!\n", filename);
            goto end;
        }
        
        if (g_appendKeyArea && !usbSendFileData(&gc_key_area, sizeof(GameCardKeyArea)))
        {
            consolePrint("failed to send gamecard key area data!\n");
            goto end;
        }
    } else {
        if (gc_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(filename))
        {
            consolePrint("failed to create concatenation file for \"%s\"!\n", filename);
            goto end;
        }
        
        shared_data.fp = fopen(filename, "wb");
        if (!shared_data.fp)
        {
            consolePrint("failed to open \"%s\" for writing!\n", filename);
            goto end;
        }
        
        if (g_appendKeyArea && fwrite(&gc_key_area, 1, sizeof(GameCardKeyArea), shared_data.fp) != sizeof(GameCardKeyArea))
        {
            consolePrint("failed to write gamecard key area data!\n");
            goto end;
        }
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
    
    if (shared_data.read_error || shared_data.write_error)
    {
        consolePrint("i/o error\n");
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
    if (shared_data.fp)
    {
        fclose(shared_data.fp);
        shared_data.fp = NULL;
    }
    
    if (!success && !g_useUsbHost) utilsRemoveConcatenationFile(filename);
    
    if (shared_data.data) free(shared_data.data);
    
    if (filename) free(filename);
    
    return success;
}

static bool saveConsoleLafwBlob(void)
{
    u64 lafw_version = 0;
    LotusAsicFirmwareBlob lafw_blob = {0};
    bool success = false;
    u32 crc = 0;
    
    if (!gamecardGetLotusAsicFirmwareBlob(&lafw_blob, &lafw_version))
    {
        consolePrint("failed to get console lafw blob\n");
        goto end;
    }
    
    const char *fw_type_str = gamecardGetLafwTypeString(lafw_blob.fw_type);
    if (!fw_type_str) fw_type_str = "Unknown";
    
    const char *dev_type_str = gamecardGetLafwDeviceTypeString(lafw_blob.device_type);
    if (!dev_type_str) dev_type_str = "Unknown";
    
    consolePrint("get console lafw blob ok\n");
    
    crc = crc32Calculate(&lafw_blob, sizeof(LotusAsicFirmwareBlob));
    snprintf(path, MAX_ELEMENTS(path), "LAFW (%s) (%s) (v%lu) (%08X).bin", fw_type_str, dev_type_str, lafw_version, crc);
    
    if (!saveFileData(path, &lafw_blob, sizeof(LotusAsicFirmwareBlob))) goto end;
    
    printf("successfully saved lafw blob as \"%s\"\n", path);
    success = true;
    
end:
    return success;
}

static void changeStorageOption(u32 idx)
{
    g_useUsbHost = (idx > 0);
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
            shared_data->xci_crc = crc32CalculateWithSeed(shared_data->xci_crc, buf, blksize);
            if (g_appendKeyArea) shared_data->full_xci_crc = crc32CalculateWithSeed(shared_data->full_xci_crc, buf, blksize);
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
            if (shared_data->transfer_cancelled && g_useUsbHost) usbCancelFileTransfer();
            mutexUnlock(&g_fileMutex);
            break;
        }
        
        /* Write current file data chunk */
        if (g_useUsbHost)
        {
            shared_data->write_error = !usbSendFileData(shared_data->data, shared_data->data_size);
        } else {
            shared_data->write_error = (fwrite(shared_data->data, 1, shared_data->data_size, shared_data->fp) != shared_data->data_size);
        }
        
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
