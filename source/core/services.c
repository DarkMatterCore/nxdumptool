/*
 * services.c
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

#include "nxdt_utils.h"
#include "services.h"
#include "es.h"

/* Type definitions. */

typedef bool (*ServiceCondFunction)(void *arg); /* Used to perform a runtime condition check (e.g. system version) before initializing the service. */
typedef Result (*ServiceInitFunction)(void);    /* Used to initialize the service. */
typedef void (*ServiceCloseFunction)(void);     /* Used to close the service. */

typedef struct {
    bool initialized;
    char name[8];
    ServiceCondFunction cond_func;
    ServiceInitFunction init_func;
    ServiceCloseFunction close_func;
} ServiceInfo;

/* Function prototypes. */

static Result servicesAtmosphereHasService(bool *out, SmServiceName name);
static bool servicesGetExosphereApiVersion(u32 *out);

static Result servicesNifmUserInitialize(void);
static bool servicesClkGetServiceType(void *arg);
static bool servicesSplCryptoCheckAvailability(void *arg);

/* Global variables. */

static ServiceInfo g_serviceInfo[] = {
    { false, "ncm", NULL, &ncmInitialize, &ncmExit },
    { false, "ns", NULL, &nsInitialize, &nsExit },
    { false, "csrng", NULL, &csrngInitialize, &csrngExit },
    { false, "spl:", NULL, &splInitialize, &splExit },
    { false, "spl:mig", &servicesSplCryptoCheckAvailability, &splCryptoInitialize, &splCryptoExit },    /* Checks if spl:mig is really available (e.g. avoid calling splInitialize twice). */
    { false, "pm:dmnt", NULL, &pmdmntInitialize, &pmdmntExit },
    { false, "psm", NULL, &psmInitialize, &psmExit },
    { false, "nifm:u", NULL, &servicesNifmUserInitialize, &nifmExit },
    { false, "clk", &servicesClkGetServiceType, NULL, NULL },                                           /* Placeholder for pcv / clkrst. */
    { false, "es", NULL, &esInitialize, &esExit },
    { false, "set", NULL, &setInitialize, &setExit },
    { false, "set:sys", NULL, &setsysInitialize, &setsysExit },
    { false, "set:cal", NULL, &setcalInitialize, &setcalExit }
};

static const u32 g_serviceInfoCount = MAX_ELEMENTS(g_serviceInfo);

static bool g_clkSvcUsePcv = false;
static ClkrstSession g_clkrstCpuSession = {0}, g_clkrstMemSession = {0};

static Mutex g_servicesMutex = 0;

/* Atmosphère-related constants. */
static const u32 g_smAtmosphereHasService = 65100;
static const SplConfigItem SplConfigItem_ExosphereApiVersion = (SplConfigItem)65000;
static const u32 g_atmosphereTipcVersion = MAKEHOSVERSION(0, 19, 0);

bool servicesInitialize(void)
{
    mutexLock(&g_servicesMutex);
    
    Result rc = 0;
    bool ret = true;
    
    for(u32 i = 0; i < g_serviceInfoCount; i++)
    {
        ServiceInfo *service_info = &(g_serviceInfo[i]);
        
        /* Check if this service has been already initialized or if it actually has a valid initialize function. */
        if (service_info->initialized || service_info->init_func == NULL) continue;
        
        /* Check if this service depends on a condition function. */
        if (service_info->cond_func != NULL)
        {
            /* Run the condition function - it will update the current service member. */
            /* Skip this service if the required conditions aren't met. */
            if (!service_info->cond_func(service_info)) continue;
        }
        
        /* Initialize service. */
        rc = service_info->init_func();
        if (R_FAILED(rc))
        {
            LOG_MSG("Failed to initialize %s service! (0x%08X).", service_info->name, rc);
            ret = false;
            break;
        }
        
        /* Update initialized flag. */
        service_info->initialized = true;
    }
    
    mutexUnlock(&g_servicesMutex);
    
    return ret;
}

void servicesClose(void)
{
    mutexLock(&g_servicesMutex);
    
    for(u32 i = 0; i < g_serviceInfoCount; i++)
    {
        ServiceInfo *service_info = &(g_serviceInfo[i]);
        
        /* Check if this service has not been initialized, or if it doesn't have a valid close function. */
        if (!service_info->initialized || service_info->close_func == NULL) continue;
        
        /* Close service. */
        service_info->close_func();
        
        /* Update initialized flag. */
        service_info->initialized = false;
    }
    
    mutexUnlock(&g_servicesMutex);
}

bool servicesCheckRunningServiceByName(const char *name)
{
    if (!name || !*name)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    bool out = false;
    SmServiceName service_name = smEncodeName(name);
    
    rc = servicesAtmosphereHasService(&out, service_name);
    if (R_FAILED(rc)) LOG_MSG("servicesAtmosphereHasService failed for \"%s\"! (0x%08X).", name, rc);
    
    return out;
}

bool servicesCheckInitializedServiceByName(const char *name)
{
    mutexLock(&g_servicesMutex);
    
    bool ret = false;
    
    if (!name || !*name) goto end;
    
    for(u32 i = 0; i < g_serviceInfoCount; i++)
    {
        ServiceInfo *service_info = &(g_serviceInfo[i]);
        
        if (!strcmp(service_info->name, name))
        {
            ret = service_info->initialized;
            break;
        }
    }
    
end:
    mutexUnlock(&g_servicesMutex);
    
    return ret;
}

void servicesChangeHardwareClockRates(u32 cpu_rate, u32 mem_rate)
{
    mutexLock(&g_servicesMutex);
    
    if (g_clkSvcUsePcv)
    {
        pcvSetClockRate(PcvModule_CpuBus, cpu_rate);
        pcvSetClockRate(PcvModule_EMC, mem_rate);
    } else {
        clkrstSetClockRate(&g_clkrstCpuSession, cpu_rate);
        clkrstSetClockRate(&g_clkrstMemSession, mem_rate);
    }
    
    mutexUnlock(&g_servicesMutex);
}

/* SM API extension available in Atmosphère and Atmosphère-based CFWs. */
static Result servicesAtmosphereHasService(bool *out, SmServiceName name)
{
    u8 tmp = 0;
    Result rc = 0;
    u32 version = 0;
    
    /* Check if service is running. */
    /* Dispatch IPC request using CMIF or TIPC serialization depending on our current environment. */
    if (hosversionAtLeast(12, 0, 0) || (servicesGetExosphereApiVersion(&version) && version >= g_atmosphereTipcVersion))
    {
        rc = tipcDispatchInOut(smGetServiceSessionTipc(), g_smAtmosphereHasService, name, tmp);
    } else {
        rc = serviceDispatchInOut(smGetServiceSession(), g_smAtmosphereHasService, name, tmp);
    }
    
    if (R_SUCCEEDED(rc) && out) *out = tmp;
    
    return rc;
}

/* SMC API extension available in Atmosphère and Atmosphère-based CFWs. */
static bool servicesGetExosphereApiVersion(u32 *out)
{
    if (!out)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 version = 0;
    
    rc = splGetConfig(SplConfigItem_ExosphereApiVersion, &version);
    if (R_FAILED(rc))
    {
        LOG_MSG("splGetConfig failed! (0x%08X).", rc);
        return false;
    }
    
    *out = (u32)((version >> 40) & 0xFFFFFF);
    
    return true;
}

static Result servicesNifmUserInitialize(void)
{
    return nifmInitialize(NifmServiceType_User);
}

static Result servicesClkrstInitialize(void)
{
    Result rc = 0;
    
    /* Open clkrst service handle. */
    rc = clkrstInitialize();
    if (R_FAILED(rc)) return rc;
    
    /* Initialize CPU and MEM clkrst sessions. */
    memset(&g_clkrstCpuSession, 0, sizeof(ClkrstSession));
    memset(&g_clkrstMemSession, 0, sizeof(ClkrstSession));
    
    rc = clkrstOpenSession(&g_clkrstCpuSession, PcvModuleId_CpuBus, 3);
    if (R_FAILED(rc))
    {
        clkrstExit();
        return rc;
    }
    
    rc = clkrstOpenSession(&g_clkrstMemSession, PcvModuleId_EMC, 3);
    if (R_FAILED(rc))
    {
        clkrstCloseSession(&g_clkrstCpuSession);
        clkrstExit();
    }
    
    return rc;
}

static void servicesClkrstExit(void)
{
    /* Close CPU and MEM clkrst sessions. */
    clkrstCloseSession(&g_clkrstMemSession);
    clkrstCloseSession(&g_clkrstCpuSession);
    
    /* Close clkrst service handle. */
    clkrstExit();
}

static bool servicesClkGetServiceType(void *arg)
{
    if (!arg) return false;
    
    ServiceInfo *info = (ServiceInfo*)arg;
    if (strcmp(info->name, "clk") != 0 || info->init_func != NULL || info->close_func != NULL) return false;
    
    /* Determine which service needs to be used to control hardware clock rates, depending on the system version. */
    /* This may either be pcv (sysver lower than 8.0.0) or clkrst (sysver equal to or greater than 8.0.0). */
    g_clkSvcUsePcv = hosversionBefore(8, 0, 0);
    
    /* Fill service info. */
    sprintf(info->name, "%s", (g_clkSvcUsePcv ? "pcv" : "clkrst"));
    info->init_func = (g_clkSvcUsePcv ? &pcvInitialize : &servicesClkrstInitialize);
    info->close_func = (g_clkSvcUsePcv ? &pcvExit : &servicesClkrstExit);
    
    return true;
}

static bool servicesSplCryptoCheckAvailability(void *arg)
{
    if (!arg) return false;
    
    ServiceInfo *info = (ServiceInfo*)arg;
    if (strcmp(info->name, "spl:mig") != 0 || info->init_func == NULL || info->close_func == NULL) return false;
    
    /* Check if spl:mig is available (sysver equal to or greater than 4.0.0). */
    return !hosversionBefore(4, 0, 0);
}
