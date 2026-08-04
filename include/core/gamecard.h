/*
 * gamecard.h
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

#ifndef __GAMECARD_H__
#define __GAMECARD_H__

#include "fs_ext.h"
#include "hfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAMECARD_HEAD_MAGIC                 0x48454144                      /* "HEAD". */
#define GAMECARD_CHVC_MAGIC                 0x43485643                      /* "CHVC". */

#define GAMECARD_PAGE_SIZE                  0x200
#define GAMECARD_PAGE_OFFSET(x)             ((u64)(x) * GAMECARD_PAGE_SIZE)

#define GAMECARD_UPDATE_TID                 SYSTEM_UPDATE_TID

#define GAMECARD_HEADER2_OFFSET             0x200
#define GAMECARD_HEADER2_CERT_OFFSET        0x400
#define GAMECARD_HEADER2_CERT_PUBKEY_OFFSET 0x800

#define GAMECARD_CERT_OFFSET                0x7000
#define GAMECARD_CERT_SIZE(IS_T2)           ((IS_T2) ? sizeof(FsGameCardT2Certificate) : (sizeof(FsGameCardT1Certificate) - MEMBER_SIZE(FsGameCardT1Certificate, padding)))

#define LAFW_MAGIC                          0x4C414657                      /* "LAFW". */
#define LAFW_FW_TYPE_ENABLED_FLAG           0xFF

/// Encrypted using AES-128-ECB with the common titlekek generator key (stored in the .rodata segment from the Lotus firmware).
typedef struct {
    union {
        u8 value[0x10];
        struct {
            u8 package_id[0x8]; ///< Matches package_id from GameCardHeader.
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

typedef enum : u8 {
    GameCardUidMakerCode_MegaChips = 0, // Macronix.
    GameCardUidMakerCode_Lapis     = 1,
    GameCardUidMakerCode_Unknown   = 2,
    GameCardUidMakerCode_Count     = 3  ///< Total values supported by this enum.
} GameCardUidMakerCode;

typedef enum : u8 {
    GameCardUidCardType_Rom          = 0,
    GameCardUidCardType_WritableDev  = 0xFE,
    GameCardUidCardType_WritableProd = 0xFF,
    GameCardUidCardType_Count        = 3        ///< Total values supported by this enum.
} GameCardUidCardType;

typedef struct {
    GameCardUidMakerCode maker_code;
    u8 version;
    GameCardUidCardType card_type;
    u8 unique_data[0x9];
    u32 random;
    u8 platform_flag;                   ///< TODO: add enum with values.
    u8 reserved[0xB];
    FsCardId1 card_id1_mirror;          ///< This field mirrors bit 5 of FsCardId1MemoryType (not always the case?).
    u8 mac[0x20];
} GameCardUid;

NXDT_ASSERT(GameCardUid, 0x40);

/// Plaintext area. Dumped from FS program memory.
/// Overall structure may change with each new LAFW version.
typedef struct {
    u32 asic_security_mode; ///< Determines how the Lotus ASIC initialised the gamecard security mode. Usually 0xFFFFFFF9.
    u32 asic_status;        ///< Bitmask of the internal gamecard interface status. Usually 0x20000000.
    FsCardId1 card_id1;
    FsCardId2 card_id2;
    GameCardUid card_uid;
    u8 reserved[0x190];
    u8 mac[0x20];           ///< Changes with each gamecard (re)insertion.
} GameCardSpecificData;

NXDT_ASSERT(GameCardSpecificData, 0x200);

/// Plaintext area. Dumped from FS program memory.
/// This struct is returned by Lotus command "ChangeToSecureMode" (0xF). This means it is only available *after* the gamecard secure area has been mounted.
/// A copy of the gamecard header without the RSA-2048 signature and a plaintext GameCardInfo precedes this struct in FS program memory.
typedef struct {
    GameCardSpecificData specific_data;
    FsGameCardCertificate certificate;
    union {
        GameCardInitialData initial_data;   ///< Available with T1 gamecards.
        u8 unknown[0x200];                  ///< Available with T2 gamecards.
    };
} GameCardSecurityInformation;

NXDT_ASSERT(GameCardSecurityInformation, 0x800);

typedef enum : u8 {
    GameCardKekIndex_Version0      = 0,
    GameCardKekIndex_VersionForDev = 1,
    GameCardKekIndex_Count         = 2  ///< Total values supported by this enum.
} GameCardKekIndex;

typedef struct {
    GameCardKekIndex kek_index : 4;
    u8 titlekey_dec_index      : 4;
} GameCardKeyIndex;

NXDT_ASSERT(GameCardKeyIndex, 0x1);

typedef enum : u8 {
    GameCardRomSize_1GiB  = 0xFA,
    GameCardRomSize_2GiB  = 0xF8,
    GameCardRomSize_4GiB  = 0xF0,
    GameCardRomSize_8GiB  = 0xE0,
    GameCardRomSize_16GiB = 0xE1,
    GameCardRomSize_32GiB = 0xE2
} GameCardRomSize;

typedef enum : u8 {
    GameCardVersion_Default     = 0,
    GameCardVersion_Unknown1    = 1,
    GameCardVersion_Unknown2    = 2,
    GameCardVersion_T2Supported = 3,
    GameCardVersion_Count       = 4     ///< Total values supported by this enum.
} GameCardVersion;

typedef enum : u8 {
    GameCardFlags_None                             = 0,
    GameCardFlags_AutoBoot                         = BIT(0),    ///< The gamecard is capable of autobooting if it's inserted into the console before powering it up.
    GameCardFlags_HistoryErase                     = BIT(1),    ///< Inserting the gamecard won't add any permanent icons to the HOME menu.
    GameCardFlags_RepairTool                       = BIT(2),
    GameCardFlags_DifferentRegionCupToTerraDevice  = BIT(3),
    GameCardFlags_DifferentRegionCupToGlobalDevice = BIT(4),
    GameCardFlags_CardHeaderSignKey                = BIT(7),
    GameCardFlags_Count                            = 6          ///< Total values supported by this enum.
} GameCardFlags;

typedef enum : u32 {
    GameCardSelSec_ForT1 = 1,
    GameCardSelSec_ForT2 = 2,
    GameCardSelSec_Count = 2    ///< Total values supported by this enum.
} GameCardSelSec;

typedef enum : u64 {
    GameCardFwVersion_ForDev       = 0,
    GameCardFwVersion_Since100NUP  = 1, ///< upp_version >= 0 (0.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Since400NUP  = 2, ///< upp_version >= 268435456 (4.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Since900NUP  = 3, ///< upp_version >= 603979776 (9.0.0-0.0) in GameCardInfo. Seems to be unused.
    GameCardFwVersion_Since1100NUP = 4, ///< upp_version >= 738197504 (11.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Since1200NUP = 5, ///< upp_version >= 805306368 (12.0.0-0.0) in GameCardInfo.
    GameCardFwVersion_Count        = 6  ///< Total values supported by this enum.
} GameCardFwVersion;

typedef enum : u32 {
    GameCardAccCtrl1_25MHz = 0xA10011,
    GameCardAccCtrl1_50MHz = 0xA10010   ///< GameCardRomSize_8GiB or greater.
} GameCardAccCtrl1;

typedef enum : u8 {
    GameCardCompatibilityType_Normal = 0,
    GameCardCompatibilityType_Terra  = 1,
    GameCardCompatibilityType_Count  = 2    ///< Total values supported by this enum.
} GameCardCompatibilityType;

/// Encrypted using AES-128-CBC with the XCI header key (found in FS program memory under HOS 9.0.0+) and the reversed IV from `GameCardHeader`.
typedef struct {
    GameCardFwVersion fw_version;                   ///< T1: GameCardFwVersion value. T2: set to UINT64_MAX.
    GameCardAccCtrl1 acc_ctrl_1;
    u32 wait_1_time_read;                           ///< Always 0x1388.
    u32 wait_2_time_read;                           ///< Always 0.
    u32 wait_1_time_write;                          ///< Always 0.
    u32 wait_2_time_write;                          ///< Always 0.
    Version fw_mode;                                ///< Current SDK version.
    Version upp_version;                            ///< Bundled system update version.
    GameCardCompatibilityType compatibility_type;
    u8 reserved_1[0x3];
    u64 upp_hash;                                   ///< Checksum for the update partition. The exact way it's calculated is currently unknown.
    u64 upp_id;                                     ///< Must match GAMECARD_UPDATE_TID.
    u8 reserved_2[0x38];
} GameCardInfo;

NXDT_ASSERT(GameCardInfo, 0x70);

/// Placed after the `GameCardKeyArea` section.
typedef struct {
    u8 signature[0x100];                            ///< RSA-2048-PKCS#1 v1.5 with SHA-256 signature over the rest of the header. Verified with Ca10Modulus.
    u32 magic;                                      ///< "HEAD".
    u32 rom_area_start_page;                        ///< Expressed in GAMECARD_PAGE_SIZE units.
    u32 backup_area_start_page;                     ///< Always 0xFFFFFFFF.
    GameCardKeyIndex key_index;
    GameCardRomSize rom_size;
    GameCardVersion version;
    GameCardFlags flags;
    u8 package_id[0x8];                             ///< Used for challenge-response authentication.
    u32 valid_data_end_page;                        ///< Expressed in GAMECARD_PAGE_SIZE units.
    u8 reserved_1[0x4];
    u8 card_info_iv[AES_128_KEY_SIZE];              ///< AES-128-CBC IV for the CardInfo area (reversed).
    u64 partition_fs_header_address;                ///< Root Hash File System header offset.
    u64 partition_fs_header_size;                   ///< Root Hash File System header size.
    u8 partition_fs_header_hash[SHA256_HASH_SIZE];
    u8 initial_data_hash[SHA256_HASH_SIZE];         ///< T1: GameCardInitialData checksum. T2: all zeroes.
    GameCardSelSec sel_sec;
    u32 sel_t1_key;                                 ///< T1: always 0x02. T2: always 0x00.
    u32 sel_key;                                    ///< Always 0x00.
    u32 lim_area_page;                              ///< Expressed in GAMECARD_PAGE_SIZE units.
    GameCardInfo card_info;
} GameCardHeader;

NXDT_ASSERT(GameCardHeader, 0x200);

/// Encrypted using AES-128-CBC with the XCI header key (found in FS program memory under HOS 9.0.0+) and the reversed IV from `GameCardHeader2`.
typedef struct {
    GameCardFwVersion fw_version;
    GameCardAccCtrl1 acc_ctrl_1;
    u32 wait_1_time_read;                           ///< Always 0x1388.
    u32 wait_2_time_read;                           ///< Always 0.
    u32 wait_1_time_write;                          ///< Always 0.
    u32 wait_2_time_write;                          ///< Always 0.
    Version fw_mode;                                ///< Current SDK version.
    Version upp_version;                            ///< Bundled system update version.
    GameCardCompatibilityType compatibility_type;
    u8 reserved_1[0x3];
    u64 upp_hash;                                   ///< Checksum for the update partition. The exact way it's calculated is currently unknown.
    u64 upp_id;                                     ///< Must match GAMECARD_UPDATE_TID.
    u8 reserved_2[0x8];
    u8 header_hash[SHA256_HASH_SIZE];               ///< SHA-256 hash for the GameCardHeader block.
    u8 reserved_3[0x10];
} GameCardInfo2;

NXDT_ASSERT(GameCardInfo2, 0x70);

typedef enum : u8 {
    GameCardFlags2_None               = 0,
    GameCardFlags2_IsSecondCardHeader = BIT(0), ///< Always enabled.
    GameCardFlags2_HasSecureContent   = BIT(1), ///< Only enabled in Ounce gamecards.
    GameCardFlags2_Count              = 2       ///< Total values supported by this enum.
} GameCardFlags2;

/// Placed immediately after the `GameCardHeader` section in T2 gamecards.
typedef struct {
    u8 signature[0x100];                            ///< RSA-2048-PKCS#1 v1.5 with SHA-256 signature over the rest of the header. Verified with Ca10Modulus.
    u32 magic;                                      ///< "HEAD".
    u32 rom_area_start_page;                        ///< Expressed in GAMECARD_PAGE_SIZE units.
    u32 backup_area_start_page;                     ///< Always 0xFFFFFFFF.
    GameCardKeyIndex key_index;
    GameCardRomSize rom_size;
    GameCardVersion version;
    GameCardFlags flags;
    u8 package_id[0x8];                             ///< Used for challenge-response authentication. Differs from the Package ID value in GameCardHeader.
    u32 valid_data_end_page;                        ///< Expressed in GAMECARD_PAGE_SIZE units.
    u8 sign_key_index;                              ///< 20.0.0+. TODO: add enum with values.
    GameCardFlags2 flags_2;                         ///< 18.0.0+.
    u16 application_id_list_entry_count;            ///< Number of entries in the application ID list located right before valid_data_end_page (19.0.0+).
    u8 card_info_iv[AES_128_KEY_SIZE];              ///< AES-128-CBC IV for the CardInfo area (reversed).
    u64 partition_fs_header_address;                ///< Root Hash File System header offset.
    u64 partition_fs_header_size;                   ///< Root Hash File System header size.
    u8 partition_fs_header_hash[SHA256_HASH_SIZE];
    u8 initial_data_hash[SHA256_HASH_SIZE];         ///< All zeroes.
    GameCardSelSec sel_sec;
    u32 sel_t1_key;                                 ///< Always 0x02.
    u32 sel_key;                                    ///< Always 0x00.
    u32 lim_area_page;                              ///< Expressed in GAMECARD_PAGE_SIZE units.
    GameCardInfo2 card_info_2;
} GameCardHeader2;

NXDT_ASSERT(GameCardHeader2, 0x200);

/// Placed immediately after the `GameCardHeader2` section.
/// Immediately followed by a 0x100-byte long RSA public key that's used to verify this certificate's signature.
typedef struct {
    u8 signature[0x100];        ///< RSA-2048-PKCS#1 v1.5 with SHA-256 signature over the data from 0x100 to 0x300. Verified with Ca10CertificateModulus.
    u32 magic;                  ///< "CHVC".
    u32 version;                ///< Always set to 1.
    u8 unknown[0x8];
    u8 sign_key_index;          ///< Matches sign_key_index field from GameCardHeader2. TODO: add enum with values.
    u8 reserved_1[0x1F];
    u8 public_key[0x100];       ///< RSA modulus used to verify the signature from GameCardHeader2.
    u8 public_exponent[0x3];    ///< RSA exponent used to verify the signature from GameCardHeader2.
    u8 reserved_2[0x1CD];
} GameCardHeader2Certificate;

NXDT_ASSERT(GameCardHeader2Certificate, 0x400);

typedef enum : u8 {
    GameCardStatus_NotInserted                     = 0, ///< No gamecard is inserted.
    GameCardStatus_Processing                      = 1, ///< A gamecard has been inserted and it's being processed.
    GameCardStatus_NoGameCardPatchEnabled          = 2, ///< A gamecard has been inserted, but the running CFW enabled the "nogc" patch at boot.
                                                        ///< This triggers an error whenever fsDeviceOperatorGetGameCardHandle is called. Nothing at all can be done with the inserted gamecard.
    GameCardStatus_LotusAsicFirmwareUpdateRequired = 3, ///< A gamecard has been inserted, but a LAFW update is needed before being able to read the secure storage area.
                                                        ///< Operations on the normal storage area are still possible, though.
    GameCardStatus_OunceGameCardInserted           = 4, ///< A Switch 2 gamecard has been inserted. Access to the secure storage area is completely blocked off.
                                                        ///< Operations on the fake normal storage area presented by the gamecard are still possible, though.
    GameCardStatus_InsertedAndInfoNotLoaded        = 5, ///< A gamecard has been inserted, but an unexpected error unrelated to both "nogc" patch and LAFW version occurred.
    GameCardStatus_InsertedAndInfoLoaded           = 6, ///< A gamecard has been inserted and all required information could be successfully retrieved from it.
    GameCardStatus_Count                           = 7  ///< Total values supported by this enum.
} GameCardStatus;

/// Plaintext Lotus ASIC firmware (LAFW) blob. Dumped from FS program memory.
typedef struct {
    u8 signature[0x100];
    u32 magic;                  ///< "LAFW".
    u8 prod_fw_flag;
    u8 dev_fw_flag;
    u8 writer_fw_flag;
    u8 reserved_1[0x9];
    struct {
        u64 fw_version : 62;    ///< Stored using a bitmask.
        u64 is_prod    : 1;
        u64 is_dev     : 1;
    };
    u32 fw_size;
    u8 reserved_2[0x4];
    u8 fw_iv[AES_128_KEY_SIZE];
    u8 lotus3_device_id[0x10];  ///< "IDIDIDIDIDIDIDID".
    u8 reserved_3[0x40];
    u8 fw_data[0x7680];         ///< Encrypted.
} LotusAsicFirmwareBlob;

NXDT_ASSERT(LotusAsicFirmwareBlob, 0x7800);

typedef enum : u8 {
    LotusAsicFirmwareType_ReadFw    = 0,
    LotusAsicFirmwareType_ReadDevFw = 1,
    LotusAsicFirmwareType_WriterFw  = 2,
    LotusAsicFirmwareType_Invalid   = 3,    ///< Placeholder.
    LotusAsicFirmwareType_Count     = 4     ///< Total values supported by this enum.
} LotusAsicFirmwareType;

typedef enum : u8 {
    LotusAsicDeviceType_Test     = 0,
    LotusAsicDeviceType_Dev      = 1,
    LotusAsicDeviceType_Prod     = 2,
    LotusAsicDeviceType_Prod2Dev = 3,
    LotusAsicDeviceType_Invalid  = 4,
    LotusAsicDeviceType_Count    = 5    ///< Total values supported by this enum.
} LotusAsicDeviceType;

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
GameCardStatus gamecardGetStatus(void);

/// Fills the provided GameCardSecurityInformation element.
/// This area can't be read using gamecardReadStorage().
bool gamecardGetSecurityInformation(GameCardSecurityInformation *out);

/// Fills the provided FsGameCardIdSet element.
/// This area can't be read using gamecardReadStorage().
bool gamecardGetCardIdSet(FsGameCardIdSet *out);

/// Fills the provided pointers with LAFW blob data from FS program memory.
/// 'out_lafw_blob' or 'out_lafw_version' may be set to NULL, but at least one of them must be a valid pointer.
bool gamecardGetLotusAsicFirmwareBlob(LotusAsicFirmwareBlob *out_lafw_blob, u64 *out_lafw_version);

/// Used to determine whether the inserted gamecard uses the T2 security scheme (extra header + certificate).
bool gamecardIsT2(bool *out);

/// Used to read raw data from the inserted gamecard. Supports unaligned reads.
/// All required handles, changes between normal <-> secure storage areas and proper offset calculations are managed internally.
/// 'offset' + 'read_size' must not exceed the value returned by gamecardGetTotalSize().
bool gamecardReadStorage(void *out, u64 read_size, u64 offset);

/// Fills the provided GameCardHeader element.
/// This area can also be read using gamecardReadStorage(), starting at offset 0x0.
bool gamecardGetHeader(GameCardHeader *out);

/// Fills the provided GameCardInfo element.
bool gamecardGetPlaintextCardInfoArea(GameCardInfo *out);

/// Fills the provided GameCardHeader2 element.
/// This area can also be read using gamecardReadStorage(), starting at offset 0x200.
/// Only usable if the inserted gamecard relies on the T2 security scheme.
bool gamecardGetHeader2(GameCardHeader2 *out);

/// Fills the provided GameCardInfo2 element.
/// Only usable if the inserted gamecard relies on the T2 security scheme.
bool gamecardGetPlaintextCardInfo2Area(GameCardInfo2 *out);

/// Fills the provided GameCardHeader2Certificate element.
/// This area can also be read using gamecardReadStorage(), starting at offset 0x400.
/// Only usable if the inserted gamecard relies on the T2 security scheme.
bool gamecardGetHeader2Certificate(GameCardHeader2Certificate *out);

/// Fills the provided buffer with the public key from the GameCardHeader2Certificate area. This key matches Ca10CertificateModulus from FS.
/// The provided buffer must have a capacity of at least 0x100 bytes.
/// This area can also be read using gamecardReadStorage(), starting at offset 0x800.
/// Only usable if the inserted gamecard relies on the T2 security scheme.
bool gamecardGetHeader2CertificatePublicKey(void *out);

/// Fills the provided FsGameCardCertificate element.
/// This area can also be read using gamecardReadStorage(), starting at offset 0x7000.
bool gamecardGetCertificate(FsGameCardCertificate *out);

/// Fills the provided u64 pointer with the total gamecard size, which is the size taken by both Normal and Secure storage areas.
bool gamecardGetTotalSize(u64 *out);

/// Fills the provided u64 pointer with the trimmed gamecard size, which is the same as the size returned by gamecardGetTotalSize() but using the trimmed Secure storage area size.
bool gamecardGetTrimmedSize(u64 *out);

/// Fills the provided u64 pointer with the gamecard ROM capacity, based on the GameCardRomSize value from the header. Not the same as gamecardGetTotalSize().
bool gamecardGetRomCapacity(u64 *out);

/// Fills the provided Version element with the bundled firmware update version in the inserted gamecard.
bool gamecardGetBundledFirmwareUpdateVersion(Version *out);

/// Fills the provided HashFileSystemContext element using information from the requested Hash FS partition.
/// Hash FS functions can be used on the retrieved HashFileSystemContext. hfsFreeContext() must be used to free the underlying data from the filled context.
bool gamecardGetHashFileSystemContext(HashFileSystemPartitionType hfs_partition_type, HashFileSystemContext *out);

/// One-shot function to retrieve meaningful information from a Hash FS entry by name without using gamecardGetHashFileSystemContext() + Hash FS functions.
/// 'out_offset' or 'out_size' may be set to NULL, but at least one of them must be a valid pointer. The returned offset is always relative to the start of the gamecard image.
/// If you need to get entry information by index, just retrieve the Hash FS context for the target partition and use Hash FS functions on it.
bool gamecardGetHashFileSystemEntryInfoByName(HashFileSystemPartitionType hfs_partition_type, const char *entry_name, u64 *out_offset, u64 *out_size);

/// Returns a LotusAsicFirmwareType value for the provided LAFW blob.
LotusAsicFirmwareType gamecardGetLafwType(LotusAsicFirmwareBlob *lafw_blob);

/// Returns a LotusAsicDeviceType value for the provided LAFW blob.
LotusAsicDeviceType gamecardGetLafwDeviceType(LotusAsicFirmwareBlob *lafw_blob);

/// Takes a GameCardVersion value. Returns a pointer to a string that represents the provided version value.
/// Returns NULL if the provided value is out of range.
const char *gamecardGetVersionString(GameCardVersion version);

/// Takes a GameCardFwVersion value. Returns a pointer to a string that represents the minimum HOS version that matches the provided LAFW version.
/// Returns NULL if the provided value is out of range.
const char *gamecardGetRequiredHosVersionString(GameCardFwVersion fw_version);

/// Takes a GameCardCompatibilityType value. Returns a pointer to a string that represents the provided compatibility type.
/// Returns NULL if the provided value is out of range.
const char *gamecardGetCompatibilityTypeString(GameCardCompatibilityType compatibility_type);

/// Takes a LotusAsicFirmwareType value. Returns a pointer to a string that represents the provided LAFW type.
/// Returns NULL if the provided value is invalid.
const char *gamecardGetLafwTypeString(LotusAsicFirmwareType fw_type);

/// Takes a LotusAsicDeviceType value. Returns a pointer to a string that represents the provided LAFW device type.
/// Returns NULL if the provided value is out of range.
const char *gamecardGetLafwDeviceTypeString(LotusAsicDeviceType device_type);

#ifdef __cplusplus
}
#endif

#endif /* __GAMECARD_H__ */
