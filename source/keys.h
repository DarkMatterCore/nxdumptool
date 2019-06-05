#pragma once

#ifndef __KEYS_H__
#define __KEYS_H__

#include <switch.h>

#include "nca.h"

#define FS_TID                  (u64)0x0100000000000000

#define SEG_TEXT                BIT(0)
#define SEG_RODATA              BIT(1)
#define SEG_DATA                BIT(2)

#define SHA256_HASH_LENGTH      0x20

typedef struct {
    u64 titleID;
    u8 mask;
    u8 *data;
    u64 dataSize;
} PACKED keyLocation;

typedef struct {
    char name[128];
    u8 hash[SHA256_HASH_LENGTH];
    u64 size;
} PACKED keyInfo;

typedef struct {
    u32 key_cnt;                                /* Should be equal to 6. */
    u8 key_area_key_application_source[0x10];   /* Seed for kaek 0. */
    u8 key_area_key_ocean_source[0x10];         /* Seed for kaek 1. */
    u8 key_area_key_system_source[0x10];        /* Seed for kaek 2. */
    u8 header_kek_source[0x10];                 /* Seed for header kek. */
    u8 header_key_source[0x20];                 /* Seed for NCA header key. */
    u8 header_key[0x20];                        /* NCA header key. */
} PACKED nca_keyset_t;

bool getNcaKeys();
bool decryptNcaKeyArea(nca_header_t *dec_nca_header, u8 *out);

#endif
