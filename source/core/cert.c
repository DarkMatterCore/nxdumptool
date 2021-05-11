/*
 * cert.c
 *
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
#include "cert.h"
#include "save.h"
#include "gamecard.h"

#define CERT_SAVEFILE_PATH              BIS_SYSTEM_PARTITION_MOUNT_NAME "/save/80000000000000e0"
#define CERT_SAVEFILE_STORAGE_BASE_PATH "/certificate/"

#define CERT_TYPE(sig)                  (pub_key_type == CertPubKeyType_Rsa4096 ? CertType_Sig##sig##_PubKeyRsa4096 : \
                                        (pub_key_type == CertPubKeyType_Rsa2048 ? CertType_Sig##sig##_PubKeyRsa2048 : CertType_Sig##sig##_PubKeyEcc480))

/* Global variables. */

static save_ctx_t *g_esCertSaveCtx = NULL;
static Mutex g_esCertSaveMutex = 0;

/* Function prototypes. */

static bool certOpenEsCertSaveFile(void);
static void certCloseEsCertSaveFile(void);

static bool _certRetrieveCertificateByName(Certificate *dst, const char *name);
static u8 certGetCertificateType(void *data, u64 data_size);

static bool _certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer);
static u32 certGetCertificateCountInSignatureIssuer(const char *issuer);

static u64 certCalculateRawCertificateChainSize(const CertificateChain *chain);
static void certCopyCertificateChainDataToMemoryBuffer(void *dst, const CertificateChain *chain);

bool certRetrieveCertificateByName(Certificate *dst, const char *name)
{
    mutexLock(&g_esCertSaveMutex);
    
    bool ret = false;
    
    if (!dst || !name || !*name)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    if (!certOpenEsCertSaveFile()) goto end;
    
    ret = _certRetrieveCertificateByName(dst, name);
    
    certCloseEsCertSaveFile();
    
end:
    mutexUnlock(&g_esCertSaveMutex);
    
    return ret;
}

bool certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer)
{
    mutexLock(&g_esCertSaveMutex);
    
    bool ret = false;
    
    if (!dst || !issuer || strncmp(issuer, "Root-", 5) != 0)
    {
        LOG_MSG("Invalid parameters!");
        goto end;
    }
    
    if (!certOpenEsCertSaveFile()) goto end;
    
    ret = _certRetrieveCertificateChainBySignatureIssuer(dst, issuer);
    
    certCloseEsCertSaveFile();
    
end:
    mutexUnlock(&g_esCertSaveMutex);
    
    return ret;
}

u8 *certGenerateRawCertificateChainBySignatureIssuer(const char *issuer, u64 *out_size)
{
    if (!issuer || !*issuer || !out_size)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }
    
    CertificateChain chain = {0};
    u8 *raw_chain = NULL;
    u64 raw_chain_size = 0;
    
    if (!certRetrieveCertificateChainBySignatureIssuer(&chain, issuer))
    {
        LOG_MSG("Error retrieving certificate chain for \"%s\"!", issuer);
        return NULL;
    }
    
    raw_chain_size = certCalculateRawCertificateChainSize(&chain);
    
    raw_chain = malloc(raw_chain_size);
    if (!raw_chain)
    {
        LOG_MSG("Unable to allocate memory for raw \"%s\" certificate chain! (0x%lX).", issuer, raw_chain_size);
        goto end;
    }
    
    certCopyCertificateChainDataToMemoryBuffer(raw_chain, &chain);
    *out_size = raw_chain_size;
    
end:
    certFreeCertificateChain(&chain);
    
    return raw_chain;
}

u8 *certRetrieveRawCertificateChainFromGameCardByRightsId(const FsRightsId *id, u64 *out_size)
{
    if (!id || !out_size)
    {
        LOG_MSG("Invalid parameters!");
        return NULL;
    }
    
    char raw_chain_filename[0x30] = {0};
    u64 raw_chain_offset = 0, raw_chain_size = 0;
    u8 *raw_chain = NULL;
    bool success = false;
    
    utilsGenerateHexStringFromData(raw_chain_filename, sizeof(raw_chain_filename), id->c, sizeof(id->c), false);
    strcat(raw_chain_filename, ".cert");
    
    if (!gamecardGetHashFileSystemEntryInfoByName(GameCardHashFileSystemPartitionType_Secure, raw_chain_filename, &raw_chain_offset, &raw_chain_size))
    {
        LOG_MSG("Error retrieving offset and size for \"%s\" entry in secure hash FS partition!", raw_chain_filename);
        return NULL;
    }
    
    if (raw_chain_size < SIGNED_CERT_MIN_SIZE)
    {
        LOG_MSG("Invalid size for \"%s\"! (0x%lX).", raw_chain_filename, raw_chain_size);
        return NULL;
    }
    
    raw_chain = malloc(raw_chain_size);
    if (!raw_chain)
    {
        LOG_MSG("Unable to allocate memory for raw \"%s\" certificate chain! (0x%lX).", raw_chain_filename, raw_chain_size);
        return NULL;
    }
    
    if (!gamecardReadStorage(raw_chain, raw_chain_size, raw_chain_offset))
    {
        LOG_MSG("Failed to read \"%s\" data from the inserted gamecard!", raw_chain_filename);
        goto end;
    }
    
    *out_size = raw_chain_size;
    success = true;
    
end:
    if (!success && raw_chain)
    {
        free(raw_chain);
        raw_chain = NULL;
    }
    
    return raw_chain;
}

static bool certOpenEsCertSaveFile(void)
{
    if (g_esCertSaveCtx) return true;
    
    g_esCertSaveCtx = save_open_savefile(CERT_SAVEFILE_PATH, 0);
    if (!g_esCertSaveCtx)
    {
        LOG_MSG("Failed to open ES certificate system savefile!");
        return false;
    }
    
    return true;
}

static void certCloseEsCertSaveFile(void)
{
    if (!g_esCertSaveCtx) return;
    save_close_savefile(g_esCertSaveCtx);
    g_esCertSaveCtx = NULL;
}

static bool _certRetrieveCertificateByName(Certificate *dst, const char *name)
{
    if (!g_esCertSaveCtx)
    {
        LOG_MSG("ES certificate savefile not opened!");
        return false;
    }
    
    u64 cert_size = 0;
    char cert_path[SAVE_FS_LIST_MAX_NAME_LENGTH] = {0};
    allocation_table_storage_ctx_t fat_storage = {0};
    
    snprintf(cert_path, SAVE_FS_LIST_MAX_NAME_LENGTH, CERT_SAVEFILE_STORAGE_BASE_PATH "%s", name);
    
    if (!save_get_fat_storage_from_file_entry_by_path(g_esCertSaveCtx, cert_path, &fat_storage, &cert_size))
    {
        LOG_MSG("Failed to locate certificate \"%s\" in ES certificate system save!", name);
        return false;
    }
    
    if (cert_size < SIGNED_CERT_MIN_SIZE || cert_size > SIGNED_CERT_MAX_SIZE)
    {
        LOG_MSG("Invalid size for certificate \"%s\"! (0x%lX).", name, cert_size);
        return false;
    }
    
    dst->size = cert_size;
    
    u64 br = save_allocation_table_storage_read(&fat_storage, dst->data, 0, dst->size);
    if (br != dst->size)
    {
        LOG_MSG("Failed to read 0x%lX bytes from certificate \"%s\"! Read 0x%lX bytes.", dst->size, name, br);
        return false;
    }
    
    dst->type = certGetCertificateType(dst->data, dst->size);
    if (dst->type == CertType_None)
    {
        LOG_MSG("Invalid certificate type for \"%s\"!", name);
        return false;
    }
    
    return true;
}

static u8 certGetCertificateType(void *data, u64 data_size)
{
    CertCommonBlock *cert_common_block = NULL;
    u32 sig_type = 0, pub_key_type = 0;
    u64 signed_cert_size = 0;
    u8 type = CertType_None;
    
    if (!data || data_size < SIGNED_CERT_MIN_SIZE || data_size > SIGNED_CERT_MAX_SIZE)
    {
        LOG_MSG("Invalid parameters!");
        return type;
    }
    
    if (!(cert_common_block = certGetCommonBlock(data)) || !(signed_cert_size = certGetSignedCertificateSize(data)) || signed_cert_size > data_size)
    {
        LOG_MSG("Input buffer doesn't hold a valid signed certificate!");
        return type;
    }
    
    sig_type = signatureGetSigType(data, true);
    pub_key_type = __builtin_bswap32(cert_common_block->pub_key_type);
    
    switch(sig_type)
    {
        case SignatureType_Rsa4096Sha1:
        case SignatureType_Rsa4096Sha256:
            type = CERT_TYPE(Rsa4096);
            break;
        case SignatureType_Rsa2048Sha1:
        case SignatureType_Rsa2048Sha256:
            type = CERT_TYPE(Rsa2048);
            break;
        case SignatureType_Ecc480Sha1:
        case SignatureType_Ecc480Sha256:
            type = CERT_TYPE(Ecc480);
            break;
        case SignatureType_Hmac160Sha1:
            type = CERT_TYPE(Hmac160);
            break;
        default:
            break;
    }
    
    return type;
}

static bool _certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer)
{
    if (!g_esCertSaveCtx)
    {
        LOG_MSG("ES certificate savefile not opened!");
        return false;
    }
    
    u32 i = 0;
    char issuer_copy[0x40] = {0};
    bool success = true;
    
    dst->count = certGetCertificateCountInSignatureIssuer(issuer);
    if (!dst->count)
    {
        LOG_MSG("Invalid signature issuer string!");
        return false;
    }
    
    dst->certs = calloc(dst->count, sizeof(Certificate));
    if (!dst->certs)
    {
        LOG_MSG("Unable to allocate memory for the certificate chain! (0x%lX).", dst->count * sizeof(Certificate));
        return false;
    }
    
    /* Copy string to avoid problems with strtok(). */
    /* The "Root-" parent from the issuer string is skipped. */
    snprintf(issuer_copy, 0x40, "%s", issuer + 5);
    
    char *pch = strtok(issuer_copy, "-");
    while(pch != NULL)
    {
        if (!_certRetrieveCertificateByName(&(dst->certs[i]), pch))
        {
            LOG_MSG("Unable to retrieve certificate \"%s\"!", pch);
            success = false;
            break;
        }
        
        i++;
        pch = strtok(NULL, "-");
    }
    
    if (!success) certFreeCertificateChain(dst);
    
    return success;
}

static u32 certGetCertificateCountInSignatureIssuer(const char *issuer)
{
    if (!issuer || !*issuer) return 0;
    
    u32 count = 0;
    char issuer_copy[0x40] = {0};
    
    /* Copy string to avoid problems with strtok(). */
    /* The "Root-" parent from the issuer string is skipped. */
    snprintf(issuer_copy, 0x40, issuer + 5);
    
    char *pch = strtok(issuer_copy, "-");
    while(pch != NULL)
    {
        count++;
        pch = strtok(NULL, "-");
    }
    
    return count;
}

static u64 certCalculateRawCertificateChainSize(const CertificateChain *chain)
{
    if (!chain || !chain->count || !chain->certs) return 0;
    
    u64 chain_size = 0;
    for(u32 i = 0; i < chain->count; i++) chain_size += chain->certs[i].size;
    return chain_size;
}

static void certCopyCertificateChainDataToMemoryBuffer(void *dst, const CertificateChain *chain)
{
    if (!chain || !chain->count || !chain->certs) return;
    
    u8 *dst_u8 = (u8*)dst;
    for(u32 i = 0; i < chain->count; i++)
    {
        Certificate *cert = &(chain->certs[i]);
        memcpy(dst_u8, cert->data, cert->size);
        dst_u8 += cert->size;
    }
}
