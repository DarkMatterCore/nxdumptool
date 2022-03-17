/*
 * gamecard.h
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

#ifndef __GAMECARD_H__
#define __GAMECARD_H__

#include "fs_ext.h"
#include "hfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAMECARD_HEAD_MAGIC         0x48454144                      /* "HEAD". */

#define GAMECARD_PAGE_SIZE          0x200
#define GAMECARD_PAGE_OFFSET(x)     ((u64)(x) * GAMECARD_PAGE_SIZE)

#define GAMECARD_UPDATE_TID         SYSTEM_UPDATE_TID

#define GAMECARD_CERTIFICATE_OFFSET 0x7000

/// Plaintext area. Dumped from FS program memory.
/// Overall structure may change with each new LAFW version.
typedef struct {
    u32 asic_security_mode;     ///< Determines how the Lotus ASIC initialised the gamecard security mode. Usually 0xFFFFFFF9.
    u32 asic_status;            ///< Bitmask of the internal gamecard interface status. Usually 0x20000000.
    FsCardId1 card_id1;
    FsCardId2 card_id2;
    u8 card_uid[0x40];
    u8 reserved[0x190];
    u8 asic_session_hash[0x20]; ///< Changes with each gamecard (re)insertion.
} GameCardSpecificData;

NXDT_ASSERT(GameCardSpecificData, 0x200);

/// Encrypted using AES-128-ECB with the common titlekek generator key (stored in the .rodata segment from the Lotus firmware).
typedef struct {
    union {
        u8 value[0x10];
        struct {
            u64 package_id;     ///< Matches package_id from GameCardHeader.
            u8 reserved[0x8];   ///< Just zeroes.
        };
    };
} GameCardKeySource;

NXDT_ASSERT(GameCardKeySource, 0x10);

/// Plaintext area. Dumped from FS program memory.
typedef struct {
    GameCardKeySource key_source;
    u8 encrypted_titlekey[0x10];    ///< Encrypted using AES-128-CCM with the decrypted key_source and the nonce from this section.
    u8 mac[0x10];                   ///< Used to verify the validity of the decrypted titlekey.
    u8 nonce[0xC];                  ///< Used as the IV to decrypt encrypted_titlekey using AES-128-CCM.
    u8 reserved[0x1C4];
} GameCardInitialData;

NXDT_ASSERT(GameCardInitialData, 0x200);

/// Plaintext area. Dumped from FS program memory.
/// This struct is returned by Lotus command "ChangeToSecureMode" (0xF). This means it is only available *after* the gamecard secure area has been mounted.
/// A copy of the gamecard header without the RSA-2048 signature and a plaintext GameCardInfo precedes this struct in FS program memory.
typedef struct {
    GameCardSpecificData specific_data;
    FsGameCardCertificate certificate;
    u8 reserved[0x200];
    GameCardInitialData initial_data;
} GameCardSecurityInformation;

NXDT_ASSERT(GameCardSecurityInformation, 0x800);

/// Encrypted using AES-128-CTR with the key and IV/counter from the `GameCardTitleKeyAreaEncryption` section. Assumed to be all zeroes in retail gamecards.
typedef struct {
    u8 titlekey[0x10];  ///< Decrypted titlekey from the `GameCardInitialData` section.
    u8 reserved[0xCF0];
} GameCardTitleKeyArea;

NXDT_ASSERT(GameCardTitleKeyArea, 0xD00);

/// Encrypted using RSA-2048-OAEP and a private OAEP key from AuthoringTool. Assumed to be all zeroes in retail gamecards.
typedef struct {
    u8 titlekey_encryption_key[0x10];   ///< Used as the AES-128-CTR key for the `GameCardTitleKeyArea` section. Randomly generated during XCI creation by AuthoringTool.
    u8 titlekey_encryption_iv[0x10];    ///< Used as the AES-128-CTR IV/counter for the `GameCardTitleKeyArea` section. Randomly generated during XCI creation by AuthoringTool.
    u8 reserved[0xE0];
} GameCardTitleKeyAreaEncryption;

NXDT_ASSERT(GameCardTitleKeyAreaEncryption, 0x100);

/// Used to secure communications between the Lotus and the inserted gamecard.
/// Supposedly precedes the gamecard header.
typedef struct {
    GameCardInitialData initial_data;
    GameCardTitleKeyArea titlekey_area;
    GameCardTitleKeyAreaEncryption titlekey_area_encryption;
} GameCardKeyArea;

NXDT_ASSERT(GameCardKeyArea, 0x1000);

typedef enum {
    GameCardKekIndex_Version0      = 0,
    GameCardKekIndex_VersionForDev = 1
} GameCardKekIndex;

typedef struct {
    u8 kek_index          : 4;  ///< GameCardKekIndex.
    u8 titlekey_dec_index : 4;
} GameCardKeyIndex;

NXDT_ASSERT(GameCardKeyIndex, 0x1);

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
    GameCardFlags_DifferentRegionCupToGlobalDevice = BIT(4),
    GameCardFlags_HasCa10Certificate               = BIT(7)
} GameCardFlags;

typedef enum {
    GameCardSelSec_ForT1 = 1,
    GameCardSelSec_ForT2 = 2
} GameCardSelSec;

typedef enum {
    GameCardFwVersion_ForDev       = 0,
    GameCardFwVersion_Since100NUP  = 1, ///< upp_version >= 0 (0.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Since400NUP  = 2, ///< upp_version >= 268435456 (4.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Since900NUP  = 3, ///< upp_version >= 603979776 (9.0.0-0.0) in GameCardInfo. Seems to be unused.
    GameCardFwVersion_Since1100NUP = 4, ///< upp_version >= 738197504 (11.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Since1200NUP = 5, ///< upp_version >= 805306368 (12.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Count        = 6
} GameCardFwVersion;

typedef enum {
    GameCardAccCtrl1_25MHz = 0xA10011,
    GameCardAccCtrl1_50MHz = 0xA10010   ///< GameCardRomSize_8GiB or greater.
} GameCardAccCtrl1;

typedef enum {
    GameCardCompatibilityType_Normal = 0,
    GameCardCompatibilityType_Terra  = 1,
    GameCardCompatibilityType_Count  = 2
} GameCardCompatibilityType;

/// Encrypted using AES-128-CBC with the XCI header key (found in FS program memory under HOS 9.0.0+) and the IV from `GameCardHeader`.
/// Key hashes for documentation purposes:
/// Production XCI header key hash:  2E36CC55157A351090A73E7AE77CF581F69B0B6E48FB066C984879A6ED7D2E96
/// Development XCI header key hash: 61D5C02244188810E2E3DE69341AC0F3C7653D370C6D3F77CA82B0B7E59F39AD
typedef struct {
    u64 fw_version;             ///< GameCardFwVersion.
    u32 acc_ctrl_1;             ///< GameCardAccCtrl1.
    u32 wait_1_time_read;       ///< Always 0x1388.
    u32 wait_2_time_read;       ///< Always 0.
    u32 wait_1_time_write;      ///< Always 0.
    u32 wait_2_time_write;      ///< Always 0.
    SdkAddOnVersion fw_mode;    ///< Current SdkAddOnVersion.
    Version upp_version;        ///< Bundled system update version.
    u8 compatibility_type;      ///< GameCardCompatibilityType.
    u8 reserved_1[0x3];
    u64 upp_hash;               ///< SHA-256 (?) checksum for the update partition. The exact way it's calculated is currently unknown.
    u64 upp_id;                 ///< Must match GAMECARD_UPDATE_TID.
    u8 reserved_2[0x38];
} GameCardInfo;

NXDT_ASSERT(GameCardInfo, 0x70);

/// Placed after the `GameCardKeyArea` section.
typedef struct {
    u8 signature[0x100];                            ///< RSA-2048-PSS with SHA-256 signature over the rest of the header.
    u32 magic;                                      ///< "HEAD".
    u32 rom_area_start_page_address;                ///< Expressed in GAMECARD_PAGE_SIZE units.
    u32 backup_area_start_page_address;             ///< Always 0xFFFFFFFF.
    GameCardKeyIndex key_index;
    u8 rom_size;                                    ///< GameCardRomSize.
    u8 header_version;                              ///< Always 0.
    u8 flags;                                       ///< GameCardFlags.
    u64 package_id;                                 ///< Used for challenge-response authentication.
    u32 valid_data_end_address;                     ///< Expressed in GAMECARD_PAGE_SIZE units.
    u8 reserved[0x4];
    u8 card_info_iv[AES_128_KEY_SIZE];              ///< AES-128-CBC IV for the CardInfo area (reversed).
    u64 partition_fs_header_address;                ///< Root Hash File System header offset.
    u64 partition_fs_header_size;                   ///< Root Hash File System header size.
    u8 partition_fs_header_hash[SHA256_HASH_SIZE];
    u8 initial_data_hash[SHA256_HASH_SIZE];
    u32 sel_sec;                                    ///< GameCardSelSec.
    u32 sel_t1_key;                                 ///< Always 2.
    u32 sel_key;                                    ///< Always 0.
    u32 lim_area;                                   ///< Expressed in GAMECARD_PAGE_SIZE units.
    GameCardInfo card_info;
} GameCardHeader;

NXDT_ASSERT(GameCardHeader, 0x200);

typedef enum {
    GameCardStatus_NotInserted                     = 0, ///< No gamecard is inserted.
    GameCardStatus_Processing                      = 1, ///< A gamecard has been inserted and it's being processed.
    GameCardStatus_NoGameCardPatchEnabled          = 2, ///< A gamecard has been inserted, but the running CFW enabled the "nogc" patch at boot.
                                                        ///< This triggers an error whenever fsDeviceOperatorGetGameCardHandle is called. Nothing at all can be done with the inserted gamecard.
    GameCardStatus_LotusAsicFirmwareUpdateRequired = 3, ///< A gamecard has been inserted, but a LAFW update is needed before being able to read the secure storage area.
                                                        ///< Operations on the normal storage area are still possible, though.
    GameCardStatus_InsertedAndInfoNotLoaded        = 4, ///< A gamecard has been inserted, but an unexpected error unrelated to both "nogc" patch and LAFW version occurred.
    GameCardStatus_InsertedAndInfoLoaded           = 5  ///< A gamecard has been inserted and all required information could be successfully retrieved from it.
} GameCardStatus;

typedef enum {
    GameCardHashFileSystemPartitionType_None   = 0, ///< Not a real value.
    GameCardHashFileSystemPartitionType_Root   = 1,
    GameCardHashFileSystemPartitionType_Update = 2,
    GameCardHashFileSystemPartitionType_Logo   = 3, ///< Only available in GameCardFwVersion_Since400NUP or greater gamecards.
    GameCardHashFileSystemPartitionType_Normal = 4,
    GameCardHashFileSystemPartitionType_Secure = 5,
    GameCardHashFileSystemPartitionType_Boot   = 6,
    GameCardHashFileSystemPartitionType_Count  = 7  ///< Not a real value.
} GameCardHashFileSystemPartitionType;

typedef enum {
    LotusAsicFirmwareType_ReadFw    = 0xFF,
    LotusAsicFirmwareType_ReadDevFw = 0xFFFF,
    LotusAsicFirmwareType_WriterFw  = 0xFFFFFF,
    LotusAsicFirmwareType_RmaFw     = 0xFFFFFFFF
} LotusAsicFirmwareType;

typedef enum {
    LotusAsicDeviceType_Test     = 0,
    LotusAsicDeviceType_Dev      = 1,
    LotusAsicDeviceType_Prod     = 2,
    LotusAsicDeviceType_Prod2Dev = 3,
    LotusAsicDeviceType_Count    = 4    ///< Not a real value.
} LotusAsicDeviceType;

/// Plaintext Lotus ASIC firmware (LAFW) blob. Dumped from FS program memory.
typedef struct {
    u8 signature[0x100];
    u32 magic;                      ///< "LAFW".
    u32 fw_type;                    ///< LotusAsicFirmwareType.
    u8 reserved_1[0x8];
    struct {
        u64 fw_version  : 62;       ///< Stored using a bitmask.
        u64 device_type : 2;        ///< LotusAsicDeviceType.
    };
    u32 data_size;
    u8 reserved_2[0x4];
    u8 data_iv[AES_128_KEY_SIZE];
    char placeholder_str[0x10];     ///< "IDIDIDIDIDIDIDID".
    u8 reserved_3[0x40];
    u8 data[0x7680];
} LotusAsicFirmwareBlob;

NXDT_ASSERT(LotusAsicFirmwareBlob, 0x7800);

/// Initializes data needed to access raw gamecard storage areas.
/// Also spans a background thread to automatically detect gamecard status changes and to cache data from the inserted gamecard.
bool gamecardInitialize(void);

/// Deinitializes data generated by gamecardInitialize().
/// This includes destroying the background gamecard detection thread and freeing all cached gamecard data.
void gamecardExit(void);

/// Returns a user-mode gamecard status change event that can be used to wait for status changes on other threads.
/// If the gamecard interface hasn't been initialized, this returns NULL.
UEvent *gamecardGetStatusChangeUserEvent(void);

/// Returns the current GameCardStatus value.
u8 gamecardGetStatus(void);

/// Fills the provided GameCardSecurityInformation pointer.
/// This area can't be read using gamecardReadStorage().
bool gamecardGetSecurityInformation(GameCardSecurityInformation* out);

/// Fills the provided FsGameCardIdSet pointer.
/// This area can't be read using gamecardReadStorage().
bool gamecardGetIdSet(FsGameCardIdSet *out);

/// Fills the provided pointers with LAFW blob data from FS program memory.
/// 'out_lafw_blob' or 'out_lafw_version' may be set to NULL, but at least one of them must be a valid pointer.
bool gamecardGetLotusAsicFirmwareBlob(LotusAsicFirmwareBlob *out_lafw_blob, u64 *out_lafw_version);

/// Used to read raw data from the inserted gamecard. Supports unaligned reads.
/// All required handles, changes between normal <-> secure storage areas and proper offset calculations are managed internally.
/// 'offset' + 'read_size' must not exceed the value returned by gamecardGetTotalSize().
bool gamecardReadStorage(void *out, u64 read_size, u64 offset);

/// Fills the provided GameCardHeader pointer.
/// This area can also be read using gamecardReadStorage(), starting at offset 0.
bool gamecardGetHeader(GameCardHeader *out);

/// Fills the provided GameCardInfo pointer.
bool gamecardGetDecryptedCardInfoArea(GameCardInfo *out);

/// Fills the provided FsGameCardCertificate pointer.
/// This area can also be read using gamecardReadStorage(), starting at GAMECARD_CERTIFICATE_OFFSET.
bool gamecardGetCertificate(FsGameCardCertificate *out);

/// Fills the provided u64 pointer with the total gamecard size, which is the size taken by both Normal and Secure storage areas.
bool gamecardGetTotalSize(u64 *out);

/// Fills the provided u64 pointer with the trimmed gamecard size, which is the same as the size returned by gamecardGetTotalSize() but using the trimmed Secure storage area size.
bool gamecardGetTrimmedSize(u64 *out);

/// Fills the provided u64 pointer with the gamecard ROM capacity, based on the GameCardRomSize value from the header. Not the same as gamecardGetTotalSize().
bool gamecardGetRomCapacity(u64 *out);

/// Fills the provided Version pointer with the bundled firmware update version in the inserted gamecard.
bool gamecardGetBundledFirmwareUpdateVersion(Version *out);

/// Fills the provided HashFileSystemContext pointer using information from the requested Hash FS partition.
/// Hash FS functions can be used on the retrieved HashFileSystemContext. hfsFreeContext() must be used to free the underlying data from the filled context.
bool gamecardGetHashFileSystemContext(u8 hfs_partition_type, HashFileSystemContext *out);

/// One-shot function to retrieve meaningful information from a Hash FS entry by name without using gamecardGetHashFileSystemContext() + Hash FS functions.
/// 'out_offset' or 'out_size' may be set to NULL, but at least one of them must be a valid pointer. The returned offset is always relative to the start of the gamecard image.
/// If you need to get entry information by index, just retrieve the Hash FS context for the target partition and use Hash FS functions on it.
bool gamecardGetHashFileSystemEntryInfoByName(u8 hfs_partition_type, const char *entry_name, u64 *out_offset, u64 *out_size);

/// Takes a GameCardFwVersion value. Returns a pointer to a string that represents the minimum HOS version that matches the provided LAFW version.
/// Returns NULL if the provided value is out of range.
const char *gamecardGetRequiredHosVersionString(u64 fw_version);

/// Takes a GameCardCompatibilityType value. Returns a pointer to a string that represents the provided compatibility type.
/// Returns NULL if the provided value is out of range.
const char *gamecardGetCompatibilityTypeString(u8 compatibility_type);

/// Takes a LotusAsicFirmwareType value. Returns a pointer to a string that represents the provided LAFW type.
/// Returns NULL if the provided value is invalid.
const char *gamecardGetLafwTypeString(u32 fw_type);

/// Takes a LotusAsicDeviceType value. Returns a pointer to a string that represents the provided LAFW device type.
/// Returns NULL if the provided value is out of range.
const char *gamecardGetLafwDeviceTypeString(u64 device_type);

#ifdef __cplusplus
}
#endif

#endif /* __GAMECARD_H__ */
