#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include "dumper.h"
#include "fsext.h"
#include "ui.h"
#include "util.h"

extern FsDeviceOperator fsOperatorInstance;

extern bool gameCardInserted;

extern u32 currentFBWidth, currentFBHeight;
extern u8 *currentFB;

extern char gameCardSizeStr[32], trimmedCardSizeStr[32];

extern char *hfs0_header;
extern u64 hfs0_offset, hfs0_size;
extern u32 hfs0_partition_cnt;

extern u64 gameCardTitleID;
extern u32 gameCardVersion;
extern char gameCardName[0x201], fixedGameCardName[0x201], gameCardAuthor[0x101], gameCardVersionStr[64];

extern char gameCardUpdateVersionStr[128];

static bool isFat32 = false, dumpCert = false, trimDump = false, calcCrc = true;

static u32 selectedOption;

static char statusMessage[2048] = {'\0'};
static int statusMessageFadeout = 0;

static char fileCopyPath[NAME_BUF_LEN * 2] = {'\0'};
static char currentDirectory[NAME_BUF_LEN] = {'\0'};
static int cursor = 0;
static int scroll = 0;

static int headlineCnt = 0;
int breaks = 0;

u64 freeSpace = 0;
static char freeSpaceStr[64] = {'\0'};

static char titlebuf[NAME_BUF_LEN * 2] = {'\0'};

static const int maxListElements = 45;

static char *filenameBuffer = NULL;
static char *filenames[FILENAME_MAX_CNT];
static int filenamesCount = 0;

static UIState uiState;

static const char *appHeadline = "Nintendo Switch Game Card Dump Tool v" APP_VERSION ".\nOriginal code by MCMrARM.\nAdditional modifications by DarkMatterCore.\n\n";
static const char *appControls = "[D-Pad / Analog Stick] Move | [A] Select | [B] Back | [+] Exit";

static const char *mainMenuItems[] = { "Full XCI Dump", "Raw Partition Dump", "Partition Data Dump", "View Game Card Files", "Dump Game Card Certificate", "Update nswdb.com XML database", "Update application (not working at this moment)" };
static const char *xciDumpMenuItems[] = { "Start XCI dump process", "Split output dump (FAT32 support): ", "Dump certificate: ", "Trim output dump: ", "CRC32 checksum calculation + dump verification: " };
static const char *partitionDumpType1MenuItems[] = { "Dump Partition 0 (Update)", "Dump Partition 1 (Normal)", "Dump Partition 2 (Secure)" };
static const char *partitionDumpType2MenuItems[] = { "Dump Partition 0 (Update)", "Dump Partition 1 (Logo)", "Dump Partition 2 (Normal)", "Dump Partition 3 (Secure)" };
static const char *viewGameCardFsType1MenuItems[] = { "View Files from Partition 0 (Update)", "View Files from Partition 1 (Normal)", "View Files from Partition 2 (Secure)" };
static const char *viewGameCardFsType2MenuItems[] = { "View Files from Partition 0 (Update)", "View Files from Partition 1 (Logo)", "View Files from Partition 2 (Normal)", "View Files from Partition 3 (Secure)" };

static unsigned char asciiData[128][8] = {
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x3E, 0x41, 0x55, 0x41, 0x55, 0x49, 0x3E},
	{0x00, 0x3E, 0x7F, 0x6B, 0x7F, 0x6B, 0x77, 0x3E}, {0x00, 0x22, 0x77, 0x7F, 0x7F, 0x3E, 0x1C, 0x08},
	{0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x1C, 0x08}, {0x00, 0x08, 0x1C, 0x2A, 0x7F, 0x2A, 0x08, 0x1C},
	{0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x3E, 0x08, 0x1C}, {0x00, 0x00, 0x1C, 0x3E, 0x3E, 0x3E, 0x1C, 0x00},
	{0xFF, 0xFF, 0xE3, 0xC1, 0xC1, 0xC1, 0xE3, 0xFF}, {0x00, 0x00, 0x1C, 0x22, 0x22, 0x22, 0x1C, 0x00},
	{0xFF, 0xFF, 0xE3, 0xDD, 0xDD, 0xDD, 0xE3, 0xFF}, {0x00, 0x0F, 0x03, 0x05, 0x39, 0x48, 0x48, 0x30},
	{0x00, 0x08, 0x3E, 0x08, 0x1C, 0x22, 0x22, 0x1C}, {0x00, 0x18, 0x14, 0x10, 0x10, 0x30, 0x70, 0x60},
	{0x00, 0x0F, 0x19, 0x11, 0x13, 0x37, 0x76, 0x60}, {0x00, 0x08, 0x2A, 0x1C, 0x77, 0x1C, 0x2A, 0x08},
	{0x00, 0x60, 0x78, 0x7E, 0x7F, 0x7E, 0x78, 0x60}, {0x00, 0x03, 0x0F, 0x3F, 0x7F, 0x3F, 0x0F, 0x03},
	{0x00, 0x08, 0x1C, 0x2A, 0x08, 0x2A, 0x1C, 0x08}, {0x00, 0x66, 0x66, 0x66, 0x66, 0x00, 0x66, 0x66},
	{0x00, 0x3F, 0x65, 0x65, 0x3D, 0x05, 0x05, 0x05}, {0x00, 0x0C, 0x32, 0x48, 0x24, 0x12, 0x4C, 0x30},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x7F, 0x7F}, {0x00, 0x08, 0x1C, 0x2A, 0x08, 0x2A, 0x1C, 0x3E},
	{0x00, 0x08, 0x1C, 0x3E, 0x7F, 0x1C, 0x1C, 0x1C}, {0x00, 0x1C, 0x1C, 0x1C, 0x7F, 0x3E, 0x1C, 0x08},
	{0x00, 0x08, 0x0C, 0x7E, 0x7F, 0x7E, 0x0C, 0x08}, {0x00, 0x08, 0x18, 0x3F, 0x7F, 0x3F, 0x18, 0x08},
	{0x00, 0x00, 0x00, 0x70, 0x70, 0x70, 0x7F, 0x7F}, {0x00, 0x00, 0x14, 0x22, 0x7F, 0x22, 0x14, 0x00},
	{0x00, 0x08, 0x1C, 0x1C, 0x3E, 0x3E, 0x7F, 0x7F}, {0x00, 0x7F, 0x7F, 0x3E, 0x3E, 0x1C, 0x1C, 0x08},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18},
	{0x00, 0x36, 0x36, 0x14, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36},
	{0x00, 0x08, 0x1E, 0x20, 0x1C, 0x02, 0x3C, 0x08}, {0x00, 0x60, 0x66, 0x0C, 0x18, 0x30, 0x66, 0x06},
	{0x00, 0x3C, 0x66, 0x3C, 0x28, 0x65, 0x66, 0x3F}, {0x00, 0x18, 0x18, 0x18, 0x30, 0x00, 0x00, 0x00},
	{0x00, 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06}, {0x00, 0x60, 0x30, 0x18, 0x18, 0x18, 0x30, 0x60},
	{0x00, 0x00, 0x36, 0x1C, 0x7F, 0x1C, 0x36, 0x00}, {0x00, 0x00, 0x08, 0x08, 0x3E, 0x08, 0x08, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x30, 0x60}, {0x00, 0x00, 0x00, 0x00, 0x3C, 0x00, 0x00, 0x00},
	{0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x60, 0x60}, {0x00, 0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x00},
	{0x00, 0x3C, 0x66, 0x6E, 0x76, 0x66, 0x66, 0x3C}, {0x00, 0x18, 0x18, 0x38, 0x18, 0x18, 0x18, 0x7E},
	{0x00, 0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E}, {0x00, 0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C},
	{0x00, 0x0C, 0x1C, 0x2C, 0x4C, 0x7E, 0x0C, 0x0C}, {0x00, 0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C},
	{0x00, 0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x3C}, {0x00, 0x7E, 0x66, 0x0C, 0x0C, 0x18, 0x18, 0x18},
	{0x00, 0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x3C}, {0x00, 0x3C, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C},
	{0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00}, {0x00, 0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30},
	{0x00, 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06}, {0x00, 0x00, 0x00, 0x3C, 0x00, 0x3C, 0x00, 0x00},
	{0x00, 0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60}, {0x00, 0x3C, 0x66, 0x06, 0x1C, 0x18, 0x00, 0x18},
	{0x00, 0x38, 0x44, 0x5C, 0x58, 0x42, 0x3C, 0x00}, {0x00, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66},
	{0x00, 0x7C, 0x66, 0x66, 0x7C, 0x66, 0x66, 0x7C}, {0x00, 0x3C, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3C},
	{0x00, 0x7C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x7C}, {0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x7E},
	{0x00, 0x7E, 0x60, 0x60, 0x7C, 0x60, 0x60, 0x60}, {0x00, 0x3C, 0x66, 0x60, 0x60, 0x6E, 0x66, 0x3C},
	{0x00, 0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66}, {0x00, 0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C},
	{0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x6C, 0x6C, 0x38}, {0x00, 0x66, 0x6C, 0x78, 0x70, 0x78, 0x6C, 0x66},
	{0x00, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7E}, {0x00, 0x63, 0x77, 0x7F, 0x6B, 0x63, 0x63, 0x63},
	{0x00, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x63, 0x63}, {0x00, 0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C},
	{0x00, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x60, 0x60}, {0x00, 0x3C, 0x66, 0x66, 0x66, 0x6E, 0x3C, 0x06},
	{0x00, 0x7C, 0x66, 0x66, 0x7C, 0x78, 0x6C, 0x66}, {0x00, 0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C},
	{0x00, 0x7E, 0x5A, 0x18, 0x18, 0x18, 0x18, 0x18}, {0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3E},
	{0x00, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18}, {0x00, 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63},
	{0x00, 0x63, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x63}, {0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18},
	{0x00, 0x7E, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E}, {0x00, 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E},
	{0x00, 0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00}, {0x00, 0x78, 0x18, 0x18, 0x18, 0x18, 0x18, 0x78},
	{0x00, 0x08, 0x14, 0x22, 0x41, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F},
	{0x00, 0x0C, 0x0C, 0x06, 0x00, 0x00, 0x00, 0x00}, {0x00, 0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E},
	{0x00, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x7C}, {0x00, 0x00, 0x00, 0x3C, 0x66, 0x60, 0x66, 0x3C},
	{0x00, 0x06, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3E}, {0x00, 0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C},
	{0x00, 0x1C, 0x36, 0x30, 0x30, 0x7C, 0x30, 0x30}, {0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C},
	{0x00, 0x60, 0x60, 0x60, 0x7C, 0x66, 0x66, 0x66}, {0x00, 0x00, 0x18, 0x00, 0x18, 0x18, 0x18, 0x3C},
	{0x00, 0x0C, 0x00, 0x0C, 0x0C, 0x6C, 0x6C, 0x38}, {0x00, 0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66},
	{0x00, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18}, {0x00, 0x00, 0x00, 0x63, 0x77, 0x7F, 0x6B, 0x6B},
	{0x00, 0x00, 0x00, 0x7C, 0x7E, 0x66, 0x66, 0x66}, {0x00, 0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C},
	{0x00, 0x00, 0x7C, 0x66, 0x66, 0x7C, 0x60, 0x60}, {0x00, 0x00, 0x3C, 0x6C, 0x6C, 0x3C, 0x0D, 0x0F},
	{0x00, 0x00, 0x00, 0x7C, 0x66, 0x66, 0x60, 0x60}, {0x00, 0x00, 0x00, 0x3E, 0x40, 0x3C, 0x02, 0x7C},
	{0x00, 0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x18}, {0x00, 0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E},
	{0x00, 0x00, 0x00, 0x00, 0x66, 0x66, 0x3C, 0x18}, {0x00, 0x00, 0x00, 0x63, 0x6B, 0x6B, 0x6B, 0x3E},
	{0x00, 0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66}, {0x00, 0x00, 0x00, 0x66, 0x66, 0x3E, 0x06, 0x3C},
	{0x00, 0x00, 0x00, 0x3C, 0x0C, 0x18, 0x30, 0x3C}, {0x00, 0x0E, 0x18, 0x18, 0x30, 0x18, 0x18, 0x0E},
	{0x00, 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18}, {0x00, 0x70, 0x18, 0x18, 0x0C, 0x18, 0x18, 0x70},
	{0x00, 0x00, 0x00, 0x3A, 0x6C, 0x00, 0x00, 0x00}, {0x00, 0x08, 0x1C, 0x36, 0x63, 0x41, 0x41, 0x7F}
};

int uiGetIndex(int x, int y)
{
	return (x + y * currentFBWidth) * 4;
}

void uiFill(int x, int y, int width, int height, u8 r, u8 g, u8 b)
{
	if (currentFB == NULL) return;
	
	if (x + width < 0 || y + height < 0 || x >= currentFBWidth || y >= currentFBHeight) return;

	if (x < 0)
	{
		width += x;
		x = 0;
	}
	
	if (y < 0)
	{
		height += y;
		y = 0;
	}

	if (x + width >= currentFBWidth) width = currentFBWidth - x;

	if (y + height >= currentFBHeight) height = currentFBHeight - y;

	u8 colorLine[width * 4];
	for (int ly = 0; ly < width; ly++)
	{
		colorLine[ly * 4 + 0] = r;
		colorLine[ly * 4 + 1] = g;
		colorLine[ly * 4 + 2] = b;
		colorLine[ly * 4 + 3] = 255;
	}

	u8* fbAddr = currentFB + uiGetIndex(x, y);  // - (width * 4);
	for (int dx = 0; dx < height; dx++)
	{
		memcpy(fbAddr, colorLine, (size_t)(width * 4));
		fbAddr += currentFBWidth * 4;
	}
}

void uiDrawChar(char c, int x, int y, u8 r, u8 g, u8 b)
{
	if (currentFB == NULL) return;

	if (c & 0x80) c = '?';  // Unicode chars

	unsigned char* data = asciiData[(int)c];
	for (int cy = 0; cy < 8; cy++)
	{
		if (y + cy < 0 || y + cy >= currentFBHeight) continue;
		
		unsigned char l = data[cy];
		for (int cx = 0; cx < 8; cx++)
		{
			if (x + cx < 0 || x + cx >= currentFBWidth) continue;
			
			if ((0b10000000 >> cx) & l)
			{
				u8* ptr = &currentFB[uiGetIndex(x + cx, y + cy)];
				*(ptr + 0) = r;
				*(ptr + 1) = g;
				*(ptr + 2) = b;
				*(ptr + 3) = 255;
			}
		}
	}
}

void uiDrawString(const char* string, int x, int y, u8 r, u8 g, u8 b)
{
	if (currentFB == NULL) return;
	
	int len = (int)strlen(string);
	int cx = x;
	int cy = y;
	
	for (int i = 0; i < len; i++)
	{
		char c = string[i];
		if (c == '\n')
		{
			cx = x;
			cy += 8;
		} else {
			uiDrawChar(c, cx, cy, r, g, b);
			cx += 8;
		}
		
		if (cx > (currentFBWidth - 8)) break;
	}
}

void uiStatusMsg(const char* format, ...)
{
	statusMessageFadeout = 1000;
	va_list args;
	va_start(args, format);
	vsnprintf(statusMessage, sizeof(statusMessage) / sizeof(statusMessage[0]), format, args);
	va_end(args);
	
	//printf("Status message: %s\n", statusMessage);
}

void uiUpdateStatusMsg()
{
	if (!strlen(statusMessage) || !statusMessageFadeout) return;
	
	int fadeout = (statusMessageFadeout > 255 ? 255 : statusMessageFadeout);
	uiFill(0, currentFBHeight - 12, currentFBWidth, 8, 50, 50, 50);
	uiDrawString(statusMessage, 4, currentFBHeight - 12, fadeout, fadeout, fadeout);
	statusMessageFadeout -= 4;
}

void uiPleaseWait()
{
	breaks = headlineCnt;
	uiDrawString("Please wait...", 0, breaks * 8, 115, 115, 255);
	syncDisplay();
	delay(2);
}

void uiUpdateFreeSpace()
{
	getSdCardFreeSpace(&freeSpace);
	
	char tmp[32] = {'\0'};
	convertSize(freeSpace, tmp, sizeof(tmp) / sizeof(tmp[0]));
	
	snprintf(freeSpaceStr, sizeof(freeSpaceStr) / sizeof(freeSpaceStr[0]), "Free SD card space: %s.", tmp);
}

void uiInit()
{
	uiState = stateMainMenu;
	cursor = 0;
	scroll = 0;
	headlineCnt = 0;
	
	filenameBuffer = (char*)malloc(FILENAME_BUFFER_SIZE);
	
	int i, headlineLen = strlen(appHeadline);
	for(i = 0; i < headlineLen; i++)
	{
		if (appHeadline[i] == '\n') headlineCnt++;
	}
	
	if (!headlineCnt) headlineCnt = 2;
	
	uiUpdateFreeSpace();
}

void uiDeinit()
{
	if (filenameBuffer) free(filenameBuffer);
	
	if (uiState == stateViewGameCardFsBrowser) fsdevUnmountDevice("view");
}

void uiSetState(UIState state)
{
	uiState = state;
	cursor = 0;
	scroll = 0;
}

UIState uiGetState()
{
	return uiState;
}

void uiClearScreen()
{
	uiFill(0, 0, currentFBWidth, currentFBHeight, 50, 50, 50);
}

void uiPrintHeadline()
{
	uiClearScreen();
	uiDrawString(appHeadline, 0, 0, 255, 255, 255);
}

static void enterDirectory(const char *path)
{
	snprintf(currentDirectory, sizeof(currentDirectory) / sizeof(currentDirectory[0]), "%s", path);
	
	filenamesCount = FILENAME_MAX_CNT;
	getDirectoryContents(filenameBuffer, &filenames[0], &filenamesCount, currentDirectory, (!strcmp(currentDirectory, "view:/") && strlen(currentDirectory) == 6));
	
	cursor = 0;
	scroll = 0;
}

UIResult uiLoop(u32 keysDown)
{
	UIResult res = resultNone;
	
	int i;
	breaks = headlineCnt;
	
	if (uiState == stateMainMenu || uiState == stateXciDumpMenu || uiState == stateRawPartitionDumpMenu || uiState == statePartitionDataDumpMenu || uiState == stateViewGameCardFsMenu || uiState == stateViewGameCardFsBrowser)
	{
		// Exit
		if (keysDown & KEY_PLUS) return resultExit;
		
		uiDrawString(appControls, 0, breaks * 8, 255, 255, 255);
		breaks += 2;
		
		uiDrawString(freeSpaceStr, 0, breaks * 8, 255, 255, 255);
		breaks += 2;
		
		if (uiState != stateViewGameCardFsBrowser)
		{
			if (gameCardInserted && hfs0_header != NULL && (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT || hfs0_partition_cnt == GAMECARD_TYPE2_PARTITION_CNT) && gameCardTitleID != 0)
			{
				uiDrawString("Game Card is inserted!", 0, breaks * 8, 0, 255, 0);
				breaks += 2;
				
				/*snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Root HFS0 header offset: 0x%016lX", hfs0_offset);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Root HFS0 header size: 0x%016lX", hfs0_size);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;*/
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Name: %s", gameCardName);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Developer: %s", gameCardAuthor);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Title ID: %016lX", gameCardTitleID);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Version: %s", gameCardVersionStr);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Size: %s", gameCardSizeStr);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Used space: %s", trimmedCardSizeStr);
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				breaks++;
				
				snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Partition count: %u (%s)", hfs0_partition_cnt, GAMECARD_TYPE(hfs0_partition_cnt));
				uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				
				if (strlen(gameCardUpdateVersionStr))
				{
					breaks++;
					snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Game Card FW Version: %s", gameCardUpdateVersionStr);
					uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
				}
			} else {
				if (gameCardInserted)
				{
					if (hfs0_header != NULL)
					{
						if (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT || hfs0_partition_cnt == GAMECARD_TYPE2_PARTITION_CNT)
						{
							uiDrawString("Error: unable to retrieve the game card Title ID!", 0, breaks * 8, 255, 0, 0);
							
							if (strlen(gameCardUpdateVersionStr))
							{
								breaks++;
								snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Game Card FW Version: %s", gameCardUpdateVersionStr);
								uiDrawString(titlebuf, 0, breaks * 8, 0, 255, 0);
								breaks++;
								
								uiDrawString("In order to be able to dump data from this cartridge, make sure your console is at least on this FW version.", 0, breaks * 8, 255, 255, 255);
							}
						} else {
							snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Error: unknown root HFS0 header partition count! (%u)", hfs0_partition_cnt);
							uiDrawString(titlebuf, 0, breaks * 8, 255, 0, 0);
						}
					} else {
						uiDrawString("Error: unable to get root HFS0 header data!", 0, breaks * 8, 255, 0, 0);
					}
				} else {
					uiDrawString("Game Card is not inserted!", 0, breaks * 8, 255, 0, 0);
				}
				
				res = resultShowMainMenu;
			}
			
			breaks += 2;
		}
		
		if (gameCardInserted && hfs0_header != NULL && (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT || hfs0_partition_cnt == GAMECARD_TYPE2_PARTITION_CNT) && gameCardTitleID != 0)
		{
			const char **menu = NULL;
			int menuItemsCount;
			
			switch(uiState)
			{
				case stateMainMenu:
					menu = mainMenuItems;
					menuItemsCount = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);
					break;
				case stateXciDumpMenu:
					menu = xciDumpMenuItems;
					menuItemsCount = sizeof(xciDumpMenuItems) / sizeof(xciDumpMenuItems[0]);
					
					uiDrawString(mainMenuItems[0], 0, breaks * 8, 115, 115, 255);
					
					break;
				case stateRawPartitionDumpMenu:
				case statePartitionDataDumpMenu:
					menu = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? partitionDumpType1MenuItems : partitionDumpType2MenuItems);
					menuItemsCount = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? (sizeof(partitionDumpType1MenuItems) / sizeof(partitionDumpType1MenuItems[0])) : (sizeof(partitionDumpType2MenuItems) / sizeof(partitionDumpType2MenuItems[0])));
					
					uiDrawString((uiState == stateRawPartitionDumpMenu ? mainMenuItems[1] : mainMenuItems[2]), 0, breaks * 8, 115, 115, 255);
					
					break;
				case stateViewGameCardFsMenu:
					menu = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? viewGameCardFsType1MenuItems : viewGameCardFsType2MenuItems);
					menuItemsCount = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? (sizeof(viewGameCardFsType1MenuItems) / sizeof(viewGameCardFsType1MenuItems[0])) : (sizeof(viewGameCardFsType2MenuItems) / sizeof(viewGameCardFsType2MenuItems[0])));
					
					uiDrawString(mainMenuItems[3], 0, breaks * 8, 115, 115, 255);
					
					break;
				case stateViewGameCardFsBrowser:
					menu = (const char**)filenames;
					menuItemsCount = filenamesCount;
					
					uiDrawString((hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? viewGameCardFsType1MenuItems[selectedOption] : viewGameCardFsType2MenuItems[selectedOption]), 0, breaks * 8, 115, 115, 255);
					breaks += 2;
					
					snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Current directory: %s", currentDirectory);
					uiDrawString(titlebuf, 0, breaks * 8, 255, 255, 255);
					
					break;
				default:
					break;
			}
			
			if (menu && menuItemsCount)
			{
				if (uiState != stateMainMenu) breaks += 2;
				
				int scrollAmount = 0;
				if ((keysDown & KEY_DOWN) || (keysDown & KEY_RSTICK_DOWN) || (keysDown & KEY_LSTICK_DOWN)) scrollAmount = 1;
				if ((keysDown & KEY_UP) || (keysDown & KEY_RSTICK_UP) || (keysDown & KEY_LSTICK_UP)) scrollAmount = -1;
				if (uiState != stateXciDumpMenu && ((keysDown & KEY_LEFT) || (keysDown & KEY_RSTICK_LEFT) || (keysDown & KEY_LSTICK_LEFT))) scrollAmount = -5;
				if (uiState != stateXciDumpMenu && ((keysDown & KEY_RIGHT) || (keysDown & KEY_RSTICK_RIGHT) || (keysDown & KEY_LSTICK_RIGHT))) scrollAmount = 5;
				
				if (scrollAmount > 0)
				{
					for (i = 0; i < scrollAmount; i++)
					{
						if (cursor < menuItemsCount - 1)
						{
							cursor++;
							if ((cursor - scroll) >= maxListElements) scroll++;
						}
					}
				} else
				if (scrollAmount < 0)
				{
					for (i = 0; i < -scrollAmount; i++)
					{
						if (cursor > 0)
						{
							cursor--;
							if ((cursor - scroll) < 0) scroll--;
						}
					}
				}
				
				i = 0;
				
				for (int j = scroll; j < menuItemsCount; j++)
				{
					u8 color = 255;
					
					if (i + scroll == cursor)
					{
						uiFill(0, (breaks * 8) + (i * 13), currentFBWidth / 2, 13, 33, 34, 39);
						uiDrawString(menu[j], 0, (breaks * 8) + (i * 13) + 2, 0, 255, 197);
					} else {
						uiDrawString(menu[j], 0, (breaks * 8) + (i * 13) + 2, color, color, color);
					}
					
					// Print XCI dump menu settings values
					if (uiState == stateXciDumpMenu && i > 0)
					{
						switch(i)
						{
							case 1: // Split output dump (FAT32 support)
								if (isFat32)
								{
									uiDrawString("Yes", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 0, 255, 0);
								} else {
									uiDrawString("No", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 255, 0, 0);
								}
								break;
							case 2: // Dump certificate
								if (dumpCert)
								{
									uiDrawString("Yes", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 0, 255, 0);
								} else {
									uiDrawString("No", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 255, 0, 0);
								}
								break;
							case 3: // Trim output dump
								if (trimDump)
								{
									uiDrawString("Yes", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 0, 255, 0);
								} else {
									uiDrawString("No", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 255, 0, 0);
								}
								break;
							case 4: // CRC32 checksum calculation + dump verification
								if (calcCrc)
								{
									uiDrawString("Yes", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 0, 255, 0);
								} else {
									uiDrawString("No", strlen(menu[i]) * 8, (breaks * 8) + (i * 13) + 2, 255, 0, 0);
								}
								break;
							default:
								break;
						}
					}
					
					i++;
					
					if (i >= maxListElements) break;
				}
				
				if (uiState == stateXciDumpMenu)
				{
					// Select
					if ((keysDown & KEY_A) && cursor == 0)
					{
						selectedOption = (u32)cursor;
						res = resultDumpXci;
					}
					
					// Back
					if (keysDown & KEY_B) res = resultShowMainMenu;
					
					// Change option to false
					if ((keysDown & KEY_LEFT) || (keysDown & KEY_RSTICK_LEFT) || (keysDown & KEY_LSTICK_LEFT))
					{
						switch(cursor)
						{
							case 1: // Split output dump (FAT32 support)
								isFat32 = false;
								break;
							case 2: // Dump certificate
								dumpCert = false;
								break;
							case 3: // Trim output dump
								trimDump = false;
								break;
							case 4: // CRC32 checksum calculation + dump verification
								calcCrc = false;
								break;
							default:
								break;
						}
					}
					
					// Change option to true
					if ((keysDown & KEY_RIGHT) || (keysDown & KEY_RSTICK_RIGHT) || (keysDown & KEY_LSTICK_RIGHT))
					{
						switch(cursor)
						{
							case 1: // Split output dump (FAT32 support)
								isFat32 = true;
								break;
							case 2: // Dump certificate
								dumpCert = true;
								break;
							case 3: // Trim output dump
								trimDump = true;
								break;
							case 4: // CRC32 checksum calculation + dump verification
								calcCrc = true;
								break;
							default:
								break;
						}
					}
				} else {
					// Select
					if (keysDown & KEY_A)
					{
						if (uiState == stateMainMenu)
						{
							switch(cursor)
							{
								case 0:
									res = resultShowXciDumpMenu;
									break;
								case 1:
									res = resultShowRawPartitionDumpMenu;
									break;
								case 2:
									res = resultShowPartitionDataDumpMenu;
									break;
								case 3:
									res = resultShowViewGameCardFsMenu;
									break;
								case 4:
									res = resultDumpGameCardCertificate;
									break;
								case 5:
									res = resultUpdateNSWDBXml;
									break;
								case 6:
									//res = resultUpdateApplication;
									break;
								default:
									break;
							}
						} else
						if (uiState == stateRawPartitionDumpMenu)
						{
							selectedOption = (u32)cursor;
							res = resultDumpRawPartition;
						} else
						if (uiState == statePartitionDataDumpMenu)
						{
							selectedOption = (u32)cursor;
							res = resultDumpPartitionData;
						} else
						if (uiState == stateViewGameCardFsMenu)
						{
							selectedOption = (u32)cursor;
							res = resultShowViewGameCardFsGetList;
						} else
						if (uiState == stateViewGameCardFsBrowser)
						{
							char *selectedPath = (char*)malloc(strlen(currentDirectory) + 1 + strlen(filenames[cursor]) + 2);
							memset(selectedPath, 0, strlen(currentDirectory) + 1 + strlen(filenames[cursor]) + 2);
							
							if (strlen(filenames[cursor]) == 2 && !strcmp(filenames[cursor], ".."))
							{
								for(i = (strlen(currentDirectory) - 1); i >= 0; i--)
								{
									if (currentDirectory[i] == '/')
									{
										strncpy(selectedPath, currentDirectory, i);
										selectedPath[i] = '\0';
										break;
									}
								}
							} else {
								snprintf(selectedPath, strlen(currentDirectory) + 1 + strlen(filenames[cursor]) + 2, "%s/%s", currentDirectory, filenames[cursor]);
							}
							
							if (isDirectory(selectedPath))
							{
								enterDirectory(selectedPath);
							} else {
								snprintf(fileCopyPath, sizeof(fileCopyPath) / sizeof(fileCopyPath[0]), "%s", selectedPath);
								res = resultViewGameCardFsBrowserCopyFile;
							}
							
							free(selectedPath);
						}
					}
					
					// Back
					if (keysDown & KEY_B)
					{
						if (uiState == stateRawPartitionDumpMenu || uiState == statePartitionDataDumpMenu || uiState == stateViewGameCardFsMenu)
						{
							res = resultShowMainMenu;
						} else
						if (uiState == stateViewGameCardFsBrowser)
						{
							if (!strcmp(currentDirectory, "view:/") && strlen(currentDirectory) == 6)
							{
								fsdevUnmountDevice("view");
								
								res = resultShowViewGameCardFsMenu;
							} else {
								char *selectedPath = (char*)malloc(strlen(currentDirectory) + 1);
								memset(selectedPath, 0, strlen(currentDirectory) + 1);
								
								for(i = (strlen(currentDirectory) - 1); i >= 0; i--)
								{
									if (currentDirectory[i] == '/')
									{
										strncpy(selectedPath, currentDirectory, i);
										selectedPath[i] = '\0';
										break;
									}
								}
								
								if (isDirectory(selectedPath)) enterDirectory(selectedPath);
								
								free(selectedPath);
							}
						}
					}
				}
			}
		}
	} else
	if (uiState == stateDumpXci)
	{
		uiDrawString(mainMenuItems[0], 0, breaks * 8, 115, 115, 255);
		breaks++;
		
		snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "%s%s", xciDumpMenuItems[1], (isFat32 ? "Yes" : "No"));
		uiDrawString(titlebuf, 0, breaks * 8, 115, 115, 255);
		breaks++;
		
		snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "%s%s", xciDumpMenuItems[2], (dumpCert ? "Yes" : "No"));
		uiDrawString(titlebuf, 0, breaks * 8, 115, 115, 255);
		breaks++;
		
		snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "%s%s", xciDumpMenuItems[3], (trimDump ? "Yes" : "No"));
		uiDrawString(titlebuf, 0, breaks * 8, 115, 115, 255);
		breaks++;
		
		snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "%s%s", xciDumpMenuItems[4], (calcCrc ? "Yes" : "No"));
		uiDrawString(titlebuf, 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		dumpGameCartridge(&fsOperatorInstance, isFat32, dumpCert, trimDump, calcCrc);
		
		waitForButtonPress();
		
		uiUpdateFreeSpace();
		res = resultShowXciDumpMenu;
	} else
	if (uiState == stateDumpRawPartition)
	{
		snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Raw %s", (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? partitionDumpType1MenuItems[selectedOption] : partitionDumpType2MenuItems[selectedOption]));
		uiDrawString(titlebuf, 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		dumpRawPartition(&fsOperatorInstance, selectedOption, true);
		
		waitForButtonPress();
		
		uiUpdateFreeSpace();
		res = resultShowRawPartitionDumpMenu;
	} else
	if (uiState == stateDumpPartitionData)
	{
		snprintf(titlebuf, sizeof(titlebuf) / sizeof(titlebuf[0]), "Data %s", (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? partitionDumpType1MenuItems[selectedOption] : partitionDumpType2MenuItems[selectedOption]));
		uiDrawString(titlebuf, 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		dumpPartitionData(&fsOperatorInstance, selectedOption);
		
		waitForButtonPress();
		
		uiUpdateFreeSpace();
		res = resultShowPartitionDataDumpMenu;
	} else
	if (uiState == stateViewGameCardFsGetList)
	{
		uiDrawString((hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? viewGameCardFsType1MenuItems[selectedOption] : viewGameCardFsType1MenuItems[selectedOption]), 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		if (mountViewPartition(&fsOperatorInstance, selectedOption))
		{
			enterDirectory("view:/");
			
			res = resultShowViewGameCardFsBrowser;
		} else {
			breaks += 2;
			waitForButtonPress();
			res = resultShowViewGameCardFsMenu;
		}
	} else
	if (uiState == stateViewGameCardFsBrowserCopyFile)
	{
		uiDrawString("Manual File Dump", 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		FILE *inFile = fopen(fileCopyPath, "rb");
		if (inFile)
		{
			fseek(inFile, 0L, SEEK_END);
			u64 input_filesize = ftell(inFile);
			fclose(inFile);
			
			if (input_filesize <= freeSpace)
			{
				char destCopyPath[NAME_BUF_LEN] = {'\0'};
				
				for(i = (strlen(fileCopyPath) - 1); i >= 0; i--)
				{
					if (fileCopyPath[i] == '/')
					{
						snprintf(destCopyPath, sizeof(destCopyPath) / sizeof(destCopyPath[0]), "sdmc:/%s v%u (%016lX) - Partition %u (%s)", fixedGameCardName, gameCardVersion, gameCardTitleID, selectedOption, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, selectedOption));
						mkdir(destCopyPath, 0744);
						
						snprintf(destCopyPath, sizeof(destCopyPath) / sizeof(destCopyPath[0]), "sdmc:/%s v%u (%016lX) - Partition %u (%s)/%.*s", fixedGameCardName, gameCardVersion, gameCardTitleID, selectedOption, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, selectedOption), (int)(strlen(fileCopyPath) - i), fileCopyPath + i + 1);
						break;
					}
				}
				
				uiDrawString("Hold B to cancel", 0, breaks * 8, 255, 255, 255);
				breaks += 2;
				
				copyFile(fileCopyPath, destCopyPath, true, true);
			} else {
				uiDrawString("Error: not enough free space available in the SD card.", 0, breaks * 8, 255, 0, 0);
			}
		} else {
			uiDrawString("Error: unable to get input file size.", 0, breaks * 8, 255, 0, 0);
		}
		
		breaks += 2;
		
		waitForButtonPress();
		
		uiUpdateFreeSpace();
		res = resultShowViewGameCardFsBrowser;
	} else
	if (uiState == stateDumpGameCardCertificate)
	{
		uiDrawString(mainMenuItems[4], 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		dumpGameCertificate(&fsOperatorInstance);
		
		waitForButtonPress();
		
		uiUpdateFreeSpace();
		res = resultShowMainMenu;
	} else
	if (uiState == stateUpdateNSWDBXml)
	{
		uiDrawString(mainMenuItems[5], 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		updateNSWDBXml();
		
		waitForButtonPress();
		
		uiUpdateFreeSpace();
		res = resultShowMainMenu;
	} else
	if (uiState == stateUpdateApplication)
	{
		uiDrawString(mainMenuItems[6], 0, breaks * 8, 115, 115, 255);
		breaks += 2;
		
		updateApplication();
		
		waitForButtonPress();
		
		uiUpdateFreeSpace();
		res = resultShowMainMenu;
	}
	
	uiUpdateStatusMsg();
	
	return res;
}
