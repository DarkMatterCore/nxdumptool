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

extern gamecard_ctx_t gameCardInfo;

extern u32 titleAppCount, titlePatchCount, titleAddOnCount;
extern u32 sdCardTitleAppCount, sdCardTitlePatchCount, sdCardTitleAddOnCount;
extern u32 emmcTitleAppCount, emmcTitlePatchCount, emmcTitleAddOnCount;

extern base_app_ctx_t *baseAppEntries;
extern patch_addon_ctx_t *patchEntries, *addOnEntries;

extern char **filenameBuffer;
extern int filenameCount;

extern char curRomFsPath[NAME_BUF_LEN];
extern romfs_browser_entry *romFsBrowserEntries;

extern browser_entry_size_info *hfs0ExeFsEntriesSizes;

extern orphan_patch_addon_entry *orphanEntries;

extern char strbuf[NAME_BUF_LEN];

extern char cfwDirStr[32];

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

int titleListCursor = 0, titleListScroll = 0;
int orphanListCursor = 0, orphanListScroll = 0;
int browserCursor = 0, browserScroll = 0;

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

extern u64 freeSpace;
extern char freeSpaceStr[32];

static UIState uiState;

static bool fb_init = false, romfs_init = false, ft_lib_init = false, ft_faces_init[PlSharedFontType_Total];

static const char *dirNormalIconPath = "romfs:/browser/dir_normal.jpg";
static u8 *dirNormalIconBuf = NULL;

static const char *dirHighlightIconPath = "romfs:/browser/dir_highlight.jpg";
static u8 *dirHighlightIconBuf = NULL;

static const char *fileNormalIconPath = "romfs:/browser/file_normal.jpg";
u8 *fileNormalIconBuf = NULL;

static const char *fileHighlightIconPath = "romfs:/browser/file_highlight.jpg";
u8 *fileHighlightIconBuf = NULL;

static const char *enabledNormalIconPath = "romfs:/browser/enabled_normal.jpg";
u8 *enabledNormalIconBuf = NULL;

static const char *enabledHighlightIconPath = "romfs:/browser/enabled_highlight.jpg";
u8 *enabledHighlightIconBuf = NULL;

static const char *disabledNormalIconPath = "romfs:/browser/disabled_normal.jpg";
u8 *disabledNormalIconBuf = NULL;

static const char *disabledHighlightIconPath = "romfs:/browser/disabled_highlight.jpg";
u8 *disabledHighlightIconBuf = NULL;

static const char *appHeadline = "NXDumpTool v" APP_VERSION ". Built on " __DATE__ " - " __TIME__ ".\nMade by DarkMatterCore.\n\n";
static const char *appControlsCommon = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsGameCardMultiApp = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_L " / " NINTENDO_FONT_R " / " NINTENDO_FONT_ZL " / " NINTENDO_FONT_ZR " ] Show info from another base application | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsNoContent = "[ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsSdCardEmmcFull = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_X " ] Batch mode | [ " NINTENDO_FONT_Y " ] Dump installed content with missing base application | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsSdCardEmmcNoApp = "[ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_X " ] Batch mode | [ " NINTENDO_FONT_Y " ] Dump installed content with missing base application | [ " NINTENDO_FONT_PLUS " ] Exit";
static const char *appControlsRomFs = "[ " NINTENDO_FONT_DPAD " / " NINTENDO_FONT_LSTICK " / " NINTENDO_FONT_RSTICK " ] Move | [ " NINTENDO_FONT_A " ] Select | [ " NINTENDO_FONT_B " ] Back | [ " NINTENDO_FONT_Y " ] Dump current directory | [ " NINTENDO_FONT_PLUS " ] Exit";

static const char *mainMenuItems[] = { "Dump gamecard content", "Dump installed SD card / eMMC content", "Update options" };
static const char *gameCardMenuItems[] = { "NX Card Image (XCI) dump", "Nintendo Submission Package (NSP) dump", "HFS0 options", "ExeFS options", "RomFS options", "Dump gamecard certificate" };
static const char *xciDumpMenuItems[] = { "Start XCI dump process", "Split output dump (FAT32 support): ", "Create directory with archive bit set: ", "Keep certificate: ", "Trim output dump: ", "CRC32 checksum calculation + dump verification: ", "Dump verification method: ", "Output naming scheme: " };
static const char *nspDumpGameCardMenuItems[] = { "Dump base application NSP", "Dump bundled update NSP", "Dump bundled DLC NSP" };
static const char *nspDumpSdCardEmmcMenuItems[] = { "Dump base application NSP", "Dump installed update NSP", "Dump installed DLC NSP" };
static const char *nspAppDumpMenuItems[] = { "Start NSP dump process", "Split output dump (FAT32 support): ", "Verify dump using No-Intro database: ", "Remove console specific data: ", "Generate ticket-less dump: ", "Change NPDM RSA key/sig in Program NCA: ", "Base application to dump: ", "Output naming scheme: " };
static const char *nspPatchDumpMenuItems[] = { "Start NSP dump process", "Split output dump (FAT32 support): ", "Verify dump using No-Intro database: ", "Remove console specific data: ", "Generate ticket-less dump: ", "Change NPDM RSA key/sig in Program NCA: ", "Dump delta fragments: ", "Update to dump: ", "Output naming scheme: " };
static const char *nspAddOnDumpMenuItems[] = { "Start NSP dump process", "Split output dump (FAT32 support): ", "Verify dump using No-Intro database: ", "Remove console specific data: ", "Generate ticket-less dump: ", "DLC to dump: ", "Output naming scheme: " };
static const char *hfs0MenuItems[] = { "Raw HFS0 partition dump", "HFS0 partition data dump", "Browse HFS0 partitions" };
static const char *hfs0PartitionDumpType1MenuItems[] = { "Dump HFS0 partition 0 (Update)", "Dump HFS0 partition 1 (Normal)", "Dump HFS0 partition 2 (Secure)" };
static const char *hfs0PartitionDumpType2MenuItems[] = { "Dump HFS0 partition 0 (Update)", "Dump HFS0 partition 1 (Logo)", "Dump HFS0 partition 2 (Normal)", "Dump HFS0 partition 3 (Secure)" };
static const char *hfs0BrowserType1MenuItems[] = { "Browse HFS0 partition 0 (Update)", "Browse HFS0 partition 1 (Normal)", "Browse HFS0 partition 2 (Secure)" };
static const char *hfs0BrowserType2MenuItems[] = { "Browse HFS0 partition 0 (Update)", "Browse HFS0 partition 1 (Logo)", "Browse HFS0 partition 2 (Normal)", "Browse HFS0 partition 3 (Secure)" };
static const char *exeFsMenuItems[] = { "ExeFS section data dump", "Browse ExeFS section", "Split files bigger than 4 GiB (FAT32 support): ", "Save data to CFW directory (LayeredFS): ", "Use update: " };
static const char *exeFsSectionDumpMenuItems[] = { "Start ExeFS data dump process", "Base application to dump: ", "Use update: " };
static const char *exeFsSectionBrowserMenuItems[] = { "Browse ExeFS section", "Base application to browse: ", "Use update: " };
static const char *romFsMenuItems[] = { "RomFS section data dump", "Browse RomFS section", "Split files bigger than 4 GiB (FAT32 support): ", "Save data to CFW directory (LayeredFS): ", "Use update/DLC: " };
static const char *romFsSectionDumpMenuItems[] = { "Start RomFS data dump process", "Base application to dump: ", "Use update/DLC: " };
static const char *romFsSectionBrowserMenuItems[] = { "Browse RomFS section", "Base application to browse: ", "Use update/DLC: " };
static const char *sdCardEmmcMenuItems[] = { "Nintendo Submission Package (NSP) dump", "ExeFS options", "RomFS options", "Ticket options" };
static const char *batchModeMenuItems[] = { "Start batch dump process", "Dump base applications: ", "Dump updates: ", "Dump DLCs: ", "Split output dumps (FAT32 support): ", "Remove console specific data: ", "Generate ticket-less dumps: ", "Change NPDM RSA key/sig in Program NCA: ", "Dump delta fragments from updates: ", "Skip already dumped titles: ", "Remember dumped titles: ", "Halt dump process on errors: ", "Output naming scheme: ", "Source storage: " };
static const char *ticketMenuItems[] = { "Start ticket dump", "Remove console specific data: ", "Use ticket from title: " };
static const char *updateMenuItems[] = { "Update NSWDB.COM XML database", "Update application" };

static const char *xciChecksumLookupMethods[] = { "NSWDB.COM XML database (offline)", "No-Intro database lookup (online)" };

static const char *xciNamingSchemes[] = { "TitleName v[TitleVersion] ([TitleID])", "TitleName [[TitleID]][v[TitleVersion]]" };
static const char *nspNamingSchemes[] = { "TitleName v[TitleVersion] ([TitleID]) ([TitleType])", "TitleName [[TitleID]][v[TitleVersion]][[TitleType]]" };

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
    
    int lx, ly;
    u32 framex, framey;
    
    for (ly = 0; ly < height; ly++)
    {
        for (lx = 0; lx < width; lx++)
        {
            framex = (u32)(x + lx);
            framey = (u32)(y + ly);
            
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
    
    int lx, ly;
    u32 framex, framey, pos;
    
    for (ly = 0; ly < height; ly++)
    {
        for (lx = 0; lx < width; lx++)
        {
            framex = (u32)(x + lx);
            framey = (u32)(y + ly);
            
            pos = (u32)(((ly * width) + lx) * 3);
            
            framebuf[(framey * framebuf_width) + framex] = RGBA8_MAXALPHA(icon[pos], icon[pos + 1], icon[pos + 2]);
        }
    }
}

bool uiLoadJpgFromMem(u8 *rawJpg, size_t rawJpgSize, int expectedWidth, int expectedHeight, int desiredWidth, int desiredHeight, u8 **outBuf)
{
    if (!rawJpg || !rawJpgSize || !expectedWidth || !expectedHeight || !desiredWidth || !desiredHeight || !outBuf)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid parameters to process JPG image buffer!", __func__);
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
    if (!_jpegDecompressor)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: tjInitDecompress failed!", __func__);
        return success;
    }
    
    ret = tjDecompressHeader2(_jpegDecompressor, rawJpg, rawJpgSize, &w, &h, &samp);
    if (ret == -1)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: tjDecompressHeader2 failed! (%d)", __func__, ret);
        goto out;
    }
    
    if (w != expectedWidth || h != expectedHeight)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid image width/height!", __func__);
        goto out;
    }
    
    scalingFactors = tjGetScalingFactors(&numScalingFactors);
    if (!scalingFactors)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: unable to retrieve scaling factors!", __func__);
        goto out;
    }
    
    for(i = 0; i < numScalingFactors; i++)
    {
        if (TJSCALED(expectedWidth, scalingFactors[i]) == desiredWidth && TJSCALED(expectedHeight, scalingFactors[i]) == desiredHeight)
        {
            foundScalingFactor = true;
            break;
        }
    }
    
    if (!foundScalingFactor)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: unable to find a valid scaling factor!", __func__);
        goto out;
    }
    
    pitch = TJPAD(desiredWidth * tjPixelSize[TJPF_RGB]);
    
    jpgScaledBuf = malloc(pitch * desiredHeight);
    if (!jpgScaledBuf)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: unable to allocate memory for the scaled RGB image output!", __func__);
        goto out;
    }
    
    ret = tjDecompress2(_jpegDecompressor, rawJpg, rawJpgSize, jpgScaledBuf, desiredWidth, 0, desiredHeight, TJPF_RGB, TJFLAG_ACCURATEDCT);
    if (ret == -1)
    {
        free(jpgScaledBuf);
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: tjDecompress2 failed! (%d)", __func__, ret);
        goto out;
    }
    
    *outBuf = jpgScaledBuf;
    success = true;
    
out:
    tjDestroy(_jpegDecompressor);
    
    return success;
}

bool uiLoadJpgFromFile(const char *filename, int expectedWidth, int expectedHeight, int desiredWidth, int desiredHeight, u8 **outBuf)
{
    if (!filename || !desiredWidth || !desiredHeight || !outBuf)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: invalid parameters to process JPG image file!", __func__);
        return false;
    }
    
    u8 *buf = NULL;
    FILE *fp = NULL;
    size_t filesize = 0, read = 0;
    
    fp = fopen(filename, "rb");
    if (!fp)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: failed to open file \"%s\"!", __func__, filename);
        return false;
    }
    
    fseek(fp, 0, SEEK_END);
    filesize = ftell(fp);
    rewind(fp);
    
    if (!filesize)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: file \"%s\" is empty!", __func__, filename);
        fclose(fp);
        return false;
    }
    
    buf = malloc(filesize);
    if (!buf)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: error allocating memory for image \"%s\"!", __func__, filename);
        fclose(fp);
        return false;
    }
    
    read = fread(buf, 1, filesize, fp);
    fclose(fp);
    
    if (read != filesize)
    {
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s: error reading image \"%s\"!", __func__, filename);
        free(buf);
        return false;
    }
    
    bool ret = uiLoadJpgFromMem(buf, filesize, expectedWidth, expectedHeight, desiredWidth, desiredHeight, outBuf);
    
    free(buf);
    
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
    vsnprintf(string, MAX_CHARACTERS(string), fmt, args);
    va_end(args);
    
    u32 tmpx = (x < 8 ? 8 : x);
    u32 tmpy = (font_height + (y < 8 ? 8 : y));
    
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
        
        if ((tmpx + (sharedFontsFaces[j]->glyph->advance.x >> 6)) > (FB_WIDTH - 8))
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
    vsnprintf(string, MAX_CHARACTERS(string), fmt, args);
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
            break;
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
    statusMessageFadeout = 2500;
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(statusMessage, MAX_CHARACTERS(statusMessage), fmt, args);
    va_end(args);
}

void uiUpdateStatusMsg()
{
	if (!strlen(statusMessage) || !statusMessageFadeout) return;
	
    uiFill(0, FB_HEIGHT - (font_height * 2), FB_WIDTH, font_height * 2, BG_COLOR_RGB);
    
    if ((statusMessageFadeout - 4) > bgColors[0])
    {
        int fadeout = (statusMessageFadeout > 255 ? 255 : statusMessageFadeout);
        uiDrawString(STRING_X_POS, FB_HEIGHT - (font_height * 2), fadeout, fadeout, fadeout, statusMessage);
        statusMessageFadeout -= 4;
    } else {
        statusMessageFadeout = 0;
    }
}

void uiClearStatusMsg()
{
    statusMessageFadeout = 0;
    statusMessage[0] = '\0';
}

void uiPleaseWait(u8 wait)
{
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Please wait...");
    uiRefreshDisplay();
    if (wait) delay(wait);
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
    if (x < 8 || x > OPTIONS_X_END_POS || y < 8 || y > (FB_HEIGHT - 8) || endPosition < OPTIONS_X_END_POS || endPosition > (FB_WIDTH - 8) || !fmt || !*fmt) return;
    
    int xpos = x;
    char option[NAME_BUF_LEN] = {'\0'};
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(option, MAX_CHARACTERS(option), fmt, args);
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
    if (!str || !strlen(str) || x < 8 || x > OPTIONS_X_END_POS || y < 8 || y > (FB_HEIGHT - 8) || endPosition < OPTIONS_X_END_POS || endPosition > (FB_WIDTH - 8)) return;
    
    int xpos = x;
    char *option = str;
    
    u32 optionStrWidth = uiGetStrWidth(option);
    u32 limit = (u32)(endPosition - xpos - (font_height * 2));
    
    // Check if we're dealing with a long title selector string
    if (optionStrWidth >= limit)
    {
        while(optionStrWidth >= limit)
        {
            option++;
            optionStrWidth = uiGetStrWidth(option);
        }
        
        option[0] = option[1] = option[2] = '.';
        
        memmove(str, option, strlen(option));
        
        str[strlen(option)] = '\0';
    }
}

bool uiInit()
{
    Result result = 0;
    FT_Error ret = 0;
    
    u32 i;
    bool success = false;
    char tmp[256] = {'\0'};
    
    /* Set initial UI state */
    uiState = stateMainMenu;
    menuType = MENUTYPE_MAIN;
    cursor = 0;
    scroll = 0;
    
    /* Clear FreeType init flags */
    memset(ft_faces_init, 0, PlSharedFontType_Total);
    
    /* Retrieve shared fonts */
    for(i = 0; i < PlSharedFontType_Total; i++)
    {
        result = plGetSharedFontByType(&sharedFonts[i], i);
        if (R_FAILED(result)) break;
    }
    
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: plGetSharedFontByType() failed to retrieve shared font #%u! (0x%08X)", __func__, i, result);
        goto out;
    }
    
    /* Initialize FreeType */
    ret = FT_Init_FreeType(&library);
    if (ret)
    {
        consoleErrorScreen("%s: FT_Init_FreeType() failed! (%d)", __func__, ret);
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
        consoleErrorScreen("%s: FT_New_Memory_Face() failed to create memory face for shared font #%u! (%d)", __func__, i, ret);
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
        consoleErrorScreen("%s: FT_Set_Char_Size() failed to set character size for shared font #%u! (%d)", __func__, i, ret);
        goto out;
    }
    
    /* Store font height */
    font_height = (sharedFontsFaces[0]->size->metrics.height / 64);
    
    /* Mount Application's RomFS */
    result = romfsInit();
    if (R_FAILED(result))
    {
        consoleErrorScreen("%s: romfsInit() failed! (0x%08X)", __func__, result);
        goto out;
    }
    
    romfs_init = true;
    
    if (!uiLoadJpgFromFile(dirNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &dirNormalIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load directory icon (normal)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(dirHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &dirHighlightIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load directory icon (highlighted)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(fileNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &fileNormalIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load file icon (normal)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(fileHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &fileHighlightIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load file icon (highlighted)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(enabledNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &enabledNormalIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load enabled icon (normal)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(enabledHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &enabledHighlightIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load enabled icon (highlighted)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(disabledNormalIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &disabledNormalIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load disabled icon (normal)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    if (!uiLoadJpgFromFile(disabledHighlightIconPath, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, &disabledHighlightIconBuf))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to load disabled icon (highlighted)!", __func__);
        strcat(strbuf, tmp);
        consoleErrorScreen(strbuf);
        goto out;
    }
    
    /* Unmount Application's RomFS */
    romfsExit();
    romfs_init = false;
    
    /* Create framebuffer */
    framebufferCreate(&fb, nwindowGetDefault(), FB_WIDTH, FB_HEIGHT, PIXEL_FORMAT_RGBA_8888, 2);
    framebufferMakeLinear(&fb);
    fb_init = true;
    
    /* Clear screen */
    uiClearScreen();
    
    /* Set output status */
    success = true;
    
out:
    return success;
}

void uiDeinit()
{
    /* Free framebuffer object */
    if (fb_init) framebufferClose(&fb);
    
    /* Free enabled/disabled icons (batch mode summary list) */
    if (disabledHighlightIconBuf) free(disabledHighlightIconBuf);
    if (disabledNormalIconBuf) free(disabledNormalIconBuf);
    if (enabledHighlightIconBuf) free(enabledHighlightIconBuf);
    if (enabledNormalIconBuf) free(enabledNormalIconBuf);
    
    /* Free directory/file icons */
    if (fileHighlightIconBuf) free(fileHighlightIconBuf);
    if (fileNormalIconBuf) free(fileNormalIconBuf);
    if (dirHighlightIconBuf) free(dirHighlightIconBuf);
    if (dirNormalIconBuf) free(dirNormalIconBuf);
    
    /* Unmount Application's RomFS */
    if (romfs_init) romfsExit();
    
    /* Free FreeType resources */
    for(u32 i = 0; i < PlSharedFontType_Total; i++)
    {
        if (ft_faces_init[i]) FT_Done_Face(sharedFontsFaces[i]);
    }
    
    if (ft_lib_init) FT_Done_FreeType(library);
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
    } else
    if (uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser || uiState == stateRomFsSectionBrowser)
    {
        if ((uiState == stateHfs0Browser && state != stateHfs0BrowserMenu) || (uiState == stateExeFsSectionBrowser && state != stateExeFsSectionBrowserMenu && state != stateExeFsMenu) || (uiState == stateRomFsSectionBrowser && state != stateRomFsSectionBrowserMenu && state != stateRomFsMenu && state != stateRomFsSectionBrowserChangeDir))
        {
            // Store current cursor/scroll values
            browserCursor = cursor;
            browserScroll = scroll;
        } else {
            // Reset browser cursor/scroll values
            browserCursor = 0;
            browserScroll = 0;
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
    } else
    if (uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser || uiState == stateRomFsSectionBrowser)
    {
        // Override cursor/scroll values
        cursor = browserCursor;
        scroll = browserScroll;
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
    
    u64 keysDown = 0, keysHeld = 0;
    
    int scrollAmount = 0;
    bool scrollWithKeysDown = false;
    
    u32 patch, addon, xpos, ypos, startYPos;
    
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, (!gameCardInfo.isInserted ? appControlsNoContent : (titleAppCount > 1 ? appControlsGameCardMultiApp : appControlsCommon)));
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
                            if (uiState == stateSdCardEmmcMenu && (calculateOrphanPatchOrAddOnCount(false) || calculateOrphanPatchOrAddOnCount(true)))
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
    }
    
    if (uiState != stateSdCardEmmcBatchDump)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Free SD card space: %s (%lu bytes).", freeSpaceStr, freeSpace);
        breaks += 2;
    }
    
    if (menuType == MENUTYPE_GAMECARD)
    {
        if (!gameCardInfo.isInserted || !gameCardInfo.rootHfs0Header || (gameCardInfo.hfs0PartitionCnt != GAMECARD_TYPE1_PARTITION_CNT && gameCardInfo.hfs0PartitionCnt != GAMECARD_TYPE2_PARTITION_CNT) || !titleAppCount || !baseAppEntries)
        {
            if (gameCardInfo.isInserted)
            {
                if (gameCardInfo.rootHfs0Header)
                {
                    if (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT || gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE2_PARTITION_CNT)
                    {
                        forcedXciDump = true;
                        
                        if (titleAppCount)
                        {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to retrieve title entries from the inserted gamecard!");
                        } else {
                            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: gamecard application count is zero!");
                        }
                    } else {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unknown root HFS0 header partition count! (%u)", gameCardInfo.hfs0PartitionCnt);
                    }
                } else {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to get root HFS0 header data!");
                }
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Gamecard is not inserted!");
            }
            
            breaks += 2;
            
            if (gameCardInfo.isInserted)
            {
                if (forcedXciDump)
                {
                    if (strlen(gameCardInfo.updateVersionStr))
                    {
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Bundled FW Update: %s", gameCardInfo.updateVersionStr);
                        breaks++;
                        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "In order to be able to parse any kind of metadata from this gamecard and/or dump its contents to NSPs,\nmake sure your console is at least on this FW version.");
                        breaks += 2;
                    }
                    
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Press " NINTENDO_FONT_Y " to dump the gamecard to \"gamecard.xci\".");
                } else {
                    if (!gameCardInfo.rootHfs0Header) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "Are you using \"nogc\" spoofing in your CFW? If so, please consider this option disables all gamecard I/O.");
                }
            }
            
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            res = resultShowGameCardMenu;
            
            hidScanInput();
            keysDown = hidKeysAllDown(CONTROLLER_P1_AUTO);
            
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
                memset(&xciDumpCfg, 0, sizeof(xciOptions));
                
                xciDumpCfg.isFat32 = true;
                xciDumpCfg.keepCert = true;
                
                dumpNXCardImage(&xciDumpCfg);
                
                waitForButtonPress();
                
                updateFreeSpace();
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
            keysDown = hidKeysAllDown(CONTROLLER_P1_AUTO);
            
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
            /* Print application info */
            xpos = STRING_X_POS;
            ypos = STRING_Y_POS(breaks);
            startYPos = ypos;
            
            /* Draw icon */
            if (baseAppEntries[selectedAppInfoIndex].icon != NULL)
            {
                uiDrawIcon(baseAppEntries[selectedAppInfoIndex].icon, NACP_ICON_DOWNSCALED, NACP_ICON_DOWNSCALED, xpos, ypos);
                xpos += (NACP_ICON_DOWNSCALED + 8);
                ypos += 8;
            }
            
            if (strlen(baseAppEntries[selectedAppInfoIndex].name))
            {
                uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Name: %s", baseAppEntries[selectedAppInfoIndex].name);
                ypos += LINE_HEIGHT;
            }
            
            if (strlen(baseAppEntries[selectedAppInfoIndex].author))
            {
                uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Publisher: %s", baseAppEntries[selectedAppInfoIndex].author);
                ypos += LINE_HEIGHT;
            }
            
            uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Title ID: %016lX", baseAppEntries[selectedAppInfoIndex].titleId);
            
            if (titlePatchCount > 0)
            {
                u32 patchCnt = 0;
                
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s update(s): v", (menuType == MENUTYPE_GAMECARD ? "Bundled" : "Installed"));
                
                for(patch = 0; patch < titlePatchCount; patch++)
                {
                    if (checkIfPatchOrAddOnBelongsToBaseApplication(patch, selectedAppInfoIndex, false) && ((menuType == MENUTYPE_GAMECARD && baseAppEntries[selectedAppInfoIndex].storageId == patchEntries[patch].storageId) || menuType == MENUTYPE_SDCARD_EMMC))
                    {
                        if (patchCnt > 0) strcat(strbuf, ", v");
                        
                        strcat(strbuf, patchEntries[patch].versionStr);
                        
                        patchCnt++;
                    }
                }
                
                if (patchCnt > 0) uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), ypos, FONT_COLOR_SUCCESS_RGB, strbuf);
            }
            
            ypos += LINE_HEIGHT;
            
            uiDrawString(xpos, ypos, FONT_COLOR_SUCCESS_RGB, "Version: %s", baseAppEntries[selectedAppInfoIndex].versionStr);
            
            if (titleAddOnCount > 0)
            {
                u32 addOnCnt = 0;
                
                for(addon = 0; addon < titleAddOnCount; addon++)
                {
                    if (checkIfPatchOrAddOnBelongsToBaseApplication(addon, selectedAppInfoIndex, true) && ((menuType == MENUTYPE_GAMECARD && baseAppEntries[selectedAppInfoIndex].storageId == addOnEntries[addon].storageId) || menuType == MENUTYPE_SDCARD_EMMC)) addOnCnt++;
                }
                
                if (addOnCnt > 0) uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), ypos, FONT_COLOR_SUCCESS_RGB, "%s DLC(s): %u", (menuType == MENUTYPE_GAMECARD ? "Bundled" : "Installed"), addOnCnt);
            }
            
            ypos += LINE_HEIGHT;
            
            ypos += 8;
            if (xpos > 8 && (ypos - NACP_ICON_DOWNSCALED) < startYPos) ypos += (NACP_ICON_DOWNSCALED - (ypos - startYPos));
            ypos += LINE_HEIGHT;
            
            breaks += (int)round((double)(ypos - startYPos) / (double)LINE_HEIGHT);
            
            if (menuType == MENUTYPE_GAMECARD)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Capacity: %s | Used space: %s", gameCardInfo.sizeStr, gameCardInfo.trimmedSizeStr);
                
                if (titleAppCount > 1)
                {
                    if (baseAppEntries[selectedAppInfoIndex].contentSize)
                    {
                        uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Base application count: %u | Base application displayed: %u | Content size: %s", titleAppCount, selectedAppInfoIndex + 1, baseAppEntries[selectedAppInfoIndex].contentSizeStr);
                    } else {
                        uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Base application count: %u | Base application displayed: %u", titleAppCount, selectedAppInfoIndex + 1);
                    }
                } else {
                    if (baseAppEntries[selectedAppInfoIndex].contentSize) uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Content size: %s", baseAppEntries[selectedAppInfoIndex].contentSizeStr);
                }
                
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Partition count: %u (%s)", gameCardInfo.hfs0PartitionCnt, GAMECARD_TYPE(gameCardInfo.hfs0PartitionCnt));
                
                if (strlen(gameCardInfo.updateVersionStr)) uiDrawString((FB_WIDTH / 2) - (FB_WIDTH / 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Bundled FW update: %s", gameCardInfo.updateVersionStr);
                
                breaks++;
                
                if (titleAppCount > 1 && (titlePatchCount > 0 || titleAddOnCount > 0))
                {
                    if (titlePatchCount > 0) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Total bundled update(s): %u", titlePatchCount);
                    
                    if (titleAddOnCount > 0) uiDrawString((titlePatchCount > 0 ? ((FB_WIDTH / 2) - (FB_WIDTH / 8)) : 8), STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Total bundled DLC(s): %u", titleAddOnCount);
                    
                    breaks++;
                }
            } else
            if (menuType == MENUTYPE_SDCARD_EMMC)
            {
                if (baseAppEntries[selectedAppInfoIndex].contentSize)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Content size: %s", baseAppEntries[selectedAppInfoIndex].contentSizeStr);
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
                
                snprintf(dumpedContentInfoStr, MAX_CHARACTERS(dumpedContentInfoStr), "Content already dumped: ");
                
                if (menuType == MENUTYPE_GAMECARD)
                {
                    for(i = 0; i < 2; i++)
                    {
                        dumpName = generateGameCardDumpName(i == 1);
                        if (!dumpName) continue;
                        
                        // First check if a full XCI dump is available
                        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xci", XCI_DUMP_PATH, dumpName);
                        if (!(dumpedXci = checkIfFileExists(dumpPath)))
                        {
                            // Check if a split XCI dump is available
                            snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.xc0", XCI_DUMP_PATH, dumpName);
                            dumpedXci = checkIfFileExists(dumpPath);
                        }
                        
                        free(dumpName);
                        dumpName = NULL;
                        
                        if (dumpedXci)
                        {
                            dumpedXciCertificate = checkIfDumpedXciContainsCertificate(dumpPath);
                            break;
                        }
                    }
                }
                
                // Now search for dumped NSPs
                
                // Look for a dumped base application
                for(i = 0; i < 2; i++)
                {
                    dumpName = generateNSPDumpName(DUMP_APP_NSP, selectedAppInfoIndex, (i == 1));
                    if (!dumpName) continue;
                    
                    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                    
                    free(dumpName);
                    dumpName = NULL;
                    
                    if (checkIfFileExists(dumpPath))
                    {
                        dumpedBase = true;
                        dumpedBaseConsoleData = checkIfDumpedNspContainsConsoleData(dumpPath);
                        break;
                    }
                }
                
                // Look for dumped updates
                for(patch = 0; patch < titlePatchCount; patch++)
                {
                    if (!checkIfPatchOrAddOnBelongsToBaseApplication(patch, selectedAppInfoIndex, false)) continue;
                    
                    for(i = 0; i < 2; i++)
                    {
                        dumpName = generateNSPDumpName(DUMP_PATCH_NSP, patch, (i == 1));
                        if (!dumpName) continue;
                        
                        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                        
                        free(dumpName);
                        dumpName = NULL;
                        
                        if (checkIfFileExists(dumpPath))
                        {
                            patchCnt++;
                            if (checkIfDumpedNspContainsConsoleData(dumpPath)) patchCntConsoleData++;
                            break;
                        }
                    }
                }
                
                // Look for dumped DLCs
                for(addon = 0; addon < titleAddOnCount; addon++)
                {
                    if (!checkIfPatchOrAddOnBelongsToBaseApplication(addon, selectedAppInfoIndex, true)) continue;
                    
                    for(i = 0; i < 2; i++)
                    {
                        dumpName = generateNSPDumpName(DUMP_ADDON_NSP, addon, (i == 1));
                        if (!dumpName) continue;
                        
                        snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                        
                        free(dumpName);
                        dumpName = NULL;
                        
                        if (checkIfFileExists(dumpPath))
                        {
                            addOnCnt++;
                            if (checkIfDumpedNspContainsConsoleData(dumpPath)) addOnCntConsoleData++;
                            break;
                        }
                    }
                }
                
                if (!dumpedXci && !dumpedBase && !patchCnt && !addOnCnt)
                {
                    strcat(dumpedContentInfoStr, "NONE");
                } else {
                    if (dumpedXci)
                    {
                        snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "XCI (%s cert)", (dumpedXciCertificate ? "with" : "without"));
                        strcat(dumpedContentInfoStr, tmpStr);
                    }
                    
                    if (dumpedBase)
                    {
                        if (dumpedXci) strcat(dumpedContentInfoStr, ", ");
                        snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "BASE (%s console data)", (dumpedBaseConsoleData ? "with" : "without"));
                        strcat(dumpedContentInfoStr, tmpStr);
                    }
                    
                    if (patchCnt)
                    {
                        if (dumpedXci || dumpedBase) strcat(dumpedContentInfoStr, ", ");
                        
                        if (patchCntConsoleData)
                        {
                            if (patchCntConsoleData == patchCnt)
                            {
                                if (patchCnt > 1)
                                {
                                    snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "%u UPD (all with console data)", patchCnt);
                                } else {
                                    snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "UPD (with console data)");
                                }
                            } else {
                                snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "%u UPD (%u with console data)", patchCnt, patchCntConsoleData);
                            }
                        } else {
                            if (patchCnt > 1)
                            {
                                snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "%u UPD (all without console data)", patchCnt);
                            } else {
                                snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "UPD (without console data)");
                            }
                        }
                        
                        strcat(dumpedContentInfoStr, tmpStr);
                    }
                    
                    if (addOnCnt)
                    {
                        if (dumpedXci || dumpedBase || patchCnt) strcat(dumpedContentInfoStr, ", ");
                        
                        if (addOnCntConsoleData)
                        {
                            if (addOnCntConsoleData == addOnCnt)
                            {
                                snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "%u DLC (%s console data)", addOnCnt, (addOnCnt > 1 ? "all with" : "with"));
                            } else {
                                snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "%u DLC (%u with console data)", addOnCnt, addOnCntConsoleData);
                            }
                        } else {
                            snprintf(tmpStr, MAX_CHARACTERS(tmpStr), "%u DLC (%s console data)", addOnCnt, (addOnCnt > 1 ? "all without" : "without"));
                        }
                        
                        strcat(dumpedContentInfoStr, tmpStr);
                    }
                }
            }
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, dumpedContentInfoStr);
            breaks += 2;
        } else
        if (menuType == MENUTYPE_SDCARD_EMMC && orphanMode && (uiState == stateSdCardEmmcTitleMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu || uiState == stateExeFsMenu || uiState == stateRomFsMenu || uiState == stateTicketMenu))
        {
            if (strlen(orphanEntries[orphanListCursor].name))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Parent base application: %s", orphanEntries[orphanListCursor].name);
                breaks++;
            }
            
            patch_addon_ctx_t *ptr = (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? &(patchEntries[selectedPatchIndex]) : &(addOnEntries[selectedAddOnIndex]));
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Title ID: %016lX", ptr->titleId);
            breaks++;
            
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Version: %s", ptr->versionStr);
            breaks++;
            
            if (ptr->contentSize)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_SUCCESS_RGB, "Content size: %s", ptr->contentSizeStr);
                breaks++;
            }
            
            if (!strlen(dumpedContentInfoStr))
            {
                // Look for dumped content in the SD card
                char *dumpName = NULL;
                char dumpPath[NAME_BUF_LEN] = {'\0'};
                bool dumpedOrphan = false;
                nspDumpType orphanDumpType = (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? DUMP_PATCH_NSP : DUMP_ADDON_NSP);
                u32 orphanIndex = (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? selectedPatchIndex : selectedAddOnIndex);
                
                snprintf(dumpedContentInfoStr, MAX_CHARACTERS(dumpedContentInfoStr), "Title already dumped: ");
                
                for(i = 0; i < 2; i++)
                {
                    dumpName = generateNSPDumpName(orphanDumpType, orphanIndex, (i == 1));
                    if (!dumpName) continue;
                    
                    snprintf(dumpPath, MAX_CHARACTERS(dumpPath), "%s%s.nsp", NSP_DUMP_PATH, dumpName);
                    
                    free(dumpName);
                    dumpName = NULL;
                    
                    dumpedOrphan = checkIfFileExists(dumpPath);
                    if (dumpedOrphan)
                    {
                        strcat(dumpedContentInfoStr, "Yes");
                        
                        if (checkIfDumpedNspContainsConsoleData(dumpPath))
                        {
                            strcat(dumpedContentInfoStr, " (with console data)");
                        } else {
                            strcat(dumpedContentInfoStr, " (without console data)");
                        }
                        
                        break;
                    }
                }
                
                if (!dumpedOrphan) strcat(dumpedContentInfoStr, "No");
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
                menu = (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0PartitionDumpType1MenuItems : hfs0PartitionDumpType2MenuItems);
                menuItemsCount = (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? MAX_ELEMENTS(hfs0PartitionDumpType1MenuItems) : MAX_ELEMENTS(hfs0PartitionDumpType2MenuItems));
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (uiState == stateRawHfs0PartitionDumpMenu ? hfs0MenuItems[0] : hfs0MenuItems[1]));
                
                break;
            case stateHfs0BrowserMenu:
                menu = (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0BrowserType1MenuItems : hfs0BrowserType2MenuItems);
                menuItemsCount = (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? MAX_ELEMENTS(hfs0BrowserType1MenuItems) : MAX_ELEMENTS(hfs0BrowserType2MenuItems));
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, hfs0MenuItems[2]);
                
                break;
            case stateHfs0Browser:
                menu = (const char**)filenameBuffer;
                menuItemsCount = filenameCount;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0BrowserType1MenuItems[selectedPartitionIndex] : hfs0BrowserType2MenuItems[selectedPartitionIndex]));
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
                menu = (const char**)filenameBuffer;
                menuItemsCount = filenameCount;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[1]);
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", exeFsMenuItems[2], (dumpCfg.exeFsDumpCfg.isFat32 ? "Yes" : "No"), exeFsMenuItems[3], (dumpCfg.exeFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
                breaks++;
                
                if (!exeFsUpdateFlag)
                {
                    snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", exeFsSectionBrowserMenuItems[1], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                } else {
                    retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update to browse: ", strbuf, MAX_CHARACTERS(strbuf));
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
                menu = (const char**)filenameBuffer;
                menuItemsCount = filenameCount;
                
                // Skip the parent directory entry ("..") in the RomFS browser if we're currently at the root directory
                if (menu && menuItemsCount && strlen(curRomFsPath) <= 1)
                {
                    menu++;
                    menuItemsCount--;
                }
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[1]);
                breaks++;
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", romFsMenuItems[2], (dumpCfg.romFsDumpCfg.isFat32 ? "Yes" : "No"), romFsMenuItems[3], (dumpCfg.romFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
                breaks++;
                
                switch(curRomFsType)
                {
                    case ROMFS_TYPE_APP:
                        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", romFsSectionBrowserMenuItems[1], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                        break;
                    case ROMFS_TYPE_PATCH:
                        retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update to browse: ", strbuf, MAX_CHARACTERS(strbuf));
                        break;
                    case ROMFS_TYPE_ADDON:
                        retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, "DLC to browse: ", strbuf, MAX_CHARACTERS(strbuf));
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
                    snprintf(strbuf, MAX_CHARACTERS(strbuf), "Entry count: %d | Current entry: %d", filenameCount - 1, (strlen(curRomFsPath) <= 1 ? (cursor + 1) : cursor));
                } else {
                    snprintf(strbuf, MAX_CHARACTERS(strbuf), "Entry count: %d", filenameCount - 1);
                }
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, strbuf);
                
                break;
            case stateSdCardEmmcMenu:
                generateSdCardEmmcTitleList();
                
                menu = (const char**)filenameBuffer;
                menuItemsCount = filenameCount;
                
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
                
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (!orphanMode ? mainMenuItems[1] : (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH ? "Dump orphan update content" : "Dump orphan DLC content")));
                
                break;
            case stateSdCardEmmcOrphanPatchAddOnMenu:
                // Generate orphan content list
                // If orphanEntries == NULL or if orphanEntriesCnt == 0, both variables will be regenerated
                // Otherwise, this will only fill filenameBuffer
                generateOrphanPatchOrAddOnList();
                
                menu = (const char**)filenameBuffer;
                menuItemsCount = filenameCount;
                
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
            
            if (scroll > 0) uiDrawString((FB_WIDTH / 2) - (uiGetStrWidth(upwardsArrow) / 2), STRING_Y_POS(breaks), FONT_COLOR_RGB, upwardsArrow);
            
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
                
                // Avoid printing the "Dump verification method" option if "CRC32 checksum calculation + dump verification" is disabled
                if (uiState == stateXciDumpMenu && i == 6 && !dumpCfg.xciDumpCfg.calcCrc)
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
                
                // Avoid printing the "Verify dump using No-Intro database" option in the NSP dump menus if we're dealing with a gamecard title
                // Also, avoid printing the "Remove console specific data" option in the NSP dump menus if we're dealing with a gamecard title
                // Also, avoid printing the "Generate ticket-less dump" option in the NSP dump menus if we're dealing with a gamecard Application/AddOn title
                if (menuType == MENUTYPE_GAMECARD && (((uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu) && (i == 2 || i == 3)) || ((uiState == stateNspAppDumpMenu || uiState == stateNspAddOnDumpMenu) && i == 4)))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Dump delta fragments" option in the update NSP dump menu if we're dealing with a gamecard update
                if (menuType == MENUTYPE_GAMECARD && uiState == stateNspPatchDumpMenu && i == 6)
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
                if (uiState == stateSdCardEmmcBatchModeMenu && ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && ((!titleAppCount && i == 1) || (!titlePatchCount && i == 2) || (!titleAddOnCount && i == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && ((!sdCardTitleAppCount && i == 1) || (!sdCardTitlePatchCount && i == 2) || (!sdCardTitleAddOnCount && i == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && ((!emmcTitleAppCount && i == 1) || (!emmcTitlePatchCount && i == 2) || (!emmcTitleAddOnCount && i == 3)))))
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
                
                // Avoid printing the "Dump delta fragments from updates" option in the batch mode menu if the "Dump updates" option is disabled
                if (uiState == stateSdCardEmmcBatchModeMenu && i == 8 && !dumpCfg.batchDumpCfg.dumpPatchTitles)
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Source storage" option in the batch mode menu if we only have titles available in a single source storage device
                if (uiState == stateSdCardEmmcBatchModeMenu && i == 13 && ((!sdCardTitleAppCount && !sdCardTitlePatchCount && !sdCardTitleAddOnCount) || (!emmcTitleAppCount && !emmcTitlePatchCount && !emmcTitleAddOnCount)))
                {
                    j--;
                    continue;
                }
                
                // Avoid printing the "Use update" option in the ExeFS menu if we're dealing with a gamecard and either its base application count is greater than 1 or it has no available patches
                // Also avoid printing it if we're dealing with a SD/eMMC title and it has no available patches, or if we're dealing with an orphan Patch
                if (uiState == stateExeFsMenu && i == 4 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || !checkIfBaseApplicationHasPatchOrAddOn(0, false))) || (menuType == MENUTYPE_SDCARD_EMMC && ((!orphanMode && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)) || orphanMode))))
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
                if (uiState == stateRomFsMenu && i == 4 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || (!checkIfBaseApplicationHasPatchOrAddOn(0, false) && !checkIfBaseApplicationHasPatchOrAddOn(0, true)))) || (menuType == MENUTYPE_SDCARD_EMMC && (orphanMode || (!checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))))
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
                
                // Avoid printing the "RomFS options" element in the SD card / eMMC title menu if we're dealing with an orphan Patch
                // Also avoid printing the "ExeFS options" element in the SD card / eMMC title menu if we're dealing with an orphan DLC
                if (uiState == stateSdCardEmmcTitleMenu && orphanMode && ((orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH && i == 2) || (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_ADDON && i == 1)))
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
                ypos = (8 + (breaks * LINE_HEIGHT) + (uiState == stateSdCardEmmcMenu ? (j * (NACP_ICON_DOWNSCALED + 12)) : (j * (font_height + 12))) + 6);
                
                if (i == cursor)
                {
                    highlight = true;
                    uiFill(0, ypos - 6, FB_WIDTH, (uiState == stateSdCardEmmcMenu ? (NACP_ICON_DOWNSCALED + 12) : (font_height + 12)), HIGHLIGHT_BG_COLOR_RGB);
                }
                
                if (uiState == stateSdCardEmmcMenu)
                {
                    if (baseAppEntries[i].icon != NULL)
                    {
                        uiDrawIcon(baseAppEntries[i].icon, NACP_ICON_DOWNSCALED, NACP_ICON_DOWNSCALED, xpos, ypos);
                        
                        xpos += (NACP_ICON_DOWNSCALED + 8);
                    }
                    
                    ypos += ((NACP_ICON_DOWNSCALED / 2) - (font_height / 2));
                } else
                if (uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser || uiState == stateRomFsSectionBrowser)
                {
                    u8 *icon = NULL;
                    
                    if (uiState == stateRomFsSectionBrowser)
                    {
                        u32 idx = (strlen(curRomFsPath) <= 1 ? (i + 1) : i); // Adjust index if we're at the root directory
                        icon = (highlight ? (romFsBrowserEntries[idx].type == ROMFS_ENTRY_DIR ? dirHighlightIconBuf : fileHighlightIconBuf) : (romFsBrowserEntries[idx].type == ROMFS_ENTRY_DIR ? dirNormalIconBuf : fileNormalIconBuf));
                    } else {
                        icon = (highlight ? fileHighlightIconBuf : fileNormalIconBuf);
                    }
                    
                    uiDrawIcon(icon, BROWSER_ICON_DIMENSION, BROWSER_ICON_DIMENSION, xpos, ypos);
                    
                    xpos += (BROWSER_ICON_DIMENSION + 8);
                }
                
                if (highlight)
                {
                    uiDrawString(xpos, ypos, HIGHLIGHT_FONT_COLOR_RGB, menu[i]);
                    
                    u32 idx = ((uiState == stateRomFsSectionBrowser && strlen(curRomFsPath) <= 1) ? (i + 1) : i); // Adjust index if we're at the root directory
                    
                    if (uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser || (uiState == stateRomFsSectionBrowser && romFsBrowserEntries[idx].type == ROMFS_ENTRY_FILE))
                    {
                        snprintf(strbuf, MAX_CHARACTERS(strbuf), "(%s)", ((uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser) ? hfs0ExeFsEntriesSizes[idx].sizeStr : romFsBrowserEntries[idx].sizeInfo.sizeStr));
                        uiDrawString(FB_WIDTH - (8 + uiGetStrWidth(strbuf)), ypos, HIGHLIGHT_FONT_COLOR_RGB, strbuf);
                    }
                } else {
                    uiDrawString(xpos, ypos, FONT_COLOR_RGB, menu[i]);
                    
                    u32 idx = ((uiState == stateRomFsSectionBrowser && strlen(curRomFsPath) <= 1) ? (i + 1) : i); // Adjust index if we're at the root directory
                    
                    if (uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser || (uiState == stateRomFsSectionBrowser && romFsBrowserEntries[idx].type == ROMFS_ENTRY_FILE))
                    {
                        snprintf(strbuf, MAX_CHARACTERS(strbuf), "(%s)", ((uiState == stateHfs0Browser || uiState == stateExeFsSectionBrowser) ? hfs0ExeFsEntriesSizes[idx].sizeStr : romFsBrowserEntries[idx].sizeInfo.sizeStr));
                        uiDrawString(FB_WIDTH - (8 + uiGetStrWidth(strbuf)), ypos, FONT_COLOR_RGB, strbuf);
                    }
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
                        case 6: // Dump verification method
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, dumpCfg.xciDumpCfg.useNoIntroLookup, !dumpCfg.xciDumpCfg.useNoIntroLookup, FONT_COLOR_RGB, (dumpCfg.xciDumpCfg.useNoIntroLookup ? xciChecksumLookupMethods[1] : xciChecksumLookupMethods[0]));
                            break;
                        case 7: // Output naming scheme
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, dumpCfg.xciDumpCfg.useBrackets, !dumpCfg.xciDumpCfg.useBrackets, FONT_COLOR_RGB, (dumpCfg.xciDumpCfg.useBrackets ? xciNamingSchemes[1] : xciNamingSchemes[0]));
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
                        case 2: // Verify dump using No-Intro database
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.nspDumpCfg.useNoIntroLookup, !dumpCfg.nspDumpCfg.useNoIntroLookup, (dumpCfg.nspDumpCfg.useNoIntroLookup ? 0 : 255), (dumpCfg.nspDumpCfg.useNoIntroLookup ? 255 : 0), 0, (dumpCfg.nspDumpCfg.useNoIntroLookup ? "Yes" : "No"));
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
                                    retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, (menuType == MENUTYPE_GAMECARD), NULL, titleSelectorStr, MAX_CHARACTERS(titleSelectorStr));
                                    
                                    if (addOnEntries[selectedAddOnIndex].contentSize)
                                    {
                                        strcat(titleSelectorStr, " (");
                                        strcat(titleSelectorStr, addOnEntries[selectedAddOnIndex].contentSizeStr);
                                        strcat(titleSelectorStr, ")");
                                    }
                                    
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = ((menuType == MENUTYPE_GAMECARD && titleAddOnCount > 0 && selectedAddOnIndex > 0) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppInfoIndex, true) != selectedAddOnIndex));
                                rightArrowCondition = ((menuType == MENUTYPE_GAMECARD && titleAddOnCount > 0 && selectedAddOnIndex < (titleAddOnCount - 1)) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedAddOnIndex, selectedAppInfoIndex, true) != selectedAddOnIndex));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, titleSelectorStr);
                            }
                            break;
                        case 6: // Application to dump || Dump delta fragments || Output naming scheme (DLC)
                            if (uiState == stateNspAppDumpMenu)
                            {
                                if (!strlen(titleSelectorStr))
                                {
                                    // Print application name
                                    snprintf(titleSelectorStr, MAX_CHARACTERS(titleSelectorStr), "%s v%s", baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                                    
                                    if (baseAppEntries[selectedAppIndex].contentSize)
                                    {
                                        strcat(titleSelectorStr, " (");
                                        strcat(titleSelectorStr, baseAppEntries[selectedAppIndex].contentSizeStr);
                                        strcat(titleSelectorStr, ")");
                                    }
                                    
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex > 0);
                                rightArrowCondition = (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && selectedAppIndex < (titleAppCount - 1));
                            } else
                            if (uiState == stateNspPatchDumpMenu)
                            {
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.nspDumpCfg.dumpDeltaFragments, !dumpCfg.nspDumpCfg.dumpDeltaFragments, (dumpCfg.nspDumpCfg.dumpDeltaFragments ? 0 : 255), (dumpCfg.nspDumpCfg.dumpDeltaFragments ? 255 : 0), 0, (dumpCfg.nspDumpCfg.dumpDeltaFragments ? "Yes" : "No"));
                            } else
                            if (uiState == stateNspAddOnDumpMenu)
                            {
                                leftArrowCondition = dumpCfg.nspDumpCfg.useBrackets;
                                rightArrowCondition = !dumpCfg.nspDumpCfg.useBrackets;
                            }
                            
                            if (uiState != stateNspPatchDumpMenu) uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, (uiState == stateNspAddOnDumpMenu ? (dumpCfg.nspDumpCfg.useBrackets ? nspNamingSchemes[1] : nspNamingSchemes[0]) : titleSelectorStr));
                            
                            break;
                        case 7: // Output naming scheme (base application) || Update to dump
                            if (uiState == stateNspAppDumpMenu)
                            {
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, dumpCfg.nspDumpCfg.useBrackets, !dumpCfg.nspDumpCfg.useBrackets, FONT_COLOR_RGB, (dumpCfg.nspDumpCfg.useBrackets ? nspNamingSchemes[1] : nspNamingSchemes[0]));
                            } else
                            if (uiState == stateNspPatchDumpMenu)
                            {
                                if (!strlen(titleSelectorStr))
                                {
                                    // Find a matching application to print its name
                                    // Otherwise, just print the Title ID
                                    retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, (menuType == MENUTYPE_GAMECARD), NULL, titleSelectorStr, MAX_CHARACTERS(titleSelectorStr));
                                    
                                    if (patchEntries[selectedPatchIndex].contentSize)
                                    {
                                        strcat(titleSelectorStr, " (");
                                        strcat(titleSelectorStr, patchEntries[selectedPatchIndex].contentSizeStr);
                                        strcat(titleSelectorStr, ")");
                                    }
                                    
                                    uiTruncateOptionStr(titleSelectorStr, xpos, ypos, OPTIONS_X_END_POS_NSP);
                                }
                                
                                leftArrowCondition = ((menuType == MENUTYPE_GAMECARD && titlePatchCount > 0 && selectedPatchIndex > 0) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrievePreviousPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppInfoIndex, false) != selectedPatchIndex));
                                rightArrowCondition = ((menuType == MENUTYPE_GAMECARD && titlePatchCount > 0 && selectedPatchIndex < (titlePatchCount - 1)) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && retrieveNextPatchOrAddOnIndexFromBaseApplication(selectedPatchIndex, selectedAppInfoIndex, false) != selectedPatchIndex));
                                
                                uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, leftArrowCondition, rightArrowCondition, FONT_COLOR_RGB, titleSelectorStr);
                            }
                            
                            break;
                        case 8: // Output naming scheme (update)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, dumpCfg.nspDumpCfg.useBrackets, !dumpCfg.nspDumpCfg.useBrackets, FONT_COLOR_RGB, (dumpCfg.nspDumpCfg.useBrackets ? nspNamingSchemes[1] : nspNamingSchemes[0]));
                            break;
                        default:
                            break;
                    }
                    
                    if (i == 2)
                    {
                        if (dumpCfg.nspDumpCfg.useNoIntroLookup)
                        {
                            uiDrawString(FB_WIDTH / 2, ypos, FONT_COLOR_RGB, "This requires a working Internet connection!");
                        } else {
                            if (highlight)
                            {
                                uiFill(FB_WIDTH / 2, ypos - 6, FB_WIDTH / 2, font_height + 12, HIGHLIGHT_BG_COLOR_RGB);
                            } else {
                                uiFill(FB_WIDTH / 2, ypos - 6, FB_WIDTH / 2, font_height + 12, BG_COLOR_RGB);
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
                        case 8: // Dump delta fragments from updates
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.dumpDeltaFragments, !dumpCfg.batchDumpCfg.dumpDeltaFragments, (dumpCfg.batchDumpCfg.dumpDeltaFragments ? 0 : 255), (dumpCfg.batchDumpCfg.dumpDeltaFragments ? 255 : 0), 0, (dumpCfg.batchDumpCfg.dumpDeltaFragments ? "Yes" : "No"));
                            break;
                        case 9: // Skip dumped titles
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.skipDumpedTitles, !dumpCfg.batchDumpCfg.skipDumpedTitles, (dumpCfg.batchDumpCfg.skipDumpedTitles ? 0 : 255), (dumpCfg.batchDumpCfg.skipDumpedTitles ? 255 : 0), 0, (dumpCfg.batchDumpCfg.skipDumpedTitles ? "Yes" : "No"));
                            break;
                        case 10: // Remember dumped titles
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.rememberDumpedTitles, !dumpCfg.batchDumpCfg.rememberDumpedTitles, (dumpCfg.batchDumpCfg.rememberDumpedTitles ? 0 : 255), (dumpCfg.batchDumpCfg.rememberDumpedTitles ? 255 : 0), 0, (dumpCfg.batchDumpCfg.rememberDumpedTitles ? "Yes" : "No"));
                            break;
                        case 11: // Halt dump process on errors
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.batchDumpCfg.haltOnErrors, !dumpCfg.batchDumpCfg.haltOnErrors, (dumpCfg.batchDumpCfg.haltOnErrors ? 0 : 255), (dumpCfg.batchDumpCfg.haltOnErrors ? 255 : 0), 0, (dumpCfg.batchDumpCfg.haltOnErrors ? "Yes" : "No"));
                            break;
                        case 12: // Output naming scheme
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS_NSP, dumpCfg.batchDumpCfg.useBrackets, !dumpCfg.batchDumpCfg.useBrackets, FONT_COLOR_RGB, (dumpCfg.batchDumpCfg.useBrackets ? nspNamingSchemes[1] : nspNamingSchemes[0]));
                            break;
                        case 13: // Source storage
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
                        case 2: // Split files bigger than 4 GiB (FAT32 support)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.exeFsDumpCfg.isFat32, !dumpCfg.exeFsDumpCfg.isFat32, (dumpCfg.exeFsDumpCfg.isFat32 ? 0 : 255), (dumpCfg.exeFsDumpCfg.isFat32 ? 255 : 0), 0, (dumpCfg.exeFsDumpCfg.isFat32 ? "Yes" : "No"));
                            break;
                        case 3: // Save data to CFW directory (LayeredFS)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.exeFsDumpCfg.useLayeredFSDir, !dumpCfg.exeFsDumpCfg.useLayeredFSDir, (dumpCfg.exeFsDumpCfg.useLayeredFSDir ? 0 : 255), (dumpCfg.exeFsDumpCfg.useLayeredFSDir ? 255 : 0), 0, (dumpCfg.exeFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
                            break;
                        case 4: // Use update
                            if (exeFsUpdateFlag)
                            {
                                if (!strlen(exeFsAndRomFsSelectorStr))
                                {
                                    // Find a matching application to print its name
                                    // Otherwise, just print the Title ID
                                    retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, (menuType == MENUTYPE_GAMECARD && titleAppCount > 1), NULL, exeFsAndRomFsSelectorStr, MAX_CHARACTERS(exeFsAndRomFsSelectorStr));
                                    
                                    // Concatenate patch source storage
                                    strcat(exeFsAndRomFsSelectorStr, (patchEntries[selectedPatchIndex].storageId == NcmStorageId_GameCard ? " (gamecard)" : (patchEntries[selectedPatchIndex].storageId == NcmStorageId_SdCard ? " (SD card)" : " (eMMC)")));
                                    
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
                                snprintf(titleSelectorStr, MAX_CHARACTERS(titleSelectorStr), "%s v%s", baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
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
                                    retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, (menuType == MENUTYPE_GAMECARD), NULL, exeFsAndRomFsSelectorStr, MAX_CHARACTERS(exeFsAndRomFsSelectorStr));
                                    
                                    // Concatenate patch source storage
                                    strcat(exeFsAndRomFsSelectorStr, (patchEntries[selectedPatchIndex].storageId == NcmStorageId_GameCard ? " (gamecard)" : (patchEntries[selectedPatchIndex].storageId == NcmStorageId_SdCard ? " (SD card)" : " (eMMC)")));
                                    
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
                        case 2: // Split files bigger than 4 GiB (FAT32 support)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.romFsDumpCfg.isFat32, !dumpCfg.romFsDumpCfg.isFat32, (dumpCfg.romFsDumpCfg.isFat32 ? 0 : 255), (dumpCfg.romFsDumpCfg.isFat32 ? 255 : 0), 0, (dumpCfg.romFsDumpCfg.isFat32 ? "Yes" : "No"));
                            break;
                        case 3: // Save data to CFW directory (LayeredFS)
                            uiPrintOption(xpos, ypos, OPTIONS_X_END_POS, dumpCfg.romFsDumpCfg.useLayeredFSDir, !dumpCfg.romFsDumpCfg.useLayeredFSDir, (dumpCfg.romFsDumpCfg.useLayeredFSDir ? 0 : 255), (dumpCfg.romFsDumpCfg.useLayeredFSDir ? 255 : 0), 0, (dumpCfg.romFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
                            break;
                        case 4: // Use update/DLC
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
                                            retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, (menuType == MENUTYPE_GAMECARD && titleAppCount > 1), NULL, exeFsAndRomFsSelectorStr, MAX_CHARACTERS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (UPD)");
                                            strcat(exeFsAndRomFsSelectorStr, (patchEntries[selectedPatchIndex].storageId == NcmStorageId_GameCard ? " (gamecard)" : (patchEntries[selectedPatchIndex].storageId == NcmStorageId_SdCard ? " (SD card)" : " (eMMC)")));
                                            break;
                                        case ROMFS_TYPE_ADDON:
                                            retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, (menuType == MENUTYPE_GAMECARD && titleAppCount > 1), NULL, exeFsAndRomFsSelectorStr, MAX_CHARACTERS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (DLC)");
                                            strcat(exeFsAndRomFsSelectorStr, (addOnEntries[selectedAddOnIndex].storageId == NcmStorageId_GameCard ? " (gamecard)" : (addOnEntries[selectedAddOnIndex].storageId == NcmStorageId_SdCard ? " (SD card)" : " (eMMC)")));
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
                                snprintf(titleSelectorStr, MAX_CHARACTERS(titleSelectorStr), "%s v%s", baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
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
                                            retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, (menuType == MENUTYPE_GAMECARD), NULL, exeFsAndRomFsSelectorStr, MAX_CHARACTERS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (UPD)");
                                            strcat(exeFsAndRomFsSelectorStr, (patchEntries[selectedPatchIndex].storageId == NcmStorageId_GameCard ? " (gamecard)" : (patchEntries[selectedPatchIndex].storageId == NcmStorageId_SdCard ? " (SD card)" : " (eMMC)")));
                                            break;
                                        case ROMFS_TYPE_ADDON:
                                            retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, (menuType == MENUTYPE_GAMECARD), NULL, exeFsAndRomFsSelectorStr, MAX_CHARACTERS(exeFsAndRomFsSelectorStr));
                                            strcat(exeFsAndRomFsSelectorStr, " (DLC)");
                                            strcat(exeFsAndRomFsSelectorStr, (addOnEntries[selectedAddOnIndex].storageId == NcmStorageId_GameCard ? " (gamecard)" : (addOnEntries[selectedAddOnIndex].storageId == NcmStorageId_SdCard ? " (SD card)" : " (eMMC)")));
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
                                            retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, false, NULL, titleSelectorStr, MAX_CHARACTERS(titleSelectorStr));
                                            strcat(titleSelectorStr, " (UPD)");
                                            break;
                                        case TICKET_TYPE_ADDON:
                                            retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, false, NULL, titleSelectorStr, MAX_CHARACTERS(titleSelectorStr));
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
                                    snprintf(titleSelectorStr, MAX_CHARACTERS(titleSelectorStr), "%016lX v%s (BASE)", baseAppEntries[selectedAppInfoIndex].titleId, baseAppEntries[selectedAppInfoIndex].versionStr);
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
                ypos = (8 + (breaks * LINE_HEIGHT) + (uiState == stateSdCardEmmcMenu ? (j * (NACP_ICON_DOWNSCALED + 12)) : (j * (font_height + 12))));
                uiDrawString((FB_WIDTH / 2) - (uiGetStrWidth(downwardsArrow) / 2), ypos, FONT_COLOR_RGB, downwardsArrow);
            }
            
            j++;
            if ((scroll + maxElements) < menuItemsCount) j++;
            
            ypos = (8 + (breaks * LINE_HEIGHT) + (j * (font_height + 12)));
            
            if (uiState == stateMainMenu)
            {
                // Print warning about missing Lockpick_RCM keys file
                if (!keysFileAvailable)
                {
                    uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "Warning: missing keys file at \"%s\".", KEYS_FILE_PATH);
                    ypos += (LINE_HEIGHT + LINE_STRING_OFFSET);
                    
                    uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "This file is needed to deal with the encryption schemes used by Nintendo Switch content files.");
                    ypos += (LINE_HEIGHT + LINE_STRING_OFFSET);
                    
                    uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "SD/eMMC operations will be entirely disabled, along with NSP/ExeFS/RomFS related operations.");
                    ypos += (LINE_HEIGHT + LINE_STRING_OFFSET);
                    
                    uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "Please run Lockpick_RCM to generate this file. More info at: " LOCKPICK_RCM_URL);
                }
                
                // Print warning about running the application under applet mode
                if (appletModeCheck())
                {
                    if (!keysFileAvailable) ypos += ((LINE_HEIGHT * 2) + LINE_STRING_OFFSET);
                    
                    uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "Warning: running under applet mode.");
                    ypos += (LINE_HEIGHT + LINE_STRING_OFFSET);
                    
                    uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "It seems you used an applet (Album, Settings, etc.) to run the application. This mode greatly limits the amount of usable RAM.");
                    ypos += (LINE_HEIGHT + LINE_STRING_OFFSET);
                    
                    uiDrawString(STRING_X_POS, ypos, FONT_COLOR_ERROR_RGB, "If you ever get any memory allocation errors, please consider running the application through title override (hold R while launching a game).");
                }
            }
            
            // Print information about the "Change NPDM RSA key/sig in Program NCA" option
            if (((uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu) && cursor == 5) || (uiState == stateSdCardEmmcBatchModeMenu && cursor == 7))
            {
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Replaces the public RSA key in the NPDM ACID section and the NPDM RSA signature in the Program NCA (only if it needs other modifications).");
                ypos += (LINE_HEIGHT + LINE_STRING_OFFSET);
                
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Disabling this will make the output NSP require ACID patches to work under any CFW, but will also make the Program NCA verifiable by PC tools.");
            }
            
            // Print information about the "Dump delta fragments" option
            if ((uiState == stateNspPatchDumpMenu && cursor == 6) || (uiState == stateSdCardEmmcBatchModeMenu && cursor == 8))
            {
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Dumps delta fragments for the selected update(s), if available. These are commonly excluded - they serve no real purpose in output dumps.");
            }
            
            // Print information about the "Split files bigger than 4 GiB (FAT32 support)" option
            if ((uiState == stateExeFsMenu || uiState == stateRomFsMenu) && cursor == 2)
            {
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "If FAT32 support is enabled, files bigger than 4 GiB will be split and stored in a subdirectory with the archive bit set (like NSPs).");
            }
            
            // Print information about the "Save data to CFW directory (LayeredFS)" option
            if ((uiState == stateExeFsMenu || uiState == stateRomFsMenu) && cursor == 3)
            {
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Enabling this option will save output data to \"%s[TitleID]/%s/\" (LayeredFS directory structure).", strchr(cfwDirStr, '/'), (uiState == stateExeFsMenu ? "exefs" : "romfs"));
            }
            
            // Print hint about dumping RomFS content from DLCs
            if ((uiState == stateRomFsMenu && cursor == 4 && ((menuType == MENUTYPE_GAMECARD && titleAppCount <= 1 && checkIfBaseApplicationHasPatchOrAddOn(0, true)) || (menuType == MENUTYPE_SDCARD_EMMC && !orphanMode && checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true)))) || ((uiState == stateRomFsSectionDataDumpMenu || uiState == stateRomFsSectionBrowserMenu) && cursor == 2 && (menuType == MENUTYPE_GAMECARD && titleAppCount > 1 && checkIfBaseApplicationHasPatchOrAddOn(selectedAppIndex, true))))
            {
                uiDrawString(STRING_X_POS, ypos, FONT_COLOR_RGB, "Hint: choosing a DLC will only access RomFS data from it, unlike updates (which share their RomFS data with its base application).");
            }
        } else {
            breaks += 2;
            
            if (uiState == stateRomFsSectionBrowser)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "No entries available in the selected directory! Press " NINTENDO_FONT_B " to go back.");
            } else
            if (uiState == stateSdCardEmmcOrphanPatchAddOnMenu)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_RGB, "No orphan updates/DLCs available in the SD card / eMMC storage!");
            }
        }
        
        bool curGcStatus = gameCardInfo.isInserted;
        
        while(true)
        {
            uiUpdateStatusMsg();
            uiRefreshDisplay();
            
            hidScanInput();
            
            keysDown = hidKeysAllDown(CONTROLLER_P1_AUTO);
            keysHeld = hidKeysAllHeld(CONTROLLER_P1_AUTO);
            
            if ((keysDown && !(keysDown & KEY_TOUCH)) || (keysHeld && !(keysHeld & KEY_TOUCH)) || (menuType == MENUTYPE_GAMECARD && gameCardInfo.isInserted != curGcStatus)) break;
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
                        case 6: // Dump verification method
                            dumpCfg.xciDumpCfg.useNoIntroLookup = false;
                            break;
                        case 7: // Output naming scheme
                            dumpCfg.xciDumpCfg.useBrackets = false;
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
                        case 6: // Dump verification method
                            dumpCfg.xciDumpCfg.useNoIntroLookup = true;
                            break;
                        case 7: // Output naming scheme
                            dumpCfg.xciDumpCfg.useBrackets = true;
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
                        case 2: // Verify dump using No-Intro database
                            dumpCfg.nspDumpCfg.useNoIntroLookup = false;
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
                        case 6: // Application to dump || Dump delta fragments || Output naming scheme (DLC)
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
                                dumpCfg.nspDumpCfg.dumpDeltaFragments = false;
                            } else
                            if (uiState == stateNspAddOnDumpMenu)
                            {
                                dumpCfg.nspDumpCfg.useBrackets = false;
                            }
                            break;
                        case 7: // Output naming scheme (base application) || Update to dump
                            if (uiState == stateNspAppDumpMenu)
                            {
                                dumpCfg.nspDumpCfg.useBrackets = false;
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
                        case 8: // Output naming scheme (update)
                            dumpCfg.nspDumpCfg.useBrackets = false;
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
                        case 2: // Verify dump using No-Intro database
                            dumpCfg.nspDumpCfg.useNoIntroLookup = true;
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
                        case 6: // Application to dump || Dump delta fragments || Output naming scheme (DLC)
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
                            } else
                            if (uiState == stateNspPatchDumpMenu)
                            {
                                dumpCfg.nspDumpCfg.dumpDeltaFragments = true;
                            } else
                            if (uiState == stateNspAddOnDumpMenu)
                            {
                                dumpCfg.nspDumpCfg.useBrackets = true;
                            }
                            break;
                        case 7: // Output naming scheme (base application) || Update to dump
                            if (uiState == stateNspAppDumpMenu)
                            {
                                dumpCfg.nspDumpCfg.useBrackets = true;
                            } else
                            if (uiState == stateNspPatchDumpMenu)
                            {
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
                        case 8: // Output naming scheme (update)
                            dumpCfg.nspDumpCfg.useBrackets = true;
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
                        case 8: // Dump delta fragments from updates
                            dumpCfg.batchDumpCfg.dumpDeltaFragments = false;
                            break;
                        case 9: // Skip already dumped titles
                            dumpCfg.batchDumpCfg.skipDumpedTitles = false;
                            break;
                        case 10: // Remember dumped titles
                            dumpCfg.batchDumpCfg.rememberDumpedTitles = false;
                            break;
                        case 11: // Halt dump process on errors
                            dumpCfg.batchDumpCfg.haltOnErrors = false;
                            break;
                        case 12: // Output naming scheme
                            dumpCfg.batchDumpCfg.useBrackets = false;
                            break;
                        case 13: // Source storage
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
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (emmcTitleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (emmcTitlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (emmcTitleAddOnCount > 0);
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
                        case 8: // Dump delta fragments from updates
                            dumpCfg.batchDumpCfg.dumpDeltaFragments = true;
                            break;
                        case 9: // Skip already dumped titles
                            dumpCfg.batchDumpCfg.skipDumpedTitles = true;
                            break;
                        case 10: // Remember dumped titles
                            dumpCfg.batchDumpCfg.rememberDumpedTitles = true;
                            break;
                        case 11: // Halt dump process on errors
                            dumpCfg.batchDumpCfg.haltOnErrors = true;
                            break;
                        case 12: // Output naming scheme
                            dumpCfg.batchDumpCfg.useBrackets = true;
                            break;
                        case 13: // Source storage
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
                                    dumpCfg.batchDumpCfg.dumpAppTitles = (emmcTitleAppCount > 0);
                                    dumpCfg.batchDumpCfg.dumpPatchTitles = (emmcTitlePatchCount > 0);
                                    dumpCfg.batchDumpCfg.dumpAddOnTitles = (emmcTitleAddOnCount > 0);
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
                        freeTitlesFromSdCardAndEmmc(NcmContentMetaType_Patch);
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
                        case 2: // Split files bigger than 4 GiB (FAT32 support)
                            dumpCfg.exeFsDumpCfg.isFat32 = false;
                            break;
                        case 3: // Save data to CFW directory (LayeredFS)
                            dumpCfg.exeFsDumpCfg.useLayeredFSDir = false;
                            break;
                        case 4: // Use update
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
                        case 2: // Split files bigger than 4 GiB (FAT32 support)
                            dumpCfg.exeFsDumpCfg.isFat32 = true;
                            break;
                        case 3: // Save data to CFW directory (LayeredFS)
                            dumpCfg.exeFsDumpCfg.useLayeredFSDir = true;
                            break;
                        case 4: // Use update
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
                        freeTitlesFromSdCardAndEmmc(NcmContentMetaType_Patch);
                        freeTitlesFromSdCardAndEmmc(NcmContentMetaType_AddOnContent);
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
                        case 2: // Split files bigger than 4 GiB (FAT32 support)
                            dumpCfg.romFsDumpCfg.isFat32 = false;
                            break;
                        case 3: // Save data to CFW directory (LayeredFS)
                            dumpCfg.romFsDumpCfg.useLayeredFSDir = false;
                            break;
                        case 4: // Use update/DLC
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
                        case 2: // Split files bigger than 4 GiB (FAT32 support)
                            dumpCfg.romFsDumpCfg.isFat32 = true;
                            break;
                        case 3: // Save data to CFW directory (LayeredFS)
                            dumpCfg.romFsDumpCfg.useLayeredFSDir = true;
                            break;
                        case 4: // Use update/DLC
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
                                    loadTitlesFromSdCardAndEmmc(NcmContentMetaType_Patch);
                                    
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
                                    loadTitlesFromSdCardAndEmmc(NcmContentMetaType_Patch);
                                    loadTitlesFromSdCardAndEmmc(NcmContentMetaType_AddOnContent);
                                    
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
                        if (menu && menuItemsCount)
                        {
                            // Save selected file index
                            selectedFileIndex = (u32)cursor;
                            res = resultHfs0BrowserCopyFile;
                        }
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
                            res = (romFsBrowserEntries[selectedFileIndex].type == ROMFS_ENTRY_DIR ? resultRomFsSectionBrowserChangeDir : resultRomFsSectionBrowserCopyFile);
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
                                res = resultShowExeFsMenu;
                                
                                if (!orphanMode)
                                {
                                    // Reset options to their default values
                                    exeFsUpdateFlag = false;
                                    selectedPatchIndex = 0;
                                } else {
                                    exeFsUpdateFlag = true;
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
                        freeHfs0ExeFsEntriesSizes();
                        freeFilenameBuffer();
                        
                        res = resultShowHfs0BrowserMenu;
                    } else
                    if (uiState == stateExeFsSectionBrowser)
                    {
                        freeHfs0ExeFsEntriesSizes();
                        freeFilenameBuffer();
                        
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
                            freeRomFsBrowserEntries();
                            freeFilenameBuffer();
                            if (curRomFsType == ROMFS_TYPE_PATCH) freeBktrContext();
                            freeRomFsContext();
                            
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
                        res = resultShowSdCardEmmcMenu;
                        orphanMode = false;
                    }
                }
                
                // Special action #1
                if (keysDown & KEY_Y)
                {
                    if (uiState == stateSdCardEmmcMenu && (calculateOrphanPatchOrAddOnCount(false) || calculateOrphanPatchOrAddOnCount(true)))
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
                
                // Avoid placing the cursor on the "Dump verification method" option if "CRC32 checksum calculation + dump verification" is disabled
                if (uiState == stateXciDumpMenu && cursor == 6 && !dumpCfg.xciDumpCfg.calcCrc)
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
                
                // Avoid placing the cursor on the "Verify dump using No-Intro database" option in the NSP dump menus if we're dealing with a gamecard title
                // Also, avoid placing the cursor on the "Remove console specific data" option in the NSP dump menus if we're dealing with a gamecard title
                // Also, avoid placing the cursor on the "Generate ticket-less dump" option in the NSP dump menus if we're dealing with a gamecard Application/AddOn title
                if (menuType == MENUTYPE_GAMECARD && (((uiState == stateNspAppDumpMenu || uiState == stateNspPatchDumpMenu || uiState == stateNspAddOnDumpMenu) && (cursor == 2 || cursor == 3)) || ((uiState == stateNspAppDumpMenu || uiState == stateNspAddOnDumpMenu) && cursor == 4)))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = ((uiState == stateNspPatchDumpMenu && cursor == 3) ? 4 : 5);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = 1;
                    }
                }
                
                // Avoid printing the "Dump delta fragments" option in the update NSP dump menu if we're dealing with a gamecard update
                if (menuType == MENUTYPE_GAMECARD && uiState == stateNspPatchDumpMenu && cursor == 6)
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
                if (uiState == stateSdCardEmmcBatchModeMenu && ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && ((!titleAppCount && cursor == 1) || (!titlePatchCount && cursor == 2) || (!titleAddOnCount && cursor == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && ((!sdCardTitleAppCount && cursor == 1) || (!sdCardTitlePatchCount && cursor == 2) || (!sdCardTitleAddOnCount && cursor == 3))) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && ((!emmcTitleAppCount && cursor == 1) || (!emmcTitlePatchCount && cursor == 2) || (!emmcTitleAddOnCount && cursor == 3)))))
                {
                    if (cursor == 1)
                    {
                        if (scrollAmount > 0)
                        {
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL)
                            {
                                cursor = (titlePatchCount ? 2 : (titleAddOnCount ? 3 : 4));
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD)
                            {
                                cursor = (sdCardTitlePatchCount ? 2 : (sdCardTitleAddOnCount ? 3 : 4));
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC)
                            {
                                cursor = (emmcTitlePatchCount ? 2 : (emmcTitleAddOnCount ? 3 : 4));
                            }
                        } else
                        if (scrollAmount < 0)
                        {
                            cursor = 0;
                        }
                    } else
                    if (cursor == 2)
                    {
                        if (scrollAmount > 0)
                        {
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL)
                            {
                                cursor = (titleAddOnCount ? 3 : 4);
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD)
                            {
                                cursor = (sdCardTitleAddOnCount ? 3 : 4);
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC)
                            {
                                cursor = (emmcTitleAddOnCount ? 3 : 4);
                            }
                        } else
                        if (scrollAmount < 0)
                        {
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL)
                            {
                                cursor = (titleAppCount ? 1 : 0);
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD)
                            {
                                cursor = (sdCardTitleAppCount ? 1 : 0);
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC)
                            {
                                cursor = (emmcTitleAppCount ? 1 : 0);
                            }
                        }
                    } else
                    if (cursor == 3)
                    {
                        if (scrollAmount > 0)
                        {
                            cursor = 4;
                        } else
                        if (scrollAmount < 0)
                        {
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL)
                            {
                                cursor = (titlePatchCount ? 2 : (titleAppCount ? 1 : 0));
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD)
                            {
                                cursor = (sdCardTitlePatchCount ? 2 : (sdCardTitleAppCount ? 1 : 0));
                            } else
                            if (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC)
                            {
                                cursor = (emmcTitlePatchCount ? 2 : (emmcTitleAppCount ? 1 : 0));
                            }
                        }
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
                
                // Avoid placing the cursor on the "Dump delta fragments from updates" option in the batch mode menu if the "Dump updates" option is disabled
                if (uiState == stateSdCardEmmcBatchModeMenu && cursor == 8 && !dumpCfg.batchDumpCfg.dumpPatchTitles)
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
                if (uiState == stateSdCardEmmcBatchModeMenu && cursor == 13 && ((!sdCardTitleAppCount && !sdCardTitlePatchCount && !sdCardTitleAddOnCount) || (!emmcTitleAppCount && !emmcTitlePatchCount && !emmcTitleAddOnCount)))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 12);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor--;
                    }
                }
                
                // Avoid placing the cursor on the "Use update" option in the ExeFS menu if we're dealing with a gamecard and either its base application count is greater than 1 or it has no available patches
                // Also avoid placing the cursor on it if we're dealing with a SD/eMMC title and it has no available patches, or if we're dealing with an orphan Patch
                if (uiState == stateExeFsMenu && cursor == 4 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || !checkIfBaseApplicationHasPatchOrAddOn(0, false))) || (menuType == MENUTYPE_SDCARD_EMMC && ((!orphanMode && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false)) || orphanMode))))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = (scrollWithKeysDown ? 1 : 0);
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
                        cursor = (scrollWithKeysDown ? 1 : 0);
                    }
                }
                
                // Avoid placing the cursor on the "Use update/DLC" option in the RomFS menu if we're dealing with a gamecard and either its base application count is greater than 1 or it has no available patches/DLCs
                // Also avoid placing the cursor on it if we're dealing with a SD/eMMC title and it has no available patches/DLCs (or if its an orphan title)
                if (uiState == stateRomFsMenu && cursor == 4 && ((menuType == MENUTYPE_GAMECARD && (titleAppCount > 1 || (!checkIfBaseApplicationHasPatchOrAddOn(0, false) && !checkIfBaseApplicationHasPatchOrAddOn(0, true)))) || (menuType == MENUTYPE_SDCARD_EMMC && (orphanMode || (!checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, false) && !checkIfBaseApplicationHasPatchOrAddOn(selectedAppInfoIndex, true))))))
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = (scrollWithKeysDown ? 1 : 0);
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
                        cursor = (scrollWithKeysDown ? 1 : 0);
                    }
                }
                
                // Avoid placing the cursor on the "RomFS options" element in the SD card / eMMC title menu if we're dealing with an orphan Patch
                // Also avoid placing the cursor on the "ExeFS options" element in the SD card / eMMC title menu if we're dealing with an orphan DLC
                if (uiState == stateSdCardEmmcTitleMenu && orphanMode && ((orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_PATCH && cursor == 2) || (orphanEntries[orphanListCursor].type == ORPHAN_ENTRY_TYPE_ADDON && cursor == 1)))
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
                
                // Avoid placing the cursor on the "Use ticket from title" element in the Ticket menu if we're dealing with an orphan title
                if (uiState == stateTicketMenu && orphanMode && cursor == 2)
                {
                    if (scrollAmount > 0)
                    {
                        cursor = (scrollWithKeysDown ? 0 : 1);
                    } else
                    if (scrollAmount < 0)
                    {
                        cursor = (scrollWithKeysDown ? 1 : 0);
                    }
                }
            }
        }
    } else
    if (uiState == stateDumpXci)
    {
        char tmp[128] = {'\0'};
        strbuf[0] = '\0';
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[0]);
        breaks++;
        
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s", xciDumpMenuItems[1], (dumpCfg.xciDumpCfg.isFat32 ? "Yes" : "No"));
        
        if (dumpCfg.xciDumpCfg.isFat32)
        {
            strcat(strbuf, " | ");
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", xciDumpMenuItems[2], (dumpCfg.xciDumpCfg.setXciArchiveBit ? "Yes" : "No"));
            strcat(strbuf, tmp);
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[3], (dumpCfg.xciDumpCfg.keepCert ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[4], (dumpCfg.xciDumpCfg.trimDump ? "Yes" : "No"));
        breaks++;
        
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s", xciDumpMenuItems[5], (dumpCfg.xciDumpCfg.calcCrc ? "Yes" : "No"));
        
        if (dumpCfg.xciDumpCfg.calcCrc)
        {
            strcat(strbuf, " | ");
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", xciDumpMenuItems[6], (dumpCfg.xciDumpCfg.useNoIntroLookup ? xciChecksumLookupMethods[1] : xciChecksumLookupMethods[0]));
            strcat(strbuf, tmp);
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", xciDumpMenuItems[7], (dumpCfg.xciDumpCfg.useBrackets ? xciNamingSchemes[1] : xciNamingSchemes[0]));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpNXCardImage(&(dumpCfg.xciDumpCfg));
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowXciDumpMenu;
        
        dumpedContentInfoStr[0] = '\0';
    } else
    if (uiState == stateDumpNsp)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (menuType == MENUTYPE_GAMECARD ? nspDumpGameCardMenuItems[selectedNspDumpType] : nspDumpSdCardEmmcMenuItems[selectedNspDumpType]));
        breaks++;
        
        menu = (selectedNspDumpType == DUMP_APP_NSP ? nspAppDumpMenuItems : (selectedNspDumpType == DUMP_PATCH_NSP ? nspPatchDumpMenuItems : nspAddOnDumpMenuItems));
        
        char tmp[128] = {'\0'};
        strbuf[0] = '\0';
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[1], (dumpCfg.nspDumpCfg.isFat32 ? "Yes" : "No"));
        breaks++;
        
        if (menuType != MENUTYPE_GAMECARD)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[2], (dumpCfg.nspDumpCfg.useNoIntroLookup ? "Yes" : "No"));
            breaks++;
        }
        
        if (menuType == MENUTYPE_GAMECARD && selectedNspDumpType == DUMP_PATCH_NSP)
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s", menu[4], (dumpCfg.nspDumpCfg.tiklessDump ? "Yes" : "No"));
        } else
        if (menuType == MENUTYPE_SDCARD_EMMC)
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s", menu[3], (dumpCfg.nspDumpCfg.removeConsoleData ? "Yes" : "No"));
            
            if (dumpCfg.nspDumpCfg.removeConsoleData)
            {
                strcat(strbuf, " | ");
                snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[4], (dumpCfg.nspDumpCfg.tiklessDump ? "Yes" : "No"));
                strcat(strbuf, tmp);
            }
        }
        
        if (selectedNspDumpType == DUMP_APP_NSP || selectedNspDumpType == DUMP_PATCH_NSP)
        {
            if ((menuType == MENUTYPE_GAMECARD && selectedNspDumpType == DUMP_PATCH_NSP) || menuType == MENUTYPE_SDCARD_EMMC) strcat(strbuf, " | ");
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[5], (dumpCfg.nspDumpCfg.npdmAcidRsaPatch ? "Yes" : "No"));
            strcat(strbuf, tmp);
            
            if (selectedNspDumpType == DUMP_PATCH_NSP)
            {
                strcat(strbuf, " | ");
                snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[6], (dumpCfg.nspDumpCfg.dumpDeltaFragments ? "Yes" : "No"));
                strcat(strbuf, tmp);
            }
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks++;
        
        if (selectedNspDumpType == DUMP_APP_NSP)
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", menu[6], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
        } else
        if (selectedNspDumpType == DUMP_PATCH_NSP)
        {
            retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, menu[7], strbuf, MAX_CHARACTERS(strbuf));
        } else
        if (selectedNspDumpType == DUMP_ADDON_NSP)
        {
            retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, menu[5], strbuf, MAX_CHARACTERS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", (selectedNspDumpType == DUMP_ADDON_NSP ? menu[6] : (selectedNspDumpType == DUMP_APP_NSP ? menu[7] : menu[8])), (dumpCfg.nspDumpCfg.useBrackets ? nspNamingSchemes[1] : nspNamingSchemes[0]));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpNintendoSubmissionPackage(selectedNspDumpType, (selectedNspDumpType == DUMP_APP_NSP ? selectedAppIndex : (selectedNspDumpType == DUMP_PATCH_NSP ? selectedPatchIndex : selectedAddOnIndex)), &(dumpCfg.nspDumpCfg), false);
        
        waitForButtonPress();
        
        updateFreeSpace();
        
        res = (selectedNspDumpType == DUMP_APP_NSP ? resultShowNspAppDumpMenu : (selectedNspDumpType == DUMP_PATCH_NSP ? resultShowNspPatchDumpMenu : resultShowNspAddOnDumpMenu));
        
        dumpedContentInfoStr[0] = '\0';
    } else
    if (uiState == stateSdCardEmmcBatchDump)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Batch dump");
        breaks++;
        
        menu = batchModeMenuItems;
        
        char tmp[128] = {'\0'};
        strbuf[0] = '\0';
        
        if ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && titleAppCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && sdCardTitleAppCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && emmcTitleAppCount))
        {
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[1], (dumpCfg.batchDumpCfg.dumpAppTitles ? "Yes" : "No"));
            strcat(strbuf, tmp);
        }
        
        if ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && titlePatchCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && sdCardTitlePatchCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && emmcTitlePatchCount))
        {
            if (strlen(strbuf)) strcat(strbuf, " | ");
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[2], (dumpCfg.batchDumpCfg.dumpPatchTitles ? "Yes" : "No"));
            strcat(strbuf, tmp);
        }
        
        if ((dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL && titleAddOnCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD && sdCardTitleAddOnCount) || (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_EMMC && emmcTitleAddOnCount))
        {
            if (strlen(strbuf)) strcat(strbuf, " | ");
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[3], (dumpCfg.batchDumpCfg.dumpAddOnTitles ? "Yes" : "No"));
            strcat(strbuf, tmp);
        }
        
        if (strlen(strbuf))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
            breaks++;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[4], (dumpCfg.batchDumpCfg.isFat32 ? "Yes" : "No"));
        breaks++;
        
        snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s", menu[5], (dumpCfg.batchDumpCfg.removeConsoleData ? "Yes" : "No"));
        
        if (dumpCfg.batchDumpCfg.removeConsoleData)
        {
            strcat(strbuf, " | ");
            snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[6], (dumpCfg.batchDumpCfg.tiklessDump ? "Yes" : "No"));
            strcat(strbuf, tmp);
        }
        
        strcat(strbuf, " | ");
        snprintf(tmp, MAX_CHARACTERS(tmp), "%s%s", menu[7], (dumpCfg.batchDumpCfg.npdmAcidRsaPatch ? "Yes" : "No"));
        strcat(strbuf, tmp);
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks++;
        
        if (dumpCfg.batchDumpCfg.dumpPatchTitles)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[8], (dumpCfg.batchDumpCfg.dumpDeltaFragments ? "Yes" : "No"));
            breaks++;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s | %s%s", menu[9], (dumpCfg.batchDumpCfg.skipDumpedTitles ? "Yes" : "No"), menu[10], (dumpCfg.batchDumpCfg.rememberDumpedTitles ? "Yes" : "No"), menu[11], (dumpCfg.batchDumpCfg.haltOnErrors ? "Yes" : "No"));
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[12], (dumpCfg.batchDumpCfg.useBrackets ? nspNamingSchemes[1] : nspNamingSchemes[0]));
        breaks++;
        
        if ((sdCardTitleAppCount || sdCardTitlePatchCount || sdCardTitleAddOnCount) && (emmcTitleAppCount || emmcTitlePatchCount || emmcTitleAddOnCount))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s", menu[13], (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_ALL ? "All (SD card + eMMC)" : (dumpCfg.batchDumpCfg.batchModeSrc == BATCH_SOURCE_SDCARD ? "SD card" : "eMMC")));
            breaks++;
        }
        
        breaks++;
        uiRefreshDisplay();
        
        int ret = dumpNintendoSubmissionPackageBatch(&(dumpCfg.batchDumpCfg));
        
        if (ret == -2)
        {
            uiRefreshDisplay();
            res = resultExit;
        } else {
            waitForButtonPress();
            
            updateFreeSpace();
            res = resultShowSdCardEmmcBatchModeMenu;
        }
    } else
    if (uiState == stateDumpRawHfs0Partition)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Raw %s", (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0PartitionDumpType1MenuItems[selectedPartitionIndex] : hfs0PartitionDumpType2MenuItems[selectedPartitionIndex]));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpRawHfs0Partition(selectedPartitionIndex, true);
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowRawHfs0PartitionDumpMenu;
    } else
    if (uiState == stateDumpHfs0PartitionData)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Data %s", (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0PartitionDumpType1MenuItems[selectedPartitionIndex] : hfs0PartitionDumpType2MenuItems[selectedPartitionIndex]));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpHfs0PartitionData(selectedPartitionIndex, true);
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowHfs0PartitionDataDumpMenu;
    } else
    if (uiState == stateHfs0BrowserGetList)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, (gameCardInfo.hfs0PartitionCnt == GAMECARD_TYPE1_PARTITION_CNT ? hfs0BrowserType1MenuItems[selectedPartitionIndex] : hfs0BrowserType2MenuItems[selectedPartitionIndex]));
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual File Dump: %s (HFS0 partition %u [%s])", filenameBuffer[selectedFileIndex], selectedPartitionIndex, GAMECARD_PARTITION_NAME(gameCardInfo.hfs0PartitionCnt, selectedPartitionIndex));
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpFileFromHfs0Partition(selectedPartitionIndex, selectedFileIndex, filenameBuffer[selectedFileIndex], true);
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowHfs0Browser;
    } else
    if (uiState == stateDumpExeFsSectionData)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[0]);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", exeFsMenuItems[2], (dumpCfg.exeFsDumpCfg.isFat32 ? "Yes" : "No"), exeFsMenuItems[3], (dumpCfg.exeFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        if (!exeFsUpdateFlag)
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", exeFsSectionDumpMenuItems[1], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
        } else {
            retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update to dump: ", strbuf, MAX_CHARACTERS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        u32 curIndex = (!exeFsUpdateFlag ? selectedAppIndex : selectedPatchIndex);
        
        dumpExeFsSectionData(curIndex, exeFsUpdateFlag, &(dumpCfg.exeFsDumpCfg));
        
        waitForButtonPress();
        
        updateFreeSpace();
        
        res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowExeFsSectionDataDumpMenu : resultShowExeFsMenu);
    } else
    if (uiState == stateExeFsSectionBrowserGetList)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, exeFsMenuItems[1]);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", exeFsMenuItems[2], (dumpCfg.exeFsDumpCfg.isFat32 ? "Yes" : "No"), exeFsMenuItems[3], (dumpCfg.exeFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        if (!exeFsUpdateFlag)
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", exeFsSectionBrowserMenuItems[1], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
        } else {
            retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update to browse: ", strbuf, MAX_CHARACTERS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiPleaseWait(0);
        breaks += 2;
        
        bool exefs_fail = false;
        
        u32 curIndex = (!exeFsUpdateFlag ? selectedAppIndex : selectedPatchIndex);
        
        if (readNcaExeFsSection(curIndex, exeFsUpdateFlag))
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual File Dump: %s (ExeFS)", filenameBuffer[selectedFileIndex]);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", exeFsMenuItems[2], (dumpCfg.exeFsDumpCfg.isFat32 ? "Yes" : "No"), exeFsMenuItems[3], (dumpCfg.exeFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        if (!exeFsUpdateFlag)
        {
            snprintf(strbuf, MAX_CHARACTERS(strbuf), "Base application: %s v%s", baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
        } else {
            retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update: ", strbuf, MAX_CHARACTERS(strbuf));
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        u32 curIndex = (!exeFsUpdateFlag ? selectedAppIndex : selectedPatchIndex);
        
        dumpFileFromExeFsSection(curIndex, selectedFileIndex, exeFsUpdateFlag, &(dumpCfg.exeFsDumpCfg));
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowExeFsSectionBrowser;
    } else
    if (uiState == stateDumpRomFsSectionData)
    {
        u32 curIndex = 0;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[0]);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", romFsMenuItems[2], (dumpCfg.romFsDumpCfg.isFat32 ? "Yes" : "No"), romFsMenuItems[3], (dumpCfg.romFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", romFsSectionDumpMenuItems[1], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update to dump: ", strbuf, MAX_CHARACTERS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, "DLC to dump: ", strbuf, MAX_CHARACTERS(strbuf));
                curIndex = selectedAddOnIndex;
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpRomFsSectionData(curIndex, curRomFsType, &(dumpCfg.romFsDumpCfg));
        
        waitForButtonPress();
        
        updateFreeSpace();
        
        res = ((menuType == MENUTYPE_GAMECARD && titleAppCount > 1) ? resultShowRomFsSectionDataDumpMenu : resultShowRomFsMenu);
    } else
    if (uiState == stateRomFsSectionBrowserGetEntries)
    {
        u32 curIndex = 0;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, romFsMenuItems[1]);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", romFsMenuItems[2], (dumpCfg.romFsDumpCfg.isFat32 ? "Yes" : "No"), romFsMenuItems[3], (dumpCfg.romFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", romFsSectionBrowserMenuItems[1], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update to browse: ", strbuf, MAX_CHARACTERS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, "DLC to browse: ", strbuf, MAX_CHARACTERS(strbuf));
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
        
        if (readNcaRomFsSection(curIndex, curRomFsType, -1))
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
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", romFsMenuItems[2], (dumpCfg.romFsDumpCfg.isFat32 ? "Yes" : "No"), romFsMenuItems[3], (dumpCfg.romFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s v%s", romFsSectionBrowserMenuItems[1], baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update to browse: ", strbuf, MAX_CHARACTERS(strbuf));
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, "DLC to browse: ", strbuf, MAX_CHARACTERS(strbuf));
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
            freeRomFsBrowserEntries();
            freeFilenameBuffer();
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
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual File Dump: %s (RomFS)", filenameBuffer[selectedFileIndex]);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", romFsMenuItems[2], (dumpCfg.romFsDumpCfg.isFat32 ? "Yes" : "No"), romFsMenuItems[3], (dumpCfg.romFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "Base application: %s v%s", baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update: ", strbuf, MAX_CHARACTERS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, "DLC: ", strbuf, MAX_CHARACTERS(strbuf));
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
            dumpFileFromRomFsSection(curIndex, romFsBrowserEntries[selectedFileIndex].offset, curRomFsType, &(dumpCfg.romFsDumpCfg));
        } else {
            // Unexpected condition
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Error: the selected entry is not a file!");
            breaks += 2;
        }
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowRomFsSectionBrowser;
    } else
    if (uiState == stateRomFsSectionBrowserCopyDir)
    {
        u32 curIndex = 0;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "Manual Directory Dump: romfs:%s (RomFS)", curRomFsPath);
        breaks++;
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, "%s%s | %s%s", romFsMenuItems[2], (dumpCfg.romFsDumpCfg.isFat32 ? "Yes" : "No"), romFsMenuItems[3], (dumpCfg.romFsDumpCfg.useLayeredFSDir ? "Yes" : "No"));
        breaks++;
        
        switch(curRomFsType)
        {
            case ROMFS_TYPE_APP:
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "Base application: %s v%s", baseAppEntries[selectedAppIndex].name, baseAppEntries[selectedAppIndex].versionStr);
                curIndex = selectedAppIndex;
                break;
            case ROMFS_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, "Update: ", strbuf, MAX_CHARACTERS(strbuf));
                curIndex = selectedPatchIndex;
                break;
            case ROMFS_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, "DLC: ", strbuf, MAX_CHARACTERS(strbuf));
                curIndex = selectedAddOnIndex;
                break;
            default:
                break;
        }
        
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, strbuf);
        breaks += 2;
        
        uiRefreshDisplay();
        
        dumpCurrentDirFromRomFsSection(curIndex, curRomFsType, &(dumpCfg.romFsDumpCfg));
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowRomFsSectionBrowser;
    } else
    if (uiState == stateDumpGameCardCertificate)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, gameCardMenuItems[5]);
        breaks += 2;
        
        dumpGameCardCertificate();
        
        waitForButtonPress();
        
        updateFreeSpace();
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
                snprintf(strbuf, MAX_CHARACTERS(strbuf), "%s%s | %016lX v%s (BASE)", menu[2], baseAppEntries[selectedAppInfoIndex].name, baseAppEntries[selectedAppInfoIndex].titleId, baseAppEntries[selectedAppInfoIndex].versionStr);
                break;
            case TICKET_TYPE_PATCH:
                retrieveDescriptionForPatchOrAddOn(selectedPatchIndex, false, true, menu[2], strbuf, MAX_CHARACTERS(strbuf));
                strcat(strbuf, " (UPD)");
                break;
            case TICKET_TYPE_ADDON:
                retrieveDescriptionForPatchOrAddOn(selectedAddOnIndex, true, true, menu[2], strbuf, MAX_CHARACTERS(strbuf));
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
        
        updateFreeSpace();
        res = resultShowTicketMenu;
    } else
    if (uiState == stateUpdateNSWDBXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, updateMenuItems[0]);
        breaks += 2;
        
        updateNSWDBXml();
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowUpdateMenu;
    } else
    if (uiState == stateUpdateApplication)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_TITLE_RGB, updateMenuItems[1]);
        breaks += 2;
        
        updatePerformed = updateApplication();
        
        waitForButtonPress();
        
        updateFreeSpace();
        res = resultShowUpdateMenu;
    }
    
    return res;
}
