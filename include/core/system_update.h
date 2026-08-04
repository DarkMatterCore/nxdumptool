/*
 * system_update.h
 *
 * Copyright (c) 2020-2026, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
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

#ifndef __SYSTEM_UPDATE_H__
#define __SYSTEM_UPDATE_H__

#include "hos_version_structs.h"
#include "nca.h"
#include "title.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    u64 cur_size;                   ///< Current dump size.
    u64 total_size;                 ///< Total dump size.
    u32 content_idx;                ///< Current content index.
    u32 content_count;              ///< Total content count.
    u64 cur_content_offset;         ///< Current content offset.
    Sha256Context sha256_ctx;       ///< SHA-256 hash context. Used to verify dumped NCAs.
    NcaContext **nca_ctxs;          ///< NCA context pointer array for all system update contents. Used to read content data.
    SystemVersionFile version_file; ///< File data from the SystemVersion title.
} SystemUpdateDumpContext;

/// Initializes the system update interface.
bool systemUpdateInitialize(void);

/// Closes the system update interface.
void systemUpdateExit(void);

/// Initializes a system update dump context.
bool systemUpdateInitializeDumpContext(SystemUpdateDumpContext *ctx);

/// Returns the size for the current NCA pointed to by the provided system update dump context.
/// Returns false if the input system update dump context is invalid or if it has already been fully processed.
bool systemUpdateGetCurrentContentFileSizeFromDumpContext(SystemUpdateDumpContext *ctx, u64 *out_size);

/// Returns a pointer to a dynamically allocated string that holds the filename for the current NCA pointed to by the provided system update dump context.
/// The allocated buffer must be freed by the caller using free().
/// Returns NULL if the input system update dump context is invalid, if it has already been fully processed or if an allocation error occurs.
char *systemUpdateGetCurrentContentFileNameFromDumpContext(SystemUpdateDumpContext *ctx);

/// Reads raw data from the current NCA pointed to by the provided system update dump context.
/// The internal content offset variable is used to keep track of the current file position.
/// Use systemUpdateGetCurrentContentFileSizeFromDumpContext() to get the size for the current NCA.
bool systemUpdateReadCurrentContentFileFromDumpContext(SystemUpdateDumpContext *ctx, void *out, u64 read_size);

/// Helper inline functions.

NX_INLINE void systemUpdateFreeDumpContext(SystemUpdateDumpContext *ctx)
{
    if (!ctx) return;

    if (ctx->nca_ctxs)
    {
        for(u16 i = 0; i < ctx->content_count; i++)
        {
            if (ctx->nca_ctxs[i]) free(ctx->nca_ctxs[i]);
        }

        free(ctx->nca_ctxs);
    }

    memset(ctx, 0, sizeof(SystemUpdateDumpContext));
}

NX_INLINE bool systemUpdateIsValidDumpContext(const SystemUpdateDumpContext *ctx)
{
    return (ctx && ctx->total_size && ctx->content_count && ctx->nca_ctxs);
}

NX_INLINE bool systemUpdateIsDumpContextFinished(const SystemUpdateDumpContext *ctx)
{
    return (ctx && ctx->cur_size >= ctx->total_size && ctx->content_idx >= ctx->content_count);
}

#ifdef __cplusplus
}
#endif

#endif /* __SYSTEM_UPDATE_H__ */
