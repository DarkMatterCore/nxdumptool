#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "fatfs/ff.h"
#include "keys.h"
#include "util.h"
#include "ui.h"
#include "es.h"
#include "save.h"

/* Extern variables */

extern int breaks;
extern int font_height;

extern u8 *dumpBuf;

extern char strbuf[NAME_BUF_LEN];

/* Statically allocated variables */

nca_keyset_t nca_keyset;

static SetCalRsa2048DeviceKey eticket_data;
static bool setcal_eticket_retrieved = false;

static keyLocation FSRodata = {
    FS_TID,
    SEG_RODATA,
    NULL,
    0
};

static keyLocation FSData = {
    FS_TID,
    SEG_DATA,
    NULL,
    0
};

static const keyInfo header_kek_source = {
    "header_kek_source",
    { 0x18, 0x88, 0xCA, 0xED, 0x55, 0x51, 0xB3, 0xED, 0xE0, 0x14, 0x99, 0xE8, 0x7C, 0xE0, 0xD8, 0x68,
      0x27, 0xF8, 0x08, 0x20, 0xEF, 0xB2, 0x75, 0x92, 0x10, 0x55, 0xAA, 0x4E, 0x2A, 0xBD, 0xFF, 0xC2 },
    0x10
};

static const keyInfo header_key_source = {
    "header_key_source",
    { 0x8F, 0x78, 0x3E, 0x46, 0x85, 0x2D, 0xF6, 0xBE, 0x0B, 0xA4, 0xE1, 0x92, 0x73, 0xC4, 0xAD, 0xBA,
      0xEE, 0x16, 0x38, 0x00, 0x43, 0xE1, 0xB8, 0xC4, 0x18, 0xC4, 0x08, 0x9A, 0x8B, 0xD6, 0x4A, 0xA6 },
    0x20
};

static const keyInfo key_area_key_application_source = {
    "key_area_key_application_source",
    { 0x04, 0xAD, 0x66, 0x14, 0x3C, 0x72, 0x6B, 0x2A, 0x13, 0x9F, 0xB6, 0xB2, 0x11, 0x28, 0xB4, 0x6F,
      0x56, 0xC5, 0x53, 0xB2, 0xB3, 0x88, 0x71, 0x10, 0x30, 0x42, 0x98, 0xD8, 0xD0, 0x09, 0x2D, 0x9E },
    0x10
};

static const keyInfo key_area_key_ocean_source = {
    "key_area_key_ocean_source",
    { 0xFD, 0x43, 0x40, 0x00, 0xC8, 0xFF, 0x2B, 0x26, 0xF8, 0xE9, 0xA9, 0xD2, 0xD2, 0xC1, 0x2F, 0x6B,
      0xE5, 0x77, 0x3C, 0xBB, 0x9D, 0xC8, 0x63, 0x00, 0xE1, 0xBD, 0x99, 0xF8, 0xEA, 0x33, 0xA4, 0x17 },
    0x10
};

static const keyInfo key_area_key_system_source = {
    "key_area_key_system_source",
    { 0x1F, 0x17, 0xB1, 0xFD, 0x51, 0xAD, 0x1C, 0x23, 0x79, 0xB5, 0x8F, 0x15, 0x2C, 0xA4, 0x91, 0x2E,
      0xC2, 0x10, 0x64, 0x41, 0xE5, 0x17, 0x22, 0xF3, 0x87, 0x00, 0xD5, 0x93, 0x7A, 0x11, 0x62, 0xF7 },
    0x10
};

/* Empty string hash */
static const u8 null_hash[0x20] = {
    0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
    0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
};

void freeProcessMemory(keyLocation *location)
{
    if (location && location->data)
    {
        free(location->data);
        location->data = NULL;
    }
}

bool retrieveProcessMemory(keyLocation *location)
{
    if (!location || !location->titleID || !location->mask)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve process memory.", __func__);
        return false;
    }
    
    Result result;
    Handle debug_handle = INVALID_HANDLE;
    
    u64 d[8];
    memset(d, 0, 8 * sizeof(u64));
    
    if ((location->titleID > 0x0100000000000005) && (location->titleID != 0x0100000000000028))
    {
        // If not a kernel process, get PID from pm:dmnt
        u64 pid;
        
        result = pmdmntGetProcessId(&pid, location->titleID);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: pmdmntGetProcessId failed! (0x%08X)", __func__, result);
            return false;
        }
        
        result = svcDebugActiveProcess(&debug_handle, pid);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: svcDebugActiveProcess failed! (0x%08X)", __func__, result);
            return false;
        }
        
        result = svcGetDebugEvent((u8*)&d, debug_handle);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: svcGetDebugEvent failed! (0x%08X)", __func__, result);
            return false;
        }
    } else {
        // Otherwise, query svc for the process list
        u64 pids[300];
        u32 num_processes;
        
        result = svcGetProcessList(&num_processes, pids, 300);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: svcGetProcessList failed! (0x%08X)", __func__, result);
            return false;
        }
        
        u32 i;
        
        for(i = 0; i < (num_processes - 1); i++)
        {
            if (R_SUCCEEDED(svcDebugActiveProcess(&debug_handle, pids[i])) && R_SUCCEEDED(svcGetDebugEvent((u8*)&d, debug_handle)) && (d[2] == location->titleID)) break;
            if (debug_handle) svcCloseHandle(debug_handle);
        }
        
        if (i == (num_processes - 1))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to retrieve debug handle for process with Title ID %016lX!", __func__, location->titleID);
            if (debug_handle) svcCloseHandle(debug_handle);
            return false;
        }
    }

    MemoryInfo mem_info;
    memset(&mem_info, 0, sizeof(MemoryInfo));

    u32 page_info;
    u64 addr = 0;
    u8 segment;
    u64 last_text_addr = 0;
    u8 *dataTmp = NULL;
    
    bool success = true;
    
    // Locate "real" .text segment as Atmosphere emuMMC has two
    for(;;)
    {
        result = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: svcQueryDebugProcessMemory failed! (0x%08X)", __func__, result);
            success = false;
            break;
        }
        
        if ((mem_info.perm & Perm_X) && ((mem_info.type & 0xFF) >= MemType_CodeStatic) && ((mem_info.type & 0xFF) < MemType_Heap)) last_text_addr = mem_info.addr;
        
        addr = (mem_info.addr + mem_info.size);
        if (!addr) break;
    }
    
    if (!success)
    {
        svcCloseHandle(debug_handle);
        return success;
    }
    
    addr = last_text_addr;

    for(segment = 1; segment < BIT(3);)
    {
        result = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: svcQueryDebugProcessMemory failed! (0x%08X)", __func__, result);
            success = false;
            break;
        }
        
        // Weird code to allow for bitmasking segments
        if ((mem_info.perm & Perm_R) && ((mem_info.type & 0xFF) >= MemType_CodeStatic) && ((mem_info.type & 0xFF) < MemType_Heap) && ((segment <<= 1) >> 1 & location->mask) > 0)
        {
            // If location->data == NULL, realloc will essentially act as a malloc
            dataTmp = realloc(location->data, location->dataSize + mem_info.size);
            if (!dataTmp)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to resize key location data buffer to %lu bytes.", __func__, location->dataSize + mem_info.size);
                success = false;
                break;
            }
            
            location->data = dataTmp;
            dataTmp = NULL;
            
            memset(location->data + location->dataSize, 0, mem_info.size);
            
            result = svcReadDebugProcessMemory(location->data + location->dataSize, debug_handle, mem_info.addr, mem_info.size);
            if (R_FAILED(result))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: svcReadDebugProcessMemory failed! (0x%08X)", __func__, result);
                success = false;
                break;
            }
            
            location->dataSize += mem_info.size;
        }
        
        addr = (mem_info.addr + mem_info.size);
        if (addr == 0) break;
    }
    
    svcCloseHandle(debug_handle);
    
    if (success)
    {
        if (!location->data || !location->dataSize) success = false;
    }
    
    return success;
}

bool findKeyInProcessMemory(const keyLocation *location, const keyInfo *findKey, u8 *out)
{
    if (!location || !location->data || !location->dataSize || !findKey || !strlen(findKey->name) || !findKey->size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to locate key in process memory.", __func__);
        return false;
    }
    
    u64 i;
    u8 temp_hash[SHA256_HASH_SIZE];
    bool found = false;
    
    // Hash every key-length-sized byte chunk in data until it matches a key hash
    for(i = 0; i < location->dataSize; i++)
    {
        if (!found && (location->dataSize - i) < findKey->size) break;
        
        sha256CalculateHash(temp_hash, location->data + i, findKey->size);
        
        if (!memcmp(temp_hash, findKey->hash, SHA256_HASH_SIZE))
        {
            // Jackpot
            memcpy(out, location->data + i, findKey->size);
            found = true;
            break;
        }
    }
    
    if (!found) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to locate key \"%s\" in process memory!", __func__, findKey->name);
    
    return found;
}

bool findFSRodataKeys(keyLocation *location)
{
    if (!location || location->titleID != FS_TID || location->mask != SEG_RODATA || !location->data || !location->dataSize)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to locate keys in FS .rodata segment.", __func__);
        return false;
    }
    
    if (!findKeyInProcessMemory(location, &header_kek_source, nca_keyset.header_kek_source)) return false;
    nca_keyset.memory_key_cnt++;
    
    if (!findKeyInProcessMemory(location, &key_area_key_application_source, nca_keyset.key_area_key_application_source)) return false;
    nca_keyset.memory_key_cnt++;
    
    if (!findKeyInProcessMemory(location, &key_area_key_ocean_source, nca_keyset.key_area_key_ocean_source)) return false;
    nca_keyset.memory_key_cnt++;
    
    if (!findKeyInProcessMemory(location, &key_area_key_system_source, nca_keyset.key_area_key_system_source)) return false;
    nca_keyset.memory_key_cnt++;
    
    return true;
}

bool loadMemoryKeys()
{
    if (nca_keyset.memory_key_cnt > 0) return true;
    
    Result result;
    bool proceed;
    
    if (!retrieveProcessMemory(&FSRodata)) return false;
    proceed = findFSRodataKeys(&FSRodata);
    freeProcessMemory(&FSRodata);
    if (!proceed) return false;
    
    if (!retrieveProcessMemory(&FSData)) return false;
    proceed = findKeyInProcessMemory(&FSData, &header_key_source, nca_keyset.header_key_source);
    freeProcessMemory(&FSData);
    if (!proceed) return false;
    nca_keyset.memory_key_cnt++;
    
    // Derive NCA header key
    result = splCryptoInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize the spl:crypto service! (0x%08X)", __func__, result);
        return false;
    }
    
    result = splCryptoGenerateAesKek(nca_keyset.header_kek_source, 0, 0, nca_keyset.header_kek);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splCryptoGenerateAesKek(header_kek_source) failed! (0x%08X)", __func__, result);
        splCryptoExit();
        return false;
    }
    
    nca_keyset.memory_key_cnt++;
    
    result = splCryptoGenerateAesKey(nca_keyset.header_kek, nca_keyset.header_key_source + 0x00, nca_keyset.header_key + 0x00);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splCryptoGenerateAesKey(header_key_source + 0x00) failed! (0x%08X)", __func__, result);
        splCryptoExit();
        return false;
    }
    
    result = splCryptoGenerateAesKey(nca_keyset.header_kek, nca_keyset.header_key_source + 0x10, nca_keyset.header_key + 0x10);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splCryptoGenerateAesKey(header_key_source + 0x10) failed! (0x%08X)", __func__, result);
        splCryptoExit();
        return false;
    }
    
    nca_keyset.memory_key_cnt++;
    
    nca_keyset.total_key_cnt += nca_keyset.memory_key_cnt;
    
    splCryptoExit();
    
    return true;
}

bool decryptNcaKeyArea(nca_header_t *dec_nca_header, u8 *out)
{
    if (!dec_nca_header || dec_nca_header->kaek_ind > 2 || !out)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to decrypt NCA key area.", __func__);
        return false;
    }
    
    Result result;
    
    u8 i;
	u8 tmp_kek[0x10];
    
    u8 crypto_type = (dec_nca_header->crypto_type2 > dec_nca_header->crypto_type ? dec_nca_header->crypto_type2 : dec_nca_header->crypto_type);
    if (crypto_type > 0x20)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA keyblob index!", __func__);
        return false;
    }
    
    u8 *kek_source = (dec_nca_header->kaek_ind == 0 ? nca_keyset.key_area_key_application_source : (dec_nca_header->kaek_ind == 1 ? nca_keyset.key_area_key_ocean_source : nca_keyset.key_area_key_system_source));
    
    result = splCryptoInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize the spl:crypto service! (0x%08X)", __func__, result);
        return false;
    }
    
    result = splCryptoGenerateAesKek(kek_source, crypto_type, 0, tmp_kek);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splCryptoGenerateAesKek(kek_source) failed! (0x%08X)", __func__, result);
        splCryptoExit();
        return false;
    }
    
    bool success = true;
    u8 decrypted_nca_keys[NCA_KEY_AREA_KEY_CNT][NCA_KEY_AREA_KEY_SIZE];
    
    for(i = 0; i < NCA_KEY_AREA_KEY_CNT; i++)
    {
        result = splCryptoGenerateAesKey(tmp_kek, dec_nca_header->nca_keys[i], decrypted_nca_keys[i]);
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splCryptoGenerateAesKey(nca_kaek_%02u) failed! (0x%08X)", __func__, i, result);
            success = false;
            break;
        }
    }
    
    splCryptoExit();
    
    memcpy(out, decrypted_nca_keys, NCA_KEY_AREA_SIZE);
    
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
static int get_kv(FILE *f, char **key, char **value)
{
#define SKIP_SPACE(p) do {\
    for (; (*p == ' ' || *p == '\t'); ++p);\
} while(0);
    
    static char line[512];
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
    
    /* Bail if the final ++p put us at the end of string */
    if (*p == '\0') return -1;
    
    /* We should be at the end of key now and either whitespace or [,=]
     * follows.
     */
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
    
    /* Skip trailing whitespace */
    for (p = end - 1; *p == '\t' || *p == ' '; --p);
    
    *(p + 1) = '\0';
    
    *key = k;
    *value = v;
    
    return 0;
    
#undef SKIP_SPACE
}

static char hextoi(char c)
{
    if ('a' <= c && c <= 'f') return (c - 'a' + 0xA);
    if ('A' <= c && c <= 'F') return (c - 'A' + 0xA);
    if ('0' <= c && c <= '9') return (c - '0');
    return 'z';
}

int parse_hex_key(unsigned char *key, const char *hex, unsigned int len)
{
    u32 i;
    size_t hex_str_len = (2 * len);
    
    if (strlen(hex) != hex_str_len)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: key (%s) must be %u hex digits!", __func__, hex, hex_str_len);
        return 0;
    }
    
    memset(key, 0, len);
    
    for(i = 0; i < hex_str_len; i++)
    {
        char val = hextoi(hex[i]);
        if (val == 'z')
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: key (%s) must be %u hex digits!", __func__, hex, hex_str_len);
            return 0;
        }
        
        if ((i & 1) == 0) val <<= 4;
        key[i >> 1] |= val;
    }
    
    return 1;
}

int readKeysFromFile(FILE *f)
{
    u32 i;
    int ret;
    char *key, *value;
    char test_name[0x100];
    bool common_eticket = false, personalized_eticket = false;
    
    while((ret = get_kv(f, &key, &value)) != 1 && ret != -2)
    {
        if (ret == 0)
        {
            if (key == NULL || value == NULL) continue;
            
            if (!common_eticket && !personalized_eticket && strcasecmp(key, "eticket_rsa_kek") == 0)
            {
                if (!parse_hex_key(nca_keyset.eticket_rsa_kek, value, sizeof(nca_keyset.eticket_rsa_kek))) return 0;
                nca_keyset.ext_key_cnt++;
                common_eticket = true;
            } else
            if (!personalized_eticket && strcasecmp(key, "eticket_rsa_kek_personalized") == 0)
            {
                /* Use the personalized eTicket RSA kek if available */
                /* This only appears on consoles that use the new PRODINFO key generation scheme */
                if (!parse_hex_key(nca_keyset.eticket_rsa_kek, value, sizeof(nca_keyset.eticket_rsa_kek))) return 0;
                if (!common_eticket) nca_keyset.ext_key_cnt++;
                personalized_eticket = true;
            } else {
                memset(test_name, 0, sizeof(test_name));
                
                for(i = 0; i < 0x20; i++)
                {
                    snprintf(test_name, sizeof(test_name), "titlekek_%02x", i);
                    if (strcasecmp(key, test_name) == 0)
                    {
                        if (!parse_hex_key(nca_keyset.titlekeks[i], value, sizeof(nca_keyset.titlekeks[i]))) return 0;
                        nca_keyset.ext_key_cnt++;
                        break;
                    }
                    
                    snprintf(test_name, sizeof(test_name), "key_area_key_application_%02x", i);
                    if (strcasecmp(key, test_name) == 0)
                    {
                        if (!parse_hex_key(nca_keyset.key_area_keys[i][0], value, sizeof(nca_keyset.key_area_keys[i][0]))) return 0;
                        nca_keyset.ext_key_cnt++;
                        break;
                    }
                    
                    snprintf(test_name, sizeof(test_name), "key_area_key_ocean_%02x", i);
                    if (strcasecmp(key, test_name) == 0)
                    {
                        if (!parse_hex_key(nca_keyset.key_area_keys[i][1], value, sizeof(nca_keyset.key_area_keys[i][1]))) return 0;
                        nca_keyset.ext_key_cnt++;
                        break;
                    }
                    
                    snprintf(test_name, sizeof(test_name), "key_area_key_system_%02x", i);
                    if (strcasecmp(key, test_name) == 0)
                    {
                        if (!parse_hex_key(nca_keyset.key_area_keys[i][2], value, sizeof(nca_keyset.key_area_keys[i][2]))) return 0;
                        nca_keyset.ext_key_cnt++;
                        break;
                    }
                }
            }
        }
    }
    
    if (!nca_keyset.ext_key_cnt) return -1;
    
    nca_keyset.total_key_cnt += nca_keyset.ext_key_cnt;
    
    return 1;
}

bool loadExternalKeys()
{
    // Check if the keyset has been already loaded
    if (nca_keyset.ext_key_cnt > 0) return true;
    
    // Open keys file
    FILE *keysFile = fopen(KEYS_FILE_PATH, "rb");
    if (!keysFile)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to open \"%s\" to retrieve \"eticket_rsa_kek\", titlekeks and KAEKs!", __func__, KEYS_FILE_PATH);
        return false;
    }
    
    // Load keys
    int ret = readKeysFromFile(keysFile);
    fclose(keysFile);
    
    if (ret < 1)
    {
        if (ret == -1) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to parse necessary keys from \"%s\"! (keys file empty?)", __func__, KEYS_FILE_PATH);
        return false;
    }
    
    return true;
}

bool testKeyPair(const void *E, const void *D, const void *N)
{
    if (!E || !D || !N)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to test RSA key pair.", __func__);
        return false;
    }
    
    Result result;
    u8 X[0x100] = {0}, Y[0x100] = {0}, Z[0x100] = {0};
    size_t i;
    
    // 0xCAFEBABE
    X[0xFC] = 0xCA;
    X[0xFD] = 0xFE;
    X[0xFE] = 0xBA;
    X[0xFF] = 0xBE;
    
    result = splUserExpMod(X, N, D, 0x100, Y);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splUserExpMod failed! (testKeyPair #1) (0x%08X)", __func__, result);
        return false;
    }
    
    result = splUserExpMod(Y, N, E, 4, Z);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splUserExpMod failed! (testKeyPair #2) (0x%08X)", __func__, result);
        return false;
    }
    
    for(i = 0; i < 0x100; i++)
    {
        if (X[i] != Z[i])
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid RSA key pair!", __func__);
            return false;
        }
    }
    
    return true;
}

void mgf1(const u8 *data, size_t data_length, u8 *mask, size_t mask_length)
{
    if (!data || !data_length || !mask || !mask_length) return;
    
    u8 *data_counter = calloc(data_length + 4, sizeof(u8));
    if (!data_counter) return;
    
    memcpy(data_counter, data, data_length);
    
    sha256CalculateHash(mask, data_counter, data_length + 4);
    
    u32 i, j;
    for(i = 1; i < ((mask_length / 0x20) + 1); i++)
    {
        for(j = 0; j < 4; j++) data_counter[data_length + 3 - j] = ((i >> (8 * j)) & 0xFF);
        
        if ((i * 0x20) <= mask_length)
        {
            sha256CalculateHash(mask + (i * 0x20), data_counter, data_length + 4);
        } else {
            u8 temp_mask[0x20];
            sha256CalculateHash(temp_mask, data_counter, data_length + 4);
            memcpy(mask + (i * 0x20), temp_mask, mask_length - (i * 0x20));
        }
    }
    
    free(data_counter);
}

int retrieveNcaTikTitleKey(nca_header_t *dec_nca_header, u8 *out_tik, u8 *out_enc_key, u8 *out_dec_key)
{
    int ret = -1;
    
    if (!dec_nca_header || dec_nca_header->kaek_ind > 2 || (!out_tik && !out_dec_key && !out_enc_key))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve NCA ticket and/or titlekey.", __func__);
        return ret;
    }
    
    u32 i, j;
    bool has_rights_id = false;
    
    for(i = 0; i < 0x10; i++)
    {
        if (dec_nca_header->rights_id[i] != 0)
        {
            has_rights_id = true;
            break;
        }
    }
    
    if (!has_rights_id)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: NCA doesn't use titlekey crypto.", __func__);
        return ret;
    }
    
    u8 crypto_type = (dec_nca_header->crypto_type2 > dec_nca_header->crypto_type ? dec_nca_header->crypto_type2 : dec_nca_header->crypto_type);
    if (crypto_type) crypto_type--;
    
    if (crypto_type >= 0x20)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA keyblob index.", __func__);
        return ret;
    }
    
    Result result;
    u32 common_count, personalized_count, ids_written;
    FsRightsId *common_rights_ids = NULL, *personalized_rights_ids = NULL;
    
    bool foundRightsId = false;
    u8 rightsIdType = 0; // 1 = Common, 2 = Personalized
    
    Aes128CtrContext eticket_aes_ctx;
    unsigned char ctr[0x10];
    
    u8 *D = NULL, *N = NULL, *E = NULL;
    
    FRESULT fr;
    FIL eTicketSave;
    
    save_ctx_t *save_ctx = NULL;
    allocation_table_storage_ctx_t fat_storage;
    save_fs_list_entry_t entry;
    const char ticket_bin_path[SAVE_FS_LIST_MAX_NAME_LENGTH] = "/ticket.bin";
    
    u32 buf_size = (ETICKET_ENTRY_SIZE * 0x10);
    u32 br = buf_size;
    u64 total_br = 0;
    
    char tmp[NAME_BUF_LEN / 2] = {'\0'};
    
    bool foundEticket = false, proceed = true;
    
    u8 titlekey[0x10];
    Aes128Context titlekey_aes_ctx;
    
    result = esInitialize();
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize the ES service! (0x%08X)", __func__, result);
        return ret;
    }
    
    result = esCountCommonTicket(&common_count);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: esCountCommonTicket failed! (0x%08X)", __func__, result);
        esExit();
        return ret;
    }
    
    result = esCountPersonalizedTicket(&personalized_count);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: esCountPersonalizedTicket failed! (0x%08X)", __func__, result);
        esExit();
        return ret;
    }
    
    if (!common_count && !personalized_count)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: no tickets available!", __func__);
        esExit();
        return ret;
    }
    
    if (common_count)
    {
        common_rights_ids = calloc(common_count, sizeof(FsRightsId));
        if (!common_rights_ids)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for common tickets' rights IDs!", __func__);
            esExit();
            return ret;
        }
        
        result = esListCommonTicket(&ids_written, common_rights_ids, common_count * sizeof(FsRightsId));
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: esListCommonTicket failed! (0x%08X)", __func__, result);
            free(common_rights_ids);
            esExit();
            return ret;
        }
        
        for(i = 0; i < common_count; i++)
        {
            if (!memcmp(common_rights_ids[i].c, dec_nca_header->rights_id, 0x10))
            {
                foundRightsId = true;
                rightsIdType = 1; // Common
                break;
            }
        }
        
        free(common_rights_ids);
    }
    
    if (!foundRightsId && personalized_count)
    {
        personalized_rights_ids = calloc(personalized_count, sizeof(FsRightsId));
        if (!personalized_rights_ids)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for personalized tickets' rights IDs!", __func__);
            esExit();
            return ret;
        }
        
        result = esListPersonalizedTicket(&ids_written, personalized_rights_ids, personalized_count * sizeof(FsRightsId));
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: esListPersonalizedTicket failed! (0x%08X)", __func__, result);
            free(personalized_rights_ids);
            esExit();
            return ret;
        }
        
        for(i = 0; i < personalized_count; i++)
        {
            if (!memcmp(personalized_rights_ids[i].c, dec_nca_header->rights_id, 0x10))
            {
                foundRightsId = true;
                rightsIdType = 2; // Personalized
                break;
            }
        }
        
        free(personalized_rights_ids);
    }
    
    esExit();
    
    if (!foundRightsId || (rightsIdType != 1 && rightsIdType != 2))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: NCA rights ID unavailable in this console!", __func__);
        ret = -2;
        dumpSharedTikSavedata();
        return ret;
    }
    
    // Load external keys
    if (!loadExternalKeys()) return ret;
    
    if (!setcal_eticket_retrieved)
    {
        // Get extended eTicket RSA key from PRODINFO
        memset(&eticket_data, 0, sizeof(SetCalRsa2048DeviceKey));
        
        result = setcalInitialize();
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to initialize the set:cal service! (0x%08X)", __func__, result);
            return ret;
        }
        
        result = setcalGetEticketDeviceKey(&eticket_data);
        
        setcalExit();
        
        if (R_FAILED(result))
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: setcalGetEticketDeviceKey failed! (0x%08X)", __func__, result);
            return ret;
        }
        
        // Decrypt eTicket RSA key
        memcpy(ctr, eticket_data.key, ETICKET_DEVKEY_RSA_CTR_SIZE);
        aes128CtrContextCreate(&eticket_aes_ctx, nca_keyset.eticket_rsa_kek, ctr);
        aes128CtrCrypt(&eticket_aes_ctx, eticket_data.key + ETICKET_DEVKEY_RSA_OFFSET, eticket_data.key + ETICKET_DEVKEY_RSA_OFFSET, ETICKET_DEVKEY_RSA_SIZE);
        
        // Public exponent must use RSA-2048 SHA-1 signature method
        // The value is stored use big endian byte order
        if (__builtin_bswap32(*((u32*)(eticket_data.key + ETICKET_DEVKEY_RSA_OFFSET + 0x200))) != SIGTYPE_RSA2048_SHA1)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid public RSA exponent for eTicket data! Wrong keys?\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__);
            return ret;
        }
    }
    
    D = (eticket_data.key + ETICKET_DEVKEY_RSA_OFFSET);
    N = (eticket_data.key + ETICKET_DEVKEY_RSA_OFFSET + 0x100);
    E = (eticket_data.key + ETICKET_DEVKEY_RSA_OFFSET + 0x200);
    
    if (!setcal_eticket_retrieved)
    {
        if (!testKeyPair(E, D, N)) return ret;
        setcal_eticket_retrieved = true;
    }
    
    // FatFs is used to mount the BIS System partition and read the ES savedata files to avoid 0xE02 (file already in use) errors
    fr = f_open(&eTicketSave, (rightsIdType == 1 ? BIS_COMMON_TIK_SAVE_NAME : BIS_PERSONALIZED_TIK_SAVE_NAME), FA_READ | FA_OPEN_EXISTING);
    if (fr)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to open ES %s eTicket save! (%u)", __func__, (rightsIdType == 1 ? "common" : "personalized"), fr);
        return ret;
    }
    
    save_ctx = calloc(1, sizeof(save_ctx_t));
    if (!save_ctx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to allocate memory for ticket savefile context!");
        f_close(&eTicketSave);
        return ret;
    }
    
    save_ctx->file = &eTicketSave;
    save_ctx->tool_ctx.action = 0;
    
    if (!save_process(save_ctx))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to process ticket savefile!", __func__);
        strcat(strbuf, tmp);
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
        free(save_ctx);
        f_close(&eTicketSave);
        return ret;
    }
    
    if (!save_hierarchical_file_table_get_file_entry_by_path(&save_ctx->save_filesystem_core.file_table, ticket_bin_path, &entry))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to get file entry for \"%s\" in ticket savefile!", __func__, ticket_bin_path);
        strcat(strbuf, tmp);
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
        save_free_contexts(save_ctx);
        free(save_ctx);
        f_close(&eTicketSave);
        return ret;
    }
    
    if (!save_open_fat_storage(&save_ctx->save_filesystem_core, &fat_storage, entry.value.save_file_info.start_block))
    {
        snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to open FAT storage at block 0x%X for \"%s\" in ticket savefile!", __func__, entry.value.save_file_info.start_block, ticket_bin_path);
        strcat(strbuf, tmp);
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
        save_free_contexts(save_ctx);
        free(save_ctx);
        f_close(&eTicketSave);
        return ret;
    }
    
    while(br == buf_size && total_br < entry.value.save_file_info.length)
    {
        br = save_allocation_table_storage_read(&fat_storage, dumpBuf, total_br, buf_size);
        if (br != buf_size)
        {
            snprintf(tmp, MAX_CHARACTERS(tmp), "\n%s: failed to read %u bytes chunk at offset 0x%lX from \"%s\" in ticket savefile!", __func__, buf_size, total_br, ticket_bin_path);
            strcat(strbuf, tmp);
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, strbuf);
            proceed = false;
            break;
        }
        
        if (dumpBuf[0] == 0) break;
        
        total_br += br;
        
        for(i = 0; i < buf_size; i += ETICKET_ENTRY_SIZE)
        {
            // Only read eTicket entries with RSA-2048 SHA-256 signature method
            // Also check if our current eTicket entry matches our rights ID
            if (*((u32*)(dumpBuf + i)) != SIGTYPE_RSA2048_SHA256 || memcmp(dumpBuf + i + ETICKET_RIGHTSID_OFFSET, dec_nca_header->rights_id, 0x10) != 0) continue;
            
            foundEticket = true;
            
            if (rightsIdType == 1)
            {
                // Common
                memcpy(titlekey, dumpBuf + i + ETICKET_TITLEKEY_OFFSET, 0x10);
            } else {
                // Personalized
                u8 M[0x100], salt[0x20], db[0xDF];
                
                u8 *titleKeyBlock = (dumpBuf + i + ETICKET_TITLEKEY_OFFSET);
                
                result = splUserExpMod(titleKeyBlock, N, D, 0x100, M);
                if (R_FAILED(result))
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: splUserExpMod failed! (titleKeyBlock) (0x%08X)", __func__, result);
                    proceed = false;
                    break;
                }
                
                // Decrypt the titlekey
                mgf1(M + 0x21, 0xDF, salt, 0x20);
                for(j = 0; j < 0x20; j++) salt[j] ^= M[j + 1];
                
                mgf1(salt, 0x20, db, 0xDF);
                for(j = 0; j < 0xDF; j++) db[j] ^= M[j + 0x21];
                
                // Verify if it starts with a null string hash
                if (memcmp(db, null_hash, 0x20) != 0)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: titlekey decryption failed! Wrong keys?\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__);
                    proceed = false;
                    break;
                }
                
                memcpy(titlekey, db + 0xCF, 0x10);
            }
            
            break;
        }
        
        if (foundEticket) break;
    }
    
    save_free_contexts(save_ctx);
    free(save_ctx);
    f_close(&eTicketSave);
    
    if (!proceed) return ret;
    
    if (!foundEticket)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find a matching eTicket entry for NCA rights ID!", __func__);
        ret = -2;
        dumpSharedTikSavedata();
        return ret;
    }
    
    ret = 0;
    
    // Copy ticket data to output pointer
    if (out_tik != NULL) memcpy(out_tik, dumpBuf + i, ETICKET_TIK_FILE_SIZE);
    
    // Copy encrypted titlekey to output pointer
    // It is used in personalized -> common ticket conversion
    if (out_enc_key != NULL) memcpy(out_enc_key, titlekey, 0x10);
    
    // Generate decrypted titlekey ready to use for section decryption
    // It is also used in ticket-less dumps as the NCA key area slot #2 key (before encryption)
    if (out_dec_key != NULL)
    {
        aes128ContextCreate(&titlekey_aes_ctx, nca_keyset.titlekeks[crypto_type], false);
        aes128DecryptBlock(&titlekey_aes_ctx, out_dec_key, titlekey);
    }
    
    return ret;
}

bool generateEncryptedNcaKeyAreaWithTitlekey(nca_header_t *dec_nca_header, u8 *decrypted_nca_keys)
{
    if (!dec_nca_header || dec_nca_header->kaek_ind > 2 || !decrypted_nca_keys || !nca_keyset.ext_key_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to generate encrypted NCA key area using titlekey!", __func__);
        return false;
    }
    
    u8 i;
    Aes128Context key_area_ctx;
    
    u8 crypto_type = (dec_nca_header->crypto_type2 > dec_nca_header->crypto_type ? dec_nca_header->crypto_type2 : dec_nca_header->crypto_type);
    if (crypto_type) crypto_type--;
    
    if (crypto_type >= 0x20)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA keyblob index.", __func__);
        return false;
    }
    
    aes128ContextCreate(&key_area_ctx, nca_keyset.key_area_keys[crypto_type][dec_nca_header->kaek_ind], true);
    
    for(i = 0; i < NCA_KEY_AREA_KEY_CNT; i++) aes128EncryptBlock(&key_area_ctx, dec_nca_header->nca_keys[i], decrypted_nca_keys + (i * NCA_KEY_AREA_KEY_SIZE));
    
    return true;
}
