#pragma once

#ifndef __FS_EXT_H__
#define __FS_EXT_H__

#include <switch/types.h>
#include <switch/services/fs.h>

// IFileSystemProxy
Result fsOpenGameCardStorage(FsStorage* out, const FsGameCardHandle* handle, u32 partition);
Result fsOpenGameCardDetectionEventNotifier(FsEventNotifier* out);

// IDeviceOperator
Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator* d, const FsGameCardHandle* handle, u32* out_title_version, u64* out_title_id);

#endif
