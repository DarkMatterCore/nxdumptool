#include <string.h>
#include <stdio.h>
#include <malloc.h>

#include <switch.h>
#include "fsext.h"

bool isGameCardInserted(FsDeviceOperator* o) {
    bool inserted;
    if (R_FAILED(fsDeviceOperatorIsGameCardInserted(o, &inserted)))
        return false;
    return inserted;
}

bool doLogic(FsDeviceOperator* fsOperator) {
    bool cardInserted = isGameCardInserted(fsOperator);
    printf("IsGameCardInserted = %i\n", cardInserted);
    if (!cardInserted)
        return false;

    u32 handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle))) {
        printf("GetGameCardHandle failed\n");
        return false;
    }
    FsStorage gameCardStorage;
    Result result;
    if (R_FAILED(result = fsOpenGameCard(&gameCardStorage, handle, 0))) {
        printf("MountGameCard failed %i\n", result);
        return false;
    }
    printf("Opened card\n");

    u64 size;
    fsStorageGetSize(&gameCardStorage, &size);
    printf("Size = %li\n", size);
    FILE* outFile = fopen("out.bin", "wb");
    printf("Opened output file\n");

    const size_t bufs = 1024 * 1024;
    char* buf = (char*) malloc(bufs);
    for (u64 off = 0; off < size; off += bufs) {
        u64 n = bufs;
        if (size - off < n)
            n = size - off;
        if (R_FAILED(result = fsStorageRead(&gameCardStorage, off, buf, n))) {
            printf("fsStorageRead error\n");
            fsStorageClose(&gameCardStorage);
            return false;
        }
        if (fwrite(buf, 1, n, outFile) != n) {
            printf("fwrite error\n");
            fsStorageClose(&gameCardStorage);
            return false;
        }
        printf("Dumping %i%% [%li / %li]\n", (int) (off * 100 / size), off, size);
    }
    free(buf);

    fclose(outFile);

    fsStorageClose(&gameCardStorage);

    printf("Done!\n");

    return true;
}

int main(int argc, char **argv)
{
    gfxInitDefault();
    consoleInit(NULL);

    FsDeviceOperator fsOperator;
    if (R_FAILED(fsOpenDeviceOperator(&fsOperator))) {
        printf("Failed to open device operator\n");
        return -1;
    }

    doLogic(&fsOperator);

    fsDeviceOperatorClose(&fsOperator);


    while(appletMainLoop())
    {
        hidScanInput();

        //

        u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);

        if (kDown & KEY_PLUS) break;

        gfxFlushBuffers();
        gfxSwapBuffers();
        gfxWaitForVsync();
    }

    gfxExit();
    return 0;
}

