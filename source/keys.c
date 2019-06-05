#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "keys.h"
#include "util.h"
#include "ui.h"

/* Extern variables */

extern int breaks;
extern int font_height;

extern char strbuf[NAME_BUF_LEN * 4];

/* Statically allocated variables */

nca_keyset_t nca_keyset;

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
        uiDrawString("Error: invalid parameters to retrieve keys from process memory.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
        
        if (R_FAILED(result = pmdmntGetTitlePid(&pid, location->titleID)))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: pmdmntGetTitlePid failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
        
        if (R_FAILED(result = svcDebugActiveProcess(&debug_handle, pid)))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: svcDebugActiveProcess failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
        
        if (R_FAILED(result = svcGetDebugEvent((u8*)&d, debug_handle)))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: svcGetDebugEvent failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
    } else {
        // Otherwise, query svc for the process list
        u64 pids[300];
        u32 num_processes;
        
        if (R_FAILED(result = svcGetProcessList(&num_processes, pids, 300)))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: svcGetProcessList failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to retrieve debug handle for process with Title ID %016lX!", location->titleID);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            if (debug_handle) svcCloseHandle(debug_handle);
            return false;
        }
    }

    MemoryInfo mem_info;
    memset(&mem_info, 0, sizeof(MemoryInfo));

    u32 page_info;
    u64 addr = 0;
    u8 segment;
    
    u8 *dataTmp = NULL;
    
    bool success = true;

    for(segment = 1; segment < BIT(3);)
    {
        if (R_FAILED(result = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr)))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: svcQueryDebugProcessMemory failed! (0x%08X)", result);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            success = false;
            break;
        }
        
        // Weird code to allow for bitmasking segments
        if ((mem_info.perm & Perm_R) && ((mem_info.type & 0xff) >= MemType_CodeStatic) && ((mem_info.type & 0xff) < MemType_Heap) && ((segment <<= 1) >> 1 & location->mask) > 0)
        {
            // If location->data == NULL, realloc will essentially act as a malloc
            dataTmp = realloc(location->data, location->dataSize + mem_info.size);
            if (!dataTmp)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to resize key location data buffer to %lu bytes.", location->dataSize + mem_info.size);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                success = false;
                break;
            }
            
            location->data = dataTmp;
            dataTmp = NULL;
            
            memset(location->data + location->dataSize, 0, mem_info.size);
            
            if (R_FAILED(result = svcReadDebugProcessMemory(location->data + location->dataSize, debug_handle, mem_info.addr, mem_info.size)))
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: svcReadDebugProcessMemory failed! (0x%08X)", result);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
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
        uiDrawString("Error: invalid parameters to locate key in process memory.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u64 i;
    u8 temp_hash[SHA256_HASH_LENGTH];
    bool found = false;
    
    // Hash every key-length-sized byte chunk in data until it matches a key hash
    for(i = 0; i < location->dataSize; i++)
    {
        if (!found && (location->dataSize - i) < findKey->size) break;
        
        sha256CalculateHash(temp_hash, location->data + i, findKey->size);
        
        if (!memcmp(temp_hash, findKey->hash, SHA256_HASH_LENGTH))
        {
            // Jackpot
            memcpy(out, location->data + i, findKey->size);
            found = true;
            break;
        }
    }
    
    if (!found)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to locate key \"%s\" in process memory!", findKey->name);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
    }
    
    return found;
}

bool findFSRodataKeys(keyLocation *location)
{
    if (!location || location->titleID != FS_TID || location->mask != SEG_RODATA || !location->data || !location->dataSize)
    {
        uiDrawString("Error: invalid parameters to locate keys in FS RODATA segment.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!findKeyInProcessMemory(location, &header_kek_source, nca_keyset.header_kek_source)) return false;
    nca_keyset.key_cnt++;
    
    if (!findKeyInProcessMemory(location, &key_area_key_application_source, nca_keyset.key_area_key_application_source)) return false;
    nca_keyset.key_cnt++;
    
    if (!findKeyInProcessMemory(location, &key_area_key_ocean_source, nca_keyset.key_area_key_ocean_source)) return false;
    nca_keyset.key_cnt++;
    
    if (!findKeyInProcessMemory(location, &key_area_key_system_source, nca_keyset.key_area_key_system_source)) return false;
    nca_keyset.key_cnt++;
    
    return true;
}

bool getNcaKeys()
{
    Result result;
    u8 nca_header_kek[0x10];
    
    if (!retrieveProcessMemory(&FSRodata)) return false;
    
    if (!retrieveProcessMemory(&FSData))
    {
        freeProcessMemory(&FSRodata);
        return false;
    }
    
    if (!findFSRodataKeys(&FSRodata))
    {
        freeProcessMemory(&FSData);
        freeProcessMemory(&FSRodata);
        return false;
    }
    
    if (!findKeyInProcessMemory(&FSData, &header_key_source, nca_keyset.header_key_source))
    {
        freeProcessMemory(&FSData);
        freeProcessMemory(&FSRodata);
        return false;
    }
    
    nca_keyset.key_cnt++;
    
    // Derive NCA header key
    if (R_FAILED(result = splCryptoInitialize()))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to initialize the spl:crypto service! (0x%08X)", result);
        uiDrawString(strbuf, 8, 8, 255, 255, 255);
        freeProcessMemory(&FSData);
        freeProcessMemory(&FSRodata);
        return false;
    }
    
    if (R_FAILED(result = splCryptoGenerateAesKek(nca_keyset.header_kek_source, 0, 0, nca_header_kek)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: splCryptoGenerateAesKek(header_kek_source) failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, 8, 255, 255, 255);
        splCryptoExit();
        freeProcessMemory(&FSData);
        freeProcessMemory(&FSRodata);
        return false;
    }
    
    if (R_FAILED(result = splCryptoGenerateAesKey(nca_header_kek, nca_keyset.header_key_source + 0x00, nca_keyset.header_key + 0x00)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: splCryptoGenerateAesKey(header_key_source + 0x00) failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, 8, 255, 255, 255);
        splCryptoExit();
        freeProcessMemory(&FSData);
        freeProcessMemory(&FSRodata);
        return false;
    }
    
    if (R_FAILED(result = splCryptoGenerateAesKey(nca_header_kek, nca_keyset.header_key_source + 0x10, nca_keyset.header_key + 0x10)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: splCryptoGenerateAesKey(header_key_source + 0x10) failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, 8, 255, 255, 255);
        splCryptoExit();
        freeProcessMemory(&FSData);
        freeProcessMemory(&FSRodata);
        return false;
    }
    
    nca_keyset.key_cnt++;
    
    splCryptoExit();
    
    freeProcessMemory(&FSData);
    freeProcessMemory(&FSRodata);
    
    return true;
}

bool decryptNcaKeyArea(nca_header_t *dec_nca_header, u8 *out)
{
    if (!dec_nca_header || dec_nca_header->kaek_ind > 2)
    {
        uiDrawString("Error: invalid parameters to decrypt NCA key area.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    Result result;
    
    u8 i;
	u8 tmp_kek[0x10];
    
    u8 crypto_type = (dec_nca_header->crypto_type2 > dec_nca_header->crypto_type ? dec_nca_header->crypto_type2 : dec_nca_header->crypto_type);
    u8 *kek_source = (dec_nca_header->kaek_ind == 0 ? nca_keyset.key_area_key_application_source : (dec_nca_header->kaek_ind == 1 ? nca_keyset.key_area_key_ocean_source : nca_keyset.key_area_key_system_source));
    
    if (R_FAILED(result = splCryptoInitialize()))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: failed to initialize the spl:crypto service! (0x%08X)", result);
        uiDrawString(strbuf, 8, 8, 255, 255, 255);
        return false;
    }
    
    if (R_FAILED(result = splCryptoGenerateAesKek(kek_source, crypto_type, 0, tmp_kek)))
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: splCryptoGenerateAesKek(kek_source) failed! (0x%08X)", result);
        uiDrawString(strbuf, 8, 8, 255, 255, 255);
        splCryptoExit();
        return false;
    }
    
    bool success = true;
    u8 decrypted_nca_keys[NCA_KEY_AREA_KEY_CNT][NCA_KEY_AREA_KEY_SIZE];
    
    for(i = 0; i < NCA_KEY_AREA_KEY_CNT; i++)
    {
        if (R_FAILED(result = splCryptoGenerateAesKey(tmp_kek, dec_nca_header->nca_keys[i], decrypted_nca_keys[i])))
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: splCryptoGenerateAesKey(nca_kaek_%02u) failed! (0x%08X)", i, result);
            uiDrawString(strbuf, 8, 8, 255, 255, 255);
            success = false;
            break;
        }
    }
    
    splCryptoExit();
    
    memcpy(out, decrypted_nca_keys, NCA_KEY_AREA_SIZE);
    
    return success;
}
