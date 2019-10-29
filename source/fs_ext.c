#include <switch/kernel/ipc.h>
#include <stdlib.h>
#include <string.h>

#include <switch/services/fs.h>
#include "fs_ext.h"

// IFileSystemProxy
Result fsOpenGameCardStorage(FsStorage* out, const FsGameCardHandle* handle, u32 partition)
{
    struct {
        u32 handle;
        u32 partition;
    } in = { handle->value, partition };

    return serviceDispatchIn(fsGetServiceSession(), 30, in,
        .out_num_objects = 1,
        .out_objects = out);
}

Result fsOpenGameCardDetectionEventNotifier(FsEventNotifier* out)
{
    return serviceDispatch(fsGetServiceSession(), 501,
        .out_num_objects = 1,
        .out_objects = out);
}

// IDeviceOperator
Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator* d, const FsGameCardHandle* handle, u32* out_title_version, u64* out_title_id)
{
    struct {
        u32 handle;
    } in = { handle->value };

    struct {
        u32 title_ver;
        u64 title_id;
    } out;

    Result rc = serviceDispatchInOut(&d->s, 203, in, out);

    if (R_SUCCEEDED(rc) && out_title_version) *out_title_version = out.title_ver;
    if (R_SUCCEEDED(rc) && out_title_id) *out_title_id = out.title_id;
    
    return rc;
}
