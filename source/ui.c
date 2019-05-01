#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "dumper.h"
#include "fsext.h"
#include "ui.h"
#include "util.h"

/* Extern variables */

extern FsDeviceOperator fsOperatorInstance;

extern AppletType programAppletType;

extern bool gameCardInserted;

extern char gameCardSizeStr[32], trimmedCardSizeStr[32];

extern char *hfs0_header;
extern u64 hfs0_offset, hfs0_size;
extern u32 hfs0_partition_cnt;

extern char *partitionHfs0Header;
extern u64 partitionHfs0HeaderOffset, partitionHfs0HeaderSize;
extern u32 partitionHfs0FileCount, partitionHfs0StrTableSize;

extern u32 gameCardAppCount;
extern u64 *gameCardTitleID;

extern char **gameCardName;
extern char **gameCardAuthor;
extern char **gameCardVersionStr;

extern char gameCardUpdateVersionStr[128];

extern char *filenameBuffer;
extern char *filenames[FILENAME_MAX_CNT];
extern int filenamesCount;

extern char strbuf[NAME_BUF_LEN * 4];

/* Statically allocated variables */

static PlFontData font;
static FT_Library library;
static FT_Face face;
static Framebuffer fb;

static u32 *framebuf = NULL;
static u32 framebuf_width = 0;

int cursor = 0;
int scroll = 0;
int breaks = 0;
int font_height = 0;

static u32 selectedPartitionIndex;
static u32 selectedFileIndex;
static u32 selectedAppIndex;

static bool highlight = false;

static bool isFat32 = false, dumpCert = false, trimDump = false, calcCrc = true;

static char statusMessage[2048] = {'\0'};
static int statusMessageFadeout = 0;

u64 freeSpace = 0;
static char freeSpaceStr[64] = {'\0'};

static const int maxListElements = 15;

static UIState uiState;

static const char *appHeadline = "Nintendo Switch Game Card Dump Tool v" APP_VERSION ".\nOriginal codebase by MCMrARM.\nUpdated and maintained by DarkMatterCore.\n\n";
static const char *appControls = "[D-Pad / Analog Sticks] Move | [A] Select | [B] Back | [+] Exit";

static const char *mainMenuItems[] = { "Cartridge Image (XCI) dump", "Nintendo Submission Package (NSP) dump", "Raw partition dump", "Partition data dump", "View game card files", "Dump game card certificate", "Update NSWDB.COM XML database", "Update application" };
static const char *xciDumpMenuItems[] = { "Start XCI dump process", "Split output dump (FAT32 support): ", "Dump certificate: ", "Trim output dump: ", "CRC32 checksum calculation + dump verification: " };
static const char *nspDumpMenuItems[] = { "Start NSP dump process", "Split output dump (FAT32 support): ", "CRC32 checksum calculation: ", "Bundled application to dump: " };
static const char *partitionDumpType1MenuItems[] = { "Dump partition 0 (Update)", "Dump partition 1 (Normal)", "Dump partition 2 (Secure)" };
static const char *partitionDumpType2MenuItems[] = { "Dump partition 0 (Update)", "Dump partition 1 (Logo)", "Dump partition 2 (Normal)", "Dump partition 3 (Secure)" };
static const char *viewGameCardFsType1MenuItems[] = { "View files from partition 0 (Update)", "View files from partition 1 (Normal)", "View files from partition 2 (Secure)" };
static const char *viewGameCardFsType2MenuItems[] = { "View files from partition 0 (Update)", "View files from partition 1 (Logo)", "View files from partition 2 (Normal)", "View files from partition 3 (Secure)" };

void uiFill(int x, int y, int width, int height, u8 r, u8 g, u8 b)
{
    /* Perform validity checks */
	if ((x + width) < 0 || (y + height) < 0 || x >= FB_WIDTH || y >= FB_HEIGHT) return;
    
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

	if ((x + width) >= FB_WIDTH) width = (FB_WIDTH - x);

	if ((y + height) >= FB_HEIGHT) height = (FB_HEIGHT - y);
    
    if (framebuf == NULL)
    {
        /* Begin new frame */
        u32 stride;
        framebuf = (u32*)framebufferBegin(&fb, &stride);
        framebuf_width = (stride / sizeof(u32));
    }
    
    u32 lx, ly;
    u32 framex, framey;
    
    for (ly = 0; ly < height; ly++)
    {
        for (lx = 0; lx < width; lx++)
        {
            framex = (x + lx);
            framey = (y + ly);
            
            framebuf[(framey * framebuf_width) + framex] = RGBA8_MAXALPHA(r, g, b);
        }
    }
}

void uiDrawChar(FT_Bitmap *bitmap, int x, int y, u8 r, u8 g, u8 b)
{
    if (framebuf == NULL) return;
    
    u32 framex, framey;
    u32 tmpx, tmpy;
    u8 *imageptr = bitmap->buffer;
    
    u8 src_val;
    float opacity;
    
    u8 fontR;
    u8 fontG;
    u8 fontB;
    
    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY) return;
    
    for(tmpy = 0; tmpy < bitmap->rows; tmpy++)
    {
        for (tmpx = 0; tmpx < bitmap->width; tmpx++)
        {
            framex = (x + tmpx);
            framey = (y + tmpy);
            
            if (framex >= FB_WIDTH || framey >= FB_HEIGHT) continue;
            
            src_val = imageptr[tmpx];
            if (!src_val)
            {
                /* Render background color */
                if (highlight)
                {
                    framebuf[(framey * framebuf_width) + framex] = RGBA8_MAXALPHA(HIGHLIGHT_BG_COLOR_R, HIGHLIGHT_BG_COLOR_G, HIGHLIGHT_BG_COLOR_B);
                } else {
                    framebuf[(framey * framebuf_width) + framex] = RGBA8_MAXALPHA(BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
                }
            } else {
                /* Calculate alpha (opacity) */
                opacity = (src_val / 255.0);
                
                if (highlight)
                {
                    fontR = (r * opacity + (1 - opacity) * HIGHLIGHT_BG_COLOR_R);
                    fontG = (g * opacity + (1 - opacity) * HIGHLIGHT_BG_COLOR_G);
                    fontB = (b * opacity + (1 - opacity) * HIGHLIGHT_BG_COLOR_B);
                } else {
                    fontR = (r * opacity + (1 - opacity) * BG_COLOR_RGB);
                    fontG = (g * opacity + (1 - opacity) * BG_COLOR_RGB);
                    fontB = (b * opacity + (1 - opacity) * BG_COLOR_RGB);
                }
                
                framebuf[(framey * framebuf_width) + framex] = RGBA8_MAXALPHA(fontR, fontG, fontB);
            }
        }
        
        imageptr += bitmap->pitch;
    }
}

void uiScroll()
{
    if (framebuf == NULL)
    {
        /* Begin new frame */
        u32 stride;
        framebuf = (u32*)framebufferBegin(&fb, &stride);
        framebuf_width = (stride / sizeof(u32));
    }
    
    u32 lx, ly;
    
    for (ly = 0; ly < (FB_HEIGHT - font_height); ly++)
    {
        for (lx = 0; lx < FB_WIDTH; lx++)
        {
            framebuf[(ly * framebuf_width) + lx] = framebuf[((ly + font_height) * framebuf_width) + lx];
        }
    }
    
    uiFill(0, FB_HEIGHT - font_height, FB_WIDTH, font_height, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
}

void uiDrawString(const char *string, int x, int y, u8 r, u8 g, u8 b)
{
    u32 tmpx = x;
    u32 tmpy = (font_height + y);
    FT_Error ret = 0;
    FT_UInt glyph_index;
    FT_GlyphSlot slot = face->glyph;
    
    u32 i;
    u32 str_size = strlen(string);
    uint32_t tmpchar;
    ssize_t unitcount = 0;
    
    if (framebuf == NULL)
    {
        /* Begin new frame */
        u32 stride;
        framebuf = (u32*)framebufferBegin(&fb, &stride);
        framebuf_width = (stride / sizeof(u32));
    }
    
    if (tmpy >= FB_HEIGHT)
    {
        tmpy = (FB_HEIGHT - font_height);
        uiScroll();
    }
    
    for(i = 0; i < str_size;)
    {
        unitcount = decode_utf8(&tmpchar, (const uint8_t*)&string[i]);
        if (unitcount <= 0) break;
        i += unitcount;
        
        if (tmpchar == '\n')
        {
            tmpx = 0;
            tmpy += font_height;
            breaks++;
            continue;
        } else
        if (tmpchar == '\t')
        {
            tmpx += (font_height * TAB_WIDTH);
            continue;
        } else
        if (tmpchar == '\r')
        {
            continue;
        }
        
        glyph_index = FT_Get_Char_Index(face, tmpchar);
        
        ret = FT_Load_Glyph(face, glyph_index, FT_LOAD_DEFAULT);
        if (ret == 0) ret = FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL);
        
        if (ret) break;
        
        if ((tmpx + (slot->advance.x >> 6)) >= FB_WIDTH)
        {
            tmpx = 0;
            tmpy += font_height;
            breaks++;
        }
        
        uiDrawChar(&slot->bitmap, tmpx + slot->bitmap_left, tmpy - slot->bitmap_top, r, g, b);
        
        tmpx += (slot->advance.x >> 6);
        tmpy += (slot->advance.y >> 6);
    }
}

void uiRefreshDisplay()
{
    if (framebuf != NULL)
    {
        framebufferEnd(&fb);
        framebuf = NULL;
        framebuf_width = 0;
    }
}

void uiStatusMsg(const char *fmt, ...)
{
	statusMessageFadeout = 1000;
	
	va_list args;
	va_start(args, fmt);
	vsnprintf(statusMessage, sizeof(statusMessage) / sizeof(statusMessage[0]), fmt, args);
	va_end(args);
}

void uiUpdateStatusMsg()
{
	if (!strlen(statusMessage) || !statusMessageFadeout) return;
	
    if ((statusMessageFadeout - 4) > BG_COLOR_RGB)
    {
        int fadeout = (statusMessageFadeout > 255 ? 255 : statusMessageFadeout);
        uiFill(0, FB_HEIGHT - (font_height * 2), FB_WIDTH, font_height, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
        uiDrawString(statusMessage, 0, FB_HEIGHT - (font_height * 2), fadeout, fadeout, fadeout);
        statusMessageFadeout -= 4;
    } else {
        statusMessageFadeout = 0;
    }
}

void uiPleaseWait(u8 wait)
{
    uiDrawString("Please wait...", 0, breaks * font_height, 115, 115, 255);
    uiRefreshDisplay();
    if (wait) delay(wait);
}

void uiUpdateFreeSpace()
{
    getSdCardFreeSpace(&freeSpace);
    
    char tmp[32] = {'\0'};
    convertSize(freeSpace, tmp, sizeof(tmp) / sizeof(tmp[0]));
    
    snprintf(freeSpaceStr, sizeof(freeSpaceStr) / sizeof(freeSpaceStr[0]), "Free SD card space: %s.", tmp);
}

void uiClearScreen()
{
    uiFill(0, 0, FB_WIDTH, FB_HEIGHT, BG_COLOR_RGB, BG_COLOR_RGB, BG_COLOR_RGB);
}

void uiPrintHeadline()
{
    breaks = 0;
    uiClearScreen();
    uiDrawString(appHeadline, 0, 0, 255, 255, 255);
}

int error_screen(const char *fmt, ...)
{
    consoleInit(NULL);
    
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    
    printf("Press [+] to exit.\n");
    
    while (appletMainLoop())
    {
        hidScanInput();
        if (hidKeysDown(CONTROLLER_P1_AUTO) & KEY_PLUS) break;
        consoleUpdate(NULL);
    }
    
    consoleExit(NULL);
    
    return 0;
}

int uiInit()
{
    Result rc = 0;
    FT_Error ret = 0;
    
    /* Initialize pl service */
    rc = plInitialize();
    if (R_FAILED(rc)) return error_screen("plInitialize() failed: 0x%x\n", rc);
    
    /* Retrieve shared font */
    rc = plGetSharedFontByType(&font, PlSharedFontType_Standard);
    if (R_FAILED(rc))
    {
        plExit();
        return error_screen("plGetSharedFontByType() failed: 0x%x\n", rc);
    }
    
    /* Initialize FreeType */
    ret = FT_Init_FreeType(&library);
    if (ret)
    {
        plExit();
        return error_screen("FT_Init_FreeType() failed: %d\n", ret);
    }
    
    /* Create memory face */
    ret = FT_New_Memory_Face(library, font.address, font.size, 0, &face);
    if (ret)
    {
        FT_Done_FreeType(library);
        plExit();
        return error_screen("FT_New_Memory_Face() failed: %d\n", ret);
    }
    
    /* Set font character size */
    ret = FT_Set_Char_Size(face, 0, CHAR_PT_SIZE * 64, SCREEN_DPI_CNT, SCREEN_DPI_CNT);
    if (ret)
    {
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        plExit();
        return error_screen("FT_Set_Char_Size() failed: %d\n", ret);
    }
    
    /* Store font height and max width */
    font_height = (face->size->metrics.height / 64);
    
    /* Create framebuffer */
    framebufferCreate(&fb, nwindowGetDefault(), FB_WIDTH, FB_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);
    
    /* Prepare additional data needed by the UI functions */
    uiState = stateMainMenu;
    cursor = 0;
    scroll = 0;
    
    filenameBuffer = (char*)calloc(1, FILENAME_BUFFER_SIZE);
    if (!filenameBuffer)
    {
        framebufferClose(&fb);
        FT_Done_Face(face);
        FT_Done_FreeType(library);
        plExit();
        return error_screen("Failed to allocate memory for the filename buffer.\n");
    }
    
    uiUpdateFreeSpace();
    
    /* Disable screen dimming and auto sleep */
    appletSetMediaPlaybackState(true);
    
    /* Get applet type */
    programAppletType = appletGetAppletType();
    
    /* Block HOME menu button presses if we're running as a regular application or a system application */
    if (programAppletType == AppletType_Application || programAppletType == AppletType_SystemApplication) appletBeginBlockingHomeButton(0);
    
    /* Clear screen */
    uiClearScreen();
    
    return 1;
}

void uiDeinit()
{
    /* Unblock HOME menu button presses if we're running as a regular application or a system application */
    if (programAppletType == AppletType_Application || programAppletType == AppletType_SystemApplication) appletEndBlockingHomeButton();
    
    /* Enable screen dimming and auto sleep */
    appletSetMediaPlaybackState(false);
    
    /* Free filename buffer */
    if (filenameBuffer) free(filenameBuffer);
    
    /* Free framebuffer object */
    framebufferClose(&fb);
    
    /* Free FreeType resources */
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    
    /* Deinitialize pl service */
    plExit();
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

UIResult uiProcess()
{
    UIResult res = resultNone;
    
    int i, j;
    
    const char **menu = NULL;
    int menuItemsCount = 0;
    
    u32 keysDown;
    u32 keysHeld;
    
    uiPrintHeadline();
    loadGameCardInfo();
    
    if (uiState == stateMainMenu || uiState == stateXciDumpMenu || uiState == stateNspDumpMenu || uiState == stateRawPartitionDumpMenu || uiState == statePartitionDataDumpMenu || uiState == stateViewGameCardFsMenu || uiState == stateViewGameCardFsBrowser)
    {
        uiDrawString(appControls, 0, breaks * font_height, 255, 255, 255);
        breaks += 2;
        
        uiDrawString(freeSpaceStr, 0, breaks * font_height, 255, 255, 255);
        breaks += 2;
        
        if (uiState != stateViewGameCardFsBrowser)
        {
            if (gameCardInserted && hfs0_header != NULL && (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT || hfs0_partition_cnt == GAMECARD_TYPE2_PARTITION_CNT) && gameCardAppCount > 0 && gameCardTitleID != NULL)
            {
                uiDrawString("Game card is inserted!", 0, breaks * font_height, 0, 255, 0);
                breaks += 2;
                
                /*snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Root HFS0 header offset: 0x%016lX", hfs0_offset);
                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                breaks++;
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Root HFS0 header size: 0x%016lX", hfs0_size);
                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                breaks++;*/
                
                u32 app;
                for(app = 0; app < gameCardAppCount; app++)
                {
                    if (gameCardAppCount > 1)
                    {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Bundled application #%u:", app + 1);
                        uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                        breaks++;
                    }
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Name: %s", gameCardName[app]);
                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                    breaks++;
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Developer: %s", gameCardAuthor[app]);
                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                    breaks++;
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Title ID: %016lX", gameCardTitleID[app]);
                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                    breaks++;
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Version: %s", gameCardVersionStr[app]);
                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                    breaks += (gameCardAppCount > 1 ? 2 : 1);
                }
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Size: %s", gameCardSizeStr);
                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                breaks++;
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Used space: %s", trimmedCardSizeStr);
                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                breaks++;
                
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Partition count: %u (%s)", hfs0_partition_cnt, GAMECARD_TYPE(hfs0_partition_cnt));
                uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                
                if (strlen(gameCardUpdateVersionStr))
                {
                    breaks++;
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Bundled FW update: %s", gameCardUpdateVersionStr);
                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                }
            } else {
                if (gameCardInserted)
                {
                    if (hfs0_header != NULL)
                    {
                        if (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT || hfs0_partition_cnt == GAMECARD_TYPE2_PARTITION_CNT)
                        {
                            if (gameCardAppCount > 0)
                            {
                                uiDrawString("Error: unable to retrieve the game card Title ID!", 0, breaks * font_height, 255, 0, 0);
                                
                                if (strlen(gameCardUpdateVersionStr))
                                {
                                    breaks++;
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Bundled FW Update: %s", gameCardUpdateVersionStr);
                                    uiDrawString(strbuf, 0, breaks * font_height, 0, 255, 0);
                                    breaks++;
                                    
                                    uiDrawString("In order to be able to dump data from this cartridge, make sure your console is at least on this FW version.", 0, breaks * font_height, 255, 255, 255);
                                }
                            } else {
                                uiDrawString("Error: gamecard application count is zero!", 0, breaks * font_height, 255, 0, 0);
                            }
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unknown root HFS0 header partition count! (%u)", hfs0_partition_cnt);
                            uiDrawString(strbuf, 0, breaks * font_height, 255, 0, 0);
                        }
                    } else {
                        uiDrawString("Error: unable to get root HFS0 header data!", 0, breaks * font_height, 255, 0, 0);
                    }
                } else {
                    uiDrawString("Game card is not inserted!", 0, breaks * font_height, 255, 0, 0);
                }
                
                res = resultShowMainMenu;
            }
            
            breaks += 2;
        }
        
        if (gameCardInserted && hfs0_header != NULL && (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT || hfs0_partition_cnt == GAMECARD_TYPE2_PARTITION_CNT) && gameCardAppCount > 0 && gameCardTitleID != NULL)
        {
            switch(uiState)
            {
                case stateMainMenu:
                    menu = mainMenuItems;
                    menuItemsCount = sizeof(mainMenuItems) / sizeof(mainMenuItems[0]);
                    break;
                case stateXciDumpMenu:
                    menu = xciDumpMenuItems;
                    menuItemsCount = sizeof(xciDumpMenuItems) / sizeof(xciDumpMenuItems[0]);
                    
                    uiDrawString(mainMenuItems[0], 0, breaks * font_height, 115, 115, 255);
                    
                    break;
                case stateNspDumpMenu:
                    menu = nspDumpMenuItems;
                    menuItemsCount = sizeof(nspDumpMenuItems) / sizeof(nspDumpMenuItems[0]);
                    
                    uiDrawString(mainMenuItems[1], 0, breaks * font_height, 115, 115, 255);
                    
                    break;
                case stateRawPartitionDumpMenu:
                case statePartitionDataDumpMenu:
                    menu = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? partitionDumpType1MenuItems : partitionDumpType2MenuItems);
                    menuItemsCount = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? (sizeof(partitionDumpType1MenuItems) / sizeof(partitionDumpType1MenuItems[0])) : (sizeof(partitionDumpType2MenuItems) / sizeof(partitionDumpType2MenuItems[0])));
                    
                    uiDrawString((uiState == stateRawPartitionDumpMenu ? mainMenuItems[2] : mainMenuItems[3]), 0, breaks * font_height, 115, 115, 255);
                    
                    break;
                case stateViewGameCardFsMenu:
                    menu = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? viewGameCardFsType1MenuItems : viewGameCardFsType2MenuItems);
                    menuItemsCount = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? (sizeof(viewGameCardFsType1MenuItems) / sizeof(viewGameCardFsType1MenuItems[0])) : (sizeof(viewGameCardFsType2MenuItems) / sizeof(viewGameCardFsType2MenuItems[0])));
                    
                    uiDrawString(mainMenuItems[4], 0, breaks * font_height, 115, 115, 255);
                    
                    break;
                case stateViewGameCardFsBrowser:
                    menu = (const char**)filenames;
                    menuItemsCount = filenamesCount;
                    
                    uiDrawString((hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? viewGameCardFsType1MenuItems[selectedPartitionIndex] : viewGameCardFsType2MenuItems[selectedPartitionIndex]), 0, breaks * font_height, 115, 115, 255);
                    breaks += 2;
                    
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "File count: %d | Current file: %d", menuItemsCount, cursor + 1);
                    uiDrawString(strbuf, 0, breaks * font_height, 255, 255, 255);
                    breaks++;
                    
                    break;
                default:
                    break;
            }
            
            if (menu && menuItemsCount)
            {
                if (uiState != stateMainMenu) breaks += 2;
                
                j = 0;
                
                for(i = scroll; i < menuItemsCount; i++, j++)
                {
                    if (j >= maxListElements) break;
                    
                    // Avoid printing the "Bundled application to dump" option in the NSP dump menu if we're not dealing with a multigame cart
                    if (uiState != stateNspDumpMenu || (uiState == stateNspDumpMenu && (j < 3 || (j == 3 && gameCardAppCount > 1))))
                    {
                        if ((j + scroll) == cursor)
                        {
                            highlight = true;
                            uiFill(0, (breaks * font_height) + (j * (font_height + 12)), FB_WIDTH / 2, font_height + 12, HIGHLIGHT_BG_COLOR_R, HIGHLIGHT_BG_COLOR_G, HIGHLIGHT_BG_COLOR_B);
                            uiDrawString(menu[i], 0, (breaks * font_height) + (j * (font_height + 12)) + 6, HIGHLIGHT_FONT_COLOR_R, HIGHLIGHT_FONT_COLOR_G, HIGHLIGHT_FONT_COLOR_B);
                            highlight = false;
                        } else {
                            uiDrawString(menu[i], 0, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 255, 255);
                        }
                        
                        // Print XCI dump menu settings values
                        if (uiState == stateXciDumpMenu && j > 0)
                        {
                            if ((j + scroll) == cursor) highlight = true;
                            
                            switch(j)
                            {
                                case 1: // Split output dump (FAT32 support)
                                    if (isFat32)
                                    {
                                        uiDrawString("Yes", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 0, 255, 0);
                                    } else {
                                        uiDrawString("No", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 0, 0);
                                    }
                                    break;
                                case 2: // Dump certificate
                                    if (dumpCert)
                                    {
                                        uiDrawString("Yes", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 0, 255, 0);
                                    } else {
                                        uiDrawString("No", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 0, 0);
                                    }
                                    break;
                                case 3: // Trim output dump
                                    if (trimDump)
                                    {
                                        uiDrawString("Yes", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 0, 255, 0);
                                    } else {
                                        uiDrawString("No", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 0, 0);
                                    }
                                    break;
                                case 4: // CRC32 checksum calculation + dump verification
                                    if (calcCrc)
                                    {
                                        uiDrawString("Yes", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 0, 255, 0);
                                    } else {
                                        uiDrawString("No", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 0, 0);
                                    }
                                    break;
                                default:
                                    break;
                            }
                            
                            if ((j + scroll) == cursor) highlight = false;
                        }
                        
                        // Print NSP dump menu settings values
                        if (uiState == stateNspDumpMenu && j > 0)
                        {
                            if ((j + scroll) == cursor) highlight = true;
                            
                            switch(j)
                            {
                                case 1: // Split output dump (FAT32 support)
                                    if (isFat32)
                                    {
                                        uiDrawString("Yes", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 0, 255, 0);
                                    } else {
                                        uiDrawString("No", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 0, 0);
                                    }
                                    break;
                                case 2: // CRC32 checksum calculation
                                    if (calcCrc)
                                    {
                                        uiDrawString("Yes", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 0, 255, 0);
                                    } else {
                                        uiDrawString("No", OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 0, 0);
                                    }
                                    break;
                                case 3: // Bundled application to dump
                                    uiDrawString(gameCardName[selectedAppIndex], OPTIONS_X_POS, (breaks * font_height) + (j * (font_height + 12)) + 6, 255, 255, 255);
                                    break;
                                default:
                                    break;
                            }
                            
                            if ((j + scroll) == cursor) highlight = false;
                        }
                    }
                }
            }
        }
        
        uiUpdateStatusMsg();
        uiRefreshDisplay();
        
        hidScanInput();
        keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        keysHeld = hidKeysHeld(CONTROLLER_P1_AUTO);
        
        // Exit
        if (keysDown & KEY_PLUS) res = resultExit;
        
        // Process key inputs only if the UI state hasn't been changed
        if (res == resultNone)
        {
            int scrollAmount = 0;
            
            if (uiState == stateXciDumpMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0) res = resultDumpXci;
                
                // Back
                if (keysDown & KEY_B) res = resultShowMainMenu;
                
                // Change option to false
                if (keysDown & KEY_LEFT)
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
                if (keysDown & KEY_RIGHT)
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
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysHeld & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP)) scrollAmount = -1;
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysHeld & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN)) scrollAmount = 1;
            } else
            if (uiState == stateNspDumpMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0) res = resultDumpNsp;
                
                // Back
                if (keysDown & KEY_B) res = resultShowMainMenu;
                
                // Change option to false
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 1: // Split output dump (FAT32 support)
                            isFat32 = false;
                            break;
                        case 2: // CRC32 checksum calculation
                            calcCrc = false;
                            break;
                        case 3: // Bundled application to dump
                            if (selectedAppIndex > 0)
                            {
                                selectedAppIndex--;
                            } else {
                                selectedAppIndex = 0;
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Change option to true
                if (keysDown & KEY_RIGHT)
                {
                    switch(cursor)
                    {
                        case 1: // Split output dump (FAT32 support)
                            isFat32 = true;
                            break;
                        case 2: // CRC32 checksum calculation
                            calcCrc = true;
                            break;
                        case 3: // Bundled application to dump
                            if (gameCardAppCount > 1)
                            {
                                selectedAppIndex++;
                                if (selectedAppIndex >= gameCardAppCount) selectedAppIndex = (gameCardAppCount - 1);
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysHeld & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP)) scrollAmount = -1;
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysHeld & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN)) scrollAmount = 1;
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
                                selectedAppIndex = 0;
                                res = resultShowNspDumpMenu;
                                break;
                            case 2:
                                res = resultShowRawPartitionDumpMenu;
                                break;
                            case 3:
                                res = resultShowPartitionDataDumpMenu;
                                break;
                            case 4:
                                res = resultShowViewGameCardFsMenu;
                                break;
                            case 5:
                                res = resultDumpGameCardCertificate;
                                break;
                            case 6:
                                res = resultUpdateNSWDBXml;
                                break;
                            case 7:
                                res = resultUpdateApplication;
                                break;
                            default:
                                break;
                        }
                    } else
                    if (uiState == stateRawPartitionDumpMenu)
                    {
                        // Save selected partition index
                        selectedPartitionIndex = (u32)cursor;
                        res = resultDumpRawPartition;
                    } else
                    if (uiState == statePartitionDataDumpMenu)
                    {
                        // Save selected partition index
                        selectedPartitionIndex = (u32)cursor;
                        res = resultDumpPartitionData;
                    } else
                    if (uiState == stateViewGameCardFsMenu)
                    {
                        // Save selected partition index
                        selectedPartitionIndex = (u32)cursor;
                        res = resultShowViewGameCardFsGetList;
                    } else
                    if (uiState == stateViewGameCardFsBrowser)
                    {
                        // Save selected file index
                        selectedFileIndex = (u32)cursor;
                        res = resultViewGameCardFsBrowserCopyFile;
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
                        free(partitionHfs0Header);
                        partitionHfs0Header = NULL;
                        partitionHfs0HeaderOffset = 0;
                        partitionHfs0HeaderSize = 0;
                        partitionHfs0FileCount = 0;
                        partitionHfs0StrTableSize = 0;
                        
                        res = resultShowViewGameCardFsMenu;
                    }
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysHeld & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP)) scrollAmount = -1;
                if ((keysDown & KEY_DLEFT) || (keysHeld & KEY_LSTICK_LEFT) || (keysHeld & KEY_RSTICK_LEFT)) scrollAmount = -5;
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysHeld & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN)) scrollAmount = 1;
                if ((keysDown & KEY_DRIGHT) || (keysHeld & KEY_LSTICK_RIGHT) || (keysHeld & KEY_RSTICK_RIGHT)) scrollAmount = 5;
            }
            
            // Calculate scroll only if the UI state hasn't been changed
            if (res == resultNone)
            {
                if (scrollAmount > 0)
                {
                    for(i = 0; i < scrollAmount; i++)
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
                    for(i = 0; i < -scrollAmount; i++)
                    {
                        if (cursor > 0)
                        {
                            cursor--;
                            if ((cursor - scroll) < 0) scroll--;
                        }
                    }
                }
                
                // Avoid placing the cursor on the "Bundled application to dump" option in the NSP dump menu if we're not dealing with multigame carts
                if (uiState == stateNspDumpMenu && cursor == 3 && gameCardAppCount == 1) cursor = 2;
            }
        }
    } else
    if (uiState == stateDumpXci)
    {
        uiDrawString(mainMenuItems[0], 0, breaks * font_height, 115, 115, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%s%s", xciDumpMenuItems[1], (isFat32 ? "Yes" : "No"));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%s%s", xciDumpMenuItems[2], (dumpCert ? "Yes" : "No"));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%s%s", xciDumpMenuItems[3], (trimDump ? "Yes" : "No"));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%s%s", xciDumpMenuItems[4], (calcCrc ? "Yes" : "No"));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpCartridgeImage(&fsOperatorInstance, isFat32, dumpCert, trimDump, calcCrc);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowXciDumpMenu;
    } else
    if (uiState == stateDumpNsp)
    {
        uiDrawString(mainMenuItems[1], 0, breaks * font_height, 115, 115, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%s%s", nspDumpMenuItems[1], (isFat32 ? "Yes" : "No"));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks++;
        
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%s%s", nspDumpMenuItems[2], (calcCrc ? "Yes" : "No"));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        
        if (gameCardAppCount > 1)
        {
            breaks++;
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "%s%s", nspDumpMenuItems[3], gameCardName[selectedAppIndex]);
            uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        }
        
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpApplicationNSP(&fsOperatorInstance, isFat32, calcCrc, selectedAppIndex);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowNspDumpMenu;
    } else
    if (uiState == stateDumpRawPartition)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Raw %s", (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? partitionDumpType1MenuItems[selectedPartitionIndex] : partitionDumpType2MenuItems[selectedPartitionIndex]));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpRawPartition(&fsOperatorInstance, selectedPartitionIndex, true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowRawPartitionDumpMenu;
    } else
    if (uiState == stateDumpPartitionData)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Data %s", (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? partitionDumpType1MenuItems[selectedPartitionIndex] : partitionDumpType2MenuItems[selectedPartitionIndex]));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpPartitionData(&fsOperatorInstance, selectedPartitionIndex);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowPartitionDataDumpMenu;
    } else
    if (uiState == stateViewGameCardFsGetList)
    {
        uiDrawString((hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? viewGameCardFsType1MenuItems[selectedPartitionIndex] : viewGameCardFsType2MenuItems[selectedPartitionIndex]), 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        uiPleaseWait(0);
        breaks += 2;
        
        if (getHfs0FileList(selectedPartitionIndex))
        {
            cursor = 0;
            scroll = 0;
            res = resultShowViewGameCardFsBrowser;
        } else {
            breaks += 2;
            waitForButtonPress();
            res = resultShowViewGameCardFsMenu;
        }
    } else
    if (uiState == stateViewGameCardFsBrowserCopyFile)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Manual File Dump: %s (Partition %u [%s])", filenames[selectedFileIndex], selectedPartitionIndex, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, selectedPartitionIndex));
        uiDrawString(strbuf, 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpFileFromPartition(&fsOperatorInstance, selectedPartitionIndex, selectedFileIndex, filenames[selectedFileIndex]);
        
        breaks += 2;
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowViewGameCardFsBrowser;
    } else
    if (uiState == stateDumpGameCardCertificate)
    {
        uiDrawString(mainMenuItems[5], 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        dumpGameCertificate(&fsOperatorInstance);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowMainMenu;
    } else
    if (uiState == stateUpdateNSWDBXml)
    {
        uiDrawString(mainMenuItems[6], 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        updateNSWDBXml();
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowMainMenu;
    } else
    if (uiState == stateUpdateApplication)
    {
        uiDrawString(mainMenuItems[7], 0, breaks * font_height, 115, 115, 255);
        breaks += 2;
        
        updateApplication();
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowMainMenu;
    }
    
    return res;
}
