/*
 * nca_storage.h
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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
    NcaStorageBaseStorageType_Invalid    = 0,   /* Placeholder. */
    NcaStorageBaseStorageType_Regular    = 1,
    NcaStorageBaseStorageType_Sparse     = 2,
    NcaStorageBaseStorageType_Indirect   = 3,
    NcaStorageBaseStorageType_Compressed = 4
} NcaStorageBaseStorageType;

/// Used to perform multi-layer reads within a single NCA FS section.
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

/// Reads data from the NCA storage using a previously initialized NcaStorageContext.
bool ncaStorageRead(NcaStorageContext *ctx, void *out, u64 read_size, u64 offset);

/// Frees a previously initialized NCA storage context.
void ncaStorageFreeContext(NcaStorageContext *ctx);

#ifdef __cplusplus
}
#endif

#endif /* __NCA_STORAGE_H__ */
