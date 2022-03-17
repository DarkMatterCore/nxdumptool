/*
 * es.h
 *
 * Copyright (c) 2018-2020, Addubz.
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

#ifndef __ES_H__
#define __ES_H__

#ifdef __cplusplus
extern "C" {
#endif

Result esInitialize(void);
void esExit(void);

Result esCountCommonTicket(s32 *out_count);
Result esCountPersonalizedTicket(s32 *out_count);
Result esListCommonTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count);
Result esListPersonalizedTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count);

#ifdef __cplusplus
}
#endif

#endif /* __ES_H__ */
