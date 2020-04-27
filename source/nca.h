/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#ifndef __NCA_H__
#define __NCA_H__

#include <switch.h>
#include "tik.h"

#define NCA_HEADER_LENGTH                       0x400
#define NCA_FS_HEADER_LENGTH                    0x200
#define NCA_FS_HEADER_COUNT                     4
#define NCA_FULL_HEADER_LENGTH                  (NCA_HEADER_LENGTH + (NCA_FS_HEADER_LENGTH * NCA_FS_HEADER_COUNT))

#define NCA_NCA0_MAGIC                          0x4E434130  /* "NCA0" */
#define NCA_NCA2_MAGIC                          0x4E434132  /* "NCA2" */
#define NCA_NCA3_MAGIC                          0x4E434133  /* "NCA3" */

#define NCA_HIERARCHICAL_SHA256_LAYER_COUNT     2

#define NCA_IVFC_MAGIC                          0x49564643  /* "IVFC" */
#define NCA_IVFC_LAYER_COUNT                    7
#define NCA_IVFC_HASH_DATA_LAYER_COUNT          5
#define NCA_IVFC_BLOCK_SIZE(x)                  (1 << (x))

#define NCA_BKTR_MAGIC                          0x424B5452  /* "BKTR" */

#define NCA_FS_ENTRY_BLOCK_SIZE                 0x200
#define NCA_FS_ENTRY_BLOCK_OFFSET(x)            ((u64)(x) * NCA_FS_ENTRY_BLOCK_SIZE)

#define NCA_AES_XTS_SECTOR_SIZE                 0x200
#define NCA_NCA0_FS_HEADER_AES_XTS_SECTOR(x)    (((x) - NCA_HEADER_LENGTH) >> 9)

typedef enum {
    NcaDistributionType_Download = 0,
    NcaDistributionType_GameCard = 1
} NcaDistributionType;

typedef enum {
    NcaContentType_Program       = 0,
    NcaContentType_Meta          = 1,
    NcaContentType_Control       = 2,
    NcaContentType_Manual        = 3,
    NcaContentType_Data          = 4,
    NcaContentType_PublicData    = 5
} NcaContentType;

typedef enum {
    NcaKeyGenerationOld_100_230 = 0,
    NcaKeyGenerationOld_300     = 2
} NcaKeyGenerationOld;

typedef enum {
    NcaKeyAreaEncryptionKeyIndex_Application = 0,
    NcaKeyAreaEncryptionKeyIndex_Ocean       = 1,
    NcaKeyAreaEncryptionKeyIndex_System      = 2
} NcaKeyAreaEncryptionKeyIndex;

typedef struct {
    u8 relstep;
    u8 micro;
    u8 minor;
    u8 major;
} NcaSdkAddOnVersion;

/// 'NcaKeyGeneration_Current' will always point to the last known key generation value.
typedef enum {
    NcaKeyGeneration_301_302   = 3,
    NcaKeyGeneration_400_410   = 4,
    NcaKeyGeneration_500_510   = 5,
    NcaKeyGeneration_600_610   = 6,
    NcaKeyGeneration_620       = 7,
    NcaKeyGeneration_700_801   = 8,
    NcaKeyGeneration_810_811   = 9,
    NcaKeyGeneration_900_901   = 10,
    NcaKeyGeneration_910_920   = 11,
    NcaKeyGeneration_1000_1001 = 12,
    NcaKeyGeneration_Current   = NcaKeyGeneration_1000_1001
} NcaKeyGeneration;

typedef struct {
    u32 start_block_offset; ///< Expressed in NCA_FS_ENTRY_BLOCK_SIZE blocks.
    u32 end_block_offset;   ///< Expressed in NCA_FS_ENTRY_BLOCK_SIZE blocks.
    u8 enable_entry;
    u8 reserved[0x7];
} NcaFsEntry;

typedef struct {
    u8 hash[SHA256_HASH_SIZE];
} NcaFsHash;

typedef struct {
    u8 key[0x10];
} NcaKey;

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
    NcaEncryptionType_AesCtrEx = 4,
    NcaEncryptionType_Nca0     = 5  ///< Only used to represent NCA0 AES XTS FS section crypto - not actually used as a possible value for this field.
} NcaEncryptionType;

typedef struct {
    u64 offset;
    u64 size;
} NcaHierarchicalSha256LayerInfo;

/// Used for NcaFsType_PartitionFs and NCA0 NcaFsType_RomFsRomFS.
typedef struct {
    u8 master_hash[SHA256_HASH_SIZE];
    u32 hash_block_size;
    u32 layer_count;
    NcaHierarchicalSha256LayerInfo hash_data_layer_info;
    NcaHierarchicalSha256LayerInfo hash_target_layer_info;
} NcaHierarchicalSha256;

typedef struct {
    u64 offset;
    u64 size;
    u32 block_size;     ///< Use NCA_IVFC_BLOCK_SIZE to calculate the actual block size using this value.
    u8 reserved[0x4];
} NcaHierarchicalIntegrityLayerInfo;

/// Used for NcaFsType_RomFs.
typedef struct {
    u32 magic;                                                                              ///< "IVFC".
    u32 version;
    u32 master_hash_size;
    u32 layer_count;
    NcaHierarchicalIntegrityLayerInfo hash_data_layer_info[NCA_IVFC_HASH_DATA_LAYER_COUNT];
    NcaHierarchicalIntegrityLayerInfo hash_target_layer_info;
    u8 signature_salt[0x20];
    u8 master_hash[0x20];
} NcaHierarchicalIntegrity;

typedef struct {
    union {
        struct {
            ///< Used if hash_type == NcaHashType_HierarchicalSha256 (NcaFsType_PartitionFs and NCA0 NcaFsType_RomFs).
            NcaHierarchicalSha256 hierarchical_sha256;
            u8 reserved_1[0xB0];
        };
        struct {
            ///< Used if hash_type == NcaHashType_HierarchicalIntegrity (NcaFsType_RomFs).
            NcaHierarchicalIntegrity hierarchical_integrity;
            u8 reserved_2[0x18];
        };
    };
} NcaHashInfo;

typedef struct {
    u32 magic;          ///< "BKTR".
    u32 bucket_count;
    u32 entry_count;
    u8 reserved[0x4];
} NcaBucketTreeHeader;

/// Only used for NcaEncryptionType_AesCtrEx (PatchRomFs).
typedef struct {
    u64 indirect_offset;
    u64 indirect_size;
    NcaBucketTreeHeader indirect_header;
    u64 aes_ctr_ex_offset;
    u64 aes_ctr_ex_size;
    NcaBucketTreeHeader aes_ctr_ex_header;
} NcaPatchInfo;

/// Format unknown.
typedef struct {
    u8 unknown[0x30];
} NcaSparseInfo;

typedef struct {
    u16 version;
    u8 fs_type;                 ///< NcaFsType.
    u8 hash_type;               ///< NcaHashType.
    u8 encryption_type;         ///< NcaEncryptionType.
    u8 reserved_1[0x3];
    NcaHashInfo hash_info;
    NcaPatchInfo patch_info;
    union {
        u8 section_ctr[0x8];
        struct {
            u32 generation;
            u32 secure_value;
        };
    };
    NcaSparseInfo sparse_info;
    u8 reserved_2[0x88];
} NcaFsHeader;

typedef struct {
    u8 main_signature[0x100];               ///< RSA-PSS signature over header with fixed key.
    u8 acid_signature[0x100];               ///< RSA-PSS signature over header with key in NPDM.
    u32 magic;                              ///< "NCA0" / "NCA2" / "NCA3".
    u8 distribution_type;                   ///< NcaDistributionType.
    u8 content_type;                        ///< NcaContentType.
    u8 key_generation_old;                  ///< NcaKeyGenerationOld.
    u8 kaek_index;                          ///< NcaKeyAreaEncryptionKeyIndex.
    u64 content_size;
    u64 program_id;
    u32 content_index;
    NcaSdkAddOnVersion sdk_addon_version;
    u8 key_generation;                      ///< NcaKeyGeneration.
    u8 main_signature_key_generation;
    u8 reserved_1[0xE];
    FsRightsId rights_id;                   ///< Used for titlekey crypto.
    NcaFsEntry fs_entries[4];               ///< Start and end offsets for each NCA FS section.
    NcaFsHash fs_hashes[4];                 ///< SHA-256 hashes calculated over each NCA FS section header.
    NcaKey encrypted_keys[4];               ///< Only the encrypted key at index #2 is used. The other three are zero filled before the key area is encrypted.
    u8 reserved_2[0xC0];
    NcaFsHeader fs_headers[4];              /// NCA FS section headers.
} NcaHeader;

typedef enum {
    NcaVersion_Nca0     = 0,
    NcaVersion_Nca2     = 1,
    NcaVersion_Nca3     = 2
} NcaVersion;

typedef enum {
    NcaFsSectionType_PartitionFs = 0,   ///< NcaFsType_PartitionFs + NcaHashType_HierarchicalSha256.
    NcaFsSectionType_RomFs       = 1,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity.
    NcaFsSectionType_PatchRomFs  = 2,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalIntegrity + NcaEncryptionType_AesCtrEx.
    NcaFsSectionType_Nca0RomFs   = 3,   ///< NcaFsType_RomFs + NcaHashType_HierarchicalSha256 + NcaVersion_Nca0.
    NcaFsSectionType_Invalid     = 4
} NcaFsSectionType;

typedef struct {
    void *nca_ctx;                      ///< NcaContext. Used to perform NCA reads.
    u8 section_num;
    u64 section_offset;
    u64 section_size;
    u8 section_type;                    ///< NcaFsSectionType.
    u8 encryption_type;                 ///< NcaEncryptionType.
    NcaFsHeader *header;
    u8 ctr[0x10];                       ///< Used to update the AES CTR context IV based on the desired offset.
    Aes128CtrContext ctr_ctx;
    Aes128XtsContext xts_decrypt_ctx;
    Aes128XtsContext xts_encrypt_ctx;
} NcaFsSectionContext;

typedef struct {
    u8 storage_id;                      ///< NcmStorageId.
    NcmContentStorage *ncm_storage;     ///< Pointer to a NcmContentStorage instance. Used to read NCA data.
    u64 gamecard_offset;                ///< Used to read NCA data from a gamecard using a FsStorage instance when storage_id == NcmStorageId_GameCard.
    NcmContentId content_id;            ///< Also used to read NCA data.
    char content_id_str[0x21];
    u8 hash[0x20];                      ///< Retrieved from NcmPackagedContentInfo.
    char hash_str[0x41];
    u8 format_version;                  ///< NcaVersion.
    u8 content_type;                    ///< NcmContentType. Retrieved from NcmPackagedContentInfo.
    u64 content_size;                   ///< Retrieved from NcmPackagedContentInfo.
    u8 key_generation;                  ///< NcaKeyGenerationOld / NcaKeyGeneration. Retrieved from the decrypted header.
    u8 id_offset;                       ///< Retrieved from NcmPackagedContentInfo.
    bool rights_id_available;
    u8 titlekey[0x10];
    bool dirty_header;
    NcaHeader header;
    NcaFsSectionContext fs_contexts[4];
    NcaKey decrypted_keys[4];
} NcaContext;

/// Functions to control the internal heap buffer used by NCA FS section crypto operations.
/// Must be called at startup.
bool ncaAllocateCryptoBuffer(void);
void ncaFreeCryptoBuffer(void);

/// Initializes a NCA context.
/// If 'storage_id' != NcmStorageId_GameCard, the 'ncm_storage' argument must point to a valid NcmContentStorage instance, previously opened using the same NcmStorageId value.
/// If 'storage_id' == NcmStorageId_GameCard, the 'hfs_partition_type' argument must be a valid GameCardHashFileSystemPartitionType value.
/// If the NCA holds a populated Rights ID field, and if the Ticket object pointed to by 'tik' hasn't been filled, ticket data will be retrieved.
bool ncaInitializeContext(NcaContext *out, u8 storage_id, NcmContentStorage *ncm_storage, u8 hfs_partition_type, const NcmPackagedContentInfo *content_info, Ticket *tik);

/// Reads raw encrypted data from a NCA using an input context, previously initialized by ncaInitializeContext().
bool ncaReadContentFile(NcaContext *ctx, void *out, u64 read_size, u64 offset);

/// Reads decrypted data from a NCA FS section using an input context.
/// Input offset must be relative to the start of the NCA FS section.
/// If dealing with Patch RomFS sections, this function should only be used when *not* reading BKTR subsections.
bool ncaReadFsSection(NcaFsSectionContext *ctx, void *out, u64 read_size, u64 offset);

/// Returns a pointer to a heap-allocated buffer used to encrypt the input plaintext data, based on the encryption type used by the input NCA FS section, as well as its offset and size.
/// Input offset must be relative to the start of the NCA FS section.
/// Output size and offset are guaranteed to be aligned to the AES sector size used by the encryption type from the FS section.
/// Output offset is relative to the start of the NCA content file, making it easier to use the output encrypted block to replace data in-place while writing a NCA.
void *ncaGenerateEncryptedFsSectionBlock(NcaFsSectionContext *ctx, const void *data, u64 data_size, u64 data_offset, u64 *out_block_size, u64 *out_block_offset);





bool ncaEncryptKeyArea(NcaContext *nca_ctx);
bool ncaEncryptHeader(NcaContext *ctx);





/// Miscellanous functions.

NX_INLINE void ncaConvertNcmContentSizeToU64(const u8 *size, u64 *out)
{
    if (!size || !out) return;
    *out = 0;
    memcpy(out, size, 6);
}

NX_INLINE void ncaConvertU64ToNcmContentSize(const u64 *size, u8 *out)
{
    if (size && out) memcpy(out, size, 6);
}

NX_INLINE void ncaSetDownloadDistributionType(NcaContext *ctx)
{
    if (!ctx || ctx->header.distribution_type == NcaDistributionType_Download) return;
    ctx->header.distribution_type = NcaDistributionType_Download;
    ctx->dirty_header = true;
}

NX_INLINE void ncaWipeRightsId(NcaContext *ctx)
{
    if (!ctx || !ctx->rights_id_available) return;
    memset(&(ctx->header.rights_id), 0, sizeof(FsRightsId));
    ctx->dirty_header = true;
}

NX_INLINE bool ncaValidateHierarchicalSha256Offsets(NcaHierarchicalSha256 *hierarchical_sha256, u64 section_size)
{
    if (!hierarchical_sha256 || !section_size || !hierarchical_sha256->hash_block_size || hierarchical_sha256->layer_count != NCA_HIERARCHICAL_SHA256_LAYER_COUNT) return false;
    
    /* Validate layer offsets and sizes */
    for(u8 i = 0; i < NCA_HIERARCHICAL_SHA256_LAYER_COUNT; i++)
    {
        NcaHierarchicalSha256LayerInfo *layer_info = (i == 0 ? &(hierarchical_sha256->hash_data_layer_info) : &(hierarchical_sha256->hash_target_layer_info));
        if (layer_info->offset >= section_size || !layer_info->size || (layer_info->offset + layer_info->size) > section_size) return false;
    }
    
    return true;
}

NX_INLINE bool ncaValidateHierarchicalIntegrityOffsets(NcaHierarchicalIntegrity *hierarchical_integrity, u64 section_size)
{
    if (!hierarchical_integrity || !section_size || __builtin_bswap32(hierarchical_integrity->magic) != NCA_IVFC_MAGIC || !hierarchical_integrity->master_hash_size || \
        hierarchical_integrity->layer_count != NCA_IVFC_LAYER_COUNT) return false;
    
    /* Validate layer offsets and sizes */
    for(u8 i = 0; i < (NCA_IVFC_HASH_DATA_LAYER_COUNT + 1); i++)
    {
        NcaHierarchicalIntegrityLayerInfo *layer_info = (i < NCA_IVFC_HASH_DATA_LAYER_COUNT ? &(hierarchical_integrity->hash_data_layer_info[i]) : &(hierarchical_integrity->hash_target_layer_info));
        if (layer_info->offset >= section_size || !layer_info->size || !layer_info->block_size || (layer_info->offset + layer_info->size) > section_size) return false;
    }
    
    return true;
}

#endif /* __NCA_H__ */
