/*
 * tik.c
 *
 * Copyright (c) 2019-2020, shchmue.
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include "nca.h"
#include "cert.h"
#include "save.h"
#include "es.h"
#include "keys.h"
#include "gamecard.h"
#include "mem.h"
#include "aes.h"
#include "rsa.h"

#define TIK_COMMON_SAVEFILE_PATH        BIS_SYSTEM_PARTITION_MOUNT_NAME "/save/80000000000000e1"
#define TIK_PERSONALIZED_SAVEFILE_PATH  BIS_SYSTEM_PARTITION_MOUNT_NAME "/save/80000000000000e2"

#define TIK_LIST_STORAGE_PATH           "/ticket_list.bin"
#define TIK_DB_STORAGE_PATH             "/ticket.bin"

#define TIK_COMMON_CERT_NAME            "XS00000020"
#define TIK_DEV_CERT_ISSUER             "CA00000004"

/* Type definitions. */

/// Used to parse ticket_list.bin entries.
typedef struct {
    FsRightsId rights_id;
    u64 ticket_id;
    u32 account_id;
    u8 reserved[0x04];
} TikListEntry;

NXDT_ASSERT(TikListEntry, 0x20);

/// 9.x+ CTR key entry in ES .data segment. Used to store CTR key/IV data for encrypted volatile tickets in ticket.bin and/or encrypted entries in ticket_list.bin.
/// This is always stored in pairs. The first entry holds the key/IV for the encrypted volatile ticket, while the second entry holds the key/IV for the encrypted entry in ticket_list.bin.
/// First index in this list is always 0.
typedef struct {
    u32 idx;                    ///< Entry index.
    u8 key[AES_128_KEY_SIZE];   ///< AES-128-CTR key.
    u8 ctr[AES_128_KEY_SIZE];   ///< AES-128-CTR counter/IV. Always zeroed out.
} TikEsCtrKeyEntry9x;

NXDT_ASSERT(TikEsCtrKeyEntry9x, 0x24);

/// Lookup pattern for TikEsCtrKeyEntry9x.
typedef struct {
    u32 idx1;                           ///< Always set to 0 (first entry).
    u8 ctrdata[AES_128_KEY_SIZE * 2];
    u32 idx2;                           ///< Always set to 1 (second entry).
} TikEsCtrKeyPattern9x;

NXDT_ASSERT(TikEsCtrKeyPattern9x, 0x28);

/* Global variables. */

static Mutex g_esTikSaveMutex = 0;

#if LOG_LEVEL <= LOG_LEVEL_ERROR
static const char *g_tikTitleKeyTypeStrings[] = {
    [TikTitleKeyType_Common] = "common",
    [TikTitleKeyType_Personalized] = "personalized"
};
#endif

static MemoryLocation g_esMemoryLocation = {
    .program_id = ES_SYSMODULE_TID,
    .mask = 0,
    .data = NULL,
    .data_size = 0
};

/* Function prototypes. */

static bool tikRetrieveTicketFromGameCardByRightsId(Ticket *dst, const FsRightsId *id);
static bool tikRetrieveTicketFromEsSaveDataByRightsId(Ticket *dst, const FsRightsId *id);

static bool tikFixTamperedCommonTicket(Ticket *tik);
static bool tikVerifyRsa2048Sha256Signature(const TikCommonBlock *tik_common_block, u64 hash_area_size, const u8 *signature);

static bool tikGetEncryptedTitleKey(Ticket *tik);
static bool tikGetDecryptedTitleKey(void *dst, const void *src, u8 key_generation);

static bool tikGetTitleKeyTypeFromRightsId(const FsRightsId *id, u8 *out);
static bool tikRetrieveRightsIdsByTitleKeyType(FsRightsId **out, u32 *out_count, bool personalized);

static bool tikGetTicketEntryOffsetFromTicketList(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u8 titlekey_type, u64 *out_offset);
static bool tikRetrieveTicketEntryFromTicketBin(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u8 titlekey_type, u64 ticket_offset);
static bool tikDecryptVolatileTicket(u8 *buf, u64 ticket_offset);

static bool tikGetTicketTypeAndSize(void *data, u64 data_size, u8 *out_type, u64 *out_size);

bool tikRetrieveTicketByRightsId(Ticket *dst, const FsRightsId *id, u8 key_generation, bool use_gamecard)
{
    if (!dst || !id || key_generation > NcaKeyGeneration_Max)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    TikCommonBlock *tik_common_block = NULL;
    bool success = false, tik_retrieved = false;

    LOG_DATA_INFO(id->c, sizeof(id->c), "Input rights ID:");

    /* Check if this ticket has already been retrieved. */
    tik_common_block = tikGetCommonBlockFromTicket(dst);
    if (tik_common_block && !memcmp(tik_common_block->rights_id.c, id->c, sizeof(id->c)))
    {
        success = true;
        goto end;
    }

    /* Clear output ticket. */
    memset(dst, 0, sizeof(Ticket));

    /* Validate the key generation field within the rights ID. */
    u8 key_gen_rid = id->c[0xF];
    bool old_key_gen = (key_generation < NcaKeyGeneration_Since301NUP);

    if ((old_key_gen && key_gen_rid) || (!old_key_gen && key_gen_rid != key_generation))
    {
        LOG_MSG_ERROR("Invalid rights ID key generation! Got 0x%02X, expected 0x%02X.", key_gen_rid, old_key_gen ? 0 : key_generation);
        goto end;
    }

    /* Update key generation field. */
    dst->key_generation = key_generation;

    /* Retrieve ticket data. */
    if (use_gamecard)
    {
        tik_retrieved = tikRetrieveTicketFromGameCardByRightsId(dst, id);
    } else {
        SCOPED_LOCK(&g_esTikSaveMutex) tik_retrieved = tikRetrieveTicketFromEsSaveDataByRightsId(dst, id);
    }

    if (!tik_retrieved)
    {
        LOG_MSG_ERROR("Unable to retrieve ticket data!");
        goto end;
    }

    /* Fix tampered common ticket, if needed. */
    if (!tikFixTamperedCommonTicket(dst)) goto end;

    /* Get encrypted titlekey. */
    if (!tikGetEncryptedTitleKey(dst))
    {
        LOG_MSG_ERROR("Unable to retrieve encrypted titlekey from ticket!");
        goto end;
    }

    /* Get decrypted titlekey. */
    if (!(success = tikGetDecryptedTitleKey(dst->dec_titlekey, dst->enc_titlekey, dst->key_generation)))
    {
        LOG_MSG_ERROR("Unable to decrypt titlekey!");
        goto end;
    }

    /* Generate hex strings. */
    tik_common_block = tikGetCommonBlockFromSignedTicketBlob(dst->data);

    utilsGenerateHexString(dst->enc_titlekey_str, sizeof(dst->enc_titlekey_str), dst->enc_titlekey, sizeof(dst->enc_titlekey), false);
    utilsGenerateHexString(dst->dec_titlekey_str, sizeof(dst->dec_titlekey_str), dst->dec_titlekey, sizeof(dst->dec_titlekey), false);
    utilsGenerateHexString(dst->rights_id_str, sizeof(dst->rights_id_str), tik_common_block->rights_id.c, sizeof(tik_common_block->rights_id.c), false);

end:
    return success;
}

bool tikConvertPersonalizedTicketToCommonTicket(Ticket *tik, u8 **out_raw_cert_chain, u64 *out_raw_cert_chain_size)
{
    TikCommonBlock *tik_common_block = NULL;

    u32 sig_type = 0;
    u8 *signature = NULL;
    u64 signature_size = 0;

    bool dev_cert = false;
    char cert_chain_issuer[0x40] = {0};

    u8 *raw_cert_chain = NULL;
    u64 raw_cert_chain_size = 0;

    if (!(tik_common_block = tikGetCommonBlockFromTicket(tik)) || tik_common_block->titlekey_type != TikTitleKeyType_Personalized || \
        (!out_raw_cert_chain && out_raw_cert_chain_size) || (out_raw_cert_chain && !out_raw_cert_chain_size))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Generate raw certificate chain for the new signature issuer (common). */
    dev_cert = (strstr(tik_common_block->issuer, TIK_DEV_CERT_ISSUER) != NULL);
    sprintf(cert_chain_issuer, "Root-CA%08X-%s", dev_cert ? 4 : 3, TIK_COMMON_CERT_NAME);

    raw_cert_chain = certGenerateRawCertificateChainBySignatureIssuer(cert_chain_issuer, &raw_cert_chain_size);
    if (!raw_cert_chain)
    {
        LOG_MSG_ERROR("Failed to generate raw certificate chain for common ticket signature issuer!");
        return false;
    }

    /* Wipe signature. */
    sig_type = signatureGetTypeFromSignedBlob(tik->data, false);
    signature = signatureGetSigFromSignedBlob(tik->data);
    signature_size = signatureGetSigSizeByType(sig_type);
    memset(signature, 0xFF, signature_size);

    /* Change signature issuer. */
    memset(tik_common_block->issuer, 0, sizeof(tik_common_block->issuer));
    sprintf(tik_common_block->issuer, "%s", cert_chain_issuer);

    /* Wipe the titlekey block and copy the encrypted titlekey to it. */
    memset(tik_common_block->titlekey_block, 0, sizeof(tik_common_block->titlekey_block));
    memcpy(tik_common_block->titlekey_block, tik->enc_titlekey, sizeof(tik->enc_titlekey));

    /* Update ticket size. */
    tik->size = (signatureGetBlockSizeByType(sig_type) + sizeof(TikCommonBlock));

    /* Update the rest of the ticket fields. */
    tik_common_block->titlekey_type = TikTitleKeyType_Common;
    tik_common_block->license_type = TikLicenseType_Permanent;
    tik_common_block->property_mask = TikPropertyMask_None;

    tik_common_block->ticket_id = 0;
    tik_common_block->device_id = 0;
    tik_common_block->account_id = 0;

    tik_common_block->sect_total_size = 0;
    tik_common_block->sect_hdr_offset = (u32)tik->size;
    tik_common_block->sect_hdr_count = 0;
    tik_common_block->sect_hdr_entry_size = 0;

    memset(tik->data + tik->size, 0, SIGNED_TIK_MAX_SIZE - tik->size);

    /* Update output pointers. */
    if (out_raw_cert_chain)
    {
        *out_raw_cert_chain = raw_cert_chain;
    } else {
        free(raw_cert_chain);
    }

    if (out_raw_cert_chain_size) *out_raw_cert_chain_size = raw_cert_chain_size;

    return true;
}

static bool tikRetrieveTicketFromGameCardByRightsId(Ticket *dst, const FsRightsId *id)
{
    if (!dst || !id)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    char tik_filename[0x30] = {0};
    u64 tik_offset = 0, tik_size = 0;
    bool success = false;

    utilsGenerateHexString(tik_filename, sizeof(tik_filename), id->c, sizeof(id->c), false);
    strcat(tik_filename, ".tik");

    /* Get ticket entry info. */
    if (!gamecardGetHashFileSystemEntryInfoByName(HashFileSystemPartitionType_Secure, tik_filename, &tik_offset, &tik_size))
    {
        LOG_MSG_ERROR("Error retrieving offset and size for \"%s\" entry in secure hash FS partition!", tik_filename);
        goto end;
    }

    /* Validate ticket size. */
    if (tik_size < SIGNED_TIK_MIN_SIZE || tik_size > SIGNED_TIK_MAX_SIZE)
    {
        LOG_MSG_ERROR("Invalid size for \"%s\"! (0x%lX).", tik_filename, tik_size);
        goto end;
    }

    /* Read ticket data. */
    if (!gamecardReadStorage(dst->data, tik_size, tik_offset))
    {
        LOG_MSG_ERROR("Failed to read \"%s\" data from the inserted gamecard!", tik_filename);
        goto end;
    }

    /* Get ticket type and size. */
    if (!(success = tikGetTicketTypeAndSize(dst->data, tik_size, &(dst->type), &(dst->size)))) LOG_MSG_ERROR("Unable to determine ticket type and size!");

end:
    return success;
}

static bool tikRetrieveTicketFromEsSaveDataByRightsId(Ticket *dst, const FsRightsId *id)
{
    if (!dst || !id)
    {
        LOG_MSG_ERROR("Invalid parameters!");
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
        LOG_MSG_ERROR("Unable to allocate 0x%lX bytes block for temporary read buffer!", buf_size);
        goto end;
    }

    /* Get titlekey type. */
    if (!tikGetTitleKeyTypeFromRightsId(id, &titlekey_type))
    {
        LOG_MSG_ERROR("Unable to retrieve ticket titlekey type!");
        goto end;
    }

    /* Open ES common/personalized system savefile. */
    if (!(save_ctx = save_open_savefile(titlekey_type == TikTitleKeyType_Common ? TIK_COMMON_SAVEFILE_PATH : TIK_PERSONALIZED_SAVEFILE_PATH, 0)))
    {
        LOG_MSG_ERROR("Failed to open ES %s ticket system savefile!", g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }

    /* Get ticket entry offset from ticket_list.bin. */
    if (!tikGetTicketEntryOffsetFromTicketList(save_ctx, buf, buf_size, id, titlekey_type, &ticket_offset))
    {
        LOG_MSG_ERROR("Unable to find an entry with a matching Rights ID in \"%s\" from ES %s ticket system save!", TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }

    /* Get ticket entry from ticket.bin. */
    if (!tikRetrieveTicketEntryFromTicketBin(save_ctx, buf, buf_size, id, titlekey_type, ticket_offset))
    {
        LOG_MSG_ERROR("Unable to find a matching %s ticket entry for the provided Rights ID!", g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }

    /* Get ticket type and size. */
    if (!(success = tikGetTicketTypeAndSize(buf, SIGNED_TIK_MAX_SIZE, &(dst->type), &(dst->size))))
    {
        LOG_MSG_ERROR("Unable to determine ticket type and size!");
        goto end;
    }

    /* Copy ticket data. */
    memcpy(dst->data, buf, dst->size);

end:
    if (save_ctx) save_close_savefile(save_ctx);

    if (buf) free(buf);

    return success;
}

static bool tikFixTamperedCommonTicket(Ticket *tik)
{
    TikCommonBlock *tik_common_block = NULL;

    u32 sig_type = 0;
    u8 *signature = NULL;
    u64 signature_size = 0, hash_area_size = 0;

    bool success = false;

    if (!tik || tik->key_generation > NcaKeyGeneration_Max || !(tik_common_block = tikGetCommonBlockFromSignedTicketBlob(tik->data)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Get ticket signature and its properties, as well as the ticket hash area size. */
    sig_type = signatureGetTypeFromSignedBlob(tik->data, false);
    signature = signatureGetSigFromSignedBlob(tik->data);
    signature_size = signatureGetSigSizeByType(sig_type);
    hash_area_size = tikGetSignedTicketBlobHashAreaSize(tik->data);

    /* Return right away if we're not dealing with a common ticket, if the signature type doesn't match RSA-2048 + SHA-256, or if the signature is valid. */
    if (tik_common_block->titlekey_type != TikTitleKeyType_Common || sig_type != SignatureType_Rsa2048Sha256 || \
        tikVerifyRsa2048Sha256Signature(tik_common_block, hash_area_size, signature))
    {
        success = true;
        goto end;
    }

    LOG_MSG_DEBUG("Detected tampered common ticket!");

    /* Nintendo didn't start putting the key generation value into the rights ID until HOS 3.0.1. */
    /* Old custom tools used to wipe the key generation field and/or save its value into a different offset. */
    /* We're gonna take care of that by setting the correct values where they need to go. */
    memset(signature, 0xFF, signature_size);

    tik_common_block->titlekey_type = TikTitleKeyType_Common;
    tik_common_block->license_type = TikLicenseType_Permanent;
    tik_common_block->key_generation = tik->key_generation;
    tik_common_block->property_mask = TikPropertyMask_None;

    tik_common_block->ticket_id = 0;
    tik_common_block->device_id = 0;
    tik_common_block->account_id = 0;

    tik_common_block->sect_total_size = 0;
    tik_common_block->sect_hdr_offset = (u32)tik->size;
    tik_common_block->sect_hdr_count = 0;
    tik_common_block->sect_hdr_entry_size = 0;

    /* Update return value. */
    success = true;

end:
    return success;
}

static bool tikVerifyRsa2048Sha256Signature(const TikCommonBlock *tik_common_block, u64 hash_area_size, const u8 *signature)
{
    if (!tik_common_block || hash_area_size < sizeof(TikCommonBlock) || hash_area_size > (SIGNED_TIK_MAX_SIZE - sizeof(SignatureBlockRsa2048)) || !signature)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    const char *cert_name = (strrchr(tik_common_block->issuer, '-') + 1);
    Certificate cert = {0};
    const u8 *modulus = NULL, *public_exponent = NULL;

    /* Get certificate for the ticket signature issuer. */
    if (!certRetrieveCertificateByName(&cert, cert_name))
    {
        LOG_MSG_ERROR("Failed to retrieve certificate for \"%s\".", cert_name);
        return false;
    }

    /* Get certificate modulus and public exponent. */
    modulus = certGetPublicKeyFromCertificate(&cert);
    public_exponent = certGetPublicExponentFromCertificate(&cert);

    /* Validate the ticket signature. */
    return rsa2048VerifySha256BasedPkcs1v15Signature(tik_common_block, hash_area_size, signature, modulus, public_exponent, CERT_RSA_PUB_EXP_SIZE);
}

static bool tikGetEncryptedTitleKey(Ticket *tik)
{
    TikCommonBlock *tik_common_block = NULL;

    if (!tik || !(tik_common_block = tikGetCommonBlockFromSignedTicketBlob(tik->data)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool success = false;

    switch(tik_common_block->titlekey_type)
    {
        case TikTitleKeyType_Common:
            /* No console-specific crypto used. Copy encrypted titlekey right away. */
            memcpy(tik->enc_titlekey, tik_common_block->titlekey_block, sizeof(tik->enc_titlekey));
            success = true;
            break;
        case TikTitleKeyType_Personalized:
            /* The titlekey block is encrypted using RSA-OAEP with a console-specific RSA key. */
            /* We have to perform a RSA-OAEP unwrap operation to get the encrypted titlekey. */
            success = keysDecryptRsaOaepWrappedTitleKey(tik_common_block->titlekey_block, tik->enc_titlekey);
            break;
        default:
            LOG_MSG_ERROR("Invalid titlekey type value! (0x%02X).", tik_common_block->titlekey_type);
            break;
    }

    return success;
}

static bool tikGetDecryptedTitleKey(void *dst, const void *src, u8 key_generation)
{
    if (!dst || !src)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    const u8 *ticket_common_key = NULL;

    ticket_common_key = keysGetTicketCommonKey(key_generation);
    if (!ticket_common_key)
    {
        LOG_MSG_ERROR("Unable to retrieve ticket common key for key generation 0x%02X!", key_generation);
        return false;
    }

    aes128EcbCrypt(dst, src, ticket_common_key, false);

    return true;
}

static bool tikGetTitleKeyTypeFromRightsId(const FsRightsId *id, u8 *out)
{
    if (!id || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u32 count = 0;
    FsRightsId *rights_ids = NULL;
    bool found = false;

    for(u8 i = TikTitleKeyType_Common; i < TikTitleKeyType_Count; i++)
    {
        /* Get all rights IDs for the current titlekey type. */
        if (!tikRetrieveRightsIdsByTitleKeyType(&rights_ids, &count, i == TikTitleKeyType_Personalized))
        {
            LOG_MSG_ERROR("Unable to retrieve %s rights IDs!", g_tikTitleKeyTypeStrings[i]);
            continue;
        }

        /* Look for the provided rights ID. */
        for(u32 j = 0; j < count; j++)
        {
            if (!memcmp(rights_ids[j].c, id->c, sizeof(id->c)))
            {
                *out = i;
                found = true;
                break;
            }
        }

        if (rights_ids) free(rights_ids);

        if (found) break;
    }

    return found;
}

static bool tikRetrieveRightsIdsByTitleKeyType(FsRightsId **out, u32 *out_count, bool personalized)
{
    if (!out || !out_count)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    Result rc = 0;
    u32 count = 0, ids_written = 0;
    FsRightsId *rights_ids = NULL;
    bool success = false;

#if LOG_LEVEL <= LOG_LEVEL_ERROR
    u8 str_idx = (personalized ? TikTitleKeyType_Personalized : TikTitleKeyType_Common);
#endif

    *out = NULL;
    *out_count = 0;

    /* Get ticket count for the provided titlekey type. */
    rc = (personalized ? esCountPersonalizedTicket((s32*)&count) : esCountCommonTicket((s32*)&count));
    if (R_FAILED(rc))
    {
        LOG_MSG_ERROR("esCount%c%sTicket failed! (0x%X).", toupper(g_tikTitleKeyTypeStrings[str_idx][0]), g_tikTitleKeyTypeStrings[str_idx] + 1, rc);
        goto end;
    }

    if (!count)
    {
        LOG_MSG_WARNING("No %s tickets available!", g_tikTitleKeyTypeStrings[str_idx]);
        success = true;
        goto end;
    }

    /* Allocate memory for our rights ID array. */
    rights_ids = calloc(count, sizeof(FsRightsId));
    if (!rights_ids)
    {
        LOG_MSG_ERROR("Unable to allocate memory for %s rights IDs!", g_tikTitleKeyTypeStrings[str_idx]);
        goto end;
    }

    /* Get rights IDs from all tickets that match the provided titlekey type. */
    rc = (personalized ? esListPersonalizedTicket((s32*)&ids_written, rights_ids, (s32)count) : esListCommonTicket((s32*)&ids_written, rights_ids, (s32)count));
    success = (R_SUCCEEDED(rc) && ids_written);
    if (!success)
    {
        LOG_MSG_ERROR("esList%c%sTicket failed! (0x%X). Wrote %u entries, expected %u entries.", toupper(g_tikTitleKeyTypeStrings[str_idx][0]), g_tikTitleKeyTypeStrings[str_idx] + 1, rc, ids_written, count);
        goto end;
    }

    /* Update output values. */
    *out = rights_ids;
    *out_count = ids_written;

end:
    if (!success && rights_ids) free(rights_ids);

    return success;
}

static bool tikGetTicketEntryOffsetFromTicketList(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u8 titlekey_type, u64 *out_offset)
{
    if (!save_ctx || !buf || !buf_size || (buf_size % sizeof(TikListEntry)) != 0 || !id || titlekey_type >= TikTitleKeyType_Count || !out_offset)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    allocation_table_storage_ctx_t fat_storage = {0};
    u64 ticket_list_bin_size = 0, br = 0, total_br = 0;

    u8 last_rights_id[0x10] = {0};
    memset(last_rights_id, 0xFF, sizeof(last_rights_id));

    bool last_entry_found = false, success = false;

    /* Get FAT storage info for the ticket_list.bin stored within the opened system savefile. */
    if (!save_get_fat_storage_from_file_entry_by_path(save_ctx, TIK_LIST_STORAGE_PATH, &fat_storage, &ticket_list_bin_size))
    {
        LOG_MSG_ERROR("Failed to locate \"%s\" in ES %s ticket system save!", TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }

    /* Validate ticket_list.bin size. */
    if (ticket_list_bin_size < sizeof(TikListEntry) || (ticket_list_bin_size % sizeof(TikListEntry)) != 0)
    {
        LOG_MSG_ERROR("Invalid size for \"%s\" in ES %s ticket system save! (0x%lX).", TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type], ticket_list_bin_size);
        goto end;
    }

    /* Look for an entry matching our rights ID in ticket_list.bin. */
    while(total_br < ticket_list_bin_size)
    {
        /* Update chunk size, if needed. */
        if (buf_size > (ticket_list_bin_size - total_br)) buf_size = (ticket_list_bin_size - total_br);

        /* Read current chunk. */
        if ((br = save_allocation_table_storage_read(&fat_storage, buf, total_br, buf_size)) != buf_size)
        {
            LOG_MSG_ERROR("Failed to read 0x%lX bytes chunk at offset 0x%lX from \"%s\" in ES %s ticket system save!", buf_size, total_br, TIK_LIST_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
            break;
        }

        /* Process individual ticket list entries. */
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
                *out_offset = (entry_offset << 5); /* (entry_offset / sizeof(TikListEntry)) * SIGNED_TIK_MAX_SIZE */
                success = true;
                break;
            }
        }

        total_br += br;

        if (last_entry_found || success) break;
    }

end:
    return success;
}

static bool tikRetrieveTicketEntryFromTicketBin(save_ctx_t *save_ctx, u8 *buf, u64 buf_size, const FsRightsId *id, u8 titlekey_type, u64 ticket_offset)
{
    if (!save_ctx || !buf || buf_size < SIGNED_TIK_MAX_SIZE || !id || titlekey_type >= TikTitleKeyType_Count || (ticket_offset % SIGNED_TIK_MAX_SIZE) != 0)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    allocation_table_storage_ctx_t fat_storage = {0};
    u64 ticket_bin_size = 0, br = 0;

    TikCommonBlock *tik_common_block = NULL;

    bool is_volatile = false, success = false;

    /* Get FAT storage info for the ticket.bin stored within the opened system savefile. */
    if (!save_get_fat_storage_from_file_entry_by_path(save_ctx, TIK_DB_STORAGE_PATH, &fat_storage, &ticket_bin_size))
    {
        LOG_MSG_ERROR("Failed to locate \"%s\" in ES %s ticket system save!", TIK_DB_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }

    /* Validate ticket.bin size. */
    if (ticket_bin_size < SIGNED_TIK_MIN_SIZE || (ticket_bin_size % SIGNED_TIK_MAX_SIZE) != 0 || ticket_bin_size < (ticket_offset + SIGNED_TIK_MAX_SIZE))
    {
        LOG_MSG_ERROR("Invalid size for \"%s\" in ES %s ticket system save! (0x%lX).", TIK_DB_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type], ticket_bin_size);
        goto end;
    }

    /* Read ticket data. */
    if ((br = save_allocation_table_storage_read(&fat_storage, buf, ticket_offset, SIGNED_TIK_MAX_SIZE)) != SIGNED_TIK_MAX_SIZE)
    {
        LOG_MSG_ERROR("Failed to read 0x%X-byte long ticket at offset 0x%lX from \"%s\" in ES %s ticket system save!", SIGNED_TIK_MAX_SIZE, ticket_offset, TIK_DB_STORAGE_PATH, \
                                                                                                                       g_tikTitleKeyTypeStrings[titlekey_type]);
        goto end;
    }

    /* Get ticket common block. */
    tik_common_block = tikGetCommonBlockFromSignedTicketBlob(buf);

    /* Check if we're dealing with a volatile (encrypted) ticket. */
    is_volatile = (!tik_common_block || strncmp(tik_common_block->issuer, "Root-", 5) != 0);
    if (is_volatile)
    {
        /* Attempt to decrypt the ticket. */
        if (!tikDecryptVolatileTicket(buf, ticket_offset))
        {
            LOG_MSG_ERROR("Unable to decrypt volatile ticket at offset 0x%lX in \"%s\" from ES %s ticket system save!", ticket_offset, TIK_DB_STORAGE_PATH, g_tikTitleKeyTypeStrings[titlekey_type]);
            goto end;
        }

        /* Get ticket common block. */
        tik_common_block = tikGetCommonBlockFromSignedTicketBlob(buf);
    }

    /* Check if the rights ID from the ticket common block matches the one we're looking for. */
    if (!(success = (memcmp(tik_common_block->rights_id.c, id->c, sizeof(id->c)) == 0))) LOG_MSG_ERROR("Retrieved ticket doesn't hold a matching Rights ID!");

end:
    return success;
}

static bool tikDecryptVolatileTicket(u8 *buf, u64 ticket_offset)
{
    if (!buf || (ticket_offset % SIGNED_TIK_MAX_SIZE) != 0)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    Aes128CtrContext ctr_ctx = {0};
    u8 null_ctr[AES_128_KEY_SIZE] = {0}, ctr[AES_128_KEY_SIZE] = {0}, dec_tik[SIGNED_TIK_MAX_SIZE] = {0};
    TikCommonBlock *tik_common_block = NULL;
    bool success = false;

    /* Don't proceed if HOS version isn't at least 9.0.0. */
    if (!hosversionAtLeast(9, 0, 0))
    {
        LOG_MSG_ERROR("Unable to retrieve ES key entry for volatile tickets under HOS versions below 9.0.0!");
        goto end;
    }

    /* Retrieve ES program memory. */
    if (!memRetrieveFullProgramMemory(&g_esMemoryLocation))
    {
        LOG_MSG_ERROR("Failed to retrieve ES program memory!");
        goto end;
    }

    /* Retrieve the CTR key/IV from ES program memory in order to decrypt this ticket. */
    for(u64 i = 0; i < g_esMemoryLocation.data_size; i++)
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
        if ((tik_common_block = tikGetCommonBlockFromSignedTicketBlob(dec_tik)) != NULL && !strncmp(tik_common_block->issuer, "Root-", 5))
        {
            memcpy(buf, dec_tik, SIGNED_TIK_MAX_SIZE);
            success = true;
            break;
        }
    }

    if (!success) LOG_MSG_ERROR("Unable to find ES memory key entry!");

end:
    memFreeMemoryLocation(&g_esMemoryLocation);

    return success;
}

static bool tikGetTicketTypeAndSize(void *data, u64 data_size, u8 *out_type, u64 *out_size)
{
    if (!data || data_size < SIGNED_TIK_MIN_SIZE || data_size > SIGNED_TIK_MAX_SIZE || !out_type || !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u32 sig_type = 0;
    u64 signed_ticket_size = 0;
    u8 type = TikType_None;
    bool success = false;

    /* Get signature type and signed ticket size. */
    sig_type = signatureGetTypeFromSignedBlob(data, false);
    signed_ticket_size = tikGetSignedTicketBlobSize(data);

    if (!signatureIsValidType(sig_type) || signed_ticket_size < SIGNED_TIK_MIN_SIZE || signed_ticket_size > data_size)
    {
        LOG_MSG_ERROR("Input buffer doesn't hold a valid signed ticket!");
        goto end;
    }

    /* Determine ticket type. */
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

    /* Update output. */
    success = (type > TikType_None && type < TikType_Count);
    if (success)
    {
        *out_type = type;
        *out_size = signed_ticket_size;
    }

end:
    return success;
}
