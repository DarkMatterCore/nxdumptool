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

#define NCA_HEADER_LENGTH               0x400
#define NCA_FS_HEADER_LENGTH            0x200
#define NCA_FS_HEADER_COUNT             4
#define NCA_FULL_HEADER_LENGTH          (NCA_HEADER_LENGTH + (NCA_FS_HEADER_LENGTH * NCA_FS_HEADER_COUNT))

#define NCA_NCA0_MAGIC                  0x4E434130  /* "NCA0" */
#define NCA_NCA2_MAGIC                  0x4E434132  /* "NCA2" */
#define NCA_NCA3_MAGIC                  0x4E434133  /* "NCA3" */

#define NCA_IVFC_MAGIC                  0x49564643  /* "IVFC" */

#define NCA_BKTR_MAGIC                  0x424B5452  /* "BKTR" */

#define NCA_FS_ENTRY_BLOCK_SIZE         0x200

#define NCA_AES_XTS_SECTOR_SIZE         0x200

#define NCA_IVFC_BLOCK_SIZE(x)          (1 << (x))

typedef enum {
    NcaVersion_Nca0     = 0,
    NcaVersion_Nca2     = 1,
    NcaVersion_Nca3     = 2
} NcaVersion;

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

/// 'NcaKeyGeneration_Latest' will always point to the last known key generation value.
typedef enum {
    NcaKeyGeneration_301_302 = 3,
    NcaKeyGeneration_400_410 = 4,
    NcaKeyGeneration_500_510 = 5,
    NcaKeyGeneration_600_610 = 6,
    NcaKeyGeneration_620     = 7,
    NcaKeyGeneration_700_801 = 8,
    NcaKeyGeneration_810_811 = 9,
    NcaKeyGeneration_900_901 = 10,
    NcaKeyGeneration_910_920 = 11,
    NcaKeyGeneration_Latest  = NcaKeyGeneration_910_920
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
} NcaEncryptedKey;

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
} NcaHierarchicalSha256LayerInfo;

/// Used for NcaFsType_PartitionFs and NCA0 RomFS.
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
    u32 block_size;     ///< Use NCA_IVFC_CALC_BLOCK_SIZE to calculate the actual block size using this value.
    u8 reserved[0x4];
} NcaHierarchicalIntegrityLayerInfo;

/// Used for NcaFsType_RomFs.
typedef struct {
    u32 magic;                                                  ///< "IVFC".
    u32 version;
    u32 master_hash_size;
    u32 layer_count;
    NcaHierarchicalIntegrityLayerInfo hash_data_layer_info[5];
    NcaHierarchicalIntegrityLayerInfo hash_target_layer_info;
    u8 signature_salt[0x20];
    u8 master_hash[0x20];
} NcaHierarchicalIntegrity;

typedef struct {
    union {
        struct {
            ///< Used if hash_type == NcaHashType_HierarchicalSha256 (NcaFsType_PartitionFs).
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
    u8 main_signature[0x100];           ///< RSA-PSS signature over header with fixed key.
    u8 acid_signature[0x100];           ///< RSA-PSS signature over header with key in NPDM.
    u32 magic;                          ///< "NCA0" / "NCA2" / "NCA3".
    u8 distribution_type;               ///< NcaDistributionType.
    u8 content_type;                    ///< NcaContentType.
    u8 key_generation_old;              ///< NcaKeyGenerationOld.
    u8 kaek_index;                      ///< NcaKeyAreaEncryptionKeyIndex.
    u64 content_size;
    u64 program_id;
    u32 content_index;
    union {
        u32 sdk_addon_version;
        struct {
            u8 sdk_addon_revision;
            u8 sdk_addon_micro;
            u8 sdk_addon_minor;
            u8 sdk_addon_major;
        };
    };
    u8 key_generation;                  ///< NcaKeyGeneration.
    u8 main_signature_key_generation;
    u8 reserved_1[0xE];
    FsRightsId rights_id;               ///< Used for titlekey crypto.
    NcaFsEntry fs_entries[4];           ///< Start and end offsets for each NCA FS section.
    NcaFsHash fs_hashes[4];             ///< SHA-256 hashes calculated over each NCA FS section header.
    NcaEncryptedKey encrypted_keys[4];  ///< Only the encrypted key at index #2 is used. The other three are zero filled before the key area is encrypted.
    u8 reserved_2[0xC0];
    NcaFsHeader fs_headers[4];          /// NCA FS section headers.
} NcaHeader;










typedef struct {
    u64 offset;
    u64 size;
    u32 section_num;
    NcaFsHeader *fs_header;
    
    
    
    
    
    u8 ctr;
} NcaFsContext;

typedef struct {
    u8 storage_id;                      ///< NcmStorageId.
    NcmContentStorage *ncm_storage;     ///< Pointer to a NcmContentStorage instance. Used to read NCA data.
    u64 gamecard_offset;                ///< Used to read NCA data from a gamecard using a FsStorage instance when storage_id == NcmStorageId_GameCard.
    NcmContentId id;                    ///< Also used to read NCA data.
    char id_str[0x21];
    u8 hash[0x20];
    char hash_str[0x41];
    u8 format_version;                  ///< NcaVersion.
    u8 type;                            ///< NcmContentType. Retrieved from NcmContentInfo.
    u64 size;                           ///< Retrieved from NcmContentInfo.
    u8 key_generation;                  ///< NcaKeyGenerationOld / NcaKeyGeneration. Retrieved from the decrypted header.
    u8 id_offset;                       ///< Retrieved from NcmContentInfo.
    bool rights_id_available;
    NcaHeader header;
    bool dirty_header;
    NcaEncryptedKey decrypted_keys[4];
    NcaFsContext fs_contexts[4];
} NcaContext;

static inline void ncaConvertNcmContentSizeToU64(const u8 *size, u64 *out)
{
    if (!size || !out) return;
    *out = 0;
    memcpy(out, size, 6);
}

static inline void ncaConvertU64ToNcmContentSize(const u64 *size, u8 *out)
{
    if (!size || !out) return;
    memcpy(out, size, 6);
}



static inline u8 ncaGetKeyGenerationValue(NcaContext *ctx)
{
    if (!ctx) return 0;
    return (ctx->header.key_generation > ctx->header.key_generation_old ? ctx->header.key_generation : ctx->header.key_generation_old);
}

static inline void ncaSetDownloadDistributionType(NcaContext *ctx)
{
    if (!ctx || ctx->header.distribution_type == NcaDistributionType_Download) return;
    ctx->header.distribution_type = NcaDistributionType_Download;
    ctx->dirty_header = true;
}

static inline bool ncaCheckRightsIdAvailability(NcaContext *ctx)
{
    if (!ctx) return false;
    
    bool rights_id_available = false;
    
    for(u8 i = 0; i < 0x10; i++)
    {
        if (ctx->header.rights_id.c[i] != 0)
        {
            rights_id_available = true;
            break;
        }
    }
    
    return rights_id_available;
}

static inline void ncaWipeRightsId(NcaContext *ctx)
{
    if (!ctx) return;
    memset(&(ctx->header.rights_id), 0, sizeof(FsRightsId));
    ctx->dirty_header = true;
}




bool ncaDecryptKeyArea(NcaContext *nca_ctx);
bool ncaEncryptKeyArea(NcaContext *nca_ctx);

bool ncaDecryptHeader(NcaContext *ctx);
bool ncaEncryptHeader(NcaContext *ctx);







#endif /* __NCA_H__ */
