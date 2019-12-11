#pragma once

#ifndef __UI_H__
#define __UI_H__

#define FB_WIDTH                    1280
#define FB_HEIGHT                   720

#define CHAR_PT_SIZE                12
#define SCREEN_DPI_CNT              96

#define LINE_HEIGHT                 (font_height + (font_height / 4))
#define LINE_STRING_OFFSET          (font_height / 8)

#define STRING_DEFAULT_POS          8, 8

#define STRING_X_POS                8
#define STRING_Y_POS(x)             (((x) * LINE_HEIGHT) + LINE_STRING_OFFSET)

#define BG_COLOR_RGB                50, 50, 50
#define FONT_COLOR_RGB              255, 255, 255

#define HIGHLIGHT_BG_COLOR_RGB      33, 34, 39
#define HIGHLIGHT_FONT_COLOR_RGB    0, 255, 197

#define FONT_COLOR_SUCCESS_RGB      0, 255, 0
#define FONT_COLOR_ERROR_RGB        255, 0, 0
#define FONT_COLOR_TITLE_RGB        115, 115, 255

#define EMPTY_BAR_COLOR_RGB         0, 0, 0

#define COMMON_MAX_ELEMENTS         9
#define HFS0_MAX_ELEMENTS           14
#define ROMFS_MAX_ELEMENTS          12
#define SDCARD_MAX_ELEMENTS         3
#define ORPHAN_MAX_ELEMENTS         12
#define BATCH_MAX_ELEMENTS          14

#define OPTIONS_X_START_POS         (35 * CHAR_PT_SIZE)
#define OPTIONS_X_END_POS           (OPTIONS_X_START_POS + (6 * CHAR_PT_SIZE))
#define OPTIONS_X_END_POS_NSP       (FB_WIDTH - (4 * CHAR_PT_SIZE))

#define TAB_WIDTH                   4

#define BROWSER_ICON_DIMENSION      16

// UTF-8 sequences

#define UPWARDS_ARROW               "\xE2\x86\x91"
#define DOWNWARDS_ARROW             "\xE2\x86\x93"

#define NINTENDO_FONT_A             "\xEE\x82\xA0"
#define NINTENDO_FONT_B             "\xEE\x82\xA1"
#define NINTENDO_FONT_X             "\xEE\x82\xA2"
#define NINTENDO_FONT_Y             "\xEE\x82\xA3"
#define NINTENDO_FONT_L             "\xEE\x82\xA4"
#define NINTENDO_FONT_R             "\xEE\x82\xA5"
#define NINTENDO_FONT_ZL            "\xEE\x82\xA6"
#define NINTENDO_FONT_ZR            "\xEE\x82\xA7"
#define NINTENDO_FONT_DPAD          "\xEE\x82\xAA"
#define NINTENDO_FONT_PLUS          "\xEE\x82\xB5"
#define NINTENDO_FONT_HOME          "\xEE\x82\xB9"
#define NINTENDO_FONT_LSTICK        "\xEE\x83\x81"
#define NINTENDO_FONT_RSTICK        "\xEE\x83\x82"

typedef enum {
    resultNone,
    resultShowMainMenu,
    resultShowGameCardMenu,
    resultShowXciDumpMenu,
    resultDumpXci,
    resultShowNspDumpMenu,
    resultShowNspAppDumpMenu,
    resultShowNspPatchDumpMenu,
    resultShowNspAddOnDumpMenu,
    resultDumpNsp,
    resultShowHfs0Menu,
    resultShowRawHfs0PartitionDumpMenu,
    resultDumpRawHfs0Partition,
    resultShowHfs0PartitionDataDumpMenu,
    resultDumpHfs0PartitionData,
    resultShowHfs0BrowserMenu,
    resultHfs0BrowserGetList,
    resultShowHfs0Browser,
    resultHfs0BrowserCopyFile,
    resultShowExeFsMenu,
    resultShowExeFsSectionDataDumpMenu,
    resultDumpExeFsSectionData,
    resultShowExeFsSectionBrowserMenu,
    resultExeFsSectionBrowserGetList,
    resultShowExeFsSectionBrowser,
    resultExeFsSectionBrowserCopyFile,
    resultShowRomFsMenu,
    resultShowRomFsSectionDataDumpMenu,
    resultDumpRomFsSectionData,
    resultShowRomFsSectionBrowserMenu,
    resultRomFsSectionBrowserGetEntries,
    resultShowRomFsSectionBrowser,
    resultRomFsSectionBrowserChangeDir,
    resultRomFsSectionBrowserCopyFile,
    resultRomFsSectionBrowserCopyDir,
    resultDumpGameCardCertificate,
    resultShowSdCardEmmcMenu,
    resultShowSdCardEmmcTitleMenu,
    resultShowSdCardEmmcOrphanPatchAddOnMenu,
    resultShowSdCardEmmcBatchModeMenu,
    resultSdCardEmmcBatchDump,
    resultShowTicketMenu,
    resultDumpTicket,
    resultShowUpdateMenu,
    resultUpdateNSWDBXml,
    resultUpdateApplication,
    resultExit
} UIResult;

typedef enum {
    stateMainMenu,
    stateGameCardMenu,
    stateXciDumpMenu,
    stateDumpXci,
    stateNspDumpMenu,
    stateNspAppDumpMenu,
    stateNspPatchDumpMenu,
    stateNspAddOnDumpMenu,
    stateDumpNsp,
    stateHfs0Menu,
    stateRawHfs0PartitionDumpMenu,
    stateDumpRawHfs0Partition,
    stateHfs0PartitionDataDumpMenu,
    stateDumpHfs0PartitionData,
    stateHfs0BrowserMenu,
    stateHfs0BrowserGetList,
    stateHfs0Browser,
    stateHfs0BrowserCopyFile,
    stateExeFsMenu,
    stateExeFsSectionDataDumpMenu,
    stateDumpExeFsSectionData,
    stateExeFsSectionBrowserMenu,
    stateExeFsSectionBrowserGetList,
    stateExeFsSectionBrowser,
    stateExeFsSectionBrowserCopyFile,
    stateRomFsMenu,
    stateRomFsSectionDataDumpMenu,
    stateDumpRomFsSectionData,
    stateRomFsSectionBrowserMenu,
    stateRomFsSectionBrowserGetEntries,
    stateRomFsSectionBrowser,
    stateRomFsSectionBrowserChangeDir,
    stateRomFsSectionBrowserCopyFile,
    stateRomFsSectionBrowserCopyDir,
    stateDumpGameCardCertificate,
    stateSdCardEmmcMenu,
    stateSdCardEmmcTitleMenu,
    stateSdCardEmmcOrphanPatchAddOnMenu,
    stateSdCardEmmcBatchModeMenu,
    stateSdCardEmmcBatchDump,
    stateTicketMenu,
    stateDumpTicket,
    stateUpdateMenu,
    stateUpdateNSWDBXml,
    stateUpdateApplication
} UIState;

typedef enum {
    MENUTYPE_MAIN = 0,
    MENUTYPE_GAMECARD,
    MENUTYPE_SDCARD_EMMC
} curMenuType;

void uiFill(int x, int y, int width, int height, u8 r, u8 g, u8 b);

void uiDrawIcon(const u8 *icon, int width, int height, int x, int y);

bool uiLoadJpgFromMem(u8 *rawJpg, size_t rawJpgSize, int expectedWidth, int expectedHeight, int desiredWidth, int desiredHeight, u8 **outBuf);

bool uiLoadJpgFromFile(const char *filename, int expectedWidth, int expectedHeight, int desiredWidth, int desiredHeight, u8 **outBuf);

void uiDrawString(int x, int y, u8 r, u8 g, u8 b, const char *fmt, ...);

u32 uiGetStrWidth(const char *fmt, ...);

void uiRefreshDisplay();

void uiStatusMsg(const char *fmt, ...);

void uiUpdateStatusMsg();

void uiClearStatusMsg();

void uiPleaseWait(u8 wait);

void uiClearScreen();

void uiPrintHeadline();

bool uiInit();

void uiDeinit();

void uiSetState(UIState state);

UIState uiGetState();

UIResult uiProcess();

#endif
