/*
 * rsa.c
 *
 * Copyright (c) 2018-2019, SciresM.
 * Copyright (c) 2018-2019, The-4n.
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
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

#pragma once

#ifndef __RSA_H__
#define __RSA_H__

#define RSA2048_SIGNATURE_SIZE  0x100

bool rsa2048GenerateSha256BasedCustomAcidSignature(void *dst, const void *src, size_t size);
const u8 *rsa2048GetCustomAcidPublicKey(void);

bool rsa2048OaepDecryptAndVerify(void *dst, size_t dst_size, const void *signature, const void *modulus, const void *exponent, size_t exponent_size, const void *label_hash, size_t *out_size);

#endif /* __RSA_H__ */
