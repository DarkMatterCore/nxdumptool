#pragma once

#ifndef __FSEXT_H__
#define __FSEXT_H__

#include <switch/types.h>
#include <switch/services/fs.h>

// IFileSystemProxy
Result fsOpenGameCardStorage(FsStorage* out, u32 handle, u32 partition);
Result fsOpenGameCardFileSystem(FsFileSystem* out, u32 handle, u32 partition);

// IDeviceOperator
Result fsDeviceOperatorIsGameCardInserted(FsDeviceOperator* d, bool* out);
Result fsDeviceOperatorGetGameCardHandle(FsDeviceOperator* d, u32* out);
Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator* d, u32 handle, u32* out_title_version, u64* out_title_id);

#endif
