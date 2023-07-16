/*
 * nca_storage.h
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __NCA_STORAGE_H__
#define __NCA_STORAGE_H__

#include "bktr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NcaStorageBaseStorageType_Invalid    = 0,   ///< Placeholder.
    NcaStorageBaseStorageType_Regular    = 1,
    NcaStorageBaseStorageType_Sparse     = 2,
    NcaStorageBaseStorageType_Indirect   = 3,
    NcaStorageBaseStorageType_Compressed = 4,
    NcaStorageBaseStorageType_Count      = 5    ///< Total values supported by this enum.
} NcaStorageBaseStorageType;

/// Used to perform multi-layered reads within a single NCA FS section.
typedef struct {
    u8 base_storage_type;                   ///< NcaStorageBaseStorageType.
    NcaFsSectionContext *nca_fs_ctx;        ///< NCA FS section context used to initialize this context.
    BucketTreeContext *sparse_storage;      ///< Sparse storage context.
    BucketTreeContext *aes_ctr_ex_storage;  ///< AesCtrEx storage context.
    BucketTreeContext *indirect_storage;    ///< Indirect storage context.
    BucketTreeContext *compressed_storage;  ///< Compressed storage context.
} NcaStorageContext;

/// Initializes a NCA storage context using a NCA FS section context.
bool ncaStorageInitializeContext(NcaStorageContext *out, NcaFsSectionContext *nca_fs_ctx);

/// Sets a storage from the provided Base NcaStorageContext as the original substorage for the provided Patch NcaStorageContext's Indirect Storage.
/// Needed to perform combined reads between a base NCA and a patch NCA.
bool ncaStorageSetPatchOriginalSubStorage(NcaStorageContext *patch_ctx, NcaStorageContext *base_ctx);

/// Retrieves the underlying NCA FS section's hierarchical hash target layer extents. Virtual extents may be returned, depending on the base storage type.
/// Output offset is relative to the start of the NCA FS section.
/// Either 'out_offset' or 'out_size' can be NULL, but at least one of them must be a valid pointer.
bool ncaStorageGetHashTargetExtents(NcaStorageContext *ctx, u64 *out_offset, u64 *out_size);

/// Reads data from the NCA storage using a previously initialized NcaStorageContext.
bool ncaStorageRead(NcaStorageContext *ctx, void *out, u64 read_size, u64 offset);

/// Checks if the provided block extents are within the provided Patch NcaStorageContext's Indirect Storage.
bool ncaStorageIsBlockWithinPatchStorageRange(NcaStorageContext *ctx, u64 offset, u64 size, bool *out);

/// Frees a previously initialized NCA storage context.
void ncaStorageFreeContext(NcaStorageContext *ctx);

/// Helper inline functions.

NX_INLINE bool ncaStorageIsValidContext(NcaStorageContext *ctx)
{
    return (ctx && ctx->base_storage_type >= NcaStorageBaseStorageType_Regular && ctx->base_storage_type <= NcaStorageBaseStorageType_Compressed && ctx->nca_fs_ctx && \
            ctx->nca_fs_ctx->enabled);
}

#ifdef __cplusplus
}
#endif

#endif /* __NCA_STORAGE_H__ */
