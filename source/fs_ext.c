#include <switch/kernel/ipc.h>
#include <stdlib.h>
#include <string.h>

#include "fs_ext.h"

// IFileSystemProxy
Result fsOpenGameCardStorage(FsStorage* out, const FsGameCardHandle* handle, u32 partition)
{
    IpcCommand c;
    ipcInitialize(&c);
    
    struct {
        u64 magic;
        u64 cmd_id;
        u32 handle;
        u32 partition;
    } *raw;
    
    raw = serviceIpcPrepareHeader(fsGetServiceSession(), &c, sizeof(*raw));
    
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 30;
    raw->handle = handle->value;
    raw->partition = partition;
    
    Result rc = serviceIpcDispatch(fsGetServiceSession());
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        
        struct {
            u64 magic;
            u64 result;
        } *resp;
        
        serviceIpcParse(fsGetServiceSession(), &r, sizeof(*resp));
        resp = r.Raw;
        
        rc = resp->result;
        
        if (R_SUCCEEDED(rc)) serviceCreateSubservice(&out->s, fsGetServiceSession(), &r, 0);
    }
    
    return rc;
}

Result fsOpenGameCardDetectionEventNotifier(FsEventNotifier* out)
{
    IpcCommand c;
    ipcInitialize(&c);
    
    struct {
        u64 magic;
        u64 cmd_id;
    } *raw;
    
    raw = serviceIpcPrepareHeader(fsGetServiceSession(), &c, sizeof(*raw));
    
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 501;
    
    Result rc = serviceIpcDispatch(fsGetServiceSession());
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        
        struct {
            u64 magic;
            u64 result;
        } *resp;
        
        serviceIpcParse(fsGetServiceSession(), &r, sizeof(*resp));
        resp = r.Raw;
        
        rc = resp->result;
        
        if (R_SUCCEEDED(rc)) serviceCreateSubservice(&out->s, fsGetServiceSession(), &r, 0);
    }
    
    return rc;
}

// IDeviceOperator
Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator* d, const FsGameCardHandle* handle, u32* out_title_version, u64* out_title_id)
{
    IpcCommand c;
    ipcInitialize(&c);
    
    struct {
        u64 magic;
        u64 cmd_id;
        u32 handle;
    } *raw;
    
    raw = serviceIpcPrepareHeader(&d->s, &c, sizeof(*raw));
    
    raw->magic = SFCI_MAGIC;
    raw->cmd_id = 203;
    raw->handle = handle->value;
    
    Result rc = serviceIpcDispatch(&d->s);
    
    if (R_SUCCEEDED(rc))
    {
        IpcParsedCommand r;
        
        struct {
            u64 magic;
            u64 result;
            u32 title_ver;
            u64 title_id;
        } *resp;
        
        serviceIpcParse(&d->s, &r, sizeof(*resp));
        resp = r.Raw;
        
        rc = resp->result;
        
        if (R_SUCCEEDED(rc))
        {
            if (out_title_version != NULL) *out_title_version = resp->title_ver;
            if (out_title_id != NULL) *out_title_id = resp->title_id;
        }
    }
    
    return rc;
}
