#pragma once

#ifndef __UTIL_H__
#define __UTIL_H__

#include <switch.h>

#define APP_VERSION                     "1.0.8"

#define KiB                             (1024.0)
#define MiB                             (1024.0 * KiB)
#define GiB                             (1024.0 * MiB)

#define NAME_BUF_LEN                    4096

#define SOCK_BUFFERSIZE                 65536

#define META_DB_REGULAR_APPLICATION     0x80

#define FILENAME_BUFFER_SIZE            (1024 * 512)        // 512 KiB
#define FILENAME_MAX_CNT                2048

#define NACP_APPNAME_LEN                0x200
#define NACP_AUTHOR_LEN                 0x100
#define VERSION_STR_LEN                 0x40

#define GAMECARD_WAIT_TIME              3                   // 3 seconds

#define GAMECARD_HEADER_SIZE            0x200
#define GAMECARD_SIZE_ADDR              0x10D
#define GAMECARD_DATAEND_ADDR           0x118

#define HFS0_OFFSET_ADDR                0x130
#define HFS0_SIZE_ADDR                  0x138
#define HFS0_MAGIC                      0x48465330          // "HFS0"
#define HFS0_FILE_COUNT_ADDR            0x04
#define HFS0_STR_TABLE_SIZE_ADDR        0x08
#define HFS0_ENTRY_TABLE_ADDR           0x10

#define PFS0_MAGIC                      0x50465330          // "PFS0"

#define MEDIA_UNIT_SIZE                 0x200

#define NCA3_MAGIC                      0x4E434133          // "NCA3"
#define NCA2_MAGIC                      0x4E434132          // "NCA2"

#define NCA_HEADER_LENGTH               0x400
#define NCA_SECTION_HEADER_LENGTH       0x200
#define NCA_SECTION_HEADER_CNT          4
#define NCA_FULL_HEADER_LENGTH          (NCA_HEADER_LENGTH + (NCA_SECTION_HEADER_LENGTH * NCA_SECTION_HEADER_CNT))

#define NCA_AES_XTS_SECTOR_SIZE         0x200

#define NCA_KEY_AREA_KEY_CNT            4
#define NCA_KEA_AREA_KEY_SIZE           0x10
#define NCA_KEY_AREA_SIZE               (NCA_KEY_AREA_KEY_CNT * NCA_KEA_AREA_KEY_SIZE)

#define NCA_FS_HEADER_PARTITION_PFS0    0x01
#define NCA_FS_HEADER_FSTYPE_PFS0       0x02
#define NCA_FS_HEADER_CRYPT_NONE        0x01
#define NCA_FS_HEADER_CRYPT_XTS         0x02
#define NCA_FS_HEADER_CRYPT_CTR         0x03
#define NCA_FS_HEADER_CRYPT_BKTR        0x04

#define NCA_CNMT_DIGEST_SIZE            0x20

#define GAMECARD_TYPE1_PARTITION_CNT    3                   // "update" (0), "normal" (1), "update" (2)
#define GAMECARD_TYPE2_PARTITION_CNT    4                   // "update" (0), "logo" (1), "normal" (2), "update" (3)
#define GAMECARD_TYPE(x)                ((x) == GAMECARD_TYPE1_PARTITION_CNT ? "Type 0x01" : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? "Type 0x02" : "Unknown"))
#define GAMECARD_TYPE1_PART_NAMES(x)    ((x) == 0 ? "Update" : ((x) == 1 ? "Normal" : ((x) == 2 ? "Secure" : "Unknown")))
#define GAMECARD_TYPE2_PART_NAMES(x)    ((x) == 0 ? "Update" : ((x) == 1 ? "Logo" : ((x) == 2 ? "Normal" : ((x) == 3 ? "Secure" : "Unknown"))))
#define GAMECARD_PARTITION_NAME(x, y)   ((x) == GAMECARD_TYPE1_PARTITION_CNT ? GAMECARD_TYPE1_PART_NAMES(y) : ((x) == GAMECARD_TYPE2_PARTITION_CNT ? GAMECARD_TYPE2_PART_NAMES(y) : "Unknown"))

#define HFS0_TO_ISTORAGE_IDX(x, y)      ((x) == GAMECARD_TYPE1_PARTITION_CNT ? ((y) < 2 ? 0 : 1) : ((y) < 3 ? 0 : 1))

#define GAMECARD_SIZE_1GiB              (u64)0x40000000
#define GAMECARD_SIZE_2GiB              (u64)0x80000000
#define GAMECARD_SIZE_4GiB              (u64)0x100000000
#define GAMECARD_SIZE_8GiB              (u64)0x200000000
#define GAMECARD_SIZE_16GiB             (u64)0x400000000
#define GAMECARD_SIZE_32GiB             (u64)0x800000000

/* Reference: https://switchbrew.org/wiki/Title_list */
#define GAMECARD_UPDATE_TITLEID         (u64)0x0100000000000816

#define SYSUPDATE_100                   (u32)450
#define SYSUPDATE_200                   (u32)65796
#define SYSUPDATE_210                   (u32)131162
#define SYSUPDATE_220                   (u32)196628
#define SYSUPDATE_230                   (u32)262164
#define SYSUPDATE_300                   (u32)201327002
#define SYSUPDATE_301                   (u32)201392178
#define SYSUPDATE_302                   (u32)201457684
#define SYSUPDATE_400                   (u32)268435656
#define SYSUPDATE_401                   (u32)268501002
#define SYSUPDATE_410                   (u32)269484082
#define SYSUPDATE_500                   (u32)335544750
#define SYSUPDATE_501                   (u32)335609886
#define SYSUPDATE_502                   (u32)335675432
#define SYSUPDATE_510                   (u32)336592976
#define SYSUPDATE_600                   (u32)402653544
#define SYSUPDATE_601                   (u32)402718730
#define SYSUPDATE_610                   (u32)403701850
#define SYSUPDATE_620                   (u32)404750376
#define SYSUPDATE_700                   (u32)469762248
#define SYSUPDATE_701                   (u32)469827614
#define SYSUPDATE_800                   (u32)536871442

#define bswap_32(a)                     ((((a) << 24) & 0xff000000) | (((a) << 8) & 0xff0000) | (((a) >> 8) & 0xff00) | (((a) >> 24) & 0xff))
#define round_up(x, y)                  ((x) + (((y) - ((x) % (y))) % (y)))			// Aligns 'x' bytes to a 'y' bytes boundary

typedef struct
{
    u64 file_offset;
    u64 file_size;
    u32 filename_offset;
    u32 hashed_region_size;
    u64 reserved;
    u8 hashed_region_sha256[0x20];
} PACKED hfs0_entry_table;

typedef struct
{
    u32 magic;
    u32 file_cnt;
    u32 str_table_size;
    u32 reserved;
} PACKED pfs0_header;

typedef struct
{
    u64 file_offset;
    u64 file_size;
    u32 filename_offset;
    u32 reserved;
} PACKED pfs0_entry_table;

typedef struct {
    u32 media_start_offset;
    u32 media_end_offset;
    u8 _0x8[0x8]; /* Padding. */
} PACKED nca_section_entry_t;

typedef struct {
    u8 master_hash[0x20]; /* SHA-256 hash of the hash table. */
    u32 block_size; /* In bytes. */
    u32 always_2;
    u64 hash_table_offset; /* Normally zero. */
    u64 hash_table_size; 
    u64 pfs0_offset;
    u64 pfs0_size;
    u8 _0x48[0xF0];
} PACKED pfs0_superblock_t;

/* NCA FS header. */
typedef struct {
    u8 _0x0;
    u8 _0x1;
    u8 partition_type;
    u8 fs_type;
    u8 crypt_type;
    u8 _0x5[0x3];
    pfs0_superblock_t pfs0_superblock; /* FS-specific superblock. Size = 0x138. */
    union {
        u8 section_ctr[0x8];
        struct {
            u32 section_ctr_low;
            u32 section_ctr_high;
        };
    };
    u8 _0x148[0xB8]; /* Padding. */
} PACKED nca_fs_header_t;

/* Nintendo content archive header. */
typedef struct {
    u8 fixed_key_sig[0x100]; /* RSA-PSS signature over header with fixed key. */
    u8 npdm_key_sig[0x100]; /* RSA-PSS signature over header with key in NPDM. */
    u32 magic;
    u8 distribution; /* System vs gamecard. */
    u8 content_type;
    u8 crypto_type; /* Which keyblob (field 1) */
    u8 kaek_ind; /* Which kaek index? */
    u64 nca_size; /* Entire archive size. */
    u64 title_id;
    u8 _0x218[0x4]; /* Padding. */
    union {
        u32 sdk_version; /* What SDK was this built with? */
        struct {
            u8 sdk_revision;
            u8 sdk_micro;
            u8 sdk_minor;
            u8 sdk_major;
        };
    };
    u8 crypto_type2; /* Which keyblob (field 2) */
    u8 _0x221[0xF]; /* Padding. */
    u8 rights_id[0x10]; /* Rights ID (for titlekey crypto). */
    nca_section_entry_t section_entries[4]; /* Section entry metadata. */
    u8 section_hashes[4][0x20]; /* SHA-256 hashes for each section header. */
    u8 nca_keys[4][0x10]; /* Key area (encrypted, but later decrypted by decryptNcaHeader()) */
    u8 _0x340[0xC0]; /* Padding. */
    nca_fs_header_t fs_headers[4]; /* FS section headers. */
} PACKED nca_header_t;

typedef struct {
    u64 title_id;
    u32 version;
    u8 type;
    u8 unk1;
    u16 table_offset;
    u16 content_records_cnt;
    u16 meta_records_cnt;
    u8 unk2[12];
} PACKED cnmt_header;

typedef struct {
    u64 patch_tid;
    u64 min_sysver;
} PACKED cnmt_application_header;

typedef struct {
    u8 hash[0x20];
    u8 nca_id[0x10];
    u8 size[6];
    u8 type;
    u8 unk;
} PACKED cnmt_content_record;

typedef struct {
    u8 type;
    u64 title_id;
    u32 version;
    u32 required_dl_sysver;
    u32 nca_cnt;
    u8 digest[32];
    char digest_str[65];
    u8 min_keyblob;
    u32 min_sysver;
    u64 patch_tid;
} PACKED cnmt_xml_program_info;

typedef struct {
    u8 type;
    u8 nca_id[16];
    char nca_id_str[33];
    u64 size;
    u8 hash[32];
    char hash_str[65];
    u8 keyblob;
    u8 encrypted_header_mod[NCA_FULL_HEADER_LENGTH];
} PACKED cnmt_xml_content_info;

bool isGameCardInserted();

void fsGameCardDetectionThreadFunc(void *arg);

void delay(u8 seconds);

void convertTitleVersionToDecimal(u32 version, char *versionBuf, int versionBufSize);

void removeIllegalCharacters(char *name);

void strtrim(char *str);

void freeStringsPtr(char **var);

void freeGameCardInfo();

void loadGameCardInfo();

bool getHfs0EntryDetails(char *hfs0Header, u64 hfs0HeaderOffset, u64 hfs0HeaderSize, u32 num_entries, u32 entry_idx, bool isRoot, u32 partitionIndex, u64 *out_offset, u64 *out_size);

bool getPartitionHfs0Header(u32 partition);

bool getHfs0FileList(u32 partition);

int getSdCardFreeSpace(u64 *out);

void convertSize(u64 size, char *out, int bufsize);

char *generateDumpName();

void waitForButtonPress();

void convertDataToHexString(const u8 *data, const u32 dataSize, char *outBuf, const u32 outBufSize);

void convertNcaSizeToU64(const u8 size[0x6], u64 *out);

bool encryptNcaHeader(nca_header_t *input, u8 *outBuf, u64 outBufSize);

bool decryptNcaHeader(const char *ncaBuf, u64 ncaBufSize, nca_header_t *out);

bool decryptCnmtNca(char *ncaBuf, u64 ncaBufSize);

bool calculateSHA256(const u8 *data, const u32 dataSize, u8 out[32]);

void generateCnmtMetadataXml(cnmt_xml_program_info *xml_program_info, cnmt_xml_content_info *xml_content_info, char *out);

void addStringToFilenameBuffer(const char *string, char **nextFilename);

void removeDirectory(const char *path);

void gameCardDumpNSWDBCheck(u32 crc);

void updateNSWDBXml();

void updateApplication();

#endif
