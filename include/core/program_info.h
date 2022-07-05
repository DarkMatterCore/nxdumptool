/*
 * program_info.h
 *
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

#ifndef __PROGRAM_INFO_H__
#define __PROGRAM_INFO_H__

#include "npdm.h"
#include "nso.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    NcaContext *nca_ctx;                ///< Pointer to the NCA context for the Program NCA from which program data (NPDM / NSO) is retrieved.
    PartitionFileSystemContext pfs_ctx; ///< PartitionFileSystemContext for the Program NCA ExeFS, which is where program data (NPDM / NSO) is stored.
    NpdmContext npdm_ctx;               ///< NpdmContext for the NPDM stored in Program NCA ExeFS.
    u32 nso_count;                      ///< Number of NSOs stored in Program NCA FS section #0.
    NsoContext *nso_ctx;                ///< Pointer to a dynamically allocated buffer that holds 'nso_count' NSO contexts.
    char *authoring_tool_xml;           ///< Pointer to a dynamically allocated, NULL-terminated buffer that holds AuthoringTool-like XML data.
                                        ///< This is always NULL unless programInfoGenerateAuthoringToolXml() is used on this ProgramInfoContext.
    u64 authoring_tool_xml_size;        ///< Size for the AuthoringTool-like XML. This is essentially the same as using strlen() on 'authoring_tool_xml'.
                                        ///< This is always 0 unless programInfoGenerateAuthoringToolXml() is used on this ProgramInfoContext.
} ProgramInfoContext;

/// Initializes a ProgramInfoContext using a previously initialized NcaContext (which must belong to a Program NCA).
bool programInfoInitializeContext(ProgramInfoContext *out, NcaContext *nca_ctx);

/// Generates an AuthoringTool-like XML using information from a previously initialized ProgramInfoContext.
/// If the function succeeds, XML data and size will get saved to the 'authoring_tool_xml' and 'authoring_tool_xml_size' members from the ProgramInfoContext.
bool programInfoGenerateAuthoringToolXml(ProgramInfoContext *program_info_ctx);

/// Helper inline functions.

NX_INLINE void programInfoFreeContext(ProgramInfoContext *program_info_ctx)
{
    if (!program_info_ctx) return;

    pfsFreeContext(&(program_info_ctx->pfs_ctx));
    npdmFreeContext(&(program_info_ctx->npdm_ctx));

    if (program_info_ctx->nso_ctx)
    {
        for(u32 i = 0; i < program_info_ctx->nso_count; i++) nsoFreeContext(&(program_info_ctx->nso_ctx[i]));
        free(program_info_ctx->nso_ctx);
    }

    if (program_info_ctx->authoring_tool_xml) free(program_info_ctx->authoring_tool_xml);
    memset(program_info_ctx, 0, sizeof(ProgramInfoContext));
}

NX_INLINE bool programInfoIsValidContext(ProgramInfoContext *program_info_ctx)
{
    return (program_info_ctx && program_info_ctx->nca_ctx && npdmIsValidContext(&(program_info_ctx->npdm_ctx)) && program_info_ctx->nso_count && program_info_ctx->nso_ctx);
}

#ifdef __cplusplus
}
#endif

#endif /* __PROGRAM_INFO_H__ */
