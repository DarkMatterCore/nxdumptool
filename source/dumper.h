#pragma once

#include <switch.h>

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator);
bool openPartitionFs(FsFileSystem* ret, FsDeviceOperator* fsOperator, u32 partition);
bool dumpPartitionRaw(FsDeviceOperator* fsOperator, u32 partition);