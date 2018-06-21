#pragma once

#ifndef __UI_H__
#define __UI_H__

#define FILENAMEBUFFER_SIZE	(1024 * 32)  // 32 KiB
#define FILENAMES_COUNT_MAX	2048

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

void uiStatusMsg(const char* fmt, ...);
void uiFill(int x, int y, int width, int height, u8 r, u8 g, u8 b);
void uiDrawString(const char* string, int x, int y, u8 r, u8 g, u8 b);

void uiUpdateFreeSpace();

void uiInit();
void uiDeinit();

void uiSetState(UIState state);
UIState uiGetState();

void uiClearScreen();

UIResult uiLoop(u32 keysDown);

#endif
