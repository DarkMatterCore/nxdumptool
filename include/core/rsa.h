/*
 * rsa.c
 *
 * Copyright (c) 2018-2019, SciresM.
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

#pragma once

#ifndef __RSA_H__
#define __RSA_H__

#ifdef __cplusplus
extern "C" {
#endif

#define RSA2048_BYTES       0x100
#define RSA2048_BITS        (RSA2048_BYTES * 8)

#define RSA2048_SIG_SIZE    RSA2048_BYTES
#define RSA2048_PUBKEY_SIZE RSA2048_BYTES

/// Verifies a RSA-2048-PSS with SHA-256 signature.
/// Suitable for NCA and NPDM signatures.
/// The provided signature and modulus must have sizes of at least RSA2048_SIG_SIZE and RSA2048_PUBKEY_SIZE, respectively.
bool rsa2048VerifySha256BasedPssSignature(const void *data, size_t data_size, const void *signature, const void *modulus, const void *public_exponent, size_t public_exponent_size);

/// Verifies a RSA-2048-PKCS#1 v1.5 with SHA-256 signature.
/// Suitable for ticket and certificate chain signatures.
/// The provided signature and modulus must have sizes of at least RSA2048_SIG_SIZE and RSA2048_PUBKEY_SIZE, respectively.
bool rsa2048VerifySha256BasedPkcs1v15Signature(const void *data, size_t data_size, const void *signature, const void *modulus, const void *public_exponent, size_t public_exponent_size);

/// Performs RSA-2048-OAEP decryption.
/// Suitable to decrypt the titlekey block from personalized tickets.
/// The provided signature and modulus must have sizes of at least RSA2048_SIG_SIZE and RSA2048_PUBKEY_SIZE, respectively.
/// 'label' and 'label_size' arguments are optional -- if not needed, these may be set to NULL and 0, respectively.
bool rsa2048OaepDecrypt(void *dst, size_t dst_size, const void *signature, const void *modulus, const void *public_exponent, size_t public_exponent_size, const void *private_exponent, \
                        size_t private_exponent_size, const void *label, size_t label_size, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* __RSA_H__ */
