/*
 * legal_info.h
 *
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

#pragma once

#ifndef __LEGAL_INFO_H__
#define __LEGAL_INFO_H__

#include "romfs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    NcaContext *nca_ctx;            ///< Pointer to the NCA context for the LegalInformation NCA from which XML data is retrieved.
    char *authoring_tool_xml;       ///< Pointer to a dynamically allocated, NULL-terminated buffer that holds AuthoringTool-like XML data.
    u64 authoring_tool_xml_size;    ///< Size for the AuthoringTool-like XML. This is essentially the same as using strlen() on 'authoring_tool_xml'.
} LegalInfoContext;

/// Initializes a LegalInfoContext using a previously initialized NcaContext (which must belong to a LegalInformation NCA).
bool legalInfoInitializeContext(LegalInfoContext *out, NcaContext *nca_ctx);

/// Helper inline functions.

NX_INLINE void legalInfoFreeContext(LegalInfoContext *legal_info_ctx)
{
    if (!legal_info_ctx) return;
    if (legal_info_ctx->authoring_tool_xml) free(legal_info_ctx->authoring_tool_xml);
    memset(legal_info_ctx, 0, sizeof(LegalInfoContext));
}

#ifdef __cplusplus
}
#endif

#endif /* __LEGAL_INFO_H__ */
