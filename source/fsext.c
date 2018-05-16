#include "fsext.h"

#include <switch/kernel/ipc.h>


// IFileSystemProxy
Result fsOpenGameCardStorage(FsStorage* out, u32 handle, u32 partition) {
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
        u32 handle;
        u32 partition;
    } PACKED *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 30;
    raw->handle = handle;
    raw->partition = partition;

    Result rc = serviceIpcDispatch(fsGetServiceSession());

    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);

        struct {
            u64 magic;
            u64 result;
        } *resp = r.Raw;

        rc = resp->result;

        if (R_SUCCEEDED(rc)) {
            serviceCreate(&out->s, r.Handles[0]);
        }
    }

    return rc;
}
Result fsMountGameCard(FsFileSystem* out, u32 handle, u32 partition) {
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
        u32 handle;
        u32 partition;
    } PACKED *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 31;
    raw->handle = handle;
    raw->partition = partition;

    Result rc = serviceIpcDispatch(fsGetServiceSession());

    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);

        struct {
            u64 magic;
            u64 result;
        } *resp = r.Raw;

        rc = resp->result;

        if (R_SUCCEEDED(rc)) {
            serviceCreate(&out->s, r.Handles[0]);
        }
    }

    return rc;
}


// IDeviceOperator
Result fsDeviceOperatorIsGameCardInserted(FsDeviceOperator* d, bool* out) {
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 200;

    Result rc = serviceIpcDispatch(&d->s);

    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);

        struct {
            u64 magic;
            u64 result;
            u8 is_inserted;
        } *resp = r.Raw;

        rc = resp->result;

        if (R_SUCCEEDED(rc)) {
            *out = resp->is_inserted != 0;
        }
    }

    return rc;
}

Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator* d, u32 handle, u32* out_title_version, u64* out_title_id) {
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
        u32 handle;
    } *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 203;
    raw->handle = handle;

    Result rc = serviceIpcDispatch(&d->s);

    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);

        struct {
            u64 magic;
            u64 result;
            u32 title_ver;
            u64 title_id;
        } *resp = r.Raw;

        rc = resp->result;

        if (R_SUCCEEDED(rc)) {
            if (out_title_version != NULL)
                *out_title_version = resp->title_ver;
            if (out_title_id != NULL)
                *out_title_id = resp->title_id;
        }
    }

    return rc;
}

Result fsDeviceOperatorGetGameCardHandle(FsDeviceOperator* d, u32* out) {
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 202;

    Result rc = serviceIpcDispatch(&d->s);

    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);

        struct {
            u64 magic;
            u64 result;
            u32 handle;
        } *resp = r.Raw;

        rc = resp->result;

        if (R_SUCCEEDED(rc)) {
            *out = resp->handle;
        }
    }

    return rc;
}


// FsStorage
Result fsStorageGetSize(FsStorage* s, u64* out) {
    IpcCommand c;
    ipcInitialize(&c);

    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;

    raw = ipcPrepareHeader(&c, sizeof(*raw));

    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 4;

    Result rc = serviceIpcDispatch(&s->s);

    if (R_SUCCEEDED(rc)) {
        IpcParsedCommand r;
        ipcParse(&r);

        struct {
            u64 magic;
            u64 result;
            u64 size;
        } *resp = r.Raw;

        rc = resp->result;

        if (R_SUCCEEDED(rc)) {
            *out = resp->size;
        }
    }

    return rc;
}