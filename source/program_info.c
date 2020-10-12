/*
 * program_info.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include <mbedtls/base64.h>

#include "utils.h"
#include "program_info.h"
#include "elf_symbol.h"

/* Function prototypes. */

static bool programInfoAddNsoMiddlewareListToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const NsoContext *nso_ctx);
static bool programInfoAddNsoSymbolsToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const NsoContext *nso_ctx, bool is_64bit);

bool programInfoInitializeContext(ProgramInfoContext *out, NcaContext *nca_ctx)
{
    if (!out || !nca_ctx || !strlen(nca_ctx->content_id_str) || nca_ctx->content_type != NcmContentType_Program || nca_ctx->content_size < NCA_FULL_HEADER_LENGTH || \
        (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        nca_ctx->header.content_type != NcaContentType_Program || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u32 i = 0, pfs_entry_count = 0, magic = 0;
    NsoContext *tmp_nso_ctx = NULL;
    
    bool success = false;
    
    /* Free output context beforehand. */
    programInfoFreeContext(out);
    
    /* Initialize Partition FS context. */
    if (!pfsInitializeContext(&(out->pfs_ctx), &(nca_ctx->fs_contexts[0])))
    {
        LOGFILE("Failed to initialize Partition FS context!");
        goto end;
    }
    
    /* Check if we're indeed dealing with an ExeFS. */
    if (!out->pfs_ctx.is_exefs)
    {
        LOGFILE("Initialized Partition FS is not an ExeFS!");
        goto end;
    }
    
    /* Get ExeFS entry count. Edge case, we should never trigger this. */
    if (!(pfs_entry_count = pfsGetEntryCount(&(out->pfs_ctx))))
    {
        LOGFILE("ExeFS has no file entries!");
        goto end;
    }
    
    /* Initialize NPDM context. */
    if (!npdmInitializeContext(&(out->npdm_ctx), &(out->pfs_ctx)))
    {
        LOGFILE("Failed to initialize NPDM context!");
        goto end;
    }
    
    /* Initialize NSO contexts. */
    for(i = 0; i < pfs_entry_count; i++)
    {
        /* Skip the main.npdm entry, as well as any other entries without a NSO header. */
        PartitionFileSystemEntry *pfs_entry = pfsGetEntryByIndex(&(out->pfs_ctx), i);
        char *pfs_entry_name = pfsGetEntryName(&(out->pfs_ctx), pfs_entry);
        if (!pfs_entry || !pfs_entry_name || !strncmp(pfs_entry_name, "main.npdm", 9) || !pfsReadEntryData(&(out->pfs_ctx), pfs_entry, &magic, sizeof(u32), 0) || \
            __builtin_bswap32(magic) != NSO_HEADER_MAGIC) continue;
        
        /* Reallocate NSO context buffer. */
        if (!(tmp_nso_ctx = realloc(out->nso_ctx, (out->nso_count + 1) * sizeof(NsoContext))))
        {
            LOGFILE("Failed to reallocate NSO context buffer for NSO \"%s\"! (entry #%u).", pfs_entry_name, i);
            goto end;
        }
        
        out->nso_ctx = tmp_nso_ctx;
        tmp_nso_ctx = NULL;
        
        memset(&(out->nso_ctx[out->nso_count]), 0, sizeof(NsoContext));
        
        /* Initialize NSO context. */
        if (!nsoInitializeContext(&(out->nso_ctx[out->nso_count]), &(out->pfs_ctx), pfs_entry))
        {
            LOGFILE("Failed to initialize context for NSO \"%s\"! (entry #%u).", pfs_entry_name, i);
            goto end;
        }
        
        /* Update NSO count. */
        out->nso_count++;
    }
    
    /* Safety check. */
    if (!out->nso_count)
    {
        LOGFILE("ExeFS has no NSOs!");
        goto end;
    }
    
    /* Update output context. */
    out->nca_ctx = nca_ctx;
    
    success = true;
    
end:
    if (!success) programInfoFreeContext(out);
    
    return success;
}

bool programInfoGenerateAuthoringToolXml(ProgramInfoContext *program_info_ctx)
{
    if (!programInfoIsValidContext(program_info_ctx))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    char *xml_buf = NULL;
    u64 xml_buf_size = 0;
    
    VersionType2 *sdk_addon_version = &(program_info_ctx->nca_ctx->header.sdk_addon_version);
    bool is_64bit = (program_info_ctx->npdm_ctx.meta_header->flags.is_64bit_instruction == 1);
    
    u8 *npdm_acid = (u8*)program_info_ctx->npdm_ctx.acid_header;
    u64 npdm_acid_size = program_info_ctx->npdm_ctx.meta_header->acid_size, npdm_acid_b64_size = 0;
    char *npdm_acid_b64 = NULL;
    
    bool success = false;
    
    /* Free AuthoringTool-like XML data if needed. */
    if (program_info_ctx->authoring_tool_xml) free(program_info_ctx->authoring_tool_xml);
    program_info_ctx->authoring_tool_xml = NULL;
    program_info_ctx->authoring_tool_xml_size = 0;
    
    /* Retrieve the Base64 conversion length for the whole NPDM ACID section. */
    mbedtls_base64_encode(NULL, 0, &npdm_acid_b64_size, npdm_acid, npdm_acid_size);
    if (npdm_acid_b64_size <= npdm_acid_size)
    {
        LOGFILE("Invalid Base64 conversion length for the NPDM ACID section! (0x%lX, 0x%lX).", npdm_acid_b64_size, npdm_acid_size);
        goto end;
    }
    
    /* Allocate memory for the NPDM ACID Base64 string. */
    if (!(npdm_acid_b64 = calloc(npdm_acid_b64_size + 1, sizeof(char))))
    {
        LOGFILE("Failed to allocate 0x%lX bytes for the NPDM ACID section Base64 string!", npdm_acid_b64_size);
        goto end;
    }
    
    /* Convert NPDM ACID section to a Base64 string. */
    if (mbedtls_base64_encode((u8*)npdm_acid_b64, npdm_acid_b64_size + 1, &npdm_acid_b64_size, npdm_acid, npdm_acid_size) != 0)
    {
        LOGFILE("Base64 conversion failed for the NPDM ACID section!");
        goto end;
    }
    
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
                                            "<ProgramInfo>\n" \
                                            "  <SdkVersion>%u_%u_%u</SdkVersion>\n" \
                                            "  <BuildTarget>%u</BuildTarget>\n" \
                                            "  <BuildType>Release</BuildType>\n"    /* Default to Release. */ \
                                            "  <Desc>%s</Desc>\n" \
                                            "  <DescFlags>\n" \
                                            "    <Production>%s</Production>\n" \
                                            "    <UnqualifiedApproval>%s</UnqualifiedApproval>\n" \
                                            "  </DescFlags>\n" \
                                            "  <MiddlewareList>\n", \
                                            sdk_addon_version->major, sdk_addon_version->minor, sdk_addon_version->micro, \
                                            is_64bit ? 64 : 32, \
                                            npdm_acid_b64, \
                                            program_info_ctx->npdm_ctx.acid_header->flags.production ? "true" : "false", \
                                            program_info_ctx->npdm_ctx.acid_header->flags.unqualified_approval ? "true" : "false")) goto end;
    
    /* Add Middleware info. */
    for(u32 i = 0; i < program_info_ctx->nso_count; i++)
    {
        if (!programInfoAddNsoMiddlewareListToAuthoringToolXml(&xml_buf, &xml_buf_size, &(program_info_ctx->nso_ctx[i]))) goto end;
    }
    
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "  </MiddlewareList>\n" \
                                            "  <DebugApiList />\n"      /* Fill this? */ \
                                            "  <PrivateApiList />\n"    /* Fill this? */ \
                                            "  <UnresolvedApiList>\n")) goto end;
    
    /* Add symbols from main NSO. */
    for(u32 i = 0; i < program_info_ctx->nso_count; i++)
    {
        /* Only proceed if we're dealing with the main NSO. */
        NsoContext *nso_ctx = &(program_info_ctx->nso_ctx[i]);
        if (!nso_ctx->nso_filename || strlen(nso_ctx->nso_filename) != 4 || strcmp(nso_ctx->nso_filename, "main") != 0) continue;
        if (!programInfoAddNsoSymbolsToAuthoringToolXml(&xml_buf, &xml_buf_size, nso_ctx, is_64bit)) goto end;
        break;
    }
    
    /* To do: add GuidelineApi. */
    
    if (!(success = utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                       "  </UnresolvedApiList>\n" \
                                                       "  <FsAccessControlData />\n"    /* Fill this? */ \
                                                       "</ProgramInfo>"))) goto end;
    
    /* Update ProgramInfo context. */
    program_info_ctx->authoring_tool_xml = xml_buf;
    program_info_ctx->authoring_tool_xml_size = strlen(xml_buf);
    
end:
    if (npdm_acid_b64) free(npdm_acid_b64);
    
    if (!success)
    {
        if (xml_buf) free(xml_buf);
        LOGFILE("Failed to generate ProgramInfo AuthoringTool XML!");
    }
    
    return success;
}

static bool programInfoAddNsoMiddlewareListToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const NsoContext *nso_ctx)
{
    if (!xml_buf || !xml_buf_size || !nso_ctx || !nso_ctx->nso_filename)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Check if this NSO holds an .api_info section. */
    /* If not, then just return right away. */
    if (!nso_ctx->rodata_api_info_section || !nso_ctx->rodata_api_info_section_size) return true;
    
    /* Look for SDK Middlewares in the .api_info section from this NSO's .rodata segment. */
    for(u64 i = 0; i < nso_ctx->rodata_api_info_section_size; i++)
    {
        if ((nso_ctx->rodata_api_info_section_size - i) <= 7) break;
        
        char *sdk_mw = (nso_ctx->rodata_api_info_section + i);
        if (strncmp(sdk_mw, "SDK MW+", 7) != 0) continue;
        
        /* Found a match. Let's retrieve pointers to the middleware vender and name. */
        char *sdk_mw_vender = (sdk_mw + 7);
        char *sdk_mw_name = (strchr(sdk_mw_vender, '+') + 1);
        
        /* Filter nnSdk entries. */
        if (!strncasecmp(sdk_mw_name, "NintendoSdk_nnSdk", 17))
        {
            i += strlen(sdk_mw);
            continue;
        }
        
        if (!utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, \
                                                "    <Middleware>\n" \
                                                "      <ModuleName>%s</ModuleName>\n" \
                                                "      <VenderName>%.*s</VenderName>\n" \
                                                "      <NsoName>%s</NsoName>\n" \
                                                "    </Middleware>\n", \
                                                sdk_mw_name, \
                                                (int)(sdk_mw_name - sdk_mw_vender - 1), sdk_mw_vender, \
                                                nso_ctx->nso_filename)) return false;
        
        /* Update counter. */
        i += strlen(sdk_mw);
    }
    
    return true;
}

static bool programInfoAddNsoSymbolsToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const NsoContext *nso_ctx, bool is_64bit)
{
    if (!xml_buf || !xml_buf_size || !nso_ctx || !nso_ctx->nso_filename || !nso_ctx->rodata_dynstr_section || !nso_ctx->rodata_dynstr_section_size || !nso_ctx->rodata_dynsym_section || \
        !nso_ctx->rodata_dynsym_section_size)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    u64 symbol_size = (!is_64bit ? sizeof(Elf32Symbol) : sizeof(Elf64Symbol));
    
    /* Parse ELF dynamic symbol table to retrieve the right symbol strings. */
    for(u64 i = 0; i < nso_ctx->rodata_dynsym_section_size; i += symbol_size)
    {
        if ((nso_ctx->rodata_dynsym_section_size - i) < symbol_size) break;
        
        char *symbol_str = NULL;
        u8 st_type = 0;
        
        /* To do: change ELF symbol filters? */
        if (!is_64bit)
        {
            /* Parse 32-bit ELF symbol. */
            Elf32Symbol *elf32_symbol = (Elf32Symbol*)(nso_ctx->rodata_dynsym_section + i);
            st_type = ELF_ST_TYPE(elf32_symbol->st_info);
            
            symbol_str = ((elf32_symbol->st_name < nso_ctx->rodata_dynstr_section_size && !elf32_symbol->st_value && (st_type == STT_NOTYPE || st_type == STT_FUNC) && \
                          elf32_symbol->st_shndx == SHN_UNDEF) ? (nso_ctx->rodata_dynstr_section + elf32_symbol->st_name) : NULL);
        } else {
            /* Parse 64-bit ELF symbol. */
            Elf64Symbol *elf64_symbol = (Elf64Symbol*)(nso_ctx->rodata_dynsym_section + i);
            st_type = ELF_ST_TYPE(elf64_symbol->st_info);
            
            symbol_str = ((elf64_symbol->st_name < nso_ctx->rodata_dynstr_section_size && !elf64_symbol->st_value && (st_type == STT_NOTYPE || st_type == STT_FUNC) && \
                          elf64_symbol->st_shndx == SHN_UNDEF) ? (nso_ctx->rodata_dynstr_section + elf64_symbol->st_name) : NULL);
        }
        
        if (!symbol_str) continue;
        
        if (!utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, \
                                                "    <UnresolvedApi>\n" \
                                                "      <ApiName>%s</ApiName>\n" \
                                                "      <NsoName>%s</NsoName>\n" \
                                                "    </UnresolvedApi>\n", \
                                                symbol_str, \
                                                nso_ctx->nso_filename)) return false;
    }
    
    return true;
}
