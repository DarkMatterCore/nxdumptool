/*
 * keys.c
 *
 * Copyright (c) 2018-2020, SciresM.
 * Copyright (c) 2019, shchmue.
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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

#define ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT  0x10001

/* Type definitions. */

typedef bool (*KeysIsKeyMandatoryFunction)(void);   /* Used to determine if a key is mandatory or not at runtime. */

typedef struct {
    char name[64];
    u8 hash[SHA256_HASH_SIZE];
    u64 size;
    void *dst;
    KeysIsKeyMandatoryFunction mandatory_func;  ///< If NULL, key is mandatory.
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
    
    ///< RSA-2048-PSS moduli used to verify the main signature from NCA headers.
    u8 nca_main_signature_moduli_prod[NcaMainSignatureKeyGeneration_Max][RSA2048_PUBKEY_SIZE];      ///< Moduli used in retail units. Retrieved from the .rodata segment in the FS sysmodule.
    u8 nca_main_signature_moduli_dev[NcaMainSignatureKeyGeneration_Max][RSA2048_PUBKEY_SIZE];       ///< Moduli used in development units. Retrieved from the .rodata segment in the FS sysmodule.
    
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

typedef struct {
    /// AES-128-CBC keys needed to decrypt the CardInfo area from gamecard headers.
    const u8 gc_cardinfo_kek_source[AES_128_KEY_SIZE];      ///< Randomly generated KEK source to decrypt official CardInfo area keys.
    const u8 gc_cardinfo_key_prod_source[AES_128_KEY_SIZE]; ///< CardInfo area key used in retail units. Obfuscated using the above KEK source and SMC AES engine keydata.
    const u8 gc_cardinfo_key_dev_source[AES_128_KEY_SIZE];  ///< CardInfo area key used in development units. Obfuscated using the above KEK source and SMC AES engine keydata.
    
    u8 gc_cardinfo_kek_sealed[AES_128_KEY_SIZE];            ///< Generated from gc_cardinfo_kek_source. Sealed by the SMC AES engine.
    u8 gc_cardinfo_key_prod[AES_128_KEY_SIZE];              ///< Generated from gc_cardinfo_kek_sealed and gc_cardinfo_key_prod_source.
    u8 gc_cardinfo_key_dev[AES_128_KEY_SIZE];               ///< Generated from gc_cardinfo_kek_sealed and gc_cardinfo_key_dev_source.
} KeysGameCardKeyset;

/// Used to parse the eTicket RSA device key retrieved from PRODINFO via setcalGetEticketDeviceKey().
/// Everything after the AES CTR is encrypted using the eTicket RSA device key encryption key.
typedef struct {
    u8 ctr[AES_128_KEY_SIZE];
    u8 private_exponent[RSA2048_BYTES];
    u8 modulus[RSA2048_BYTES];
    u32 public_exponent;                ///< Stored using big endian byte order. Must match ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT.
    u8 padding[0x14];
    u64 device_id;
    u8 ghash[0x10];
} EticketRsaDeviceKey;

NXDT_ASSERT(EticketRsaDeviceKey, 0x240);

/* Function prototypes. */

static bool keysIsProductionModulus1xMandatory(void);
static bool keysIsProductionModulus9xMandatory(void);

static bool keysIsDevelopmentModulus1xMandatory(void);
static bool keysIsDevelopmentModulus9xMandatory(void);

static bool keysRetrieveKeysFromProgramMemory(KeysMemoryInfo *info);

static bool keysDeriveNcaHeaderKey(void);
static bool keysDeriveSealedNcaKeyAreaEncryptionKeys(void);

static int keysGetKeyAndValueFromFile(FILE *f, char **line, char **key, char **value);
static char keysConvertHexDigitToBinary(char c);
static bool keysParseHexKey(u8 *out, const char *key, const char *value, u32 size);
static bool keysReadKeysFromFile(void);

static bool keysGetDecryptedEticketRsaDeviceKey(void);
static bool keysTestEticketRsaDeviceKey(const void *e, const void *d, const void *n);

static bool keysDeriveGameCardKeys(void);

/* Global variables. */

static KeysNcaKeyset g_ncaKeyset = {0};

static KeysGameCardKeyset g_gameCardKeyset = {
    .gc_cardinfo_kek_source      = { 0xDE, 0xC6, 0x3F, 0x6A, 0xBF, 0x37, 0x72, 0x0B, 0x7E, 0x54, 0x67, 0x6A, 0x2D, 0xEF, 0xDD, 0x97 },
    .gc_cardinfo_key_prod_source = { 0xF4, 0x92, 0x06, 0x52, 0xD6, 0x37, 0x70, 0xAF, 0xB1, 0x9C, 0x6F, 0x63, 0x09, 0x01, 0xF6, 0x29 },
    .gc_cardinfo_key_dev_source  = { 0x0B, 0x7D, 0xBB, 0x2C, 0xCF, 0x64, 0x1A, 0xF4, 0xD7, 0x38, 0x81, 0x3F, 0x0C, 0x33, 0xF4, 0x1C },
    .gc_cardinfo_kek_sealed      = {0},
    .gc_cardinfo_key_prod        = {0},
    .gc_cardinfo_key_dev         = {0}
};

static bool g_keysetLoaded = false;
static Mutex g_keysetMutex = 0;

static SetCalRsa2048DeviceKey g_eTicketRsaDeviceKey = {0};

static KeysMemoryInfo g_fsRodataMemoryInfo = {
    .location = {
        .program_id = FS_SYSMODULE_TID,
        .mask = MemoryProgramSegmentType_Rodata,
        .data = NULL,
        .data_size = 0
    },
    .key_count = 8,
    .keys = {
        {
            .name = "nca_header_kek_source",
            .hash = {
                0x18, 0x88, 0xCA, 0xED, 0x55, 0x51, 0xB3, 0xED, 0xE0, 0x14, 0x99, 0xE8, 0x7C, 0xE0, 0xD8, 0x68,
                0x27, 0xF8, 0x08, 0x20, 0xEF, 0xB2, 0x75, 0x92, 0x10, 0x55, 0xAA, 0x4E, 0x2A, 0xBD, 0xFF, 0xC2
            },
            .size = sizeof(g_ncaKeyset.nca_header_kek_source),
            .dst = g_ncaKeyset.nca_header_kek_source,
            .mandatory_func = NULL
        },
        {
            .name = "nca_main_signature_modulus_prod_00",
            .hash = {
                0xF9, 0x2E, 0x84, 0x98, 0x17, 0x2C, 0xAF, 0x9C, 0x20, 0xE3, 0xF1, 0xF7, 0xD3, 0xE7, 0x2C, 0x62,
                0x50, 0xA9, 0x40, 0x7A, 0xE7, 0x84, 0xE0, 0x03, 0x58, 0x07, 0x85, 0xA5, 0x68, 0x0B, 0x80, 0x33
            },
            .size = sizeof(g_ncaKeyset.nca_main_signature_moduli_prod[NcaMainSignatureKeyGeneration_Since100NUP]),
            .dst = g_ncaKeyset.nca_main_signature_moduli_prod[NcaMainSignatureKeyGeneration_Since100NUP],
            .mandatory_func = &keysIsProductionModulus1xMandatory
        },
        {
            .name = "nca_main_signature_modulus_prod_01",
            .hash = {
                0x5F, 0x6B, 0xE3, 0x1C, 0x31, 0x6E, 0x7C, 0xB2, 0x1C, 0xA7, 0xB9, 0xA1, 0x70, 0x6A, 0x9D, 0x58,
                0x04, 0xEB, 0x90, 0x53, 0x72, 0xEF, 0xCB, 0x56, 0xD1, 0x93, 0xF2, 0xAF, 0x9E, 0x8A, 0xD1, 0xFA
            },
            .size = sizeof(g_ncaKeyset.nca_main_signature_moduli_prod[NcaMainSignatureKeyGeneration_Since900NUP]),
            .dst = g_ncaKeyset.nca_main_signature_moduli_prod[NcaMainSignatureKeyGeneration_Since900NUP],
            .mandatory_func = &keysIsProductionModulus9xMandatory
        },
        {
            .name = "nca_main_signature_modulus_dev_00",
            .hash = {
                0x50, 0xF8, 0x26, 0xBB, 0x13, 0xFE, 0xB2, 0x6D, 0x83, 0xCF, 0xFF, 0xD8, 0x38, 0x45, 0xC3, 0x51,
                0x4D, 0xCB, 0x06, 0x91, 0x83, 0x52, 0x06, 0x35, 0x7A, 0xC1, 0xDA, 0x6B, 0xF1, 0x60, 0x9F, 0x18
            },
            .size = sizeof(g_ncaKeyset.nca_main_signature_moduli_dev[NcaMainSignatureKeyGeneration_Since100NUP]),
            .dst = g_ncaKeyset.nca_main_signature_moduli_dev[NcaMainSignatureKeyGeneration_Since100NUP],
            .mandatory_func = &keysIsDevelopmentModulus1xMandatory
        },
        {
            .name = "nca_main_signature_modulus_dev_01",
            .hash = {
                0x56, 0xF5, 0x06, 0xEF, 0x8E, 0xCA, 0x2A, 0x29, 0x6F, 0x65, 0x45, 0xE1, 0x87, 0x60, 0x01, 0x11,
                0xBC, 0xC7, 0x38, 0x56, 0x99, 0x16, 0xAD, 0xA5, 0xDD, 0x89, 0xF2, 0xE9, 0xAB, 0x28, 0x5B, 0x18
            },
            .size = sizeof(g_ncaKeyset.nca_main_signature_moduli_dev[NcaMainSignatureKeyGeneration_Since900NUP]),
            .dst = g_ncaKeyset.nca_main_signature_moduli_dev[NcaMainSignatureKeyGeneration_Since900NUP],
            .mandatory_func = &keysIsDevelopmentModulus9xMandatory
        },
        {
            .name = "nca_kaek_application_source",
            .hash = {
                0x04, 0xAD, 0x66, 0x14, 0x3C, 0x72, 0x6B, 0x2A, 0x13, 0x9F, 0xB6, 0xB2, 0x11, 0x28, 0xB4, 0x6F,
                0x56, 0xC5, 0x53, 0xB2, 0xB3, 0x88, 0x71, 0x10, 0x30, 0x42, 0x98, 0xD8, 0xD0, 0x09, 0x2D, 0x9E
            },
            .size = sizeof(g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_Application]),
            .dst = g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_Application],
            .mandatory_func = NULL
        },
        {
            .name = "nca_kaek_ocean_source",
            .hash = {
                0xFD, 0x43, 0x40, 0x00, 0xC8, 0xFF, 0x2B, 0x26, 0xF8, 0xE9, 0xA9, 0xD2, 0xD2, 0xC1, 0x2F, 0x6B,
                0xE5, 0x77, 0x3C, 0xBB, 0x9D, 0xC8, 0x63, 0x00, 0xE1, 0xBD, 0x99, 0xF8, 0xEA, 0x33, 0xA4, 0x17
            },
            .size = sizeof(g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_Ocean]),
            .dst = g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_Ocean],
            .mandatory_func = NULL
        },
        {
            .name = "nca_kaek_system_source",
            .hash = {
                0x1F, 0x17, 0xB1, 0xFD, 0x51, 0xAD, 0x1C, 0x23, 0x79, 0xB5, 0x8F, 0x15, 0x2C, 0xA4, 0x91, 0x2E,
                0xC2, 0x10, 0x64, 0x41, 0xE5, 0x17, 0x22, 0xF3, 0x87, 0x00, 0xD5, 0x93, 0x7A, 0x11, 0x62, 0xF7
            },
            .size = sizeof(g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_System]),
            .dst = g_ncaKeyset.nca_kaek_sources[NcaKeyAreaEncryptionKeyIndex_System],
            .mandatory_func = NULL
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
            .size = sizeof(g_ncaKeyset.nca_header_key_source),
            .dst = g_ncaKeyset.nca_header_key_source,
            .mandatory_func = NULL
        }
    }
};

bool keysLoadKeyset(void)
{
    bool ret = false;
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        ret = g_keysetLoaded;
        if (ret) break;
        
        /* Retrieve FS .rodata keys. */
        if (!keysRetrieveKeysFromProgramMemory(&g_fsRodataMemoryInfo))
        {
            LOG_MSG("Unable to retrieve keys from FS .rodata segment!");
            break;
        }
        
        /* Retrieve FS .data keys. */
        if (!keysRetrieveKeysFromProgramMemory(&g_fsDataMemoryInfo))
        {
            LOG_MSG("Unable to retrieve keys from FS .data segment!");
            break;
        }
        
        /* Derive NCA header key. */
        if (!keysDeriveNcaHeaderKey())
        {
            LOG_MSG("Unable to derive NCA header key!");
            break;
        }
        
        /* Derive sealed NCA KAEKs. */
        if (!keysDeriveSealedNcaKeyAreaEncryptionKeys())
        {
            LOG_MSG("Unable to derive sealed NCA KAEKs!");
            break;
        }
        
        /* Read additional keys from the keys file. */
        if (!keysReadKeysFromFile()) break;
        
        /* Get decrypted eTicket RSA device key. */
        if (!keysGetDecryptedEticketRsaDeviceKey()) break;
        
        /* Derive gamecard keys. */
        if (!keysDeriveGameCardKeys()) break;
        
        /* Update flags. */
        ret = g_keysetLoaded = true;
    }
    
    /*if (ret)
    {
        LOG_DATA(&g_ncaKeyset, sizeof(KeysNcaKeyset), "NCA keyset dump:");
        LOG_DATA(&g_eTicketRsaDeviceKey, sizeof(SetCalRsa2048DeviceKey), "eTicket RSA device key dump:");
        LOG_DATA(&g_gameCardKeyset, sizeof(KeysGameCardKeyset), "Gamecard keyset dump:");
    }*/
    
    return ret;
}

const u8 *keysGetNcaHeaderKey(void)
{
    const u8 *ret = NULL;
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        if (g_keysetLoaded) ret = (const u8*)(g_ncaKeyset.nca_header_key);
    }
    
    return ret;
}

const u8 *keysGetNcaMainSignatureModulus(u8 key_generation)
{
    if (key_generation > NcaMainSignatureKeyGeneration_Current)
    {
        LOG_MSG("Unsupported key generation value! (0x%02X).", key_generation);
        return NULL;
    }
    
    bool dev_unit = utilsIsDevelopmentUnit();
    const u8 *ret = NULL, null_modulus[RSA2048_PUBKEY_SIZE] = {0};
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        if (!g_keysetLoaded) break;
        
        ret = (const u8*)(dev_unit ? g_ncaKeyset.nca_main_signature_moduli_dev[key_generation] : g_ncaKeyset.nca_main_signature_moduli_prod[key_generation]);
        
        if (!memcmp(ret, null_modulus, RSA2048_PUBKEY_SIZE))
        {
            LOG_MSG("%s NCA header main signature modulus 0x%02X unavailable.", dev_unit ? "Development" : "Retail", key_generation);
            ret = NULL;
        }
    }
    
    return ret;
}

bool keysDecryptNcaKeyAreaEntry(u8 kaek_index, u8 key_generation, void *dst, const void *src)
{
    bool ret = false;
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
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        if (!g_keysetLoaded) break;
        Result rc = splCryptoGenerateAesKey(g_ncaKeyset.nca_kaek_sealed[kaek_index][key_gen_val], src, dst);
        if (!(ret = R_SUCCEEDED(rc))) LOG_MSG("splCryptoGenerateAesKey failed! (0x%08X).", rc);
    }
    
end:
    return ret;
}

const u8 *keysGetNcaKeyAreaEncryptionKey(u8 kaek_index, u8 key_generation)
{
    const u8 *ret = NULL;
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
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        if (g_keysetLoaded) ret = (const u8*)(g_ncaKeyset.nca_kaek[kaek_index][key_gen_val]);
    }
    
end:
    return ret;
}

bool keysDecryptRsaOaepWrappedTitleKey(const void *rsa_wrapped_titlekey, void *out_titlekey)
{
    if (!rsa_wrapped_titlekey || !out_titlekey)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    bool ret = false;
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        if (!g_keysetLoaded) break;
        
        size_t out_keydata_size = 0;
        u8 out_keydata[RSA2048_BYTES] = {0};
        
        /* Get eTicket RSA device key. */
        EticketRsaDeviceKey *eticket_rsa_key = (EticketRsaDeviceKey*)g_eTicketRsaDeviceKey.key;
        
        /* Perform a RSA-OAEP unwrap operation to get the encrypted titlekey. */
        /* ES uses a NULL string as the label. */
        ret = (rsa2048OaepDecrypt(out_keydata, sizeof(out_keydata), rsa_wrapped_titlekey, eticket_rsa_key->modulus, &(eticket_rsa_key->public_exponent), sizeof(eticket_rsa_key->public_exponent), \
                                  eticket_rsa_key->private_exponent, sizeof(eticket_rsa_key->private_exponent), NULL, 0, &out_keydata_size) && out_keydata_size >= AES_128_KEY_SIZE);
        if (ret)
        {
            /* Copy RSA-OAEP unwrapped titlekey. */
            memcpy(out_titlekey, out_keydata, AES_128_KEY_SIZE);
        } else {
            LOG_MSG("RSA-OAEP titlekey decryption failed!");
        }
    }
    
    return ret;
}

const u8 *keysGetTicketCommonKey(u8 key_generation)
{
    const u8 *ret = NULL;
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);
    
    if (key_gen_val >= NcaKeyGeneration_Max)
    {
        LOG_MSG("Invalid key generation value! (0x%02X).", key_gen_val);
        goto end;
    }
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        if (g_keysetLoaded) ret = (const u8*)(g_ncaKeyset.ticket_common_keys[key_gen_val]);
    }
    
end:
    return ret;
}

const u8 *keysGetGameCardInfoKey(void)
{
    const u8 *ret = NULL;
    
    SCOPED_LOCK(&g_keysetMutex)
    {
        if (g_keysetLoaded) ret = (const u8*)(utilsIsDevelopmentUnit() ? g_gameCardKeyset.gc_cardinfo_key_dev : g_gameCardKeyset.gc_cardinfo_key_prod);
    }
    
    return ret;
}

static bool keysIsProductionModulus1xMandatory(void)
{
    return !utilsIsDevelopmentUnit();
}

static bool keysIsProductionModulus9xMandatory(void)
{
    return (!utilsIsDevelopmentUnit() && hosversionAtLeast(9, 0, 0));
}

static bool keysIsDevelopmentModulus1xMandatory(void)
{
    return utilsIsDevelopmentUnit();
}

static bool keysIsDevelopmentModulus9xMandatory(void)
{
    return (utilsIsDevelopmentUnit() && hosversionAtLeast(9, 0, 0));
}

static bool keysRetrieveKeysFromProgramMemory(KeysMemoryInfo *info)
{
    if (!info || !info->key_count)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    u8 tmp_hash[SHA256_HASH_SIZE];
    bool success = false;
    
    if (!memRetrieveProgramMemorySegment(&(info->location))) return false;
    
    for(u32 i = 0; i < info->key_count; i++)
    {
        KeysMemoryKey *key = &(info->keys[i]);
        bool found = false, mandatory = (key->mandatory_func != NULL ? key->mandatory_func() : true);
        
        /* Skip key if it's not mandatory. */
        if (!mandatory) continue;
        
        /* Check destination pointer. */
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
 * The format of a line must match /^[ \t]*\w+[ \t]*[,=][ \t]*(?:[A-Fa-f0-9]{2})+[ \t]*$/.
 * If a line ends in \r, the final \r is stripped.
 * The input file is assumed to have been opened with the 'b' flag.
 * The input file is assumed to contain only ASCII.
 *
 * On success, *line will point to a dynamically allocated buffer that holds
 * the read line, whilst *key and *value will be set to point to the key and
 * value strings within *line, respectively. *line must be freed by the caller.
 * On failure, *line, *key and *value will all be set to NULL.
 * Empty lines and end of file are both considered failures.
 *
 * This function is thread-safe.
 *
 * Both key and value strings will be converted to lowercase.
 * Empty key and/or value strings are both considered a parse error.
 * Furthermore, a parse error will also be returned if the value string length
 * is not a multiple of 2.
 *
 * This function assumes that the file can be trusted not to contain any NUL in
 * the contents.
 *
 * Whitespace (' ', ASCII 0x20, as well as '\t', ASCII 0x09) at the beginning of
 * the line, at the end of the line as well as around = (or ,) will be ignored.
 *
 * @param f the file to read
 * @param line pointer to change to point to the read line
 * @param key pointer to change to point to the key
 * @param value pointer to change to point to the value
 * @return 0 on success,
 *         1 on end of file,
 *         -1 on parse error (line malformed, empty line)
 *         -2 on I/O error
 */
static int keysGetKeyAndValueFromFile(FILE *f, char **line, char **key, char **value)
{
    if (!f || !line || !key || !value)
    {
        LOG_MSG("Invalid parameters!");
        return -2;
    }
    
    int ret = -1;
    size_t n = 0;
    ssize_t read = 0;
    char *l = NULL, *k = NULL, *v = NULL, *p = NULL, *e = NULL;
    
    /* Clear inputs beforehand. */
    if (*line) free(*line);
    *line = *key = *value = NULL;
    errno = 0;
    
    /* Read line. */
    read = __getline(line, &n, f);
    if (errno != 0 || read <= 0)
    {
        ret = ((errno == 0 && (read == 0 || feof(f))) ? 1 : -2);
        if (ret != 1) LOG_MSG("__getline failed! (0x%lX, %ld, %d, %d).", ftell(f), read, errno, ret);
        goto end;
    }
    
    n = (ftell(f) - (size_t)read);
    
    /* Check if we're dealing with an empty line. */
    l = *line;
    if (*l == '\n' || *l == '\r' || *l == '\0')
    {
        LOG_MSG("Empty line detected! (0x%lX, 0x%lX).", n, read);
        goto end;
    }
    
    /* Not finding '\r' or '\n' is not a problem. */
    /* It's possible that the last line of a file isn't actually a line (i.e., does not end in '\n'). */
    /* We do want to handle those. */
    if ((p = strchr(l, '\r')) != NULL || (p = strchr(l, '\n')) != NULL)
    {
        e = p;
        *p = '\0';
    } else {
        e = (l + read + 1);
    }
    
#define SKIP_SPACE(p) do { \
        for(; (*p == ' ' || *p == '\t'); ++p); \
    } while(0);
    
    /* Skip leading whitespace before the key name string. */
    p = l;
    SKIP_SPACE(p);
    k = p;
    
    /* Validate key name string. */
    for(; *p != ' ' && *p != '\t' && *p != ',' && *p != '='; ++p)
    {
        /* Bail out if we reached the end of string. */
        if (*p == '\0')
        {
            LOG_MSG("End of string reached while validating key name string! (#1) (0x%lX, 0x%lX, 0x%lX).", n, read, (size_t)(p - l));
            goto end;
        }
        
        /* Convert uppercase characters to lowercase. */
        if (*p >= 'A' && *p <= 'Z')
        {
            *p = ('a' + (*p - 'A'));
            continue;
        }
        
        /* Handle unsupported characters. */
        if (*p != '_' && (*p < '0' || *p > '9') && (*p < 'a' || *p > 'z'))
        {
            LOG_MSG("Unsupported character detected in key name string! (0x%lX, 0x%lX, 0x%lX, 0x%02X).", n, read, (size_t)(p - l), *p);
            goto end;
        }
    }
    
    /* Bail if the final ++p put us at the end of string. */
    if (*p == '\0')
    {
        LOG_MSG("End of string reached while validating key name string! (#2) (0x%lX, 0x%lX, 0x%lX).", n, read, (size_t)(p - l));
        goto end;
    }
    
    /* We should be at the end of the key name string now and either whitespace or [,=] follows. */
    if (*p == '=' || *p == ',')
    {
        *p++ = '\0';
    } else {
        /* Skip leading whitespace before [,=]. */
        *p++ = '\0';
        SKIP_SPACE(p);
        
        if (*p != '=' && *p != ',')
        {
            LOG_MSG("Unable to find expected [,=]! (0x%lX, 0x%lX, 0x%lX).", n, read, (size_t)(p - l));
            goto end;
        }
        
        *p++ = '\0';
    }
    
    /* Empty key name string is an error. */
    if (*k == '\0')
    {
        LOG_MSG("Key name string empty! (0x%lX, 0x%lX).", n, read);
        goto end;
    }
    
    /* Skip trailing whitespace after [,=]. */
    SKIP_SPACE(p);
    v = p;
    
#undef SKIP_SPACE
    
    /* Validate value string. */
    for(; p < e && *p != ' ' && *p != '\t'; ++p)
    {
        /* Bail out if we reached the end of string. */
        if (*p == '\0')
        {
            LOG_MSG("End of string reached while validating value string! (0x%lX, 0x%lX, 0x%lX, %s).", n, read, (size_t)(p - l), k);
            goto end;
        }
        
        /* Convert uppercase characters to lowercase. */
        if (*p >= 'A' && *p <= 'F')
        {
            *p = ('a' + (*p - 'A'));
            continue;
        }
        
        /* Handle unsupported characters. */
        if ((*p < '0' || *p > '9') && (*p < 'a' || *p > 'f'))
        {
            LOG_MSG("Unsupported character detected in value string! (0x%lX, 0x%lX, 0x%lX, 0x%02X, %s).", n, read, (size_t)(p - l), *p, k);
            goto end;
        }
    }
    
    /* We should be at the end of the value string now and whitespace may optionally follow. */
    l = p;
    if (p < e)
    {
        /* Skip trailing whitespace after the value string. */
        /* Make sure there's no additional data after this. */
        *p++ = '\0';
        for(; p < e && (*p == ' ' || *p == '\t'); ++p);
        
        if (p < e)
        {
            LOG_MSG("Additional data detected after value string and before line end! (0x%lX, 0x%lX, 0x%lX, %s).", n, read, (size_t)(p - *line), k);
            goto end;
        }
    }
    
    /* Empty value string and value string length not being a multiple of 2 are both errors. */
    if (*v == '\0' || ((l - v) % 2) != 0)
    {
        LOG_MSG("Invalid value string length! (0x%lX, 0x%lX, 0x%lX, %s).", n, read, (size_t)(l - v), k);
        goto end;
    }
    
    /* Update pointers. */
    *key = k;
    *value = v;
    
    /* Update return value. */
    ret = 0;
    
end:
    if (ret != 0)
    {
        if (*line) free(*line);
        *line = *key = *value = NULL;
    }
    
    return ret;
}

static char keysConvertHexDigitToBinary(char c)
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
        char val = keysConvertHexDigitToBinary(value[i]);
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
    char *line = NULL, *key = NULL, *value = NULL;
    char test_name[0x40] = {0};
    bool eticket_rsa_kek_available = false;
    const char *keys_file_path = (utilsIsDevelopmentUnit() ? DEV_KEYS_FILE_PATH : PROD_KEYS_FILE_PATH);
    
    keys_file = fopen(keys_file_path, "rb");
    if (!keys_file)
    {
        LOG_MSG("Unable to open \"%s\" to retrieve keys!", keys_file_path);
        return false;
    }
    
#define PARSE_HEX_KEY(name, out, decl) \
    if (!strcmp(key, name) && keysParseHexKey(out, key, value, sizeof(out))) { \
        key_count++; \
        decl; \
    }
    
#define PARSE_HEX_KEY_WITH_INDEX(name, out) \
    snprintf(test_name, sizeof(test_name), "%s_%02x", name, i); \
    PARSE_HEX_KEY(test_name, out, break);
    
    while(true)
    {
        /* Get key and value strings from the current line. */
        /* Break from the while loop if EOF is reached or if an I/O error occurs. */
        ret = keysGetKeyAndValueFromFile(keys_file, &line, &key, &value);
        if (ret == 1 || ret == -2) break;
        
        /* Ignore malformed or empty lines. */
        if (ret != 0 || !key || !value) continue;
        
        PARSE_HEX_KEY("eticket_rsa_kek", g_ncaKeyset.eticket_rsa_kek, eticket_rsa_kek_available = true; continue);
        
        /* This only appears on consoles that use the new PRODINFO key generation scheme. */
        PARSE_HEX_KEY("eticket_rsa_kek_personalized", g_ncaKeyset.eticket_rsa_kek_personalized, eticket_rsa_kek_available = true; continue);
        
        for(u32 i = 0; i < NcaKeyGeneration_Max; i++)
        {
            PARSE_HEX_KEY_WITH_INDEX("titlekek", g_ncaKeyset.ticket_common_keys[i]);
            
            PARSE_HEX_KEY_WITH_INDEX("key_area_key_application", g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_Application][i]);
            
            PARSE_HEX_KEY_WITH_INDEX("key_area_key_ocean", g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_Ocean][i]);
            
            PARSE_HEX_KEY_WITH_INDEX("key_area_key_system", g_ncaKeyset.nca_kaek[NcaKeyAreaEncryptionKeyIndex_System][i]);
        }
    }
    
#undef PARSE_HEX_KEY_WITH_INDEX
    
#undef PARSE_HEX_KEY
    
    if (line) free(line);
    
    fclose(keys_file);
    
    if (key_count)
    {
        LOG_MSG("Loaded %u key(s) from \"%s\".", key_count, keys_file_path);
    } else {
        LOG_MSG("Unable to parse keys from \"%s\"! (keys file empty?).", keys_file_path);
        return false;
    }
    
    if (!eticket_rsa_kek_available)
    {
        LOG_MSG("\"eticket_rsa_kek\" unavailable in \"%s\"!", keys_file_path);
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
    aes128CtrCrypt(&eticket_aes_ctx, &(eticket_rsa_key->private_exponent), &(eticket_rsa_key->private_exponent), sizeof(EticketRsaDeviceKey) - sizeof(eticket_rsa_key->ctr));
    
    /* Public exponent value must be 0x10001. */
    /* It is stored using big endian byte order. */
    public_exponent = __builtin_bswap32(eticket_rsa_key->public_exponent);
    if (public_exponent != ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT)
    {
        LOG_MSG("Invalid public exponent for decrypted eTicket RSA device key! Wrong keys? (0x%08X).", public_exponent);
        return false;
    }
    
    /* Test RSA key pair. */
    if (!keysTestEticketRsaDeviceKey(&(eticket_rsa_key->public_exponent), eticket_rsa_key->private_exponent, eticket_rsa_key->modulus))
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
    u8 x[RSA2048_BYTES] = {0}, y[RSA2048_BYTES] = {0}, z[RSA2048_BYTES] = {0};
    
    /* 0xCAFEBABE. */
    x[0xFC] = 0xCA;
    x[0xFD] = 0xFE;
    x[0xFE] = 0xBA;
    x[0xFF] = 0xBE;
    
    rc = splUserExpMod(x, n, d, RSA2048_BYTES, y);
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
    
    if (memcmp(x, z, RSA2048_BYTES) != 0)
    {
        LOG_MSG("Invalid RSA key pair!");
        return false;
    }
    
    return true;
}

static bool keysDeriveGameCardKeys(void)
{
    Result rc = 0;
    
    /* Derive gc_cardinfo_kek_sealed from gc_cardinfo_kek_source. */
    rc = splCryptoGenerateAesKek(g_gameCardKeyset.gc_cardinfo_kek_source, 0, 0, g_gameCardKeyset.gc_cardinfo_kek_sealed);
    if (R_FAILED(rc))
    {
        LOG_MSG("splCryptoGenerateAesKek failed! (0x%08X) (gc_cardinfo_kek_sealed).", rc);
        return false;
    }
    
    /* Derive gc_cardinfo_key_prod from gc_cardinfo_kek_sealed and gc_cardinfo_key_prod_source. */
    rc = splCryptoGenerateAesKey(g_gameCardKeyset.gc_cardinfo_kek_sealed, g_gameCardKeyset.gc_cardinfo_key_prod_source, g_gameCardKeyset.gc_cardinfo_key_prod);
    if (R_FAILED(rc))
    {
        LOG_MSG("splCryptoGenerateAesKey failed! (0x%08X) (gc_cardinfo_key_prod).", rc);
        return false;
    }
    
    /* Derive gc_cardinfo_key_dev from gc_cardinfo_kek_sealed and gc_cardinfo_key_dev_source. */
    rc = splCryptoGenerateAesKey(g_gameCardKeyset.gc_cardinfo_kek_sealed, g_gameCardKeyset.gc_cardinfo_key_dev_source, g_gameCardKeyset.gc_cardinfo_key_dev);
    if (R_FAILED(rc))
    {
        LOG_MSG("splCryptoGenerateAesKey failed! (0x%08X) (gc_cardinfo_key_dev).", rc);
        return false;
    }
    
    return true;
}
