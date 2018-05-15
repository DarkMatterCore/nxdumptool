#pragma once

#include <switch/types.h>
#include <switch/services/fs.h>

// IFileSystemProxy
Result fsOpenGameCard(FsStorage* out, u32 handle, u32 partition);


// IDeviceOperator
Result fsDeviceOperatorIsGameCardInserted(FsDeviceOperator* d, bool* out);
Result fsDeviceOperatorGetGameCardHandle(FsDeviceOperator* d, u32* out);


// FsStorage
Result fsStorageGetSize(FsStorage* s, u64* out);