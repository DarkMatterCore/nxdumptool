/*
 * nca.h
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

#ifndef __NCA_H__
#define __NCA_H__

#include "tik.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NCA_FS_HEADER_COUNT                         4
#define NCA_FULL_HEADER_LENGTH                      (sizeof(NcaHeader) + (sizeof(NcaFsHeader) * NCA_FS_HEADER_COUNT))

#define NCA_NCA0_MAGIC                              0x4E434130                  /* "NCA0". */
#define NCA_NCA2_MAGIC                              0x4E434132                  /* "NCA2". */
#define NCA_NCA3_MAGIC                              0x4E434133                  /* "NCA3". */

#define NCA_KEY_AREA_KEY_COUNT                      0x10
#define NCA_KEY_AREA_SIZE                           (NCA_KEY_AREA_KEY_COUNT * AES_128_KEY_SIZE)

#define NCA_KEY_AREA_USED_KEY_COUNT                 3
#define NCA_KEY_AREA_USED_SIZE                      (NCA_KEY_AREA_USED_KEY_COUNT * AES_128_KEY_SIZE)

#define NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT    5

#define NCA_IVFC_MAGIC                              0x49564643                  /* "IVFC". */
#define NCA_IVFC_MAX_LEVEL_COUNT                    7
#define NCA_IVFC_LEVEL_COUNT                        (NCA_IVFC_MAX_LEVEL_COUNT - 1)
#define NCA_IVFC_BLOCK_SIZE(x)                      (1U << (x))

#define NCA_BKTR_MAGIC                              0x424B5452                  /* "BKTR". */
#define NCA_BKTR_VERSION                            1

#define NCA_FS_SECTOR_SIZE                          0x200
#define NCA_FS_SECTOR_OFFSET(x)                     ((u64)(x) * NCA_FS_SECTOR_SIZE)

#define NCA_AES_XTS_SECTOR_SIZE                     0x200

#define NCA_SIGNATURE_AREA_SIZE                     0x200                       /* Signature is calculated starting at the NCA header magic word. */

typedef enum {
    NcaDistributionType_Download = 0,
    NcaDistributionType_GameCard = 1
} NcaDistributionType;

typedef enum {
    NcaContentType_Program    = 0,
    NcaContentType_Meta       = 1,
    NcaContentType_Control    = 2,
    NcaContentType_Manual     = 3,
    NcaContentType_Data       = 4,
    NcaContentType_PublicData = 5
} NcaContentType;

/// 'NcaKeyGeneration_Current' will always point to the last known key generation value.
/// TODO: update on master key changes.
typedef enum {
    NcaKeyGeneration_Since100NUP  = 0,                              ///< 1.0.0 - 2.3.0.
    NcaKeyGeneration_Since300NUP  = 2,                              ///< 3.0.0.
    NcaKeyGeneration_Since301NUP  = 3,                              ///< 3.0.1 - 3.0.2.
    NcaKeyGeneration_Since400NUP  = 4,                              ///< 4.0.0 - 4.1.0.
    NcaKeyGeneration_Since500NUP  = 5,                              ///< 5.0.0 - 5.1.0.
    NcaKeyGeneration_Since600NUP  = 6,                              ///< 6.0.0 - 6.1.0.
    NcaKeyGeneration_Since620NUP  = 7,                              ///< 6.2.0.
    NcaKeyGeneration_Since700NUP  = 8,                              ///< 7.0.0 - 8.0.1.
    NcaKeyGeneration_Since810NUP  = 9,                              ///< 8.1.0 - 8.1.1.
    NcaKeyGeneration_Since900NUP  = 10,                             ///< 9.0.0 - 9.0.1.
    NcaKeyGeneration_Since910NUP  = 11,                             ///< 9.1.0 - 12.0.3.
    NcaKeyGeneration_Since1210NUP = 12,                             ///< 12.1.0.
    NcaKeyGeneration_Since1300NUP = 13,                             ///< 13.0.0 - 13.2.1.
    NcaKeyGeneration_Since1400NUP = 14,                             ///< 14.0.0 - 14.1.2.
    NcaKeyGeneration_Since1500NUP = 15,                             ///< 15.0.0 - 15.0.1.
    NcaKeyGeneration_Since1600NUP = 16,                             ///< 16.0.0 - 16.0.1.
    NcaKeyGeneration_Current      = NcaKeyGeneration_Since1600NUP,
    NcaKeyGeneration_Max          = 32
} NcaKeyGeneration;

typedef enum {
    NcaKeyAreaEncryptionKeyIndex_Application = 0,
    NcaKeyAreaEncryptionKeyIndex_Ocean       = 1,
    NcaKeyAreaEncryptionKeyIndex_System      = 2,
    NcaKeyAreaEncryptionKeyIndex_Count       = 3
} NcaKeyAreaEncryptionKeyIndex;

/// 'NcaSignatureKeyGeneration_Current' will always point to the last known key generation value.
/// TODO: update on signature keygen changes.
typedef enum {
    NcaSignatureKeyGeneration_Since100NUP = 0,                                      ///< 1.0.0 - 8.1.1.
    NcaSignatureKeyGeneration_Since900NUP = 1,                                      ///< 9.0.0 - 16.0.1.
    NcaSignatureKeyGeneration_Current     = NcaSignatureKeyGeneration_Since900NUP,
    NcaSignatureKeyGeneration_Max         = (NcaSignatureKeyGeneration_Current + 1)
} NcaSignatureKeyGeneration;

typedef struct {
    u32 start_sector;   ///< Expressed in NCA_FS_SECTOR_SIZE sectors.
    u32 end_sector;     ///< Expressed in NCA_FS_SECTOR_SIZE sectors.
    u32 hash_sector;
    u8 reserved[0x4];
} NcaFsInfo;

NXDT_ASSERT(NcaFsInfo, 0x10);

typedef struct {
    u8 hash[SHA256_HASH_SIZE];
} NcaFsHeaderHash;

NXDT_ASSERT(NcaFsHeaderHash, 0x20);

/// Encrypted NCA key area used to hold NCA FS section encryption keys. Zeroed out if the NCA uses titlekey crypto.
/// If a particular key entry is unused, it is zeroed out before this area is encrypted.
typedef struct {
    union {
        u8 keys[NCA_KEY_AREA_KEY_COUNT][AES_128_KEY_SIZE];
        struct {
            u8 aes_xts_1[AES_128_KEY_SIZE];                 ///< AES-128-XTS key 0 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
            u8 aes_xts_2[AES_128_KEY_SIZE];                 ///< AES-128-XTS key 1 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
            u8 aes_ctr[AES_128_KEY_SIZE];                   ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtr* and NcaEncryptionType_AesCtrEx* crypto.
            u8 aes_ctr_ex[AES_128_KEY_SIZE];                ///< Unused AES-128-CTR key.
            u8 aes_ctr_hw[AES_128_KEY_SIZE];                ///< Unused AES-128-CTR key.
            u8 reserved[0xB0];
        };
    };
} NcaEncryptedKeyArea;

NXDT_ASSERT(NcaEncryptedKeyArea, NCA_KEY_AREA_SIZE);

/// First 0x400 bytes from every NCA.
typedef struct {
    u8 main_signature[0x100];                               ///< RSA-2048-PSS with SHA-256 signature over header using a fixed key.
    u8 acid_signature[0x100];                               ///< RSA-2048-PSS with SHA-256 signature over header using the ACID public key from the NPDM in ExeFS. Only used in Program NCAs.
    u32 magic;                                              ///< "NCA0" / "NCA2" / "NCA3".
    u8 distribution_type;                                   ///< NcaDistributionType.
    u8 content_type;                                        ///< NcaContentType.
    u8 key_generation_old;                                  ///< NcaKeyGeneration. Only uses NcaKeyGeneration_Since100NUP and NcaKeyGeneration_Since300NUP values.
    u8 kaek_index;                                          ///< NcaKeyAreaEncryptionKeyIndex.
    u64 content_size;
    u64 program_id;
    u32 content_index;
    SdkAddOnVersion sdk_addon_version;
    u8 key_generation;                                      ///< NcaKeyGeneration. Uses NcaKeyGeneration_Since301NUP or greater values.
    u8 main_signature_key_generation;                       ///< NcaSignatureKeyGeneration.
    u8 reserved[0xE];
    FsRightsId rights_id;                                   ///< Used for titlekey crypto.
    NcaFsInfo fs_info[NCA_FS_HEADER_COUNT];                 ///< Start and end sectors for each NCA FS section.
    NcaFsHeaderHash fs_header_hash[NCA_FS_HEADER_COUNT];    ///< SHA-256 hashes calculated over each NCA FS section header.
    NcaEncryptedKeyArea encrypted_key_area;
} NcaHeader;

NXDT_ASSERT(NcaHeader, 0x400);

typedef enum {
    NcaFsType_RomFs       = 0,
    NcaFsType_PartitionFs = 1
} NcaFsType;

typedef enum {
    NcaHashType_Auto                      = 0,
    NcaHashType_None                      = 1,  ///< Possibly used by all filesystem types.
    NcaHashType_HierarchicalSha256        = 2,  ///< Used by NcaFsType_PartitionFs.
    NcaHashType_HierarchicalIntegrity     = 3,  ///< Used by NcaFsType_RomFs.
    NcaHashType_AutoSha3                  = 4,
    NcaHashType_HierarchicalSha3256       = 5,  ///< Used by NcaFsType_PartitionFs.
    NcaHashType_HierarchicalIntegritySha3 = 6   ///< Used by NcaFsType_RomFs.
} NcaHashType;

typedef enum {
    NcaEncryptionType_Auto                  = 0,
    NcaEncryptionType_None                  = 1,
    NcaEncryptionType_AesXts                = 2,
    NcaEncryptionType_AesCtr                = 3,
    NcaEncryptionType_AesCtrEx              = 4,
    NcaEncryptionType_AesCtrSkipLayerHash   = 5,
    NcaEncryptionType_AesCtrExSkipLayerHash = 6
} NcaEncryptionType;

typedef enum {
    NcaMetaDataHashType_None                      = 0,
    NcaMetaDataHashType_HierarchicalIntegrity     = 1,
    NcaMetaDataHashType_HierarchicalIntegritySha3 = 2
} NcaMetaDataHashType;

typedef struct {
    u64 offset;
    u64 size;
} NcaRegion;

NXDT_ASSERT(NcaRegion, 0x10);

/// Used by NcaFsType_PartitionFs and NCA0 NcaFsType_RomFs.
typedef struct {
    u8 master_hash[SHA256_HASH_SIZE];
    u32 hash_block_size;
    u32 hash_region_count;
    NcaRegion hash_region[NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT];
} NcaHierarchicalSha256Data;

NXDT_ASSERT(NcaHierarchicalSha256Data, 0x78);

typedef struct {
    u64 offset;
    u64 size;
    u32 block_order;    ///< Use NCA_IVFC_BLOCK_SIZE to calculate the actual block size using this value.
    u8 reserved[0x4];
} NcaHierarchicalIntegrityVerificationLevelInformation;

NXDT_ASSERT(NcaHierarchicalIntegrityVerificationLevelInformation, 0x18);

typedef struct {
    u8 value[0x20];
} NcaSignatureSalt;

NXDT_ASSERT(NcaSignatureSalt, 0x20);

#pragma pack(push, 1)
typedef struct {
    u32 max_level_count;                                                                            ///< Always NCA_IVFC_MAX_LEVEL_COUNT.
    NcaHierarchicalIntegrityVerificationLevelInformation level_information[NCA_IVFC_LEVEL_COUNT];
    NcaSignatureSalt signature_salt;
} NcaInfoLevelHash;
#pragma pack(pop)

NXDT_ASSERT(NcaInfoLevelHash, 0xB4);

/// Used by NcaFsType_RomFs.
typedef struct {
    u32 magic;                          ///< "IVFC".
    u32 version;
    u32 master_hash_size;               ///< Always SHA256_HASH_SIZE.
    NcaInfoLevelHash info_level_hash;
    u8 master_hash[SHA256_HASH_SIZE];
} NcaIntegrityMetaInfo;

NXDT_ASSERT(NcaIntegrityMetaInfo, 0xE0);

typedef struct {
    union {
        struct {
            ///< Used for NcaHashType_HierarchicalSha256 and NcaHashType_HierarchicalSha3256 (NcaFsType_PartitionFs and NCA0 NcaFsType_RomFs).
            NcaHierarchicalSha256Data hierarchical_sha256_data;
            u8 reserved_1[0x80];
        };
        struct {
            ///< Used if NcaHashType_HierarchicalIntegrity and NcaHashType_HierarchicalIntegritySha3 (NcaFsType_RomFs).
            NcaIntegrityMetaInfo integrity_meta_info;
            u8 reserved_2[0x18];
        };
    };
} NcaHashData;

NXDT_ASSERT(NcaHashData, 0xF8);

typedef struct {
    u32 magic;          ///< "BKTR".
    u32 version;        ///< Always NCA_BKTR_VERSION.
    u32 entry_count;
    u8 reserved[0x4];
} NcaBucketTreeHeader;

NXDT_ASSERT(NcaBucketTreeHeader, 0x10);

typedef struct {
    u64 offset;
    u64 size;
    NcaBucketTreeHeader header;
} NcaBucketInfo;

NXDT_ASSERT(NcaBucketInfo, 0x20);

/// Only used for NcaEncryptionType_AesCtrEx and NcaEncryptionType_AesCtrExSkipLayerHash (PatchRomFs).
typedef struct {
    NcaBucketInfo indirect_bucket;
    NcaBucketInfo aes_ctr_ex_bucket;
} NcaPatchInfo;

NXDT_ASSERT(NcaPatchInfo, 0x40);

typedef struct {
    union {
        u8 value[0x8];
        struct {
            u32 generation;
            u32 secure_value;
        };
    };
} NcaAesCtrUpperIv;

NXDT_ASSERT(NcaAesCtrUpperIv, 0x8);

/// Used in NCAs with sparse storage.
typedef struct {
    NcaBucketInfo bucket;
    u64 physical_offset;
    u16 generation;
    u8 reserved[0x6];
} NcaSparseInfo;

NXDT_ASSERT(NcaSparseInfo, 0x30);

/// Used in NCAs with LZ4-compressed sections.
typedef struct {
    NcaBucketInfo bucket;
    u8 reserved[0x8];
} NcaCompressionInfo;

NXDT_ASSERT(NcaCompressionInfo, 0x28);

typedef struct {
    u64 offset;
    u64 size;
    u8 hash[SHA256_HASH_SIZE];
} NcaMetaDataHashDataInfo;

NXDT_ASSERT(NcaMetaDataHashDataInfo, 0x30);

/// Four NCA FS headers are placed right after the 0x400 byte long NCA header in NCA2 and NCA3.
/// NCA0 place the FS headers at the start sector from the NcaFsInfo entries.
typedef struct {
    u16 version;
    u8 fs_type;                                 ///< NcaFsType.
    u8 hash_type;                               ///< NcaHashType.
    u8 encryption_type;                         ///< NcaEncryptionType.
    u8 metadata_hash_type;                      ///< NcaMetaDataHashType.
    u8 reserved_1[0x2];
    NcaHashData hash_data;
    NcaPatchInfo patch_info;
    NcaAesCtrUpperIv aes_ctr_upper_iv;
    NcaSparseInfo sparse_info;
    NcaCompressionInfo compression_info;
    NcaMetaDataHashDataInfo metadata_hash_info;
    u8 reserved_2[0x30];
} NcaFsHeader;

NXDT_ASSERT(NcaFsHeader, 0x200);

typedef enum {
    NcaFsSectionType_PartitionFs = 0,   ///< NcaFsType_PartitionFs + NcaHashType_HierarchicalSha256 OR NcaHashType_HierarchicalSha3256 + NcaEncryptionType_AesCtr OR NcaEncryptionType_AesCtrSkipLayerHash.
    NcaFsSectionType_RomFs       = 1,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity OR NcaHashType_HierarchicalIntegritySha3 + NcaEncryptionType_AesCtr OR NcaEncryptionType_AesCtrSkipLayerHash.
    NcaFsSectionType_PatchRomFs  = 2,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity OR NcaHashType_HierarchicalIntegritySha3 + NcaEncryptionType_AesCtrEx OR NcaEncryptionType_AesCtrExSkipLayerHash.
    NcaFsSectionType_Nca0RomFs   = 3,   ///< NcaVersion_Nca0 + NcaFsType_RomFs + NcaHashType_HierarchicalSha256 + NcaEncryptionType_AesXts.
    NcaFsSectionType_Invalid     = 4
} NcaFsSectionType;

// Forward declaration for NcaFsSectionContext.
typedef struct _NcaContext NcaContext;

/// Unlike NCA contexts, we don't need to keep a hash for the NCA FS section header in NCA FS section contexts.
/// This is because the functions that modify the NCA FS section header also update the NCA FS section header hash stored in the NCA header.
typedef struct {
    bool enabled;                       ///< Set to true if this NCA FS section has passed all validation checks and can be safely used.
    NcaContext *nca_ctx;                ///< NcaContext. Used to perform NCA reads.
    NcaFsHeader header;                 ///< Plaintext NCA FS section header.
    NcaFsHeader encrypted_header;       ///< Encrypted NCA FS section header. If the plaintext NCA FS section header is modified, this will hold an encrypted copy of it.
                                        ///< Otherwise, this holds the unmodified, encrypted NCA FS section header.
    u8 section_idx;                     ///< Index within [0 - 3].
    u64 section_offset;                 ///< Relative to the start of the NCA content file. Placed here for convenience.
    u64 section_size;                   ///< Placed here for convenience.
    u8 hash_type;                       ///< NcaHashType.
    u8 encryption_type;                 ///< NcaEncryptionType.
    u8 section_type;                    ///< NcaFsSectionType.

    ///< PatchInfo-related fields.
    bool has_patch_indirect_layer;      ///< Set to true if this NCA FS section has an Indirect patch layer.
    bool has_patch_aes_ctr_ex_layer;    ///< Set to true if this NCA FS section has an AesCtrEx patch layer.

    ///< SparseInfo-related fields.
    bool has_sparse_layer;              ///< Set to true if this NCA FS section has a sparse layer.
    u64 sparse_table_offset;            ///< header.sparse_info.physical_offset + header.sparse_info.bucket.offset. Relative to the start of the NCA content file. Placed here for convenience.
    u64 cur_sparse_virtual_offset;      ///< Current sparse layer virtual offset. Used for content decryption if a sparse layer is available.

    ///< CompressionInfo-related fields.
    bool has_compression_layer;         ///< Set to true if this NCA FS section has a compression layer.

    ///< Hash-layer-related fields.
    bool skip_hash_layer_crypto;        ///< Set to true if hash layer crypto should be skipped while reading section data.
    NcaRegion hash_region;              ///< Holds the properties for the full hash layer region that precedes the actual FS section data.

    ///< Crypto-related fields.
    u8 ctr[AES_BLOCK_SIZE];             ///< Used internally by NCA functions to update the AES-128-CTR context IV based on the desired offset.
    Aes128CtrContext ctr_ctx;           ///< Used internally by NCA functions to perform AES-128-CTR crypto.
    Aes128XtsContext xts_decrypt_ctx;   ///< Used internally by NCA functions to perform AES-128-XTS decryption.
    Aes128XtsContext xts_encrypt_ctx;   ///< Used internally by NCA functions to perform AES-128-XTS encryption.

    ///< NSP-related fields.
    bool header_written;                ///< Set to true after this FS section header has been written to an output dump.
} NcaFsSectionContext;

typedef enum {
    NcaVersion_Nca0 = 0,
    NcaVersion_Nca2 = 2,
    NcaVersion_Nca3 = 3
} NcaVersion;

typedef struct {
    union {
        u8 keys[NCA_KEY_AREA_USED_KEY_COUNT][AES_128_KEY_SIZE];
        struct {
            u8 aes_xts_1[AES_128_KEY_SIZE];                     ///< AES-128-XTS key 0 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
            u8 aes_xts_2[AES_128_KEY_SIZE];                     ///< AES-128-XTS key 1 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
            u8 aes_ctr[AES_128_KEY_SIZE];                       ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtr and NcaEncryptionType_AesCtrSkipLayerHash crypto.
        };
    };
} NcaDecryptedKeyArea;

NXDT_ASSERT(NcaDecryptedKeyArea, NCA_KEY_AREA_USED_SIZE);

struct _NcaContext {
    u8 storage_id;                                      ///< NcmStorageId.
    NcmContentStorage *ncm_storage;                     ///< Pointer to a NcmContentStorage instance. Used to read NCA data from eMMC/SD.
    u64 gamecard_offset;                                ///< Used to read NCA data from a gamecard using a FsStorage instance when storage_id == NcmStorageId_GameCard.
    NcmContentId content_id;                            ///< Also used to read NCA data.
    char content_id_str[0x21];
    u8 hash[SHA256_HASH_SIZE];                          ///< Manually calculated (if needed).
    char hash_str[0x41];
    u8 format_version;                                  ///< NcaVersion.
    u8 content_type;                                    ///< NcmContentType. Retrieved from NcmContentInfo.
    u64 content_size;                                   ///< Retrieved from NcmContentInfo.
    u8 key_generation;                                  ///< NcaKeyGeneration. Retrieved from the decrypted header.
    u8 id_offset;                                       ///< Retrieved from NcmContentInfo.
    u32 title_version;
    bool rights_id_available;
    bool titlekey_retrieved;
    bool valid_main_signature;
    u8 titlekey[AES_128_KEY_SIZE];                      ///< Decrypted titlekey from the ticket.
    NcaHeader header;                                   ///< Plaintext NCA header.
    u8 header_hash[SHA256_HASH_SIZE];                   ///< Plaintext NCA header hash. Used to determine if it's necessary to replace the NCA header while dumping this NCA.
    NcaHeader encrypted_header;                         ///< Encrypted NCA header. If the plaintext NCA header is modified, this will hold an encrypted copy of it.
                                                        ///< Otherwise, this holds the unmodified, encrypted NCA header.
    NcaDecryptedKeyArea decrypted_key_area;
    NcaFsSectionContext fs_ctx[NCA_FS_HEADER_COUNT];

    ///< NSP-related fields.
    bool header_written;                                ///< Set to true after the NCA header and the FS section headers have been written to an output dump.
    void *content_type_ctx;                             ///< Pointer to a content type context (e.g. ContentMetaContext, ProgramInfoContext, NacpContext, LegalInfoContext). Set to NULL if unused.
    bool content_type_ctx_patch;                        ///< Set to true if a NCA patch generated by the content type context is needed and hasn't been completely written yet.
    u32 content_type_ctx_data_idx;                      ///< Start index for the data generated by the content type context. Used while creating NSPs.
};

typedef struct {
    bool written;   ///< Set to true if this patch has already been written.
    u64 offset;     ///< New data offset (relative to the start of the NCA content file).
    u64 size;       ///< New data size.
    u8 *data;       ///< New data.
} NcaHashDataPatch;

typedef struct {
    bool written;                                                                   ///< Set to true if all hash region patches have been written.
    NcmContentId content_id;
    u32 hash_region_count;
    NcaHashDataPatch hash_region_patch[NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT];
} NcaHierarchicalSha256Patch;

typedef struct {
    bool written;                                               ///< Set to true if all hash level patches have been written.
    NcmContentId content_id;
    NcaHashDataPatch hash_level_patch[NCA_IVFC_LEVEL_COUNT];
} NcaHierarchicalIntegrityPatch;

/// Functions to control the internal heap buffer used by NCA FS section crypto operations.
/// Must be called at startup.
bool ncaAllocateCryptoBuffer(void);
void ncaFreeCryptoBuffer(void);

/// Initializes a NCA context.
/// If 'storage_id' == NcmStorageId_GameCard, the 'hfs_partition_type' argument must be a valid GameCardHashFileSystemPartitionType value.
/// If the NCA holds a populated Rights ID field, ticket data will need to be retrieved.
/// If the 'tik' argument points to a valid Ticket element, it will either be updated (if it's empty) or used to read ticket data that has already been retrieved.
/// If the 'tik' argument is NULL, the function will just retrieve the necessary ticket data on its own.
/// If ticket data can't be retrieved, the context will still be initialized, but anything that involves working with encrypted NCA FS section blocks won't be possible (e.g. ncaReadFsSection()).
bool ncaInitializeContext(NcaContext *out, u8 storage_id, u8 hfs_partition_type, const NcmContentInfo *content_info, u32 title_version, Ticket *tik);

/// Reads raw encrypted data from a NCA using an input context, previously initialized by ncaInitializeContext().
/// Input offset must be relative to the start of the NCA content file.
bool ncaReadContentFile(NcaContext *ctx, void *out, u64 read_size, u64 offset);

/// Retrieves the FS section's hierarchical hash target layer extents.
/// Output offset is relative to the start of the FS section.
/// Either 'out_offset' or 'out_size' can be NULL, but at least one of them must be a valid pointer.
bool ncaGetFsSectionHashTargetExtents(NcaFsSectionContext *ctx, u64 *out_offset, u64 *out_size);

/// Reads decrypted data from a NCA FS section using an input context.
/// Input offset must be relative to the start of the NCA FS section.
/// If dealing with Patch RomFS sections, this function should only be used when *not* reading AesCtrEx storage data. Use ncaReadAesCtrExStorage() for that.
bool ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads plaintext AesCtrEx storage data from a NCA Patch RomFS section using an input context and an AesCtrEx CTR value.
/// Input offset must be relative to the start of the NCA FS section.
bool ncaReadAesCtrExStorage(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val, bool decrypt);

/// Generates HierarchicalSha256 FS section patch data, which can be used to seamlessly replace NCA data.
/// Input offset must be relative to the start of the last HierarchicalSha256 hash region (actual underlying FS).
/// Bear in mind that this function recalculates both the NcaHashData block master hash and the NCA FS header hash from the NCA header.
/// As such, this function is not designed to generate more than one patch per HierarchicalSha256 FS section.
bool ncaGenerateHierarchicalSha256Patch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalSha256Patch *out);

/// Overwrites block(s) from a buffer holding raw NCA data using previously initialized NcaContext and NcaHierarchicalSha256Patch.
/// 'buf_offset' must hold the raw NCA offset where the data stored in 'buf' was read from.
/// The 'written' fields from the input NcaHierarchicalSha256Patch and its underlying NcaHashDataPatch elements are updated by this function.
void ncaWriteHierarchicalSha256PatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalSha256Patch *patch, void *buf, u64 buf_size, u64 buf_offset);

/// Generates HierarchicalIntegrity FS section patch data, which can be used to seamlessly replace NCA data.
/// Input offset must be relative to the start of the last HierarchicalIntegrity hash level (actual underlying FS).
/// Bear in mind that this function recalculates both the NcaHashData block master hash and the NCA FS header hash from the NCA header.
/// As such, this function is not designed to generate more than one patch per HierarchicalIntegrity FS section.
bool ncaGenerateHierarchicalIntegrityPatch(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, NcaHierarchicalIntegrityPatch *out);

/// Overwrites block(s) from a buffer holding raw NCA data using a previously initialized NcaContext and NcaHierarchicalIntegrityPatch.
/// 'buf_offset' must hold the raw NCA offset where the data stored in 'buf' was read from.
/// The 'written' fields from the input NcaHierarchicalIntegrityPatch and its underlying NcaHashDataPatch elements are updated by this function.
void ncaWriteHierarchicalIntegrityPatchToMemoryBuffer(NcaContext *ctx, NcaHierarchicalIntegrityPatch *patch, void *buf, u64 buf_size, u64 buf_offset);

/// Sets the distribution type field from the underlying NCA header in the provided NCA context to NcaDistributionType_Download.
/// Needed for NSP dumps from gamecard titles.
void ncaSetDownloadDistributionType(NcaContext *ctx);

/// Removes titlekey crypto dependency from a NCA context by wiping the Rights ID from the underlying NCA header and copying the decrypted titlekey to the NCA key area.
bool ncaRemoveTitleKeyCrypto(NcaContext *ctx);

/// Encrypts NCA header and NCA FS headers.
/// The 'encrypted_header' member from the NCA context and its underlying NCA FS section contexts is updated by this function.
/// Internally uses ncaIsHeaderDirty() to determine if NCA header / NCA FS section header re-encryption is needed.
bool ncaEncryptHeader(NcaContext *ctx);

/// Used to replace the NCA header and the NCA FS section headers while writing a NCA if they were modified in any way.
/// Overwrites block(s) from a buffer holding raw NCA data using a previously initialized NcaContext.
/// 'buf_offset' must hold the raw NCA offset where the data stored in 'buf' was read from.
/// Bear in mind this function doesn't call ncaIsHeaderDirty() on its own to avoid taking up too much execution time, so it will attempt to overwrite data even if it isn't needed.
void ncaWriteEncryptedHeaderDataToMemoryBuffer(NcaContext *ctx, void *buf, u64 buf_size, u64 buf_offset);

/// Updates the content ID and hash from a NCA context using a provided SHA-256 checksum.
void ncaUpdateContentIdAndHash(NcaContext *ctx, u8 hash[SHA256_HASH_SIZE]);

/// Returns a pointer to a string holding the name of the section type from the provided NCA FS section context.
const char *ncaGetFsSectionTypeName(NcaFsSectionContext *ctx);

/// Helper inline functions.

NX_INLINE bool ncaIsHeaderDirty(NcaContext *ctx)
{
    if (!ctx) return false;
    u8 tmp_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(tmp_hash, &(ctx->header), sizeof(NcaHeader));
    return (memcmp(tmp_hash, ctx->header_hash, SHA256_HASH_SIZE) != 0);
}

NX_INLINE bool ncaVerifyBucketInfo(NcaBucketInfo *bucket)
{
    return (bucket && __builtin_bswap32(bucket->header.magic) == NCA_BKTR_MAGIC && bucket->header.version <= NCA_BKTR_VERSION && bucket->header.entry_count >= 0);
}

NX_INLINE void ncaFreeHierarchicalSha256Patch(NcaHierarchicalSha256Patch *patch)
{
    if (!patch) return;

    for(u32 i = 0; i < NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT; i++)
    {
        if (patch->hash_region_patch[i].data) free(patch->hash_region_patch[i].data);
    }

    memset(patch, 0, sizeof(NcaHierarchicalSha256Patch));
}

NX_INLINE void ncaFreeHierarchicalIntegrityPatch(NcaHierarchicalIntegrityPatch *patch)
{
    if (!patch) return;

    for(u32 i = 0; i < NCA_IVFC_LEVEL_COUNT; i++)
    {
        if (patch->hash_level_patch[i].data) free(patch->hash_level_patch[i].data);
    }

    memset(patch, 0, sizeof(NcaHierarchicalIntegrityPatch));
}

#ifdef __cplusplus
}
#endif

#endif /* __NCA_H__ */
