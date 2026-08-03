/*
 * zbic.c
 *
 * Copyright (c) Atmosphère-NX.
 * Copyright (c) 2020-2026, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 * Based on util_compression_zstd_bic.cpp from Atmosphère.
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

#define ZSTD_STATIC_LINKING_ONLY    /* Required by Zstandard to enable in-place decompression. */
#define ZSTD_ZBIC_SUPPORT 1         /* Required by Zstandard to enable custom ZBIC dictionary support. */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define ZSTDLIB_VISIBLE   static
#define ZSTDLIB_HIDDEN    static
#include <core/zstd.h>
#include "zstd.inc"
#pragma GCC diagnostic pop

size_t zbicGetInPlaceDecompressionBufferSize(const void *src, size_t src_size, size_t expected_dec_size)
{
    if (!src || !src_size || expected_dec_size <= src_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return 0;
    }

    size_t dec_buf_size = ZSTD_decompressionMargin(src, src_size);
    if (ZSTD_isError(dec_buf_size))
    {
        ZSTD_ErrorCode zstd_err = ZSTD_getErrorCode(dec_buf_size);
        const char* zstd_err_str = ZSTD_getErrorString(zstd_err);

        LOG_MSG_ERROR("ZSTD_decompressionMargin() failed! %s (error %d, margin 0x%lX).", zstd_err_str, zstd_err, dec_buf_size);
        return 0;
    }

    return (dec_buf_size + expected_dec_size);
}

size_t zbicDecompress(void *dst, size_t dst_size, const void *src, size_t src_size)
{
    if (!dst || !src || !src_size || dst_size <= src_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return 0;
    }

    size_t dec_size = ZSTD_decompress(dst, dst_size, src, src_size);
    if (ZSTD_isError(dec_size))
    {
        ZSTD_ErrorCode zstd_err = ZSTD_getErrorCode(dec_size);
        const char* zstd_err_str = ZSTD_getErrorString(zstd_err);

        LOG_MSG_ERROR("ZSTD_decompress() failed! %s (error %d, margin 0x%lX).", zstd_err_str, zstd_err, dec_size);
        return 0;
    }

    return dec_size;
}
