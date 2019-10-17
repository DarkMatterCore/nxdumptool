#include <stdio.h>
#include <string.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/md.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509.h>

#include "rsa.h"
#include "rsa_keys.h"
#include "ui.h"
#include "util.h"

/* Extern variables */

extern int breaks;
extern int font_height;

bool rsa_sign(void* input, size_t input_size, unsigned char* output, size_t output_size)
{
    unsigned char hash[32];
    unsigned char buf[MBEDTLS_MPI_MAX_SIZE];
    const char *pers = "rsa_sign_pss";
    size_t olen = 0;
    
    int ret;
    bool success = false;
    
    mbedtls_pk_context pk;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    
    mbedtls_entropy_init(&entropy);
    mbedtls_pk_init(&pk);
    mbedtls_ctr_drbg_init(&ctr_drbg);
    
    // Calculate SHA-256 checksum for the input data
    sha256CalculateHash(hash, input, input_size);
    
    // Seed the random number generator
    ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)pers, strlen(pers));
    if (ret == 0)
    {
        // Parse private key
        ret = mbedtls_pk_parse_key(&pk, (unsigned char*)rsa_private_key, strlen(rsa_private_key) + 1, NULL, 0);
        if (ret == 0)
        {
            // Set RSA padding
            mbedtls_rsa_set_padding(mbedtls_pk_rsa(pk), MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
            
            // Calculate hash signature
            ret = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA256, hash, 0, buf, &olen, mbedtls_ctr_drbg_random, &ctr_drbg);
            if (ret == 0)
            {
                // Copy signature to output
                memcpy(output, buf, output_size);
                success = true;
            } else {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "rsa_sign: mbedtls_pk_sign failed! (%d)", ret);
            }
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "rsa_sign: mbedtls_pk_parse_key failed! (%d)", ret);
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "rsa_sign: mbedtls_ctr_drbg_seed failed! (%d)", ret);
    }
    
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_pk_free(&pk);
    mbedtls_entropy_free(&entropy);
    
    return success;
}

const unsigned char *rsa_get_public_key()
{
    return rsa_public_key;
}
