/*
 * services.h
 *
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

#ifndef __SERVICES_H__
#define __SERVICES_H__

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware clocks expressed in MHz. */
#define CPU_CLKRT_NORMAL        1020
#define CPU_CLKRT_OVERCLOCKED   1785
#define MEM_CLKRT_NORMAL        1331
#define MEM_CLKRT_OVERCLOCKED   1600

/// Initializes the background services needed by the application.
bool servicesInitialize();

/// Closes services previously initialized by servicesInitialize().
void servicesClose();

/// Check if a service has been initialized by this interface using its name.
bool servicesCheckInitializedServiceByName(const char *name);

/// Checks if a service is running using its name, even if it wasn't initialized by this interface.
/// This servers as a wrapper for the "AtmosphereHasService" SM API extension from Atmosphère and Atmosphère-based CFWs.
/// Perfectly safe to use under development units. Not available in older Atmosphère releases.
bool servicesCheckRunningServiceByName(const char *name);

/// Changes CPU/MEM clock rates at runtime.
void servicesChangeHardwareClockRates(u32 cpu_rate, u32 mem_rate);

#ifdef __cplusplus
}
#endif

#endif /* __SERVICES_H__ */
