/*
 * keys.h
 *
 * Copyright (c) 2018-2020, SciresM.
 * Copyright (c) 2019, shchmue.
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

#ifndef __KEYS_H__
#define __KEYS_H__

bool keysLoadNcaKeyset(void);

const u8 *keysGetNcaHeaderKey(void);
const u8 *keysGetKeyAreaEncryptionKeySource(u8 kaek_index);
const u8 *keysGetEticketRsaKek(void);
const u8 *keysGetTitlekek(u8 key_generation);
const u8 *keysGetKeyAreaEncryptionKey(u8 key_generation, u8 kaek_index);

#endif /* __KEYS_H__ */
