#include "filebrowser.h"
#include <dirent.h>
#include <string.h>
#include <stdlib.h>

static MenuItem* currentFileListBuf;
static const char* currentBasePath;
static void (*currentExitCb)();

static void freeCurrentFileListBuf() {
    MenuItem* ptr = currentFileListBuf;
    while (ptr != NULL) {
        free(ptr->text);
        if (ptr->userdata != NULL)
            free(ptr->userdata);
        ptr++;
    }
    free(currentFileListBuf);
}
static void exitFileList() {
    freeCurrentFileListBuf();
    currentBasePath = NULL;
    currentExitCb();
}
static char* getParentDir(const char* path, const char* basePath) {
    char* ptr = strrchr(path, '/');
    if (ptr == NULL || (ptr - path < strlen(basePath) && strcmp(path, basePath) == 0))
        return NULL;
    char* retval = (char*) malloc(ptr - path + 1);
    memcpy(retval, path, ptr - path);
    retval[ptr - path] = '\0';
    return retval;
}
static char* pathJoin(const char* p1, const char* p2) {
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
static void printFilesInDirMenuItem(MenuItem* item);
void printFilesInDir(const char* path, const char* basePath, void (*exitCb)()) {
    int maxMenuItemCount = 48;
    MenuItem* buf = (MenuItem*) malloc(sizeof(MenuItem) * (maxMenuItemCount + 1));
    currentFileListBuf = buf;
    currentBasePath = basePath;
    currentExitCb = exitCb;
    DIR* dir = opendir(path);
    struct dirent* ent;
    int bufi = 0;
    char* parentDir = getParentDir(path, basePath);
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

static void printFilesInDirMenuItem(MenuItem* item) {
    printFilesInDir(item->userdata, currentBasePath, currentExitCb);
}
