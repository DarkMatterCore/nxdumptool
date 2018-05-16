#include <stdio.h>

#include <malloc.h>
#include <switch.h>
#include <dirent.h>
#include <memory.h>
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

void dumpPartitionZero(MenuItem* item) {
    startOperation("Raw Dump Partition 0 (SysUpdate)");
    workaroundPartitionZeroAccess(&fsOperatorInstance);
    dumpPartitionRaw(&fsOperatorInstance, 0);
    menuWaitForAnyButton();
}

void printFilesInDirMenuItem(MenuItem* item);

MenuItem mainMenu[] = {
        { .text = "Raw Dump Partition 0 (SysUpdate)", .callback = dumpPartitionZero },
        { .text = "Print files on SD Card", .callback = printFilesInDirMenuItem, .userdata = "/" },
        { .text = NULL }
};


MenuItem* currentFileListBuf;

void freeCurrentFileListBuf() {
    MenuItem* ptr = currentFileListBuf;
    while (ptr != NULL) {
        free(ptr->text);
        if (ptr->userdata != NULL)
            free(ptr->userdata);
        ptr++;
    }
    free(currentFileListBuf);
}
void exitFileList() {
    freeCurrentFileListBuf();
    menuSetCurrent(mainMenu, menuExit);
}
char* getParentDir(const char* path) {
    char* ptr = strrchr(path, '/');
    if (ptr == NULL || ptr == path) // not found or first character
        return NULL;
    char* retval = (char*) malloc(ptr - path + 1);
    memcpy(retval, path, ptr - path);
    retval[ptr - path] = '\0';
    return retval;
}
char* pathJoin(const char* p1, const char* p2) {
    size_t p1s = strlen(p1);
    if (p1s == 0)
        return strdup(p2);
    size_t p2s = strlen(p2);
    char* retval = (char*) malloc(p1s + 1 + p2s + 1);
    memcpy(retval, p1, p1s);
    retval[p1s] = '/';
    memcpy(&retval[p1s + 1], p2, p2s + 1); // copy with null terminator
    return retval;
}
void printFilesInDir(const char* path) {
    int maxMenuItemCount = 48;
    MenuItem* buf = (MenuItem*) malloc(sizeof(MenuItem) * (maxMenuItemCount + 1));
    currentFileListBuf = buf;
    DIR* dir = opendir(path);
    struct dirent* ent;
    int bufi = 0;
    char* parentDir = getParentDir(path);
    if (parentDir != NULL) {
        buf[bufi].userdata = parentDir;
        buf[bufi].text = strdup("..");
        buf[bufi].callback = printFilesInDirMenuItem;
        bufi++;
    }
    while ((ent = readdir(dir)) != NULL) {
        if (ent->d_type == DT_DIR) {
            buf[bufi].text = pathJoin(ent->d_name, "");
            buf[bufi].userdata = pathJoin(path, ent->d_name);
            buf[bufi].callback = printFilesInDirMenuItem;
        } else {
            buf[bufi].text = strdup(ent->d_name);
            buf[bufi].userdata = buf[bufi].callback = NULL;
        }
        if (++bufi >= maxMenuItemCount)
            break;
    }
    buf[bufi].text = NULL;
    closedir(dir);
    menuSetCurrent(buf, exitFileList);
}

void printFilesInDirMenuItem(MenuItem* item) {
    printFilesInDir(item->userdata);
}


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

