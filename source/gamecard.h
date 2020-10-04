/*
 * gamecard.h
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __GAMECARD_H__
#define __GAMECARD_H__

#include "fs_ext.h"

#define GAMECARD_HEAD_MAGIC             0x48454144              /* "HEAD". */

#define GAMECARD_MEDIA_UNIT_SIZE        0x200
#define GAMECARD_MEDIA_UNIT_OFFSET(x)   ((u64)(x) * GAMECARD_MEDIA_UNIT_SIZE)

#define GAMECARD_UPDATE_TID             (u64)0x0100000000000816

#define GAMECARD_CERTIFICATE_OFFSET     0x7000

/// Encrypted using AES-128-ECB with the common titlekek generator key (stored in the .rodata segment from the Lotus firmware).
typedef struct {
    u64 package_id;         ///< Matches package_id from GameCardHeader.
    u8 reserved[0x8];       ///< Just zeroes.
} GameCardKeySource;

/// Plaintext area. Dumped from FS program memory.
typedef struct {
    GameCardKeySource key_source;
    u8 encrypted_titlekey[0x10];    ///< Encrypted using AES-128-CCM with the decrypted key_source and the nonce from this section.
    u8 mac[0x10];                   ///< Used to verify the validity of the decrypted titlekey.
    u8 nonce[0xC];                  ///< Used as the IV to decrypt encrypted_titlekey using AES-128-CCM.
    u8 reserved[0x1C4];
} GameCardInitialData;

/// Encrypted using AES-128-CTR with the key and IV/counter from the `GameCardTitleKeyEncryption` section. Assumed to be all zeroes in retail gamecards.
typedef struct {
    u8 titlekey[0x10];  ///< Decrypted titlekey from the `GameCardInitialData` section.
    u8 reserved[0xCF0];
} GameCardTitleKey;

/// Encrypted using RSA-2048-OAEP and a private OAEP key from AuthoringTool. Assumed to be all zeroes in retail gamecards.
typedef struct {
    u8 titlekey_encryption_key[0x10];   ///< Used as the AES-128-CTR key for the `GameCardTitleKey` section. Randomly generated during XCI creation by AuthoringTool.
    u8 titlekey_encryption_iv[0x10];    ///< Used as the AES-128-CTR IV/counter for the `GameCardTitleKey` section. Randomly generated during XCI creation by AuthoringTool.
    u8 reserved[0xE0];
} GameCardTitleKeyEncryption;

/// Used to secure communications between the Lotus and the inserted gamecard.
/// Precedes the gamecard header.
typedef struct {
    GameCardInitialData initial_data;
    GameCardTitleKey titlekey_block;
    GameCardTitleKeyEncryption titlekey_encryption;
} GameCardKeyArea;

typedef enum {
    GameCardKekIndex_Version0      = 0,
    GameCardKekIndex_VersionForDev = 1
} GameCardKekIndex;

typedef struct {
    u8 kek_index          : 4;  ///< GameCardKekIndex.
    u8 titlekey_dec_index : 4;
} GameCardKeyIndex;

typedef enum {
    GameCardRomSize_1GiB  = 0xFA,
    GameCardRomSize_2GiB  = 0xF8,
    GameCardRomSize_4GiB  = 0xF0,
    GameCardRomSize_8GiB  = 0xE0,
    GameCardRomSize_16GiB = 0xE1,
    GameCardRomSize_32GiB = 0xE2
} GameCardRomSize;

typedef enum {
    GameCardFlags_AutoBoot                         = BIT(0),
    GameCardFlags_HistoryErase                     = BIT(1),
    GameCardFlags_RepairTool                       = BIT(2),
    GameCardFlags_DifferentRegionCupToTerraDevice  = BIT(3),
    GameCardFlags_DifferentRegionCupToGlobalDevice = BIT(4)
} GameCardFlags;

typedef enum {
    GameCardSelSec_ForT1 = 1,
    GameCardSelSec_ForT2 = 2
} GameCardSelSec;

typedef enum {
    GameCardFwVersion_ForDev       = 0,
    GameCardFwVersion_Before400NUP = 1, ///< cup_version < 268435456 (4.0.0-0.0) in GameCardHeaderEncryptedArea.
    GameCardFwVersion_Since400NUP  = 2  ///< cup_version >= 268435456 (4.0.0-0.0) in GameCardHeaderEncryptedArea.
} GameCardFwVersion;

typedef enum {
    GameCardAccCtrl1_25MHz = 0xA10011,
    GameCardAccCtrl1_50MHz = 0xA10010   ///< GameCardRomSize_8GiB or greater.
} GameCardAccCtrl1;

typedef enum {
    GameCardCompatibilityType_Normal = 0,
    GameCardCompatibilityType_Terra  = 1
} GameCardCompatibilityType;

typedef struct {
    u32 GameCardFwMode_Relstep : 8;
    u32 GameCardFwMode_Micro   : 8;
    u32 GameCardFwMode_Minor   : 8;
    u32 GameCardFwMode_Major   : 8;
} GameCardFwMode;

typedef struct {
    u32 GameCardCupVersion_MinorRelstep : 8;
    u32 GameCardCupVersion_MajorRelstep : 8;
    u32 GameCardCupVersion_Micro        : 4;
    u32 GameCardCupVersion_Minor        : 6;
    u32 GameCardCupVersion_Major        : 6;
} GameCardCupVersion;

/// Encrypted using AES-128-CBC with the XCI header key (found in FS program memory under newer versions of HOS) and the IV from `GameCardHeader`.
/// Key hashes for documentation purposes:
/// Production XCI header key hash:  2E36CC55157A351090A73E7AE77CF581F69B0B6E48FB066C984879A6ED7D2E96
/// Development XCI header key hash: 61D5C02244188810E2E3DE69341AC0F3C7653D370C6D3F77CA82B0B7E59F39AD
typedef struct {
    u64 fw_version;                 ///< GameCardFwVersion.
    u32 acc_ctrl_1;                 ///< GameCardAccCtrl1.
    u32 wait_1_time_read;           ///< Always 0x1388.
    u32 wait_2_time_read;           ///< Always 0.
    u32 wait_1_time_write;          ///< Always 0.
    u32 wait_2_time_write;          ///< Always 0.
    GameCardFwMode fw_mode;
    GameCardCupVersion cup_version;
    u8 compatibility_type;          ///< GameCardCompatibilityType.
    u8 reserved_1[0x3];
    u64 cup_hash;
    u64 cup_id;                     ///< Must match GAMECARD_UPDATE_TID.
    u8 reserved_2[0x38];
} GameCardHeaderEncryptedArea;

/// Placed after the `GameCardKeyArea` section.
typedef struct {
    u8 signature[0x100];                            ///< RSA-2048 PKCS #1 signature over the rest of the header.
    u32 magic;                                      ///< "HEAD".
    u32 secure_area_start_address;                  ///< Expressed in GAMECARD_MEDIA_UNIT_SIZE blocks.
    u32 backup_area_start_address;                  ///< Always 0xFFFFFFFF.
    GameCardKeyIndex key_index;
    u8 rom_size;                                    ///< GameCardRomSize.
    u8 header_version;                              ///< Always 0.
    u8 flags;                                       ///< GameCardFlags.
    u64 package_id;
    u32 valid_data_end_address;                     ///< Expressed in GAMECARD_MEDIA_UNIT_SIZE blocks.
    u8 reserved[0x4];
    u8 iv[0x10];
    u64 partition_fs_header_address;                ///< Root HFS0 header offset.
    u64 partition_fs_header_size;                   ///< Root HFS0 header size.
    u8 partition_fs_header_hash[SHA256_HASH_SIZE];
    u8 initial_data_hash[SHA256_HASH_SIZE];
    u32 sel_sec;                                    ///< GameCardSelSec.
    u32 sel_t1_key;                                 ///< Always 2.
    u32 sel_key;                                    ///> Always 0.
    u32 normal_area_end_address;                    ///< Expressed in GAMECARD_MEDIA_UNIT_SIZE blocks.
    GameCardHeaderEncryptedArea encrypted_area;
} GameCardHeader;

typedef enum {
    GameCardStatus_NotInserted              = 0,
    GameCardStatus_InsertedAndInfoNotLoaded = 1,    ///< Most likely related to the "nogc" patch being enabled. Means nothing at all can be done with the inserted gamecard.
    GameCardStatus_InsertedAndInfoLoaded    = 2
} GameCardStatus;

typedef enum {
    GameCardHashFileSystemPartitionType_Root    = 0,
    GameCardHashFileSystemPartitionType_Update  = 1,
    GameCardHashFileSystemPartitionType_Logo    = 2,    ///< Only available in GameCardFwVersion_Since400NUP gamecards.
    GameCardHashFileSystemPartitionType_Normal  = 3,
    GameCardHashFileSystemPartitionType_Secure  = 4,
    GameCardHashFileSystemPartitionType_Boot    = 5,    ///< Only available in Terra (Tencent) gamecards.
    GameCardHashFileSystemPartitionType_Count   = 6     ///< Not a real value.
} GameCardHashFileSystemPartitionType;

/// Initializes data needed to access raw gamecard storage areas.
/// Also spans a background thread to automatically detect gamecard status changes and to cache data from the inserted gamecard.
bool gamecardInitialize(void);

/// Deinitializes data generated by gamecardInitialize().
/// This includes destroying the background gamecard detection thread and freeing all cached gamecard data.
void gamecardExit(void);

/// Returns an usermode gamecard status change event that can be used to wait for status changes on other threads.
/// If the gamecard interface hasn't been initialized, this returns NULL.
UEvent *gamecardGetStatusChangeUserEvent(void);

/// Returns the current GameCardStatus value.
u8 gamecardGetStatus(void);

/// Used to read data from the inserted gamecard.
/// All required handles, changes between normal <-> secure storage areas and proper offset calculations are managed internally.
/// 'offset' + 'read_size' must not exceed the value returned by gamecardGetTotalSize().
bool gamecardReadStorage(void *out, u64 read_size, u64 offset);

/// Miscellaneous functions.

bool gamecardGetKeyArea(GameCardKeyArea *out);
bool gamecardGetHeader(GameCardHeader *out);
bool gamecardGetCertificate(FsGameCardCertificate *out);
bool gamecardGetTotalSize(u64 *out);
bool gamecardGetTrimmedSize(u64 *out);
bool gamecardGetRomCapacity(u64 *out); ///< Not the same as gamecardGetTotalSize().
bool gamecardGetBundledFirmwareUpdateVersion(u32 *out);

/// Returns a pointer to a string holding the name of the provided hash file system partition type. Returns NULL if the provided value is invalid.
const char *gamecardGetHashFileSystemPartitionName(u8 hfs_partition_type);

/// Retrieves the entry count from a hash FS partition.
bool gamecardGetEntryCountFromHashFileSystemPartition(u8 hfs_partition_type, u32 *out_count);

/// Retrieves info from a hash FS partition entry using an entry index.
/// 'out_offset', 'out_size' or 'out_name' may be set to NULL, but at least one of them must be a valid pointer.
/// If 'out_name' != NULL and the function call succeeds, a pointer to a heap allocated buffer is returned.
bool gamecardGetEntryInfoFromHashFileSystemPartitionByIndex(u8 hfs_partition_type, u32 idx, u64 *out_offset, u64 *out_size, char **out_name);

/// Retrieves info from a hash FS partition entry using an entry name.
/// 'out_offset' or 'out_size' may be set to NULL, but at least one of them must be a valid pointer.
bool gamecardGetEntryInfoFromHashFileSystemPartitionByName(u8 hfs_partition_type, const char *name, u64 *out_offset, u64 *out_size);

#endif /* __GAMECARD_H__ */
