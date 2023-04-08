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
#include "nca.h"
#include "rsa.h"
#include "aes.h"
#include "smc.h"
#include "key_sources.h"

#define ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT  0x10001

/* Type definitions. */

typedef struct {
    ///< AES-128-ECB key used to derive master KEKs from Erista master KEK sources.
    ///< Only available in Erista units. Retrieved from the Lockpick_RCM keys file.
    u8 tsec_root_key[AES_128_KEY_SIZE];

    ///< AES-128-ECB key used to derive master KEKs from Mariko master KEK sources.
    ///< Only available in Mariko units. Retrieved from the Lockpick_RCM keys file -- if available, because it must be manually bruteforced on a PC after running Lockpick_RCM.
    u8 mariko_kek[AES_128_KEY_SIZE];

    ///< AES-128-ECB keys used to decrypt the vast majority of Switch content.
    ///< Derived at runtime using hardcoded key sources and additional keydata retrieved from the Lockpick_RCM keys file.
    u8 master_keys[NcaKeyGeneration_Max][AES_128_KEY_SIZE];

    ///< AES-128-XTS key needed to handle NCA header crypto.
    ///< Generated from hardcoded key sources.
    u8 nca_header_key[AES_128_KEY_SIZE * 2];

    ///< AES-128-ECB keys needed to handle key area crypto from NCA headers.
    ///< Generated from hardcoded key sources and master keys.
    u8 nca_kaek[NcaKeyAreaEncryptionKeyIndex_Count][NcaKeyGeneration_Max][AES_128_KEY_SIZE];

    ///< AES-128-CTR key needed to decrypt the console-specific eTicket RSA device key stored in PRODINFO.
    ///< Retrieved from the Lockpick_RCM keys file. Verified by decrypting the eTicket RSA device key.
    ///< The key itself may or may not be console-specific (personalized), based on the eTicket RSA device key generation value.
    u8 eticket_rsa_kek[AES_128_KEY_SIZE];

    ///< AES-128-ECB keys needed to decrypt titlekeys.
    ///< Generated from a hardcoded key source and master keys.
    u8 ticket_common_keys[NcaKeyGeneration_Max][AES_128_KEY_SIZE];

    ///< AES-128-CBC key needed to decrypt the CardInfo area from gamecard headers.
    ///< Generated from hardcoded key sources.
    u8 gc_cardinfo_key[AES_128_KEY_SIZE];
} KeysNxKeyset;

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

static bool keysIsKeyEmpty(const void *key);

static int keysGetKeyAndValueFromFile(FILE *f, char **line, char **key, char **value);
static char keysConvertHexDigitToBinary(char c);
static bool keysParseHexKey(u8 *out, const char *key, const char *value, u32 size);
static bool keysReadKeysFromFile(void);

static bool keysDeriveMasterKeys(void);
static bool keysDeriveNcaHeaderKey(void);
static bool keysDerivePerGenerationKeys(void);
static bool keysDeriveGcCardInfoKey(void);

static bool keysGetDecryptedEticketRsaDeviceKey(void);
static bool keysTestEticketRsaDeviceKey(const void *e, const void *d, const void *n);

static bool keysGenerateAesKek(const u8 *kek_src, u8 key_generation, SmcGenerateAesKekOption option, u8 *out_kek);
static bool keysLoadAesKey(const u8 *kek, const u8 *key_src, u8 *out_key);
static bool keysGenerateAesKey(const u8 *kek, const u8 *key_src, u8 *out_key);

static bool keysLoadAesKeyFromAesKek(const u8 *kek_src, u8 key_generation, SmcGenerateAesKekOption option, const u8 *key_src, u8 *out_key);
static bool keysGenerateAesKeyFromAesKek(const u8 *kek_src, u8 key_generation, SmcGenerateAesKekOption option, const u8 *key_src, u8 *out_key);

/* Global variables. */

static bool g_keysetLoaded = false;
static Mutex g_keysetMutex = 0;

static SetCalRsa2048DeviceKey g_eTicketRsaDeviceKey = {0};
static KeysNxKeyset g_nxKeyset = {0};

static bool g_latestMasterKeyAvailable = false;

bool keysLoadKeyset(void)
{
    bool ret = false;

    SCOPED_LOCK(&g_keysetMutex)
    {
        ret = g_keysetLoaded;
        if (ret) break;

        /* Get eTicket RSA device key. */
        Result rc = setcalGetEticketDeviceKey(&g_eTicketRsaDeviceKey);
        if (R_FAILED(rc))
        {
            LOG_MSG_ERROR("setcalGetEticketDeviceKey failed! (0x%X).", rc);
            break;
        }

        /* Read data from the Lockpick_RCM keys file. */
        if (!keysReadKeysFromFile()) break;

        /* Derive master keys. */
        if (!keysDeriveMasterKeys())
        {
            LOG_MSG_ERROR("Failed to derive master keys!");
            break;
        }

        /* Derive NCA header key. */
        if (!keysDeriveNcaHeaderKey()) break;

        /* Derive per-generation keys. */
        if (!keysDerivePerGenerationKeys()) break;

        /* Derive gamecard CardInfo key */
        if (!keysDeriveGcCardInfoKey())
        {
            LOG_MSG_ERROR("Failed to derive gamecard CardInfo key!");
            break;
        }

        /* Get decrypted eTicket RSA device key. */
        if (!keysGetDecryptedEticketRsaDeviceKey()) break;

        /* Update flags. */
        ret = g_keysetLoaded = true;
    }

#if LOG_LEVEL == LOG_LEVEL_DEBUG
    LOG_DATA_DEBUG(&g_eTicketRsaDeviceKey, sizeof(SetCalRsa2048DeviceKey), "eTicket RSA device key dump:");
    LOG_DATA_DEBUG(&g_nxKeyset, sizeof(KeysNxKeyset), "NX keyset dump:");
#endif

    return ret;
}

const u8 *keysGetNcaHeaderKey(void)
{
    const u8 *ret = NULL;

    SCOPED_LOCK(&g_keysetMutex)
    {
        if (g_keysetLoaded) ret = (const u8*)(g_nxKeyset.nca_header_key);
    }

    return ret;
}

const u8 *keysGetNcaKeyAreaEncryptionKey(u8 kaek_index, u8 key_generation)
{
    const u8 *ret = NULL;
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);

    if (kaek_index >= NcaKeyAreaEncryptionKeyIndex_Count)
    {
        LOG_MSG_ERROR("Invalid KAEK index! (0x%02X).", kaek_index);
        goto end;
    }

    if (key_generation >= NcaKeyGeneration_Max)
    {
        LOG_MSG_ERROR("Invalid key generation value! (0x%02X).", key_generation);
        goto end;
    }

    SCOPED_LOCK(&g_keysetMutex)
    {
        if (!g_keysetLoaded) break;

        ret = (const u8*)(g_nxKeyset.nca_kaek[kaek_index][key_gen_val]);

        if (keysIsKeyEmpty(ret))
        {
            LOG_MSG_ERROR("NCA KAEK for type %u and generation %u unavailable.", kaek_index, key_gen_val);
            ret = NULL;
        }
    }

end:
    return ret;
}

bool keysDecryptRsaOaepWrappedTitleKey(const void *rsa_wrapped_titlekey, void *out_titlekey)
{
    if (!rsa_wrapped_titlekey || !out_titlekey)
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
            LOG_MSG_ERROR("RSA-OAEP titlekey decryption failed!");
        }
    }

    return ret;
}

const u8 *keysGetTicketCommonKey(u8 key_generation)
{
    const u8 *ret = NULL;
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);

    if (key_generation >= NcaKeyGeneration_Max)
    {
        LOG_MSG_ERROR("Invalid key generation value! (0x%02X).", key_generation);
        goto end;
    }

    SCOPED_LOCK(&g_keysetMutex)
    {
        if (!g_keysetLoaded) break;

        ret = (const u8*)(g_nxKeyset.ticket_common_keys[key_gen_val]);

        if (keysIsKeyEmpty(ret))
        {
            LOG_MSG_ERROR("Ticket common key for generation %u unavailable.", key_gen_val);
            ret = NULL;
        }
    }

end:
    return ret;
}

const u8 *keysGetGameCardInfoKey(void)
{
    const u8 *ret = NULL;

    SCOPED_LOCK(&g_keysetMutex)
    {
        if (g_keysetLoaded) ret = (const u8*)(g_nxKeyset.gc_cardinfo_key);
    }

    return ret;
}

static bool keysIsKeyEmpty(const void *key)
{
    const u8 null_key[AES_128_KEY_SIZE] = {0};
    return (memcmp(key, null_key, AES_128_KEY_SIZE) == 0);
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
        LOG_MSG_ERROR("Invalid parameters!");
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
        if (ret != 1) LOG_MSG_ERROR("__getline failed! (0x%lX, %ld, %d, %d).", ftell(f), read, errno, ret);
        goto end;
    }

    n = (ftell(f) - (size_t)read);

    /* Check if we're dealing with an empty line. */
    l = *line;
    if (*l == '\n' || *l == '\r' || *l == '\0')
    {
        LOG_MSG_WARNING("Empty line detected! (0x%lX, 0x%lX).", n, read);
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
            LOG_MSG_ERROR("End of string reached while validating key name string! (#1) (0x%lX, 0x%lX, 0x%lX).", n, read, (size_t)(p - l));
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
            LOG_MSG_ERROR("Unsupported character detected in key name string! (0x%lX, 0x%lX, 0x%lX, 0x%02X).", n, read, (size_t)(p - l), *p);
            goto end;
        }
    }

    /* Bail if the final ++p put us at the end of string. */
    if (*p == '\0')
    {
        LOG_MSG_ERROR("End of string reached while validating key name string! (#2) (0x%lX, 0x%lX, 0x%lX).", n, read, (size_t)(p - l));
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
            LOG_MSG_ERROR("Unable to find expected [,=]! (0x%lX, 0x%lX, 0x%lX).", n, read, (size_t)(p - l));
            goto end;
        }

        *p++ = '\0';
    }

    /* Empty key name string is an error. */
    if (*k == '\0')
    {
        LOG_MSG_ERROR("Key name string empty! (0x%lX, 0x%lX).", n, read);
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
            LOG_MSG_ERROR("End of string reached while validating value string! (0x%lX, 0x%lX, 0x%lX, %s).", n, read, (size_t)(p - l), k);
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
            LOG_MSG_ERROR("Unsupported character detected in value string! (0x%lX, 0x%lX, 0x%lX, 0x%02X, %s).", n, read, (size_t)(p - l), *p, k);
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
            LOG_MSG_ERROR("Additional data detected after value string and before line end! (0x%lX, 0x%lX, 0x%lX, %s).", n, read, (size_t)(p - *line), k);
            goto end;
        }
    }

    /* Empty value string and value string length not being a multiple of 2 are both errors. */
    if (*v == '\0' || ((l - v) % 2) != 0)
    {
        LOG_MSG_ERROR("Invalid value string length! (0x%lX, 0x%lX, 0x%lX, %s).", n, read, (size_t)(l - v), k);
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
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    if (value_len != hex_str_len)
    {
        LOG_MSG_ERROR("Key \"%s\" must be %u hex digits long!", key, hex_str_len);
        return false;
    }

    memset(out, 0, size);

    for(u32 i = 0; i < hex_str_len; i++)
    {
        char val = keysConvertHexDigitToBinary(value[i]);
        if (val == 'z')
        {
            LOG_MSG_ERROR("Invalid hex character in key \"%s\" at position %u!", key, i);
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

    const char *keys_file_path = (utilsIsDevelopmentUnit() ? DEV_KEYS_FILE_PATH : PROD_KEYS_FILE_PATH);

    bool is_mariko = utilsIsMarikoUnit();
    bool tsec_root_key_available = false, mariko_kek_available = false;
    bool use_personalized_eticket_rsa_kek = (g_eTicketRsaDeviceKey.generation > 0), eticket_rsa_kek_available = false;

    keys_file = fopen(keys_file_path, "rb");
    if (!keys_file)
    {
        LOG_MSG_ERROR("Unable to open \"%s\" to retrieve keys!", keys_file_path);
        return false;
    }

#define PARSE_HEX_KEY(name, out, decl) \
    if (!strcasecmp(key, name) && keysParseHexKey(out, key, value, sizeof(out))) { \
        key_count++; \
        decl; \
    }

#define PARSE_HEX_KEY_WITH_INDEX(name, idx, out, decl) \
    snprintf(test_name, sizeof(test_name), "%s_%02x", name, idx); \
    PARSE_HEX_KEY(test_name, out, decl);

    while(true)
    {
        /* Get key and value strings from the current line. */
        /* Break from the while loop if EOF is reached or if an I/O error occurs. */
        ret = keysGetKeyAndValueFromFile(keys_file, &line, &key, &value);
        if (ret == 1 || ret == -2) break;

        /* Ignore malformed or empty lines. */
        if (ret != 0 || !key || !value) continue;

        if (is_mariko)
        {
            /* Parse Mariko KEK. */
            /* This will only appear on Mariko units. */
            if (!mariko_kek_available)
            {
                PARSE_HEX_KEY("mariko_kek", g_nxKeyset.mariko_kek, mariko_kek_available = true; continue);
            }
        } else {
            /* Parse TSEC root key. */
            /* This will only appear on Erista units. */
            if (!tsec_root_key_available)
            {
                PARSE_HEX_KEY_WITH_INDEX("tsec_root_key", TSEC_ROOT_KEY_VERSION, g_nxKeyset.tsec_root_key, tsec_root_key_available = true; continue);
            }
        }

        /* Parse eTicket RSA device KEK. */
        /* The personalized entry only appears on consoles that use the new PRODINFO key generation scheme. */
        if (!eticket_rsa_kek_available)
        {
            PARSE_HEX_KEY(use_personalized_eticket_rsa_kek ? "eticket_rsa_kek_personalized" : "eticket_rsa_kek", g_nxKeyset.eticket_rsa_kek, eticket_rsa_kek_available = true; continue);
        }

        /* Parse master keys, starting with the last known one. */
        for(u8 i = NcaKeyGeneration_Current; i <= NcaKeyGeneration_Max; i++)
        {
            u8 key_gen_val = (i - 1);
            PARSE_HEX_KEY_WITH_INDEX("master_key", key_gen_val, g_nxKeyset.master_keys[key_gen_val], break);
        }
    }

#undef PARSE_HEX_KEY_WITH_INDEX

#undef PARSE_HEX_KEY

    if (line) free(line);

    fclose(keys_file);

    /* Bail out if we didn't retrieve a single key. */
    if (key_count)
    {
        LOG_MSG_INFO("Loaded %u key(s) from \"%s\".", key_count, keys_file_path);
    } else {
        LOG_MSG_ERROR("Unable to parse keys from \"%s\"! (keys file empty?).", keys_file_path);
        return false;
    }

    /* Check if the latest master key was retrieved. */
    g_latestMasterKeyAvailable = !keysIsKeyEmpty(g_nxKeyset.master_keys[NcaKeyGeneration_Current - 1]);
    if (!g_latestMasterKeyAvailable)
    {
        LOG_MSG_WARNING("Latest known master key (%02X) unavailable in \"%s\". Latest master key derivation will be carried out.", NcaKeyGeneration_Current - 1, keys_file_path);

        /* Make sure we have what we need to derive the latest master key. */
        if (is_mariko)
        {
            if (!mariko_kek_available)
            {
                LOG_MSG_ERROR("Mariko KEK unavailable in \"%s\"!", keys_file_path);
                return false;
            }
        } else {
            if (!tsec_root_key_available)
            {
                LOG_MSG_ERROR("TSEC root key unavailable in \"%s\"!", keys_file_path);
                return false;
            }
        }
    }

    if (!eticket_rsa_kek_available)
    {
        LOG_MSG_ERROR("eTicket RSA KEK unavailable in \"%s\"!", keys_file_path);
        return false;
    }

    return true;
}

static bool keysDeriveMasterKeys(void)
{
    u8 tmp[AES_128_KEY_SIZE] = {0};
    u8 latest_mkey_index = (NcaKeyGeneration_Current - 1);
    bool is_dev = utilsIsDevelopmentUnit();

    /* Only derive the latest master key if it hasn't been populated already. */
    if (!g_latestMasterKeyAvailable)
    {
        if (utilsIsMarikoUnit())
        {
            /* Derive the latest master KEK using the hardcoded Mariko master KEK source and the Mariko KEK. */
            aes128EcbCrypt(tmp, is_dev ? g_marikoMasterKekSourceDev : g_marikoMasterKekSourceProd, g_nxKeyset.mariko_kek, false);
        } else {
            /* Derive the latest master KEK using the hardcoded Erista master KEK source and the TSEC root key. */
            aes128EcbCrypt(tmp, g_eristaMasterKekSource, g_nxKeyset.tsec_root_key, false);
        }

        /* Derive the latest master key using the hardcoded master key source and the latest master KEK. */
        aes128EcbCrypt(g_nxKeyset.master_keys[latest_mkey_index], g_masterKeySource, tmp, false);
    }

    /* Derive all lower master keys using the latest master key and the master key vectors. */
    for(u8 i = latest_mkey_index; i > NcaKeyGeneration_Since100NUP; i--) aes128EcbCrypt(g_nxKeyset.master_keys[i - 1], is_dev ? g_masterKeyVectorsDev[i] : g_masterKeyVectorsProd[i], \
                                                                                        g_nxKeyset.master_keys[i], false);

    /* Check if we derived the right keys. */
    aes128EcbCrypt(tmp, is_dev ? g_masterKeyVectorsDev[NcaKeyGeneration_Since100NUP] : g_masterKeyVectorsProd[NcaKeyGeneration_Since100NUP], \
                   g_nxKeyset.master_keys[NcaKeyGeneration_Since100NUP], false);

    return keysIsKeyEmpty(tmp);
}

static bool keysDeriveNcaHeaderKey(void)
{
    u8 nca_header_kek[AES_128_KEY_SIZE] = {0};

    SmcGenerateAesKekOption option = {0};
    smcPrepareGenerateAesKekOption(false, SmcKeyType_Default, SmcSealKey_LoadAesKey, &option);

    /* Derive nca_header_kek using g_ncaHeaderKekSource and master key 00. */
    if (!keysGenerateAesKek(g_ncaHeaderKekSource, NcaKeyGeneration_Since100NUP, option, nca_header_kek))
    {
        LOG_MSG_ERROR("Failed to derive NCA header KEK!");
        return false;
    }

    /* Derive nca_header_key (first half) from nca_header_kek and g_ncaHeaderKeySource. */
    if (!keysGenerateAesKey(nca_header_kek, g_ncaHeaderKeySource, g_nxKeyset.nca_header_key))
    {
        LOG_MSG_ERROR("Failed to derive NCA header key! (#1).");
        return false;
    }

    /* Derive nca_header_key (second half) from nca_header_kek and g_ncaHeaderKeySource. */
    if (!keysGenerateAesKey(nca_header_kek, g_ncaHeaderKeySource + AES_128_KEY_SIZE, g_nxKeyset.nca_header_key + AES_128_KEY_SIZE))
    {
        LOG_MSG_ERROR("Failed to derive NCA header key! (#2).");
        return false;
    }

    return true;
}

static bool keysDerivePerGenerationKeys(void)
{
    SmcGenerateAesKekOption option = {0};
    smcPrepareGenerateAesKekOption(false, SmcKeyType_Default, SmcSealKey_LoadAesKey, &option);

    bool success = true;

    for(u8 i = 1; i <= NcaKeyGeneration_Max; i++)
    {
        u8 key_gen_val = (i - 1);

        /* Make sure we're not dealing with an unpopulated master key entry. */
        if (i > NcaKeyGeneration_Current && keysIsKeyEmpty(g_nxKeyset.master_keys[key_gen_val]))
        {
            //LOG_MSG_DEBUG("Master key %02X unavailable.", key_gen_val);
            continue;
        }

        /* Derive NCA key area keys for this generation. */
        for(u8 j = 0; j < NcaKeyAreaEncryptionKeyIndex_Count; j++)
        {
            if (!keysLoadAesKeyFromAesKek(g_ncaKeyAreaEncryptionKeySources[j], i, option, g_aesKeyGenerationSource, g_nxKeyset.nca_kaek[j][key_gen_val]))
            {
                LOG_MSG_DEBUG("Failed to derive NCA KAEK for type %u and generation %u!", j, key_gen_val);
                success = false;
                break;
            }
        }

        if (!success) break;

        /* Derive ticket common key for this generation. */
        aes128EcbCrypt(g_nxKeyset.ticket_common_keys[key_gen_val], g_ticketCommonKeySource, g_nxKeyset.master_keys[key_gen_val], false);
    }

    return success;
}

static bool keysDeriveGcCardInfoKey(void)
{
    SmcGenerateAesKekOption option = {0};
    const u8 *key_src = (utilsIsDevelopmentUnit() ? g_gcCardInfoKeySourceDev : g_gcCardInfoKeySourceProd);
    smcPrepareGenerateAesKekOption(false, SmcKeyType_Default, SmcSealKey_LoadAesKey, &option);
    return keysGenerateAesKeyFromAesKek(g_gcCardInfoKekSource, NcaKeyGeneration_Since100NUP, option, key_src, g_nxKeyset.gc_cardinfo_key);
}

static bool keysGetDecryptedEticketRsaDeviceKey(void)
{
    u32 public_exponent = 0;
    Aes128CtrContext eticket_aes_ctx = {0};
    EticketRsaDeviceKey *eticket_rsa_key = (EticketRsaDeviceKey*)g_eTicketRsaDeviceKey.key;

    /* Decrypt eTicket RSA device key. */
    aes128CtrContextCreate(&eticket_aes_ctx, g_nxKeyset.eticket_rsa_kek, eticket_rsa_key->ctr);
    aes128CtrCrypt(&eticket_aes_ctx, &(eticket_rsa_key->private_exponent), &(eticket_rsa_key->private_exponent), sizeof(EticketRsaDeviceKey) - sizeof(eticket_rsa_key->ctr));

    /* Public exponent value must be 0x10001. */
    /* It is stored using big endian byte order. */
    public_exponent = __builtin_bswap32(eticket_rsa_key->public_exponent);
    if (public_exponent != ETICKET_RSA_DEVICE_KEY_PUBLIC_EXPONENT)
    {
        LOG_MSG_ERROR("Invalid public exponent for decrypted eTicket RSA device key! Wrong keys? (0x%X).", public_exponent);
        return false;
    }

    /* Test RSA key pair. */
    if (!keysTestEticketRsaDeviceKey(&(eticket_rsa_key->public_exponent), eticket_rsa_key->private_exponent, eticket_rsa_key->modulus))
    {
        LOG_MSG_ERROR("eTicket RSA device key test failed! Wrong keys?");
        return false;
    }

    return true;
}

static bool keysTestEticketRsaDeviceKey(const void *e, const void *d, const void *n)
{
    if (!e || !d || !n)
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
        LOG_MSG_ERROR("splUserExpMod failed! (#1) (0x%X).", rc);
        return false;
    }

    rc = splUserExpMod(y, n, e, 4, z);
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("splUserExpMod failed! (#2) (0x%X).", rc);
        return false;
    }

    if (memcmp(x, z, RSA2048_BYTES) != 0)
    {
        LOG_MSG_ERROR("Invalid RSA key pair!");
        return false;
    }

    return true;
}

/* Based on splCryptoGenerateAesKek(). Excludes key sealing and device-unique shenanigans. */
static bool keysGenerateAesKek(const u8 *kek_src, u8 key_generation, SmcGenerateAesKekOption option, u8 *out_kek)
{
    bool is_device_unique = (option.fields.is_device_unique == 1);
    u8 key_type_idx = option.fields.key_type_idx;
    u8 seal_key_idx = option.fields.seal_key_idx;

    if (!kek_src || key_generation > NcaKeyGeneration_Max || is_device_unique || key_type_idx >= SmcKeyType_Count || seal_key_idx >= SmcSealKey_Count || \
        option.fields.reserved != 0 || !out_kek)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    if (key_generation) key_generation--;

    u8 kekek_src[AES_128_KEY_SIZE] = {0}, kekek[AES_128_KEY_SIZE] = {0};
    const u8 *mkey = g_nxKeyset.master_keys[key_generation];

    /* Make sure this master key is available. */
    if (keysIsKeyEmpty(mkey))
    {
        LOG_MSG_ERROR("Master key %02X unavailable!", key_generation);
        return false;
    }

    /* Derive the KEKEK source using hardcoded data. */
    for(u8 i = 0; i < AES_128_KEY_SIZE; i++) kekek_src[i] = (g_smcKeyTypeSources[key_type_idx][i] ^ g_smcSealKeyMasks[seal_key_idx][i]);

    /* Derive the KEKEK using the KEKEK source and the master key. */
    aes128EcbCrypt(kekek, kekek_src, mkey, false);

    /* Derive the KEK using the provided KEK source and the derived KEKEK. */
    aes128EcbCrypt(out_kek, kek_src, kekek, false);

    return true;
}

/* Based on splCryptoLoadAesKey(). Excludes key sealing shenanigans. */
static bool keysLoadAesKey(const u8 *kek, const u8 *key_src, u8 *out_key)
{
    if (!kek || !key_src || !out_key)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    aes128EcbCrypt(out_key, key_src, kek, false);

    return true;
}

/* Based on splCryptoGenerateAesKey(). Excludes key sealing shenanigans. */
static bool keysGenerateAesKey(const u8 *kek, const u8 *key_src, u8 *out_key)
{
    if (!kek || !key_src || !out_key)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u8 aes_key[AES_128_KEY_SIZE] = {0};

    keysLoadAesKey(kek, g_aesKeyGenerationSource, aes_key);
    aes128EcbCrypt(out_key, key_src, aes_key, false);

    return true;
}

/* Wrapper for keysGenerateAesKek() + keysLoadAesKey() to generate a single usable AES key in one shot. */
static bool keysLoadAesKeyFromAesKek(const u8 *kek_src, u8 key_generation, SmcGenerateAesKekOption option, const u8 *key_src, u8 *out_key)
{
    u8 kek[AES_128_KEY_SIZE] = {0};
    return (keysGenerateAesKek(kek_src, key_generation, option, kek) && keysLoadAesKey(kek, key_src, out_key));
}

/* Wrapper for keysGenerateAesKek() + keysGenerateAesKey() to generate a single usable AES key in one shot. */
static bool keysGenerateAesKeyFromAesKek(const u8 *kek_src, u8 key_generation, SmcGenerateAesKekOption option, const u8 *key_src, u8 *out_key)
{
    u8 kek[AES_128_KEY_SIZE] = {0};
    return (keysGenerateAesKek(kek_src, key_generation, option, kek) && keysGenerateAesKey(kek, key_src, out_key));
}
