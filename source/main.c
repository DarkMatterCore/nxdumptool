#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>
#include <memory.h>

#include "dumper.h"
#include "ui.h"
#include "util.h"
#include "fsext.h"
#include "extkeys.h"

/* Extern variables */

extern FsDeviceOperator fsOperatorInstance;

extern FsEventNotifier fsGameCardEventNotifier;
extern Handle fsGameCardEventHandle;
extern Event fsGameCardKernelEvent;
extern UEvent exitEvent;

extern bool gameCardInserted;

extern char strbuf[NAME_BUF_LEN * 4];

extern nca_keyset_t nca_keyset;

int main(int argc, char *argv[])
{
    /* Initialize UI */
    if (!uiInit()) return -1;
    
    int ret = 0;
    Result result;
    
    /* Initialize the fsp-srv service */
    result = fsInitialize();
    if (R_SUCCEEDED(result))
    {
        /* Open device operator */
        result = fsOpenDeviceOperator(&fsOperatorInstance);
        if (R_SUCCEEDED(result))
        {
            /* Initialize the ncm service */
            result = ncmInitialize();
            if (R_SUCCEEDED(result))
            {
                /* Initialize the ns service */
                result = nsInitialize();
                if (R_SUCCEEDED(result))
                {
                    /* Initialize the time service */
                    result = timeInitialize();
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
                                                case resultShowXciDumpMenu:
                                                    uiSetState(stateXciDumpMenu);
                                                    break;
                                                case resultDumpXci:
                                                    uiSetState(stateDumpXci);
                                                    break;
                                                case resultShowNspDumpMenu:
                                                    uiSetState(stateNspDumpMenu);
                                                    break;
                                                case resultDumpNsp:
                                                    uiSetState(stateDumpNsp);
                                                    break;
                                                case resultShowRawPartitionDumpMenu:
                                                    uiSetState(stateRawPartitionDumpMenu);
                                                    break;
                                                case resultDumpRawPartition:
                                                    uiSetState(stateDumpRawPartition);
                                                    break;
                                                case resultShowPartitionDataDumpMenu:
                                                    uiSetState(statePartitionDataDumpMenu);
                                                    break;
                                                case resultDumpPartitionData:
                                                    uiSetState(stateDumpPartitionData);
                                                    break;
                                                case resultShowViewGameCardFsMenu:
                                                    uiSetState(stateViewGameCardFsMenu);
                                                    break;
                                                case resultShowViewGameCardFsGetList:
                                                    uiSetState(stateViewGameCardFsGetList);
                                                    break;
                                                case resultShowViewGameCardFsBrowser:
                                                    uiSetState(stateViewGameCardFsBrowser);
                                                    break;
                                                case resultViewGameCardFsBrowserCopyFile:
                                                    uiSetState(stateViewGameCardFsBrowserCopyFile);
                                                    break;
                                                case resultDumpGameCardCertificate:
                                                    uiSetState(stateDumpGameCardCertificate);
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
                                        uiDrawString(strbuf, 0, 0, 255, 255, 255);
                                        uiRefreshDisplay();
                                        delay(5);
                                        ret = -10;
                                    }
                                    
                                    /* Close gamecard detection thread */
                                    threadClose(&thread);
                                } else {
                                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to create gamecard detection thread! (0x%08X)", result);
                                    uiDrawString(strbuf, 0, 0, 255, 255, 255);
                                    uiRefreshDisplay();
                                    delay(5);
                                    ret = -9;
                                }
                                
                                /* Close gamecard detection kernel event */
                                eventClose(&fsGameCardKernelEvent);
                            } else {
                                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to retrieve gamecard detection event handle! (0x%08X)", result);
                                uiDrawString(strbuf, 0, 0, 255, 255, 255);
                                uiRefreshDisplay();
                                delay(5);
                                ret = -8;
                            }
                            
                            /* Close gamecard detection event notifier */
                            fsEventNotifierClose(&fsGameCardEventNotifier);
                        } else {
                            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open gamecard detection event notifier! (0x%08X)", result);
                            uiDrawString(strbuf, 0, 0, 255, 255, 255);
                            uiRefreshDisplay();
                            delay(5);
                            ret = -7;
                        }
                        
                        /* Denitialize the time service */
                        timeExit();
                    } else {
                        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the time service! (0x%08X)", result);
                        uiDrawString(strbuf, 0, 0, 255, 255, 255);
                        uiRefreshDisplay();
                        delay(5);
                        ret = -6;
                    }
                    
                    /* Denitialize the ns service */
                    nsExit();
                } else {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the ns service! (0x%08X)", result);
                    uiDrawString(strbuf, 0, 0, 255, 255, 255);
                    uiRefreshDisplay();
                    delay(5);
                    ret = -5;
                }
                
                /* Denitialize the ncm service */
                ncmExit();
            } else {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the ncm service! (0x%08X)", result);
                uiDrawString(strbuf, 0, 0, 255, 255, 255);
                uiRefreshDisplay();
                delay(5);
                ret = -4;
            }
            
            /* Close device operator */
            fsDeviceOperatorClose(&fsOperatorInstance);
        } else {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to open device operator! (0x%08X)", result);
            uiDrawString(strbuf, 0, 0, 255, 255, 255);
            uiRefreshDisplay();
            delay(5);
            ret = -3;
        }
        
        /* Denitialize the fs-srv service */
        fsExit();
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to initialize the fsp-srv service! (0x%08X)", result);
        uiDrawString(strbuf, 0, 0, 255, 255, 255);
        uiRefreshDisplay();
        delay(5);
        ret = -2;
    }
    
    /* Free gamecard resources */
    freeGameCardInfo();
    
    /* Deinitialize UI */
    uiDeinit();
    
    return ret;
}
