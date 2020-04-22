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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "keys.h"
#include "nca.h"
#include "utils.h"

#define KEYS_FILE_PATH      "sdmc:/switch/prod.keys"    /* Location used by Lockpick_RCM */

#define FS_SYSMODULE_TID    (u64)0x0100000000000000

#define SEGMENT_TEXT        BIT(0)
#define SEGMENT_RODATA      BIT(1)
#define SEGMENT_DATA        BIT(2)

/* Type definitions. */

typedef struct {
    u64 program_id;
    u8 mask;
    u8 *data;
    u64 data_size;
} keysMemoryLocation;

typedef struct {
    char name[64];
    u8 hash[SHA256_HASH_SIZE];
    u64 size;
    void *dst;
} keysMemoryKey;

typedef struct {
    keysMemoryLocation location;
    u32 key_count;
    keysMemoryKey keys[];
} keysMemoryInfo;

typedef struct {
    ///< Needed to decrypt the NCA header using AES-128-XTS
    u8 header_kek_source[0x10];                 ///< Seed for header kek. Retrieved from the .rodata section in the FS sysmodule.
    u8 header_key_source[0x20];                 ///< Seed for NCA header key. Retrieved from the .data section in the FS sysmodule.
    u8 header_kek[0x10];                        ///< NCA header kek. Generated from header_kek_source.
    u8 header_key[0x20];                        ///< NCA header key. Generated from header_kek and header_key_source.
    
    ///< Needed to derive the KAEK used to decrypt the NCA key area
    u8 key_area_key_application_source[0x10];   ///< Seed for kaek 0. Retrieved from the .rodata section in the FS sysmodule.
    u8 key_area_key_ocean_source[0x10];         ///< Seed for kaek 1. Retrieved from the .rodata section in the FS sysmodule.
    u8 key_area_key_system_source[0x10];        ///< Seed for kaek 2. Retrieved from the .rodata section in the FS sysmodule.
    
    ///< Needed to decrypt the titlekey block from a ticker. Retrieved from the Lockpick_RCM keys file.
    u8 eticket_rsa_kek[0x10];                   ///< eTicket RSA kek.
    u8 titlekeks[0x20][0x10];                   ///< Titlekey encryption keys.
    
    ///< Needed to reencrypt the NCA key area for tik-less NSP dumps. Retrieved from the Lockpick_RCM keys file.
    u8 key_area_keys[0x20][3][0x10];            ///< Key area encryption keys.
} keysNcaKeyset;

/* Global variables. */

static keysNcaKeyset g_ncaKeyset = {0};

static keysMemoryInfo g_fsRodataMemoryInfo = {
    .location = {
        .program_id = FS_SYSMODULE_TID,
        .mask = SEGMENT_RODATA,
        .data = NULL,
        .data_size = 0
    },
    .key_count = 4,
    .keys = {
        {
            .name = "header_kek_source",
            .hash = { 0x18, 0x88, 0xCA, 0xED, 0x55, 0x51, 0xB3, 0xED, 0xE0, 0x14, 0x99, 0xE8, 0x7C, 0xE0, 0xD8, 0x68,
                      0x27, 0xF8, 0x08, 0x20, 0xEF, 0xB2, 0x75, 0x92, 0x10, 0x55, 0xAA, 0x4E, 0x2A, 0xBD, 0xFF, 0xC2 },
            .size = 0x10,
            .dst = g_ncaKeyset.header_kek_source
        },
        {
            .name = "key_area_key_application_source",
            .hash = { 0x04, 0xAD, 0x66, 0x14, 0x3C, 0x72, 0x6B, 0x2A, 0x13, 0x9F, 0xB6, 0xB2, 0x11, 0x28, 0xB4, 0x6F,
                      0x56, 0xC5, 0x53, 0xB2, 0xB3, 0x88, 0x71, 0x10, 0x30, 0x42, 0x98, 0xD8, 0xD0, 0x09, 0x2D, 0x9E },
            .size = 0x10,
            .dst = g_ncaKeyset.key_area_key_application_source
        },
        {
            .name = "key_area_key_ocean_source",
            .hash = { 0xFD, 0x43, 0x40, 0x00, 0xC8, 0xFF, 0x2B, 0x26, 0xF8, 0xE9, 0xA9, 0xD2, 0xD2, 0xC1, 0x2F, 0x6B,
                      0xE5, 0x77, 0x3C, 0xBB, 0x9D, 0xC8, 0x63, 0x00, 0xE1, 0xBD, 0x99, 0xF8, 0xEA, 0x33, 0xA4, 0x17 },
            .size = 0x10,
            .dst = g_ncaKeyset.key_area_key_ocean_source
        },
        {
            .name = "key_area_key_system_source",
            .hash = { 0x1F, 0x17, 0xB1, 0xFD, 0x51, 0xAD, 0x1C, 0x23, 0x79, 0xB5, 0x8F, 0x15, 0x2C, 0xA4, 0x91, 0x2E,
                      0xC2, 0x10, 0x64, 0x41, 0xE5, 0x17, 0x22, 0xF3, 0x87, 0x00, 0xD5, 0x93, 0x7A, 0x11, 0x62, 0xF7 },
            .size = 0x10,
            .dst = g_ncaKeyset.key_area_key_system_source
        }
    }
};

static keysMemoryInfo g_fsDataMemoryInfo = {
    .location = {
        .program_id = FS_SYSMODULE_TID,
        .mask = SEGMENT_DATA,
        .data = NULL,
        .data_size = 0
    },
    .key_count = 1,
    .keys = {
        {
            .name = "header_key_source",
            .hash = { 0x8F, 0x78, 0x3E, 0x46, 0x85, 0x2D, 0xF6, 0xBE, 0x0B, 0xA4, 0xE1, 0x92, 0x73, 0xC4, 0xAD, 0xBA,
                      0xEE, 0x16, 0x38, 0x00, 0x43, 0xE1, 0xB8, 0xC4, 0x18, 0xC4, 0x08, 0x9A, 0x8B, 0xD6, 0x4A, 0xA6 },
            .size = 0x20,
            .dst = g_ncaKeyset.header_key_source
        }
    }
};

/* Function prototypes. */

static bool keysRetrieveDebugHandleFromProcessByProgramId(Handle *out, u64 program_id);
static bool keysRetrieveProcessMemory(keysMemoryLocation *location);
static void keysFreeProcessMemory(keysMemoryLocation *location);
static bool keysRetrieveKeysFromProcessMemory(keysMemoryInfo *info);
static bool keysDeriveNcaHeaderKey(void);
static int keysGetKeyAndValueFromFile(FILE *f, char **key, char **value);
static char keysConvertHexCharToBinary(char c);
static bool keysParseHexKey(u8 *out, const char *key, const char *value, u32 size);
static bool keysReadKeysFromFile(void);

bool keysLoadNcaKeyset(void)
{
    if (!(envIsSyscallHinted(0x60) &&   /* svcDebugActiveProcess */
          envIsSyscallHinted(0x63) &&   /* svcGetDebugEvent */
          envIsSyscallHinted(0x65) &&   /* svcGetProcessList */
          envIsSyscallHinted(0x69) &&   /* svcQueryDebugProcessMemory */
          envIsSyscallHinted(0x6A)))    /* svcReadDebugProcessMemory */
    {
        LOGFILE("Debug SVC permissions not available!");
        return false;
    }
    
    if (!keysRetrieveKeysFromProcessMemory(&g_fsRodataMemoryInfo))
    {
        LOGFILE("Unable to retrieve keys from FS .rodata section!");
        return false;
    }
    
    if (!keysRetrieveKeysFromProcessMemory(&g_fsDataMemoryInfo))
    {
        LOGFILE("Unable to retrieve keys from FS .data section!");
        return false;
    }
    
    if (!keysDeriveNcaHeaderKey())
    {
        LOGFILE("Unable to derive NCA header key!");
        return false;
    }
    
    if (!keysReadKeysFromFile()) return false;
    
    return true;
}

const u8 *keysGetNcaHeaderKey(void)
{
    return (const u8*)(g_ncaKeyset.header_key);
}

const u8 *keysGetKeyAreaEncryptionKeySource(u8 kaek_index)
{
    const u8 *ptr = NULL;
    
    switch(kaek_index)
    {
        case NcaKeyAreaEncryptionKeyIndex_Application:
            ptr = (const u8*)(g_ncaKeyset.key_area_key_application_source);
            break;
        case NcaKeyAreaEncryptionKeyIndex_Ocean:
            ptr = (const u8*)(g_ncaKeyset.key_area_key_ocean_source);
            break;
        case NcaKeyAreaEncryptionKeyIndex_System:
            ptr = (const u8*)(g_ncaKeyset.key_area_key_system_source);
            break;
        default:
            LOGFILE("Invalid KAEK index! (0x%02X)", kaek_index);
            break;
    }
    
    return ptr;
}

const u8 *keysGetEticketRsaKek(void)
{
    return (const u8*)(g_ncaKeyset.eticket_rsa_kek);
}

const u8 *keysGetTitlekek(u8 key_generation)
{
    if (key_generation > 0x20)
    {
        LOGFILE("Invalid key generation value! (0x%02X)", key_generation);
        return NULL;
    }
    
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);
    
    return (const u8*)(g_ncaKeyset.titlekeks[key_gen_val]);
}

const u8 *keysGetKeyAreaEncryptionKey(u8 key_generation, u8 kaek_index)
{
    if (key_generation > 0x20)
    {
        LOGFILE("Invalid key generation value! (0x%02X)", key_generation);
        return NULL;
    }
    
    if (kaek_index > NcaKeyAreaEncryptionKeyIndex_System)
    {
        LOGFILE("Invalid KAEK index! (0x%02X)", kaek_index);
        return NULL;
    }
    
    u8 key_gen_val = (key_generation ? (key_generation - 1) : key_generation);
    
    return (const u8*)(g_ncaKeyset.key_area_keys[key_gen_val][kaek_index]);
}

static bool keysRetrieveDebugHandleFromProcessByProgramId(Handle *out, u64 program_id)
{
    if (!out || !program_id)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 d[8] = {0};
    Handle debug_handle = INVALID_HANDLE;
    
    if (program_id > 0x0100000000000005 && program_id != 0x0100000000000028)
    {
        /* If not a kernel process, get PID from pm:dmnt */
        u64 pid;
        
        rc = pmdmntGetProcessId(&pid, program_id);
        if (R_FAILED(rc))
        {
            LOGFILE("pmdmntGetProcessId failed! (0x%08X)", rc);
            return false;
        }
        
        rc = svcDebugActiveProcess(&debug_handle, pid);
        if (R_FAILED(rc))
        {
            LOGFILE("svcDebugActiveProcess failed! (0x%08X)", rc);
            return false;
        }
        
        rc = svcGetDebugEvent((u8*)&d, debug_handle);
        if (R_FAILED(rc))
        {
            LOGFILE("svcGetDebugEvent failed! (0x%08X)", rc);
            return false;
        }
    } else {
        /* Otherwise, query svc for the process list */
        u32 i, num_processes = 0;
        
        u64 *pids = calloc(300, sizeof(u64));
        if (!pids)
        {
            LOGFILE("Failed to allocate memory for PID list!");
            return false;
        }
        
        rc = svcGetProcessList((s32*)&num_processes, pids, 300);
        if (R_FAILED(rc))
        {
            LOGFILE("svcGetProcessList failed! (0x%08X)", rc);
            return false;
        }
        
        for(i = 0; i < (num_processes - 1); i++)
        {
            rc = svcDebugActiveProcess(&debug_handle, pids[i]);
            if (R_FAILED(rc)) continue;
            
            rc = svcGetDebugEvent((u8*)&d, debug_handle);
            if (R_SUCCEEDED(rc) && d[2] == program_id) break;
            
            svcCloseHandle(debug_handle);
            debug_handle = INVALID_HANDLE;
        }
        
        free(pids);
        
        if (i == (num_processes - 1))
        {
            LOGFILE("Kernel process lookup failed! (0x%08X)", rc);
            return false;
        }
    }
    
    *out = debug_handle;
    
    return true;
}

static bool keysRetrieveProcessMemory(keysMemoryLocation *location)
{
    if (!location || !location->program_id || !location->mask)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    Handle debug_handle = INVALID_HANDLE;
    
    MemoryInfo mem_info = {0};
    
    u32 page_info = 0;
    u64 addr = 0, last_text_addr = 0;
    u8 segment = 0;
    u8 *tmp = NULL;
    
    bool success = true;
    
    if (!keysRetrieveDebugHandleFromProcessByProgramId(&debug_handle, location->program_id))
    {
        LOGFILE("Unable to retrieve debug handle for program %016lX!", location->program_id);
        return false;
    }
    
    /* Locate "real" .text segment as Atmosphere emuMMC has two */
    for(;;)
    {
        rc = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
        if (R_FAILED(rc))
        {
            LOGFILE("svcQueryDebugProcessMemory failed! (0x%08X)", rc);
            success = false;
            goto out;
        }
        
        if ((mem_info.perm & Perm_X) && ((mem_info.type & 0xFF) >= MemType_CodeStatic) && ((mem_info.type & 0xFF) < MemType_Heap)) last_text_addr = mem_info.addr;
        
        addr = (mem_info.addr + mem_info.size);
        if (!addr) break;
    }
    
    addr = last_text_addr;

    for(segment = 1; segment < BIT(3);)
    {
        rc = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
        if (R_FAILED(rc))
        {
            LOGFILE("svcQueryDebugProcessMemory failed! (0x%08X)", rc);
            success = false;
            break;
        }
        
        /* Code to allow for bitmasking segments */
        if ((mem_info.perm & Perm_R) && ((mem_info.type & 0xFF) >= MemType_CodeStatic) && ((mem_info.type & 0xFF) < MemType_Heap) && ((segment <<= 1) >> 1 & location->mask) > 0)
        {
            /* If location->data == NULL, realloc will essentially act as a malloc */
            tmp = realloc(location->data, location->data_size + mem_info.size);
            if (!tmp)
            {
                LOGFILE("Failed to resize key location data buffer to 0x%lX bytes.", location->data_size + mem_info.size);
                success = false;
                break;
            }
            
            location->data = tmp;
            tmp = NULL;
            
            rc = svcReadDebugProcessMemory(location->data + location->data_size, debug_handle, mem_info.addr, mem_info.size);
            if (R_FAILED(rc))
            {
                LOGFILE("svcReadDebugProcessMemory failed! (0x%08X)", rc);
                success = false;
                break;
            }
            
            location->data_size += mem_info.size;
        }
        
        addr = (mem_info.addr + mem_info.size);
        if (addr == 0) break;
    }
    
out:
    svcCloseHandle(debug_handle);
    
    if (success && (!location->data || !location->data_size)) success = false;
    
    return success;
}

static void keysFreeProcessMemory(keysMemoryLocation *location)
{
    if (location && location->data)
    {
        free(location->data);
        location->data = NULL;
    }
}

static bool keysRetrieveKeysFromProcessMemory(keysMemoryInfo *info)
{
    if (!info || !info->key_count)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    bool found;
    u8 tmp_hash[SHA256_HASH_SIZE];
    bool success = false;
    
    if (!keysRetrieveProcessMemory(&(info->location)))
    {
        LOGFILE("Unable to retrieve process memory from program %016lX!", info->location.program_id);
        return false;
    }
    
    for(u32 i = 0; i < info->key_count; i++)
    {
        found = false;
        
        if (!info->keys[i].dst)
        {
            LOGFILE("Invalid destination pointer for key \"%s\" in program %016lX!", info->keys[i].name, info->location.program_id);
            goto out;
        }
        
        /* Hash every key length-sized byte chunk in the process memory buffer until a match is found */
        for(u64 j = 0; j < info->location.data_size; j++)
        {
            if ((info->location.data_size - j) < info->keys[i].size) break;
            
            sha256CalculateHash(tmp_hash, info->location.data + j, info->keys[i].size);
            
            if (!memcmp(tmp_hash, info->keys[i].hash, SHA256_HASH_SIZE))
            {
                /* Jackpot */
                memcpy(info->keys[i].dst, info->location.data + j, info->keys[i].size);
                found = true;
                break;
            }
        }
        
        if (!found)
        {
            LOGFILE("Unable to locate key \"%s\" in process memory from program %016lX!", info->keys[i].name, info->location.program_id);
            goto out;
        }
    }
    
    success = true;
    
out:
    keysFreeProcessMemory(&(info->location));
    
    return success;
}

static bool keysDeriveNcaHeaderKey(void)
{
    Result rc = 0;
    
    rc = splCryptoGenerateAesKek(g_ncaKeyset.header_kek_source, 0, 0, g_ncaKeyset.header_kek);
    if (R_FAILED(rc))
    {
        LOGFILE("splCryptoGenerateAesKek(header_kek_source) failed! (0x%08X)", rc);
        return false;
    }
    
    rc = splCryptoGenerateAesKey(g_ncaKeyset.header_kek, g_ncaKeyset.header_key_source + 0x00, g_ncaKeyset.header_key + 0x00);
    if (R_FAILED(rc))
    {
        LOGFILE("splCryptoGenerateAesKey(header_key_source + 0x00) failed! (0x%08X)", rc);
        return false;
    }
    
    rc = splCryptoGenerateAesKey(g_ncaKeyset.header_kek, g_ncaKeyset.header_key_source + 0x10, g_ncaKeyset.header_key + 0x10);
    if (R_FAILED(rc))
    {
        LOGFILE("splCryptoGenerateAesKey(header_key_source + 0x10) failed! (0x%08X)", rc);
        return false;
    }
    
    return true;
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
        LOGFILE("Invalid parameters!");
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

static char keysConvertHexCharToBinary(char c)
{
    if ('a' <= c && c <= 'f') return (c - 'a' + 0xA);
    if ('A' <= c && c <= 'F') return (c - 'A' + 0xA);
    if ('0' <= c && c <= '9') return (c - '0');
    return 'z';
}

static bool keysParseHexKey(u8 *out, const char *key, const char *value, u32 size)
{
    if (!out || !key || !strlen(key) || !value || !strlen(value) || !size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 hex_str_len = (2 * size);
    
    if (strlen(value) != hex_str_len)
    {
        LOGFILE("Key \"%s\" must be %u hex digits long!", key, hex_str_len);
        return false;
    }
    
    memset(out, 0, size);
    
    for(u32 i = 0; i < hex_str_len; i++)
    {
        char val = keysConvertHexCharToBinary(value[i]);
        if (val == 'z')
        {
            LOGFILE("Invalid hex character in key \"%s\" at position %u!", key, i);
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
    bool common_eticket_rsa_kek = false, personalized_eticket_rsa_kek = false;
    
    keys_file = fopen(KEYS_FILE_PATH, "rb");
    if (!keys_file)
    {
        LOGFILE("Unable to open \"%s\" to retrieve keys!", KEYS_FILE_PATH);
        return false;
    }
    
    while(true)
    {
        ret = keysGetKeyAndValueFromFile(keys_file, &key, &value);
        if (ret == 1 || ret == -2) break; /* Break from the while loop if EOF is reached or if an I/O error occurs */
        
        /* Ignore malformed lines */
        if (ret != 0 || !key || !value) continue;
        
        if (!common_eticket_rsa_kek && !personalized_eticket_rsa_kek && !strcasecmp(key, "eticket_rsa_kek"))
        {
            if (!keysParseHexKey(g_ncaKeyset.eticket_rsa_kek, key, value, sizeof(g_ncaKeyset.eticket_rsa_kek))) return false;
            common_eticket_rsa_kek = true;
            key_count++;
        } else
        if (!personalized_eticket_rsa_kek && !strcasecmp(key, "eticket_rsa_kek_personalized"))
        {
            /* Use the personalized eTicket RSA kek if available */
            /* This only appears on consoles that use the new PRODINFO key generation scheme */
            if (!keysParseHexKey(g_ncaKeyset.eticket_rsa_kek, key, value, sizeof(g_ncaKeyset.eticket_rsa_kek))) return false;
            personalized_eticket_rsa_kek = true;
            key_count++;
        } else {
            for(u32 i = 0; i < 0x20; i++)
            {
                snprintf(test_name, sizeof(test_name), "titlekek_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if (!keysParseHexKey(g_ncaKeyset.titlekeks[i], key, value, sizeof(g_ncaKeyset.titlekeks[i]))) return false;
                    key_count++;
                    break;
                }
                
                snprintf(test_name, sizeof(test_name), "key_area_key_application_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if (!keysParseHexKey(g_ncaKeyset.key_area_keys[i][0], key, value, sizeof(g_ncaKeyset.key_area_keys[i][0]))) return false;
                    key_count++;
                    break;
                }
                
                snprintf(test_name, sizeof(test_name), "key_area_key_ocean_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if (!keysParseHexKey(g_ncaKeyset.key_area_keys[i][1], key, value, sizeof(g_ncaKeyset.key_area_keys[i][1]))) return false;
                    key_count++;
                    break;
                }
                
                snprintf(test_name, sizeof(test_name), "key_area_key_system_%02x", i);
                if (!strcasecmp(key, test_name))
                {
                    if (!keysParseHexKey(g_ncaKeyset.key_area_keys[i][2], key, value, sizeof(g_ncaKeyset.key_area_keys[i][2]))) return false;
                    key_count++;
                    break;
                }
            }
        }
    }
    
    fclose(keys_file);
    
    if (!key_count)
    {
        LOGFILE("Unable to parse necessary keys from \"%s\"! (keys file empty?)", KEYS_FILE_PATH);
        return false;
    }
    
    return true;
}
