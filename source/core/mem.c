/*
 * mem.c
 *
 * Copyright (c) 2019, shchmue.
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include "mem.h"

#define MEMLOG_DEBUG(fmt, ...)              LOG_MSG_BUF_DEBUG(&g_memLogBuf, &g_memLogBufSize, fmt, ##__VA_ARGS__)
#define MEMLOG_ERROR(fmt, ...)              LOG_MSG_BUF_ERROR(&g_memLogBuf, &g_memLogBufSize, fmt, ##__VA_ARGS__)

#define MEM_PID_BUF_SIZE                    300

#define MEM_INVALID_SEGMENT_PAGE_TYPE(x)    ((x) != MemType_CodeStatic && (x) != MemType_CodeMutable)

#define MEM_INVALID_FS_PAGE_TYPE(x)         ((x) == MemType_Unmapped || (x) == MemType_Io || (x) == MemType_ThreadLocal || (x) == MemType_Reserved)

/* Global variables. */

static Mutex g_memMutex = 0;
static u64 g_fsTextSegmentAddr = 0;

#if LOG_LEVEL < LOG_LEVEL_NONE
static char *g_memLogBuf = NULL;
static size_t g_memLogBufSize = 0;
#endif

/* Function prototypes. */

static bool memRetrieveProgramMemory(MemoryLocation *location, bool is_segment);
static bool memRetrieveDebugHandleFromProgramById(Handle *out, u64 program_id);

bool memRetrieveProgramMemorySegment(MemoryLocation *location)
{
    if (!location || !location->program_id || !location->mask || location->mask >= MemoryProgramSegmentType_Limit)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;
    SCOPED_LOCK(&g_memMutex) ret = memRetrieveProgramMemory(location, true);
    return ret;
}

bool memRetrieveFullProgramMemory(MemoryLocation *location)
{
    if (!location || !location->program_id)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    bool ret = false;
    SCOPED_LOCK(&g_memMutex) ret = memRetrieveProgramMemory(location, false);
    return ret;
}

static bool memRetrieveProgramMemory(MemoryLocation *location, bool is_segment)
{
    Result rc = 0;
    Handle debug_handle = INVALID_HANDLE;

    MemoryInfo mem_info = {0};

    u32 page_info = 0;
    u64 addr = 0, last_text_addr = 0;
    u8 segment = MemoryProgramSegmentType_Text, mem_type = 0;
    u8 *tmp = NULL;

    bool success = true;

    /* Make sure we have access to debug SVC calls. */
    if (!(envIsSyscallHinted(0x60) &&   /* svcDebugActiveProcess. */
          envIsSyscallHinted(0x63) &&   /* svcGetDebugEvent. */
          envIsSyscallHinted(0x65) &&   /* svcGetProcessList. */
          envIsSyscallHinted(0x69) &&   /* svcQueryDebugProcessMemory. */
          envIsSyscallHinted(0x6A)))    /* svcReadDebugProcessMemory. */
    {
        LOG_MSG_ERROR("Debug SVC permissions not available!");
        return false;
    }

    /* Clear output MemoryLocation element. */
    memFreeMemoryLocation(location);

#if LOG_LEVEL < LOG_LEVEL_NONE
    /* LOG_*() macros will be useless if the target program is the FS sysmodule. */
    /* This is because any FS I/O operation *will* lock up the console while FS itself is being debugged. */
    /* So we'll just log data to a temporary buffer using LOG_MSG_BUF_*() macros, then write it all out to the logfile after calling svcCloseHandle(). */
    /* However, we must prevent other threads from logging data as well in order to avoid a lock up, so we'll temporarily lock the logfile mutex. */
    /* We don't need to take care of manually (re)allocating memory for our buffer -- the log handler ABI takes care of that for us. */
    logControlMutex(true);
#endif

    /* Retrieve debug handle by program ID. */
    if (!memRetrieveDebugHandleFromProgramById(&debug_handle, location->program_id))
    {
        MEMLOG_ERROR("Unable to retrieve debug handle for program %016lX!", location->program_id);
        goto end;
    }

    if (is_segment && location->program_id == FS_SYSMODULE_TID)
    {
        /* Locate the "real" FS .text segment, since Atmosphère emuMMC has two. */
        /* We'll only look for it if we haven't previously retrieved it, though. */
        if (!g_fsTextSegmentAddr)
        {
            do {
                /* Query memory page info. */
                rc = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
                if (R_FAILED(rc))
                {
                    MEMLOG_ERROR("svcQueryDebugProcessMemory failed for program %016lX! (0x%X).", location->program_id, rc);
                    success = false;
                    goto end;
                }

                mem_type = (u8)(mem_info.type & MemState_Type);
                addr = (mem_info.addr + mem_info.size);

                /* Filter out unwanted memory pages. */
                if (MEM_INVALID_SEGMENT_PAGE_TYPE(mem_type) || mem_info.attr || (mem_info.perm & Perm_Rx) != Perm_Rx) continue;

#if LOG_LEVEL == LOG_LEVEL_DEBUG
                MEMLOG_DEBUG("svcQueryDebugProcessMemory info (FS .text segment lookup) (program %016lX, page 0x%X, debug handle 0x%X):\r\n" \
                             "- addr: 0x%lX\r\n" \
                             "- size: 0x%lX\r\n" \
                             "- type: 0x%X\r\n" \
                             "- attr: 0x%X\r\n" \
                             "- perm: 0x%X\r\n" \
                             "- ipc_refcount: 0x%X\r\n" \
                             "- device_refcount: 0x%X", \
                             location->program_id, page_info, debug_handle, mem_info.addr, mem_info.size, mem_info.type, mem_info.attr, mem_info.perm, \
                             mem_info.ipc_refcount, mem_info.device_refcount);
#endif

                /* Update .text segment address. */
                last_text_addr = mem_info.addr;
            } while(addr != 0);

            g_fsTextSegmentAddr = last_text_addr;
            MEMLOG_DEBUG("FS .text segment address: 0x%lX.", g_fsTextSegmentAddr);
        }

        /* Update variable so we can start reading data right from this address in the next steps. */
        addr = g_fsTextSegmentAddr;
    }

    do {
        /* Query memory page info. */
        rc = svcQueryDebugProcessMemory(&mem_info, &page_info, debug_handle, addr);
        if (R_FAILED(rc))
        {
            MEMLOG_ERROR("svcQueryDebugProcessMemory failed for program %016lX! (0x%X).", location->program_id, rc);
            success = false;
            break;
        }

        mem_type = (u8)(mem_info.type & MemState_Type);
        addr = (mem_info.addr + mem_info.size);

        /* Filter out unwanted memory pages. */
        if (mem_info.attr || !(mem_info.perm & Perm_R) || \
            (is_segment && (MEM_INVALID_SEGMENT_PAGE_TYPE(mem_type) || !(((segment <<= 1) >> 1) & location->mask))) || \
            (!is_segment && location->program_id == FS_SYSMODULE_TID && MEM_INVALID_FS_PAGE_TYPE(mem_type))) continue;

#if LOG_LEVEL == LOG_LEVEL_DEBUG
        MEMLOG_DEBUG("svcQueryDebugProcessMemory info (program %016lX, page 0x%X, debug handle 0x%X):\r\n" \
                     "- addr: 0x%lX\r\n" \
                     "- size: 0x%lX\r\n" \
                     "- type: 0x%X\r\n" \
                     "- attr: 0x%X\r\n" \
                     "- perm: 0x%X\r\n" \
                     "- ipc_refcount: 0x%X\r\n" \
                     "- device_refcount: 0x%X", \
                     location->program_id, page_info, debug_handle, mem_info.addr, mem_info.size, mem_info.type, mem_info.attr, mem_info.perm, \
                     mem_info.ipc_refcount, mem_info.device_refcount);
#endif

        /* Reallocate data buffer. */
        tmp = realloc(location->data, location->data_size + mem_info.size);
        if (!tmp)
        {
            MEMLOG_ERROR("Failed to resize segment data buffer to 0x%lX bytes for program %016lX!", location->data_size + mem_info.size, location->program_id);
            success = false;
            break;
        }

        location->data = tmp;
        tmp = NULL;

        /* Read memory page. */
        rc = svcReadDebugProcessMemory(location->data + location->data_size, debug_handle, mem_info.addr, mem_info.size);
        if (R_FAILED(rc))
        {
            MEMLOG_ERROR("svcReadDebugProcessMemory failed for program %016lX! (0x%X).", location->program_id, rc);
            success = false;
            break;
        }

        /* Increase data buffer size. */
        location->data_size += mem_info.size;
    } while(addr != 0 && segment < MemoryProgramSegmentType_Limit);

end:
    /* Close debug handle. */
    if (debug_handle != INVALID_HANDLE) svcCloseHandle(debug_handle);

#if LOG_LEVEL < LOG_LEVEL_NONE
    /* Unlock logfile mutex. */
    logControlMutex(false);
#endif

    if (success && (!location->data || !location->data_size))
    {
        MEMLOG_ERROR("Unable to locate readable program memory pages for %016lX that match the required criteria!", location->program_id);
        success = false;
    }

    if (!success) memFreeMemoryLocation(location);

#if LOG_LEVEL < LOG_LEVEL_NONE
    /* Write log buffer data. This will do nothing if the log buffer length is zero. */
    logWriteStringToLogFile(g_memLogBuf);

    /* Free memory log buffer. */
    if (g_memLogBuf)
    {
        free(g_memLogBuf);
        g_memLogBuf = NULL;
    }

    g_memLogBufSize = 0;
#endif

    return success;
}

static bool memRetrieveDebugHandleFromProgramById(Handle *out, u64 program_id)
{
    if (!out || !program_id)
    {
        MEMLOG_ERROR("Invalid parameters!");
        return false;
    }

    Result rc = 0;
    u64 pid = 0, d[8] = {0};
    Handle debug_handle = INVALID_HANDLE;

    u32 i = 0, num_processes = 0;
    u64 *pids = NULL;

    bool success = false;

    if (program_id > BOOT_SYSMODULE_TID && program_id != SPL_SYSMODULE_TID)
    {
        /* If not a kernel process, get process ID from pm:dmnt. */
        rc = pmdmntGetProcessId(&pid, program_id);
        if (R_FAILED(rc))
        {
            MEMLOG_ERROR("pmdmntGetProcessId failed for program %016lX! (0x%X).", program_id, rc);
            goto end;
        }

        MEMLOG_DEBUG("Process ID (%016lX): 0x%lX.", program_id, pid);

        /* Retrieve debug handle right away. */
        rc = svcDebugActiveProcess(&debug_handle, pid);
        if (R_FAILED(rc))
        {
            MEMLOG_ERROR("svcDebugActiveProcess failed for program %016lX! (0x%X).", program_id, rc);
            goto end;
        }
    } else {
        /* Otherwise, query svc for the process list. */
        pids = calloc(MEM_PID_BUF_SIZE, sizeof(u64));
        if (!pids)
        {
            MEMLOG_ERROR("Failed to allocate memory for PID list!");
            goto end;
        }

        rc = svcGetProcessList((s32*)&num_processes, pids, MEM_PID_BUF_SIZE);
        if (R_FAILED(rc))
        {
            MEMLOG_ERROR("svcGetProcessList failed! (0x%X).", rc);
            goto end;
        }

        MEMLOG_DEBUG("svcGetProcessList returned %u process IDs.", num_processes);

        /* Perform a lookup using the retrieved process list. */
        for(i = 0; i < num_processes; i++)
        {
            /* Retrieve debug handle for the current process ID. */
            rc = svcDebugActiveProcess(&debug_handle, pids[i]);
            if (R_FAILED(rc))
            {
                MEMLOG_DEBUG("svcDebugActiveProcess failed for process ID 0x%lX! (0x%X).", pids[i], rc);
                continue;
            }

            MEMLOG_DEBUG("Debug handle (process 0x%lX): 0x%X.", pids[i], debug_handle);

            /* Get debug event using the debug handle. */
            /* This will let us know the program ID for the current process ID. */
            rc = svcGetDebugEvent((u8*)&d, debug_handle);
            if (R_SUCCEEDED(rc))
            {
                /* Jackpot. */
                if (d[2] == program_id) break;
            } else {
                MEMLOG_DEBUG("svcGetDebugEvent failed for debug handle 0x%X! (0x%X).", debug_handle, rc);
            }

            /* No match. Close debug handle and keep looking for our program. */
            svcCloseHandle(debug_handle);
            debug_handle = INVALID_HANDLE;
        }

        if (i == num_processes)
        {
            MEMLOG_ERROR("Unable to find program %016lX in kernel process list! (0x%X).", program_id, rc);
            goto end;
        }
    }

    MEMLOG_DEBUG("Output debug handle for program ID %016lX: 0x%X.", program_id, debug_handle);

    /* Set output debug handle. */
    *out = debug_handle;

    /* Update output flag. */
    success = true;

end:
    if (pids) free(pids);

    return success;
}
