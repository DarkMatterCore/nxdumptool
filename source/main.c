#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <memory.h>

#include "dumper.h"
#include "ui.h"
#include "util.h"
#include "fs_ext.h"
#include "keys.h"

/* Extern variables */

extern bool keysFileAvailable;

extern FsDeviceOperator fsOperatorInstance;

extern FsEventNotifier fsGameCardEventNotifier;
extern Handle fsGameCardEventHandle;
extern Event fsGameCardKernelEvent;
extern UEvent exitEvent;

extern bool gameCardInserted;

extern char appLaunchPath[NAME_BUF_LEN];

extern nca_keyset_t nca_keyset;

int main(int argc, char *argv[])
{
    /* Copy launch path */
    if (!envIsNso() && argc > 0)
    {
        int i;
        for(i = 0; i < argc; i++)
        {
            if (strlen(argv[i]) > 10 && !strncasecmp(argv[i], "sdmc:/", 6) && !strncasecmp(argv[i] + strlen(argv[i]) - 4, ".nro", 4))
            {
                snprintf(appLaunchPath, MAX_ELEMENTS(appLaunchPath), argv[i]);
                break;
            }
        }
    }
    
    /* Initialize UI */
    if (!uiInit()) return -1;
    
    /* Zero out NCA keyset */
    memset(&nca_keyset, 0, sizeof(nca_keyset_t));
    
    /* Init ExeFS context */
    initExeFsContext();
    
    /* Init RomFS context */
    initRomFsContext();
    
    /* Init BKTR context */
    initBktrContext();
    
    /* Make sure output directories exist */
    createOutputDirectories();
    
    /* Load settings from configuration file */
    loadConfig();
    
    /* Check if the Lockpick_RCM keys file is available */
    keysFileAvailable = checkIfFileExists(KEYS_FILE_PATH);
    
    /* Enable CPU boost mode */
    appletSetCpuBoostMode(ApmCpuBoostMode_Type1);
    
    Result result;
    Thread thread;
    
    int ret = 0;
    bool exitMainLoop = false;
    bool initNcm = false, initNs = false, initCsrng = false, initSpl = false, initPmdmnt = false;
    bool openFsDevOp = false, openGcEvtNotifier = false, loadGcKernEvt = false, startGcThread = false;
    
    /* Initialize the ncm service */
    result = ncmInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to initialize the ncm service! (0x%08X)", result);
        ret = -2;
        goto out;
    }
    
    initNcm = true;
    
    /* Initialize the ns service */
    result = nsInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to initialize the ns service! (0x%08X)", result);
        ret = -3;
        goto out;
    }
    
    initNs = true;
    
    /* Initialize the csrng service */
    result = csrngInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to initialize the csrng service! (0x%08X)", result);
        ret = -4;
        goto out;
    }
    
    initCsrng = true;
    
    /* Initialize the spl service */
    result = splInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to initialize the spl service! (0x%08X)", result);
        ret = -5;
        goto out;
    }
    
    initSpl = true;
    
    /* Initialize the pm:dmnt service */
    result = pmdmntInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to initialize the pm:dmnt service! (0x%08X)", result);
        ret = -6;
        goto out;
    }
    
    initPmdmnt = true;
    
    /* Open device operator */
    result = fsOpenDeviceOperator(&fsOperatorInstance);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to open device operator! (0x%08X)", result);
        ret = -7;
        goto out;
    }
    
    openFsDevOp = true;
    
    /* Open gamecard detection event notifier */
    result = fsOpenGameCardDetectionEventNotifier(&fsGameCardEventNotifier);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to open gamecard detection event notifier! (0x%08X)", result);
        ret = -8;
        goto out;
    }
    
    openGcEvtNotifier = true;
    
    /* Retrieve gamecard detection event handle */
    result = fsEventNotifierGetEventHandle(&fsGameCardEventNotifier, &fsGameCardEventHandle);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to retrieve gamecard detection event handle! (0x%08X)", result);
        ret = -9;
        goto out;
    }
    
    /* Retrieve initial gamecard status */
    gameCardInserted = isGameCardInserted();
    
    /* Load gamecard detection kernel event */
    eventLoadRemote(&fsGameCardKernelEvent, fsGameCardEventHandle, false);
    
    loadGcKernEvt = true;
    
    /* Create usermode exit event */
    ueventCreate(&exitEvent, false);
    
    /* Create gamecard detection thread */
    result = threadCreate(&thread, fsGameCardDetectionThreadFunc, NULL, 0x10000, 0x2C, -2);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to create gamecard detection thread! (0x%08X)", result);
        ret = -10;
        goto out;
    }
    
    /* Start gamecard detection thread */
    result = threadStart(&thread);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_DEFAULT_POS, FONT_COLOR_ERROR_RGB, "Failed to start gamecard detection thread! (0x%08X)", result);
        ret = -11;
        goto out;
    }
    
    startGcThread = true;
    
    /* Mount BIS System partition from the eMMC */
    if (!mountSysEmmcPartition())
    {
        ret = -12;
        goto out;
    }
    
    /* Main application loop */
    while(appletMainLoop())
    {
        UIResult result = uiProcess();
        switch(result)
        {
            case resultShowMainMenu:
                uiSetState(stateMainMenu);
                break;
            case resultShowGameCardMenu:
                uiSetState(stateGameCardMenu);
                break;
            case resultShowXciDumpMenu:
                uiSetState(stateXciDumpMenu);
                break;
            case resultDumpXci:
                uiSetState(stateDumpXci);
                break;
            case resultShowNspDumpMenu:
                uiSetState(stateNspDumpMenu);
                break;
            case resultShowNspAppDumpMenu:
                uiSetState(stateNspAppDumpMenu);
                break;
            case resultShowNspPatchDumpMenu:
                uiSetState(stateNspPatchDumpMenu);
                break;
            case resultShowNspAddOnDumpMenu:
                uiSetState(stateNspAddOnDumpMenu);
                break;
            case resultDumpNsp:
                uiSetState(stateDumpNsp);
                break;
            case resultShowHfs0Menu:
                uiSetState(stateHfs0Menu);
                break;
            case resultShowRawHfs0PartitionDumpMenu:
                uiSetState(stateRawHfs0PartitionDumpMenu);
                break;
            case resultDumpRawHfs0Partition:
                uiSetState(stateDumpRawHfs0Partition);
                break;
            case resultShowHfs0PartitionDataDumpMenu:
                uiSetState(stateHfs0PartitionDataDumpMenu);
                break;
            case resultDumpHfs0PartitionData:
                uiSetState(stateDumpHfs0PartitionData);
                break;
            case resultShowHfs0BrowserMenu:
                uiSetState(stateHfs0BrowserMenu);
                break;
            case resultHfs0BrowserGetList:
                uiSetState(stateHfs0BrowserGetList);
                break;
            case resultShowHfs0Browser:
                uiSetState(stateHfs0Browser);
                break;
            case resultHfs0BrowserCopyFile:
                uiSetState(stateHfs0BrowserCopyFile);
                break;
            case resultShowExeFsMenu:
                uiSetState(stateExeFsMenu);
                break;
            case resultShowExeFsSectionDataDumpMenu:
                uiSetState(stateExeFsSectionDataDumpMenu);
                break;
            case resultDumpExeFsSectionData:
                uiSetState(stateDumpExeFsSectionData);
                break;
            case resultShowExeFsSectionBrowserMenu:
                uiSetState(stateExeFsSectionBrowserMenu);
                break;
            case resultExeFsSectionBrowserGetList:
                uiSetState(stateExeFsSectionBrowserGetList);
                break;
            case resultShowExeFsSectionBrowser:
                uiSetState(stateExeFsSectionBrowser);
                break;
            case resultExeFsSectionBrowserCopyFile:
                uiSetState(stateExeFsSectionBrowserCopyFile);
                break;
            case resultShowRomFsMenu:
                uiSetState(stateRomFsMenu);
                break;
            case resultShowRomFsSectionDataDumpMenu:
                uiSetState(stateRomFsSectionDataDumpMenu);
                break;
            case resultDumpRomFsSectionData:
                uiSetState(stateDumpRomFsSectionData);
                break;
            case resultShowRomFsSectionBrowserMenu:
                uiSetState(stateRomFsSectionBrowserMenu);
                break;
            case resultRomFsSectionBrowserGetEntries:
                uiSetState(stateRomFsSectionBrowserGetEntries);
                break;
            case resultShowRomFsSectionBrowser:
                uiSetState(stateRomFsSectionBrowser);
                break;
            case resultRomFsSectionBrowserChangeDir:
                uiSetState(stateRomFsSectionBrowserChangeDir);
                break;
            case resultRomFsSectionBrowserCopyFile:
                uiSetState(stateRomFsSectionBrowserCopyFile);
                break;
            case resultRomFsSectionBrowserCopyDir:
                uiSetState(stateRomFsSectionBrowserCopyDir);
                break;
            case resultDumpGameCardCertificate:
                uiSetState(stateDumpGameCardCertificate);
                break;
            case resultShowSdCardEmmcMenu:
                uiSetState(stateSdCardEmmcMenu);
                break;
            case resultShowSdCardEmmcTitleMenu:
                uiSetState(stateSdCardEmmcTitleMenu);
                break;
            case resultShowSdCardEmmcOrphanPatchAddOnMenu:
                uiSetState(stateSdCardEmmcOrphanPatchAddOnMenu);
                break;
            case resultShowSdCardEmmcBatchModeMenu:
                uiSetState(stateSdCardEmmcBatchModeMenu);
                break;
            case resultSdCardEmmcBatchDump:
                uiSetState(stateSdCardEmmcBatchDump);
                break;
            case resultShowTicketMenu:
                uiSetState(stateTicketMenu);
                break;
            case resultDumpTicket:
                uiSetState(stateDumpTicket);
                break;
            case resultShowUpdateMenu:
                uiSetState(stateUpdateMenu);
                break;
            case resultUpdateNSWDBXml:
                uiSetState(stateUpdateNSWDBXml);
                break;
            case resultUpdateApplication:
                uiSetState(stateUpdateApplication);
                break;
            case resultExit:
                exitMainLoop = true;
                break;
            default:
                break;
        }
        
        if (exitMainLoop) break;
    }
    
    /* Signal the exit event to terminate the gamecard detection thread */
    ueventSignal(&exitEvent);
    
    /* Wait for the gamecard detection thread to exit */
    threadWaitForExit(&thread);
    
out:
    if (ret < 0)
    {
        uiRefreshDisplay();
        delay(5);
    }
    
    /* Unmount BIS System partition from the eMMC */
    unmountSysEmmcPartition();
    
    /* Close gamecard detection thread */
    if (startGcThread) threadClose(&thread);
    
    /* Close gamecard detection kernel event */
    if (loadGcKernEvt) eventClose(&fsGameCardKernelEvent);
    
    /* Close gamecard detection event notifier */
    if (openGcEvtNotifier) fsEventNotifierClose(&fsGameCardEventNotifier);
    
    /* Close device operator */
    if (openFsDevOp) fsDeviceOperatorClose(&fsOperatorInstance);
    
    /* Denitialize the pm:dmnt service */
    if (initPmdmnt) pmdmntExit();
    
    /* Denitialize the spl service */
    if (initSpl) splExit();
    
    /* Denitialize the csrng service */
    if (initCsrng) csrngExit();
    
    /* Denitialize the ns service */
    if (initNs) nsExit();
    
    /* Denitialize the ncm service */
    if (initNcm) ncmExit();
    
    /* Disable CPU boost mode */
    appletSetCpuBoostMode(ApmCpuBoostMode_Disabled);
    
    /* Free global resources */
    freeGlobalData();
    
    /* Deinitialize UI */
    uiDeinit();
    
    return ret;
}
