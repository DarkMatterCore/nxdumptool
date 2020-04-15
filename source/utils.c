#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <switch.h>

//#include "freetype_helper.h"
//#include "lvgl_helper.h"
#include "services.h"
#include "gamecard.h"
#include "utils.h"

/* Global variables. */

static AppletHookCookie g_systemOverclockCookie = {0};

static Mutex g_logfileMutex = 0;

/* Function prototypes. */

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param);

u64 utilsHidKeysAllDown(void)
{
    u8 controller;
    u64 keys_down = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keys_down |= hidKeysDown((HidControllerID)controller);
    
    return keys_down;
}

u64 utilsHidKeysAllHeld(void)
{
    u8 controller;
    u64 keys_held = 0;
    
    for(controller = 0; controller < (u8)CONTROLLER_P1_AUTO; controller++) keys_held |= hidKeysHeld((HidControllerID)controller);
    
    return keys_held;
}

void utilsWaitForButtonPress(void)
{
    u64 flag, keys_down;
    
    /* Don't consider touch screen presses nor stick movement as button inputs */
    flag = ~(KEY_TOUCH | KEY_LSTICK_LEFT | KEY_LSTICK_RIGHT | KEY_LSTICK_UP | KEY_LSTICK_DOWN | KEY_RSTICK_LEFT | KEY_RSTICK_RIGHT | KEY_RSTICK_UP | KEY_RSTICK_DOWN);
    
    while(appletMainLoop())
    {
        hidScanInput();
        keys_down = utilsHidKeysAllDown();
        if (keys_down & flag) break;
    }
}

void utilsConsoleErrorScreen(const char *fmt, ...)
{
    consoleInit(NULL);
    
    va_list va;
    va_start(va, fmt);
    vprintf(fmt, va);
    va_end(va);
    
    printf("\nPress any button to exit.\n");
    
    consoleUpdate(NULL);
    utilsWaitForButtonPress();
    consoleExit(NULL);
}

void utilsWriteLogMessage(const char *func_name, const char *fmt, ...)
{
    mutexLock(&g_logfileMutex);
    
    va_list args;
    FILE *logfile = NULL;
    
    logfile = fopen(APP_BASE_PATH "nxdumptool.log", "a+");
    if (!logfile) goto out;
    
    time_t now = time(NULL);
    struct tm *ts = localtime(&now);
    
    fprintf(logfile, "%d/%d/%d %d:%d:%d -> %s: ", ts->tm_year + 1900, ts->tm_mon + 1, ts->tm_mday, ts->tm_hour, ts->tm_min, ts->tm_sec, func_name);
    
    va_start(args, fmt);
    vfprintf(logfile, fmt, args);
    va_end(args);
    
    fprintf(logfile, "\r\n");
    fclose(logfile);
    
out:
    mutexUnlock(&g_logfileMutex);
}

void utilsOverclockSystem(bool restore)
{
    u32 cpuClkRate = ((restore ? CPU_CLKRT_NORMAL : CPU_CLKRT_OVERCLOCKED) * 1000000);
    u32 memClkRate = ((restore ? MEM_CLKRT_NORMAL : MEM_CLKRT_OVERCLOCKED) * 1000000);
    servicesChangeHardwareClockRates(cpuClkRate, memClkRate);
}

bool utilsInitializeResources(void)
{
    /* Initialize all needed services */
    if (!servicesInitialize()) return false;
    
    /* Initialize FreeType */
    //if (!freeTypeHelperInitialize()) return false;
    
    /* Initialize LVGL */
    //if (!lvglHelperInitialize()) return false;
    
    /* Overclock system */
    utilsOverclockSystem(false);
    
    /* Setup an applet hook to change the hardware clocks after a system mode change (docked <-> undocked) */
    appletHook(&g_systemOverclockCookie, utilsOverclockSystemAppletHook, NULL);
    
    /* Initialize gamecard */
    Result rc = gamecardInitialize();
    if (R_FAILED(rc))
    {
        utilsConsoleErrorScreen("gamecard fail\n");
        return false;
    }
    
    return true;
}

void utilsCloseResources(void)
{
    /* Deinitialize gamecard */
    gamecardExit();
    
    /* Unset our overclock applet hook */
    appletUnhook(&g_systemOverclockCookie);
    
    /* Restore hardware clocks */
    utilsOverclockSystem(true);
    
    /* Free LVGL resources */
    //lvglHelperExit();
    
    /* Free FreeType resouces */
    //freeTypeHelperExit();
    
    /* Close initialized services */
    servicesClose();
}

static void utilsOverclockSystemAppletHook(AppletHookType hook, void *param)
{
    (void)param;
    
    if (hook != AppletHookType_OnOperationMode && hook != AppletHookType_OnPerformanceMode) return;
    
    /* To do: read config here to actually know the value to use with utilsOverclockSystem */
    utilsOverclockSystem(true);
}
