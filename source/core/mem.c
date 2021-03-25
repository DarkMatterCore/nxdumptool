/*
 * mem.c
 *
 * Copyright (c) 2019, shchmue.
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

#include "utils.h"
#include "mem.h"

#define MEMLOG(fmt, ...)    LOG_MSG_BUF(&g_memLogBuf, &g_memLogBufSize, fmt, ##__VA_ARGS__)

/* Global variables. */

static Mutex g_memMutex = 0;

static char *g_memLogBuf = NULL;
static size_t g_memLogBufSize = 0;

/* Function prototypes. */

static bool memRetrieveProgramMemory(MemoryLocation *location, bool is_segment);
static bool memRetrieveDebugHandleFromProgramById(Handle *out, u64 program_id);

bool memRetrieveProgramMemorySegment(MemoryLocation *location)
{
    if (!location || !location->program_id || !location->mask || location->mask >= BIT(3))
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    mutexLock(&g_memMutex);
    bool ret = memRetrieveProgramMemory(location, true);
    mutexUnlock(&g_memMutex);
    
    return ret;
}

bool memRetrieveFullProgramMemory(MemoryLocation *location)
{
    if (!location || !location->program_id)
    {
        LOG_MSG("Invalid parameters!");
        return false;
    }
    
    mutexLock(&g_memMutex);
    bool ret = memRetrieveProgramMemory(location, false);
    mutexUnlock(&g_memMutex);
    
    return ret;
}

static bool memRetrieveProgramMemory(MemoryLocation *location, bool is_segment)
{
    Result rc = 0;
    Handle debug_handle = INVALID_HANDLE;
    
    MemoryInfo mem_info = {0};
    
    u32 page_info = 0;
    u64 addr = 0, last_text_addr = 0;
    u8 segment = 1, mem_type = 0;
    u8 *tmp = NULL;
    
    bool success = true;
    
    /* Clear output MemoryLocation element. */
    memFreeMemoryLocation(location);
    
    /* LOG_MSG() will be useless if the target program is the FS sysmodule. */
    /* This is because any FS I/O operation *will* lock up the console while FS itself is being debugged. */
    /* So we'll just temporarily log data to a char array using LOG_MSG_BUF(), then write it all out after calling svcCloseHandle(). */
    /* However, we must prevent other threads from logging data as well in order to avoid a lock up, so we'll temporarily lock the logfile mutex. */
    logControlMutex(true);
    
    /* Retrieve debug handle by program ID. */
    if (!memRetrieveDebugHandleFromProgramById(&debug_handle, location->program_id))
    {
        MEMLOG("Unable to retrieve debug handle for program %016lX!", location->program_id);
        goto end;
    }
    
    if (is_segment && location->program_id == FS_SYSMODULE_TID)
    {
        /* If dealing with FS, locate the "real" .text segment, since Atmosphère emuMMC has two. */
        do {
            rc = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
            if (R_FAILED(rc))
            {
                MEMLOG("svcQueryDebugProcessMemory failed for program %016lX! (0x%08X).", location->program_id, rc);
                success = false;
                goto end;
            }
            
            mem_type = (u8)(mem_info.type & 0xFF);
            if ((mem_info.perm & Perm_X) && (mem_type == MemType_CodeStatic || mem_type == MemType_CodeMutable)) last_text_addr = mem_info.addr;
            
            addr = (mem_info.addr + mem_info.size);
        } while(addr != 0);
        
        addr = last_text_addr;
    }
    
    do {
        rc = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
        if (R_FAILED(rc))
        {
            MEMLOG("svcQueryDebugProcessMemory failed for program %016lX! (0x%08X).", location->program_id, rc);
            success = false;
            break;
        }
        
        mem_type = (u8)(mem_info.type & 0xFF);
        
        /* Code to allow for bitmasking segments. */
        if ((mem_info.perm & Perm_R) && ((!is_segment && !mem_info.attr && (location->program_id != FS_SYSMODULE_TID || (location->program_id == FS_SYSMODULE_TID && mem_type != MemType_Unmapped && \
            mem_type != MemType_Io && mem_type != MemType_ThreadLocal && mem_type != MemType_Reserved))) || (is_segment && (mem_type == MemType_CodeStatic || mem_type == MemType_CodeMutable) && \
            (((segment <<= 1) >> 1) & location->mask))))
        {
            /* Reallocate data buffer. */
            tmp = realloc(location->data, location->data_size + mem_info.size);
            if (!tmp)
            {
                MEMLOG("Failed to resize segment data buffer to 0x%lX bytes for program %016lX!", location->data_size + mem_info.size, location->program_id);
                success = false;
                break;
            }
            
            location->data = tmp;
            tmp = NULL;
            
            rc = svcReadDebugProcessMemory(location->data + location->data_size, debug_handle, mem_info.addr, mem_info.size);
            if (R_FAILED(rc))
            {
                MEMLOG("svcReadDebugProcessMemory failed for program %016lX! (0x%08X).", location->program_id, rc);
                success = false;
                break;
            }
            
            location->data_size += mem_info.size;
        }
        
        addr = (mem_info.addr + mem_info.size);
    } while(addr != 0 && segment < BIT(3));
    
end:
    /* Close debug handle. */
    if (debug_handle != INVALID_HANDLE) svcCloseHandle(debug_handle);
    
    /* Unlock logfile mutex. */
    logControlMutex(false);
    
    if (success && (!location->data || !location->data_size))
    {
        MEMLOG("Unable to locate readable program memory pages for %016lX that match the required criteria!", location->program_id);
        success = false;
    }
    
    if (!success) memFreeMemoryLocation(location);
    
    /* Write log buffer data. This will do nothing if the log buffer length is zero. */
    logWriteStringToLogFile(g_memLogBuf);
    
    /* Free memory log buffer. */
    if (g_memLogBuf)
    {
        free(g_memLogBuf);
        g_memLogBuf = NULL;
    }
    
    g_memLogBufSize = 0;
    
    return success;
}

static bool memRetrieveDebugHandleFromProgramById(Handle *out, u64 program_id)
{
    if (!out || !program_id)
    {
        MEMLOG("Invalid parameters!");
        return false;
    }
    
    Result rc = 0;
    u64 pid = 0, d[8] = {0};
    Handle debug_handle = INVALID_HANDLE;
    
    u32 i = 0, num_processes = 0;
    u64 *pids = NULL;
    
    if (program_id > BOOT_SYSMODULE_TID && program_id != SPL_SYSMODULE_TID)
    {
        /* If not a kernel process, get PID from pm:dmnt. */
        rc = pmdmntGetProcessId(&pid, program_id);
        if (R_FAILED(rc))
        {
            MEMLOG("pmdmntGetProcessId failed for program %016lX! (0x%08X).", program_id, rc);
            return false;
        }
        
        /* Retrieve debug handle right away. */
        rc = svcDebugActiveProcess(&debug_handle, pid);
        if (R_FAILED(rc))
        {
            MEMLOG("svcDebugActiveProcess failed for program %016lX! (0x%08X).", program_id, rc);
            return false;
        }
    } else {
        /* Otherwise, query svc for the process list. */
        pids = calloc(300, sizeof(u64));
        if (!pids)
        {
            MEMLOG("Failed to allocate memory for PID list!");
            return false;
        }
        
        rc = svcGetProcessList((s32*)&num_processes, pids, 300);
        if (R_FAILED(rc))
        {
            MEMLOG("svcGetProcessList failed! (0x%08X).", rc);
            free(pids);
            return false;
        }
        
        /* Perform a lookup using the retrieved process list. */
        for(i = 0; i < num_processes; i++)
        {
            /* Retrieve debug handle for the current PID. */
            rc = svcDebugActiveProcess(&debug_handle, pids[i]);
            if (R_FAILED(rc)) continue;
            
            /* Get debug event using the debug handle. */
            /* This will let us know the program ID from the current PID. */
            rc = svcGetDebugEvent((u8*)&d, debug_handle);
            if (R_SUCCEEDED(rc) && d[2] == program_id) break;
            
            /* No match. Close debug handle and keep looking for our program. */
            svcCloseHandle(debug_handle);
            debug_handle = INVALID_HANDLE;
        }
        
        free(pids);
        
        if (i == num_processes)
        {
            MEMLOG("Unable to find program %016lX in kernel process list! (0x%08X).", program_id, rc);
            return false;
        }
    }
    
    /* Set output debug handle. */
    *out = debug_handle;
    
    return true;
}
