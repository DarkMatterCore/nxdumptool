/*
 * smc.h
 *
 * Copyright (c) Atmosphère-NX.
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

#ifndef __SMC_H__
#define __SMC_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SmcKeyType_Default           = 0,   ///< Also known as "aes_kek_generation_source".
    SmcKeyType_NormalOnly        = 1,
    SmcKeyType_RecoveryOnly      = 2,
    SmcKeyType_NormalAndRecovery = 3,
    SmcKeyType_Count             = 4
} SmcKeyType;

typedef enum {
    SmcSealKey_LoadAesKey                = 0,
    SmcSealKey_DecryptDeviceUniqueData   = 1,
    SmcSealKey_ImportLotusKey            = 2,
    SmcSealKey_ImportEsDeviceKey         = 3,
    SmcSealKey_ReencryptDeviceUniqueData = 4,
    SmcSealKey_ImportSslKey              = 5,
    SmcSealKey_ImportEsClientCertKey     = 6,
    SmcSealKey_Count                     = 7
} SmcSealKey;

typedef struct {
    union {
        u32 value;                      ///< Can be used with spl calls.
        struct {
            u32 is_device_unique : 1;
            u32 key_type_idx     : 4;   ///< SmcKeyType.
            u32 seal_key_idx     : 3;   ///< SmcSealKey.
            u32 reserved         : 24;
        } fields;
    };
} SmcGenerateAesKekOption;

/// Helper inline functions.

NX_INLINE bool smcPrepareGenerateAesKekOption(bool is_device_unique, u32 key_type_idx, u32 seal_key_idx, SmcGenerateAesKekOption *out)
{
    if (key_type_idx >= SmcKeyType_Count || seal_key_idx >= SmcSealKey_Count) return false;

    out->fields.is_device_unique = (u32)(is_device_unique & 1);
    out->fields.key_type_idx = key_type_idx;
    out->fields.seal_key_idx = seal_key_idx;
    out->fields.reserved = 0;

    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* __SMC_H__ */
