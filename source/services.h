/*
 * services.h
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __SERVICES_H__
#define __SERVICES_H__

/* Hardware clocks expressed in MHz. */
#define CPU_CLKRT_NORMAL        1020
#define CPU_CLKRT_OVERCLOCKED   1785
#define MEM_CLKRT_NORMAL        1331
#define MEM_CLKRT_OVERCLOCKED   1600

/// Initializes the background services needed by the application.
bool servicesInitialize();

/// Closes services previously initialized by servicesInitialize().
void servicesClose();

/// Checks if a service is running by its name.
/// Uses the smRegisterService() call, which may crash under development units.
bool servicesCheckRunningServiceByName(const char *name);

/// Check if a service has been initialized by its name.
bool servicesCheckInitializedServiceByName(const char *name);

/// Changes CPU/MEM clock rates at runtime.
void servicesChangeHardwareClockRates(u32 cpu_rate, u32 mem_rate);

/// Wrapper for the "AtmosphereHasService" SM API extension from Atmosphère and Atmosphère-based CFWs.
/// Perfectly safe under development units. Not available in older Atmosphère releases.
Result servicesHasService(bool *out, const char *name);

#endif /* __SERVICES_H__ */

#ifdef __cplusplus
}
#endif