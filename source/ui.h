#pragma once

#ifndef __UI_H__
#define __UI_H__

#define FB_WIDTH                1280
#define FB_HEIGHT               720

#define CHAR_PT_SIZE            12
#define SCREEN_DPI_CNT          96

#define BG_COLOR_RGB            50

#define HIGHLIGHT_BG_COLOR_R    33
#define HIGHLIGHT_BG_COLOR_G    34
#define HIGHLIGHT_BG_COLOR_B    39

#define HIGHLIGHT_FONT_COLOR_R  0
#define HIGHLIGHT_FONT_COLOR_G  255
#define HIGHLIGHT_FONT_COLOR_B  197

#define OPTIONS_X_POS   (35 * CHAR_PT_SIZE)

#define TAB_WIDTH               4

typedef enum {
    resultNone,
    resultShowMainMenu,
    resultShowXciDumpMenu,
    resultDumpXci,
    resultShowNspDumpMenu,
    resultDumpNsp,
    resultShowRawPartitionDumpMenu,
    resultDumpRawPartition,
    resultShowPartitionDataDumpMenu,
    resultDumpPartitionData,
    resultShowViewGameCardFsMenu,
    resultShowViewGameCardFsGetList,
    resultShowViewGameCardFsBrowser,
    resultViewGameCardFsBrowserCopyFile,
    resultDumpGameCardCertificate,
    resultUpdateNSWDBXml,
    resultUpdateApplication,
    resultExit
} UIResult;

typedef enum {
    stateMainMenu,
    stateXciDumpMenu,
    stateDumpXci,
    stateNspDumpMenu,
    stateDumpNsp,
    stateRawPartitionDumpMenu,
    stateDumpRawPartition,
    statePartitionDataDumpMenu,
    stateDumpPartitionData,
    stateViewGameCardFsMenu,
    stateViewGameCardFsGetList,
    stateViewGameCardFsBrowser,
    stateViewGameCardFsBrowserCopyFile,
    stateDumpGameCardCertificate,
    stateUpdateNSWDBXml,
    stateUpdateApplication
} UIState;

void uiFill(int x, int y, int width, int height, u8 r, u8 g, u8 b);

void uiDrawString(const char *string, int x, int y, u8 r, u8 g, u8 b);

void uiRefreshDisplay();

void uiStatusMsg(const char *fmt, ...);

void uiUpdateStatusMsg();

void uiPleaseWait(u8 wait);

void uiUpdateFreeSpace();

void uiClearScreen();

void uiPrintHeadline();

int uiInit();

void uiDeinit();

void uiSetState(UIState state);

UIState uiGetState();

UIResult uiProcess();

#endif
