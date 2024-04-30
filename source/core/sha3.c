/*
 * sha3.c
 *
 * Copyright (c) Atmosphère-NX.
 * Copyright (c) 2023-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 * Loosely based on crypto_sha3_impl.cpp from Atmosphere-libs.
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

#include <core/nxdt_utils.h>
#include <core/sha3.h>

#define SHA3_NUM_ROUNDS 24

#define _SHA3_CTX_OPS(bits) \
void sha3##bits##ContextCreate(Sha3Context *out) { \
    sha3ContextCreate(out, bits); \
} \
void sha3##bits##CalculateHash(void *dst, const void *src, size_t size) { \
    Sha3Context ctx; \
    sha3##bits##ContextCreate(&ctx); \
    sha3ContextUpdate(&ctx, src, size); \
    sha3ContextGetHash(&ctx, dst); \
}

/* Global constants. */

static const u64 g_iotaRoundConstant[SHA3_NUM_ROUNDS] = {
    0x0000000000000001, 0x0000000000008082,
    0x800000000000808A, 0x8000000080008000,
    0x000000000000808B, 0x0000000080000001,
    0x8000000080008081, 0x8000000000008009,
    0x000000000000008A, 0x0000000000000088,
    0x0000000080008009, 0x000000008000000A,
    0x000000008000808B, 0x800000000000008B,
    0x8000000000008089, 0x8000000000008003,
    0x8000000000008002, 0x8000000000000080,
    0x000000000000800A, 0x800000008000000A,
    0x8000000080008081, 0x8000000000008080,
    0x0000000080000001, 0x8000000080008008
};

static const int g_rhoShiftBit[SHA3_NUM_ROUNDS] = {
     1,  3,  6, 10, 15, 21, 28, 36,
    45, 55,  2, 14, 27, 41, 56,  8,
    25, 43, 62, 18, 39, 61, 20, 44
};

static const int g_rhoNextIndex[SHA3_NUM_ROUNDS] = {
    10,  7, 11, 17, 18,  3,  5, 16,
     8, 21, 24,  4, 15, 23, 19, 13,
    12,  2, 20, 14, 22,  9,  6,  1
};

static const u64 g_finalMask = 0x8000000000000000;

/* Function prototypes. */

static u64 rotl_u64(u64 x, int s);
static u64 rotr_u64(u64 x, int s);

static void sha3ContextCreate(Sha3Context *out, u32 hash_size);

static void sha3ProcessBlock(Sha3Context *ctx);
static void sha3ProcessLastBlock(Sha3Context *ctx);

void sha3ContextUpdate(Sha3Context *ctx, const void *src, size_t size)
{
    if (!ctx || !src || !size || ctx->finalized)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    const u8 *src_u8 = (u8*)src;
    size_t remaining = size;

    /* Process we have anything buffered. */
    if (ctx->buffered_bytes > 0)
    {
        /* Determine how much we can copy. */
        const size_t copy_size = MIN(ctx->block_size - ctx->buffered_bytes, remaining);

        /* Mix the bytes into our state. */
        u8 *dst = (((u8*)ctx->internal_state) + ctx->buffered_bytes);
        for(size_t i = 0; i < copy_size; ++i) dst[i] ^= src_u8[i];

        /* Advance. */
        src_u8 += copy_size;
        remaining -= copy_size;
        ctx->buffered_bytes += copy_size;

        /* Process a block, if we filled one. */
        if (ctx->buffered_bytes == ctx->block_size)
        {
            sha3ProcessBlock(ctx);
            ctx->buffered_bytes = 0;
        }
    }

    /* Process blocks, if we have any. */
    while(remaining >= ctx->block_size)
    {
        /* Mix the bytes into our state. */
        u8 *dst = (u8*)ctx->internal_state;
        for(size_t i = 0; i < ctx->block_size; ++i) dst[i] ^= src_u8[i];

        sha3ProcessBlock(ctx);

        src_u8 += ctx->block_size;
        remaining -= ctx->block_size;
    }

    /* Copy any leftover data to our buffer. */
    if (remaining > 0)
    {
        u8 *dst = (u8*)ctx->internal_state;
        for(size_t i = 0; i < remaining; ++i) dst[i] ^= src_u8[i];
        ctx->buffered_bytes = remaining;
    }
}

void sha3ContextGetHash(Sha3Context *ctx, void *dst)
{
    if (!ctx || !dst)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    /* If we need to, process the last block. */
    if (!ctx->finalized)
    {
        sha3ProcessLastBlock(ctx);
        ctx->finalized = true;
    }

    /* Copy the output hash. */
    memcpy(dst, ctx->internal_state, ctx->hash_size);
}

/* Functions for SHA3 context creation and simple all-in-one calculation. */

_SHA3_CTX_OPS(224);
_SHA3_CTX_OPS(256);
_SHA3_CTX_OPS(384);
_SHA3_CTX_OPS(512);

#undef _SHA3_CTX_OPS

static u64 rotl_u64(u64 x, int s)
{
    int N = (sizeof(u64) * 8);
    int r = (s % N);

    if (r == 0)
    {
        return x;
    } else
    if (r > 0)
    {
        return ((x << r) | (x >> (N - r)));
    }

    return rotr_u64(x, -r);
}

static u64 rotr_u64(u64 x, int s)
{
    int N = (sizeof(u64) * 8);
    int r = (s % N);

    if (r == 0)
    {
        return x;
    } else
    if (r > 0)
    {
        return ((x >> r) | (x << (N - r)));
    }

    return rotl_u64(x, -r);
}

static void sha3ContextCreate(Sha3Context *out, u32 hash_size)
{
    if (!out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return;
    }

    memset(out, 0, sizeof(Sha3Context));

    out->hash_size = SHA3_HASH_SIZE_BYTES(hash_size);
    out->block_size = SHA3_BLOCK_SIZE(hash_size);
}

static void sha3ProcessBlock(Sha3Context *ctx)
{
    u64 tmp = 0, C[5] = {0};

    /* Perform all rounds. */
    for(u8 round = 0; round < SHA3_NUM_ROUNDS; ++round)
    {
        /* Handle theta. */
        for(size_t i = 0; i < 5; ++i)
        {
            C[i] = (ctx->internal_state[i] ^ ctx->internal_state[i + 5] ^ ctx->internal_state[i + 10] ^ ctx->internal_state[i + 15] ^ ctx->internal_state[i + 20]);
        }

        for(size_t i = 0; i < 5; ++i)
        {
            tmp = (C[(i + 4) % 5] ^ rotl_u64(C[(i + 1) % 5], 1));
            for(size_t j = 0; j < 5; ++j) ctx->internal_state[(5 * j) + i] ^= tmp;
        }

        /* Handle rho/pi. */
        tmp = ctx->internal_state[1];
        for(size_t i = 0; i < SHA3_NUM_ROUNDS; ++i)
        {
            const int rho_next_idx = g_rhoNextIndex[i];
            C[0] = ctx->internal_state[rho_next_idx];
            ctx->internal_state[rho_next_idx] = rotl_u64(tmp, g_rhoShiftBit[i]);
            tmp = C[0];
        }

        /* Handle chi. */
        for(size_t i = 0; i < 5; ++i)
        {
            for(size_t j = 0; j < 5; ++j) C[j] = ctx->internal_state[(5 * i) + j];
            for(size_t j = 0; j < 5; ++j) ctx->internal_state[(5 * i) + j] ^= ((~C[(j + 1) % 5]) & C[(j + 2) % 5]);
        }

        /* Handle iota. */
        ctx->internal_state[0] ^= g_iotaRoundConstant[round];
    }
}

static void sha3ProcessLastBlock(Sha3Context *ctx)
{
    /* Mix final bits (011) into our state. */
    ((u8*)ctx->internal_state)[ctx->buffered_bytes] ^= 0b110;

    /* Mix in the high bit of the last word in our block. */
    ctx->internal_state[(ctx->block_size / sizeof(u64)) - 1] ^= g_finalMask;

    /* Process the last block. */
    sha3ProcessBlock(ctx);
}
