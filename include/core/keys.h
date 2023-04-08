/*
 * keys.h
 *
 * Copyright (c) 2018-2020, SciresM.
 * Copyright (c) 2019, shchmue.
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

#pragma once

#ifndef __KEYS_H__
#define __KEYS_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Loads (and derives) keydata from sysmodule program memory, the Lockpick_RCM keys file and hardcoded/obfuscated information.
/// Must be called (and succeed) before calling any of the functions below.
bool keysLoadKeyset(void);

/// Returns a pointer to the AES-128-XTS NCA header key, or NULL if keydata hasn't been loaded.
const u8 *keysGetNcaHeaderKey(void);

/// Returns a pointer to an AES-128-ECB NCA key area encryption key using the provided key area encryption key index and key generation values, or NULL if keydata hasn't been loaded.
const u8 *keysGetNcaKeyAreaEncryptionKey(u8 kaek_index, u8 key_generation);

/// Decrypts a RSA-OAEP wrapped titlekey using console-specific keydata.
/// 'rsa_wrapped_titlekey' must have a size of at least 0x100 bytes. 'out_titlekey' must have a size of at least AES_128_KEY_SIZE.
/// Returns false if an error occurs or if keydata hasn't been loaded.
bool keysDecryptRsaOaepWrappedTitleKey(const void *rsa_wrapped_titlekey, void *out_titlekey);

/// Returns a pointer to an AES-128-ECB ticket common key using the provided key generation value, or NULL if keydata hasn't been loaded.
const u8 *keysGetTicketCommonKey(u8 key_generation);

/// Returns a pointer to the AES-128-CBC CardInfo area key for gamecard headers, or NULL if keydata hasn't been loaded.
const u8 *keysGetGameCardInfoKey(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEYS_H__ */
