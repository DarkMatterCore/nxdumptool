#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "ui.h"
#include "util.h"

int main(int argc, char *argv[])
{
    int ret = 0;
    bool exitMainLoop = false;
    
    /* Initialize application resources */
    if (!initApplicationResources(argc, argv))
    {
        ret = -1;
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
    
out:
    /* Deinitialize application resources */
    deinitApplicationResources();
    
    return ret;
}
