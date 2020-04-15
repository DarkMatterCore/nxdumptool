#pragma once

#ifndef __SERVICES_H__
#define __SERVICES_H__

/* Hardware clocks expressed in MHz */
#define CPU_CLKRT_NORMAL        1020
#define CPU_CLKRT_OVERCLOCKED   1785
#define MEM_CLKRT_NORMAL        1331
#define MEM_CLKRT_OVERCLOCKED   1600

bool servicesInitialize();
void servicesClose();

bool servicesCheckRunningServiceByName(const char *name);
bool servicesCheckInitializedServiceByName(const char *name);

void servicesChangeHardwareClockRates(u32 cpu_rate, u32 mem_rate);

#endif /* __SERVICES_H__ */
