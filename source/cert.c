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

#include "cert.h"
#include "save.h"
#include "utils.h"

#define CERT_SAVEFILE_PATH              BIS_SYSTEM_PARTITION_MOUNT_NAME "/save/80000000000000e0"
#define CERT_SAVEFILE_STORAGE_BASE_PATH "/certificate/"

#define CERT_TYPE(sig)  (pub_key_type == CertPubKeyType_Rsa4096 ? CertType_Sig##sig##_PubKeyRsa4096 : \
                        (pub_key_type == CertPubKeyType_Rsa2048 ? CertType_Sig##sig##_PubKeyRsa2048 : CertType_Sig##sig##_PubKeyEcc480))

/* Global variables. */

static save_ctx_t *g_esCertSaveCtx = NULL;
static Mutex g_esCertSaveMutex = 0;

/* Function prototypes. */

static bool certOpenEsCertSaveFile(void);
static void certCloseEsCertSaveFile(void);

static bool _certRetrieveCertificateByName(Certificate *dst, const char *name);
static u8 certGetCertificateType(const void *data, u64 data_size);

static bool _certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer);
static u32 certGetCertificateCountInSignatureIssuer(const char *issuer);

static u64 certCalculateRawCertificateChainSize(const CertificateChain *chain);
static void certCopyCertificateChainDataToMemoryBuffer(void *dst, const CertificateChain *chain);

bool certRetrieveCertificateByName(Certificate *dst, const char *name)
{
    mutexLock(&g_esCertSaveMutex);
    
    bool ret = false;
    
    if (!dst || !name || !strlen(name))
    {
        LOGFILE("Invalid parameters!");
        goto exit;
    }
    
    if (!certOpenEsCertSaveFile()) goto exit;
    
    ret = _certRetrieveCertificateByName(dst, name);
    
    certCloseEsCertSaveFile();
    
exit:
    mutexUnlock(&g_esCertSaveMutex);
    
    return ret;
}

bool certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer)
{
    mutexLock(&g_esCertSaveMutex);
    
    bool ret = false;
    
    if (!dst || !issuer || !strlen(issuer) || strncmp(issuer, "Root-", 5) != 0)
    {
        LOGFILE("Invalid parameters!");
        goto exit;
    }
    
    if (!certOpenEsCertSaveFile()) goto exit;
    
    ret = _certRetrieveCertificateChainBySignatureIssuer(dst, issuer);
    
    certCloseEsCertSaveFile();
    
exit:
    mutexUnlock(&g_esCertSaveMutex);
    
    return ret;
}

void certFreeCertificateChain(CertificateChain *chain)
{
    if (!chain || !chain->certs) return;
    
    chain->count = 0;
    free(chain->certs);
    chain->certs = NULL;
}

CertCommonBlock *certGetCommonBlockFromCertificate(Certificate *cert)
{
    if (!cert || cert->type == CertType_None || cert->type > CertType_SigHmac160_PubKeyEcc480 || cert->size < CERT_MIN_SIZE || cert->size > CERT_MAX_SIZE)
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    CertCommonBlock *cert_common_blk = NULL;
    
    switch(cert->type)
    {
        case CertType_SigRsa4096_PubKeyRsa4096:
            cert_common_blk = &(((CertSigRsa4096PubKeyRsa4096*)cert->data)->cert_common_blk);
            break;
        case CertType_SigRsa4096_PubKeyRsa2048:
            cert_common_blk = &(((CertSigRsa4096PubKeyRsa2048*)cert->data)->cert_common_blk);
            break;
        case CertType_SigRsa4096_PubKeyEcc480:
            cert_common_blk = &(((CertSigRsa4096PubKeyEcc480*)cert->data)->cert_common_blk);
            break;
        case CertType_SigRsa2048_PubKeyRsa4096:
            cert_common_blk = &(((CertSigRsa2048PubKeyRsa4096*)cert->data)->cert_common_blk);
            break;
        case CertType_SigRsa2048_PubKeyRsa2048:
            cert_common_blk = &(((CertSigRsa2048PubKeyRsa2048*)cert->data)->cert_common_blk);
            break;
        case CertType_SigRsa2048_PubKeyEcc480:
            cert_common_blk = &(((CertSigRsa2048PubKeyEcc480*)cert->data)->cert_common_blk);
            break;
        case CertType_SigEcc480_PubKeyRsa4096:
            cert_common_blk = &(((CertSigEcc480PubKeyRsa4096*)cert->data)->cert_common_blk);
            break;
        case CertType_SigEcc480_PubKeyRsa2048:
            cert_common_blk = &(((CertSigEcc480PubKeyRsa2048*)cert->data)->cert_common_blk);
            break;
        case CertType_SigEcc480_PubKeyEcc480:
            cert_common_blk = &(((CertSigEcc480PubKeyEcc480*)cert->data)->cert_common_blk);
            break;
        case CertType_SigHmac160_PubKeyRsa4096:
            cert_common_blk = &(((CertSigHmac160PubKeyRsa4096*)cert->data)->cert_common_blk);
            break;
        case CertType_SigHmac160_PubKeyRsa2048:
            cert_common_blk = &(((CertSigHmac160PubKeyRsa2048*)cert->data)->cert_common_blk);
            break;
        case CertType_SigHmac160_PubKeyEcc480:
            cert_common_blk = &(((CertSigHmac160PubKeyEcc480*)cert->data)->cert_common_blk);
            break;
        default:
            break;
    }
    
    return cert_common_blk;
}

u8 *certGenerateRawCertificateChainBySignatureIssuer(const char *issuer, u64 *out_size)
{
    if (!issuer || !strlen(issuer) || !out_size)
    {
        LOGFILE("Invalid parameters!");
        return NULL;
    }
    
    CertificateChain chain = {0};
    u8 *raw_chain = NULL;
    u64 raw_chain_size = 0;
    
    if (!certRetrieveCertificateChainBySignatureIssuer(&chain, issuer))
    {
        LOGFILE("Error retrieving certificate chain for \"%s\"!", issuer);
        return NULL;
    }
    
    raw_chain_size = certCalculateRawCertificateChainSize(&chain);
    
    raw_chain = malloc(raw_chain_size);
    if (!raw_chain)
    {
        LOGFILE("Unable to allocate memory for raw \"%s\" certificate chain! (0x%lX)", issuer, raw_chain_size);
        goto out;
    }
    
    certCopyCertificateChainDataToMemoryBuffer(raw_chain, &chain);
    *out_size = raw_chain_size;
    
out:
    certFreeCertificateChain(&chain);
    
    return raw_chain;
}

static bool certOpenEsCertSaveFile(void)
{
    if (g_esCertSaveCtx) return true;
    
    g_esCertSaveCtx = save_open_savefile(CERT_SAVEFILE_PATH, 0);
    if (!g_esCertSaveCtx)
    {
        LOGFILE("Failed to open ES certificate system savefile!");
        return false;
    }
    
    return true;
}

static void certCloseEsCertSaveFile(void)
{
    if (g_esCertSaveCtx)
    {
        save_close_savefile(g_esCertSaveCtx);
        g_esCertSaveCtx = NULL;
    }
}

static bool _certRetrieveCertificateByName(Certificate *dst, const char *name)
{
    if (!g_esCertSaveCtx || !dst || !name || !strlen(name))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u64 cert_size = 0;
    char cert_path[SAVE_FS_LIST_MAX_NAME_LENGTH] = {0};
    allocation_table_storage_ctx_t fat_storage = {0};
    
    snprintf(cert_path, SAVE_FS_LIST_MAX_NAME_LENGTH, "%s%s", CERT_SAVEFILE_STORAGE_BASE_PATH, name);
    
    if (!save_get_fat_storage_from_file_entry_by_path(g_esCertSaveCtx, cert_path, &fat_storage, &cert_size))
    {
        LOGFILE("Failed to locate certificate \"%s\" in ES certificate system save!", name);
        return false;
    }
    
    if (cert_size < CERT_MIN_SIZE || cert_size > CERT_MAX_SIZE)
    {
        LOGFILE("Invalid size for certificate \"%s\"! (0x%lX)", name, cert_size);
        return false;
    }
    
    dst->size = cert_size;
    
    u64 br = save_allocation_table_storage_read(&fat_storage, dst->data, 0, dst->size);
    if (br != dst->size)
    {
        LOGFILE("Failed to read 0x%lX bytes from certificate \"%s\"! Read 0x%lX bytes.", dst->size, name, br);
        return false;
    }
    
    dst->type = certGetCertificateType(dst->data, dst->size);
    if (dst->type == CertType_None)
    {
        LOGFILE("Invalid certificate type for \"%s\"!", name);
        return false;
    }
    
    return true;
}

static u8 certGetCertificateType(const void *data, u64 data_size)
{
    if (!data || data_size < CERT_MIN_SIZE || data_size > CERT_MAX_SIZE)
    {
        LOGFILE("Invalid parameters!");
        return CertType_None;
    }
    
    u64 offset = 0;
    u8 type = CertType_None;
    const u8 *data_u8 = (const u8*)data;
    u32 sig_type = 0, pub_key_type = 0;
    
    memcpy(&sig_type, data_u8, sizeof(u32));
    sig_type = __builtin_bswap32(sig_type);
    
    switch(sig_type)
    {
        case SignatureType_Rsa4096Sha1:
        case SignatureType_Rsa4096Sha256:
            offset += sizeof(SignatureBlockRsa4096);
            break;
        case SignatureType_Rsa2048Sha1:
        case SignatureType_Rsa2048Sha256:
            offset += sizeof(SignatureBlockRsa2048);
            break;
        case SignatureType_Ecc480Sha1:
        case SignatureType_Ecc480Sha256:
            offset += sizeof(SignatureBlockEcc480);
            break;
        case SignatureType_Hmac160Sha1:
            offset += sizeof(SignatureBlockHmac160);
            break;
        default:
            LOGFILE("Invalid signature type value! (0x%08X)", sig_type);
            return type;
    }
    
    memcpy(&pub_key_type, data_u8 + offset, sizeof(u32));
    pub_key_type = __builtin_bswap32(pub_key_type);
    
    offset += MEMBER_SIZE(CertCommonBlock, pub_key_type);
    offset += MEMBER_SIZE(CertCommonBlock, name);
    offset += MEMBER_SIZE(CertCommonBlock, date);
    
    switch(pub_key_type)
    {
        case CertPubKeyType_Rsa4096:
            offset += sizeof(CertPublicKeyBlockRsa4096);
            break;
        case CertPubKeyType_Rsa2048:
            offset += sizeof(CertPublicKeyBlockRsa2048);
            break;
        case CertPubKeyType_Ecc480:
            offset += sizeof(CertPublicKeyBlockEcc480);
            break;
        default:
            LOGFILE("Invalid public key type value! (0x%08X)", pub_key_type);
            return type;
    }
    
    if (offset != data_size)
    {
        LOGFILE("Calculated end offset doesn't match certificate size! (0x%lX != 0x%lX)", offset, data_size);
        return type;
    }
    
    if (sig_type == SignatureType_Rsa4096Sha1 || sig_type == SignatureType_Rsa4096Sha256)
    {
        type = CERT_TYPE(Rsa4096);
    } else
    if (sig_type == SignatureType_Rsa2048Sha1 || sig_type == SignatureType_Rsa2048Sha256)
    {
        type = CERT_TYPE(Rsa2048);
    } else
    if (sig_type == SignatureType_Ecc480Sha1 || sig_type == SignatureType_Ecc480Sha256)
    {
        type = CERT_TYPE(Ecc480);
    } else
    if (sig_type == SignatureType_Hmac160Sha1)
    {
        type = CERT_TYPE(Hmac160);
    }
    
    return type;
}

static bool _certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer)
{
    if (!dst || !issuer || !strlen(issuer) || strncmp(issuer, "Root-", 5) != 0)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 i = 0;
    char issuer_copy[0x40] = {0};
    bool success = true;
    
    dst->count = certGetCertificateCountInSignatureIssuer(issuer);
    if (!dst->count)
    {
        LOGFILE("Invalid signature issuer string!");
        return false;
    }
    
    dst->certs = calloc(dst->count, sizeof(Certificate));
    if (!dst->certs)
    {
        LOGFILE("Unable to allocate memory for the certificate chain! (0x%lX)", dst->count * sizeof(Certificate));
        return false;
    }
    
    /* Copy string to avoid problems with strtok */
    /* The "Root-" parent from the issuer string is skipped */
    snprintf(issuer_copy, 0x40, issuer + 5);
    
    char *pch = strtok(issuer_copy, "-");
    while(pch != NULL)
    {
        if (!_certRetrieveCertificateByName(&(dst->certs[i]), pch))
        {
            LOGFILE("Unable to retrieve certificate \"%s\"!", pch);
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
    if (!issuer || !strlen(issuer)) return 0;
    
    u32 count = 0;
    char issuer_copy[0x40] = {0};
    
    /* Copy string to avoid problems with strtok */
    /* The "Root-" parent from the issuer string is skipped */
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
        memcpy(dst_u8, chain->certs[i].data, chain->certs[i].size);
        dst_u8 += chain->certs[i].size;
    }
}
