#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

//#include "lvgl_helper.h"
#include "utils.h"
#include "gamecard.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    int ret = 0;
    
    LOGFILE("nxdumptool starting.");
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    /*lv_test();
    
    while(appletMainLoop())
    {
        lv_task_handler();
        if (lvglHelperGetExitFlag()) break;
    }*/
    
    
    
    
    
    consoleInit(NULL);
    
    printf("waiting...\n");
    consoleUpdate(NULL);
    
    while(appletMainLoop())
    {
        if (gamecardIsReady()) break;
    }
    
    u64 size = 0;
    if (!gamecardGetTotalRomSize(&size))
    {
        printf("totalromsize failed");
        goto out2;
    }
    
    printf("totalromsize: 0x%lX\n", size);
    consoleUpdate(NULL);
    
    if (!gamecardGetTrimmedRomSize(&size))
    {
        printf("trimmedromsize failed");
        goto out2;
    }
    
    printf("trimmedromsize: 0x%lX\n", size);
    
out2:
    consoleUpdate(NULL);
    SLEEP(3);
    consoleExit(NULL);
    
out:
    utilsCloseResources();
    
    return ret;
}
