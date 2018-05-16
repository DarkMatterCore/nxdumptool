#include <stdio.h>

#include <malloc.h>
#include <switch.h>
#include <memory.h>
#include "menu.h"
#include "dumper.h"
#include "ccolor.h"
#include "filebrowser.h"

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

void dumpPartitionRawMenuItem(MenuItem* item) {
    u32 partition = (u32) (size_t) item->userdata;

    char titlebuf[64];
    sprintf(titlebuf, "Dump Partition %i (Raw)", partition);
    startOperation(titlebuf);
    if (partition == 0 || partition == 1)
        workaroundPartitionZeroAccess(&fsOperatorInstance);
    dumpPartitionRaw(&fsOperatorInstance, partition);
    menuWaitForAnyButton();
}

void dumpPartitionDataMenuItem(MenuItem* item) {
    u32 partition = (u32) (size_t) item->userdata;

    char titlebuf[64];
    sprintf(titlebuf, "Dump Partition %i", partition);
    startOperation(titlebuf);
    if (partition == 0 || partition == 1)
        workaroundPartitionZeroAccess(&fsOperatorInstance);
    FsFileSystem fs;
    if (openPartitionFs(&fs, &fsOperatorInstance, partition) &&
            fsdevMountDevice("gamecard", fs) != -1) {
        char outbuf[64];
        sprintf(outbuf, "/dump_%u", partition);
        printf("Copying to %s\n", outbuf);
        if (copyDirectory("gamecard:/", outbuf)) {
            printf("Done!\n");
        }
    }
    fsdevUnmountDevice("dump");
    menuWaitForAnyButton();
}

void openMainMenu();
void viewPartitionExitCb() {
    fsdevUnmountDevice("view");
    openMainMenu();
}
void viewPartitionMenuItem(MenuItem* item) {
    u32 partition = (u32) (size_t) item->userdata;

    startOperation("View Files");
    if (partition == 0 || partition == 1)
        workaroundPartitionZeroAccess(&fsOperatorInstance);
    FsFileSystem fs;
    if (!openPartitionFs(&fs, &fsOperatorInstance, partition)) {
        menuWaitForAnyButton();
        return;
    }
    if (fsdevMountDevice("view", fs) == -1) {
        printf("fsdevMountDevice failed\n");
        menuWaitForAnyButton();
        return;
    }
    printFilesInDir("view://", "view://", viewPartitionExitCb);
}


MenuItem mainMenu[] = {
        { .text = "Dump Partition 0 (SysUpdate)", .callback = dumpPartitionDataMenuItem, .userdata = (void*) 0 },
        { .text = "Dump Partition 1 (Normal)", .callback = dumpPartitionDataMenuItem, .userdata = (void*) 1 },
        { .text = "Dump Partition 2 (Secure)", .callback = dumpPartitionDataMenuItem, .userdata = (void*) 2 },
        { .text = "Raw Dump Partition 0 (SysUpdate)", .callback = dumpPartitionRawMenuItem, .userdata = (void*) 0 },
        { .text = "Raw Dump Partition 0 (Normal)", .callback = dumpPartitionRawMenuItem, .userdata = (void*) 1 },
        { .text = "Raw Dump Partition 0 (Secure)", .callback = dumpPartitionRawMenuItem, .userdata = (void*) 2 },
        { .text = "View files on Game Card (SysUpdate)", .callback = viewPartitionMenuItem, .userdata = (void*) 0 },
        { .text = "View files on Game Card (Normal)", .callback = viewPartitionMenuItem, .userdata = (void*) 1 },
        { .text = "View files on Game Card (Secure)", .callback = viewPartitionMenuItem, .userdata = (void*) 2 },
        { .text = NULL }
};
void openMainMenu() {
    menuSetCurrent(mainMenu, menuExit);
}



int main(int argc, char **argv) {
    gfxInitDefault();
    consoleInit(NULL);

    if (R_FAILED(fsOpenDeviceOperator(&fsOperatorInstance))) {
        printf("Failed to open device operator\n");
        return -1;
    }
    openMainMenu();

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

