/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "services.h"
#include "es.h"
#include "fspusb.h"
#include "utils.h"

/* Type definitions. */

typedef bool (*ServiceCondFunction)(void *arg);         /* Used to perform a runtime condition check (e.g. system version) before initializing the service */
typedef Result (*ServiceInitFunction)(void);            /* Used to initialize the service */
typedef void (*ServiceCloseFunction)(void);             /* Used to close the service */

typedef struct ServicesInfoEntry {
    bool initialized;
    char name[8];
    ServiceCondFunction cond_func;
    ServiceInitFunction init_func;
    ServiceCloseFunction close_func;
} ServicesInfoEntry;

/* Function prototypes. */

static Result servicesNifmUserInitialize(void);
static bool servicesClkGetServiceType(void *arg);
static bool servicesSplCryptoCheckAvailability(void *arg);
static bool servicesFspUsbCheckAvailability(void *arg);

/* Global variables. */

static ServicesInfoEntry g_serviceInfo[] = {
    { false, "ncm", NULL, &ncmInitialize, &ncmExit },
    { false, "ns", NULL, &nsInitialize, &nsExit },
    { false, "csrng", NULL, &csrngInitialize, &csrngExit },
    { false, "spl", NULL, &splInitialize, &splExit },
    { false, "spl:mig", &servicesSplCryptoCheckAvailability, &splCryptoInitialize, &splCryptoExit },    /* Checks if spl:mig is really available (e.g. avoid calling splInitialize twice) */
    { false, "pm:dmnt", NULL, &pmdmntInitialize, &pmdmntExit },
    { false, "pl", NULL, &plInitialize, &plExit },
    { false, "psm", NULL, &psmInitialize, &psmExit },
    { false, "nifm:u", NULL, &servicesNifmUserInitialize, &nifmExit },
    { false, "clk", &servicesClkGetServiceType, NULL, NULL },                                           /* Placeholder for pcv / clkrst */
    { false, "fsp-usb", &servicesFspUsbCheckAvailability, &fspusbInitialize, &fspusbExit },             /* Checks if fsp-usb is really available */
    { false, "es", NULL, &esInitialize, &esExit },
    { false, "set:cal", NULL, &setcalInitialize, &setcalExit },
    { false, "usb:ds", NULL, &usbCommsInitialize, &usbCommsExit }
};

static const u32 g_serviceInfoCount = MAX_ELEMENTS(g_serviceInfo);

static bool g_clkSvcUsePcv = false;
static ClkrstSession g_clkrstCpuSession = {0}, g_clkrstMemSession = {0};

static Mutex g_servicesMutex = 0;

bool servicesInitialize(void)
{
    mutexLock(&g_servicesMutex);
    
    Result rc = 0;
    bool ret = true;
    
    for(u32 i = 0; i < g_serviceInfoCount; i++)
    {
        /* Check if this service has been already initialized */
        if (g_serviceInfo[i].initialized) continue;
        
        /* Check if this service depends on a condition function */
        if (g_serviceInfo[i].cond_func != NULL)
        {
            /* Run the condition function - it will update the current service member */
            /* Skip this service if the required conditions aren't met */
            if (!g_serviceInfo[i].cond_func(&(g_serviceInfo[i]))) continue;
        }
        
        /* Check if this service has a valid initialize function  */
        if (g_serviceInfo[i].init_func == NULL) continue;
        
        /* Initialize service */
        rc = g_serviceInfo[i].init_func();
        if (R_FAILED(rc))
        {
            LOGFILE("Failed to initialize %s service! (0x%08X)", g_serviceInfo[i].name, rc);
            ret = false;
            break;
        }
        
        /* Update initialized flag */
        g_serviceInfo[i].initialized = true;
    }
    
    mutexUnlock(&g_servicesMutex);
    
    return ret;
}

void servicesClose(void)
{
    mutexLock(&g_servicesMutex);
    
    for(u32 i = 0; i < g_serviceInfoCount; i++)
    {
        /* Check if this service has not been initialized, or if it doesn't have a valid close function */
        if (!g_serviceInfo[i].initialized || g_serviceInfo[i].close_func == NULL) continue;
        
        /* Close service */
        g_serviceInfo[i].close_func();
    }
    
    mutexUnlock(&g_servicesMutex);
}

bool servicesCheckRunningServiceByName(const char *name)
{
    if (!name || !strlen(name)) return false;
    
    Handle handle = INVALID_HANDLE;
    SmServiceName service_name = smEncodeName(name);
    Result rc = smRegisterService(&handle, service_name, false, 1);
    bool running = R_FAILED(rc);
    
    if (handle != INVALID_HANDLE) svcCloseHandle(handle);
    
    if (!running) smUnregisterService(service_name);
    
    return running;
}

bool servicesCheckInitializedServiceByName(const char *name)
{
    mutexLock(&g_servicesMutex);
    
    bool ret = false;
    
    if (!name || !strlen(name)) goto exit;
    
    size_t name_len = strlen(name);
    
    for(u32 i = 0; i < g_serviceInfoCount; i++)
    {
        if (!strncmp(g_serviceInfo[i].name, name, name_len))
        {
            ret = g_serviceInfo[i].initialized;
            break;
        }
    }
    
exit:
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

static Result servicesNifmUserInitialize(void)
{
    return nifmInitialize(NifmServiceType_User);
}

static Result servicesClkrstInitialize(void)
{
    Result rc = 0;
    
    /* Open clkrst service handle */
    rc = clkrstInitialize();
    if (R_FAILED(rc)) return rc;
    
    /* Initialize CPU and MEM clkrst sessions */
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
    /* Close CPU and MEM clkrst sessions */
    clkrstCloseSession(&g_clkrstMemSession);
    clkrstCloseSession(&g_clkrstCpuSession);
    
    /* Close clkrst service handle */
    clkrstExit();
}

static bool servicesClkGetServiceType(void *arg)
{
    if (!arg) return false;
    
    ServicesInfoEntry *info = (ServicesInfoEntry*)arg;
    if (!strlen(info->name) || strncmp(info->name, "clk", 3) != 0 || info->init_func != NULL || info->close_func != NULL) return false;
    
    /* Determine which service needs to be used to control hardware clock rates, depending on the system version */
    /* This may either be pcv (sysver lower than 8.0.0) or clkrst (sysver equal to or greater than 8.0.0) */
    g_clkSvcUsePcv = hosversionBefore(8, 0, 0);
    
    /* Fill service info */
    sprintf(info->name, "%s", (g_clkSvcUsePcv ? "pcv" : "clkrst"));
    info->init_func = (g_clkSvcUsePcv ? &pcvInitialize : &servicesClkrstInitialize);
    info->close_func = (g_clkSvcUsePcv ? &pcvExit : &servicesClkrstExit);
    
    return true;
}

static bool servicesSplCryptoCheckAvailability(void *arg)
{
    if (!arg) return false;
    
    ServicesInfoEntry *info = (ServicesInfoEntry*)arg;
    if (!strlen(info->name) || strncmp(info->name, "spl:mig", 7) != 0 || info->init_func == NULL || info->close_func == NULL) return false;
    
    /* Check if spl:mig is available (sysver equal to or greater than 4.0.0) */
    return !hosversionBefore(4, 0, 0);
}

static bool servicesFspUsbCheckAvailability(void *arg)
{
    if (!arg) return false;
    
    ServicesInfoEntry *info = (ServicesInfoEntry*)arg;
    if (!strlen(info->name) || strncmp(info->name, "fsp-usb", 7) != 0 || info->init_func == NULL || info->close_func == NULL) return false;
    
    /* Check if fsp-usb is actually running in the background */
    return servicesCheckRunningServiceByName("fsp-usb");
}
