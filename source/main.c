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

extern FsDeviceOperator fsOperatorInstance;

extern FsEventNotifier fsGameCardEventNotifier;
extern Handle fsGameCardEventHandle;
extern Event fsGameCardKernelEvent;
extern UEvent exitEvent;

extern bool gameCardInserted;

extern char strbuf[NAME_BUF_LEN * 4];

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
                snprintf(appLaunchPath, sizeof(appLaunchPath) / sizeof(appLaunchPath[0]), argv[i]);
                break;
            }
        }
    }
    
    /* Initialize UI */
    if (!uiInit()) return -1;
    
    int ret = 0;
    Result result;
    
    /* Initialize the ncm service */
    result = ncmInitialize();
    if (R_SUCCEEDED(result))
    {
        /* Initialize the ns service */
        result = nsInitialize();
        if (R_SUCCEEDED(result))
        {
            /* Initialize the csrng service */
            result = csrngInitialize();
            if (R_SUCCEEDED(result))
            {
                /* Initialize the spl service */
                result = splInitialize();
                if (R_SUCCEEDED(result))
                {
                    /* Initialize the pm:dmnt service */
                    result = pmdmntInitialize();
                    if (R_SUCCEEDED(result))
                    {
                        /* Open device operator */
                        result = fsOpenDeviceOperator(&fsOperatorInstance);
                        if (R_SUCCEEDED(result))
                        {
                            /* Open gamecard detection event notifier */
                            result = fsOpenGameCardDetectionEventNotifier(&fsGameCardEventNotifier);
                            if (R_SUCCEEDED(result))
                            {
                                /* Retrieve gamecard detection event handle */
                                result = fsEventNotifierGetEventHandle(&fsGameCardEventNotifier, &fsGameCardEventHandle);
                                if (R_SUCCEEDED(result))
                                {
                                    /* Retrieve initial gamecard status */
                                    gameCardInserted = isGameCardInserted();
                                    
                                    /* Load gamecard detection kernel event */
                                    eventLoadRemote(&fsGameCardKernelEvent, fsGameCardEventHandle, false);
                                    
                                    /* Create usermode exit event */
                                    ueventCreate(&exitEvent, false);
                                    
                                    /* Create gamecard detection thread */
                                    Thread thread;
                                    result = threadCreate(&thread, fsGameCardDetectionThreadFunc, NULL, 0x10000, 0x2C, -2);
                                    if (R_SUCCEEDED(result))
                                    {
                                        /* Start gamecard detection thread */
                                        result = threadStart(&thread);
                                        if (R_SUCCEEDED(result))
                                        {
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
                                            
                                            /* Main application loop */
                                            bool exitLoop = false;
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
                                                        exitLoop = true;
                                                        break;
                                                    default:
                                                        break;
                                                }
                                                
                                                if (exitLoop) break;
                                            }
                                            
                                            /* Signal the exit event to terminate the gamecard detection thread */
                                            ueventSignal(&exitEvent);
                                            
                                            /* Wait for the gamecard detection thread to exit */
                                            threadWaitForExit(&thread);
                                        } else {
                                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to start gamecard detection thread! (0x%08X)", result);
                                            uiDrawString(strbuf, 8, 8, 255, 255, 255);
                                            uiRefreshDisplay();
                                            delay(5);
                                            ret = -11;
                                        }
                                        
                                        /* Close gamecard detection thread */
                                        threadClose(&thread);
                                    } else {
                                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to create gamecard detection thread! (0x%08X)", result);
                                        uiDrawString(strbuf, 8, 8, 255, 255, 255);
                                        uiRefreshDisplay();
                                        delay(5);
                                        ret = -10;
                                    }
                                    
                                    /* Close gamecard detection kernel event */
                                    eventClose(&fsGameCardKernelEvent);
                                } else {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to retrieve gamecard detection event handle! (0x%08X)", result);
                                    uiDrawString(strbuf, 8, 8, 255, 255, 255);
                                    uiRefreshDisplay();
                                    delay(5);
                                    ret = -9;
                                }
                                
                                /* Close gamecard detection event notifier */
                                fsEventNotifierClose(&fsGameCardEventNotifier);
                            } else {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open gamecard detection event notifier! (0x%08X)", result);
                                uiDrawString(strbuf, 8, 8, 255, 255, 255);
                                uiRefreshDisplay();
                                delay(5);
                                ret = -8;
                            }
                            
                            /* Close device operator */
                            fsDeviceOperatorClose(&fsOperatorInstance);
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open device operator! (0x%08X)", result);
                            uiDrawString(strbuf, 8, 8, 255, 255, 255);
                            uiRefreshDisplay();
                            delay(5);
                            ret = -7;
                        }
                        
                        /* Denitialize the pm:dmnt service */
                        pmdmntExit();
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the pm:dmnt service! (0x%08X)", result);
                        uiDrawString(strbuf, 8, 8, 255, 255, 255);
                        uiRefreshDisplay();
                        delay(5);
                        ret = -6;
                    }
                    
                    /* Denitialize the spl service */
                    splExit();
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the spl service! (0x%08X)", result);
                    uiDrawString(strbuf, 8, 8, 255, 255, 255);
                    uiRefreshDisplay();
                    delay(5);
                    ret = -5;
                }
                
                /* Denitialize the csrng service */
                csrngExit();
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the csrng service! (0x%08X)", result);
                uiDrawString(strbuf, 8, 8, 255, 255, 255);
                uiRefreshDisplay();
                delay(5);
                ret = -4;
            }
            
            /* Denitialize the ns service */
            nsExit();
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the ns service! (0x%08X)", result);
            uiDrawString(strbuf, 8, 8, 255, 255, 255);
            uiRefreshDisplay();
            delay(5);
            ret = -3;
        }
        
        /* Denitialize the ncm service */
        ncmExit();
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the ncm service! (0x%08X)", result);
        uiDrawString(strbuf, 8, 8, 255, 255, 255);
        uiRefreshDisplay();
        delay(5);
        ret = -2;
    }
    
    /* Free global resources */
    freeGlobalData();
    
    /* Deinitialize UI */
    uiDeinit();
    
    return ret;
}
