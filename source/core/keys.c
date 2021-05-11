/*
 * keys.c
 *
 * Copyright (c) 2018-2020, SciresM.
 * Copyright (c) 2019, shchmue.
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

#include "nxdt_utils.h"
#include "keys.h"
#include "mem.h"
#include "nca.h"
#include "rsa.h"

#define KEYS_FILE_PATH                          "sdmc:/switch/prod.keys"    /* Location used by Lockpick_RCM. */

#define ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT  0x10001

/* Type definitions. */

typedef struct {
    char name[64];
    u8 hash[SHA256_HASH_SIZE];
    u64 size;
    void *dst;
} KeysMemoryKey;

typedef struct {
    MemoryLocation location;
    u32 key_count;
    KeysMemoryKey keys[];
} KeysMemoryInfo;

typedef struct {
    ///< AES-128-XTS key needed to handle NCA header crypto.
    u8 nca_header_kek_source[AES_128_KEY_SIZE];                                                     ///< Retrieved from the .rodata segment in the FS sysmodule.
    u8 nca_header_key_source[AES_128_KEY_SIZE * 2];                                                 ///< Retrieved from the .data segment in the FS sysmodule.
    u8 nca_header_kek_sealed[AES_128_KEY_SIZE];                                                     ///< Generated from nca_header_kek_source. Sealed by the SMC AES engine.
    u8 nca_header_key[AES_128_KEY_SIZE * 2];                                                        ///< Generated from nca_header_kek_sealed and nca_header_key_source.
    
    ///< AES-128-ECB keys needed to handle key area crypto from NCA headers.
    u8 nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_Count][AES_128_KEY_SIZE];                      ///< Retrieved from the .rodata segment in the FS sysmodule.
    u8 nca_kaek_sealed[NcaKeyAreaEncryptionKeyIndex_Count][NcaKeyGeneration_Max][AES_128_KEY_SIZE]; ///< Generated from nca_kaek_sources. Sealed by the SMC AES engine.
    u8 nca_kaek[NcaKeyAreaEncryptionKeyIndex_Count][NcaKeyGeneration_Max][AES_128_KEY_SIZE];        ///< Unsealed key area encryption keys. Retrieved from the Lockpick_RCM keys file.
    
    ///< AES-128-CTR key needed to decrypt the console-specific eTicket RSA device key stored in PRODINFO.
    u8 eticket_rsa_kek[AES_128_KEY_SIZE];                                                           ///< eTicket RSA key encryption key (generic). Retrieved from the Lockpick_RCM keys file.
    u8 eticket_rsa_kek_personalized[AES_128_KEY_SIZE];                                              ///< eTicket RSA key encryption key (console-specific). Retrieved from the Lockpick_RCM keys file.
    
    ///< AES-128-ECB keys needed to decrypt titlekeys.
    u8 ticket_common_keys[NcaKeyGeneration_Max][AES_128_KEY_SIZE];                                  ///< Retrieved from the Lockpick_RCM keys file.
} KeysNcaKeyset;

/// Used to parse the eTicket RSA device key retrieved from PRODINFO via setcalGetEticketDeviceKey().
/// Everything after the AES CTR is encrypted using the eTicket RSA device key encryption key.
typedef struct {
    u8 ctr[0x10];
    u8 exponent[0x100];
    u8 modulus[0x100];
    u32 public_exponent;    ///< Must match ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT. Stored using big endian byte order.
    u8 padding[0x14];
    u64 device_id;
    u8 ghash[0x10];
} EticketRsaDeviceKey;

NXDT_ASSERT(EticketRsaDeviceKey, 0x240);

/* Global variables. */

static KeysNcaKeyset g_ncaKeyset = {0};
static bool g_ncaKeysetLoaded = false;
static Mutex g_ncaKeysetMutex = 0;

static SetCalRsa2048DeviceKey g_eTicketRsaDeviceKey = {0};

/// Used during the RSA-OAEP titlekey decryption steps.
static const u8 g_nullHash[0x20] = {
    0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
    0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
};

static KeysMemoryInfo g_fsRodataMemoryInfo = {
    .location = {
        .program_id = FS_SYSMODULE_TID,
        .mask = MemoryProgramSegmentType_Rodata,
        .data = NULL,
        .data_size = 0
    },
    .key_count = 4,
    .keys = {
        {
            .name = "nca_header_kek_source",
            .hash = {
                0x18, 0x88, 0xCA, 0xED, 0x55, 0x51, 0xB3, 0xED, 0xE0, 0x14, 0x99, 0xE8, 0x7C, 0xE0, 0xD8, 0x68,
                0x27, 0xF8, 0x08, 0x20, 0xEF, 0xB2, 0x75, 0x92, 0x10, 0x55, 0xAA, 0x4E, 0x2A, 0xBD, 0xFF, 0xC2
            },
            .size = AES_128_KEY_SIZE,
            .dst = g_ncaKeyset.nca_header_kek_source
        },
        {
            .name = "nca_kaek_application_source",
            .hash = {
                0x04, 0xAD, 0x66, 0x14, 0x3C, 0x72, 0x6B, 0x2A, 0x13, 0x9F, 0xB6, 0xB2, 0x11, 0x28, 0xB4, 0x6F,
                0x56, 0xC5, 0x53, 0xB2, 0xB3, 0x88, 0x71, 0x10, 0x30, 0x42, 0x98, 0xD8, 0xD0, 0x09, 0x2D, 0x9E
            },
            .size = AES_128_KEY_SIZE,
            .dst = g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_Application]
        },
        {
            .name = "nca_kaek_ocean_source",
            .hash = {
                0xFD, 0x43, 0x40, 0x00, 0xC8, 0xFF, 0x2B, 0x26, 0xF8, 0xE9, 0xA9, 0xD2, 0xD2, 0xC1, 0x2F, 0x6B,
                0xE5, 0x77, 0x3C, 0xBB, 0x9D, 0xC8, 0x63, 0x00, 0xE1, 0xBD, 0x99, 0xF8, 0xEA, 0x33, 0xA4, 0x17
            },
            .size = AES_128_KEY_SIZE,
            .dst = g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_Ocean]
        },
        {
            .name = "nca_kaek_system_source",
            .hash = {
                0x1F, 0x17, 0xB1, 0xFD, 0x51, 0xAD, 0x1C, 0x23, 0x79, 0xB5, 0x8F, 0x15, 0x2C, 0xA4, 0x91, 0x2E,
                0xC2, 0x10, 0x64, 0x41, 0xE5, 0x17, 0x22, 0xF3, 0x87, 0x00, 0xD5, 0x93, 0x7A, 0x11, 0x62, 0xF7
            },
            .size = AES_128_KEY_SIZE,
            .dst = g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_System]
        }
    }
};

static KeysMemoryInfo g_fsDataMemoryInfo = {
    .location = {
        .program_id = FS_SYSMODULE_TID,
        .mask = MemoryProgramSegmentType_Data,
        .data = NULL,
        .data_size = 0
    },
    .key_count = 1,
    .keys = {
        {
            .name = "nca_header_key_source",
            .hash = {
                0x8F, 0x78, 0x3E, 0x46, 0x85, 0x2D, 0xF6, 0xBE, 0x0B, 0xA4, 0xE1, 0x92, 0x73, 0xC4, 0xAD, 0xBA,
                0xEE, 0x16, 0x38, 0x00, 0x43, 0xE1, 0xB8, 0xC4, 0x18, 0xC4, 0x08, 0x9A, 0x8B, 0xD6, 0x4A, 0xA6
            },
            .size = (AES_128_KEY_SIZE * 2),
            .dst = g_ncaKeyset.nca_header_key_source
        }
    }
};

/* Function prototypes. */

static bool keysRetrieveKeysFromProgramMemory(KeysMemoryInfo *info);

static bool keysDeriveNcaHeaderKey(void);
static bool keysDeriveSealedNcaKeyAreaEncryptionKeys(void);

static int keysGetKeyAndValueFromFile(FILE *f, char **key, char **value);
static char keysConvertHexCharToBinary(char c);
static bool keysParseHexKey(u8 *out, const char *key, const char *value, u32 size);
static bool keysReadKeysFromFile(void);

static bool keysGetDecryptedEticketRsaDeviceKey(void);
static bool keysTestEticketRsaDeviceKey(const void *e, const void *d, const void *n);

bool keysLoadNcaKeyset(void)
{
    mutexLock(&g_ncaKeysetMutex);
    
    bool ret = g_ncaKeysetLoaded;
    if (ret) goto end;
    
    /* Retrieve FS .rodata keys. */
    if (!keysRetrieveKeysFromProgramMemory(&g_fsRodataMemoryInfo))
    {
        LOG_MSG("Unable to retrieve keys from FS .rodata segment!");
        goto end;
    }
    
    /* Retrieve FS .data keys. */
    if (!keysRetrieveKeysFromProgramMemory(&g_fsDataMemoryInfo))
    {
        LOG_MSG("Unable to retrieve keys from FS .data segment!");
        goto end;
    }
    
    /* Derive NCA header key. */
    if (!keysDeriveNcaHeaderKey())
    {
        LOG_MSG("Unable to derive NCA header key!");
        goto end;
    }
    
    /* Derive sealed NCA KAEKs. */
    if (!keysDeriveSealedNcaKeyAreaEncryptionKeys())
    {
        LOG_MSG("Unable to derive sealed NCA KAEKs!");
        goto end;
    }
    
    /* Read additional keys from the keys file. */
    if (!keysReadKeysFromFile()) goto end;
    
    /* Get decrypted eTicket RSA device key. */
    if (!keysGetDecryptedEticketRsaDeviceKey()) goto end;
    
    ret = g_ncaKeysetLoaded = true;
    
end:
    /*if (ret)
    {
        LOG_DATA(&g_ncaKeyset, sizeof(KeysNcaKeyset), "NCA keyset dump:");
        LOG_DATA(&g_eTicketRsaDeviceKey, sizeof(SetCalRsa2048DeviceKey), "eTicket RSA device key dump:");
    }*/
    
    mutexUnlock(&g_ncaKeysetMutex);
    
    return ret;
}

const u8 *keysGetNcaHeaderKey(void)
{
    mutexLock(&g_ncaKeysetMutex);
    const u8 *ptr = (g_ncaKeysetLoaded ? (const u8*)(g_ncaKeyset.nca_header_key) : NULL);
    mutexUnlock(&g_ncaKeysetMutex);
    return ptr;
}

bool keysDecryptNcaKeyAreaEntry(u8 kaek_index, u8 key_generation, void *dst, const void *src)
{
    Result rc = 0;
    bool success = false;
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);
    
    if (kaek_index >= NcaKeyAreaEncryptionKeyIndex_Count)
    {
        LOG_MSG("Invalid KAEK index! (0x%02X).", kaek_index);
        goto end;
    }
    
    if (key_gen_val >= NcaKeyGeneration_Max)
    {
        LOG_MSG("Invalid key generation value! (0x%02X).", key_gen_val);
        goto end;
    }
    
    if (!dst || !src)
    {
        LOG_MSG("Invalid destination/source pointer.");
        goto end;
    }
    
    mutexLock(&g_ncaKeysetMutex);
    
    if (g_ncaKeysetLoaded)
    {
        rc = splCryptoGenerateAesKey(g_ncaKeyset.nca_kaek_sealed[kaek_index][key_gen_val], src, dst);
        if (!(success = R_SUCCEEDED(rc))) LOG_MSG("splCryptoGenerateAesKey failed! (0x%08X).", rc);
    }
    
    mutexUnlock(&g_ncaKeysetMutex);
    
end:
    return success;
}

const u8 *keysGetNcaKeyAreaEncryptionKey(u8 kaek_index, u8 key_generation)
{
    const u8 *ptr = NULL;
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);
    
    if (kaek_index >= NcaKeyAreaEncryptionKeyIndex_Count)
    {
        LOG_MSG("Invalid KAEK index! (0x%02X).", kaek_index);
        goto end;
    }
    
    if (key_gen_val >= NcaKeyGeneration_Max)
    {
        LOG_MSG("Invalid key generation value! (0x%02X).", key_gen_val);
        goto end;
    }
    
    mutexLock(&g_ncaKeysetMutex);
    
    if (g_ncaKeysetLoaded) ptr = (const u8*)(g_ncaKeyset.nca_kaek[kaek_index][key_gen_val]);
    
    mutexUnlock(&g_ncaKeysetMutex);
    
end:
    return ptr;
}

bool keysDecryptRsaOaepWrappedTitleKey(const void *rsa_wrapped_titlekey, void *out_titlekey)
{
    if (!rsa_wrapped_titlekey || !out_titlekey)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    size_t out_keydata_size = 0;
    u8 out_keydata[0x100] = {0};
    EticketRsaDeviceKey *eticket_rsa_key = NULL;
    bool success = false;
    
    mutexLock(&g_ncaKeysetMutex);
    
    if (g_ncaKeysetLoaded)
    {
        /* Get eTicket RSA device key. */
        eticket_rsa_key = (EticketRsaDeviceKey*)g_eTicketRsaDeviceKey.key;
        
        /* Perform a RSA-OAEP unwrap operation to get the encrypted titlekey. */
        success = (rsa2048OaepDecryptAndVerify(out_keydata, sizeof(out_keydata), rsa_wrapped_titlekey, eticket_rsa_key->modulus, eticket_rsa_key->exponent, sizeof(eticket_rsa_key->exponent), \
                   g_nullHash, &out_keydata_size) && out_keydata_size >= AES_128_KEY_SIZE);
        if (success)
        {
            /* Copy RSA-OAEP unwrapped titlekey. */
            memcpy(out_titlekey, out_keydata, AES_128_KEY_SIZE);
        } else {
            LOG_MSG("RSA-OAEP titlekey decryption failed!");
        }
    }
    
    mutexUnlock(&g_ncaKeysetMutex);
    
    return success;
}

const u8 *keysGetTicketCommonKey(u8 key_generation)
{
    const u8 *ptr = NULL;
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);
    
    if (key_gen_val >= NcaKeyGeneration_Max)
    {
        LOG_MSG("Invalid key generation value! (0x%02X).", key_gen_val);
        goto end;
    }
    
    mutexLock(&g_ncaKeysetMutex);
    
    if (g_ncaKeysetLoaded) ptr = (const u8*)(g_ncaKeyset.ticket_common_keys[key_gen_val]);
    
    mutexUnlock(&g_ncaKeysetMutex);
    
end:
    return ptr;
}

static bool keysRetrieveKeysFromProgramMemory(KeysMemoryInfo *info)
{
    if (!info || !info->key_count)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    bool found;
    u8 tmp_hash[SHA256_HASH_SIZE];
    bool success = false;
    
    if (!memRetrieveProgramMemorySegment(&(info->location))) return false;
    
    for(u32 i = 0; i < info->key_count; i++)
    {
        found = false;
        
        KeysMemoryKey *key = &(info->keys[i]);
        
        if (!key->dst)
        {
            LOG_MSG("Invalid destination pointer for key \"%s\" in program %016lX!", key->name, info->location.program_id);
            goto end;
        }
        
        /* Hash every key length-sized byte chunk in the process memory buffer until a match is found. */
        for(u64 j = 0; j < info->location.data_size; j++)
        {
            if ((info->location.data_size - j) < key->size) break;
            
            sha256CalculateHash(tmp_hash, info->location.data + j, key->size);
            
            if (!memcmp(tmp_hash, key->hash, SHA256_HASH_SIZE))
            {
                /* Jackpot. */
                memcpy(key->dst, info->location.data + j, key->size);
                found = true;
                break;
            }
        }
        
        if (!found)
        {
            LOG_MSG("Unable to locate key \"%s\" in process memory from program %016lX!", key->name, info->location.program_id);
            goto end;
        }
    }
    
    success = true;
    
end:
    memFreeMemoryLocation(&(info->location));
    
    return success;
}

static bool keysDeriveNcaHeaderKey(void)
{
    Result rc = 0;
    
    /* Derive nca_header_kek_sealed from nca_header_kek_source. */
    rc = splCryptoGenerateAesKek(g_ncaKeyset.nca_header_kek_source, 0, 0, g_ncaKeyset.nca_header_kek_sealed);
    if (R_FAILED(rc))
    {
        LOG_MSG("splCryptoGenerateAesKek failed! (0x%08X) (nca_header_kek_sealed).", rc);
        return false;
    }
    
    /* Derive nca_header_key from nca_header_kek_sealed and nca_header_key_source. */
    rc = splCryptoGenerateAesKey(g_ncaKeyset.nca_header_kek_sealed, g_ncaKeyset.nca_header_key_source, g_ncaKeyset.nca_header_key);
    if (R_FAILED(rc))
    {
        LOG_MSG("splCryptoGenerateAesKey failed! (0x%08X) (nca_header_key, part 1).", rc);
        return false;
    }
    
    rc = splCryptoGenerateAesKey(g_ncaKeyset.nca_header_kek_sealed, g_ncaKeyset.nca_header_key_source + AES_128_KEY_SIZE, g_ncaKeyset.nca_header_key + AES_128_KEY_SIZE);
    if (R_FAILED(rc))
    {
        LOG_MSG("splCryptoGenerateAesKey failed! (0x%08X) (nca_header_key, part 2).", rc);
        return false;
    }
    
    return true;
}

static bool keysDeriveSealedNcaKeyAreaEncryptionKeys(void)
{
    Result rc = 0;
    u32 key_cnt = 0;
    u8 highest_key_gen = 0;
    bool success = false;
    
    for(u8 i = 0; i < NcaKeyAreaEncryptionKeyIndex_Count; i++)
    {
        /* Get pointer to current KAEK source. */
        const u8 *nca_kaek_source = (const u8*)(g_ncaKeyset.nca_kaek_sources[i]);
        
        for(u8 j = 1; j <= NcaKeyGeneration_Max; j++)
        {
            /* Get pointer to current sealed KAEK. */
            u8 key_gen_val = (j - 1);
            u8 *nca_kaek_sealed = g_ncaKeyset.nca_kaek_sealed[i][key_gen_val];
            
            /* Derive sealed KAEK using the current KAEK source and key generation. */
            rc = splCryptoGenerateAesKek(nca_kaek_source, j, 0, nca_kaek_sealed);
            if (R_FAILED(rc))
            {
                //LOG_MSG("splCryptoGenerateAesKek failed for KAEK index %u and key generation %u! (0x%08X).", i, (j <= 1 ? 0 : j), rc);
                break;
            }
            
            /* Update derived key count and highest key generation value. */
            key_cnt++;
            if (key_gen_val > highest_key_gen) highest_key_gen = key_gen_val;
        }
    }
    
    success = (key_cnt > 0);
    if (success) LOG_MSG("Derived %u sealed NCA KAEK(s) (%u key generation[s]).", key_cnt, highest_key_gen + 1);
    
    return success;
}

/**
 * Reads a line from file f and parses out the key and value from it.
 * The format of a line must match /^ *[A-Za-z0-9_] *[,=] *.+$/.
 * If a line ends in \r, the final \r is stripped.
 * The input file is assumed to have been opened with the 'b' flag.
 * The input file is assumed to contain only ASCII.
 *
 * A line cannot exceed 512 bytes in length.
 * Lines that are excessively long will be silently truncated.
 *
 * On success, *key and *value will be set to point to the key and value in
 * the input line, respectively.
 * *key and *value may also be NULL in case of empty lines.
 * On failure, *key and *value will be set to NULL.
 * End of file is considered failure.
 *
 * Because *key and *value will point to a static buffer, their contents must be
 * copied before calling this function again.
 * For the same reason, this function is not thread-safe.
 *
 * The key will be converted to lowercase.
 * An empty key is considered a parse error, but an empty value is returned as
 * success.
 *
 * This function assumes that the file can be trusted not to contain any NUL in
 * the contents.
 *
 * Whitespace (' ', ASCII 0x20, as well as '\t', ASCII 0x09) at the beginning of
 * the line, at the end of the line as well as around = (or ,) will be ignored.
 *
 * @param f the file to read
 * @param key pointer to change to point to the key
 * @param value pointer to change to point to the value
 * @return 0 on success,
 *         1 on end of file,
 *         -1 on parse error (line too long, line malformed)
 *         -2 on I/O error
 */
static int keysGetKeyAndValueFromFile(FILE *f, char **key, char **value)
{
    if (!f || !key || !value)
    {
        LOG_MSG("Invalid parameters!");
        return -2;
    }
    
#define SKIP_SPACE(p) do {\
    for (; (*p == ' ' || *p == '\t'); ++p);\
} while(0);
    
    static char line[512] = {0};
    char *k, *v, *p, *end;
    
    *key = *value = NULL;
    
    errno = 0;
    
    if (fgets(line, (int)sizeof(line), f) == NULL)
    {
        if (feof(f))
        {
            return 1;
        } else {
            return -2;
        }
    }
    
    if (errno != 0) return -2;
    
    if (*line == '\n' || *line == '\r' || *line == '\0') return 0;
    
    /* Not finding \r or \n is not a problem.
     * The line might just be exactly 512 characters long, we have no way to
     * tell.
     * Additionally, it's possible that the last line of a file is not actually
     * a line (i.e., does not end in '\n'); we do want to handle those.
     */
    if ((p = strchr(line, '\r')) != NULL || (p = strchr(line, '\n')) != NULL)
    {
        end = p;
        *p = '\0';
    } else {
        end = (line + strlen(line) + 1);
    }
    
    p = line;
    SKIP_SPACE(p);
    k = p;
    
    /* Validate key and convert to lower case. */
    for (; *p != ' ' && *p != ',' && *p != '\t' && *p != '='; ++p)
    {
        if (*p == '\0') return -1;
        
        if (*p >= 'A' && *p <= 'Z')
        {
            *p = 'a' + (*p - 'A');
            continue;
        }
        
        if (*p != '_' && (*p < '0' && *p > '9') && (*p < 'a' && *p > 'z')) return -1;
    }
    
    /* Bail if the final ++p put us at the end of string. */
    if (*p == '\0') return -1;
    
    /* We should be at the end of key now and either whitespace or [,=] follows. */
    if (*p == '=' || *p == ',')
    {
        *p++ = '\0';
    } else {
        *p++ = '\0';
        SKIP_SPACE(p);
        if (*p != '=' && *p != ',') return -1;
        *p++ = '\0';
    }
    
    /* Empty key is an error. */
    if (*k == '\0') return -1;
    
    SKIP_SPACE(p);
    v = p;
    
    /* Skip trailing whitespace. */
    for (p = end - 1; *p == '\t' || *p == ' '; --p);
    
    *(p + 1) = '\0';
    
    *key = k;
    *value = v;
    
    return 0;
    
#undef SKIP_SPACE
}

static char keysConvertHexCharToBinary(char c)
{
    if ('a' <= c && c <= 'f') return (c - 'a' + 0xA);
    if ('A' <= c && c <= 'F') return (c - 'A' + 0xA);
    if ('0' <= c && c <= '9') return (c - '0');
    return 'z';
}

static bool keysParseHexKey(u8 *out, const char *key, const char *value, u32 size)
{
    u32 hex_str_len = (2 * size);
    size_t value_len = 0;
    
    if (!out || !key || !*key || !value || !(value_len = strlen(value)) || !size)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    if (value_len != hex_str_len)
    {
        LOG_MSG("Key \"%s\" must be %u hex digits long!", key, hex_str_len);
        return false;
    }
    
    memset(out, 0, size);
    
    for(u32 i = 0; i < hex_str_len; i++)
    {
        char val = keysConvertHexCharToBinary(value[i]);
        if (val == 'z')
        {
            LOG_MSG("Invalid hex character in key \"%s\" at position %u!", key, i);
            return false;
        }
        
        if ((i & 1) == 0) val <<= 4;
        out[i >> 1] |= val;
    }
    
    return true;
}

static bool keysReadKeysFromFile(void)
{
    int ret = 0;
    u32 key_count = 0;
    FILE *keys_file = NULL;
    char *key = NULL, *value = NULL;
    char test_name[0x40] = {0};
    bool parse_fail = false, eticket_rsa_kek_available = false;
    
    keys_file = fopen(KEYS_FILE_PATH, "rb");
    if (!keys_file)
    {
        LOG_MSG("Unable to open \"%s\" to retrieve keys!", KEYS_FILE_PATH);
        return false;
    }
    
    while(true)
    {
        ret = keysGetKeyAndValueFromFile(keys_file, &key, &value);
        if (ret == 1 || ret == -2) break;   /* Break from the while loop if EOF is reached or if an I/O error occurs. */
        
        /* Ignore malformed lines. */
        if (ret != 0 || !key || !value) continue;
        
        if (!strcasecmp(key, "eticket_rsa_kek"))
        {
            if ((parse_fail = !keysParseHexKey(g_ncaKeyset.eticket_rsa_kek, key, value, sizeof(g_ncaKeyset.eticket_rsa_kek)))) break;
            eticket_rsa_kek_available = true;
            key_count++;
        } else
        if (!strcasecmp(key, "eticket_rsa_kek_personalized"))
        {
            /* This only appears on consoles that use the new PRODINFO key generation scheme. */
            if ((parse_fail = !keysParseHexKey(g_ncaKeyset.eticket_rsa_kek_personalized, key, value, sizeof(g_ncaKeyset.eticket_rsa_kek_personalized)))) break;
            eticket_rsa_kek_available = true;
            key_count++;
        } else {
            for(u32 i = 0; i < NcaKeyGeneration_Max; i++)
            {
                snprintf(test_name, sizeof(test_name), "titlekek_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if ((parse_fail = !keysParseHexKey(g_ncaKeyset.ticket_common_keys[i], key, value, sizeof(g_ncaKeyset.ticket_common_keys[i])))) break;
                    key_count++;
                    break;
                }
                
                snprintf(test_name, sizeof(test_name), "key_area_key_application_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if ((parse_fail = !keysParseHexKey(g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_Application][i], key, value, \
                        sizeof(g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_Application][i])))) break;
                    key_count++;
                    break;
                }
                
                snprintf(test_name, sizeof(test_name), "key_area_key_ocean_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if ((parse_fail = !keysParseHexKey(g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_Ocean][i], key, value, \
                        sizeof(g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_Ocean][i])))) break;
                    key_count++;
                    break;
                }
                
                snprintf(test_name, sizeof(test_name), "key_area_key_system_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if ((parse_fail = !keysParseHexKey(g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_System][i], key, value, \
                        sizeof(g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_System][i])))) break;
                    key_count++;
                    break;
                }
            }
            
            if (parse_fail) break;
        }
    }
    
    fclose(keys_file);
    
    if (parse_fail || !key_count)
    {
        if (!key_count) LOG_MSG("Unable to parse necessary keys from \"%s\"! (keys file empty?).", KEYS_FILE_PATH);
        return false;
    }
    
    if (!eticket_rsa_kek_available)
    {
        LOG_MSG("\"eticket_rsa_kek\" unavailable in \"%s\"!", KEYS_FILE_PATH);
        return false;
    }
    
    return true;
}

static bool keysGetDecryptedEticketRsaDeviceKey(void)
{
    Result rc = 0;
    u32 public_exponent = 0;
    const u8 *eticket_rsa_kek = NULL;
    EticketRsaDeviceKey *eticket_rsa_key = NULL;
    Aes128CtrContext eticket_aes_ctx = {0};
    
    /* Get eTicket RSA device key. */
    rc = setcalGetEticketDeviceKey(&g_eTicketRsaDeviceKey);
    if (R_FAILED(rc))
    {
        LOG_MSG("setcalGetEticketDeviceKey failed! (0x%08X).", rc);
        return false;
    }
    
    /* Get eTicket RSA device key encryption key. */
    eticket_rsa_kek = (const u8*)(g_eTicketRsaDeviceKey.generation > 0 ? g_ncaKeyset.eticket_rsa_kek_personalized : g_ncaKeyset.eticket_rsa_kek);
    
    /* Decrypt eTicket RSA device key. */
    eticket_rsa_key = (EticketRsaDeviceKey*)g_eTicketRsaDeviceKey.key;
    aes128CtrContextCreate(&eticket_aes_ctx, eticket_rsa_kek, eticket_rsa_key->ctr);
    aes128CtrCrypt(&eticket_aes_ctx, &(eticket_rsa_key->exponent), &(eticket_rsa_key->exponent), sizeof(EticketRsaDeviceKey) - sizeof(eticket_rsa_key->ctr));
    
    /* Public exponent value must be 0x10001. */
    /* It is stored using big endian byte order. */
    public_exponent = __builtin_bswap32(eticket_rsa_key->public_exponent);
    if (public_exponent != ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT)
    {
        LOG_MSG("Invalid public exponent for decrypted eTicket RSA device key! Wrong keys? (0x%08X).", public_exponent);
        return false;
    }
    
    /* Test RSA key pair. */
    if (!keysTestEticketRsaDeviceKey(&(eticket_rsa_key->public_exponent), eticket_rsa_key->exponent, eticket_rsa_key->modulus))
    {
        LOG_MSG("eTicket RSA device key test failed! Wrong keys?");
        return false;
    }
    
    return true;
}

static bool keysTestEticketRsaDeviceKey(const void *e, const void *d, const void *n)
{
    if (!e || !d || !n)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u8 x[0x100] = {0}, y[0x100] = {0}, z[0x100] = {0};
    
    /* 0xCAFEBABE. */
    x[0xFC] = 0xCA;
    x[0xFD] = 0xFE;
    x[0xFE] = 0xBA;
    x[0xFF] = 0xBE;
    
    rc = splUserExpMod(x, n, d, 0x100, y);
    if (R_FAILED(rc))
    {
        LOG_MSG("splUserExpMod failed! (#1) (0x%08X).", rc);
        return false;
    }
    
    rc = splUserExpMod(y, n, e, 4, z);
    if (R_FAILED(rc))
    {
        LOG_MSG("splUserExpMod failed! (#2) (0x%08X).", rc);
        return false;
    }
    
    if (memcmp(x, z, 0x100) != 0)
    {
        LOG_MSG("Invalid RSA key pair!");
        return false;
    }
    
    return true;
}
