#pragma once

#ifndef __KEYS_H__
#define __KEYS_H__

#include <switch.h>
#include "nca.h"

#define FS_TID                          (u64)0x0100000000000000

#define SEG_TEXT                        BIT(0)
#define SEG_RODATA                      BIT(1)
#define SEG_DATA                        BIT(2)

#define ETICKET_DEVKEY_DATA_SIZE        0x244
#define ETICKET_DEVKEY_CTR_OFFSET       0x4
#define ETICKET_DEVKEY_RSA_OFFSET       0x14
#define ETICKET_DEVKEY_RSA_SIZE         (ETICKET_DEVKEY_DATA_SIZE - ETICKET_DEVKEY_RSA_OFFSET)

#define SIGTYPE_RSA2048_SHA1            (u32)0x10001
#define SIGTYPE_RSA2048_SHA256          (u32)0x10004

typedef struct {
    u64 titleID;
    u8 mask;
    u8 *data;
    u64 dataSize;
} PACKED keyLocation;

typedef struct {
    char name[128];
    u8 hash[SHA256_HASH_SIZE];
    u64 size;
} PACKED keyInfo;

typedef struct {
    u16 memory_key_cnt;                         /* Key counter for keys retrieved from memory. */
    u16 ext_key_cnt;                            /* Key counter for keys retrieved from keysfile. */
    u32 total_key_cnt;                          /* Total key counter. */
    
    // Needed to decrypt the NCA header using AES-128-XTS
    u8 header_kek_source[0x10];                 /* Seed for header kek. Retrieved from the .rodata section in the FS sysmodule. */
    u8 header_key_source[0x20];                 /* Seed for NCA header key. Retrieved from the .data section in the FS sysmodule. */
    u8 header_kek[0x10];                        /* NCA header kek. Generated from header_kek_source. */
    u8 header_key[0x20];                        /* NCA header key. Generated from header_kek and header_key_source. */
    
    // Needed to derive the KAEK used to decrypt the NCA key area
    u8 key_area_key_application_source[0x10];   /* Seed for kaek 0. Retrieved from the .rodata section in the FS sysmodule. */
    u8 key_area_key_ocean_source[0x10];         /* Seed for kaek 1. Retrieved from the .rodata section in the FS sysmodule. */
    u8 key_area_key_system_source[0x10];        /* Seed for kaek 2. Retrieved from the .rodata section in the FS sysmodule. */
    
    // Needed to decrypt the title key block from an eTicket. Retrieved from the Lockpick_RCM keys file.
    u8 eticket_rsa_kek[0x10];                   /* eTicket RSA kek. */
    u8 titlekeks[0x20][0x10];                   /* Title key encryption keys. */
    
    // Needed to reencrypt the NCA key area for tik-less NSP dumps. Retrieved from the Lockpick_RCM keys file.
    u8 key_area_keys[0x20][3][0x10];            /* Key area encryption keys. */
    
    // Console specific. Needed to calculate the AES CMAC for savefiles. Retrieved from the Lockpick_RCM keys file.
    u8 save_mac_key[0x10];                      /* Savefile CMAC key */
} PACKED nca_keyset_t;

bool loadMemoryKeys();
bool decryptNcaKeyArea(nca_header_t *dec_nca_header, u8 *out);
bool loadExternalKeys();
int retrieveNcaTikTitleKey(nca_header_t *dec_nca_header, u8 *out_tik, u8 *out_enc_key, u8 *out_dec_key);
bool generateEncryptedNcaKeyAreaWithTitlekey(nca_header_t *dec_nca_header, u8 *decrypted_nca_keys);

#endif
