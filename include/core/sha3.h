/*
 * sha3.h
 *
 * Copyright (c) Atmosphère-NX.
 * Copyright (c) 2023-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 * Loosely based on crypto_sha3_impl.hpp from Atmosphere-libs.
 *
 * nxdumptool is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * nxdumptool is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __SHA3_H__
#define __SHA3_H__

#include <switch.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef SHA3_INTERNAL_STATE_SIZE
#define SHA3_INTERNAL_STATE_SIZE   200
#endif

#ifndef SHA3_HASH_SIZE_BYTES
#define SHA3_HASH_SIZE_BYTES(bits) ((bits) / 8)
#endif

#ifndef SHA3_BLOCK_SIZE
#define SHA3_BLOCK_SIZE(bits)      (SHA3_INTERNAL_STATE_SIZE - (2 * SHA3_HASH_SIZE_BYTES(bits)))
#endif

#define _SHA3_CTX_OPS(bits) \
void sha3##bits##ContextCreate(Sha3Context *out); \
void sha3##bits##CalculateHash(void *dst, const void *src, size_t size);

/// Context for SHA3 operations.
typedef struct {
    size_t hash_size;
    size_t block_size;
    size_t buffered_bytes;
    u64 internal_state[SHA3_INTERNAL_STATE_SIZE / sizeof(u64)];
    bool finalized;
} Sha3Context;

/// SHA3-224 context creation and simple all-in-one calculation functions.
_SHA3_CTX_OPS(224);

/// SHA3-256 context creation and simple all-in-one calculation functions.
_SHA3_CTX_OPS(256);

/// SHA3-384 context creation and simple all-in-one calculation functions.
_SHA3_CTX_OPS(384);

/// SHA3-512 context creation and simple all-in-one calculation functions.
_SHA3_CTX_OPS(512);

/// Updates SHA3 context with data to hash.
void sha3ContextUpdate(Sha3Context *ctx, const void *src, size_t size);

/// Gets the context's output hash, finalizes the context.
void sha3ContextGetHash(Sha3Context *ctx, void *dst);

#undef _SHA3_CTX_OPS

#ifdef __cplusplus
}
#endif

#endif /* __SHA3_H__ */
