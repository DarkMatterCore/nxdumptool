#include "dumper.h"
#include "fsext.h"
#include <stdio.h>
#include <malloc.h>
#include "ccolor.h"
#include "util.h"

bool dumpPartitionRaw(FsDeviceOperator* fsOperator, u32 partition) {
    u32 handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle))) {
        printf("GetGameCardHandle failed\n");
        return false;
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
        if (((off / bufs) % 10) == 0) {
            printf(C_CLEAR_LINE "\rDumping %i%% [%li / %li bytes]", (int) (off * 100 / size), off, size);
            syncDisplay();
        }
    }
    free(buf);
    printf(C_CLEAR_LINE "\rDone!\n");
    syncDisplay();

    fclose(outFile);

    fsStorageClose(&gameCardStorage);

    return true;
}