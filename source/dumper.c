#include "dumper.h"
#include "fsext.h"
#include <stdio.h>
#include <malloc.h>
#include "ccolor.h"
#include "util.h"

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator) {
    u32 handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
        return;
    FsStorage gameCardStorage;
    if (R_FAILED(fsOpenGameCard(&gameCardStorage, handle, 0)))
        return;
    fsStorageClose(&gameCardStorage);
}

bool dumpPartitionRaw(FsDeviceOperator* fsOperator, u32 partition) {
    u32 handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle))) {
        printf("GetGameCardHandle failed\n");
        return false;
    }

    printf("Handle = %x\n", handle);

    if (partition == 0) {
        u32 title_ver;
        fsDeviceOperatorUpdatePartitionInfo(fsOperator, handle, &title_ver, NULL);
        printf("System title-version = %i\n", title_ver);
    }

    FsStorage gameCardStorage;
    Result result;
    if (R_FAILED(result = fsOpenGameCard(&gameCardStorage, handle, partition))) {
        printf("MountGameCard failed %x\n", result);
        return false;
    }
    printf("Opened card\n");

    u64 size;
    fsStorageGetSize(&gameCardStorage, &size);
    printf("Total size = %li\n", size);
    FILE* outFile = fopen("out.bin", "wb");
    if (!outFile) {
        printf("Failed to open output file!\n");
        fsStorageClose(&gameCardStorage);
        return false;
    }

    printf("Starting...");
    syncDisplay();

    const size_t bufs = 1024 * 1024;
    char* buf = (char*) malloc(bufs);
    bool success = true;
    for (u64 off = 0; off < size; off += bufs) {
        u64 n = bufs;
        if (size - off < n)
            n = size - off;
        if (R_FAILED(result = fsStorageRead(&gameCardStorage, off, buf, n))) {
            printf("\nfsStorageRead error\n");
            success = false;
            break;
        }
        if (fwrite(buf, 1, n, outFile) != n) {
            printf("\nfwrite error\n");
            success = false;
            break;
        }
        if (((off / bufs) % 10) == 0) {
            hidScanInput();
            u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
            if (kDown & KEY_B) {
                printf("\nCancelled\n");
                success = false;
                break;
            }

            printf(C_CLEAR_LINE "\rDumping %i%% [%li / %li bytes]", (int) (off * 100 / size), off, size);
            syncDisplay();
        }
    }
    if (success) {
        printf(C_CLEAR_LINE "\rDone!\n");
        syncDisplay();
    }

    free(buf);
    fclose(outFile);
    fsStorageClose(&gameCardStorage);

    return success;
}