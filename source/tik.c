/*
 * tik.c
 *
 * Copyright (c) 2019-2020, shchmue.
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 *
 * nxdumptool is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * nxdumptool is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "utils.h"
#include "tik.h"
#include "cert.h"
#include "save.h"
#include "es.h"
#include "keys.h"
#include "rsa.h"
#include "gamecard.h"
#include "mem.h"
#include "aes.h"

#define TIK_COMMON_SAVEFILE_PATH        BIS_SYSTEM_PARTITION_MOUNT_NAME "/save/80000000000000e1"
#define TIK_PERSONALIZED_SAVEFILE_PATH  BIS_SYSTEM_PARTITION_MOUNT_NAME "/save/80000000000000e2"

#define TIK_LIST_STORAGE_PATH           "/ticket_list.bin"
#define TIK_DB_STORAGE_PATH             "/ticket.bin"

#define ETICKET_DEVKEY_PUBLIC_EXPONENT  0x10001

#define ES_CTRKEY_ENTRY_ALIGNMENT       0x8

/* Type definitions. */

/// Used to parse ticket_list.bin entries.
typedef struct {
    FsRightsId rights_id;
    u64 ticket_id;
    u32 account_id;
    u8 reserved[0x04];
} TikListEntry;

/// 9.x+ CTR key entry in ES .data segment. Used to store CTR key/IV data for encrypted volatile tickets in ticket.bin and/or encrypted entries in ticket_list.bin.
/// This is always stored in pairs. The first entry holds the key/IV for the encrypted volatile ticket, while the second entry holds the key/IV for the encrypted entry in ticket_list.bin.
/// First index in this list is always 0, and it's aligned to ES_CTRKEY_ENTRY_ALIGNMENT.
typedef struct {
    u32 idx;                ///< Entry index.
    u8 key[AES_BLOCK_SIZE]; ///< AES-128-CTR key.
    u8 ctr[AES_BLOCK_SIZE]; ///< AES-128-CTR counter/IV. Always zeroed out.
} TikEsCtrKeyEntry9x;

/// Lookup pattern for TikEsCtrKeyEntry9x.
typedef struct {
    u32 idx1;                       ///< Always set to 0 (first entry).
    u8 ctrdata[AES_BLOCK_SIZE * 2];
    u32 idx2;                       ///< Always set to 1 (second entry).
} TikEsCtrKeyPattern9x;

/// Used to parse the eTicket device key retrieved from PRODINFO via setcalGetEticketDeviceKey().
/// Everything after the AES CTR is encrypted.
typedef struct {
    u8 ctr[0x10];
    u8 exponent[0x100];
    u8 modulus[0x100];
    u32 public_exponent;    ///< Must match ETICKET_DEVKEY_PUBLIC_EXPONENT. Stored using big endian byte order.
    u8 padding[0x14];
    u64 device_id;
    u8 ghash[0x10];
} TikEticketDeviceKeyData;

/* Global variables. */

static SetCalRsa2048DeviceKey g_eTicketDeviceKey = {0};
static bool g_eTicketDeviceKeyRetrieved = false;
static Mutex g_eTicketDeviceKeyMutex = 0;

/// Used during the RSA-OAEP titlekey decryption stage.
static const u8 g_nullHash[0x20] = {
    0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
    0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
};

static const char *g_tikTitleKeyTypeStrings[] = {
    [TikTitleKeyType_Common] = "common",
    [TikTitleKeyType_Personalized] = "personalized"
};

static MemoryLocation g_esMemoryLocation = {
    .program_id = ES_SYSMODULE_TID,
    .mask = 0,
    .data = NULL,
    .data_size = 0
};

/* Function prototypes. */

static bool tikRetrieveTicketFromGameCardByRightsId(Ticket *dst, const FsRightsId *id);
static bool tikRetrieveTicketFromEsSaveDataByRightsId(Ticket *dst, const FsRightsId *id);

static bool tikGetTitleKekEncryptedTitleKeyFromTicket(Ticket *tik);
static bool tikGetTitleKekDecryptedTitleKey(void *dst, const void *src, u8 key_generation);

static bool tikGetTitleKeyTypeFromRightsId(const FsRightsId *id, u8 *out);
static bool tikRetrieveRightsIdsByTitleKeyType(FsRightsId **out, u32 *out_count, bool personalized);

static bool tikGetTicketEntryOffsetFromTicketList(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u64 *out_offset, u8 titlekey_type);
static bool tikRetrieveTicketEntryFromTicketBin(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u64 ticket_offset, u8 titlekey_type);

static bool tikGetTicketTypeAndSize(void *data, u64 data_size, u8 *out_type, u64 *out_size);

static bool tikRetrieveEticketDeviceKey(void);
static bool tikTestKeyPairFromEticketDeviceKey(const void *e, const void *d, const void *n);

bool tikRetrieveTicketByRightsId(Ticket *dst, const FsRightsId *id, bool use_gamecard)
{
    if (!dst || !id)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    TikCommonBlock *tik_common_block = NULL;
    
    /* Check if this ticket has already been retrieved. */
    if (dst->type > TikType_None && dst->type <= TikType_SigHmac160 && dst->size >= SIGNED_TIK_MIN_SIZE && dst->size <= SIGNED_TIK_MAX_SIZE)
    {
        tik_common_block = tikGetCommonBlock(dst->data);
        if (tik_common_block && !memcmp(tik_common_block->rights_id.c, id->c, 0x10)) return true;
    }
    
    /* Clear output ticket. */
    memset(dst, 0, sizeof(Ticket));
    
    bool tik_retrieved = (use_gamecard ? tikRetrieveTicketFromGameCardByRightsId(dst, id) : tikRetrieveTicketFromEsSaveDataByRightsId(dst, id));
    if (!tik_retrieved)
    {
        LOGFILE("Unable to retrieve ticket data!");
        return false;
    }
    
    mutexLock(&g_eTicketDeviceKeyMutex);
    bool titlekey_retrieved = tikGetTitleKekEncryptedTitleKeyFromTicket(dst);
    mutexUnlock(&g_eTicketDeviceKeyMutex);
    
    if (!titlekey_retrieved)
    {
        LOGFILE("Unable to retrieve titlekey from ticket!");
        return false;
    }
    
    /* Even though tickets do have a proper key_generation field, we'll just retrieve it from the rights_id field. */
    /* Old custom tools used to wipe the key_generation field or save its value to a different offset. */
    if (!tikGetTitleKekDecryptedTitleKey(dst->dec_titlekey, dst->enc_titlekey, id->c[0xF]))
    {
        LOGFILE("Unable to perform titlekek decryption!");
        return false;
    }
    
    /* Generate rights ID string. */
    tik_common_block = tikGetCommonBlock(dst->data);
    utilsGenerateHexStringFromData(dst->rights_id_str, sizeof(dst->rights_id_str), tik_common_block->rights_id.c, sizeof(tik_common_block->rights_id.c));
    
    return true;
}

bool tikConvertPersonalizedTicketToCommonTicket(Ticket *tik, u8 **out_raw_cert_chain, u64 *out_raw_cert_chain_size)
{
    TikCommonBlock *tik_common_block = NULL;
    
    u32 sig_type = 0;
    u8 *signature = NULL;
    u64 signature_size = 0;
    
    bool dev_cert = false;
    char cert_chain_issuer[0x40] = {0};
    static const char *common_cert_names[] = { "XS00000020", "XS00000022", NULL };
    
    u8 *raw_cert_chain = NULL;
    u64 raw_cert_chain_size = 0;
    
    if (!tik || tik->type == TikType_None || tik->type > TikType_SigHmac160 || tik->size < SIGNED_TIK_MIN_SIZE || tik->size > SIGNED_TIK_MAX_SIZE || \
        !(tik_common_block = tikGetCommonBlock(tik->data)) || tik_common_block->titlekey_type != TikTitleKeyType_Personalized || !out_raw_cert_chain || !out_raw_cert_chain_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Generate raw certificate chain for the new signature issuer (common). */
    dev_cert = (strstr(tik_common_block->issuer, "CA00000004") != NULL);
    
    for(u8 i = 0; common_cert_names[i] != NULL; i++)
    {
        sprintf(cert_chain_issuer, "Root-CA%08X-%s", dev_cert ? 4 : 3, common_cert_names[i]);
        raw_cert_chain = certGenerateRawCertificateChainBySignatureIssuer(cert_chain_issuer, &raw_cert_chain_size);
        if (raw_cert_chain) break;
    }
    
    if (!raw_cert_chain)
    {
        LOGFILE("Failed to generate raw certificate chain for common ticket signature issuer!");
        return false;
    }
    
    /* Wipe signature. */
    sig_type = signatureGetSigType(tik->data, false);
    signature = signatureGetSig(tik->data);
    signature_size = signatureGetSigSize(sig_type);
    memset(signature, 0xFF, signature_size);
    
    /* Change signature issuer. */
    memset(tik_common_block->issuer, 0, sizeof(tik_common_block->issuer));
    sprintf(tik_common_block->issuer, "%s", cert_chain_issuer);
    
    /* Wipe the titlekey block and copy the titlekek-encrypted titlekey to it. */
    memset(tik_common_block->titlekey_block, 0, sizeof(tik_common_block->titlekey_block));
    memcpy(tik_common_block->titlekey_block, tik->enc_titlekey, 0x10);
    
    /* Update ticket size. */
    tik->size = (signatureGetBlockSize(sig_type) + sizeof(TikCommonBlock));
    
    /* Update the rest of the ticket fields. */
    tik_common_block->titlekey_type = TikTitleKeyType_Common;
    tik_common_block->property_mask &= ~(TikPropertyMask_ELicenseRequired | TikPropertyMask_Volatile);
    tik_common_block->ticket_id = 0;
    tik_common_block->device_id = 0;
    tik_common_block->account_id = 0;
    
    tik_common_block->sect_total_size = 0;
    tik_common_block->sect_hdr_offset = (u32)tik->size;
    tik_common_block->sect_hdr_count = 0;
    tik_common_block->sect_hdr_entry_size = 0;
    
    memset(tik->data + tik->size, 0, SIGNED_TIK_MAX_SIZE - tik->size);
    
    /* Update output pointers. */
    *out_raw_cert_chain = raw_cert_chain;
    *out_raw_cert_chain_size = raw_cert_chain_size;
    
    return true;
}

static bool tikRetrieveTicketFromGameCardByRightsId(Ticket *dst, const FsRightsId *id)
{
    if (!dst || !id)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    char tik_filename[0x30] = {0};
    u64 tik_offset = 0, tik_size = 0;
    
    utilsGenerateHexStringFromData(tik_filename, sizeof(tik_filename), id->c, 0x10);
    strcat(tik_filename, ".tik");
    
    if (!gamecardGetEntryInfoFromHashFileSystemPartitionByName(GameCardHashFileSystemPartitionType_Secure, tik_filename, &tik_offset, &tik_size))
    {
        LOGFILE("Error retrieving offset and size for \"%s\" entry in secure hash FS partition!", tik_filename);
        return false;
    }
    
    if (tik_size < SIGNED_TIK_MIN_SIZE || tik_size > SIGNED_TIK_MAX_SIZE)
    {
        LOGFILE("Invalid size for \"%s\"! (0x%lX).", tik_filename, tik_size);
        return false;
    }
    
    if (!gamecardReadStorage(dst->data, tik_size, tik_offset))
    {
        LOGFILE("Failed to read \"%s\" data from the inserted gamecard!", tik_filename);
        return false;
    }
    
    if (!tikGetTicketTypeAndSize(dst->data, tik_size, &(dst->type), &(dst->size)))
    {
        LOGFILE("Unable to determine ticket type and size!");
        return false;
    }
    
    return true;
}

static bool tikRetrieveTicketFromEsSaveDataByRightsId(Ticket *dst, const FsRightsId *id)
{
    if (!dst || !id)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u8 titlekey_type = 0;
    
    save_ctx_t *save_ctx = NULL;
    
    u64 buf_size = (SIGNED_TIK_MAX_SIZE * 0x100);
    u8 *buf = NULL;
    u64 ticket_offset = 0;
    
    bool success = false;
    
    /* Allocate memory to retrieve the ticket. */
    if (!(buf = malloc(buf_size)))
    {
        LOGFILE("Unable to allocate 0x%lX bytes block for temporary read buffer!", buf_size);
        return false;
    }
    
    /* Get titlekey type. */
    if (!tikGetTitleKeyTypeFromRightsId(id, &titlekey_type))
    {
        LOGFILE("Unable to retrieve ticket titlekey type!");
        goto end;
    }
    
    /* Open ES common/personalized system savefile. */
    if (!(save_ctx = save_open_savefile(titlekey_type == TikTitleKeyType_Common ? TIK_COMMON_SAVEFILE_PATH : TIK_PERSONALIZED_SAVEFILE_PATH, 0)))
    {
        LOGFILE("Failed to open ES %s ticket system savefile!", g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }
    
    /* Get ticket entry offset from ticket_list.bin. */
    if (!tikGetTicketEntryOffsetFromTicketList(save_ctx, buf, buf_size, id, &ticket_offset, titlekey_type))
    {
        LOGFILE("Unable to find an entry with a matching Rights ID in \"%s\" from ES %s ticket system save!", TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }
    
    /* Get ticket entry from ticket.bin. */
    if (!tikRetrieveTicketEntryFromTicketBin(save_ctx, buf, buf_size, id, ticket_offset, titlekey_type))
    {
        LOGFILE("Unable to find a matching %s ticket entry for the provided Rights ID!", g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }
    
    /* Get ticket type and size. */
    if (!tikGetTicketTypeAndSize(buf, SIGNED_TIK_MAX_SIZE, &(dst->type), &(dst->size)))
    {
        LOGFILE("Unable to determine ticket type and size!");
        goto end;
    }
    
    memcpy(dst->data, buf, dst->size);
    
    success = true;
    
end:
    if (save_ctx) save_close_savefile(save_ctx);
    
    if (buf) free(buf);
    
    return success;
}

static bool tikGetTitleKekEncryptedTitleKeyFromTicket(Ticket *tik)
{
    TikCommonBlock *tik_common_block = NULL;
    
    if (!tik || !(tik_common_block = tikGetCommonBlock(tik->data)))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    size_t out_keydata_size = 0;
    u8 out_keydata[0x100] = {0};
    
    TikEticketDeviceKeyData *eticket_devkey = NULL;
    
    switch(tik_common_block->titlekey_type)
    {
        case TikTitleKeyType_Common:
            /* No titlekek crypto used. */
            memcpy(tik->enc_titlekey, tik_common_block->titlekey_block, 0x10);
            break;
        case TikTitleKeyType_Personalized:
            /* Retrieve eTicket device key. */
            if (!tikRetrieveEticketDeviceKey())
            {
                LOGFILE("Unable to retrieve eTicket device key!");
                return false;
            }
            
            eticket_devkey = (TikEticketDeviceKeyData*)g_eTicketDeviceKey.key;
            
            /* Perform a RSA-OAEP decrypt operation to get the titlekey. */
            if (!rsa2048OaepDecryptAndVerify(out_keydata, 0x100, tik_common_block->titlekey_block, eticket_devkey->modulus, eticket_devkey->exponent, 0x100, g_nullHash, &out_keydata_size) || \
                out_keydata_size < 0x10)
            {
                LOGFILE("RSA-OAEP titlekey decryption failed!");
                return false;
            }
            
            /* Copy decrypted titlekey. */
            memcpy(tik->enc_titlekey, out_keydata, 0x10);
            
            break;
        default:
            LOGFILE("Invalid titlekey type value! (0x%02X).", tik_common_block->titlekey_type);
            return false;
    }
    
    return true;
}

static bool tikGetTitleKekDecryptedTitleKey(void *dst, const void *src, u8 key_generation)
{
    if (!dst || !src)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    const u8 *titlekek = NULL;
    Aes128Context titlekey_aes_ctx = {0};
    
    titlekek = keysGetTitlekek(key_generation);
    if (!titlekek)
    {
        LOGFILE("Unable to retrieve titlekek for key generation 0x%02X!", key_generation);
        return false;
    }
    
    aes128ContextCreate(&titlekey_aes_ctx, titlekek, false);
    aes128DecryptBlock(&titlekey_aes_ctx, dst, src);
    
    return true;
}

static bool tikGetTitleKeyTypeFromRightsId(const FsRightsId *id, u8 *out)
{
    if (!id || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 count = 0;
    FsRightsId *rights_ids = NULL;
    bool found = false;
    
    for(u8 i = 0; i < 2; i++)
    {
        count = 0;
        rights_ids = NULL;
        
        if (!tikRetrieveRightsIdsByTitleKeyType(&rights_ids, &count, i == 1))
        {
            LOGFILE("Unable to retrieve %s rights IDs!", g_tikTitleKeyTypeStrings[i]);
            continue;
        }
        
        if (!count) continue;
        
        for(u32 j = 0; j < count; j++)
        {
            if (!memcmp(rights_ids[j].c, id->c, 0x10))
            {
                *out = i; /* TikTitleKeyType_Common or TikTitleKeyType_Personalized. */
                found = true;
                break;
            }
        }
        
        free(rights_ids);
        
        if (found) break;
    }
    
    return found;
}

static bool tikRetrieveRightsIdsByTitleKeyType(FsRightsId **out, u32 *out_count, bool personalized)
{
    if (!out || !out_count)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u32 count = 0, ids_written = 0;
    FsRightsId *rights_ids = NULL;
    u8 str_idx = (personalized ? TikTitleKeyType_Personalized : TikTitleKeyType_Common);
    
    *out = NULL;
    *out_count = 0;
    
    rc = (personalized ? esCountPersonalizedTicket((s32*)&count) : esCountCommonTicket((s32*)&count));
    if (R_FAILED(rc))
    {
        LOGFILE("esCount%c%sTicket failed! (0x%08X).", toupper(g_tikTitleKeyTypeStrings[str_idx][0]), g_tikTitleKeyTypeStrings[str_idx] + 1);
        return false;
    }
    
    if (!count)
    {
        LOGFILE("No %s tickets available!", g_tikTitleKeyTypeStrings[str_idx]);
        return true;
    }
    
    rights_ids = calloc(count, sizeof(FsRightsId));
    if (!rights_ids)
    {
        LOGFILE("Unable to allocate memory for %s rights IDs!", g_tikTitleKeyTypeStrings[str_idx]);
        return false;
    }
    
    rc = (personalized ? esListPersonalizedTicket((s32*)&ids_written, rights_ids, (s32)count) : esListCommonTicket((s32*)&ids_written, rights_ids, (s32)count));
    if (R_FAILED(rc) || !ids_written)
    {
        LOGFILE("esList%c%sTicket failed! (0x%08X). Wrote %u entries, expected %u entries.", toupper(g_tikTitleKeyTypeStrings[str_idx][0]), g_tikTitleKeyTypeStrings[str_idx] + 1, rc, ids_written, count);
        free(rights_ids);
        return false;
    }
    
    *out = rights_ids;
    *out_count = ids_written;
    
    return true;
}

static bool tikGetTicketEntryOffsetFromTicketList(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u64 *out_offset, u8 titlekey_type)
{
    if (!save_ctx || !buf || !buf_size || (buf_size % sizeof(TikListEntry)) != 0 || !id || !out_offset)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    allocation_table_storage_ctx_t fat_storage = {0};
    u64 ticket_list_bin_size = 0, br = 0, total_br = 0;
    
    u8 last_rights_id[0x10];
    memset(last_rights_id, 0xFF, sizeof(last_rights_id));
    
    bool last_entry_found = false, success = false;
    
    /* Get FAT storage info for the ticket_list.bin stored within the opened system savefile. */
    if (!save_get_fat_storage_from_file_entry_by_path(save_ctx, TIK_LIST_STORAGE_PATH, &fat_storage, &ticket_list_bin_size))
    {
        LOGFILE("Failed to locate \"%s\" in ES %s ticket system save!", TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
        return false;
    }
    
    /* Check ticket_list.bin size. */
    if (ticket_list_bin_size < sizeof(TikListEntry) || (ticket_list_bin_size % sizeof(TikListEntry)) != 0)
    {
        LOGFILE("Invalid size for \"%s\" in ES %s ticket system save! (0x%lX).", TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type], ticket_list_bin_size);
        return false;
    }
    
    /* Look for an entry matching our rights ID in ticket_list.bin. */
    while(total_br < ticket_list_bin_size)
    {
        if (buf_size > (ticket_list_bin_size - total_br)) buf_size = (ticket_list_bin_size - total_br);
        
        if ((br = save_allocation_table_storage_read(&fat_storage, buf, total_br, buf_size)) != buf_size)
        {
            LOGFILE("Failed to read 0x%lX bytes chunk at offset 0x%lX from \"%s\" in ES %s ticket system save!", buf_size, total_br, TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
            break;
        }
        
        for(u64 i = 0; i < buf_size; i += sizeof(TikListEntry))
        {
            if ((buf_size - i) < sizeof(TikListEntry)) break;
            
            u64 entry_offset = (total_br + i);
            TikListEntry *entry = (TikListEntry*)(buf + i);
            
            /* Check if we found the last entry. */
            if (!memcmp(entry->rights_id.c, last_rights_id, sizeof(last_rights_id)))
            {
                last_entry_found = true;
                break;
            }
            
            /* Check if this is the entry we're looking for. */
            if (!memcmp(entry->rights_id.c, id->c, sizeof(id->c)))
            {
                /* Jackpot. */
                *out_offset = (entry_offset << 5); /* (entry_offset / 0x20) * 0x400 */
                success = true;
                break;
            }
        }
        
        total_br += br;
        
        if (last_entry_found || success) break;
    }
    
    return success;
}

static bool tikRetrieveTicketEntryFromTicketBin(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u64 ticket_offset, u8 titlekey_type)
{
    if (!save_ctx || !buf || buf_size < SIGNED_TIK_MAX_SIZE || !id || (ticket_offset % SIGNED_TIK_MAX_SIZE) != 0)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    allocation_table_storage_ctx_t fat_storage = {0};
    u64 ticket_bin_size = 0, br = 0;
    
    TikCommonBlock *tik_common_block = NULL;
    
    Aes128CtrContext ctr_ctx = {0};
    u8 null_ctr[AES_BLOCK_SIZE] = {0}, ctr[AES_BLOCK_SIZE] = {0}, dec_tik[SIGNED_TIK_MAX_SIZE] = {0};
    
    bool is_volatile = false, success = false;
    
    /* Get FAT storage info for the ticket.bin stored within the opened system savefile. */
    if (!save_get_fat_storage_from_file_entry_by_path(save_ctx, TIK_DB_STORAGE_PATH, &fat_storage, &ticket_bin_size))
    {
        LOGFILE("Failed to locate \"%s\" in ES %s ticket system save!", TIK_DB_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
        return false;
    }
    
    /* Check ticket.bin size. */
    if (ticket_bin_size < SIGNED_TIK_MIN_SIZE || (ticket_bin_size % SIGNED_TIK_MAX_SIZE) != 0 || ticket_bin_size < (ticket_offset + SIGNED_TIK_MAX_SIZE))
    {
        LOGFILE("Invalid size for \"%s\" in ES %s ticket system save! (0x%lX).", TIK_DB_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type], ticket_bin_size);
        return false;
    }
    
    /* Read ticket data. */
    if ((br = save_allocation_table_storage_read(&fat_storage, buf, ticket_offset, SIGNED_TIK_MAX_SIZE)) != SIGNED_TIK_MAX_SIZE)
    {
        LOGFILE("Failed to read 0x%lX bytes long ticket at offset 0x%lX from \"%s\" in ES %s ticket system save!", SIGNED_TIK_MAX_SIZE, ticket_offset, TIK_DB_STORAGE_PATH, \
                g_tikTitleKeyTypeStrings[titlekey_type]);
        return false;
    }
    
    /* Check if we're dealing with a volatile (encrypted) ticket. */
    if (!(tik_common_block = tikGetCommonBlock(buf)) || strncmp(tik_common_block->issuer, "Root", 4) != 0)
    {
        tik_common_block = NULL;
        is_volatile = true;
        
        /* Don't proceed if HOS version isn't at least 9.0.0. */
        if (!hosversionAtLeast(9, 0, 0))
        {
            LOGFILE("Unable to retrieve ES key entry for volatile tickets under HOS versions below 9.0.0!");
            return false;
        }
        
        /* Retrieve ES program memory. */
        if (!memRetrieveFullProgramMemory(&g_esMemoryLocation))
        {
            LOGFILE("Failed to retrieve ES program memory!");
            return false;
        }
        
        /* Retrieve the CTR key/IV from ES program memory in order to decrypt this ticket. */
        for(u64 i = 0; i < g_esMemoryLocation.data_size; i += ES_CTRKEY_ENTRY_ALIGNMENT)
        {
            if ((g_esMemoryLocation.data_size - i) < (sizeof(TikEsCtrKeyEntry9x) * 2)) break;
            
            /* Check if the key indexes are valid. idx2 should always be an odd number equal to idx + 1. */
            TikEsCtrKeyPattern9x *pattern = (TikEsCtrKeyPattern9x*)(g_esMemoryLocation.data + i);
            if (pattern->idx2 != (pattern->idx1 + 1) || !(pattern->idx2 & 1)) continue;
            
            /* Check if the key is not null and if the CTR is. */
            TikEsCtrKeyEntry9x *key_entry = (TikEsCtrKeyEntry9x*)pattern;
            if (!memcmp(key_entry->key, null_ctr, sizeof(null_ctr)) || memcmp(key_entry->ctr, null_ctr, sizeof(null_ctr)) != 0) continue;
            
            /* Check if we can decrypt the current ticket with this data. */
            memset(&ctr_ctx, 0, sizeof(Aes128CtrContext));
            aes128CtrInitializePartialCtr(ctr, key_entry->ctr, ticket_offset);
            aes128CtrContextCreate(&ctr_ctx, key_entry->key, ctr);
            aes128CtrCrypt(&ctr_ctx, dec_tik, buf, SIGNED_TIK_MAX_SIZE);
            
            /* Check if we successfully decrypted this ticket. */
            if ((tik_common_block = tikGetCommonBlock(dec_tik)) != NULL && !strncmp(tik_common_block->issuer, "Root", 4))
            {
                memcpy(buf, dec_tik, SIGNED_TIK_MAX_SIZE);
                tik_common_block = tikGetCommonBlock(buf);
                break;
            }
            
            tik_common_block = NULL;
        }
        
        /* Check if we were able to decrypt the ticket. */
        if (!tik_common_block)
        {
            LOGFILE("Unable to decrypt volatile ticket at offset 0x%lX in \"%s\" from ES %s ticket system save!", ticket_offset, TIK_DB_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
            goto end;
        }
    }
    
    /* Check if the rights ID from the ticket common block matches the one we're looking for. */
    if (!(success = (memcmp(tik_common_block->rights_id.c, id->c, 0x10) == 0))) LOGFILE("Retrieved ticket doesn't hold a matching Rights ID!");
    
end:
    if (is_volatile) memFreeMemoryLocation(&g_esMemoryLocation);
    
    return success;
}

static bool tikGetTicketTypeAndSize(void *data, u64 data_size, u8 *out_type, u64 *out_size)
{
    u32 sig_type = 0;
    u64 signed_ticket_size = 0;
    u8 type = TikType_None;
    
    if (!data || data_size < SIGNED_TIK_MIN_SIZE || data_size > SIGNED_TIK_MAX_SIZE || !out_type || !out_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    if (!(signed_ticket_size = tikGetSignedTicketSize(data)) || signed_ticket_size > data_size)
    {
        LOGFILE("Input buffer doesn't hold a valid signed ticket!");
        return false;
    }
    
    sig_type = signatureGetSigType(data, false);
    
    switch(sig_type)
    {
        case SignatureType_Rsa4096Sha1:
        case SignatureType_Rsa4096Sha256:
            type = TikType_SigRsa4096;
            break;
        case SignatureType_Rsa2048Sha1:
        case SignatureType_Rsa2048Sha256:
            type = TikType_SigRsa2048;
            break;
        case SignatureType_Ecc480Sha1:
        case SignatureType_Ecc480Sha256:
            type = TikType_SigEcc480;
            break;
        case SignatureType_Hmac160Sha1:
            type = TikType_SigHmac160;
            break;
        default:
            break;
    }
    
    *out_type = type;
    *out_size = signed_ticket_size;
    
    return true;
}

static bool tikRetrieveEticketDeviceKey(void)
{
    if (g_eTicketDeviceKeyRetrieved) return true;
    
    Result rc = 0;
    u32 public_exponent = 0;
    TikEticketDeviceKeyData *eticket_devkey = NULL;
    Aes128CtrContext eticket_aes_ctx = {0};
    
    rc = setcalGetEticketDeviceKey(&g_eTicketDeviceKey);
    if (R_FAILED(rc))
    {
        LOGFILE("setcalGetEticketDeviceKey failed! (0x%08X).", rc);
        return false;
    }
    
    /* Decrypt eTicket RSA key. */
    eticket_devkey = (TikEticketDeviceKeyData*)g_eTicketDeviceKey.key;
    aes128CtrContextCreate(&eticket_aes_ctx, keysGetEticketRsaKek(g_eTicketDeviceKey.generation > 0), eticket_devkey->ctr);
    aes128CtrCrypt(&eticket_aes_ctx, &(eticket_devkey->exponent), &(eticket_devkey->exponent), sizeof(TikEticketDeviceKeyData) - 0x10);
    
    /* Public exponent value must be 0x10001. */
    /* It is stored using big endian byte order. */
    public_exponent = __builtin_bswap32(eticket_devkey->public_exponent);
    if (public_exponent != ETICKET_DEVKEY_PUBLIC_EXPONENT)
    {
        LOGFILE("Invalid public RSA exponent for eTicket device key! Wrong keys? (0x%08X).", public_exponent);
        return false;
    }
    
    /* Test RSA key pair. */
    if (!tikTestKeyPairFromEticketDeviceKey(&(eticket_devkey->public_exponent), eticket_devkey->exponent, eticket_devkey->modulus))
    {
        LOGFILE("RSA key pair test failed! Wrong keys?");
        return false;
    }
    
    g_eTicketDeviceKeyRetrieved = true;
    
    return true;
}

static bool tikTestKeyPairFromEticketDeviceKey(const void *e, const void *d, const void *n)
{
    if (!e || !d || !n)
    {
        LOGFILE("Invalid parameters!");
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
        LOGFILE("splUserExpMod failed! (#1) (0x%08X).", rc);
        return false;
    }
    
    rc = splUserExpMod(y, n, e, 4, z);
    if (R_FAILED(rc))
    {
        LOGFILE("splUserExpMod failed! (#2) (0x%08X).", rc);
        return false;
    }
    
    if (memcmp(x, z, 0x100) != 0)
    {
        LOGFILE("Invalid RSA key pair!");
        return false;
    }
    
    return true;
}
