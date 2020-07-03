/*
 * es.h
 *
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

#ifndef __ES_H__
#define __ES_H__

Result esInitialize(void);
void esExit(void);

Result esCountCommonTicket(s32 *out_count);
Result esCountPersonalizedTicket(s32 *out_count);
Result esListCommonTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count);
Result esListPersonalizedTicket(s32 *out_entries_written, FsRightsId *out_ids, s32 count);

#endif /* __ES_H__ */
