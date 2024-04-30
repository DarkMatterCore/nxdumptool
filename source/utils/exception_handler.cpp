/*
 * exception_handler.cpp
 *
 * Copyright (c) 2019-2020, WerWolv.
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * This file is part of nxdumptool (https://github.com/DarkMatterCore/nxdumptool).
 * Loosely based on debug_helpers.cpp from EdiZon-Rewrite.
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

#include <core/nxdt_utils.h>
#include <borealis.hpp>

/* Helper macros. */

#define FP_MASK                     0xFFFFFFFFFF000000UL
#define STACK_TRACE_SIZE            0x20
#define IS_HB_ADDR(x)               (info.addr && info.size && (x) >= info.addr && (x) < (info.addr + info.size))
#define EH_ADD_FMT_STR(fmt, ...)    utilsAppendFormattedStringToBuffer(&exception_str, &exception_str_size, fmt, ##__VA_ARGS__)

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

extern bool g_borealisInitialized;

extern "C" {
    /* Overrides for libnx weak symbols. */
    u32 __nx_applet_exit_mode = 0;
    alignas(16) u8 __nx_exception_stack[0x8000];
    u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);
}

namespace nxdt::utils {
    static void GetHomebrewMemoryInfo(MemoryInfo *out)
    {
        if (!out)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            return;
        }

        u32 p = 0;
        Result rc = 0;

        /* Find the memory region in which this function is stored. */
        /* The start of it will be the base address the homebrew was mapped to. */
        rc = svcQueryMemory(out, &p, static_cast<u64>(reinterpret_cast<uintptr_t>(&GetHomebrewMemoryInfo)));
        if (R_FAILED(rc)) LOG_MSG_ERROR("svcQueryMemory failed! (0x%X).", rc);
    }

#if LOG_LEVEL < LOG_LEVEL_NONE
    static bool UnwindStack(u64 *out_stack_trace, u32 *out_stack_trace_size, size_t max_stack_trace_size, u64 cur_fp)
    {
        if (!out_stack_trace || !out_stack_trace_size || !max_stack_trace_size || !cur_fp)
        {
            LOG_MSG_ERROR("Invalid parameters!");
            return false;
        }

        struct StackFrame {
            u64 fp; ///< Frame Pointer (pointer to previous stack frame).
            u64 lr; ///< Link Register (return address).
        };

        *out_stack_trace_size = 0;
        u64 fp_base = (cur_fp & FP_MASK);

        for(size_t i = 0; i < max_stack_trace_size; i++)
        {
            /* Last check fixes a crash while dealing with certain stack frame addresses. */
            if (!cur_fp || (cur_fp % sizeof(u64)) || (cur_fp & FP_MASK) != fp_base) break;

            auto cur_trace = reinterpret_cast<StackFrame*>(cur_fp);
            out_stack_trace[(*out_stack_trace_size)++] = cur_trace->lr;
            cur_fp = cur_trace->fp;
        }

        return (*out_stack_trace_size > 0);
    }
#endif  /* LOG_LEVEL < LOG_LEVEL_NONE */

    static void NX_NORETURN AbortProgramExecution(std::string str)
    {
        if (g_borealisInitialized)
        {
            /* Display crash frame and exit Borealis. */
            brls::Application::crash(str);
            while(brls::Application::mainLoop());
        } else {
            /* Print error message using console output. */
            utilsPrintConsoleError(str.c_str());
        }

        /* Clean up resources. */
        utilsCloseResources();

        /* Exit application. */
        __nx_applet_exit_mode = 1;
        exit(EXIT_FAILURE);

        __builtin_unreachable();
    }
}

extern "C" {
    /* libnx abort function override. */
    void diagAbortWithResult(Result res)
    {
        /* Log error. */
        LOG_MSG_ERROR("*** libnx aborted with error code: 0x%X ***", res);

        /* Abort program execution. */
        std::string crash_str = (g_borealisInitialized ? i18n::getStr("utils/exception_handler/libnx_abort"_i18n, res) : fmt::format("Fatal error triggered in libnx!\nError code: 0x{:08X}.", res));
        nxdt::utils::AbortProgramExecution(crash_str);
    }

    /* libnx exception handler override. */
    void __libnx_exception_handler(ThreadExceptionDump *ctx)
    {
        MemoryInfo info = {0};
        std::string error_desc_str, crash_str;

        /* Get homebrew memory info. */
        nxdt::utils::GetHomebrewMemoryInfo(&info);

        switch(ctx->error_desc)
        {
            case ThreadExceptionDesc_InstructionAbort:
                error_desc_str = "Instruction Abort";
                break;
            case ThreadExceptionDesc_MisalignedPC:
                error_desc_str = "Misaligned Program Counter";
                break;
            case ThreadExceptionDesc_MisalignedSP:
                error_desc_str = "Misaligned Stack Pointer";
                break;
            case ThreadExceptionDesc_SError:
                error_desc_str = "SError";
                break;
            case ThreadExceptionDesc_BadSVC:
                error_desc_str = "Bad SVC";
                break;
            case ThreadExceptionDesc_Trap:
                error_desc_str = "SIGTRAP";
                break;
            case ThreadExceptionDesc_Other:
                error_desc_str = "Segmentation Fault";
                break;
            default:
                error_desc_str = "Unknown";
                break;
        }

#if LOG_LEVEL < LOG_LEVEL_NONE
        char *exception_str = NULL;
        size_t exception_str_size = 0;

        u32 stack_trace_size = 0;
        u64 stack_trace[STACK_TRACE_SIZE] = {0};

        /* Log exception type. */
        LOG_MSG_ERROR("*** Exception Triggered ***");

        EH_ADD_FMT_STR("Type: %s (0x%X)\r\n", error_desc_str.c_str(), ctx->error_desc);

        /* Log CPU registers. */
        EH_ADD_FMT_STR("Registers:");

        for(size_t i = 0; i < MAX_ELEMENTS(ctx->cpu_gprs); i++)
        {
            u64 reg = ctx->cpu_gprs[i].x;
            EH_ADD_FMT_STR("\r\n    X%02lu:  0x%lX", i, reg);
            if (IS_HB_ADDR(reg)) EH_ADD_FMT_STR(" (BASE + 0x%lX)", reg - info.addr);
        }

        EH_ADD_FMT_STR("\r\n    FP:   0x%lX", ctx->fp.x);
        if (IS_HB_ADDR(ctx->fp.x)) EH_ADD_FMT_STR(" (BASE + 0x%lX)", ctx->fp.x - info.addr);

        EH_ADD_FMT_STR("\r\n    LR:   0x%lX", ctx->lr.x);
        if (IS_HB_ADDR(ctx->lr.x)) EH_ADD_FMT_STR(" (BASE + 0x%lX)", ctx->lr.x - info.addr);

        EH_ADD_FMT_STR("\r\n    SP:   0x%lX", ctx->sp.x);
        if (IS_HB_ADDR(ctx->sp.x)) EH_ADD_FMT_STR(" (BASE + 0x%lX)", ctx->sp.x - info.addr);

        EH_ADD_FMT_STR("\r\n    PC:   0x%lX", ctx->pc.x);
        if (IS_HB_ADDR(ctx->pc.x)) EH_ADD_FMT_STR(" (BASE + 0x%lX)", ctx->pc.x - info.addr);
        EH_ADD_FMT_STR("\r\n");

        /* Unwind stack. */
        if (nxdt::utils::UnwindStack(stack_trace, &stack_trace_size, STACK_TRACE_SIZE, ctx->fp.x))
        {
            /* Log stack trace. */
            EH_ADD_FMT_STR("Stack Trace:");

            for(u32 i = 0; i < stack_trace_size; i++)
            {
                u64 addr = stack_trace[i];
                EH_ADD_FMT_STR("\r\n    [%02u]: 0x%lX", stack_trace_size - i - 1, addr);
                if (IS_HB_ADDR(addr)) EH_ADD_FMT_STR(" (BASE + 0x%lX)", addr - info.addr);
            }

            EH_ADD_FMT_STR("\r\n");
        }

        /* Write log string. */
        logWriteStringToLogFile(exception_str);

        /* Free exception info string. */
        if (exception_str) free(exception_str);
#endif  /* LOG_LEVEL < LOG_LEVEL_NONE */

        /* Abort program execution. */
        crash_str = (g_borealisInitialized ? i18n::getStr("utils/exception_handler/exception_triggered"_i18n, error_desc_str, ctx->error_desc) : \
                                             fmt::format("Fatal exception triggered!\nReason: {} (0x{:X}).", error_desc_str, ctx->error_desc));
        crash_str += (fmt::format("\nPC: 0x{:X}", ctx->pc.x) + (IS_HB_ADDR(ctx->pc.x) ? fmt::format(" (BASE + 0x{:X}).", ctx->pc.x - info.addr) : "."));
        nxdt::utils::AbortProgramExecution(crash_str);
    }
}
