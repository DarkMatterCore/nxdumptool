/*
 * nca.h
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

#define NCA_USED_KEY_AREA_SIZE                      sizeof(NcaDecryptedKeyArea) /* Four keys, 0x40 bytes. */

#define NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT    5

#define NCA_IVFC_MAGIC                              0x49564643                  /* "IVFC". */
#define NCA_IVFC_MAX_LEVEL_COUNT                    7
#define NCA_IVFC_LEVEL_COUNT                        (NCA_IVFC_MAX_LEVEL_COUNT - 1)
#define NCA_IVFC_BLOCK_SIZE(x)                      (1U << (x))

#define NCA_BKTR_MAGIC                              0x424B5452                  /* "BKTR". */

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

typedef enum {
    NcaKeyGenerationOld_100_230 = 0,
    NcaKeyGenerationOld_300     = 2
} NcaKeyGenerationOld;

typedef enum {
    NcaKeyAreaEncryptionKeyIndex_Application = 0,
    NcaKeyAreaEncryptionKeyIndex_Ocean       = 1,
    NcaKeyAreaEncryptionKeyIndex_System      = 2,
    NcaKeyAreaEncryptionKeyIndex_Count       = 3
} NcaKeyAreaEncryptionKeyIndex;

/// 'NcaKeyGeneration_Current' will always point to the last known key generation value.
typedef enum {
    NcaKeyGeneration_301_302  = 3,
    NcaKeyGeneration_400_410  = 4,
    NcaKeyGeneration_500_510  = 5,
    NcaKeyGeneration_600_610  = 6,
    NcaKeyGeneration_620      = 7,
    NcaKeyGeneration_700_801  = 8,
    NcaKeyGeneration_810_811  = 9,
    NcaKeyGeneration_900_901  = 10,
    NcaKeyGeneration_910_1202 = 11,
    NcaKeyGeneration_Current  = NcaKeyGeneration_910_1202,
    NcaKeyGeneration_Max      = 32
} NcaKeyGeneration;

/// 'NcaMainSignatureKeyGeneration_Current' will always point to the last known key generation value.
typedef enum {
    NcaMainSignatureKeyGeneration_100_811  = 0,
    NcaMainSignatureKeyGeneration_900_1202 = 1,
    NcaMainSignatureKeyGeneration_Current  = NcaMainSignatureKeyGeneration_900_1202,
    NcaMainSignatureKeyGeneration_Max      = (NcaMainSignatureKeyGeneration_Current + 1)
} NcaMainSignatureKeyGeneration;

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
/// Only the first 4 key entries are encrypted.
/// If a particular key entry is unused, it is zeroed out before this area is encrypted.
typedef struct {
    u8 aes_xts_1[AES_128_KEY_SIZE];     ///< AES-128-XTS key 0 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_xts_2[AES_128_KEY_SIZE];     ///< AES-128-XTS key 1 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_ctr[AES_128_KEY_SIZE];       ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtr crypto.
    u8 aes_ctr_ex[AES_128_KEY_SIZE];    ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtrEx crypto.
    u8 aes_ctr_hw[AES_128_KEY_SIZE];    ///< Unused AES-128-CTR key.
    u8 reserved[0xB0];
} NcaEncryptedKeyArea;

NXDT_ASSERT(NcaEncryptedKeyArea, 0x100);

/// First 0x400 bytes from every NCA.
typedef struct {
    u8 main_signature[0x100];                               ///< RSA-2048-PSS with SHA-256 signature over header using a fixed key.
    u8 acid_signature[0x100];                               ///< RSA-2048-PSS with SHA-256 signature over header using the ACID public key from the NPDM in ExeFS. Only used in Program NCAs.
    u32 magic;                                              ///< "NCA0" / "NCA2" / "NCA3".
    u8 distribution_type;                                   ///< NcaDistributionType.
    u8 content_type;                                        ///< NcaContentType.
    u8 key_generation_old;                                  ///< NcaKeyGenerationOld.
    u8 kaek_index;                                          ///< NcaKeyAreaEncryptionKeyIndex.
    u64 content_size;
    u64 program_id;
    u32 content_index;
    VersionType2 sdk_addon_version;
    u8 key_generation;                                      ///< NcaKeyGeneration.
    u8 main_signature_key_generation;                       ///< NcaMainSignatureKeyGeneration.
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
    NcaHashType_Auto                  = 0,
    NcaHashType_None                  = 1,
    NcaHashType_HierarchicalSha256    = 2,  ///< Used by NcaFsType_PartitionFs.
    NcaHashType_HierarchicalIntegrity = 3   ///< Used by NcaFsType_RomFs.
} NcaHashType;

typedef enum {
    NcaEncryptionType_Auto     = 0,
    NcaEncryptionType_None     = 1,
    NcaEncryptionType_AesXts   = 2,
    NcaEncryptionType_AesCtr   = 3,
    NcaEncryptionType_AesCtrEx = 4
} NcaEncryptionType;

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
            ///< Used if hash_type == NcaHashType_HierarchicalSha256 (NcaFsType_PartitionFs and NCA0 NcaFsType_RomFs).
            NcaHierarchicalSha256Data hierarchical_sha256_data;
            u8 reserved_1[0x80];
        };
        struct {
            ///< Used if hash_type == NcaHashType_HierarchicalIntegrity (NcaFsType_RomFs).
            NcaIntegrityMetaInfo integrity_meta_info;
            u8 reserved_2[0x18];
        };
    };
} NcaHashData;

NXDT_ASSERT(NcaHashData, 0xF8);

typedef struct {
    u32 magic;          ///< "BKTR".
    u32 version;        ///< offset_count / node_count ?
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

/// Only used for NcaEncryptionType_AesCtrEx (PatchRomFs).
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
    NcaBucketInfo sparse_bucket;
    u64 physical_offset;
    u16 generation;
    u8 reserved[0x6];
} NcaSparseInfo;

NXDT_ASSERT(NcaSparseInfo, 0x30);

/// Four NCA FS headers are placed right after the 0x400 byte long NCA header in NCA2 and NCA3.
/// NCA0 place the FS headers at the start sector from the NcaFsInfo entries.
typedef struct {
    u16 version;
    u8 fs_type;                         ///< NcaFsType.
    u8 hash_type;                       ///< NcaHashType.
    u8 encryption_type;                 ///< NcaEncryptionType.
    u8 reserved_1[0x3];
    NcaHashData hash_data;
    NcaPatchInfo patch_info;
    NcaAesCtrUpperIv aes_ctr_upper_iv;
    NcaSparseInfo sparse_info;
    u8 reserved_2[0x88];
} NcaFsHeader;

NXDT_ASSERT(NcaFsHeader, 0x200);

typedef enum {
    NcaFsSectionType_PartitionFs = 0,   ///< NcaFsType_PartitionFs + NcaHashType_HierarchicalSha256.
    NcaFsSectionType_RomFs       = 1,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity.
    NcaFsSectionType_PatchRomFs  = 2,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity + NcaEncryptionType_AesCtrEx.
    NcaFsSectionType_Nca0RomFs   = 3,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalSha256 + NcaVersion_Nca0.
    NcaFsSectionType_Invalid     = 4
} NcaFsSectionType;

/// Unlike NCA contexts, we don't need to keep a hash for the NCA FS section header in NCA FS section contexts.
/// This is because the functions that modify the NCA FS section header also update the NCA FS section header hash stored in the NCA header.
typedef struct {
    bool enabled;
    void *nca_ctx;                      ///< NcaContext. Used to perform NCA reads.
    NcaFsHeader header;                 ///< Plaintext NCA FS section header.
    NcaFsHeader encrypted_header;       ///< Encrypted NCA FS section header. If the plaintext NCA FS section header is modified, this will hold an encrypted copy of it.
                                        ///< Otherwise, this holds the unmodified, encrypted NCA FS section header.
    bool header_written;                ///< Set to true after this FS section header has been written to an output dump.
    u8 section_num;
    u64 section_offset;
    u64 section_size;
    u8 section_type;                    ///< NcaFsSectionType.
    u8 encryption_type;                 ///< NcaEncryptionType.
    u8 ctr[AES_BLOCK_SIZE];             ///< Used to update the AES CTR context IV based on the desired offset.
    Aes128CtrContext ctr_ctx;
    Aes128XtsContext xts_decrypt_ctx;
    Aes128XtsContext xts_encrypt_ctx;
} NcaFsSectionContext;

typedef enum {
    NcaVersion_Nca0 = 0,
    NcaVersion_Nca2 = 2,
    NcaVersion_Nca3 = 3
} NcaVersion;

typedef struct {
    u8 aes_xts_1[AES_128_KEY_SIZE];     ///< AES-128-XTS key 0 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_xts_2[AES_128_KEY_SIZE];     ///< AES-128-XTS key 1 used for NCA FS sections with NcaEncryptionType_AesXts crypto.
    u8 aes_ctr[AES_128_KEY_SIZE];       ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtr crypto.
    u8 aes_ctr_ex[AES_128_KEY_SIZE];    ///< AES-128-CTR key used for NCA FS sections with NcaEncryptionType_AesCtrEx crypto.
} NcaDecryptedKeyArea;

NXDT_ASSERT(NcaDecryptedKeyArea, 0x40);

typedef struct {
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
    u8 key_generation;                                  ///< NcaKeyGenerationOld / NcaKeyGeneration. Retrieved from the decrypted header.
    u8 id_offset;                                       ///< Retrieved from NcmContentInfo.
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
    bool content_type_ctx_patch;                        ///< Set to true if a NCA patch generated by the content type context is needed and hasn't been completely writen yet.
    u32 content_type_ctx_data_idx;                      ///< Start index for the data generated by the content type context. Used while creating NSPs.
} NcaContext;

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
/// If the 'tik' argument points to a valid Ticket element, it will either be updated (if it's empty) or be used to read ticket data that has already been retrieved.
/// If the 'tik' argument is NULL, the function will just retrieve the necessary ticket data on its own.
/// If ticket data can't be retrieved, the context will still be initialized, but anything that involves working with encrypted NCA FS section blocks won't be possible (e.g. ncaReadFsSection()).
bool ncaInitializeContext(NcaContext *out, u8 storage_id, u8 hfs_partition_type, const NcmContentInfo *content_info, Ticket *tik);

/// Reads raw encrypted data from a NCA using an input context, previously initialized by ncaInitializeContext().
/// Input offset must be relative to the start of the NCA content file.
bool ncaReadContentFile(NcaContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads decrypted data from a NCA FS section using an input context.
/// Input offset must be relative to the start of the NCA FS section.
/// If dealing with Patch RomFS sections, this function should only be used when *not* reading BKTR AesCtrEx storage data. Use ncaReadAesCtrExStorageFromBktrSection() for that.
bool ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads decrypted BKTR AesCtrEx storage data from a NCA Patch RomFS section using an input context and a AesCtrEx CTR value.
/// Input offset must be relative to the start of the NCA FS section.
bool ncaReadAesCtrExStorageFromBktrSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset, u32 ctr_val);

/// Returns a pointer to a dynamically allocated buffer used to encrypt the input plaintext data, based on the encryption type used by the input NCA FS section, as well as its offset and size.
/// Input offset must be relative to the start of the NCA FS section.
/// Output size and offset are guaranteed to be aligned to the AES sector size used by the encryption type from the FS section.
/// Output offset is relative to the start of the NCA content file, making it easier to use the output encrypted block to seamlessly replace data while dumping a NCA.
/// This function isn't compatible with Patch RomFS sections.
/// Used internally by both ncaGenerateHierarchicalSha256Patch() and ncaGenerateHierarchicalIntegrityPatch().
void *ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset);

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

NX_INLINE bool ncaValidateHierarchicalSha256Offsets(NcaHierarchicalSha256Data *hierarchical_sha256_data, u64 section_size)
{
    if (!hierarchical_sha256_data || !section_size || !hierarchical_sha256_data->hash_block_size || !hierarchical_sha256_data->hash_region_count || \
        hierarchical_sha256_data->hash_region_count > NCA_HIERARCHICAL_SHA256_MAX_REGION_COUNT) return false;
    
    for(u32 i = 0; i < hierarchical_sha256_data->hash_region_count; i++)
    {
        NcaRegion *hash_region = &(hierarchical_sha256_data->hash_region[i]);
        if (!hash_region->size || (hash_region->offset + hash_region->size) > section_size) return false;
    }
    
    return true;
}

NX_INLINE bool ncaValidateHierarchicalIntegrityOffsets(NcaIntegrityMetaInfo *integrity_meta_info, u64 section_size)
{
    if (!integrity_meta_info || !section_size || __builtin_bswap32(integrity_meta_info->magic) != NCA_IVFC_MAGIC || integrity_meta_info->master_hash_size != SHA256_HASH_SIZE || \
        integrity_meta_info->info_level_hash.max_level_count != NCA_IVFC_MAX_LEVEL_COUNT) return false;
    
    for(u32 i = 0; i < NCA_IVFC_LEVEL_COUNT; i++)
    {
        NcaHierarchicalIntegrityVerificationLevelInformation *level_information = &(integrity_meta_info->info_level_hash.level_information[i]);
        if (!level_information->size || !level_information->block_order || (level_information->offset + level_information->size) > section_size) return false;
    }
    
    return true;
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
