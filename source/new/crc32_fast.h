#pragma once

#ifndef __CRC32_FAST_H__
#define __CRC32_FAST_H__

#include <switch/types.h>

void crc32(const void *data, u64 n_bytes, u32 *crc);

#endif /* __CRC32_FAST_H__ */
