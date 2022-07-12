/*
 * rsa.c
 *
 * Copyright (c) 2018-2019, SciresM.
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
#include "rsa.h"

#include <mbedtls/rsa.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/pk.h>

bool rsa2048VerifySha256BasedPssSignature(const void *data, size_t data_size, const void *signature, const void *modulus, const void *public_exponent, size_t public_exponent_size)
{
    if (!data || !data_size || !signature || !modulus || !public_exponent || !public_exponent_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    int mbedtls_ret = 0;
    mbedtls_rsa_context rsa;
    u8 hash[SHA256_HASH_SIZE] = {0};
    bool ret = false;

    /* Initialize RSA context. */
    mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

    /* Import RSA parameters. */
    mbedtls_ret = mbedtls_rsa_import_raw(&rsa, (const u8*)modulus, RSA2048_BYTES, NULL, 0, NULL, 0, NULL, 0, (const u8*)public_exponent, public_exponent_size);
    if (mbedtls_ret != 0)
    {
        LOG_MSG_ERROR("mbedtls_rsa_import_raw failed! (%d).", mbedtls_ret);
        goto end;
    }

    /* Calculate SHA-256 checksum for the input data. */
    sha256CalculateHash(hash, data, data_size);

    /* Verify signature. */
    mbedtls_ret = mbedtls_rsa_rsassa_pss_verify(&rsa, NULL, NULL, MBEDTLS_RSA_PUBLIC, MBEDTLS_MD_SHA256, SHA256_HASH_SIZE, hash, (const u8*)signature);
    if (mbedtls_ret != 0)
    {
        LOG_MSG_ERROR("mbedtls_rsa_rsassa_pss_verify failed! (%d).", mbedtls_ret);
        goto end;
    }

    ret = true;

end:
    mbedtls_rsa_free(&rsa);

    return ret;
}

bool rsa2048OaepDecrypt(void *dst, size_t dst_size, const void *signature, const void *modulus, const void *public_exponent, size_t public_exponent_size, const void *private_exponent, \
                        size_t private_exponent_size, const void *label, size_t label_size, size_t *out_size)
{
    if (!dst || !dst_size || !signature || !modulus || !public_exponent || !public_exponent_size || !private_exponent || !private_exponent_size || (!label && label_size) || (label && !label_size) || \
        !out_size)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_rsa_context rsa;

    const char *pers = __func__;
    int mbedtls_ret = 0;
    bool ret = false;

    /* Initialize contexts. */
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    mbedtls_rsa_init(&rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);

    /* Seed the random number generator. */
    mbedtls_ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const u8*)pers, strlen(pers));
    if (mbedtls_ret != 0)
    {
        LOG_MSG_ERROR("mbedtls_ctr_drbg_seed failed! (%d).", mbedtls_ret);
        goto end;
    }

    /* Import RSA parameters. */
    mbedtls_ret = mbedtls_rsa_import_raw(&rsa, (const u8*)modulus, RSA2048_BYTES, NULL, 0, NULL, 0, (const u8*)private_exponent, private_exponent_size, (const u8*)public_exponent, public_exponent_size);
    if (mbedtls_ret != 0)
    {
        LOG_MSG_ERROR("mbedtls_rsa_import_raw failed! (%d).", mbedtls_ret);
        goto end;
    }

    /* Derive RSA prime factors. */
    mbedtls_ret = mbedtls_rsa_complete(&rsa);
    if (mbedtls_ret != 0)
    {
        LOG_MSG_ERROR("mbedtls_rsa_complete failed! (%d).", mbedtls_ret);
        goto end;
    }

    /* Perform RSA-OAEP decryption. */
    mbedtls_ret = mbedtls_rsa_rsaes_oaep_decrypt(&rsa, mbedtls_ctr_drbg_random, &ctr_drbg, MBEDTLS_RSA_PRIVATE, (const u8*)label, label_size, out_size, (const u8*)signature, (u8*)dst, dst_size);
    if (mbedtls_ret != 0)
    {
        LOG_MSG_ERROR("mbedtls_rsa_rsaes_oaep_decrypt failed! (%d).", mbedtls_ret);
        goto end;
    }

    ret = true;

end:
    mbedtls_rsa_free(&rsa);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    return ret;
}
