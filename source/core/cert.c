/*
 * cert.c
 *
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

static void certCopyCertificateChainDataToMemoryBuffer(void *dst, const CertificateChain *chain);

bool certRetrieveCertificateByName(Certificate *dst, const char *name)
{
    if (!dst || !name || !*name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_esCertSaveMutex)
    {
        if (!certOpenEsCertSaveFile()) break;
        ret = _certRetrieveCertificateByName(dst, name);
        certCloseEsCertSaveFile();
    }

    return ret;
}

bool certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer)
{
    if (!dst || !issuer || strncmp(issuer, "Root-", 5) != 0)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;

    SCOPED_LOCK(&g_esCertSaveMutex)
    {
        if (!certOpenEsCertSaveFile()) break;
        ret = _certRetrieveCertificateChainBySignatureIssuer(dst, issuer);
        certCloseEsCertSaveFile();
    }

    return ret;
}

u8 *certGenerateRawCertificateChainBySignatureIssuer(const char *issuer, u64 *out_size)
{
    if (!issuer || !*issuer || !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    CertificateChain chain = {0};
    u8 *raw_chain = NULL;

    /* Get full certificate chain using the provided issuer. */
    if (!certRetrieveCertificateChainBySignatureIssuer(&chain, issuer))
    {
        LOG_MSG_ERROR("Error retrieving certificate chain for \"%s\"!", issuer);
        goto end;
    }

    /* Allocate memory for the raw certificate chain. */
    raw_chain = malloc(chain.size);
    if (!raw_chain)
    {
        LOG_MSG_ERROR("Unable to allocate memory for raw \"%s\" certificate chain! (0x%lX).", issuer, chain.size);
        goto end;
    }

    /* Copy all certificates to the allocated buffer. */
    certCopyCertificateChainDataToMemoryBuffer(raw_chain, &chain);

    /* Update output size. */
    *out_size = chain.size;

end:
    certFreeCertificateChain(&chain);

    return raw_chain;
}

u8 *certRetrieveRawCertificateChainFromGameCardByRightsId(const FsRightsId *id, u64 *out_size)
{
    if (!id || !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return NULL;
    }

    char raw_chain_filename[0x30] = {0};
    u64 raw_chain_offset = 0, raw_chain_size = 0;
    u8 *raw_chain = NULL;
    bool success = false;

    /* Generate certificate chain filename. */
    utilsGenerateHexStringFromData(raw_chain_filename, sizeof(raw_chain_filename), id->c, sizeof(id->c), false);
    strcat(raw_chain_filename, ".cert");

    /* Get certificate chain entry info. */
    if (!gamecardGetHashFileSystemEntryInfoByName(HashFileSystemPartitionType_Secure, raw_chain_filename, &raw_chain_offset, &raw_chain_size))
    {
        LOG_MSG_ERROR("Error retrieving offset and size for \"%s\" entry in secure hash FS partition!", raw_chain_filename);
        goto end;
    }

    /* Validate certificate chain size. */
    if (raw_chain_size < SIGNED_CERT_MIN_SIZE)
    {
        LOG_MSG_ERROR("Invalid size for \"%s\"! (0x%lX).", raw_chain_filename, raw_chain_size);
        goto end;
    }

    /* Allocate memory for the certificate chain. */
    raw_chain = malloc(raw_chain_size);
    if (!raw_chain)
    {
        LOG_MSG_ERROR("Unable to allocate memory for raw \"%s\" certificate chain! (0x%lX).", raw_chain_filename, raw_chain_size);
        goto end;
    }

    /* Read raw certificate chain from the inserted gamecard. */
    if (!gamecardReadStorage(raw_chain, raw_chain_size, raw_chain_offset))
    {
        LOG_MSG_ERROR("Failed to read \"%s\" data from the inserted gamecard!", raw_chain_filename);
        goto end;
    }

    /* Update output. */
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
        LOG_MSG_ERROR("Failed to open ES certificate system savefile!");
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
    if (!g_esCertSaveCtx || !dst || !name || !*name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u64 cert_size = 0;
    char cert_path[SAVE_FS_LIST_MAX_NAME_LENGTH] = {0};
    allocation_table_storage_ctx_t fat_storage = {0};

    /* Generate path for the requested certificate within the ES certificate save file. */
    snprintf(cert_path, MAX_ELEMENTS(cert_path), CERT_SAVEFILE_STORAGE_BASE_PATH "%s", name);

    /* Get FAT storage info for the certificate. */
    if (!save_get_fat_storage_from_file_entry_by_path(g_esCertSaveCtx, cert_path, &fat_storage, &cert_size))
    {
        LOG_MSG_ERROR("Failed to locate certificate \"%s\" in ES certificate system save!", name);
        return false;
    }

    /* Validate certificate size. */
    if (cert_size < SIGNED_CERT_MIN_SIZE || cert_size > SIGNED_CERT_MAX_SIZE)
    {
        LOG_MSG_ERROR("Invalid size for certificate \"%s\"! (0x%lX).", name, cert_size);
        return false;
    }

    /* Update output size. */
    dst->size = cert_size;

    /* Read certificate data. */
    u64 br = save_allocation_table_storage_read(&fat_storage, dst->data, 0, dst->size);
    if (br != dst->size)
    {
        LOG_MSG_ERROR("Failed to read 0x%lX bytes from certificate \"%s\"! Read 0x%lX bytes.", dst->size, name, br);
        return false;
    }

    /* Get certificate type. */
    dst->type = certGetCertificateType(dst->data, dst->size);
    if (dst->type == CertType_None || dst->type >= CertType_Count)
    {
        LOG_MSG_ERROR("Invalid certificate type for \"%s\"!", name);
        return false;
    }

    return true;
}

static u8 certGetCertificateType(void *data, u64 data_size)
{
    if (!data || data_size < SIGNED_CERT_MIN_SIZE || data_size > SIGNED_CERT_MAX_SIZE)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return CertType_None;
    }

    u32 sig_type = 0, pub_key_type = 0;
    u8 type = CertType_None;

    /* Get signature and public key types. */
    sig_type = signatureGetTypeFromSignedBlob(data, true);
    pub_key_type = certGetPublicKeyTypeFromSignedCertBlob(data);

    if (!signatureIsValidType(sig_type) || !certIsValidPublicKeyType(pub_key_type))
    {
        LOG_MSG_ERROR("Input buffer doesn't hold a valid signed certificate!");
        goto end;
    }

    /* Determine certificate type. */
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

end:
    return type;
}

static bool _certRetrieveCertificateChainBySignatureIssuer(CertificateChain *dst, const char *issuer)
{
    if (!dst || !issuer || !*issuer)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u32 i = 0;
    char issuer_copy[0x40] = {0}, *pch = NULL, *state = NULL;
    bool success = false;

    /* Free output context beforehand. */
    certFreeCertificateChain(dst);

    /* Determine how many certificate we need to retrieve. */
    dst->count = certGetCertificateCountInSignatureIssuer(issuer);
    if (!dst->count)
    {
        LOG_MSG_ERROR("Invalid signature issuer string!");
        goto end;
    }

    /* Allocate memory for all the certificates. */
    dst->certs = calloc(dst->count, sizeof(Certificate));
    if (!dst->certs)
    {
        LOG_MSG_ERROR("Unable to allocate memory for the certificate chain! (0x%lX).", dst->count * sizeof(Certificate));
        goto end;
    }

    /* Copy string to avoid problems with strtok_r(). */
    /* The "Root-" parent from the issuer string is skipped. */
    snprintf(issuer_copy, sizeof(issuer_copy), "%s", issuer + 5);

    /* Collect all required certificates. */
    pch = strtok_r(issuer_copy, "-", &state);
    while(pch)
    {
        /* Get current certificate. */
        if (!_certRetrieveCertificateByName(&(dst->certs[i]), pch))
        {
            LOG_MSG_ERROR("Unable to retrieve certificate \"%s\"!", pch);
            goto end;
        }

        /* Update certificate chain size and current index. */
        dst->size += dst->certs[i++].size;

        /* Get name for the next certificate. */
        pch = strtok_r(NULL, "-", &state);
    }

    /* Update output value. */
    success = true;

end:
    if (!success) certFreeCertificateChain(dst);

    return success;
}

static u32 certGetCertificateCountInSignatureIssuer(const char *issuer)
{
    if (!issuer || !*issuer) return 0;

    u32 count = 0;
    char issuer_copy[0x40] = {0}, *pch = NULL, *state = NULL;

    /* Copy string to avoid problems with strtok_r(). */
    /* The "Root-" parent from the issuer string is skipped. */
    snprintf(issuer_copy, sizeof(issuer_copy), "%s", issuer + 5);

    pch = strtok_r(issuer_copy, "-", &state);
    while(pch)
    {
        count++;
        pch = strtok_r(NULL, "-", &state);
    }

    return count;
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
