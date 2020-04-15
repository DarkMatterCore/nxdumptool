#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "tik.h"
#include "save.h"
#include "es.h"
#include "keys.h"
#include "rsa.h"
#include "utils.h"

#define TIK_COMMON_SAVEFILE_PATH        "sys:/save/80000000000000e1"
#define TIK_PERSONALIZED_SAVEFILE_PATH  "sys:/save/80000000000000e2"
#define TIK_SAVEFILE_STORAGE_PATH       "/ticket.bin"

#define ETICKET_DEVKEY_PUBLIC_EXPONENT  0x10001

/* Type definitions. */

/// Everything after the AES CTR is encrypted.
typedef struct {
    u8 ctr[0x10];
    u8 exponent[0x100];
    u8 modulus[0x100];
    u32 public_exponent;    ///< Must match ETICKET_DEVKEY_PUBLIC_EXPONENT. Stored using big endian byte order.
    u8 padding[0x14];
    u64 device_id;
    u8 ghash[0x10];
} tikEticketDeviceKeyData;

/* Global variables. */

static SetCalRsa2048DeviceKey g_eTicketDeviceKey = {0};
static bool g_eTicketDeviceKeyRetrieved = false;

/// Used during the RSA-OAEP titlekey decryption stage.
static const u8 g_nullHash[0x20] = {
    0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
    0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55
};

/* Function prototypes. */

static bool tikRetrieveRightsIdsByTitleKeyType(FsRightsId **out, u32 *out_count, bool personalized);
static u8 tikGetTitleKeyTypeFromRightsId(const FsRightsId *id);
static bool tikGetTicketTypeAndSize(const void *data, u64 data_size, u8 *out_type, u64 *out_size);
static bool tikTestKeyPairFromEticketDeviceKey(const void *e, const void *d, const void *n);
static bool tikRetrieveEticketDeviceKey(void);

TikCommonBlock *tikGetTicketCommonBlockFromMemoryBuffer(void *data)
{
    if (!data)
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    u8 *data_u8 = (u8*)data;
    TikCommonBlock *tik_common_blk = NULL;
    u32 sig_type;
    
    memcpy(&sig_type, data_u8, sizeof(u32));
    
    switch(sig_type)
    {
        case SignatureType_Rsa4096Sha1:
        case SignatureType_Rsa4096Sha256:
            tik_common_blk = (TikCommonBlock*)(data_u8 + sizeof(SignatureBlockRsa4096));
            break;
        case SignatureType_Rsa2048Sha1:
        case SignatureType_Rsa2048Sha256:
            tik_common_blk = (TikCommonBlock*)(data_u8 + sizeof(SignatureBlockRsa2048));
            break;
        case SignatureType_Ecsda240Sha1:
        case SignatureType_Ecsda240Sha256:
            tik_common_blk = (TikCommonBlock*)(data_u8 + sizeof(SignatureBlockEcsda240));
            break;
        default:
            LOGFILE("Invalid signature type value! (0x%08X)", sig_type);
            return NULL;
    }
    
    return tik_common_blk;
}

bool tikRetrieveTicketByRightsId(Ticket *dst, const FsRightsId *id)
{
    if (!dst || !id)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 i;
    
    save_ctx_t *save_ctx = NULL;
    allocation_table_storage_ctx_t fat_storage = {0};
    u64 ticket_bin_size = 0;
    
    u64 buf_size = (TIK_MAX_SIZE * 0x10);   /* 0x4000 */
    u64 br = buf_size, total_br = 0;
    u8 *ticket_bin_buf = NULL;
    
    bool found_tik = false, success = false;
    
    u8 title_key_type = tikGetTitleKeyTypeFromRightsId(id);
    if (title_key_type == TikTitleKeyType_Invalid)
    {
        LOGFILE("Unable to retrieve ticket titlekey type!");
        return false;
    }
    
    save_ctx = save_open_savefile(title_key_type == TikTitleKeyType_Common ? TIK_COMMON_SAVEFILE_PATH : TIK_PERSONALIZED_SAVEFILE_PATH, 0);
    if (!save_ctx)
    {
        LOGFILE("Failed to open ES %s ticket system savefile!", title_key_type == TikTitleKeyType_Common ? "common" : "personalized");
        return false;
    }
    
    if (!save_get_fat_storage_from_file_entry_by_path(save_ctx, TIK_SAVEFILE_STORAGE_PATH, &fat_storage, &ticket_bin_size))
    {
        LOGFILE("Failed to locate \"%s\" in ES %s ticket system save!", TIK_SAVEFILE_STORAGE_PATH, title_key_type == TikTitleKeyType_Common ? "common" : "personalized");
        goto out;
    }
    
    if (ticket_bin_size < TIK_MIN_SIZE || (ticket_bin_size % TIK_MAX_SIZE) != 0)
    {
        LOGFILE("Invalid size for \"%s\"! (0x%lX)", TIK_SAVEFILE_STORAGE_PATH, ticket_bin_size);
        goto out;
    }
    
    ticket_bin_buf = malloc(buf_size);
    if (ticket_bin_buf)
    {
        LOGFILE("Unable to allocate memory for temporary read buffer!");
        goto out;
    }
    
    while(br == buf_size && total_br < ticket_bin_size)
    {
        br = save_allocation_table_storage_read(&fat_storage, ticket_bin_buf, total_br, buf_size);
        if (br != buf_size)
        {
            LOGFILE("Failed to read 0x%lX bytes chunk at offset 0x%lX from \"%s\" in ES %s ticket system save!", buf_size, total_br, TIK_SAVEFILE_STORAGE_PATH, title_key_type == TikTitleKeyType_Common ? "common" : "personalized");
            goto out;
        }
        
        if (ticket_bin_buf[0] == 0) break;
        
        for(i = 0; i < buf_size; i += TIK_MAX_SIZE)
        {
            TikCommonBlock *tik_common_blk = tikGetTicketCommonBlockFromMemoryBuffer(ticket_bin_buf + i);
            if (!tik_common_blk || memcmp(tik_common_blk->rights_id.c, id->c, 0x10) != 0) continue;
            
            /* Jackpot */
            found_tik = true;
            break;
        }
    }
    
    if (!found_tik)
    {
        LOGFILE("Unable to find a matching ticket entry for the provided Rights ID!");
        goto out;
    }
    
    if (!tikGetTicketTypeAndSize(ticket_bin_buf + i, TIK_MAX_SIZE, &(dst->type), &(dst->size)))
    {
        LOGFILE("Unable to determine ticket type and size!");
        goto out;
    }
    
    memcpy(dst->data, ticket_bin_buf + i, dst->size);
    
    success = true;
    
out:
    if (ticket_bin_buf) free(ticket_bin_buf);
    
    if (save_ctx) save_close_savefile(save_ctx);
    
    return success;
}

bool tikGetTitleKeyFromTicketCommonBlock(void *dst, TikCommonBlock *tik_common_blk)
{
    if (!dst || !tik_common_blk)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    size_t out_keydata_size = 0;
    u8 out_keydata[0x100] = {0};
    tikEticketDeviceKeyData *eticket_devkey = NULL;
    
    switch(tik_common_blk->title_key_type)
    {
        case TikTitleKeyType_Common:
            /* No titlekek crypto used */
            memcpy(dst, tik_common_blk->title_key_block, 0x10);
            break;
        case TikTitleKeyType_Personalized:
            /* Retrieve eTicket device key */
            if (!tikRetrieveEticketDeviceKey())
            {
                LOGFILE("Unable to retrieve eTicket device key!");
                return false;
            }
            
            eticket_devkey = (tikEticketDeviceKeyData*)g_eTicketDeviceKey.key;
            
            /* Perform a RSA-OAEP decrypt operation to get the titlekey */
            if (!rsa2048OaepDecryptAndVerify(out_keydata, 0x100, tik_common_blk->title_key_block, eticket_devkey->modulus, eticket_devkey->exponent, 0x100, g_nullHash, &out_keydata_size) || out_keydata_size < 0x10)
            {
                LOGFILE("RSA-OAEP titlekey decryption failed!");
                return false;
            }
            
            /* Copy decrypted titlekey */
            memcpy(dst, out_keydata, 0x10);
            
            break;
        default:
            LOGFILE("Invalid titlekey type value! (0x%02X)", tik_common_blk->title_key_type);
            return false;
    }
    
    return true;
}

bool tikGetTitleKekDecryptedTitleKey(void *dst, const void *src, u8 key_generation)
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

bool tikGetTitleKekDecryptedTitleKeyFromTicket(void *dst, Ticket *tik)
{
    if (!dst || !tik)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u8 titlekey[0x10] = {0};
    TikCommonBlock *tik_common_blk = NULL;
    
    tik_common_blk = tikGetTicketCommonBlockFromTicket(tik);
    if (!tik_common_blk)
    {
        LOGFILE("Unable to retrieve ticket common block!");
        return false;
    }
    
    if (!tikGetTitleKeyFromTicketCommonBlock(titlekey, tik_common_blk))
    {
        LOGFILE("Unable to retrieve titlekey from ticket!");
        return false;
    }
    
    /* Even though tickets do have a proper key_generation field, we'll just retrieve it from the rights_id field */
    /* Old custom tools used to wipe the key_generation field or save it to a different offset */
    if (!tikGetTitleKekDecryptedTitleKey(dst, titlekey, tik_common_blk->rights_id.c[0xF]))
    {
        LOGFILE("Unable to perform titlekek decryption on titlekey!");
        return false;
    }
    
    return true;
}

void tikConvertPersonalizedTicketToCommonTicket(Ticket *tik, const void *titlekey)
{
    if (!tik || tik->type > TikType_SigEcsda240 || tik->size < TIK_MIN_SIZE || !titlekey) return;
    
    bool dev_cert = false;
    TikCommonBlock *tik_common_blk = NULL;
    
    tik_common_blk = tikGetTicketCommonBlockFromTicket(tik);
    if (!tik_common_blk || tik_common_blk->title_key_type != TikTitleKeyType_Personalized) return;
    
    switch(tik->type)
    {
        case TikType_SigRsa4096:
            tik->size = sizeof(TikSigRsa4096);
            memset(tik->data + 4, 0xFF, MEMBER_SIZE(SignatureBlockRsa4096, signature));
            break;
        case TikType_SigRsa2048:
            tik->size = sizeof(TikSigRsa2048);
            memset(tik->data + 4, 0xFF, MEMBER_SIZE(SignatureBlockRsa2048, signature));
            break;
        case TikType_SigEcsda240:
            tik->size = sizeof(TikSigEcsda240);
            memset(tik->data + 4, 0xFF, MEMBER_SIZE(SignatureBlockEcsda240, signature));
            break;
        default:
            break;
    }
    
    dev_cert = (strstr(tik_common_blk->issuer, "CA00000004") != NULL);
    
    memset(tik_common_blk->issuer, 0, sizeof(tik_common_blk->issuer));
    sprintf(tik_common_blk->issuer, "Root-CA%08X-XS00000020", dev_cert ? 4 : 3);
    
    memset(tik_common_blk->title_key_block, 0, sizeof(tik_common_blk->title_key_block));
    memcpy(tik_common_blk->title_key_block, titlekey, 0x10);
    
    tik_common_blk->title_key_type = TikTitleKeyType_Common;
    tik_common_blk->ticket_id = 0;
    tik_common_blk->device_id = 0;
    tik_common_blk->account_id = 0;
    
    tik_common_blk->sect_total_size = 0;
    tik_common_blk->sect_hdr_offset = (u32)tik->size;
    tik_common_blk->sect_hdr_count = 0;
    tik_common_blk->sect_hdr_entry_size = 0;
    
    memset(tik->data + tik->size, 0, TIK_MAX_SIZE - tik->size);
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
    
    rc = (personalized ? esCountPersonalizedTicket((s32*)&count) : esCountCommonTicket((s32*)&count));
    if (R_FAILED(rc))
    {
        LOGFILE("esCount%sTicket failed! (0x%08X)", personalized ? "Personalized" : "Common", rc);
        return false;
    }
    
    if (!count)
    {
        LOGFILE("No %s tickets available!", personalized ? "personalized" : "common");
        *out_count = 0;
        return true;
    }
    
    rights_ids = calloc(count, sizeof(FsRightsId));
    if (!rights_ids)
    {
        LOGFILE("Unable to allocate memory for %s rights IDs!", personalized ? "personalized" : "common");
        return false;
    }
    
    rc = (personalized ? esListPersonalizedTicket((s32*)&ids_written, rights_ids, count * sizeof(FsRightsId)) : esListCommonTicket((s32*)&ids_written, rights_ids, count * sizeof(FsRightsId)));
    if (R_FAILED(rc) || ids_written != count)
    {
        LOGFILE("esList%sTicket failed! (0x%08X) | Wrote %u entries, expected %u entries", personalized ? "Personalized" : "Common", rc, ids_written, count);
        free(rights_ids);
        return false;
    }
    
    *out = rights_ids;
    *out_count = count;
    
    return true;
}

static u8 tikGetTitleKeyTypeFromRightsId(const FsRightsId *id)
{
    if (!id)
    {
        LOGFILE("Invalid parameters!");
        return TikTitleKeyType_Invalid;
    }
    
    u8 type = TikTitleKeyType_Invalid;
    u32 count;
    FsRightsId *rights_ids;
    
    for(u8 i = 0; i < 2; i++)
    {
        count = 0;
        rights_ids = NULL;
        
        if (!tikRetrieveRightsIdsByTitleKeyType(&rights_ids, &count, (bool)i))
        {
            LOGFILE("Unable to retrieve %s rights IDs!", i == 0 ? "common" : "personalized");
            break;
        }
        
        if (!count) continue;
        
        for(u32 j = 0; j < count; j++)
        {
            if (!memcmp(rights_ids[j].c, id->c, 0x10))
            {
                type = i; /* TikTitleKeyType_Common or TikTitleKeyType_Personalized */
                break;
            }
        }
        
        free(rights_ids);
        
        if (type != TikTitleKeyType_Invalid) break;
    }
    
    return type;
}

static bool tikGetTicketTypeAndSize(const void *data, u64 data_size, u8 *out_type, u64 *out_size)
{
    if (!data || data_size < TIK_MIN_SIZE || data_size > TIK_MAX_SIZE || !out_type || !out_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u8 type = TikType_Invalid;
    const u8 *data_u8 = (const u8*)data;
    const TikCommonBlock *tik_common_blk = NULL;
    u32 sig_type;
    u64 offset = 0;
    
    memcpy(&sig_type, data_u8, sizeof(u32));
    
    switch(sig_type)
    {
        case SignatureType_Rsa4096Sha1:
        case SignatureType_Rsa4096Sha256:
            type = TikType_SigRsa4096;
            offset += sizeof(SignatureBlockRsa4096);
            break;
        case SignatureType_Rsa2048Sha1:
        case SignatureType_Rsa2048Sha256:
            type = TikType_SigRsa2048;
            offset += sizeof(SignatureBlockRsa2048);
            break;
        case SignatureType_Ecsda240Sha1:
        case SignatureType_Ecsda240Sha256:
            type = TikType_SigEcsda240;
            offset += sizeof(SignatureBlockEcsda240);
            break;
        default:
            LOGFILE("Invalid signature type value! (0x%08X)", sig_type);
            return false;
    }
    
    tik_common_blk = (const TikCommonBlock*)(data_u8 + offset);
    offset += sizeof(TikCommonBlock);
    
    if ((u32)offset != tik_common_blk->sect_hdr_offset)
    {
        LOGFILE("Calculated ticket common block end offset doesn't match ESv2 section records header offset! 0x%X != 0x%X", (u32)offset, tik_common_blk->sect_hdr_offset);
        return false;
    }
    
    for(u32 i = 0; i < tik_common_blk->sect_hdr_count; i++)
    {
        const TikEsv2SectionRecord *rec = (const TikEsv2SectionRecord*)(data_u8 + offset);
        
        offset += sizeof(TikEsv2SectionRecord);
        offset += ((u64)rec->record_count * (u64)rec->record_size);
        
        if (offset > data_size)
        {
            LOGFILE("Offset calculation exceeded input buffer size while counting ESv2 section records! (0x%lX)", offset);
            return false;
        }
    }
    
    *out_type = type;
    *out_size = offset;
    
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
    
    /* 0xCAFEBABE */
    x[0xFC] = 0xCA;
    x[0xFD] = 0xFE;
    x[0xFE] = 0xBA;
    x[0xFF] = 0xBE;
    
    rc = splUserExpMod(x, n, d, 0x100, y);
    if (R_FAILED(rc))
    {
        LOGFILE("splUserExpMod failed! (#1) (0x%08X)", rc);
        return false;
    }
    
    rc = splUserExpMod(y, n, e, 4, z);
    if (R_FAILED(rc))
    {
        LOGFILE("splUserExpMod failed! (#2) (0x%08X)", rc);
        return false;
    }
    
    if (memcmp(x, y, 0x100) != 0)
    {
        LOGFILE("Invalid RSA key pair!");
        return false;
    }
    
    return true;
}

static bool tikRetrieveEticketDeviceKey(void)
{
    if (g_eTicketDeviceKeyRetrieved) return true;
    
    Result rc = 0;
    u32 public_exponent = 0;
    tikEticketDeviceKeyData *eticket_devkey = NULL;
    Aes128CtrContext eticket_aes_ctx = {0};
    
    rc = setcalGetEticketDeviceKey(&g_eTicketDeviceKey);
    if (R_FAILED(rc))
    {
        LOGFILE("setcalGetEticketDeviceKey failed! (0x%08X)", rc);
        return false;
    }
    
    /* Decrypt eTicket RSA key */
    eticket_devkey = (tikEticketDeviceKeyData*)g_eTicketDeviceKey.key;
    aes128CtrContextCreate(&eticket_aes_ctx, keysGetEticketRsaKek(), eticket_devkey->ctr);
    aes128CtrCrypt(&eticket_aes_ctx, &(eticket_devkey->exponent), &(eticket_devkey->exponent), sizeof(tikEticketDeviceKeyData) - 0x10);
    
    /* Public exponent value must be 0x10001 */
    /* It is stored use big endian byte order */
    public_exponent = __builtin_bswap32(eticket_devkey->public_exponent);
    if (public_exponent != ETICKET_DEVKEY_PUBLIC_EXPONENT)
    {
        LOGFILE("Invalid public RSA exponent for eTicket device key! Wrong keys? (0x%08X)", public_exponent);
        return false;
    }
    
    /* Test RSA key pair */
    if (!tikTestKeyPairFromEticketDeviceKey(&(eticket_devkey->public_exponent), eticket_devkey->exponent, eticket_devkey->modulus))
    {
        LOGFILE("RSA key pair test failed! Wrong keys?");
        return false;
    }
    
    g_eTicketDeviceKeyRetrieved = true;
    
    return true;
}
