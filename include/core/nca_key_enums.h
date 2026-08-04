/*
 * nca_key_enums.h
 *
 * Copyright (c) 2020-2026, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __NCA_KEY_ENUMS_H__
#define __NCA_KEY_ENUMS_H__

#ifdef __cplusplus
extern "C" {
#endif

/// 'NcaKeyGeneration_Current' will always point to the last known key generation value.
/// TODO: update on master key changes.
typedef enum : u8 {
    NcaKeyGeneration_Since100NUP  = 0,                              ///< 1.0.0 - 2.3.0.
    NcaKeyGeneration_Since300NUP  = 2,                              ///< 3.0.0.
    NcaKeyGeneration_Since301NUP  = 3,                              ///< 3.0.1 - 3.0.2.
    NcaKeyGeneration_Since400NUP  = 4,                              ///< 4.0.0 - 4.1.0.
    NcaKeyGeneration_Since500NUP  = 5,                              ///< 5.0.0 - 5.1.0.
    NcaKeyGeneration_Since600NUP  = 6,                              ///< 6.0.0 - 6.1.0.
    NcaKeyGeneration_Since620NUP  = 7,                              ///< 6.2.0.
    NcaKeyGeneration_Since700NUP  = 8,                              ///< 7.0.0 - 8.0.1.
    NcaKeyGeneration_Since810NUP  = 9,                              ///< 8.1.0 - 8.1.1.
    NcaKeyGeneration_Since900NUP  = 10,                             ///< 9.0.0 - 9.0.1.
    NcaKeyGeneration_Since910NUP  = 11,                             ///< 9.1.0 - 12.0.3.
    NcaKeyGeneration_Since1210NUP = 12,                             ///< 12.1.0.
    NcaKeyGeneration_Since1300NUP = 13,                             ///< 13.0.0 - 13.2.1.
    NcaKeyGeneration_Since1400NUP = 14,                             ///< 14.0.0 - 14.1.2.
    NcaKeyGeneration_Since1500NUP = 15,                             ///< 15.0.0 - 15.0.1.
    NcaKeyGeneration_Since1600NUP = 16,                             ///< 16.0.0 - 16.1.0.
    NcaKeyGeneration_Since1700NUP = 17,                             ///< 17.0.0 - 17.0.1.
    NcaKeyGeneration_Since1800NUP = 18,                             ///< 18.0.0 - 18.1.0.
    NcaKeyGeneration_Since1900NUP = 19,                             ///< 19.0.0 - 19.0.1.
    NcaKeyGeneration_Since2000NUP = 20,                             ///< 20.0.0 - 20.5.0.
    NcaKeyGeneration_Since2100NUP = 21,                             ///< 21.0.0 - 21.2.0.
    NcaKeyGeneration_Since2200NUP = 22,                             ///< 22.0.0+.
    NcaKeyGeneration_Current      = NcaKeyGeneration_Since2200NUP,
    NcaKeyGeneration_Max          = 32
} NcaKeyGeneration;

typedef enum : u8 {
    NcaKeyAreaEncryptionKeyIndex_Application = 0,
    NcaKeyAreaEncryptionKeyIndex_Ocean       = 1,
    NcaKeyAreaEncryptionKeyIndex_System      = 2,
    NcaKeyAreaEncryptionKeyIndex_Count       = 3    ///< Total values supported by this enum.
} NcaKeyAreaEncryptionKeyIndex;

/// 'NcaSignatureKeyGeneration_Current' will always point to the last known key generation value.
/// TODO: update on signature keygen changes.
typedef enum : u8 {
    NcaSignatureKeyGeneration_Since100NUP = 0,                                      ///< 1.0.0 - 8.1.1.
    NcaSignatureKeyGeneration_Since900NUP = 1,                                      ///< 9.0.0+.
    NcaSignatureKeyGeneration_Current     = NcaSignatureKeyGeneration_Since900NUP,
    NcaSignatureKeyGeneration_Max         = (NcaSignatureKeyGeneration_Current + 1)
} NcaSignatureKeyGeneration;

#ifdef __cplusplus
}
#endif

#endif /* __NCA_KEY_ENUMS_H__ */
