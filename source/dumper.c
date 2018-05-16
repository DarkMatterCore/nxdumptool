#include "dumper.h"
#include "fsext.h"
#include <stdio.h>
#include <malloc.h>
#include <dirent.h>
#include <memory.h>
#include <sys/stat.h>
#include "ccolor.h"
#include "util.h"

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator) {
    u32 handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle)))
        return;
    FsStorage gameCardStorage;
    if (R_FAILED(fsOpenGameCardStorage(&gameCardStorage, handle, 0)))
        return;
    fsStorageClose(&gameCardStorage);
}

bool openPartitionFs(FsFileSystem* ret, FsDeviceOperator* fsOperator, u32 partition) {
    u32 handle;
    if (R_FAILED(fsDeviceOperatorGetGameCardHandle(fsOperator, &handle))) {
        printf("GetGameCardHandle failed\n");
        return false;
    }
    Result result;
    if (R_FAILED(result = fsMountGameCard(ret, handle, partition))) {
        printf("MountGameCard failed %x\n", result);
        return false;
    }
    printf("Opened card\n");
    return true;
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
    if (R_FAILED(result = fsOpenGameCardStorage(&gameCardStorage, handle, partition))) {
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

bool copyFile(const char* source, const char* dest) {
    printf("Copying %s...", source);
    FILE* inFile = fopen(source, "rb");
    if (!inFile) {
        printf("\nFailed to open input file\n");
        return false;
    }
    FILE* outFile = fopen(dest, "wb");
    if (!outFile) {
        printf("\nFailed to open output file\n");
        fclose(outFile);
        return false;
    }

    fseek(inFile, 0L, SEEK_END);
    long int size = ftell(inFile);
    fseek(inFile, 0L, SEEK_SET);

    const size_t bufs = 1024 * 1024;
    char* buf = (char*) malloc(bufs);
    bool success = true;
    ssize_t n;
    size_t total = 0;
    size_t last_report = 0;
    printf(" [00%%]");
    syncDisplay();
    while (true) {
        n = fread(buf, 1, bufs, inFile);
        if (n <= 0) {
            if (feof(inFile))
                break;
            printf("\nRead error; retrying\n");
            continue;
        }
        if (fwrite(buf, 1, n, outFile) != n) {
            printf("\nWrite error\n");
            success = false;
            break;
        }
        total += n;
        if ((total - last_report) > 1024 * 1024) {
            hidScanInput();
            u64 kDown = hidKeysDown(CONTROLLER_P1_AUTO);
            if (kDown & KEY_B) {
                printf("\nCancelled\n");
                success = false;
                break;
            }

            int p = (int) (total * 100 / size);
            if (p >= 100)
                p = 99;
            printf("\b\b\b\b%02i%%]", p);
            syncDisplay();
            last_report = total;
        }
    }
    printf("\b\b\b\b\bDone!\n");
    free(buf);
    fclose(inFile);
    fclose(outFile);
    return success;
}

#define NAME_BUF_LEN 4096

bool _copyDirectory(char* sbuf, size_t source_len, char* dbuf, size_t dest_len) {
    DIR* dir = opendir(sbuf);
    struct dirent* ent;
    sbuf[source_len] = '/';
    dbuf[dest_len] = '/';
    while ((ent = readdir(dir)) != NULL) {
        size_t d_name_len = strlen(ent->d_name);
        if (source_len + 1 + d_name_len + 1 >= NAME_BUF_LEN ||
                dest_len + 1 + d_name_len + 1 >= NAME_BUF_LEN) {
            printf("Too long file name!\n");
            closedir(dir);
            return false;
        }
        strcpy(sbuf + source_len + 1, ent->d_name);
        strcpy(dbuf + dest_len + 1, ent->d_name);
        if (ent->d_type == DT_DIR) {
            mkdir(dbuf, 0744);
            if (!_copyDirectory(sbuf, source_len + 1 + d_name_len, dbuf, dest_len + 1 + d_name_len)) {
                closedir(dir);
                return false;
            }
        } else {
            if (!copyFile(sbuf, dbuf)) {
                closedir(dir);
                return false;
            }
        }
    }
    return true;
}

bool copyDirectory(const char* source, const char* dest) {
    char sbuf[NAME_BUF_LEN];
    char dbuf[NAME_BUF_LEN];
    size_t source_len = strlen(source);
    size_t dest_len = strlen(dest);
    if (source_len + 1 >= NAME_BUF_LEN) {
        printf("Directory name too long %li: %s\n", source_len + 1, source);
        return false;
    }
    if (dest_len + 1 >= NAME_BUF_LEN) {
        printf("Directory name too long %li: %s\n", dest_len + + 1, dest);
        return false;
    }
    strcpy(sbuf, source);
    strcpy(dbuf, dest);
    mkdir(dbuf, 0744);
    return _copyDirectory(sbuf, source_len, dbuf, dest_len);
}