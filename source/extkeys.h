#pragma once

#ifndef __EXTKEYS_H__
#define __EXTKEYS_H__

#include <switch.h>
#include <string.h>

typedef struct {
    u32 key_cnt;
    unsigned char header_key[0x20];                      /* NCA header key. */
    unsigned char key_area_keys[0x20][3][0x10];          /* Key area encryption keys. */
} nca_keyset_t;

int parse_hex_key(unsigned char *key, const char *hex, unsigned int len);
int extkeys_initialize_keyset(nca_keyset_t *keyset, FILE *f);

#endif