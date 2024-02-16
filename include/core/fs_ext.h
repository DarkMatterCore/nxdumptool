/*
 * fs_ext.h
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
    u8 t1_card_device_id[0x10];
    u8 iv[0x10];
    u8 hw_key[0x10];            ///< Encrypted.
    u8 data[0xC0];              ///< Encrypted.
} FsGameCardCertificate;

NXDT_ASSERT(FsGameCardCertificate, 0x200);

typedef enum {
    FsCardId1MakerCode_MegaChips = 0xC2,
    FsCardId1MakerCode_Lapis     = 0xAE,
    FsCardId1MakerCode_Unknown   = 0x36     ///< Seen in TLoZ:TotK, SMBW and other modern releases.
} FsCardId1MakerCode;

typedef enum {
    FsCardId1MemoryType_None       = 0,
    FsCardId1MemoryType_CardModeT1 = BIT(0),
    FsCardId1MemoryType_CardModeT2 = BIT(1),
    FsCardId1MemoryType_Unknown1   = BIT(2),    ///< Related to CardMode?
    FsCardId1MemoryType_IsNand     = BIT(3),    ///< 0: Rom, 1: Nand.
    FsCardId1MemoryType_Unknown2   = BIT(4),    ///< Related to Nand memory type?
    FsCardId1MemoryType_IsLate     = BIT(5),    ///< 0: Fast, 1: Late.
    FsCardId1MemoryType_Unknown3   = BIT(6),
    FsCardId1MemoryType_Unknown4   = BIT(7),
    FsCardId1MemoryType_Count      = 8,         ///< Total values supported by this enum.

    ///< Values defined in Atmosphère source code.
    FsCardId1MemoryType_T1RomFast  = FsCardId1MemoryType_CardModeT1,
    FsCardId1MemoryType_T2RomFast  = FsCardId1MemoryType_CardModeT2,
    FsCardId1MemoryType_T1NandFast = (FsCardId1MemoryType_IsNand | FsCardId1MemoryType_CardModeT1),
    FsCardId1MemoryType_T2NandFast = (FsCardId1MemoryType_IsNand | FsCardId1MemoryType_CardModeT2),
    FsCardId1MemoryType_T1RomLate  = (FsCardId1MemoryType_IsLate | FsCardId1MemoryType_CardModeT1),
    FsCardId1MemoryType_T2RomLate  = (FsCardId1MemoryType_IsLate | FsCardId1MemoryType_CardModeT2),
    FsCardId1MemoryType_T1NandLate = (FsCardId1MemoryType_IsLate | FsCardId1MemoryType_IsNand | FsCardId1MemoryType_CardModeT1),
    FsCardId1MemoryType_T2NandLate = (FsCardId1MemoryType_IsLate | FsCardId1MemoryType_IsNand | FsCardId1MemoryType_CardModeT2)
} FsCardId1MemoryType;

typedef struct {
    u8 maker_code;      ///< FsCardId1MakerCode.
    u8 memory_capacity; ///< Matches GameCardRomSize.
    u8 reserved;        ///< Known values: 0x00, 0x01, 0x02, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0C, 0x0D, 0x0E, 0x80.
    u8 memory_type;     ///< FsCardId1MemoryType.
} FsCardId1;

NXDT_ASSERT(FsCardId1, 0x4);

typedef enum {
    FsCardId2CardType_Rom            = 0,
    FsCardId2CardType_WritableDevT1  = 1,
    FsCardId2CardType_WritableProdT1 = 2,
    FsCardId2CardType_WritableDevT2  = 3,
    FsCardId2CardType_WritableProdT2 = 4,
    FsCardId2CardType_Count          = 5    ///< Total values supported by this enum.
} FsCardId2CardType;

typedef struct {
    u8 sel_t1_key;      ///< Matches sel_t1_key value from GameCardHeader (usually 0x02).
    u8 card_type;       ///< FsCardId2CardType.
    u8 reserved[0x2];   ///< Usually filled with zeroes.
} FsCardId2;

NXDT_ASSERT(FsCardId2, 0x4);

typedef struct {
    u8 reserved[0x4];   ///< Usually filled with zeroes.
} FsCardId3;

NXDT_ASSERT(FsCardId3, 0x4);

/// Returned by fsDeviceOperatorGetGameCardIdSet.
typedef struct {
    FsCardId1 id1;  ///< Specifies maker code, memory capacity and memory type.
    FsCardId2 id2;  ///< Specifies card security number and card type.
    FsCardId3 id3;  ///< Always zero (so far).
} FsGameCardIdSet;

NXDT_ASSERT(FsGameCardIdSet, 0xC);

/// IFileSystemProxy.
Result fsOpenGameCardStorage(FsStorage *out, const FsGameCardHandle *handle, u32 partition);
Result fsOpenGameCardDetectionEventNotifier(FsEventNotifier *out);

/// IDeviceOperator.
Result fsDeviceOperatorUpdatePartitionInfo(FsDeviceOperator *d, const FsGameCardHandle *handle, u32 *out_title_version, u64 *out_title_id);

#ifdef __cplusplus
}
#endif

#endif /* __FS_EXT_H__ */
