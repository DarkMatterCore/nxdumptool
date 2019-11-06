#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <math.h>

#include <switch.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <turbojpeg.h>

#include "dumper.h"
#include "fs_ext.h"
#include "ui.h"
#include "util.h"
#include "keys.h"

/* Extern variables */

extern dumpOptions dumpCfg;

extern bool keysFileAvailable;

extern AppletType programAppletType;

extern bool runningSxOs;

extern bool gameCardInserted;

extern char gameCardSizeStr[32], trimmedCardSizeStr[32];

extern u8 *hfs0_header;
extern u64 hfs0_offset, hfs0_size;
extern u32 hfs0_partition_cnt;

extern u8 *partitionHfs0Header;
extern u64 partitionHfs0HeaderOffset, partitionHfs0HeaderSize;
extern u32 partitionHfs0FileCount, partitionHfs0StrTableSize;

extern u32 titleAppCount;
extern u64 *titleAppTitleID;
extern u32 *titleAppVersion;
extern FsStorageId *titleAppStorageId;

extern u32 titlePatchCount;
extern u64 *titlePatchTitleID;
extern u32 *titlePatchVersion;
extern FsStorageId *titlePatchStorageId;

extern u32 titleAddOnCount;
extern u64 *titleAddOnTitleID;
extern u32 *titleAddOnVersion;
extern FsStorageId *titleAddOnStorageId;

extern char **titleName;
extern char **titleAuthor;
extern char **titleAppVersionStr;
extern u8 **titleIcon;

extern u32 sdCardTitleAppCount;
extern u32 sdCardTitlePatchCount;
extern u32 sdCardTitleAddOnCount;

extern u32 nandUserTitleAppCount;
extern u32 nandUserTitlePatchCount;
extern u32 nandUserTitleAddOnCount;

extern char gameCardUpdateVersionStr[128];

extern char *filenameBuffer;
extern char *filenames[FILENAME_MAX_CNT];
extern int filenamesCount;

extern char curRomFsPath[NAME_BUF_LEN];
extern romfs_browser_entry *romFsBrowserEntries;

extern u8 *dumpBuf;
extern u8 *ncaCtrBuf;

extern orphan_patch_addon_entry *orphanEntries;

extern char strbuf[NAME_BUF_LEN];

/* Statically allocated variables */

static PlFontData sharedFonts[PlSharedFontType_Total];
static FT_Library library;
static FT_Face sharedFontsFaces[PlSharedFontType_Total];
static Framebuffer fb;

static u32 *framebuf = NULL;
static u32 framebuf_width = 0;

static const u8 bgColors[3] = { BG_COLOR_RGB };
static const u8 hlBgColors[3] = { HIGHLIGHT_BG_COLOR_RGB };

int cursor = 0;
int scroll = 0;
int breaks = 0;
int font_height = 0;

int titleListCursor = 0;
int titleListScroll = 0;

int orphanListCursor = 0;
int orphanListScroll = 0;

curMenuType menuType;
static bool orphanMode = false;

static char titleSelectorStr[NAME_BUF_LEN] = {'\0'};
static char exeFsAndRomFsSelectorStr[NAME_BUF_LEN] = {'\0'};
static char dumpedContentInfoStr[NAME_BUF_LEN] = {'\0'};

static u32 selectedAppInfoIndex = 0;
static u32 selectedAppIndex;
static u32 selectedPatchIndex;
static u32 selectedAddOnIndex;
static u32 selectedPartitionIndex;
static u32 selectedFileIndex;

static nspDumpType selectedNspDumpType;

static bool exeFsUpdateFlag = false;
static selectedRomFsType curRomFsType = ROMFS_TYPE_APP;

static selectedTicketType curTikType = TICKET_TYPE_APP;

static bool updatePerformed = false;

bool highlight = false;

static char statusMessage[2048] = {'\0'};
static int statusMessageFadeout = 0;

u64 freeSpace = 0;
static char freeSpaceStr[64] = {'\0'};

static UIState uiState;

static const char *dirNormalIconPath = "romfs:/browser/dir_normal.jpg";
static u8 *dirNormalIconBuf = NULL;

static const char *dirHighlightIconPath = "romfs:/browser/dir_highlight.jpg";
static u8 *dirHighlightIconBuf = NULL;

static const char *fileNormalIconPath = "romfs:/browser/file_normal.jpg";
static u8 *fileNormalIconBuf = NULL;

static const char *fileHighlightIconPath = "romfs:/browser/file_highlight.jpg";
static u8 *fileHighlightIconBuf = NULL;

static const char *enabledNormalIconPath = "romfs:/browser/enabled_normal.jpg";
u8 *enabledNormalIconBuf = NULL;

static const char *enabledHighlightIconPath = "romfs:/browser/enabled_highlight.jpg";
u8 *enabledHighlightIconBuf = NULL;

static const char *disabledNormalIconPath = "romfs:/browser/disabled_normal.jpg";
u8 *disabledNormalIconBuf = NULL;

static const char *disabledHighlightIconPath = "romfs:/browser/disabled_highlight.jpg";
u8 *disabledHighlightIconBuf = NULL;

static const char *appHeadline = "NXDumpTool v" APP_VERSION ".\nOriginal codebase by MCMrARM.\nUpdated and maintained by DarkMatterCore.\n\n";
static const char *appControlsCommon = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsGameCardMultiApp = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_L " / " NINTENDO_FONT_R " / " NINTENDO_FONT_ZL " / " NINTENDO_FONT_ZR " ] Show info from another base application | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsNoContent = "[ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsSdCardEmmcFull = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_X " ] Batch mode | [ " NINTENDO_FONT_Y " ] Dump installed content with missing base application | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsSdCardEmmcNoApp = "[ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_X " ] Batch mode | [ " NINTENDO_FONT_Y " ] Dump installed content with missing base application | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsRomFs = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_Y " ] Dump current directory | [ " NINTENDO_FONT_PLUS " ] Exit";

static const char *mainMenuItems[] = { "Dump gamecard content", "Dump SD card / eMMC (NANDUSER) content", "Update options" };
static const char *gameCardMenuItems[] = { "Cartridge Image (XCI) dump", "Nintendo Submission Package (NSP) dump", "HFS0 options", "ExeFS options", "RomFS options", "Dump gamecard certificate" };
static const char *xciDumpMenuItems[] = { "Start XCI dump process", "Split output dump (FAT32 support): ", "Create directory with archive bit set: ", "Keep certificate: ", "Trim output dump: ", "CRC32 checksum calculation + dump verification: " };
static const char *nspDumpGameCardMenuItems[] = { "Dump base application NSP", "Dump bundled update NSP", "Dump bundled DLC NSP" };
static const char *nspDumpSdCardEmmcMenuItems[] = { "Dump base application NSP", "Dump installed update NSP", "Dump installed DLC NSP" };
static const char *nspAppDumpMenuItems[] = { "Start NSP dump process", "Split output dump (FAT32 support): ", "CRC32 checksum calculation: ", "Remove console specific data: ", "Generate ticket-less dump: ", "Change NPDM RSA key/sig in Program NCA: ", "Base application to dump: " };
static const char *nspPatchDumpMenuItems[] = { "Start NSP dump process", "Split output dump (FAT32 support): ", "CRC32 checksum calculation: ", "Remove console specific data: ", "Generate ticket-less dump: ", "Change NPDM RSA key/sig in Program NCA: ", "Update to dump: " };
static const char *nspAddOnDumpMenuItems[] = { "Start NSP dump process", "Split output dump (FAT32 support): ", "CRC32 checksum calculation: ", "Remove console specific data: ", "Generate ticket-less dump: ", "DLC to dump: " };
static const char *hfs0MenuItems[] = { "Raw HFS0 partition dump", "HFS0 partition data dump", "Browse HFS0 partitions" };
static const char *hfs0PartitionDumpType1MenuItems[] = { "Dump HFS0 partition 0 (Update)", "Dump HFS0 partition 1 (Normal)", "Dump HFS0 partition 2 (Secure)" };
static const char *hfs0PartitionDumpType2MenuItems[] = { "Dump HFS0 partition 0 (Update)", "Dump HFS0 partition 1 (Logo)", "Dump HFS0 partition 2 (Normal)", "Dump HFS0 partition 3 (Secure)" };
static const char *hfs0BrowserType1MenuItems[] = { "Browse HFS0 partition 0 (Update)", "Browse HFS0 partition 1 (Normal)", "Browse HFS0 partition 2 (Secure)" };
static const char *hfs0BrowserType2MenuItems[] = { "Browse HFS0 partition 0 (Update)", "Browse HFS0 partition 1 (Logo)", "Browse HFS0 partition 2 (Normal)", "Browse HFS0 partition 3 (Secure)" };
static const char *exeFsMenuItems[] = { "ExeFS section data dump", "Browse ExeFS section", "Use update: " };
static const char *exeFsSectionDumpMenuItems[] = { "Start ExeFS data dump process", "Base application to dump: ", "Use update: " };
static const char *exeFsSectionBrowserMenuItems[] = { "Browse ExeFS section", "Base application to browse: ", "Use update: " };
static const char *romFsMenuItems[] = { "RomFS section data dump", "Browse RomFS section", "Use update/DLC: " };
static const char *romFsSectionDumpMenuItems[] = { "Start RomFS data dump process", "Base application to dump: ", "Use update/DLC: " };
static const char *romFsSectionBrowserMenuItems[] = { "Browse RomFS section", "Base application to browse: ", "Use update/DLC: " };
static const char *sdCardEmmcMenuItems[] = { "Nintendo Submission Package (NSP) dump", "ExeFS options", "RomFS options", "Ticket options" };
static const char *batchModeMenuItems[] = { "Start batch dump process", "Dump base applications: ", "Dump updates: ", "Dump DLCs: ", "Split output dumps (FAT32 support): ", "Remove console specific data: ", "Generate ticket-less dumps: ", "Change NPDM RSA key/sig in Program NCA: ", "Skip already dumped titles: ", "Remember dumped titles: ", "Source storage: " };
static const char *ticketMenuItems[] = { "Start ticket dump", "Remove console specific data: ", "Use ticket from title: " };
static const char *updateMenuItems[] = { "Update NSWDB.COM XML database", "Update application" };

void uiFill(int x, int y, int width, int height, u8 r, u8 g, u8 b)
{
    /* Perform validity checks */
	if (width <= 0 || height <= 0 || (x + width) < 0 || (y + height) < 0 || x >= FB_WIDTH || y >= FB_HEIGHT) return;
    
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

void uiDrawIcon(const u8 *icon, int width, int height, int x, int y)
{
    /* Perform validity checks */
    if (!icon || !width || !height || (x + width) < 0 || (y + height) < 0 || x >= FB_WIDTH || y >= FB_HEIGHT) return;
    
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
    u32 pos = 0;
    
    for (ly = 0; ly < height; ly++)
    {
        for (lx = 0; lx < width; lx++)
        {
            framex = (x + lx);
            framey = (y + ly);
            
            pos = (((ly * width) + lx) * 3);
            
            framebuf[(framey * framebuf_width) + framex] = RGBA8_MAXALPHA(icon[pos], icon[pos + 1], icon[pos + 2]);
        }
    }
}

bool uiLoadJpgFromMem(u8 *rawJpg, size_t rawJpgSize, int expectedWidth, int expectedHeight, int desiredWidth, int desiredHeight, u8 **outBuf)
{
    if (!rawJpg || !rawJpgSize || !expectedWidth || !expectedHeight || !desiredWidth || !desiredHeight || !outBuf)
    {
        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: invalid parameters to process JPG image buffer.");
        return false;
    }
    
    int ret, w, h, samp;
    tjhandle _jpegDecompressor = NULL;
    bool success = false;
    
    bool foundScalingFactor = false;
    int i, numScalingFactors = 0, pitch;
    tjscalingfactor *scalingFactors = NULL;
    
    u8 *jpgScaledBuf = NULL;
    
    _jpegDecompressor = tjInitDecompress();
    if (_jpegDecompressor)
    {
        ret = tjDecompressHeader2(_jpegDecompressor, rawJpg, rawJpgSize, &w, &h, &samp);
        if (ret != -1)
        {
            if (w == expectedWidth && h == expectedHeight)
            {
                scalingFactors = tjGetScalingFactors(&numScalingFactors);
                if (scalingFactors)
                {
                    for(i = 0; i < numScalingFactors; i++)
                    {
                        if (TJSCALED(expectedWidth, scalingFactors[i]) == desiredWidth && TJSCALED(expectedHeight, scalingFactors[i]) == desiredHeight)
                        {
                            foundScalingFactor = true;
                            break;
                        }
                    }
                    
                    if (foundScalingFactor)
                    {
                        pitch = TJPAD(desiredWidth * tjPixelSize[TJPF_RGB]);
                        
                        jpgScaledBuf = malloc(pitch * desiredHeight);
                        if (jpgScaledBuf)
                        {
                            ret = tjDecompress2(_jpegDecompressor, rawJpg, rawJpgSize, jpgScaledBuf, desiredWidth, 0, desiredHeight, TJPF_RGB, TJFLAG_ACCURATEDCT);
                            if (ret != -1)
                            {
                                *outBuf = jpgScaledBuf;
                                success = true;
                            } else {
                                free(jpgScaledBuf);
                                snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: tjDecompress2 failed (%d).", ret);
                            }
                        } else {
                            snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: unable to allocated memory for the scaled RGB image output.");
                        }
                    } else {
                        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: unable to find a valid scaling factor.");
                    }
                } else {
                    snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: error retrieving scaling factors.");
                }
            } else {
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: invalid image width/height.");
            }
        } else {
            snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: tjDecompressHeader2 failed (%d).", ret);
        }
        
        tjDestroy(_jpegDecompressor);
    } else {
        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromMem: tjInitDecompress failed.");
    }
    
    return success;
}

bool uiLoadJpgFromFile(const char *filename, int expectedWidth, int expectedHeight, int desiredWidth, int desiredHeight, u8 **outBuf)
{
    if (!filename || !desiredWidth || !desiredHeight || !outBuf)
    {
        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromFile: invalid parameters to process JPG image file.\n");
        return false;
    }
    
    u8 *buf = NULL;
    FILE *fp = NULL;
    size_t filesize = 0, read = 0;
    
    fp = fopen(filename, "rb");
    if (!fp)
    {
        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromFile: failed to open file \"%s\".\n", filename);
        return false;
    }
    
    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    rewind(fp);
    
    if (!filesize)
    {
        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromFile: file \"%s\" is empty.\n", filename);
        fclose(fp);
        return false;
    }
    
    buf = malloc(filesize);
    if (!buf)
    {
        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromFile: error allocating memory for image \"%s\".\n", filename);
        fclose(fp);
        return false;
    }
    
    read = fread(buf, 1, filesize, fp);
    
    fclose(fp);
    
    if (read != filesize)
    {
        snprintf(strbuf, MAX_ELEMENTS(strbuf), "uiLoadJpgFromFile: error reading image \"%s\".\n", filename);
        free(buf);
        return false;
    }
    
    bool ret = uiLoadJpgFromMem(buf, filesize, expectedWidth, expectedHeight, desiredWidth, desiredHeight, outBuf);
    
    free(buf);
    
    if (!ret) strcat(strbuf, "\n");
    
    return ret;
}

void uiDrawChar(FT_Bitmap *bitmap, int x, int y, u8 r, u8 g, u8 b)
{
    if (framebuf == NULL) return;
    
    u32 framex, framey, framebuf_offset;
    u32 tmpx, tmpy;
    u8 *imageptr = bitmap->buffer;
    
    u8 src_val;
    float opacity;
    
    u8 fontR, fontG, fontB;
    
    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY) return;
    
    for(tmpy = 0; tmpy < bitmap->rows; tmpy++)
    {
        for (tmpx = 0; tmpx < bitmap->width; tmpx++)
        {
            framex = (x + tmpx);
            framey = (y + tmpy);
            
            if (framex >= FB_WIDTH || framey >= FB_HEIGHT) continue;
            
            framebuf_offset = ((framey * framebuf_width) + framex);
            
            src_val = imageptr[tmpx];
            if (!src_val)
            {
                /* Render background color */
                framebuf[framebuf_offset] = (highlight ? RGBA8_MAXALPHA(hlBgColors[0], hlBgColors[1], hlBgColors[2]) : RGBA8_MAXALPHA(bgColors[0], bgColors[1], bgColors[2]));
            } else {
                /* Calculate alpha (opacity) */
                opacity = (src_val / 255.0);
                
                fontR = (r * opacity + (1 - opacity) * (highlight ? hlBgColors[0] : bgColors[0]));
                fontG = (g * opacity + (1 - opacity) * (highlight ? hlBgColors[1] : bgColors[1]));
                fontB = (b * opacity + (1 - opacity) * (highlight ? hlBgColors[2] : bgColors[2]));
                
                framebuf[framebuf_offset] = RGBA8_MAXALPHA(fontR, fontG, fontB);
            }
        }
        
        imageptr += bitmap->pitch;
    }
}

void uiDrawString(int x, int y, u8 r, u8 g, u8 b, const char *fmt, ...)
{
	if (!fmt || !*fmt) return;
    
    char string[NAME_BUF_LEN] = {'\0'};
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(string, MAX_ELEMENTS(string), fmt, args);
    va_end(args);
    
    u32 tmpx = (x <= 8 ? 8 : (x + 8));
    u32 tmpy = (font_height + (y <= 8 ? 8 : (y + 8)));
    
    FT_Error ret = 0;
    FT_UInt glyph_index = 0;
    
    u32 i, j;
    u32 str_size = strlen(string);
    u32 tmpchar;
    ssize_t unitcount = 0;
    
    if (framebuf == NULL)
    {
        /* Begin new frame */
        u32 stride;
        framebuf = (u32*)framebufferBegin(&fb, &stride);
        framebuf_width = (stride / sizeof(u32));
    }
    
    for(i = 0; i < str_size;)
    {
        unitcount = decode_utf8(&tmpchar, (const u8*)&string[i]);
        if (unitcount <= 0) break;
        i += unitcount;
        
        if (tmpchar == '\n')
        {
            tmpx = 8;
            tmpy += LINE_HEIGHT;
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
        
        for(j = 0; j < PlSharedFontType_Total; j++)
        {
            glyph_index = FT_Get_Char_Index(sharedFontsFaces[j], tmpchar);
            if (glyph_index) break;
        }
        
        if (!glyph_index && j == PlSharedFontType_Total) j = 0;
        
        ret = FT_Load_Glyph(sharedFontsFaces[j], glyph_index, FT_LOAD_DEFAULT);
        if (ret == 0) ret = FT_Render_Glyph(sharedFontsFaces[j]->glyph, FT_RENDER_MODE_NORMAL);
        
        if (ret) break;
        
        if ((tmpx + (sharedFontsFaces[j]->glyph->advance.x >> 6)) >= (FB_WIDTH - 8))
        {
            tmpx = 8;
            tmpy += LINE_HEIGHT;
            breaks++;
        }
        
        uiDrawChar(&(sharedFontsFaces[j]->glyph->bitmap), tmpx + sharedFontsFaces[j]->glyph->bitmap_left, tmpy - sharedFontsFaces[j]->glyph->bitmap_top, r, g, b);
        
        tmpx += (sharedFontsFaces[j]->glyph->advance.x >> 6);
        tmpy += (sharedFontsFaces[j]->glyph->advance.y >> 6);
    }
}

u32 uiGetStrWidth(const char *fmt, ...)
{
    if (!fmt || !*fmt) return 0;
    
    char string[NAME_BUF_LEN] = {'\0'};
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(string, MAX_ELEMENTS(string), fmt, args);
    va_end(args);
    
    FT_Error ret = 0;
    FT_UInt glyph_index = 0;
    
    u32 i, j;
    u32 str_size = strlen(string);
    u32 tmpchar;
    ssize_t unitcount = 0;
    u32 width = 0;
    
    for(i = 0; i < str_size;)
    {
        unitcount = decode_utf8(&tmpchar, (const u8*)&string[i]);
        if (unitcount <= 0) break;
        i += unitcount;
        
        if (tmpchar == '\n' || tmpchar == '\r')
        {
            continue;
        } else
        if (tmpchar == '\t')
        {
            width += (font_height * TAB_WIDTH);
            continue;
        }
        
        for(j = 0; j < PlSharedFontType_Total; j++)
        {
            glyph_index = FT_Get_Char_Index(sharedFontsFaces[j], tmpchar);
            if (glyph_index) break;
        }
        
        if (!glyph_index && j == PlSharedFontType_Total) j = 0;
        
        ret = FT_Load_Glyph(sharedFontsFaces[j], glyph_index, FT_LOAD_DEFAULT);
        if (ret == 0) ret = FT_Render_Glyph(sharedFontsFaces[j]->glyph, FT_RENDER_MODE_NORMAL);
        
        if (ret) break;
        
        width += (sharedFontsFaces[j]->glyph->advance.x >> 6);
    }
    
    return width;
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
    statusMessageFadeout = 5000;
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(statusMessage, MAX_ELEMENTS(statusMessage), fmt, args);
    va_end(args);
}

void uiUpdateStatusMsg()
{
	if (!strlen(statusMessage) || !statusMessageFadeout) return;
	
    uiFill(0, FB_HEIGHT - (font_height + STRING_Y_POS(1)), FB_WIDTH, font_height + STRING_Y_POS(1), BG_COLOR_RGB);
    
    if ((statusMessageFadeout - 4) > bgColors[0])
    {
        int fadeout = (statusMessageFadeout > 255 ? 255 : statusMessageFadeout);
        uiDrawString(STRING_X_POS, FB_HEIGHT - (font_height + STRING_Y_POS(1)), fadeout, fadeout, fadeout, statusMessage);
        statusMessageFadeout -= 4;
    } else {
        statusMessageFadeout = 0;
    }
}

void uiPleaseWait(u8 wait)
{
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Please wait...");
    uiRefreshDisplay();
    if (wait) delay(wait);
}

void uiUpdateFreeSpace()
{
    getSdCardFreeSpace(&freeSpace);
    
    char tmp[32] = {'\0'};
    convertSize(freeSpace, tmp, MAX_ELEMENTS(tmp));
    
    snprintf(freeSpaceStr, MAX_ELEMENTS(freeSpaceStr), "Free SD card space: %s.", tmp);
}

void uiClearScreen()
{
    uiFill(0, 0, FB_WIDTH, FB_HEIGHT, BG_COLOR_RGB);
}

void uiPrintHeadline()
{
    breaks = 0;
    uiClearScreen();
    uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_RGB, appHeadline);
}

void uiPrintOption(int x, int y, int endPosition, bool leftArrow, bool rightArrow, int r, int g, int b, const char *fmt, ...)
{
    if (x < 8 || x >= OPTIONS_X_END_POS || y < 8 || y >= (FB_HEIGHT - 8 - font_height) || endPosition < OPTIONS_X_END_POS || endPosition >= (FB_WIDTH - 8) || !fmt || !*fmt) return;
    
    int xpos = x;
    char option[NAME_BUF_LEN] = {'\0'};
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(option, MAX_ELEMENTS(option), fmt, args);
    va_end(args);
    
    u32 optionStrWidth = uiGetStrWidth(option);
    
    if (leftArrow) uiDrawString(xpos, y, FONT_COLOR_RGB, "<");
    
    xpos += uiGetStrWidth("<");
    
    xpos += (((endPosition - xpos) / 2) - (optionStrWidth / 2));
    
    uiDrawString(xpos, y, r, g, b, option);
    
    if (rightArrow)
    {
        xpos = endPosition;
        
        uiDrawString(xpos, y, FONT_COLOR_RGB, ">");
    }
}

void uiTruncateOptionStr(char *str, int x, int y, int endPosition)
{
    if (!str || !strlen(str) || x < 8 || x >= OPTIONS_X_END_POS || y < 8 || y >= (FB_HEIGHT - 8 - font_height) || endPosition < OPTIONS_X_END_POS || endPosition >= (FB_WIDTH - 8)) return;
    
    int xpos = x;
    char *option = str;
    u32 optionStrWidth = uiGetStrWidth(option);
    
    // Check if we're dealing with a long title selector string
    if (optionStrWidth >= (endPosition - xpos - (font_height * 2)))
    {
        while(optionStrWidth >= (endPosition - xpos - (font_height * 2)))
        {
            option++;
            optionStrWidth = uiGetStrWidth(option);
        }
        
        option[0] = option[1] = option[2] = '.';
        
        memmove(str, option, strlen(option));
        
        str[strlen(option)] = '\0';
    }
}

void error_screen(const char *fmt, ...)
{
    consoleInit(NULL);
    
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    
    printf("Press any button to exit.\n");
    
    while(appletMainLoop())
    {
        hidScanInput();
        
        u32 keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
        
        if (keysDown && !((keysDown & KEY_TOUCH) || (keysDown & KEY_LSTICK_LEFT) || (keysDown & KEY_LSTICK_RIGHT) || (keysDown & KEY_LSTICK_UP) || (keysDown & KEY_LSTICK_DOWN) || \
            (keysDown & KEY_RSTICK_LEFT) || (keysDown & KEY_RSTICK_RIGHT) || (keysDown & KEY_RSTICK_UP) || (keysDown & KEY_RSTICK_DOWN))) break;
        
        consoleUpdate(NULL);
    }
    
    consoleExit(NULL);
}

int uiInit()
{
    Result rc = 0;
    FT_Error ret = 0;
    
    u32 i;
    int status = 0;
    bool pl_init = false, romfs_init = false, ft_lib_init = false, ft_faces_init[PlSharedFontType_Total];
    
    memset(ft_faces_init, 0, PlSharedFontType_Total);
    
    /* Set initial UI state */
    uiState = stateMainMenu;
    menuType = MENUTYPE_MAIN;
    cursor = 0;
    scroll = 0;
    
    /* Check if we're running under SX OS */
    runningSxOs = checkSxOsServices();
    
    /* Initialize pl service */
    rc = plInitialize();
    if (R_FAILED(rc))
    {
        error_screen("plInitialize() failed (0x%08X).\n", rc);
        goto out;
    }
    
    pl_init = true;
    
    /* Retrieve shared fonts */
    for(i = 0; i < PlSharedFontType_Total; i++)
    {
        rc = plGetSharedFontByType(&sharedFonts[i], i);
        if (R_FAILED(rc)) break;
    }
    
    if (R_FAILED(rc))
    {
        error_screen("plGetSharedFontByType() failed to retrieve shared font #%u (0x%08X).\n", i, rc);
        goto out;
    }
    
    /* Initialize FreeType */
    ret = FT_Init_FreeType(&library);
    if (ret)
    {
        error_screen("FT_Init_FreeType() failed (%d).\n", ret);
        goto out;
    }
    
    ft_lib_init = true;
    
    /* Create memory faces for the shared fonts */
    for(i = 0; i < PlSharedFontType_Total; i++)
    {
        ret = FT_New_Memory_Face(library, sharedFonts[i].address, sharedFonts[i].size, 0, &sharedFontsFaces[i]);
        if (ret) break;
        ft_faces_init[i] = true;
    }
    
    if (ret)
    {
        error_screen("FT_New_Memory_Face() failed to create memory face for shared font #%u (%d).\n", i, ret);
        goto out;
    }
    
    /* Set character size for all shared fonts */
    for(i = 0; i < PlSharedFontType_Total; i++)
    {
        ret = FT_Set_Char_Size(sharedFontsFaces[i], 0, CHAR_PT_SIZE * 64, SCREEN_DPI_CNT, SCREEN_DPI_CNT);
        if (ret) break;
    }
    
    if (ret)
    {
        error_screen("FT_Set_Char_Size() failed to set character size for shared font #%u (%d).\n", i, ret);
        goto out;
    }
    
    /* Store font height */
    font_height = (sharedFontsFaces[0]->size->metrics.height / 64);
    
    /* Prepare additional data needed by the UI functions */
    
    /* Allocate memory for the filename buffer */
    filenameBuffer = calloc(FILENAME_BUFFER_SIZE, sizeof(char));
    if (!filenameBuffer)
    {
        error_screen("Failed to allocate memory for the filename buffer.\n");
        goto out;
    }
    
    /* Allocate memory for the dump buffer */
    dumpBuf = calloc(DUMP_BUFFER_SIZE, sizeof(u8));
    if (!dumpBuf)
    {
        error_screen("Failed to allocate memory for the dump buffer.\n");
        goto out;
    }
    
    /* Allocate memory for the NCA AES-CTR operation buffer */
    ncaCtrBuf = calloc(NCA_CTR_BUFFER_SIZE, sizeof(u8));
    if (!ncaCtrBuf)
    {
        error_screen("Failed to allocate memory for the NCA AES-CTR operation buffer.\n");
        goto out;
    }
    
    /* Mount Application's RomFS */
    rc = romfsInit();
    if (R_FAILED(rc))
    {
        error_screen("romfsInit() failed (0x%08X).\n", rc);
        goto out;
    }
    
    romfs_init = true;
    
    if (!uiLoadJpgFromFile(dirNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &dirNormalIconBuf))
    {
        strcat(strbuf, "Failed to load directory icon (normal).\n");
        error_screen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(dirHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &dirHighlightIconBuf))
    {
        strcat(strbuf, "Failed to load directory icon (highlighted).\n");
        error_screen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(fileNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &fileNormalIconBuf))
    {
        strcat(strbuf, "Failed to load file icon (normal).\n");
        error_screen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(fileHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &fileHighlightIconBuf))
    {
        strcat(strbuf, "Failed to load file icon (highlighted).\n");
        error_screen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(enabledNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &enabledNormalIconBuf))
    {
        strcat(strbuf, "Failed to load enabled icon (normal).\n");
        error_screen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(enabledHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &enabledHighlightIconBuf))
    {
        strcat(strbuf, "Failed to load enabled icon (highlighted).\n");
        error_screen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(disabledNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &disabledNormalIconBuf))
    {
        strcat(strbuf, "Failed to load disabled icon (normal).\n");
        error_screen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(disabledHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &disabledHighlightIconBuf))
    {
        strcat(strbuf, "Failed to load disabled icon (highlighted).\n");
        error_screen(strbuf);
        goto out;
    }
    
    /* Unmount Application's RomFS */
    romfsExit();
    romfs_init = false;
    
    /* Create framebuffer */
    framebufferCreate(&fb, nwindowGetDefault(), FB_WIDTH, FB_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);
    
    /* Disable screen dimming and auto sleep */
    appletSetMediaPlaybackState(true);
    
    /* Get applet type */
    programAppletType = appletGetAppletType();
    
    /* Block HOME menu button presses if we're running as a regular application or a system application */
    if (programAppletType == AppletType_Application || programAppletType == AppletType_SystemApplication) appletBeginBlockingHomeButton(0);
    
    /* Clear screen */
    uiClearScreen();
    
    /* Update free space */
    uiUpdateFreeSpace();
    
    /* Set output status */
    status = 1;
    
out:
    if (!status)
    {
        if (disabledHighlightIconBuf) free(disabledHighlightIconBuf);
        if (disabledNormalIconBuf) free(disabledNormalIconBuf);
        if (enabledHighlightIconBuf) free(enabledHighlightIconBuf);
        if (enabledNormalIconBuf) free(enabledNormalIconBuf);
        if (fileHighlightIconBuf) free(fileHighlightIconBuf);
        if (fileNormalIconBuf) free(fileNormalIconBuf);
        if (dirHighlightIconBuf) free(dirHighlightIconBuf);
        if (dirNormalIconBuf) free(dirNormalIconBuf);
        
        if (romfs_init) romfsExit();
        
        if (ncaCtrBuf) free(ncaCtrBuf);
        
        if (dumpBuf) free(dumpBuf);
        
        if (filenameBuffer) free(filenameBuffer);
        
        for(i = 0; i < PlSharedFontType_Total; i++)
        {
            if (ft_faces_init[i]) FT_Done_Face(sharedFontsFaces[i]);
        }
        
        if (ft_lib_init) FT_Done_FreeType(library);
        
        if (pl_init) plExit();
    }
    
    return status;
}

void uiDeinit()
{
    u32 i;
    
    /* Unblock HOME menu button presses if we're running as a regular application or a system application */
    if (programAppletType == AppletType_Application || programAppletType == AppletType_SystemApplication) appletEndBlockingHomeButton();
    
    /* Enable screen dimming and auto sleep */
    appletSetMediaPlaybackState(false);
    
    /* Free framebuffer object */
    framebufferClose(&fb);
    
    /* Free enabled/disabled icons (batch mode summary list) */
    free(disabledHighlightIconBuf);
    free(disabledNormalIconBuf);
    free(enabledHighlightIconBuf);
    free(enabledNormalIconBuf);
    
    /* Free directory/file icons */
    free(fileHighlightIconBuf);
    free(fileNormalIconBuf);
    free(dirHighlightIconBuf);
    free(dirNormalIconBuf);
    
    /* Free NCA AES-CTR operation buffer */
    free(ncaCtrBuf);
    
    /* Free dump buffer */
    free(dumpBuf);
    
    /* Free filename buffer */
    free(filenameBuffer);
    
    /* Free FreeType resources */
    for(i = 0; i < PlSharedFontType_Total; i++) FT_Done_Face(sharedFontsFaces[i]);
    FT_Done_FreeType(library);
    
    /* Deinitialize pl service */
    plExit();
}

void uiSetState(UIState state)
{
    if (uiState == stateSdCardEmmcMenu)
    {
        if (state != stateMainMenu)
        {
            // Store current cursor/scroll values
            titleListCursor = cursor;
            titleListScroll = scroll;
        } else {
            // Reset title list cursor/scroll values
            titleListCursor = 0;
            titleListScroll = 0;
        }
    } else
    if (uiState == stateSdCardEmmcOrphanPatchAddOnMenu)
    {
        if (state != stateSdCardEmmcMenu)
        {
            // Store current cursor/scroll values
            orphanListCursor = cursor;
            orphanListScroll = scroll;
        } else {
            // Reset orphan list cursor/scroll values
            orphanListCursor = 0;
            orphanListScroll = 0;
        }
    }
    
    uiState = state;
    
    if (state == stateSdCardEmmcMenu)
    {
        // Override cursor/scroll values
        cursor = titleListCursor;
        scroll = titleListScroll;
    } else
    if (state == stateSdCardEmmcOrphanPatchAddOnMenu)
    {
        // Override cursor/scroll values
        cursor = orphanListCursor;
        scroll = orphanListScroll;
    } else {
        cursor = 0;
        scroll = 0;
    }
    
    titleSelectorStr[0] = '\0';
    exeFsAndRomFsSelectorStr[0] = '\0';
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
    
    u32 keysDown = 0, keysHeld = 0;
    
    int scrollAmount = 0;
    bool scrollWithKeysDown = false;
    
    u32 patch, addon, xpos, ypos, startYPos;
    
    char versionStr[128] = {'\0'};
    
    int maxElements = (uiState == stateSdCardEmmcMenu ? SDCARD_MAX_ELEMENTS : (uiState == stateSdCardEmmcOrphanPatchAddOnMenu ? ORPHAN_MAX_ELEMENTS : (uiState == stateHfs0Browser ? HFS0_MAX_ELEMENTS : ((uiState == stateExeFsSectionBrowser || uiState == stateRomFsSectionBrowser) ? ROMFS_MAX_ELEMENTS : (uiState == stateSdCardEmmcBatchModeMenu ? BATCH_MAX_ELEMENTS : COMMON_MAX_ELEMENTS)))));
    
    const char *upwardsArrow = UPWARDS_ARROW;
    const char *downwardsArrow = DOWNWARDS_ARROW;
    
    bool forcedXciDump = false;
    
    uiPrintHeadline();
    loadTitleInfo();
    
    if (uiState == stateMainMenu || uiState == stateGameCardMenu || uiState == stateXciDumpMenu || uiState == stateNspDumpMenu || uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu || uiState == stateHfs0Menu || uiState == stateRawHfs0PartitionDumpMenu || uiState == stateHfs0PartitionDataDumpMenu || uiState == stateHfs0BrowserMenu || uiState == stateHfs0Browser || uiState == stateExeFsMenu || uiState == stateExeFsSectionDataDumpMenu || uiState == stateExeFsSectionBrowserMenu || uiState == stateExeFsSectionBrowser || uiState == stateRomFsMenu || uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu || uiState == stateRomFsSectionBrowser || uiState == stateSdCardEmmcMenu || uiState == stateSdCardEmmcTitleMenu || uiState == stateSdCardEmmcOrphanPatchAddOnMenu || uiState == stateSdCardEmmcBatchModeMenu || uiState == stateTicketMenu || uiState == stateUpdateMenu)
    {
        switch(menuType)
        {
            case MENUTYPE_MAIN:
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsCommon);
                break;
            case MENUTYPE_GAMECARD:
                if (uiState == stateRomFsSectionBrowser && strlen(curRomFsPath) > 1)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsRomFs);
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, (!gameCardInserted ? appControlsNoContent : (titleAppCount > 1 ? appControlsGameCardMultiApp : appControlsCommon)));
                }
                break;
            case MENUTYPE_SDCARD_EMMC:
                if (uiState == stateSdCardEmmcBatchModeMenu)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsCommon);
                } else {
                    if (!orphanMode)
                    {
                        if (titleAppCount)
                        {
                            if (uiState == stateSdCardEmmcMenu && ((titlePatchCount && checkOrphanPatchOrAddOn(false)) || (titleAddOnCount && checkOrphanPatchOrAddOn(true))))
                            {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsSdCardEmmcFull);
                                breaks += 2;
                                
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Hint: installed updates/DLCs for gamecard titles can be found in the orphan title list (press the " NINTENDO_FONT_Y " button).");
                            } else
                            if (uiState == stateRomFsSectionBrowser && strlen(curRomFsPath) > 1)
                            {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsRomFs);
                            } else {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsCommon);
                            }
                        } else {
                            if (titlePatchCount || titleAddOnCount)
                            {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsSdCardEmmcNoApp);
                            } else {
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsNoContent);
                            }
                        }
                    } else {
                        if (uiState == stateRomFsSectionBrowser && strlen(curRomFsPath) > 1)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsRomFs);
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, appControlsCommon);
                        }
                    }
                }
                break;
            default:
                break;
        }
        
        breaks += 2;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, freeSpaceStr);
        breaks += 2;
    }
    
    if (menuType == MENUTYPE_GAMECARD)
    {
        if (!gameCardInserted || hfs0_header == NULL || (hfs0_partition_cnt != GAMECARD_TYPE1_PARTITION_CNT && hfs0_partition_cnt != GAMECARD_TYPE2_PARTITION_CNT) || !titleAppCount || titleAppTitleID == NULL)
        {
            if (gameCardInserted)
            {
                if (hfs0_header != NULL)
                {
                    if (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT || hfs0_partition_cnt == GAMECARD_TYPE2_PARTITION_CNT)
                    {
                        forcedXciDump = true;
                        
                        if (titleAppCount > 0)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to retrieve the gamecard Title ID!");
                            
                            if (strlen(gameCardUpdateVersionStr))
                            {
                                breaks++;
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Bundled FW Update: %s", gameCardUpdateVersionStr);
                                breaks++;
                                
                                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "In order to be able to dump data from this cartridge, make sure your console is at least on this FW version.");
                            }
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: gamecard application count is zero!");
                        }
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unknown root HFS0 header partition count! (%u)", hfs0_partition_cnt);
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to get root HFS0 header data!");
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Gamecard is not inserted!");
            }
            
            breaks += 2;
            
            if (forcedXciDump)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Press " NINTENDO_FONT_Y " to dump the cartridge image to \"gamecard.xci\".");
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Are you using \"nogc\" spoofing in your CFW? If so, please consider this option disables all gamecard I/O.");
            }
            
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            res = resultShowGameCardMenu;
            
            hidScanInput();
            keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
            
            // Exit
            if (keysDown & KEY_PLUS) res = resultExit;
            
            // Back
            if (keysDown & KEY_B)
            {
                res = resultShowMainMenu;
                menuType = MENUTYPE_MAIN;
            }
            
            // Forced XCI dump
            if ((keysDown & KEY_Y) && forcedXciDump)
            {
                uiPrintHeadline();
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[0]);
                breaks++;
                
                uiRefreshDisplay();
                
                // Set default options
                xciOptions xciDumpCfg;
                
                xciDumpCfg.isFat32 = true;
                xciDumpCfg.setXciArchiveBit = false;
                xciDumpCfg.keepCert = true;
                xciDumpCfg.trimDump = false;
                xciDumpCfg.calcCrc = false;
                
                dumpCartridgeImage(&xciDumpCfg);
                
                waitForButtonPress();
                
                uiUpdateFreeSpace();
            }
            
            return res;
        }
    } else
    if (menuType == MENUTYPE_SDCARD_EMMC)
    {
        if (!titleAppCount && !orphanMode)
        {
            if (titlePatchCount || titleAddOnCount)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "No base applications available in the SD card / eMMC storage!");
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Use the " NINTENDO_FONT_Y " button to dump installed content with missing base applications!");
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "No titles available in the SD card / eMMC storage!");
            }
            
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            res = resultShowSdCardEmmcMenu;
            
            hidScanInput();
            keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
            
            // Exit
            if (keysDown & KEY_PLUS) res = resultExit;
            
            // Back
            if (keysDown & KEY_B)
            {
                res = resultShowMainMenu;
                menuType = MENUTYPE_MAIN;
            }
            
            // Dump installed content with missing base application
            if ((titlePatchCount || titleAddOnCount) && (keysDown & KEY_Y))
            {
                res = resultShowSdCardEmmcOrphanPatchAddOnMenu;
                orphanMode = true;
            }
            
            return res;
        }
    }
    
    if (uiState == stateMainMenu || uiState == stateGameCardMenu || uiState == stateXciDumpMenu || uiState == stateNspDumpMenu || uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu || uiState == stateHfs0Menu || uiState == stateRawHfs0PartitionDumpMenu || uiState == stateHfs0PartitionDataDumpMenu || uiState == stateHfs0BrowserMenu || uiState == stateHfs0Browser || uiState == stateExeFsMenu || uiState == stateExeFsSectionDataDumpMenu || uiState == stateExeFsSectionBrowserMenu || uiState == stateExeFsSectionBrowser || uiState == stateRomFsMenu || uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu || uiState == stateRomFsSectionBrowser || uiState == stateSdCardEmmcMenu || uiState == stateSdCardEmmcTitleMenu || uiState == stateSdCardEmmcOrphanPatchAddOnMenu || uiState == stateSdCardEmmcBatchModeMenu || uiState == stateTicketMenu || uiState == stateUpdateMenu)
    {
        if ((menuType == MENUTYPE_GAMECARD && uiState != stateHfs0Browser && uiState != stateExeFsSectionBrowser && uiState != stateRomFsSectionBrowser) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && uiState != stateSdCardEmmcMenu && uiState != stateSdCardEmmcBatchModeMenu && uiState != stateExeFsSectionBrowser && uiState != stateRomFsSectionBrowser))
        {
            if (menuType == MENUTYPE_GAMECARD)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Gamecard is inserted!");
                breaks += 2;
                
                /*uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Root HFS0 header offset: 0x%016lX", hfs0_offset);
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Root HFS0 header size: 0x%016lX", hfs0_size);
                breaks++;*/
            }
            
            /* Print application info */
            xpos = STRING_X_POS;
            ypos = STRING_Y_POS(breaks);
            startYPos = ypos;
            
            /* Draw icon */
            if (titleIcon != NULL && titleIcon[selectedAppInfoIndex] != NULL)
            {
                uiDrawIcon(titleIcon[selectedAppInfoIndex], NACP_ICON_DOWNSCALED, NACP_ICON_DOWNSCALED, xpos, ypos + 8);
                xpos += (NACP_ICON_DOWNSCALED + 8);
                ypos += 8;
            }
            
            if (titleName != NULL && titleName[selectedAppInfoIndex] != NULL && strlen(titleName[selectedAppInfoIndex]))
            {
                uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Name: %s", titleName[selectedAppInfoIndex]);
                ypos += LINE_HEIGHT;
            }
            
            if (titleAuthor != NULL && titleAuthor[selectedAppInfoIndex] != NULL && strlen(titleAuthor[selectedAppInfoIndex]))
            {
                uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Publisher: %s", titleAuthor[selectedAppInfoIndex]);
                ypos += LINE_HEIGHT;
            }
            
            uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Title ID: %016lX", titleAppTitleID[selectedAppInfoIndex]);
            
            if (titlePatchCount > 0)
            {
                u32 patchCnt = 0;
                
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s update(s): v", (menuType == MENUTYPE_GAMECARD ? "Bundled" : "Installed"));
                
                for(patch = 0; patch < titlePatchCount; patch++)
                {
                    if ((titleAppTitleID[selectedAppInfoIndex] | APPLICATION_PATCH_BITMASK) == titlePatchTitleID[patch] && ((menuType == MENUTYPE_GAMECARD && titleAppStorageId[selectedAppInfoIndex] == titlePatchStorageId[patch]) || menuType == MENUTYPE_SDCARD_EMMC))
                    {
                        if (patchCnt > 0) strcat(strbuf, ", v");
                        
                        convertTitleVersionToDecimal(titlePatchVersion[patch], versionStr, MAX_ELEMENTS(versionStr));
                        strcat(strbuf, versionStr);
                        
                        patchCnt++;
                    }
                }
                
                if (patchCnt > 0) uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), ypos, FONT_COLOR_SUCCESS_RGB, strbuf);
            }
            
            ypos += LINE_HEIGHT;
            
            if (titleAppVersionStr != NULL && titleAppVersionStr[selectedAppInfoIndex] != NULL && strlen(titleAppVersionStr[selectedAppInfoIndex]))
            {
                uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Version: %s", titleAppVersionStr[selectedAppInfoIndex]);
                if (!titleAddOnCount) ypos += LINE_HEIGHT;
            }
            
            if (titleAddOnCount > 0)
            {
                u32 addOnCnt = 0;
                
                for(addon = 0; addon < titleAddOnCount; addon++)
                {
                    if ((titleAppTitleID[selectedAppInfoIndex] & APPLICATION_ADDON_BITMASK) == (titleAddOnTitleID[addon] & APPLICATION_ADDON_BITMASK)) addOnCnt++;
                }
                
                if (addOnCnt > 0)
                {
                    uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), ypos, FONT_COLOR_SUCCESS_RGB, "%s DLC(s): %u", (menuType == MENUTYPE_GAMECARD ? "Bundled" : "Installed"), addOnCnt);
                    ypos += LINE_HEIGHT;
                }
            }
            
            ypos += 8;
            if (xpos > 8 && (ypos - NACP_ICON_DOWNSCALED) < startYPos) ypos += (NACP_ICON_DOWNSCALED - (ypos - startYPos));
            ypos += LINE_HEIGHT;
            
            breaks += (int)round((double)(ypos - startYPos) / (double)LINE_HEIGHT);
            
            if (menuType == MENUTYPE_GAMECARD)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Size: %s | Used space: %s", gameCardSizeStr, trimmedCardSizeStr);
                
                if (titleAppCount > 1) uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Base application count: %u | Base application currently displayed: %u", titleAppCount, selectedAppInfoIndex + 1);
                
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Partition count: %u (%s)", hfs0_partition_cnt, GAMECARD_TYPE(hfs0_partition_cnt));
                
                if (strlen(gameCardUpdateVersionStr)) uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Bundled FW update: %s", gameCardUpdateVersionStr);
                
                breaks++;
                
                if (titleAppCount > 1 && (titlePatchCount > 0 || titleAddOnCount > 0))
                {
                    if (titlePatchCount > 0) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Total bundled update(s): %u", titlePatchCount);
                    
                    if (titleAddOnCount > 0) uiDrawString((titlePatchCount > 0 ? ((FB_WIDTH / 2) - (FB_WIDTH / 8)) : 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Total bundled DLC(s): %u", titleAddOnCount);
                    
                    breaks++;
                }
            }
            
            if (!strlen(dumpedContentInfoStr))
            {
                // Look for dumped content in the SD card
                char *dumpName = NULL;
                char dumpPath[NAME_BUF_LEN] = {'\0'}, tmpStr[64] = {'\0'};
                bool dumpedXci = false, dumpedXciCertificate = false, dumpedBase = false, dumpedBaseConsoleData = false;
                
                u32 patchCnt = 0, addOnCnt = 0;
                u32 patchCntConsoleData = 0, addOnCntConsoleData = 0;
                
                snprintf(dumpedContentInfoStr, MAX_ELEMENTS(dumpedContentInfoStr), "Content already dumped: ");
                
                if (menuType == MENUTYPE_GAMECARD)
                {
                    char *xciName = generateFullDumpName();
                    if (xciName)
                    {
                        // First check if an unsplitted XCI dump is available
                        snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.xci", XCI_DUMP_PATH, xciName);
                        if (!(dumpedXci = checkIfFileExists(dumpPath)))
                        {
                            // Check if a splitted XCI dump is available
                            snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.xc0", XCI_DUMP_PATH, xciName);
                            dumpedXci = checkIfFileExists(dumpPath);
                        }
                        
                        free(xciName);
                        xciName = NULL;
                        
                        if (dumpedXci) dumpedXciCertificate = checkIfDumpedXciContainsCertificate(dumpPath);
                    }
                }
                
                // Now search for dumped NSPs
                
                // Look for a dumped base application
                dumpName = generateNSPDumpName(DUMP_APP_NSP, selectedAppInfoIndex);
                if (dumpName)
                {
                    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                    
                    free(dumpName);
                    dumpName = NULL;
                    
                    if (checkIfFileExists(dumpPath))
                    {
                        dumpedBase = true;
                        dumpedBaseConsoleData = checkIfDumpedNspContainsConsoleData(dumpPath);
                    }
                }
                
                // Look for dumped updates
                if (titlePatchCount > 0)
                {
                    for(patch = 0; patch < titlePatchCount; patch++)
                    {
                        if ((titleAppTitleID[selectedAppInfoIndex] | APPLICATION_PATCH_BITMASK) == titlePatchTitleID[patch])
                        {
                            dumpName = generateNSPDumpName(DUMP_PATCH_NSP, patch);
                            if (dumpName)
                            {
                                snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                                
                                free(dumpName);
                                dumpName = NULL;
                                
                                if (checkIfFileExists(dumpPath))
                                {
                                    patchCnt++;
                                    if (checkIfDumpedNspContainsConsoleData(dumpPath)) patchCntConsoleData++;
                                }
                            }
                        }
                    }
                }
                
                // Look for dumped DLCs
                if (titleAddOnCount > 0)
                {
                    for(addon = 0; addon < titleAddOnCount; addon++)
                    {
                        if ((titleAppTitleID[selectedAppInfoIndex] & APPLICATION_ADDON_BITMASK) == (titleAddOnTitleID[addon] & APPLICATION_ADDON_BITMASK))
                        {
                            dumpName = generateNSPDumpName(DUMP_ADDON_NSP, addon);
                            if (dumpName)
                            {
                                snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                                
                                free(dumpName);
                                dumpName = NULL;
                                
                                if (checkIfFileExists(dumpPath))
                                {
                                    addOnCnt++;
                                    if (checkIfDumpedNspContainsConsoleData(dumpPath)) addOnCntConsoleData++;
                                }
                            }
                        }
                    }
                }
                
                if (!dumpedXci && !dumpedBase && !patchCnt && !addOnCnt)
                {
                    strcat(dumpedContentInfoStr, "NONE");
                } else {
                    if (dumpedXci)
                    {
                        strcat(dumpedContentInfoStr, "XCI");
                        
                        if (dumpedXciCertificate)
                        {
                            strcat(dumpedContentInfoStr, " (with cert)");
                        } else {
                            strcat(dumpedContentInfoStr, " (without cert)");
                        }
                    }
                    
                    if (dumpedBase)
                    {
                        if (dumpedXci) strcat(dumpedContentInfoStr, ", ");
                        
                        strcat(dumpedContentInfoStr, "BASE");
                        
                        if (dumpedBaseConsoleData)
                        {
                            strcat(dumpedContentInfoStr, " (with console data)");
                        } else {
                            strcat(dumpedContentInfoStr, " (without console data)");
                        }
                    }
                    
                    if (patchCnt)
                    {
                        if (patchCntConsoleData)
                        {
                            if (patchCntConsoleData == patchCnt)
                            {
                                if (patchCnt > 1)
                                {
                                    snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "%u UPD (all with console data)", patchCnt);
                                } else {
                                    snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "UPD (with console data)");
                                }
                            } else {
                                snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "%u UPD (%u with console data)", patchCnt, patchCntConsoleData);
                            }
                        } else {
                            if (patchCnt > 1)
                            {
                                snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "%u UPD (all without console data)", patchCnt);
                            } else {
                                snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "UPD (without console data)");
                            }
                        }
                        
                        if (dumpedBase) strcat(dumpedContentInfoStr, ", ");
                        
                        strcat(dumpedContentInfoStr, tmpStr);
                    }
                    
                    if (addOnCnt)
                    {
                        if (addOnCntConsoleData)
                        {
                            if (addOnCntConsoleData == addOnCnt)
                            {
                                snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "%u DLC (%s console data)", addOnCnt, (addOnCnt > 1 ? "all with" : "with"));
                            } else {
                                snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "%u DLC (%u with console data)", addOnCnt, addOnCntConsoleData);
                            }
                        } else {
                            snprintf(tmpStr, MAX_ELEMENTS(tmpStr), "%u DLC (%s console data)", addOnCnt, (addOnCnt > 1 ? "all without" : "without"));
                        }
                        
                        if (dumpedBase || patchCnt) strcat(dumpedContentInfoStr, ", ");
                        
                        strcat(dumpedContentInfoStr, tmpStr);
                    }
                }
            }
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, dumpedContentInfoStr);
            
            breaks += 2;
        } else
        if (menuType == MENUTYPE_SDCARD_EMMC && orphanMode && (uiState == stateSdCardEmmcTitleMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu || uiState == stateRomFsMenu || uiState == stateTicketMenu))
        {
            if (strlen(orphanEntries[orphanListCursor].name))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Parent base application: %s", orphanEntries[orphanListCursor].name);
                breaks++;
            }
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Title ID: %016lX", (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? titlePatchTitleID[selectedPatchIndex] : titleAddOnTitleID[selectedAddOnIndex]));
            breaks++;
            
            convertTitleVersionToDecimal((orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? titlePatchVersion[selectedPatchIndex] : titleAddOnVersion[selectedAddOnIndex]), versionStr, MAX_ELEMENTS(versionStr));
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Version: %s", versionStr);
            breaks++;
            
            if (!strlen(dumpedContentInfoStr))
            {
                // Look for dumped content in the SD card
                char *dumpName = NULL;
                char dumpPath[NAME_BUF_LEN] = {'\0'};
                
                snprintf(dumpedContentInfoStr, MAX_ELEMENTS(dumpedContentInfoStr), "Title already dumped: ");
                
                if (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH)
                {
                    dumpName = generateNSPDumpName(DUMP_PATCH_NSP, selectedPatchIndex);
                } else
                if (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_ADDON)
                {
                    dumpName = generateNSPDumpName(DUMP_ADDON_NSP, selectedAddOnIndex);
                }
                
                if (dumpName)
                {
                    snprintf(dumpPath, MAX_ELEMENTS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                    
                    free(dumpName);
                    dumpName = NULL;
                    
                    if (checkIfFileExists(dumpPath))
                    {
                        strcat(dumpedContentInfoStr, "Yes");
                        
                        if (checkIfDumpedNspContainsConsoleData(dumpPath))
                        {
                            strcat(dumpedContentInfoStr, " (with console data)");
                        } else {
                            strcat(dumpedContentInfoStr, " (without console data)");
                        }
                    } else {
                        strcat(dumpedContentInfoStr, "No");
                    }
                } else {
                    strcat(dumpedContentInfoStr, "No");
                }
            }
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, dumpedContentInfoStr);
            breaks += 2;
        } else {
            dumpedContentInfoStr[0] = '\0';
        }
        
        switch(uiState)
        {
            case stateMainMenu:
                menu = mainMenuItems;
                menuItemsCount = MAX_ELEMENTS(mainMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Main menu");
                
                break;
            case stateGameCardMenu:
                menu = gameCardMenuItems;
                menuItemsCount = MAX_ELEMENTS(gameCardMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, mainMenuItems[0]);
                
                break;
            case stateXciDumpMenu:
                menu = xciDumpMenuItems;
                menuItemsCount = MAX_ELEMENTS(xciDumpMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[0]);
                
                break;
            case stateNspDumpMenu:
                if (menuType == MENUTYPE_GAMECARD)
                {
                    menu = nspDumpGameCardMenuItems;
                    menuItemsCount = MAX_ELEMENTS(nspDumpGameCardMenuItems);
                } else
                if (menuType == MENUTYPE_SDCARD_EMMC)
                {
                    menu = nspDumpSdCardEmmcMenuItems;
                    menuItemsCount = MAX_ELEMENTS(nspDumpSdCardEmmcMenuItems);
                }
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[1]);
                
                break;
            case stateNspAppDumpMenu:
                menu = nspAppDumpMenuItems;
                menuItemsCount = MAX_ELEMENTS(nspAppDumpMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, nspDumpGameCardMenuItems[0]);
                
                break;
            case stateNspPatchDumpMenu:
                menu = nspPatchDumpMenuItems;
                menuItemsCount = MAX_ELEMENTS(nspPatchDumpMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (menuType == MENUTYPE_GAMECARD ? nspDumpGameCardMenuItems[1] : nspDumpSdCardEmmcMenuItems[1]));
                
                break;
            case stateNspAddOnDumpMenu:
                menu = nspAddOnDumpMenuItems;
                menuItemsCount = MAX_ELEMENTS(nspAddOnDumpMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (menuType == MENUTYPE_GAMECARD ? nspDumpGameCardMenuItems[2] : nspDumpSdCardEmmcMenuItems[2]));
                
                break;
            case stateHfs0Menu:
                menu = hfs0MenuItems;
                menuItemsCount = MAX_ELEMENTS(hfs0MenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[2]);
                
                break;
            case stateRawHfs0PartitionDumpMenu:
            case stateHfs0PartitionDataDumpMenu:
                menu = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0PartitionDumpType1MenuItems : hfs0PartitionDumpType2MenuItems);
                menuItemsCount = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? MAX_ELEMENTS(hfs0PartitionDumpType1MenuItems) : MAX_ELEMENTS(hfs0PartitionDumpType2MenuItems));
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (uiState == stateRawHfs0PartitionDumpMenu ? hfs0MenuItems[0] : hfs0MenuItems[1]));
                
                break;
            case stateHfs0BrowserMenu:
                menu = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0BrowserType1MenuItems : hfs0BrowserType2MenuItems);
                menuItemsCount = (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? MAX_ELEMENTS(hfs0BrowserType1MenuItems) : MAX_ELEMENTS(hfs0BrowserType2MenuItems));
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, hfs0MenuItems[2]);
                
                break;
            case stateHfs0Browser:
                menu = (const char**)filenames;
                menuItemsCount = filenamesCount;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0BrowserType1MenuItems[selectedPartitionIndex] : hfs0BrowserType2MenuItems[selectedPartitionIndex]));
                breaks += 2;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Entry count: %d | Current entry: %d", menuItemsCount, cursor + 1);
                
                break;
            case stateExeFsMenu:
                menu = exeFsMenuItems;
                menuItemsCount = MAX_ELEMENTS(exeFsMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[3]);
                
                break;
            case stateExeFsSectionDataDumpMenu:
                menu = exeFsSectionDumpMenuItems;
                menuItemsCount = MAX_ELEMENTS(exeFsSectionDumpMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[0]);
                
                break;
            case stateExeFsSectionBrowserMenu:
                menu = exeFsSectionBrowserMenuItems;
                menuItemsCount = MAX_ELEMENTS(exeFsSectionBrowserMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[1]);
                
                break;
            case stateExeFsSectionBrowser:
                menu = (const char**)filenames;
                menuItemsCount = filenamesCount;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[1]);
                breaks++;
                
                if (!exeFsUpdateFlag)
                {
                    convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                    snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", exeFsSectionBrowserMenuItems[1], titleName[selectedAppIndex], versionStr);
                } else {
                    retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update to browse: ", strbuf, MAX_ELEMENTS(strbuf));
                }
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
                breaks += 2;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Entry count: %d | Current entry: %d", menuItemsCount, cursor + 1);
                
                break;
            case stateRomFsMenu:
                menu = romFsMenuItems;
                menuItemsCount = MAX_ELEMENTS(romFsMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[4]);
                
                break;
            case stateRomFsSectionDataDumpMenu:
                menu = romFsSectionDumpMenuItems;
                menuItemsCount = MAX_ELEMENTS(romFsSectionDumpMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[0]);
                
                break;
            case stateRomFsSectionBrowserMenu:
                menu = romFsSectionBrowserMenuItems;
                menuItemsCount = MAX_ELEMENTS(romFsSectionBrowserMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[1]);
                
                break;
            case stateRomFsSectionBrowser:
                menu = (const char**)filenames;
                menuItemsCount = filenamesCount;
                
                // Skip the parent directory entry ("..") in the RomFS browser if we're currently at the root directory
                if (menu && menuItemsCount && strlen(curRomFsPath) <= 1)
                {
                    menu++;
                    menuItemsCount--;
                }
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[1]);
                breaks++;
                
                switch(curRomFsType)
                {
                    case ROMFS_TYPE_APP:
                        convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                        snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", romFsSectionBrowserMenuItems[1], titleName[selectedAppIndex], versionStr);
                        break;
                    case ROMFS_TYPE_PATCH:
                        retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update to browse: ", strbuf, MAX_ELEMENTS(strbuf));
                        break;
                    case ROMFS_TYPE_ADDON:
                        retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, "DLC to browse: ", strbuf, MAX_ELEMENTS(strbuf));
                        break;
                    default:
                        break;
                }
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
                breaks += 2;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Path: romfs:%s", curRomFsPath);
                breaks += 2;
                
                if (strlen(curRomFsPath) <= 1 || (strlen(curRomFsPath) > 1 && cursor > 0))
                {
                    snprintf(strbuf, MAX_ELEMENTS(strbuf), "Entry count: %d | Current entry: %d", menuItemsCount - 1, (strlen(curRomFsPath) <= 1 ? (cursor + 1) : cursor));
                } else {
                    snprintf(strbuf, MAX_ELEMENTS(strbuf), "Entry count: %d", menuItemsCount - 1);
                }
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, strbuf);
                
                break;
            case stateSdCardEmmcMenu:
                menu = (const char**)titleName;
                menuItemsCount = (int)titleAppCount;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, mainMenuItems[1]);
                
                if (menuItemsCount)
                {
                    breaks += 2;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Title count: %d | Current title: %d", menuItemsCount, cursor + 1);
                }
                
                breaks++;
                
                break;
            case stateSdCardEmmcTitleMenu:
                menu = sdCardEmmcMenuItems;
                menuItemsCount = MAX_ELEMENTS(sdCardEmmcMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (!orphanMode ? mainMenuItems[1] : "Dump orphan DLC content"));
                
                break;
            case stateSdCardEmmcOrphanPatchAddOnMenu:
                if (orphanEntries == NULL) generateOrphanPatchOrAddOnList();
                
                menu = (const char**)filenames;
                menuItemsCount = filenamesCount;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Dump installed content with missing base application");
                
                if (menuItemsCount)
                {
                    breaks += 2;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Title count: %d | Current title: %d", menuItemsCount, cursor + 1);
                }
                
                break;
            case stateSdCardEmmcBatchModeMenu:
                menu = batchModeMenuItems;
                menuItemsCount = MAX_ELEMENTS(batchModeMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Batch mode");
                
                break;
            case stateTicketMenu:
                menu = ticketMenuItems;
                menuItemsCount = MAX_ELEMENTS(ticketMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, sdCardEmmcMenuItems[3]);
                
                break;
            case stateUpdateMenu:
                menu = updateMenuItems;
                menuItemsCount = MAX_ELEMENTS(updateMenuItems);
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, mainMenuItems[2]);
                
                break;
            default:
                break;
        }
        
        if (menu && menuItemsCount)
        {
            breaks++;
            
            if (scroll > 0)
            {
                u32 arrowWidth = uiGetStrWidth(upwardsArrow);
                
                uiDrawString((FB_WIDTH / 2) - (arrowWidth / 2), STRING_Y_POS(breaks), FONT_COLOR_RGB, upwardsArrow);
            }
            
            breaks++;
            
            j = 0;
            highlight = false;
            
            for(i = scroll; i < menuItemsCount; i++, j++)
            {
                if (j >= maxElements) break;
                
                // Avoid printing the "Create directory with archive bit set" option if "Split output dump" is disabled
                if (uiState == stateXciDumpMenu && i == 2 && !dumpCfg.xciDumpCfg.isFat32)
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Dump bundled update NSP" / "Dump installed update NSP" option in the NSP dump menu if we're dealing with a gamecard and it doesn't include any bundled updates, or if we're dealing with a SD/eMMC title without installed updates
                // Also avoid printing the "Dump bundled DLC NSP" / "Dump installed DLC NSP" option in the NSP dump menu if we're dealing with a gamecard and it doesn't include any bundled DLCs, or if we're dealing with a SD/eMMC title without installed DLCs
                if (uiState == stateNspDumpMenu && ((i == 1 && (!titlePatchCount || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)))) || (i == 2 && (!titleAddOnCount || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Remove console specific data" option in the NSP dump menus if we're dealing with a gamecard title
                // Also, avoid printing the "Generate ticket-less dump" option in the NSP dump menus if we're dealing with a gamecard Application/AddOn title
                if (menuType == MENUTYPE_GAMECARD && (((uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu) && i == 3) || ((uiState == stateNspAppDumpMenu || uiState == stateNspAddOnDumpMenu) && i == 4)))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Generate ticket-less dump" option in the NSP dump menus if we're dealing with a SD/eMMC title and the "Remove console specific data" option is disabled
                if (menuType == MENUTYPE_SDCARD_EMMC && (uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu) && i == 4 && !dumpCfg.nspDumpCfg.removeConsoleData)
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Dump base applications", "Dump updates" and/or "Dump DLCs" options in the batch mode menu if we're dealing with a storage source that doesn't hold any title belonging to the current category
                if (uiState == stateSdCardEmmcBatchModeMenu && ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && ((!titleAppCount && i == 1) || (!titlePatchCount && i == 2) || (!titleAddOnCount && i == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && ((!sdCardTitleAppCount && i == 1) || (!sdCardTitlePatchCount && i == 2) || (!sdCardTitleAddOnCount && i == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && ((!nandUserTitleAppCount && i == 1) || (!nandUserTitlePatchCount && i == 2) || (!nandUserTitleAddOnCount && i == 3)))))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Generate ticket-less dumps" option in the batch mode menu if the "Remove console specific data" option is disabled
                if (uiState == stateSdCardEmmcBatchModeMenu && i == 6 && !dumpCfg.batchDumpCfg.removeConsoleData)
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Source storage" option in the batch mode menu if we only have titles available in a single source storage device
                if (uiState == stateSdCardEmmcBatchModeMenu && i == 10 && ((!sdCardTitleAppCount && !sdCardTitlePatchCount && !sdCardTitleAddOnCount) || (!nandUserTitleAppCount && !nandUserTitlePatchCount && !nandUserTitleAddOnCount)))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Use update" option in the ExeFS menu if we're dealing with a gamecard and either its base application count is greater than 1 or it has no available patches
                // Also avoid printing it if we're dealing with a SD/eMMC title and it has no available patches
                if (uiState == stateExeFsMenu && i == 2 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || !checkIfBaseApplicationHasPatchOrAddOn(0, false))) || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false))))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Use update" option in the ExeFS data dump and browser menus if we're not dealing with a gamecard, if the base application count is equal to or less than 1, or if the selected base application has no available patches
                if ((uiState == stateExeFsSectionDataDumpMenu || uiState == stateExeFsSectionBrowserMenu) && i == 2 && (menuType != MENUTYPE_GAMECARD || titleAppCount <= 1 || !checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false)))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Use update/DLC" option in the RomFS menu if we're dealing with a gamecard and either its base application count is greater than 1 or it has no available patches/DLCs
                // Also avoid printing it if we're dealing with a SD/eMMC title and it has no available patches/DLCs (or if its an orphan title)
                if (uiState == stateRomFsMenu && i == 2 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || (!checkIfBaseApplicationHasPatchOrAddOn(0, false) && !checkIfBaseApplicationHasPatchOrAddOn(0, true)))) || (menuType == MENUTYPE_SDCARD_EMMC && (orphanMode || (!checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Use update/DLC" option in the RomFS data dump and browser menus if we're not dealing with a gamecard, if the base application count is equal to or less than 1, or if the selected base application has no available patches/DLCs
                if ((uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu) && i == 2 && (menuType != MENUTYPE_GAMECARD || titleAppCount <= 1 || (!checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false) && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true))))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "ExeFS options" element in the SD card / eMMC title menu if we're dealing with an orphan DLC
                // Also avoid printing the "RomFS options" element in the SD card / eMMC title menu if we're dealing with an orphan Patch
                if (uiState == stateSdCardEmmcTitleMenu && orphanMode && ((orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_ADDON && i == 1) || (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH && (i == 1 || i == 2))))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Use ticket from title" element in the Ticket menu if we're dealing with an orphan title
                if (uiState == stateTicketMenu && orphanMode && i == 2)
                {
                    j--;
                    continue;
                }
                
                xpos = STRING_X_POS;
                ypos = ((breaks * LINE_HEIGHT) + (uiState == stateSdCardEmmcMenu ? (j * (NACP_ICON_DOWNSCALED + 12)) : (j * (font_height + 12))) + 6);
                
                if (i == cursor)
                {
                    highlight = true;
                    uiFill(0, (ypos + 8) - 6, FB_WIDTH, (uiState == stateSdCardEmmcMenu ? (NACP_ICON_DOWNSCALED + 12) : (font_height + 12)), HIGHLIGHT_BG_COLOR_RGB);
                }
                
                if (uiState == stateSdCardEmmcMenu)
                {
                    if (titleIcon != NULL && titleIcon[i] != NULL)
                    {
                        uiDrawIcon(titleIcon[i], NACP_ICON_DOWNSCALED, NACP_ICON_DOWNSCALED, xpos, ypos + 8);
                        
                        xpos += (NACP_ICON_DOWNSCALED + 8);
                    }
                    
                    ypos += ((NACP_ICON_DOWNSCALED / 2) - (font_height / 2));
                } else
                if (uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser || uiState == stateRomFsSectionBrowser)
                {
                    u8 *icon = (highlight ? (uiState == stateRomFsSectionBrowser ? (romFsBrowserEntries[i].type == ROMFS_ENTRY_DIR ? dirHighlightIconBuf : fileHighlightIconBuf) : fileHighlightIconBuf) : (uiState == stateRomFsSectionBrowser ? (romFsBrowserEntries[i].type == ROMFS_ENTRY_DIR ? dirNormalIconBuf : fileNormalIconBuf) : fileNormalIconBuf));
                    
                    uiDrawIcon(icon, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, xpos, ypos + 8);
                    
                    xpos += (BROWSER_ICON_DIMENSION + 8);
                }
                
                if (highlight)
                {
                    uiDrawString(xpos, ypos, HIGHLIGHT_FONT_COLOR_RGB, menu[i]);
                } else {
                    uiDrawString(xpos, ypos, FONT_COLOR_RGB, menu[i]);
                }
                
                xpos = OPTIONS_X_START_POS;
                
                bool leftArrowCondition = false;
                bool rightArrowCondition = false;
                
                // Print XCI dump menu settings values
                if (uiState == stateXciDumpMenu && i > 0)
                {
                    switch(i)
                    {
                        case 1: // Split output dump (FAT32 support)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.xciDumpCfg.isFat32, !dumpCfg.xciDumpCfg.isFat32, (dumpCfg.xciDumpCfg.isFat32 ? 0 : 255), (dumpCfg.xciDumpCfg.isFat32 ? 255 : 0), 0, (dumpCfg.xciDumpCfg.isFat32 ? "Yes" : "No"));
                            break;
                        case 2: // Create directory with archive bit set
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.xciDumpCfg.setXciArchiveBit, !dumpCfg.xciDumpCfg.setXciArchiveBit, (dumpCfg.xciDumpCfg.setXciArchiveBit ? 0 : 255), (dumpCfg.xciDumpCfg.setXciArchiveBit ? 255 : 0), 0, (dumpCfg.xciDumpCfg.setXciArchiveBit ? "Yes" : "No"));
                            break;
                        case 3: // Keep certificate
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.xciDumpCfg.keepCert, !dumpCfg.xciDumpCfg.keepCert, (dumpCfg.xciDumpCfg.keepCert ? 0 : 255), (dumpCfg.xciDumpCfg.keepCert ? 255 : 0), 0, (dumpCfg.xciDumpCfg.keepCert ? "Yes" : "No"));
                            break;
                        case 4: // Trim output dump
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.xciDumpCfg.trimDump, !dumpCfg.xciDumpCfg.trimDump, (dumpCfg.xciDumpCfg.trimDump ? 0 : 255), (dumpCfg.xciDumpCfg.trimDump ? 255 : 0), 0, (dumpCfg.xciDumpCfg.trimDump ? "Yes" : "No"));
                            break;
                        case 5: // CRC32 checksum calculation + dump verification
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.xciDumpCfg.calcCrc, !dumpCfg.xciDumpCfg.calcCrc, (dumpCfg.xciDumpCfg.calcCrc ? 0 : 255), (dumpCfg.xciDumpCfg.calcCrc ? 255 : 0), 0, (dumpCfg.xciDumpCfg.calcCrc ? "Yes" : "No"));
                            break;
                        default:
                            break;
                    }
                }
                
                // Print NSP dump menus settings values
                if ((uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu) && i > 0)
                {
                    switch(i)
                    {
                        case 1: // Split output dump (FAT32 support)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.nspDumpCfg.isFat32, !dumpCfg.nspDumpCfg.isFat32, (dumpCfg.nspDumpCfg.isFat32 ? 0 : 255), (dumpCfg.nspDumpCfg.isFat32 ? 255 : 0), 0, (dumpCfg.nspDumpCfg.isFat32 ? "Yes" : "No"));
                            break;
                        case 2: // CRC32 checksum calculation
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.nspDumpCfg.calcCrc, !dumpCfg.nspDumpCfg.calcCrc, (dumpCfg.nspDumpCfg.calcCrc ? 0 : 255), (dumpCfg.nspDumpCfg.calcCrc ? 255 : 0), 0, (dumpCfg.nspDumpCfg.calcCrc ? "Yes" : "No"));
                            break;
                        case 3: // Remove console specific data
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.nspDumpCfg.removeConsoleData, !dumpCfg.nspDumpCfg.removeConsoleData, (dumpCfg.nspDumpCfg.removeConsoleData ? 0 : 255), (dumpCfg.nspDumpCfg.removeConsoleData ? 255 : 0), 0, (dumpCfg.nspDumpCfg.removeConsoleData ? "Yes" : "No"));
                            break;
                        case 4: // Generate ticket-less dump
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.nspDumpCfg.tiklessDump, !dumpCfg.nspDumpCfg.tiklessDump, (dumpCfg.nspDumpCfg.tiklessDump ? 0 : 255), (dumpCfg.nspDumpCfg.tiklessDump ? 255 : 0), 0, (dumpCfg.nspDumpCfg.tiklessDump ? "Yes" : "No"));
                            break;
                        case 5: // Change NPDM RSA key/sig in Program NCA || DLC to dump
                            if (uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu)
                            {
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.nspDumpCfg.npdmAcidRsaPatch, !dumpCfg.nspDumpCfg.npdmAcidRsaPatch, (dumpCfg.nspDumpCfg.npdmAcidRsaPatch ? 0 : 255), (dumpCfg.nspDumpCfg.npdmAcidRsaPatch ? 255 : 0), 0, (dumpCfg.nspDumpCfg.npdmAcidRsaPatch ? "Yes" : "No"));
                            } else
                            if (uiState == stateNspAddOnDumpMenu)
                            {
                                if (!strlen(titleSelectorStr))
                                {
                                    // Find a matching application to print its name and Title ID
                                    // Otherwise, just print the Title ID
                                    retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, (menuType == MENUTYPE_GAMECARD), NULL, titleSelectorStr, MAX_ELEMENTS(titleSelectorStr));
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = ((menuType == MENUTYPE_GAMECARD && titleAddOnCount > 0 && selectedAddOnIndex > 0) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppInfoIndex, true) != selectedAddOnIndex));
                                rightArrowCondition = ((menuType == MENUTYPE_GAMECARD && titleAddOnCount > 0 && selectedAddOnIndex < (titleAddOnCount - 1)) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppInfoIndex, true) != selectedAddOnIndex));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, titleSelectorStr);
                            }
                            break;
                        case 6: // Application/update to dump
                            if (uiState == stateNspAppDumpMenu)
                            {
                                if (!strlen(titleSelectorStr))
                                {
                                    // Print application name
                                    convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                                    snprintf(titleSelectorStr, MAX_ELEMENTS(titleSelectorStr), "%s v%s", titleName[selectedAppIndex], versionStr);
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex > 0);
                                rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex < (titleAppCount - 1));
                            } else
                            if (uiState == stateNspPatchDumpMenu)
                            {
                                if (!strlen(titleSelectorStr))
                                {
                                    // Find a matching application to print its name
                                    // Otherwise, just print the Title ID
                                    retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, (menuType == MENUTYPE_GAMECARD), NULL, titleSelectorStr, MAX_ELEMENTS(titleSelectorStr));
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = ((menuType == MENUTYPE_GAMECARD && titlePatchCount > 0 && selectedPatchIndex > 0) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppInfoIndex, false) != selectedPatchIndex));
                                rightArrowCondition = ((menuType == MENUTYPE_GAMECARD && titlePatchCount > 0 && selectedPatchIndex < (titlePatchCount - 1)) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppInfoIndex, false) != selectedPatchIndex));
                            }
                            
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, titleSelectorStr);
                            break;
                        default:
                            break;
                    }
                    
                    if (i == 2)
                    {
                        if (dumpCfg.nspDumpCfg.calcCrc)
                        {
                            uiDrawString(FB_WIDTH / 2, ypos, FONT_COLOR_RGB, "This takes extra time after the NSP dump has been completed!");
                        } else {
                            if (highlight)
                            {
                                uiFill(FB_WIDTH / 2, (ypos + 8) - 6, FB_WIDTH / 2, font_height + 12, HIGHLIGHT_BG_COLOR_RGB);
                            } else {
                                uiFill(FB_WIDTH / 2, (ypos + 8) - 6, FB_WIDTH / 2, font_height + 12, BG_COLOR_RGB);
                            }
                        }
                    }
                }
                
                // Print settings values for the batch mode menu
                if (uiState == stateSdCardEmmcBatchModeMenu && i > 0)
                {
                    switch(i)
                    {
                        case 1: // Dump base applications
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.dumpAppTitles, !dumpCfg.batchDumpCfg.dumpAppTitles, (dumpCfg.batchDumpCfg.dumpAppTitles ? 0 : 255), (dumpCfg.batchDumpCfg.dumpAppTitles ? 255 : 0), 0, (dumpCfg.batchDumpCfg.dumpAppTitles ? "Yes" : "No"));
                            break;
                        case 2: // Dump updates
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.dumpPatchTitles, !dumpCfg.batchDumpCfg.dumpPatchTitles, (dumpCfg.batchDumpCfg.dumpPatchTitles ? 0 : 255), (dumpCfg.batchDumpCfg.dumpPatchTitles ? 255 : 0), 0, (dumpCfg.batchDumpCfg.dumpPatchTitles ? "Yes" : "No"));
                            break;
                        case 3: // Dump DLCs
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.dumpAddOnTitles, !dumpCfg.batchDumpCfg.dumpAddOnTitles, (dumpCfg.batchDumpCfg.dumpAddOnTitles ? 0 : 255), (dumpCfg.batchDumpCfg.dumpAddOnTitles ? 255 : 0), 0, (dumpCfg.batchDumpCfg.dumpAddOnTitles ? "Yes" : "No"));
                            break;
                        case 4: // Split output dumps (FAT32 support)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.isFat32, !dumpCfg.batchDumpCfg.isFat32, (dumpCfg.batchDumpCfg.isFat32 ? 0 : 255), (dumpCfg.batchDumpCfg.isFat32 ? 255 : 0), 0, (dumpCfg.batchDumpCfg.isFat32 ? "Yes" : "No"));
                            break;
                        case 5: // Remove console specific data
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.removeConsoleData, !dumpCfg.batchDumpCfg.removeConsoleData, (dumpCfg.batchDumpCfg.removeConsoleData ? 0 : 255), (dumpCfg.batchDumpCfg.removeConsoleData ? 255 : 0), 0, (dumpCfg.batchDumpCfg.removeConsoleData ? "Yes" : "No"));
                            break;
                        case 6: // Generate ticket-less dumps
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.tiklessDump, !dumpCfg.batchDumpCfg.tiklessDump, (dumpCfg.batchDumpCfg.tiklessDump ? 0 : 255), (dumpCfg.batchDumpCfg.tiklessDump ? 255 : 0), 0, (dumpCfg.batchDumpCfg.tiklessDump ? "Yes" : "No"));
                            break;
                        case 7: // Change NPDM RSA key/sig in Program NCA
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.npdmAcidRsaPatch, !dumpCfg.batchDumpCfg.npdmAcidRsaPatch, (dumpCfg.batchDumpCfg.npdmAcidRsaPatch ? 0 : 255), (dumpCfg.batchDumpCfg.npdmAcidRsaPatch ? 255 : 0), 0, (dumpCfg.batchDumpCfg.npdmAcidRsaPatch ? "Yes" : "No"));
                            break;
                        case 8: // Skip dumped titles
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.skipDumpedTitles, !dumpCfg.batchDumpCfg.skipDumpedTitles, (dumpCfg.batchDumpCfg.skipDumpedTitles ? 0 : 255), (dumpCfg.batchDumpCfg.skipDumpedTitles ? 255 : 0), 0, (dumpCfg.batchDumpCfg.skipDumpedTitles ? "Yes" : "No"));
                            break;
                        case 9: // Remember dumped titles
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.rememberDumpedTitles, !dumpCfg.batchDumpCfg.rememberDumpedTitles, (dumpCfg.batchDumpCfg.rememberDumpedTitles ? 0 : 255), (dumpCfg.batchDumpCfg.rememberDumpedTitles ? 255 : 0), 0, (dumpCfg.batchDumpCfg.rememberDumpedTitles ? "Yes" : "No"));
                            break;
                        case 10: // Source storage
                            leftArrowCondition = (dumpCfg.batchDumpCfg.batchModeSrc != BATCH_SOURCE_ALL);
                            rightArrowCondition = (dumpCfg.batchDumpCfg.batchModeSrc != BATCH_SOURCE_EMMC);                            
                            
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL ? "All (SD card + eMMC)" : (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD ? "SD card" : "eMMC")));
                            
                            break;
                        default:
                            break;
                    }
                }
                
                // Print settings values for ExeFS menu
                if (uiState == stateExeFsMenu && i > 1)
                {
                    u32 appIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                    
                    switch(i)
                    {
                        case 2: // Use update
                            if (exeFsUpdateFlag)
                            {
                                if (!strlen(exeFsAndRomFsSelectorStr))
                                {
                                    // Find a matching application to print its name
                                    // Otherwise, just print the Title ID
                                    retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, (menuType == MENUTYPE_GAMECARD && titleAppCount > 1), NULL, exeFsAndRomFsSelectorStr, MAX_ELEMENTS(exeFsAndRomFsSelectorStr));
                                    
                                    // Concatenate patch source storage
                                    strcat(exeFsAndRomFsSelectorStr, (titlePatchStorageId[selectedPatchIndex] == FsStorageId_GameCard ? " (gamecard)" : (titlePatchStorageId[selectedPatchIndex] == FsStorageId_SdCard ? " (SD card)" : "(eMMC)")));
                                    
                                    uiTruncateOptionStr(exeFsAndRomFsSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = true;
                                rightArrowCondition = (((menuType == MENUTYPE_GAMECARD && titleAppCount == 1) || menuType == MENUTYPE_SDCARD_EMMC) && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, appIndex, false) != selectedPatchIndex);
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, exeFsAndRomFsSelectorStr);
                            } else {
                                leftArrowCondition = false;
                                rightArrowCondition = (((menuType == MENUTYPE_GAMECARD && titleAppCount == 1) || menuType == MENUTYPE_SDCARD_EMMC) && checkIfBaseApplicationHasPatchOrAddOn(appIndex, false));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_ERROR_RGB, "No");
                            }
                            
                            break;
                        default:
                            break;
                    }
                }
                
                // Print settings values for ExeFS submenus
                if ((uiState == stateExeFsSectionDataDumpMenu || uiState == stateExeFsSectionBrowserMenu) && i > 0)
                {
                    switch(i)
                    {
                        case 1: // Bundled application to dump/browse
                            if (!strlen(titleSelectorStr))
                            {
                                // Print application name
                                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                                snprintf(titleSelectorStr, MAX_ELEMENTS(titleSelectorStr), "%s v%s", titleName[selectedAppIndex], versionStr);
                                uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                            }
                            
                            leftArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex > 0);
                            rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex < (titleAppCount - 1));
                            
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, titleSelectorStr);
                            
                            break;
                        case 2: // Use update
                            if (exeFsUpdateFlag)
                            {
                                if (!strlen(exeFsAndRomFsSelectorStr))
                                {
                                    // Find a matching application to print its name
                                    // Otherwise, just print the Title ID
                                    retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, (menuType == MENUTYPE_GAMECARD), NULL, exeFsAndRomFsSelectorStr, MAX_ELEMENTS(exeFsAndRomFsSelectorStr));
                                    
                                    // Concatenate patch source storage
                                    strcat(exeFsAndRomFsSelectorStr, (titlePatchStorageId[selectedPatchIndex] == FsStorageId_GameCard ? " (gamecard)" : (titlePatchStorageId[selectedPatchIndex] == FsStorageId_SdCard ? " (SD card)" : "(eMMC)")));
                                    
                                    uiTruncateOptionStr(exeFsAndRomFsSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = true;
                                rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppIndex, false) != selectedPatchIndex);
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, exeFsAndRomFsSelectorStr);
                            } else {
                                leftArrowCondition = false;
                                rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_ERROR_RGB, "No");
                            }
                            
                            break;
                        default:
                            break;
                    }
                }
                
                // Print settings values for RomFS menu
                if (uiState == stateRomFsMenu && i > 1)
                {
                    u32 appIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                    
                    switch(i)
                    {
                        case 2: // Use update/DLC
                            if (curRomFsType != ROMFS_TYPE_APP)
                            {
                                if (!strlen(exeFsAndRomFsSelectorStr))
                                {
                                    // Find a matching application to print its name. Otherwise, just print the Title ID
                                    // Concatenate title type
                                    // Concatenate source storage
                                    
                                    switch(curRomFsType)
                                    {
                                        case ROMFS_TYPE_PATCH:
                                            retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, (menuType == MENUTYPE_GAMECARD && titleAppCount > 1), NULL, exeFsAndRomFsSelectorStr, MAX_ELEMENTS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (UPD)");
                                            strcat(exeFsAndRomFsSelectorStr, (titlePatchStorageId[selectedPatchIndex] == FsStorageId_GameCard ? " (gamecard)" : (titlePatchStorageId[selectedPatchIndex] == FsStorageId_SdCard ? " (SD card)" : "(eMMC)")));
                                            break;
                                        case ROMFS_TYPE_ADDON:
                                            retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, (menuType == MENUTYPE_GAMECARD && titleAppCount > 1), NULL, exeFsAndRomFsSelectorStr, MAX_ELEMENTS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (DLC)");
                                            strcat(exeFsAndRomFsSelectorStr, (titleAddOnStorageId[selectedAddOnIndex] == FsStorageId_GameCard ? " (gamecard)" : (titleAddOnStorageId[selectedAddOnIndex] == FsStorageId_SdCard ? " (SD card)" : "(eMMC)")));
                                            break;
                                        default:
                                            break;
                                    }
                                    
                                    uiTruncateOptionStr(exeFsAndRomFsSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = true;
                                rightArrowCondition = (((menuType == MENUTYPE_GAMECARD && titleAppCount == 1) || menuType == MENUTYPE_SDCARD_EMMC) && ((curRomFsType == ROMFS_TYPE_PATCH && (retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, appIndex, false) != selectedPatchIndex || checkIfBaseApplicationHasPatchOrAddOn(appIndex, true))) || (curRomFsType == ROMFS_TYPE_ADDON && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, appIndex, true) != selectedAddOnIndex)));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, exeFsAndRomFsSelectorStr);
                            } else {
                                leftArrowCondition = false;
                                rightArrowCondition = (((menuType == MENUTYPE_GAMECARD && titleAppCount == 1) || menuType == MENUTYPE_SDCARD_EMMC) && (checkIfBaseApplicationHasPatchOrAddOn(appIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(appIndex, true)));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_ERROR_RGB, "No");
                            }
                            
                            break;
                        default:
                            break;
                    }
                }
                
                // Print settings values for RomFS submenus
                if ((uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu) && i > 0)
                {
                    switch(i)
                    {
                        case 1: // Bundled application to dump/browse
                            if (!strlen(titleSelectorStr))
                            {
                                // Print application name
                                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                                snprintf(titleSelectorStr, MAX_ELEMENTS(titleSelectorStr), "%s v%s", titleName[selectedAppIndex], versionStr);
                                uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                            }
                            
                            leftArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex > 0);
                            rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex < (titleAppCount - 1));
                            
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, titleSelectorStr);
                            
                            break;
                        case 2: // Use update/DLC
                            if (curRomFsType != ROMFS_TYPE_APP)
                            {
                                if (!strlen(exeFsAndRomFsSelectorStr))
                                {
                                    // Find a matching application to print its name. Otherwise, just print the Title ID
                                    // Concatenate title type
                                    // Concatenate source storage
                                    
                                    switch(curRomFsType)
                                    {
                                        case ROMFS_TYPE_PATCH:
                                            retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, (menuType == MENUTYPE_GAMECARD), NULL, exeFsAndRomFsSelectorStr, MAX_ELEMENTS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (UPD)");
                                            strcat(exeFsAndRomFsSelectorStr, (titlePatchStorageId[selectedPatchIndex] == FsStorageId_GameCard ? " (gamecard)" : (titlePatchStorageId[selectedPatchIndex] == FsStorageId_SdCard ? " (SD card)" : "(eMMC)")));
                                            break;
                                        case ROMFS_TYPE_ADDON:
                                            retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, (menuType == MENUTYPE_GAMECARD), NULL, exeFsAndRomFsSelectorStr, MAX_ELEMENTS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (DLC)");
                                            strcat(exeFsAndRomFsSelectorStr, (titleAddOnStorageId[selectedAddOnIndex] == FsStorageId_GameCard ? " (gamecard)" : (titleAddOnStorageId[selectedAddOnIndex] == FsStorageId_SdCard ? " (SD card)" : "(eMMC)")));
                                            break;
                                        default:
                                            break;
                                    }
                                    
                                    uiTruncateOptionStr(exeFsAndRomFsSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = true;
                                rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && ((curRomFsType == ROMFS_TYPE_PATCH && (retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppIndex, false) != selectedPatchIndex || checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true))) || (curRomFsType == ROMFS_TYPE_ADDON && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppIndex, true) != selectedAddOnIndex)));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, exeFsAndRomFsSelectorStr);
                            } else {
                                leftArrowCondition = false;
                                rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && (checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true)));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_ERROR_RGB, "No");
                            }
                            
                            break;
                        default:
                            break;
                    }
                }
                
                // Print settings values for the Ticket menu
                if (uiState == stateTicketMenu && i > 0)
                {
                    switch(i)
                    {
                        case 1: // Remove console specific data
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.tikDumpCfg.removeConsoleData, !dumpCfg.tikDumpCfg.removeConsoleData, (dumpCfg.tikDumpCfg.removeConsoleData ? 0 : 255), (dumpCfg.tikDumpCfg.removeConsoleData ? 255 : 0), 0, (dumpCfg.tikDumpCfg.removeConsoleData ? "Yes" : "No"));
                            break;
                        case 2: // Use ticket from title
                            if (curTikType != TICKET_TYPE_APP)
                            {
                                if (!strlen(titleSelectorStr))
                                {
                                    // Print update/DLC TID
                                    switch(curTikType)
                                    {
                                        case TICKET_TYPE_PATCH:
                                            retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, false, NULL, titleSelectorStr, MAX_ELEMENTS(titleSelectorStr));
                                            strcat(titleSelectorStr, " (UPD)");
                                            break;
                                        case TICKET_TYPE_ADDON:
                                            retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, false, NULL, titleSelectorStr, MAX_ELEMENTS(titleSelectorStr));
                                            strcat(titleSelectorStr, " (DLC)");
                                            break;
                                        default:
                                            break;
                                    }
                                    
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = true;
                                rightArrowCondition = ((curTikType == TICKET_TYPE_PATCH && (retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppInfoIndex, false) != selectedPatchIndex || checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))) || (curTikType == TICKET_TYPE_ADDON && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppInfoIndex, true) != selectedAddOnIndex));
                            } else {
                                if (!strlen(titleSelectorStr))
                                {
                                    // Print application TID
                                    convertTitleVersionToDecimal(titleAppVersion[selectedAppInfoIndex], versionStr, MAX_ELEMENTS(versionStr));
                                    snprintf(titleSelectorStr, MAX_ELEMENTS(titleSelectorStr), "%016lX v%s (BASE)", titleAppTitleID[selectedAppInfoIndex], versionStr);
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = false;
                                rightArrowCondition = (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true));
                            }
                            
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, titleSelectorStr);
                            
                            break;
                        default:
                            break;
                    }
                }
                
                if (i == cursor) highlight = false;
            }
            
            if ((scroll + maxElements) < menuItemsCount)
            {
                ypos = ((breaks * LINE_HEIGHT) + (uiState == stateSdCardEmmcMenu ? (j * (NACP_ICON_DOWNSCALED + 12)) : (j * (font_height + 12))));
                
                u32 arrowWidth = uiGetStrWidth(downwardsArrow);
                
                uiDrawString((FB_WIDTH / 2) - (arrowWidth / 2), ypos, FONT_COLOR_RGB, downwardsArrow);
            }
            
            // Print warning about missing Lockpick_RCM keys file
            if (uiState == stateMainMenu && !keysFileAvailable)
            {
                j++;
                if ((scroll + maxElements) < menuItemsCount) j++;
                
                ypos = ((breaks * LINE_HEIGHT) + (j * (font_height + 12)));
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "Warning: missing keys file at \"%s\".", KEYS_FILE_PATH);
                j++;
                
                ypos = ((breaks * LINE_HEIGHT) + (j * (font_height + 12)));
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "This file is needed to deal with the encryption schemes used by Nintendo Switch content files.");
                j++;
                
                ypos = ((breaks * LINE_HEIGHT) + (j * (font_height + 12)));
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "SD/eMMC operations will be entirely disabled, along with NSP/ExeFS/RomFS related operations.");
                j++;
                
                ypos = ((breaks * LINE_HEIGHT) + (j * (font_height + 12)));
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "Please run Lockpick_RCM to generate this file. More info at: https://github.com/shchmue/Lockpick_RCM");
            }
            
            // Print information about the "Change NPDM RSA key/sig in Program NCA" option
            if (((uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu) && cursor == 5) || (uiState == stateSdCardEmmcBatchModeMenu && cursor == 7))
            {
                j++;
                if ((scroll + maxElements) < menuItemsCount) j++;
                
                ypos = ((breaks * LINE_HEIGHT) + (j * (font_height + 12)));
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Replaces the public RSA key in the NPDM ACID section and the NPDM RSA signature in the Program NCA (only if it needs other modifications).");
                j++;
                
                ypos = ((breaks * LINE_HEIGHT) + (j * (font_height + 12)));
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Disabling this will make the output NSP require ACID patches to work under any CFW, but will also make the Program NCA verifiable by PC tools.");
            }
            
            // Print hint about dumping RomFS content from DLCs
            if ((uiState == stateRomFsMenu && ((menuType == MENUTYPE_GAMECARD && titleAppCount <= 1 && checkIfBaseApplicationHasPatchOrAddOn(0, true)) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true)))) || ((uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu) && (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true))))
            {
                j++;
                if ((scroll + maxElements) < menuItemsCount) j++;
                
                ypos = ((breaks * LINE_HEIGHT) + (j * (font_height + 12)));
                
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Hint: choosing a DLC will only access RomFS data from it, unlike updates (which share their RomFS data with its base application).");
            }
        } else {
            if (uiState == stateSdCardEmmcOrphanPatchAddOnMenu)
            {
                breaks += 2;
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "No orphan updates/DLCs available in the SD card / eMMC storage!");
            }
        }
        
        while(true)
        {
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            hidScanInput();
            
            keysDown = hidKeysDown(CONTROLLER_P1_AUTO);
            keysHeld = hidKeysHeld(CONTROLLER_P1_AUTO);
            
            if ((keysDown && !(keysDown & KEY_TOUCH)) || (keysHeld && !(keysHeld & KEY_TOUCH))) break;
        }
        
        // Exit
        if (keysDown & KEY_PLUS) res = resultExit;
        
        // Process key inputs only if the UI state hasn't been changed
        if (res == resultNone)
        {
            // Process base application info change
            if (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && uiState != stateHfs0Browser && uiState != stateExeFsSectionBrowser && uiState != stateRomFsSectionBrowser)
            {
                if ((keysDown & KEY_L) || (keysDown & KEY_ZL))
                {
                    if (selectedAppInfoIndex > 0)
                    {
                        selectedAppInfoIndex--;
                        dumpedContentInfoStr[0] = '\0';
                    }
                }
                
                if ((keysDown & KEY_R) || (keysDown & KEY_ZR))
                {
                    if ((selectedAppInfoIndex + 1) < titleAppCount)
                    {
                        selectedAppInfoIndex++;
                        dumpedContentInfoStr[0] = '\0';
                    }
                }
            }
            
            if (uiState == stateXciDumpMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0) res = resultDumpXci;
                
                // Back
                if (keysDown & KEY_B) res = resultShowGameCardMenu;
                
                // Change option to false
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 1: // Split output dump (FAT32 support)
                            dumpCfg.xciDumpCfg.isFat32 = dumpCfg.xciDumpCfg.setXciArchiveBit = false;
                            break;
                        case 2: // Create directory with archive bit set
                            dumpCfg.xciDumpCfg.setXciArchiveBit = false;
                            break;
                        case 3: // Keep certificate
                            dumpCfg.xciDumpCfg.keepCert = false;
                            break;
                        case 4: // Trim output dump
                            dumpCfg.xciDumpCfg.trimDump = false;
                            break;
                        case 5: // CRC32 checksum calculation + dump verification
                            dumpCfg.xciDumpCfg.calcCrc = false;
                            break;
                        default:
                            break;
                    }
                    
                    // Save settings to configuration file
                    saveConfig();
                }
                
                // Change option to true
                if (keysDown & KEY_RIGHT)
                {
                    switch(cursor)
                    {
                        case 1: // Split output dump (FAT32 support)
                            dumpCfg.xciDumpCfg.isFat32 = true;
                            break;
                        case 2: // Create directory with archive bit set
                            dumpCfg.xciDumpCfg.setXciArchiveBit = true;
                            break;
                        case 3: // Keep certificate
                            dumpCfg.xciDumpCfg.keepCert = true;
                            break;
                        case 4: // Trim output dump
                            dumpCfg.xciDumpCfg.trimDump = true;
                            break;
                        case 5: // CRC32 checksum calculation + dump verification
                            dumpCfg.xciDumpCfg.calcCrc = true;
                            break;
                        default:
                            break;
                    }
                    
                    // Save settings to configuration file
                    saveConfig();
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else
            if (uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0)
                {
                    selectedNspDumpType = (uiState == stateNspAppDumpMenu ? DUMP_APP_NSP : (uiState == stateNspPatchDumpMenu ? DUMP_PATCH_NSP : DUMP_ADDON_NSP));
                    res = resultDumpNsp;
                }
                
                // Back
                if (keysDown & KEY_B)
                {
                    if (menuType == MENUTYPE_GAMECARD)
                    {
                        if (uiState == stateNspAppDumpMenu && !titlePatchCount && !titleAddOnCount)
                        {
                            res = resultShowGameCardMenu;
                        } else {
                            res = resultShowNspDumpMenu;
                        }
                    } else
                    if (menuType == MENUTYPE_SDCARD_EMMC)
                    {
                        if (!orphanMode)
                        {
                            if (uiState == stateNspAppDumpMenu && (!titlePatchCount || !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)) && (!titleAddOnCount || !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true)))
                            {
                                res = resultShowSdCardEmmcTitleMenu;
                            } else {
                                res = resultShowNspDumpMenu;
                            }
                        } else {
                            res = resultShowSdCardEmmcTitleMenu;
                        }
                    }
                }
                
                // Change option to false
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 1: // Split output dump (FAT32 support)
                            dumpCfg.nspDumpCfg.isFat32 = false;
                            break;
                        case 2: // CRC32 checksum calculation
                            dumpCfg.nspDumpCfg.calcCrc = false;
                            break;
                        case 3: // Remove console specific data
                            dumpCfg.nspDumpCfg.removeConsoleData = dumpCfg.nspDumpCfg.tiklessDump = false;
                            break;
                        case 4: // Generate ticket-less dump
                            dumpCfg.nspDumpCfg.tiklessDump = false;
                            break;
                        case 5: // Change NPDM RSA key/sig in Program NCA || DLC to dump
                            if (uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu)
                            {
                                dumpCfg.nspDumpCfg.npdmAcidRsaPatch = false;
                            } else
                            if (uiState == stateNspAddOnDumpMenu)
                            {
                                if (menuType == MENUTYPE_GAMECARD)
                                {
                                    if (selectedAddOnIndex > 0)
                                    {
                                        selectedAddOnIndex--;
                                        titleSelectorStr[0] = '\0';
                                    }
                                } else
                                if (menuType == MENUTYPE_SDCARD_EMMC)
                                {
                                    if (!orphanMode)
                                    {
                                        u32 newIndex = retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppInfoIndex, true);
                                        if (newIndex != selectedAddOnIndex)
                                        {
                                            selectedAddOnIndex = newIndex;
                                            titleSelectorStr[0] = '\0';
                                        }
                                    }
                                }
                            }
                            break;
                        case 6: // Application/update to dump
                            if (uiState == stateNspAppDumpMenu)
                            {
                                if (menuType == MENUTYPE_GAMECARD)
                                {
                                    if (selectedAppIndex > 0)
                                    {
                                        selectedAppIndex--;
                                        titleSelectorStr[0] = '\0';
                                    }
                                }
                            } else
                            if (uiState == stateNspPatchDumpMenu)
                            {
                                if (menuType == MENUTYPE_GAMECARD)
                                {
                                    if (selectedPatchIndex > 0)
                                    {
                                        selectedPatchIndex--;
                                        titleSelectorStr[0] = '\0';
                                    }
                                } else
                                if (menuType == MENUTYPE_SDCARD_EMMC)
                                {
                                    if (!orphanMode)
                                    {
                                        u32 newIndex = retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppInfoIndex, false);
                                        if (newIndex != selectedPatchIndex)
                                        {
                                            selectedPatchIndex = newIndex;
                                            titleSelectorStr[0] = '\0';
                                        }
                                    }
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    
                    // Save settings to configuration file
                    saveConfig();
                }
                
                // Change option to true
                if (keysDown & KEY_RIGHT)
                {
                    switch(cursor)
                    {
                        case 1: // Split output dump (FAT32 support)
                            dumpCfg.nspDumpCfg.isFat32 = true;
                            break;
                        case 2: // CRC32 checksum calculation
                            dumpCfg.nspDumpCfg.calcCrc = true;
                            break;
                        case 3: // Remove console specific data
                            dumpCfg.nspDumpCfg.removeConsoleData = true;
                            break;
                        case 4: // Generate ticket-less dump
                            dumpCfg.nspDumpCfg.tiklessDump = true;
                            break;
                        case 5: // Change NPDM RSA key/sig in Program NCA || DLC to dump
                            if (uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu)
                            {
                                dumpCfg.nspDumpCfg.npdmAcidRsaPatch = true;
                            } else {
                                if (menuType == MENUTYPE_GAMECARD)
                                {
                                    if (titleAddOnCount > 1 && (selectedAddOnIndex + 1) < titleAddOnCount)
                                    {
                                        selectedAddOnIndex++;
                                        titleSelectorStr[0] = '\0';
                                    }
                                } else
                                if (menuType == MENUTYPE_SDCARD_EMMC)
                                {
                                    if (!orphanMode)
                                    {
                                        u32 newIndex = retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppInfoIndex, true);
                                        if (newIndex != selectedAddOnIndex)
                                        {
                                            selectedAddOnIndex = newIndex;
                                            titleSelectorStr[0] = '\0';
                                        }
                                    }
                                }
                            }
                            break;
                        case 6: // Application/update to dump
                            if (uiState == stateNspAppDumpMenu)
                            {
                                if (menuType == MENUTYPE_GAMECARD)
                                {
                                    if (titleAppCount > 1 && (selectedAppIndex + 1) < titleAppCount)
                                    {
                                        selectedAppIndex++;
                                        titleSelectorStr[0] = '\0';
                                    }
                                }
                            } else {
                                if (menuType == MENUTYPE_GAMECARD)
                                {
                                    if (titlePatchCount > 1 && (selectedPatchIndex + 1) < titlePatchCount)
                                    {
                                        selectedPatchIndex++;
                                        titleSelectorStr[0] = '\0';
                                    }
                                } else
                                if (menuType == MENUTYPE_SDCARD_EMMC)
                                {
                                    if (!orphanMode)
                                    {
                                        u32 newIndex = retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppInfoIndex, false);
                                        if (newIndex != selectedPatchIndex)
                                        {
                                            selectedPatchIndex = newIndex;
                                            titleSelectorStr[0] = '\0';
                                        }
                                    }
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    
                    // Save settings to configuration file
                    saveConfig();
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else
            if (uiState == stateSdCardEmmcBatchModeMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0 && (dumpCfg.batchDumpCfg.dumpAppTitles || dumpCfg.batchDumpCfg.dumpPatchTitles || dumpCfg.batchDumpCfg.dumpAddOnTitles)) res = resultSdCardEmmcBatchDump;
                
                // Back
                if (keysDown & KEY_B) res = resultShowSdCardEmmcMenu;
                
                // Change option to false
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 1: // Dump base applications
                            dumpCfg.batchDumpCfg.dumpAppTitles = false;
                            break;
                        case 2: // Dump updates
                            dumpCfg.batchDumpCfg.dumpPatchTitles = false;
                            break;
                        case 3: // Dump DLCs
                            dumpCfg.batchDumpCfg.dumpAddOnTitles = false;
                            break;
                        case 4: // Split output dumps (FAT32 support)
                            dumpCfg.batchDumpCfg.isFat32 = false;
                            break;
                        case 5: // Remove console specific data
                            dumpCfg.batchDumpCfg.removeConsoleData = dumpCfg.batchDumpCfg.tiklessDump = false;
                            break;
                        case 6: // Generate ticket-less dumps
                            dumpCfg.batchDumpCfg.tiklessDump = false;
                            break;
                        case 7: // Change NPDM RSA key/sig in Program NCA
                            dumpCfg.batchDumpCfg.npdmAcidRsaPatch = false;
                            break;
                        case 8: // Skip already dumped titles
                            dumpCfg.batchDumpCfg.skipDumpedTitles = false;
                            break;
                        case 9: // Remember dumped titles
                            dumpCfg.batchDumpCfg.rememberDumpedTitles = false;
                            break;
                        case 10: // Source storage
                            if (dumpCfg.batchDumpCfg.batchModeSrc != BATCH_SOURCE_ALL)
                            {
                                dumpCfg.batchDumpCfg.batchModeSrc--;
                                
                                if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL)
                                {
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (titleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (titlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (titleAddOnCount > 0);
                                } else
                                if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD)
                                {
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (sdCardTitleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (sdCardTitlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (sdCardTitleAddOnCount > 0);
                                } else
                                if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC)
                                {
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (nandUserTitleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (nandUserTitlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (nandUserTitleAddOnCount > 0);
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    
                    // Save settings to configuration file
                    saveConfig();
                }
                
                // Change option to true
                if (keysDown & KEY_RIGHT)
                {
                    switch(cursor)
                    {
                        case 1: // Dump base applications
                            dumpCfg.batchDumpCfg.dumpAppTitles = true;
                            break;
                        case 2: // Dump updates
                            dumpCfg.batchDumpCfg.dumpPatchTitles = true;
                            break;
                        case 3: // Dump DLCs
                            dumpCfg.batchDumpCfg.dumpAddOnTitles = true;
                            break;
                        case 4: // Split output dumps (FAT32 support)
                            dumpCfg.batchDumpCfg.isFat32 = true;
                            break;
                        case 5: // Remove console specific data
                            dumpCfg.batchDumpCfg.removeConsoleData = true;
                            break;
                        case 6: // Generate ticket-less dumps
                            dumpCfg.batchDumpCfg.tiklessDump = true;
                            break;
                        case 7: // Change NPDM RSA key/sig in Program NCA
                            dumpCfg.batchDumpCfg.npdmAcidRsaPatch = true;
                            break;
                        case 8: // Skip already dumped titles
                            dumpCfg.batchDumpCfg.skipDumpedTitles = true;
                            break;
                        case 9: // Remember dumped titles
                            dumpCfg.batchDumpCfg.rememberDumpedTitles = true;
                            break;
                        case 10: // Source storage
                            if (dumpCfg.batchDumpCfg.batchModeSrc != BATCH_SOURCE_EMMC)
                            {
                                dumpCfg.batchDumpCfg.batchModeSrc++;
                                
                                if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL)
                                {
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (titleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (titlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (titleAddOnCount > 0);
                                } else
                                if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD)
                                {
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (sdCardTitleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (sdCardTitlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (sdCardTitleAddOnCount > 0);
                                } else
                                if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC)
                                {
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (nandUserTitleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (nandUserTitlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (nandUserTitleAddOnCount > 0);
                                }
                            }
                            break;
                        default:
                            break;
                    }
                    
                    // Save settings to configuration file
                    saveConfig();
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else
            if (uiState == stateExeFsMenu)
            {
                // Select
                if (keysDown & KEY_A)
                {
                    // Reset option to its default value
                    selectedAppIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                    
                    switch(cursor)
                    {
                        case 0:
                            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowExeFsSectionDataDumpMenu : resultDumpExeFsSectionData);
                            break;
                        case 1:
                            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowExeFsSectionBrowserMenu : resultExeFsSectionBrowserGetList);
                            break;
                        default:
                            break;
                    }
                }
                
                // Back
                if (keysDown & KEY_B)
                {
                    if (menuType == MENUTYPE_GAMECARD)
                    {
                        freeTitlesFromSdCardAndEmmc(META_DB_PATCH);
                        res = resultShowGameCardMenu;
                    } else {
                        res = resultShowSdCardEmmcTitleMenu;
                    }
                }
                
                // Go left
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 2: // Use update
                            if ((menuType == MENUTYPE_GAMECARD && titleAppCount == 1 && checkIfBaseApplicationHasPatchOrAddOn(0, false)) || (menuType == MENUTYPE_SDCARD_EMMC && checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)))
                            {
                                if (exeFsUpdateFlag)
                                {
                                    u32 appIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                                    u32 newIndex = retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, appIndex, false);
                                    
                                    if (newIndex != selectedPatchIndex)
                                    {
                                        selectedPatchIndex = newIndex;
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    } else {
                                        exeFsUpdateFlag = false;
                                    }
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go right
                if (keysDown & KEY_RIGHT)
                {
                    switch(cursor)
                    {
                        case 2: // Use update
                            if ((menuType == MENUTYPE_GAMECARD && titleAppCount == 1 && checkIfBaseApplicationHasPatchOrAddOn(0, false)) || (menuType == MENUTYPE_SDCARD_EMMC && checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)))
                            {
                                u32 appIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                                
                                if (exeFsUpdateFlag)
                                {
                                    u32 newIndex = retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, appIndex, false);
                                    if (newIndex != selectedPatchIndex)
                                    {
                                        selectedPatchIndex = newIndex;
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    }
                                } else {
                                    exeFsUpdateFlag = true;
                                    selectedPatchIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(appIndex, false);
                                    exeFsAndRomFsSelectorStr[0] = '\0';
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else
            if (uiState == stateExeFsSectionDataDumpMenu || uiState == stateExeFsSectionBrowserMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0) res = (uiState == stateExeFsSectionDataDumpMenu ? resultDumpExeFsSectionData : resultExeFsSectionBrowserGetList);
                
                // Back
                if (keysDown & KEY_B) res = resultShowExeFsMenu;
                
                // Change option to false
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 1: // Bundled application to dump/browse
                            if (menuType == MENUTYPE_GAMECARD)
                            {
                                if (selectedAppIndex > 0)
                                {
                                    selectedAppIndex--;
                                    titleSelectorStr[0] = '\0';
                                    exeFsUpdateFlag = false;
                                }
                            }
                            break;
                        case 2: // Use update
                            if (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false))
                            {
                                if (exeFsUpdateFlag)
                                {
                                    u32 newIndex = retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppIndex, false);
                                    if (newIndex != selectedPatchIndex)
                                    {
                                        selectedPatchIndex = newIndex;
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    } else {
                                        exeFsUpdateFlag = false;
                                    }
                                }
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
                        case 1: // Bundled application to dump/browse
                            if (menuType == MENUTYPE_GAMECARD)
                            {
                                if (titleAppCount > 1 && (selectedAppIndex + 1) < titleAppCount)
                                {
                                    selectedAppIndex++;
                                    titleSelectorStr[0] = '\0';
                                    exeFsUpdateFlag = false;
                                }
                            }
                            break;
                        case 2: // Use update
                            if (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false))
                            {
                                if (exeFsUpdateFlag)
                                {
                                    u32 newIndex = retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppIndex, false);
                                    if (newIndex != selectedPatchIndex)
                                    {
                                        selectedPatchIndex = newIndex;
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    }
                                } else {
                                    exeFsUpdateFlag = true;
                                    selectedPatchIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppIndex, false);
                                    exeFsAndRomFsSelectorStr[0] = '\0';
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else
            if (uiState == stateRomFsMenu)
            {
                // Select
                if (keysDown & KEY_A)
                {
                    // Reset option to its default value
                    if (!orphanMode) selectedAppIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                    
                    switch(cursor)
                    {
                        case 0:
                            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowRomFsSectionDataDumpMenu : resultDumpRomFsSectionData);
                            break;
                        case 1:
                            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowRomFsSectionBrowserMenu : resultRomFsSectionBrowserGetEntries);
                            break;
                        default:
                            break;
                    }
                }
                
                // Back
                if (keysDown & KEY_B)
                {
                    if (menuType == MENUTYPE_GAMECARD)
                    {
                        freeTitlesFromSdCardAndEmmc(META_DB_PATCH);
                        freeTitlesFromSdCardAndEmmc(META_DB_ADDON);
                        res = resultShowGameCardMenu;
                    } else {
                        res = resultShowSdCardEmmcTitleMenu;
                    }
                }
                
                // Go left
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 2: // Use update/DLC
                            if ((menuType == MENUTYPE_GAMECARD && titleAppCount == 1 && (checkIfBaseApplicationHasPatchOrAddOn(0, false) || checkIfBaseApplicationHasPatchOrAddOn(0, true))) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))
                            {
                                if (curRomFsType != ROMFS_TYPE_APP)
                                {
                                    u32 appIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                                    u32 curIndex = (curRomFsType == ROMFS_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex);
                                    u32 newIndex = retrievePreviousPatchOrAddOnIndexFromBaseApplication(curIndex, appIndex, (curRomFsType == ROMFS_TYPE_ADDON));
                                    
                                    if (newIndex != curIndex)
                                    {
                                        if (curRomFsType == ROMFS_TYPE_PATCH)
                                        {
                                            selectedPatchIndex = newIndex;
                                        } else {
                                            selectedAddOnIndex = newIndex;
                                        }
                                        
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    } else {
                                        if (curRomFsType == ROMFS_TYPE_ADDON)
                                        {
                                            if (checkIfBaseApplicationHasPatchOrAddOn(appIndex, false))
                                            {
                                                curRomFsType = ROMFS_TYPE_PATCH;
                                                selectedPatchIndex = retrieveLastPatchOrAddOnIndexFromBaseApplication(appIndex, false);
                                                exeFsAndRomFsSelectorStr[0] = '\0';
                                            } else {
                                                curRomFsType = ROMFS_TYPE_APP;
                                            }
                                        } else {
                                            curRomFsType = ROMFS_TYPE_APP;
                                        }
                                    }
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go right
                if (keysDown & KEY_RIGHT)
                {
                    switch(cursor)
                    {
                        case 2: // Use update/DLC
                            if ((menuType == MENUTYPE_GAMECARD && titleAppCount == 1 && (checkIfBaseApplicationHasPatchOrAddOn(0, false) || checkIfBaseApplicationHasPatchOrAddOn(0, true))) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))
                            {
                                u32 appIndex = (menuType == MENUTYPE_GAMECARD ? 0 : selectedAppInfoIndex);
                                
                                if (curRomFsType != ROMFS_TYPE_APP)
                                {
                                    u32 curIndex = (curRomFsType == ROMFS_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex);
                                    u32 newIndex = retrieveNextPatchOrAddOnIndexFromBaseApplication(curIndex, appIndex, (curRomFsType == ROMFS_TYPE_ADDON));
                                    
                                    if (newIndex != curIndex)
                                    {
                                        if (curRomFsType == ROMFS_TYPE_PATCH)
                                        {
                                            selectedPatchIndex = newIndex;
                                        } else {
                                            selectedAddOnIndex = newIndex;
                                        }
                                        
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    } else {
                                        if (curRomFsType == ROMFS_TYPE_PATCH)
                                        {
                                            if (checkIfBaseApplicationHasPatchOrAddOn(appIndex, true))
                                            {
                                                curRomFsType = ROMFS_TYPE_ADDON;
                                                selectedAddOnIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(appIndex, true);
                                                exeFsAndRomFsSelectorStr[0] = '\0';
                                            }
                                        }
                                    }
                                } else {
                                    if (checkIfBaseApplicationHasPatchOrAddOn(appIndex, false))
                                    {
                                        curRomFsType = ROMFS_TYPE_PATCH;
                                        selectedPatchIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(appIndex, false);
                                    } else {
                                        curRomFsType = ROMFS_TYPE_ADDON;
                                        selectedAddOnIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(appIndex, true);
                                    }
                                    
                                    exeFsAndRomFsSelectorStr[0] = '\0';
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else
            if (uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0) res = (uiState == stateRomFsSectionDataDumpMenu ? resultDumpRomFsSectionData : resultRomFsSectionBrowserGetEntries);
                
                // Back
                if (keysDown & KEY_B) res = resultShowRomFsMenu;
                
                // Change option to false
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 1: // Bundled application to dump/browse
                            if (menuType == MENUTYPE_GAMECARD)
                            {
                                if (selectedAppIndex > 0)
                                {
                                    selectedAppIndex--;
                                    titleSelectorStr[0] = '\0';
                                    curRomFsType = ROMFS_TYPE_APP;
                                }
                            }
                            break;
                        case 2: // Use update/DLC
                            if (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && (checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true)))
                            {
                                if (curRomFsType != ROMFS_TYPE_APP)
                                {
                                    u32 curIndex = (curRomFsType == ROMFS_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex);
                                    u32 newIndex = retrievePreviousPatchOrAddOnIndexFromBaseApplication(curIndex, selectedAppIndex, (curRomFsType == ROMFS_TYPE_ADDON));
                                    
                                    if (newIndex != curIndex)
                                    {
                                        if (curRomFsType == ROMFS_TYPE_PATCH)
                                        {
                                            selectedPatchIndex = newIndex;
                                        } else {
                                            selectedAddOnIndex = newIndex;
                                        }
                                        
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    } else {
                                        if (curRomFsType == ROMFS_TYPE_ADDON)
                                        {
                                            if (checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false))
                                            {
                                                curRomFsType = ROMFS_TYPE_PATCH;
                                                selectedPatchIndex = retrieveLastPatchOrAddOnIndexFromBaseApplication(selectedAppIndex, false);
                                                exeFsAndRomFsSelectorStr[0] = '\0';
                                            } else {
                                                curRomFsType = ROMFS_TYPE_APP;
                                            }
                                        } else {
                                            curRomFsType = ROMFS_TYPE_APP;
                                        }
                                    }
                                }
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
                        case 1: // Bundled application to dump/browse
                            if (menuType == MENUTYPE_GAMECARD)
                            {
                                if (titleAppCount > 1 && (selectedAppIndex + 1) < titleAppCount)
                                {
                                    selectedAppIndex++;
                                    titleSelectorStr[0] = '\0';
                                    curRomFsType = ROMFS_TYPE_APP;
                                }
                            }
                            break;
                        case 2: // Use update
                            if (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && (checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true)))
                            {
                                if (curRomFsType != ROMFS_TYPE_APP)
                                {
                                    u32 curIndex = (curRomFsType == ROMFS_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex);
                                    u32 newIndex = retrieveNextPatchOrAddOnIndexFromBaseApplication(curIndex, selectedAppIndex, (curRomFsType == ROMFS_TYPE_ADDON));
                                    
                                    if (newIndex != curIndex)
                                    {
                                        if (curRomFsType == ROMFS_TYPE_PATCH)
                                        {
                                            selectedPatchIndex = newIndex;
                                        } else {
                                            selectedAddOnIndex = newIndex;
                                        }
                                        
                                        exeFsAndRomFsSelectorStr[0] = '\0';
                                    } else {
                                        if (curRomFsType == ROMFS_TYPE_PATCH)
                                        {
                                            if (checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true))
                                            {
                                                curRomFsType = ROMFS_TYPE_ADDON;
                                                selectedAddOnIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppIndex, true);
                                                exeFsAndRomFsSelectorStr[0] = '\0';
                                            }
                                        }
                                    }
                                } else {
                                    if (checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false))
                                    {
                                        curRomFsType = ROMFS_TYPE_PATCH;
                                        selectedPatchIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppIndex, false);
                                    } else {
                                        curRomFsType = ROMFS_TYPE_ADDON;
                                        selectedAddOnIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppIndex, true);
                                    }
                                    
                                    exeFsAndRomFsSelectorStr[0] = '\0';
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else
            if (uiState == stateTicketMenu)
            {
                // Select
                if ((keysDown & KEY_A) && cursor == 0) res = resultDumpTicket;
                
                // Back
                if (keysDown & KEY_B) res = resultShowSdCardEmmcTitleMenu;
                
                // Go left
                if (keysDown & KEY_LEFT)
                {
                    switch(cursor)
                    {
                        case 1: // Remove console specific data
                            dumpCfg.tikDumpCfg.removeConsoleData = false;
                            saveConfig();
                            break;
                        case 2: // Use ticket from title
                            if (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true)))
                            {
                                if (curTikType != TICKET_TYPE_APP)
                                {
                                    u32 curIndex = (curTikType == TICKET_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex);
                                    u32 newIndex = retrievePreviousPatchOrAddOnIndexFromBaseApplication(curIndex, selectedAppInfoIndex, (curTikType == TICKET_TYPE_ADDON));
                                    
                                    if (newIndex != curIndex)
                                    {
                                        if (curTikType == TICKET_TYPE_PATCH)
                                        {
                                            selectedPatchIndex = newIndex;
                                        } else {
                                            selectedAddOnIndex = newIndex;
                                        }
                                        
                                        titleSelectorStr[0] = '\0';
                                    } else {
                                        if (curTikType == TICKET_TYPE_ADDON)
                                        {
                                            if (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false))
                                            {
                                                curTikType = TICKET_TYPE_PATCH;
                                                selectedPatchIndex = retrieveLastPatchOrAddOnIndexFromBaseApplication(selectedAppInfoIndex, false);
                                                titleSelectorStr[0] = '\0';
                                            } else {
                                                curTikType = TICKET_TYPE_APP;
                                            }
                                        } else {
                                            curTikType = TICKET_TYPE_APP;
                                            titleSelectorStr[0] = '\0';
                                        }
                                    }
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go right
                if (keysDown & KEY_RIGHT)
                {
                    switch(cursor)
                    {
                        case 1: // Remove console specific data
                            dumpCfg.tikDumpCfg.removeConsoleData = true;
                            saveConfig();
                            break;
                        case 2: // Use update/DLC
                            if (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) || checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true)))
                            {
                                if (curTikType != TICKET_TYPE_APP)
                                {
                                    u32 curIndex = (curTikType == TICKET_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex);
                                    u32 newIndex = retrieveNextPatchOrAddOnIndexFromBaseApplication(curIndex, selectedAppInfoIndex, (curTikType == TICKET_TYPE_ADDON));
                                    
                                    if (newIndex != curIndex)
                                    {
                                        if (curTikType == TICKET_TYPE_PATCH)
                                        {
                                            selectedPatchIndex = newIndex;
                                        } else {
                                            selectedAddOnIndex = newIndex;
                                        }
                                        
                                        titleSelectorStr[0] = '\0';
                                    } else {
                                        if (curTikType == TICKET_TYPE_PATCH)
                                        {
                                            if (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))
                                            {
                                                curTikType = TICKET_TYPE_ADDON;
                                                selectedAddOnIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppInfoIndex, true);
                                                titleSelectorStr[0] = '\0';
                                            }
                                        }
                                    }
                                } else {
                                    if (checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false))
                                    {
                                        curTikType = TICKET_TYPE_PATCH;
                                        selectedPatchIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppInfoIndex, false);
                                    } else {
                                        curTikType = TICKET_TYPE_ADDON;
                                        selectedAddOnIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppInfoIndex, true);
                                    }
                                    
                                    titleSelectorStr[0] = '\0';
                                }
                            }
                            break;
                        default:
                            break;
                    }
                }
                
                // Go up
                if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                {
                    scrollAmount = -1;
                    scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                }
                
                // Go down
                if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                {
                    scrollAmount = 1;
                    scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                }
            } else {
                // Select
                if (keysDown & KEY_A)
                {
                    if (uiState == stateMainMenu)
                    {
                        selectedAppInfoIndex = 0;
                        
                        switch(cursor)
                        {
                            case 0:
                                res = resultShowGameCardMenu;
                                menuType = MENUTYPE_GAMECARD;
                                break;
                            case 1:
                                if (keysFileAvailable)
                                {
                                    res = resultShowSdCardEmmcMenu;
                                    menuType = MENUTYPE_SDCARD_EMMC;
                                } else {
                                    uiStatusMsg("Keys file unavailable at \"%s\". Option disabled.", KEYS_FILE_PATH);
                                }
                                break;
                            case 2:
                                res = resultShowUpdateMenu;
                                break;
                            default:
                                break;
                        }
                    } else
                    if (uiState == stateGameCardMenu)
                    {
                        switch(cursor)
                        {
                            case 0:
                                res = resultShowXciDumpMenu;
                                break;
                            case 1:
                                if (keysFileAvailable)
                                {
                                    if (!titlePatchCount && !titleAddOnCount)
                                    {
                                        res = resultShowNspAppDumpMenu;
                                        
                                        // Reset option to its default value
                                        selectedAppIndex = 0;
                                    } else {
                                        res = resultShowNspDumpMenu;
                                    }
                                } else {
                                    uiStatusMsg("Keys file unavailable at \"%s\". Option disabled.", KEYS_FILE_PATH);
                                }
                                break;
                            case 2:
                                res = resultShowHfs0Menu;
                                break;
                            case 3:
                                if (keysFileAvailable)
                                {
                                    loadTitlesFromSdCardAndEmmc(META_DB_PATCH);
                                    
                                    res = resultShowExeFsMenu;
                                    
                                    // Reset options to their default values
                                    exeFsUpdateFlag = false;
                                    selectedPatchIndex = 0;
                                } else {
                                    uiStatusMsg("Keys file unavailable at \"%s\". Option disabled.", KEYS_FILE_PATH);
                                }
                                break;
                            case 4:
                                if (keysFileAvailable)
                                {
                                    loadTitlesFromSdCardAndEmmc(META_DB_PATCH);
                                    loadTitlesFromSdCardAndEmmc(META_DB_ADDON);
                                    
                                    res = resultShowRomFsMenu;
                                    
                                    // Reset options to their default values
                                    selectedPatchIndex = selectedAddOnIndex = 0;
                                    curRomFsType = ROMFS_TYPE_APP;
                                } else {
                                    uiStatusMsg("Keys file unavailable at \"%s\". Option disabled.", KEYS_FILE_PATH);
                                }
                                break;
                            case 5:
                                res = resultDumpGameCardCertificate;
                                break;
                            default:
                                break;
                        }
                    } else
                    if (uiState == stateNspDumpMenu)
                    {
                        // Reset options to their default values
                        selectedAppIndex = 0;
                        selectedPatchIndex = 0;
                        selectedAddOnIndex = 0;
                        
                        switch(cursor)
                        {
                            case 0:
                                res = resultShowNspAppDumpMenu;
                                if (menuType == MENUTYPE_SDCARD_EMMC) selectedAppIndex = selectedAppInfoIndex;
                                break;
                            case 1:
                                res = resultShowNspPatchDumpMenu;
                                if (menuType == MENUTYPE_SDCARD_EMMC) selectedPatchIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppInfoIndex, false);
                                break;
                            case 2:
                                res = resultShowNspAddOnDumpMenu;
                                if (menuType == MENUTYPE_SDCARD_EMMC) selectedAddOnIndex = retrieveFirstPatchOrAddOnIndexFromBaseApplication(selectedAppInfoIndex, true);
                                break;
                            default:
                                break;
                        }
                    } else
                    if (uiState == stateHfs0Menu)
                    {
                        switch(cursor)
                        {
                            case 0:
                                res = resultShowRawHfs0PartitionDumpMenu;
                                break;
                            case 1:
                                res = resultShowHfs0PartitionDataDumpMenu;
                                break;
                            case 2:
                                res = resultShowHfs0BrowserMenu;
                                break;
                            default:
                                break;
                        }
                    } else
                    if (uiState == stateRawHfs0PartitionDumpMenu)
                    {
                        // Save selected partition index
                        selectedPartitionIndex = (u32)cursor;
                        res = resultDumpRawHfs0Partition;
                    } else
                    if (uiState == stateHfs0PartitionDataDumpMenu)
                    {
                        // Save selected partition index
                        selectedPartitionIndex = (u32)cursor;
                        res = resultDumpHfs0PartitionData;
                    } else
                    if (uiState == stateHfs0BrowserMenu)
                    {
                        // Save selected partition index
                        selectedPartitionIndex = (u32)cursor;
                        res = resultHfs0BrowserGetList;
                    } else
                    if (uiState == stateHfs0Browser)
                    {
                        // Save selected file index
                        selectedFileIndex = (u32)cursor;
                        res = resultHfs0BrowserCopyFile;
                    } else
                    if (uiState == stateExeFsSectionBrowser)
                    {
                        if (menu && menuItemsCount)
                        {
                            // Save selected file index
                            selectedFileIndex = (u32)cursor;
                            res = resultExeFsSectionBrowserCopyFile;
                        }
                    } else
                    if (uiState == stateRomFsSectionBrowser)
                    {
                        if (menu && menuItemsCount)
                        {
                            // Save selected file index
                            selectedFileIndex = (u32)cursor;
                            if (strlen(curRomFsPath) <= 1) selectedFileIndex++; // Adjust index if we're at the root directory
                            res = (romFsBrowserEntries[cursor].type == ROMFS_ENTRY_DIR ? resultRomFsSectionBrowserChangeDir : resultRomFsSectionBrowserCopyFile);
                        }
                    } else
                    if (uiState == stateSdCardEmmcMenu)
                    {
                        // Save selected base application index
                        selectedAppInfoIndex = (u32)cursor;
                        res = resultShowSdCardEmmcTitleMenu;
                    } else
                    if (uiState == stateSdCardEmmcTitleMenu)
                    {
                        switch(cursor)
                        {
                            case 0:
                                if (!orphanMode)
                                {
                                    if ((!titlePatchCount || !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)) && (!titleAddOnCount || !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true)))
                                    {
                                        res = resultShowNspAppDumpMenu;
                                        selectedAppIndex = selectedAppInfoIndex;
                                    } else {
                                        res = resultShowNspDumpMenu;
                                    }
                                } else {
                                    res = (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? resultShowNspPatchDumpMenu : resultShowNspAddOnDumpMenu);
                                }
                                break;
                            case 1:
                                if (!orphanMode)
                                {
                                    res = resultShowExeFsMenu;
                                    
                                    // Reset options to their default values
                                    exeFsUpdateFlag = false;
                                    selectedPatchIndex = 0;
                                }
                                break;
                            case 2:
                                res = resultShowRomFsMenu;
                                
                                if (!orphanMode)
                                {
                                    // Reset options to their default values
                                    selectedPatchIndex = selectedAddOnIndex = 0;
                                    curRomFsType = ROMFS_TYPE_APP;
                                } else {
                                    curRomFsType = ROMFS_TYPE_ADDON;
                                }
                                
                                break;
                            case 3:
                                res = resultShowTicketMenu;
                                
                                if (!orphanMode)
                                {
                                    // Reset options to their default values
                                    selectedPatchIndex = selectedAddOnIndex = 0;
                                    curTikType = TICKET_TYPE_APP;
                                } else {
                                    curTikType = (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? TICKET_TYPE_PATCH : TICKET_TYPE_ADDON);
                                }
                                
                                break;
                            default:
                                break;
                        }
                    } else
                    if (uiState == stateSdCardEmmcOrphanPatchAddOnMenu)
                    {
                        if (menu && menuItemsCount)
                        {
                            if (orphanEntries[cursor].type == ORPHAN_ENTRY_TYPE_PATCH)
                            {
                                selectedPatchIndex = orphanEntries[cursor].index;
                            } else
                            if (orphanEntries[cursor].type == ORPHAN_ENTRY_TYPE_ADDON)
                            {
                                selectedAddOnIndex = orphanEntries[cursor].index;
                            }
                            
                            res = resultShowSdCardEmmcTitleMenu;
                        }
                    } else
                    if (uiState == stateUpdateMenu)
                    {
                        switch(cursor)
                        {
                            case 0:
                                res = resultUpdateNSWDBXml;
                                break;
                            case 1:
                                if (!updatePerformed)
                                {
                                    res = resultUpdateApplication;
                                } else {
                                    uiStatusMsg("Update already performed. Please restart the application.");
                                }
                                break;
                            default:
                                break;
                        }
                    }
                }
                
                // Back
                if (keysDown & KEY_B)
                {
                    if (uiState == stateGameCardMenu || uiState == stateSdCardEmmcMenu || uiState == stateUpdateMenu)
                    {
                        res = resultShowMainMenu;
                        menuType = MENUTYPE_MAIN;
                    } else
                    if (menuType == MENUTYPE_GAMECARD && (uiState == stateNspDumpMenu || uiState == stateHfs0Menu))
                    {
                        res = resultShowGameCardMenu;
                    } else
                    if (uiState == stateRawHfs0PartitionDumpMenu || uiState == stateHfs0PartitionDataDumpMenu || uiState == stateHfs0BrowserMenu)
                    {
                        res = resultShowHfs0Menu;
                    } else
                    if (uiState == stateHfs0Browser)
                    {
                        free(partitionHfs0Header);
                        partitionHfs0Header = NULL;
                        partitionHfs0HeaderOffset = 0;
                        partitionHfs0HeaderSize = 0;
                        partitionHfs0FileCount = 0;
                        partitionHfs0StrTableSize = 0;
                        
                        res = resultShowHfs0BrowserMenu;
                    } else
                    if (uiState == stateExeFsSectionBrowser)
                    {
                        freeExeFsContext();
                        
                        res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowExeFsSectionBrowserMenu : resultShowExeFsMenu);
                    } else
                    if (uiState == stateRomFsSectionBrowser)
                    {
                        if (strlen(curRomFsPath) > 1)
                        {
                            // Point to the parent directory entry ("..")
                            selectedFileIndex = 0;
                            res = resultRomFsSectionBrowserChangeDir;
                        } else {
                            if (romFsBrowserEntries != NULL)
                            {
                                free(romFsBrowserEntries);
                                romFsBrowserEntries = NULL;
                            }
                            
                            if (curRomFsType == ROMFS_TYPE_PATCH) freeBktrContext();
                            
                            freeRomFsContext();
                            
                            if (menuType == MENUTYPE_SDCARD_EMMC && orphanMode) generateOrphanPatchOrAddOnList();
                            
                            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowRomFsSectionBrowserMenu : resultShowRomFsMenu);
                        }
                    } else
                    if (uiState == stateSdCardEmmcTitleMenu)
                    {
                        res = (!orphanMode ? resultShowSdCardEmmcMenu : resultShowSdCardEmmcOrphanPatchAddOnMenu);
                    } else
                    if (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && uiState == stateNspDumpMenu)
                    {
                        res = resultShowSdCardEmmcTitleMenu;
                    } else
                    if (uiState == stateSdCardEmmcOrphanPatchAddOnMenu)
                    {
                        freeOrphanPatchOrAddOnList();
                        
                        res = resultShowSdCardEmmcMenu;
                        orphanMode = false;
                    }
                }
                
                // Special action #1
                if (keysDown & KEY_Y)
                {
                    if (uiState == stateSdCardEmmcMenu && ((titleAppCount && ((titlePatchCount && checkOrphanPatchOrAddOn(false)) || (titleAddOnCount && checkOrphanPatchOrAddOn(true)))) || (!titleAppCount && (titlePatchCount || titleAddOnCount))))
                    {
                        // SD/eMMC menu: Dump installed content with missing base application
                        res = resultShowSdCardEmmcOrphanPatchAddOnMenu;
                        orphanMode = true;
                    } else
                    if (uiState == stateRomFsSectionBrowser && strlen(curRomFsPath) > 1)
                    {
                        // RomFS section browser: dump current directory
                        res = resultRomFsSectionBrowserCopyDir;
                    }
                }
                
                // Special action #2
                if (keysDown & KEY_X)
                {
                    if (uiState == stateSdCardEmmcMenu && (titleAppCount || titlePatchCount || titleAddOnCount))
                    {
                        // Batch mode
                        res = resultShowSdCardEmmcBatchModeMenu;
                        
                        // Check if we're using the default configuration
                        if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && !dumpCfg.batchDumpCfg.dumpAppTitles && !dumpCfg.batchDumpCfg.dumpPatchTitles && !dumpCfg.batchDumpCfg.dumpAddOnTitles)
                        {
                            dumpCfg.batchDumpCfg.dumpAppTitles = (titleAppCount > 0);
                            dumpCfg.batchDumpCfg.dumpPatchTitles = (titlePatchCount > 0);
                            dumpCfg.batchDumpCfg.dumpAddOnTitles = (titleAddOnCount > 0);
                        }
                    }
                }
                
                if (menu && menuItemsCount)
                {
                    // Go up
                    if ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP) || (keysHeld & KEY_RSTICK_UP))
                    {
                        scrollAmount = -1;
                        scrollWithKeysDown = ((keysDown & KEY_DUP) || (keysDown & KEY_LSTICK_UP));
                    }
                    
                    if ((keysDown & KEY_DLEFT) || (keysDown & KEY_LSTICK_LEFT) || (keysHeld & KEY_RSTICK_LEFT)) scrollAmount = -5;
                    
                    // Go down
                    if ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN) || (keysHeld & KEY_RSTICK_DOWN))
                    {
                        scrollAmount = 1;
                        scrollWithKeysDown = ((keysDown & KEY_DDOWN) || (keysDown & KEY_LSTICK_DOWN));
                    }
                    
                    if ((keysDown & KEY_DRIGHT) || (keysDown & KEY_LSTICK_RIGHT) || (keysHeld & KEY_RSTICK_RIGHT)) scrollAmount = 5;
                }
            }
            
            // Calculate scroll only if the UI state hasn't been changed
            if (res == resultNone)
            {
                if (scrollAmount > 0)
                {
                    if (scrollWithKeysDown && (cursor + scrollAmount) > (menuItemsCount - 1))
                    {
                        cursor = 0;
                        scroll = 0;
                    } else {
                        for(i = 0; i < scrollAmount; i++)
                        {
                            if (cursor >= (menuItemsCount - 1)) break;
                            
                            cursor++;
                            
                            if ((cursor - scroll) >= maxElements) scroll++;
                        }
                    }
                } else
                if (scrollAmount < 0)
                {
                    if (scrollWithKeysDown && (cursor + scrollAmount) < 0)
                    {
                        cursor = (menuItemsCount - 1);
                        scroll = (menuItemsCount - maxElements);
                        if (scroll < 0) scroll = 0;
                    } else {
                        for(i = 0; i < -scrollAmount; i++)
                        {
                            if (cursor <= 0) break;
                            
                            cursor--;
                            
                            if ((cursor - scroll) < 0) scroll--;
                        }
                    }
                }
                
                // Avoid placing the cursor on the "Create directory with archive bit set" option in the XCI dump menu if "Split output dump" is disabled
                if (uiState == stateXciDumpMenu && cursor == 2 && !dumpCfg.xciDumpCfg.isFat32)
                {
                    if (scrollAmount > 0)
                    {
                        cursor++;
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor--;
                    }
                }
                
                // Avoid placing the cursor on the "Dump bundled update NSP" / "Dump installed update NSP" option in the NSP dump menu if we're dealing with a gamecard and it doesn't include any bundled updates, or if we're dealing with a SD/eMMC title without installed updates
                // Also avoid placing the cursor on the "Dump bundled DLC NSP" / "Dump installed DLC NSP" option in the NSP dump menu if we're dealing with a gamecard and it doesn't include any bundled DLCs, or if we're dealing with a SD/eMMC title without installed DLCs
                if (uiState == stateNspDumpMenu && ((cursor == 1 && (!titlePatchCount || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)))) || (cursor == 2 && (!titleAddOnCount || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))))
                {
                    if (cursor == 1)
                    {
                        if ((!titleAddOnCount || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))
                        {
                            // Just in case
                            cursor = 0;
                        } else {
                            if (scrollAmount > 0)
                            {
                                cursor = 2;
                            } else
                            if (scrollAmount < 0)
                            {
                                cursor = 0;
                            }
                        }
                    } else
                    if (cursor == 2)
                    {
                        if (!titlePatchCount || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)))
                        {
                            // Just in case
                            cursor = 0;
                        } else {
                            if (scrollAmount > 0)
                            {
                                cursor = (scrollWithKeysDown ? 0 : 1);
                            } else
                            if (scrollAmount < 0)
                            {
                                cursor = 1;
                            }
                        }
                    }
                }
                
                // Avoid placing the cursor on the "Remove console specific data" option in the NSP dump menus if we're dealing with a gamecard title
                // Also, avoid placing the cursor on the "Generate ticket-less dump" option in the NSP dump menus if we're dealing with a gamecard Application/AddOn title
                if (menuType == MENUTYPE_GAMECARD && (((uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu) && cursor == 3) || ((uiState == stateNspAppDumpMenu || uiState == stateNspAddOnDumpMenu) && cursor == 4)))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = ((uiState == stateNspPatchDumpMenu && cursor == 3) ? 4 : 5);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 2;
                    }
                }
                
                // Avoid placing the cursor on the "Generate ticket-less dump" option in the NSP dump menus if we're dealing with a SD/eMMC title and the "Remove console specific data" option is disabled
                if (menuType == MENUTYPE_SDCARD_EMMC && (uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu) && cursor == 4 && !dumpCfg.nspDumpCfg.removeConsoleData)
                {
                    if (scrollAmount > 0)
                    {
                        cursor++;
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor--;
                    }
                }
                
                // Avoid placing the cursor on the "Dump base applications", "Dump updates" and/or "Dump DLCs" options in the batch mode menu if we're dealing with a storage source that doesn't hold any title belonging to the current category
                if (uiState == stateSdCardEmmcBatchModeMenu && ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && ((!titleAppCount && cursor == 1) || (!titlePatchCount && cursor == 2) || (!titleAddOnCount && cursor == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && ((!sdCardTitleAppCount && cursor == 1) || (!sdCardTitlePatchCount && cursor == 2) || (!sdCardTitleAddOnCount && cursor == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && ((!nandUserTitleAppCount && cursor == 1) || (!nandUserTitlePatchCount && cursor == 2) || (!nandUserTitleAddOnCount && cursor == 3)))))
                {
                    if (scrollAmount > 0)
                    {
                        cursor++;
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor--;
                    }
                }
                
                // Avoid placing the cursor on the "Generate ticket-less dumps" option in the batch mode menu if the "Remove console specific data" option is disabled
                if (uiState == stateSdCardEmmcBatchModeMenu && cursor == 6 && !dumpCfg.batchDumpCfg.removeConsoleData)
                {
                    if (scrollAmount > 0)
                    {
                        cursor++;
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor--;
                    }
                }
                
                // Avoid placing the cursor on the "Source storage" option in the batch mode menu if we only have titles available in a single source storage device
                if (uiState == stateSdCardEmmcBatchModeMenu && cursor == 10 && ((!sdCardTitleAppCount && !sdCardTitlePatchCount && !sdCardTitleAddOnCount) || (!nandUserTitleAppCount && !nandUserTitlePatchCount && !nandUserTitleAddOnCount)))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 9);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor--;
                    }
                }
                
                // Avoid placing the cursor on the "Use update" option in the ExeFS menu if we're dealing with a gamecard and either its base application count is greater than 1 or it has no available patches
                // Also avoid placing the cursor on it if we're dealing with a SD/eMMC title and it has no available patches
                if (uiState == stateExeFsMenu && cursor == 2 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || !checkIfBaseApplicationHasPatchOrAddOn(0, false))) || (menuType == MENUTYPE_SDCARD_EMMC && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false))))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 0;
                    }
                }
                
                // Avoid placing the cursor on the "Use update" option in the ExeFS data dump and browser menus if we're not dealing with a gamecard, if the base application count is equal to or less than 1, or if the selected base application has no available patches
                if ((uiState == stateExeFsSectionDataDumpMenu || uiState == stateExeFsSectionBrowserMenu) && cursor == 2 && (menuType != MENUTYPE_GAMECARD || titleAppCount <= 1 || !checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false)))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 0;
                    }
                }
                
                // Avoid placing the cursor on the "Use update/DLC" option in the RomFS menu if we're dealing with a gamecard and either its base application count is greater than 1 or it has no available patches/DLCs
                // Also avoid placing the cursor on it if we're dealing with a SD/eMMC title and it has no available patches/DLCs (or if its an orphan title)
                if (uiState == stateRomFsMenu && cursor == 2 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || (!checkIfBaseApplicationHasPatchOrAddOn(0, false) && !checkIfBaseApplicationHasPatchOrAddOn(0, true)))) || (menuType == MENUTYPE_SDCARD_EMMC && (orphanMode || (!checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 0;
                    }
                }
                
                // Avoid placing the cursor on the "Use update/DLC" option in the RomFS data dump and browser menus if we're not dealing with a gamecard, if the base application count is equal to or less than 1, or if the selected base application has no available patches/DLCs
                if ((uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu) && cursor == 2 && (menuType != MENUTYPE_GAMECARD || titleAppCount <= 1 || (!checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, false) && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true))))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 0;
                    }
                }
                
                // Avoid placing the cursor on the "ExeFS options" element in the SD card / eMMC title menu if we're dealing with an orphan DLC
                // Also avoid placing the cursor on the "RomFS options" element in the SD card / eMMC title menu if we're dealing with an orphan Patch
                if (uiState == stateSdCardEmmcTitleMenu && orphanMode && ((orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_ADDON && cursor == 1) || (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH && (cursor == 1 || cursor == 2))))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_ADDON ? 2 : 3);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 0;
                    }
                }
                
                // Avoid placing the cursor on the "Use ticket from title" element in the Ticket menu if we're dealing with an orphan title
                if (uiState == stateTicketMenu && orphanMode && cursor == 2)
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 0;
                    }
                }
            }
        }
    } else
    if (uiState == stateDumpXci)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[0]);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[1], (dumpCfg.xciDumpCfg.isFat32 ? "Yes" : "No"));
        breaks++;
        
        if (dumpCfg.xciDumpCfg.isFat32)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[2], (dumpCfg.xciDumpCfg.setXciArchiveBit ? "Yes" : "No"));
            breaks++;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[3], (dumpCfg.xciDumpCfg.keepCert ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[4], (dumpCfg.xciDumpCfg.trimDump ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[5], (dumpCfg.xciDumpCfg.calcCrc ? "Yes" : "No"));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpCartridgeImage(&(dumpCfg.xciDumpCfg));
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowXciDumpMenu;
        
        dumpedContentInfoStr[0] = '\0';
    } else
    if (uiState == stateDumpNsp)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (menuType == MENUTYPE_GAMECARD ? nspDumpGameCardMenuItems[selectedNspDumpType] : nspDumpSdCardEmmcMenuItems[selectedNspDumpType]));
        breaks++;
        
        menu = (selectedNspDumpType == DUMP_APP_NSP ? nspAppDumpMenuItems : (selectedNspDumpType == DUMP_PATCH_NSP ? nspPatchDumpMenuItems : nspAddOnDumpMenuItems));
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[1], (dumpCfg.nspDumpCfg.isFat32 ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[2], (dumpCfg.nspDumpCfg.calcCrc ? "Yes" : "No"));
        breaks++;
        
        if (menuType == MENUTYPE_GAMECARD && selectedNspDumpType == DUMP_PATCH_NSP)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[4], (dumpCfg.nspDumpCfg.tiklessDump ? "Yes" : "No"));
            breaks++;
        } else
        if (menuType == MENUTYPE_SDCARD_EMMC)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[3], (dumpCfg.nspDumpCfg.removeConsoleData ? "Yes" : "No"));
            breaks++;
            
            if (dumpCfg.nspDumpCfg.removeConsoleData)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[4], (dumpCfg.nspDumpCfg.tiklessDump ? "Yes" : "No"));
                breaks++;
            }
        }
        
        if (selectedNspDumpType == DUMP_APP_NSP || selectedNspDumpType == DUMP_PATCH_NSP)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[5], (dumpCfg.nspDumpCfg.npdmAcidRsaPatch ? "Yes" : "No"));
            breaks++;
            
            if (selectedNspDumpType == DUMP_APP_NSP)
            {
                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", menu[6], titleName[selectedAppIndex], versionStr);
            } else {
                retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, menu[6], strbuf, MAX_ELEMENTS(strbuf));
            }
        } else {
            retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, menu[5], strbuf, MAX_ELEMENTS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpNintendoSubmissionPackage(selectedNspDumpType, (selectedNspDumpType == DUMP_APP_NSP ? selectedAppIndex : (selectedNspDumpType == DUMP_PATCH_NSP ? selectedPatchIndex : selectedAddOnIndex)), &(dumpCfg.nspDumpCfg), false);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        
        res = (selectedNspDumpType == DUMP_APP_NSP ? resultShowNspAppDumpMenu : (selectedNspDumpType == DUMP_PATCH_NSP ? resultShowNspPatchDumpMenu : resultShowNspAddOnDumpMenu));
        
        dumpedContentInfoStr[0] = '\0';
    } else
    if (uiState == stateSdCardEmmcBatchDump)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Batch dump");
        breaks++;
        
        menu = batchModeMenuItems;
        
        if ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && titleAppCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && sdCardTitleAppCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && nandUserTitleAppCount))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[1], (dumpCfg.batchDumpCfg.dumpAppTitles ? "Yes" : "No"));
            breaks++;
        }
        
        if ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && titlePatchCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && sdCardTitlePatchCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && nandUserTitlePatchCount))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[2], (dumpCfg.batchDumpCfg.dumpPatchTitles ? "Yes" : "No"));
            breaks++;
        }
        
        if ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && titleAddOnCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && sdCardTitleAddOnCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && nandUserTitleAddOnCount))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[3], (dumpCfg.batchDumpCfg.dumpAddOnTitles ? "Yes" : "No"));
            breaks++;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[4], (dumpCfg.batchDumpCfg.isFat32 ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[5], (dumpCfg.batchDumpCfg.removeConsoleData ? "Yes" : "No"));
        breaks++;
        
        if (dumpCfg.batchDumpCfg.removeConsoleData)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[6], (dumpCfg.batchDumpCfg.tiklessDump ? "Yes" : "No"));
            breaks++;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[7], (dumpCfg.batchDumpCfg.npdmAcidRsaPatch ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[8], (dumpCfg.batchDumpCfg.skipDumpedTitles ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[9], (dumpCfg.batchDumpCfg.rememberDumpedTitles ? "Yes" : "No"));
        breaks++;
        
        if ((sdCardTitleAppCount || sdCardTitlePatchCount || sdCardTitleAddOnCount) && (nandUserTitleAppCount || nandUserTitlePatchCount || nandUserTitleAddOnCount))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[10], (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL ? "All (SD card + eMMC)" : (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD ? "SD card" : "eMMC")));
            breaks++;
        }
        
        breaks++;
        uiRefreshDisplay();
        
        dumpNintendoSubmissionPackageBatch(&(dumpCfg.batchDumpCfg));
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        
        res = resultShowSdCardEmmcBatchModeMenu;
    } else
    if (uiState == stateDumpRawHfs0Partition)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Raw %s", (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0PartitionDumpType1MenuItems[selectedPartitionIndex] : hfs0PartitionDumpType2MenuItems[selectedPartitionIndex]));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpRawHfs0Partition(selectedPartitionIndex, true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowRawHfs0PartitionDumpMenu;
    } else
    if (uiState == stateDumpHfs0PartitionData)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Data %s", (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0PartitionDumpType1MenuItems[selectedPartitionIndex] : hfs0PartitionDumpType2MenuItems[selectedPartitionIndex]));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpHfs0PartitionData(selectedPartitionIndex, true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowHfs0PartitionDataDumpMenu;
    } else
    if (uiState == stateHfs0BrowserGetList)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (hfs0_partition_cnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0BrowserType1MenuItems[selectedPartitionIndex] : hfs0BrowserType2MenuItems[selectedPartitionIndex]));
        breaks += 2;
        
        uiPleaseWait(0);
        breaks += 2;
        
        if (getHfs0FileList(selectedPartitionIndex))
        {
            res = resultShowHfs0Browser;
        } else {
            waitForButtonPress();
            res = resultShowHfs0BrowserMenu;
        }
    } else
    if (uiState == stateHfs0BrowserCopyFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual File Dump: %s (HFS0 partition %u [%s])", filenames[selectedFileIndex], selectedPartitionIndex, GAMECARD_PARTITION_NAME(hfs0_partition_cnt, selectedPartitionIndex));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpFileFromHfs0Partition(selectedPartitionIndex, selectedFileIndex, filenames[selectedFileIndex], true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowHfs0Browser;
    } else
    if (uiState == stateDumpExeFsSectionData)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[0]);
        breaks++;
        
        if (!exeFsUpdateFlag)
        {
            convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
            snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", exeFsSectionDumpMenuItems[1], titleName[selectedAppIndex], versionStr);
        } else {
            retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update to dump: ", strbuf, MAX_ELEMENTS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpExeFsSectionData((!exeFsUpdateFlag ? selectedAppIndex : selectedPatchIndex), exeFsUpdateFlag, true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        
        res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowExeFsSectionDataDumpMenu : resultShowExeFsMenu);
    } else
    if (uiState == stateExeFsSectionBrowserGetList)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[1]);
        breaks++;
        
        if (!exeFsUpdateFlag)
        {
            convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
            snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", exeFsSectionBrowserMenuItems[1], titleName[selectedAppIndex], versionStr);
        } else {
            retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update to browse: ", strbuf, MAX_ELEMENTS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiPleaseWait(0);
        breaks += 2;
        
        bool exefs_fail = false;
        
        if (readNcaExeFsSection((!exeFsUpdateFlag ? selectedAppIndex : selectedPatchIndex), exeFsUpdateFlag))
        {
            if (getExeFsFileList())
            {
                res = resultShowExeFsSectionBrowser;
            } else {
                freeExeFsContext();
                exefs_fail = true;
            }
        } else {
            exefs_fail = true;
        }
        
        if (exefs_fail)
        {
            breaks += 2;
            waitForButtonPress();
            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowExeFsSectionBrowserMenu : resultShowExeFsMenu);
        }
    } else
    if (uiState == stateExeFsSectionBrowserCopyFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual File Dump: %s (ExeFS)", filenames[selectedFileIndex]);
        breaks++;
        
        if (!exeFsUpdateFlag)
        {
            convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
            snprintf(strbuf, MAX_ELEMENTS(strbuf), "Base application: %s v%s", titleName[selectedAppIndex], versionStr);
        } else {
            retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update: ", strbuf, MAX_ELEMENTS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpFileFromExeFsSection((!exeFsUpdateFlag ? selectedAppIndex : selectedPatchIndex), selectedFileIndex, exeFsUpdateFlag, true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowExeFsSectionBrowser;
    } else
    if (uiState == stateDumpRomFsSectionData)
    {
        u32 curIndex = 0;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[0]);
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", romFsSectionDumpMenuItems[1], titleName[selectedAppIndex], versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update to dump: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, "DLC to dump: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedAddOnIndex;
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpRomFsSectionData(curIndex, curRomFsType, true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        
        res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowRomFsSectionDataDumpMenu : resultShowRomFsMenu);
    } else
    if (uiState == stateRomFsSectionBrowserGetEntries)
    {
        u32 curIndex = 0;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[1]);
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", romFsSectionBrowserMenuItems[1], titleName[selectedAppIndex], versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update to browse: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, "DLC to browse: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedAddOnIndex;
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiPleaseWait(0);
        breaks += 2;
        
        bool romfs_fail = false;
        
        if (readNcaRomFsSection(curIndex, curRomFsType))
        {
            if (getRomFsFileList(0, (curRomFsType == ROMFS_TYPE_PATCH)))
            {
                res = resultShowRomFsSectionBrowser;
            } else {
                if (curRomFsType == ROMFS_TYPE_PATCH) freeBktrContext();
                freeRomFsContext();
                romfs_fail = true;
            }
        } else {
            romfs_fail = true;
        }
        
        if (romfs_fail)
        {
            breaks += 2;
            waitForButtonPress();
            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowRomFsSectionBrowserMenu : resultShowRomFsMenu);
        }
    } else
    if (uiState == stateRomFsSectionBrowserChangeDir)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[1]);
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s v%s", romFsSectionBrowserMenuItems[1], titleName[selectedAppIndex], versionStr);
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update to browse: ", strbuf, MAX_ELEMENTS(strbuf));
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, "DLC to browse: ", strbuf, MAX_ELEMENTS(strbuf));
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        bool romfs_fail = false;
        
        if (romFsBrowserEntries[selectedFileIndex].type == ROMFS_ENTRY_DIR)
        {
            if (getRomFsFileList(romFsBrowserEntries[selectedFileIndex].offset, (curRomFsType == ROMFS_TYPE_PATCH)))
            {
                res = resultShowRomFsSectionBrowser;
            } else {
                romfs_fail = true;
            }
        } else {
            // Unexpected condition
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Error: the selected entry is not a directory!");
            romfs_fail = true;
        }
        
        if (romfs_fail)
        {
            if (romFsBrowserEntries != NULL)
            {
                free(romFsBrowserEntries);
                romFsBrowserEntries = NULL;
            }
            
            if (curRomFsType == ROMFS_TYPE_PATCH) freeBktrContext();
            
            freeRomFsContext();
            
            breaks += 2;
            waitForButtonPress();
            res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowRomFsSectionBrowserMenu : resultShowRomFsMenu);
        }
    } else
    if (uiState == stateRomFsSectionBrowserCopyFile)
    {
        u32 curIndex = 0;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual File Dump: %s (RomFS)", filenames[selectedFileIndex]);
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "Base application: %s v%s", titleName[selectedAppIndex], versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, "DLC: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedAddOnIndex;
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        if (romFsBrowserEntries[selectedFileIndex].type == ROMFS_ENTRY_FILE)
        {
            dumpFileFromRomFsSection(curIndex, romFsBrowserEntries[selectedFileIndex].offset, curRomFsType, true);
        } else {
            // Unexpected condition
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Error: the selected entry is not a file!");
        }
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowRomFsSectionBrowser;
    } else
    if (uiState == stateRomFsSectionBrowserCopyDir)
    {
        u32 curIndex = 0;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual Directory Dump: romfs:%s (RomFS)", curRomFsPath);
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                convertTitleVersionToDecimal(titleAppVersion[selectedAppIndex], versionStr, MAX_ELEMENTS(versionStr));
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "Base application: %s v%s", titleName[selectedAppIndex], versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, "Update: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, "DLC: ", strbuf, MAX_ELEMENTS(strbuf));
                curIndex = selectedAddOnIndex;
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpCurrentDirFromRomFsSection(curIndex, curRomFsType, true);
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowRomFsSectionBrowser;
    } else
    if (uiState == stateDumpGameCardCertificate)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[4]);
        breaks += 2;
        
        dumpGameCardCertificate();
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowGameCardMenu;
    } else
    if (uiState == stateDumpTicket)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Dump ticket");
        breaks++;
        
        menu = ticketMenuItems;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[1], (dumpCfg.tikDumpCfg.removeConsoleData ? "Yes" : "No"));
        breaks++;
        
        switch(curTikType)
        {
            case TICKET_TYPE_APP:
                convertTitleVersionToDecimal(titleAppVersion[selectedAppInfoIndex], versionStr, MAX_ELEMENTS(versionStr));
                snprintf(strbuf, MAX_ELEMENTS(strbuf), "%s%s | %016lX v%s (BASE)", menu[2], titleName[selectedAppInfoIndex], titleAppTitleID[selectedAppInfoIndex], versionStr);
                break;
            case TICKET_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(titlePatchTitleID[selectedPatchIndex], titlePatchVersion[selectedPatchIndex], false, true, menu[2], strbuf, MAX_ELEMENTS(strbuf));
                strcat(strbuf, " (UPD)");
                break;
            case TICKET_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(titleAddOnTitleID[selectedAddOnIndex], titleAddOnVersion[selectedAddOnIndex], true, true, menu[2], strbuf, MAX_ELEMENTS(strbuf));
                strcat(strbuf, " (DLC)");
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        u32 titleIndex = (curTikType == TICKET_TYPE_APP ? selectedAppInfoIndex : (curTikType == TICKET_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex));
        
        dumpTicketFromTitle(titleIndex, curTikType, &(dumpCfg.tikDumpCfg));
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowTicketMenu;
    } else
    if (uiState == stateUpdateNSWDBXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, updateMenuItems[0]);
        breaks += 2;
        
        updateNSWDBXml();
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowUpdateMenu;
    } else
    if (uiState == stateUpdateApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, updateMenuItems[1]);
        breaks += 2;
        
        updatePerformed = updateApplication();
        
        waitForButtonPress();
        
        uiUpdateFreeSpace();
        res = resultShowUpdateMenu;
    }
    
    return res;
}
