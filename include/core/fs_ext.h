/*
 * fs_ext.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __FS_EXT_H__
#define __FS_EXT_H__

#ifdef __cplusplus
extern "C" {
#endif

#define GAMECARD_CERT_MAGIC 0x43455254  /* "CERT". */

/// Located at offset 0x7000 in the gamecard image.
typedef struct {
    u8 signature[0x100];        ///< RSA-2048-PSS with SHA-256 signature over the rest of the data.
    u32 magic;                  ///< "CERT".
    u32 version;
    u8 kek_index;
    u8 reserved[0x7];
    u8 device_id[0x10];
    u8 iv[0x10];
    u8 data[0xD0];              ///< Encrypted using the IV from this struct and an unknown key.
} FsGameCardCertificate;

NXDT_ASSERT(FsGameCardCertificate, 0x200);

/// IFileSystemProxy.
Result fsOpenGameCardStorage(FsStorage *out, const FsGameCardHandle *handle, u32 partition);
Result fsOpenGameCardDetectionEventNotifier(FsEventNotifier *out);

/// IDeviceOperator.
Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator *d, const FsGameCardHandle *handle, u32 *out_title_version, u64 *out_title_id);
Result fsDeviceOperatorGetGameCardDeviceCertificate(FsDeviceOperator *d, const FsGameCardHandle *handle, FsGameCardCertificate *out);

#ifdef __cplusplus
}
#endif

#endif /* __FS_EXT_H__ */
