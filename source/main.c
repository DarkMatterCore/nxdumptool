#include <stdio.h>

#include <switch.h>
#include "menu.h"
#include "dumper.h"
#include "ccolor.h"

FsDeviceOperator fsOperatorInstance;

bool shouldExit = false;
bool shouldWaitForAnyButton = false;

void menuExit() {
    shouldExit = true;
}

void menuWaitForAnyButton() {
    printf(C_DIM "Press any button to return to menu\n");
    shouldWaitForAnyButton = true;
}

void startOperation(const char* title) {
    consoleClear();
    printf(C_DIM "%s\n\n" C_RESET, title);
}

void dumpPartitionZero() {
    startOperation("Raw Dump Partition 0 (SysUpdate)");
    workaroundPartitionZeroAccess(&fsOperatorInstance);
    dumpPartitionRaw(&fsOperatorInstance, 0);
    menuWaitForAnyButton();
}

MenuItem mainMenu[] = {
        { .text = "Raw Dump Partition 0 (SysUpdate)", .callback = dumpPartitionZero },
        { .text = NULL }
};

int main(int argc, char **argv) {
    gfxInitDefault();
    consoleInit(NULL);

    if (R_FAILED(fsOpenDeviceOperator(&fsOperatorInstance))) {
        printf("Failed to open device operator\n");
        return -1;
    }

    menuSetCurrent(mainMenu, menuExit);

    while(appletMainLoop())
    {
        bool btnWait = shouldWaitForAnyButton;

        hidScanInput();

        if (!btnWait)
            menuUpdate(&fsOperatorInstance);

        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
        if (kDown & KEY_PLUS) break;
        if (btnWait && kDown) {
            shouldWaitForAnyButton = false;
            menuPrint();
        }
        if (shouldExit)
            break;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }

    fsDeviceOperatorClose(&fsOperatorInstance);

    gfxExit();
    return 0;
}

