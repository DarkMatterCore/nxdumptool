#pragma once

#ifndef __UI_H__
#define __UI_H__

#define FILENAME_BUFFER_SIZE	(1024 * 32)  // 32 KiB
#define FILENAME_MAX_CNT		2048

typedef enum {
	resultNone,
	resultShowMainMenu,
	resultShowXciDumpMenu,
	resultDumpXci,
	resultShowRawPartitionDumpMenu,
	resultDumpRawPartition,
	resultShowPartitionDataDumpMenu,
	resultDumpPartitionData,
	resultShowViewGameCardFsMenu,
	resultShowViewGameCardFsGetList,
	resultShowViewGameCardFsBrowser,
	resultViewGameCardFsBrowserCopyFile,
	resultDumpGameCardCertificate,
	resultExit
} UIResult;

typedef enum {
	stateMainMenu,
	stateXciDumpMenu,
	stateDumpXci,
	stateRawPartitionDumpMenu,
	stateDumpRawPartition,
	statePartitionDataDumpMenu,
	stateDumpPartitionData,
	stateViewGameCardFsMenu,
	stateViewGameCardFsGetList,
	stateViewGameCardFsBrowser,
	stateViewGameCardFsBrowserCopyFile,
	stateDumpGameCardCertificate
} UIState;

void uiFill(int x, int y, int width, int height, u8 r, u8 g, u8 b);
void uiDrawString(const char* string, int x, int y, u8 r, u8 g, u8 b);

void uiStatusMsg(const char* fmt, ...);
void uiUpdateStatusMsg();

void uiPleaseWait();

void uiUpdateFreeSpace();

void uiInit();
void uiDeinit();

void uiSetState(UIState state);
UIState uiGetState();

void uiClearScreen();
void uiPrintHeadline();

UIResult uiLoop(u32 keysDown);

#endif
