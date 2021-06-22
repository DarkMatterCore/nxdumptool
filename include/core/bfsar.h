/*
 * bfsar.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __BFSAR_H__
#define __BFSAR_H__

#ifdef __cplusplus
extern "C" {
#endif

/// Initializes the BFSAR interface.
bool bfsarInitialize(void);

/// Closes the BFSAR interface.
void bfsarExit(void);

/// Returns a pointer to the BFSAR file path.
const char *bfsarGetFilePath(void);

#ifdef __cplusplus
}
#endif

#endif /* __BFSAR_H__ */
