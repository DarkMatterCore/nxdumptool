#pragma once

#include <switch.h>

void workaroundPartitionZeroAccess(FsDeviceOperator* fsOperator);
bool dumpPartitionRaw(FsDeviceOperator* fsOperator, u32 partition);