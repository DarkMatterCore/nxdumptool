/*
 * program_info.c
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

#include <mbedtls/base64.h>

#include "nxdt_utils.h"
#include "program_info.h"
#include "elf_symbol.h"

/* Helper macros. */

#define PI_ADD_FMT_STR_T1(fmt, ...) utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, fmt, ##__VA_ARGS__)
#define PI_ADD_FMT_STR_T2(fmt, ...) utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, fmt, ##__VA_ARGS__)

/* Global variables. */

static const char *g_trueString = "True", *g_falseString = "False";

static const char g_nnSdkString[] = "NintendoSdk_nnSdk";
static const size_t g_nnSdkStringLength = (MAX_ELEMENTS(g_nnSdkString) - 1);

static const char *g_facAccessibilityStrings[] = {
    "None",
    "Read",
    "Write",
    "ReadWrite"
};

/* Function prototypes. */

static bool programInfoGetSdkVersionAndBuildTypeFromSdkNso(ProgramInfoContext *program_info_ctx, char **sdk_version, char **build_type);
static bool programInfoAddNsoApiListToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, ProgramInfoContext *program_info_ctx, const char *api_list_tag, const char *api_entry_prefix, \
                                                       const char *sdk_prefix);
static bool programInfoIsApiInfoEntryValid(const char *sdk_prefix, size_t sdk_prefix_len, char *sdk_entry, char **sdk_entry_vender, int *sdk_entry_vender_len, char **sdk_entry_name, bool nnsdk);

static bool programInfoAddStringFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const char *value);

static bool programInfoAddNsoSymbolsToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, ProgramInfoContext *program_info_ctx);
static bool programInfoIsElfSymbolValid(u8 *dynsym_ptr, char *dynstr_base_ptr, u64 dynstr_size, bool is_64bit, char **symbol_str);

static bool programInfoAddFsAccessControlDataToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, ProgramInfoContext *program_info_ctx);

bool programInfoInitializeContext(ProgramInfoContext *out, NcaContext *nca_ctx)
{
    if (!out || !nca_ctx || !*(nca_ctx->content_id_str) || nca_ctx->content_type != NcmContentType_Program || nca_ctx->content_size < NCA_FULL_HEADER_LENGTH || \
        (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        nca_ctx->header.content_type != NcaContentType_Program || nca_ctx->content_type_ctx || !out)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    u32 i = 0, pfs_entry_count = 0, magic = 0;
    NsoContext *tmp_nso_ctx = NULL;

    bool success = false;

    /* Free output context beforehand. */
    programInfoFreeContext(out);

    /* Initialize Partition FS context. */
    if (!pfsInitializeContext(&(out->pfs_ctx), &(nca_ctx->fs_ctx[0])))
    {
        LOG_MSG_ERROR("Failed to initialize Partition FS context!");
        goto end;
    }

    /* Check if we're indeed dealing with an ExeFS. */
    if (!out->pfs_ctx.is_exefs)
    {
        LOG_MSG_ERROR("Initialized Partition FS is not an ExeFS!");
        goto end;
    }

    /* Get ExeFS entry count. Edge case, we should never trigger this. */
    if (!(pfs_entry_count = pfsGetEntryCount(&(out->pfs_ctx))))
    {
        LOG_MSG_ERROR("ExeFS has no file entries!");
        goto end;
    }

    /* Initialize NPDM context. */
    if (!npdmInitializeContext(&(out->npdm_ctx), &(out->pfs_ctx)))
    {
        LOG_MSG_ERROR("Failed to initialize NPDM context!");
        goto end;
    }

    /* Initialize NSO contexts. */
    for(i = 0; i < pfs_entry_count; i++)
    {
        /* Skip the main.npdm entry, as well as any other entries without a NSO header. */
        PartitionFileSystemEntry *pfs_entry = pfsGetEntryByIndex(&(out->pfs_ctx), i);
        char *pfs_entry_name = pfsGetEntryName(&(out->pfs_ctx), pfs_entry);
        if (!pfs_entry || !pfs_entry_name || !strcmp(pfs_entry_name, "main.npdm") || !pfsReadEntryData(&(out->pfs_ctx), pfs_entry, &magic, sizeof(u32), 0) || \
            __builtin_bswap32(magic) != NSO_HEADER_MAGIC) continue;

        /* Reallocate NSO context buffer. */
        if (!(tmp_nso_ctx = realloc(out->nso_ctx, (out->nso_count + 1) * sizeof(NsoContext))))
        {
            LOG_MSG_ERROR("Failed to reallocate NSO context buffer for NSO \"%s\"! (entry #%u).", pfs_entry_name, i);
            goto end;
        }

        out->nso_ctx = tmp_nso_ctx;
        tmp_nso_ctx = NULL;

        memset(&(out->nso_ctx[out->nso_count]), 0, sizeof(NsoContext));

        /* Initialize NSO context. */
        if (!nsoInitializeContext(&(out->nso_ctx[out->nso_count]), &(out->pfs_ctx), pfs_entry))
        {
            LOG_MSG_ERROR("Failed to initialize context for NSO \"%s\"! (entry #%u).", pfs_entry_name, i);
            goto end;
        }

        /* Update NSO count. */
        out->nso_count++;
    }

    /* Safety check. */
    if (!out->nso_count)
    {
        LOG_MSG_ERROR("ExeFS has no NSOs!");
        goto end;
    }

    /* Update output context. */
    out->nca_ctx = nca_ctx;

    /* Update content type context info in NCA context. */
    nca_ctx->content_type_ctx = out;
    nca_ctx->content_type_ctx_patch = false;

    success = true;

end:
    if (!success) programInfoFreeContext(out);

    return success;
}

bool programInfoGenerateAuthoringToolXml(ProgramInfoContext *program_info_ctx)
{
    if (!programInfoIsValidContext(program_info_ctx))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    char *xml_buf = NULL;
    u64 xml_buf_size = 0;

    char *sdk_version = NULL, *build_type = NULL;
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
        LOG_MSG_ERROR("Invalid Base64 conversion length for the NPDM ACID section! (0x%lX, 0x%lX).", npdm_acid_b64_size, npdm_acid_size);
        goto end;
    }

    /* Allocate memory for the NPDM ACID Base64 string. */
    if (!(npdm_acid_b64 = calloc(npdm_acid_b64_size + 1, sizeof(char))))
    {
        LOG_MSG_ERROR("Failed to allocate 0x%lX bytes for the NPDM ACID section Base64 string!", npdm_acid_b64_size);
        goto end;
    }

    /* Convert NPDM ACID section to a Base64 string. */
    if (mbedtls_base64_encode((u8*)npdm_acid_b64, npdm_acid_b64_size + 1, &npdm_acid_b64_size, npdm_acid, npdm_acid_size) != 0)
    {
        LOG_MSG_ERROR("Base64 conversion failed for the NPDM ACID section!");
        goto end;
    }

    /* Get SDK version and build type strings. */
    if (!programInfoGetSdkVersionAndBuildTypeFromSdkNso(program_info_ctx, &sdk_version, &build_type)) goto end;

    if (!PI_ADD_FMT_STR_T1("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
                           "<ProgramInfo>\n")) goto end;

    /* SdkVersion. */
    if (!programInfoAddStringFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "SdkVersion", sdk_version)) goto end;

    if (!PI_ADD_FMT_STR_T1("  <ToolVersion />\n"                        /* Impossible to get. */ \
                           "  <NxAddonVersion>%s</NxAddonVersion>\n" \
                           "  <PatchToolVersion />\n"                   /* Impossible to get. */ \
                           "  <BuildTarget>%u</BuildTarget>\n", \
                           sdk_version, \
                           is_64bit ? 64 : 32)) goto end;

    /* BuildType. */
    if (!programInfoAddStringFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "BuildType", build_type)) goto end;

    if (!PI_ADD_FMT_STR_T1("  <EnableDeadStrip />\n"                                /* Impossible to get. */ \
                           "  <EnableDeadStripSpecified />\n"                       /* Impossible to get. */ \
                           "  <Desc>%s</Desc>\n" \
                           "  <DescFileName />\n"                                   /* Impossible to get. */ \
                           "  <DescFlags>\n" \
                           "    <Production>%s</Production>\n" \
                           "    <UnqualifiedApproval>%s</UnqualifiedApproval>\n" \
                           "  </DescFlags>\n", \
                           npdm_acid_b64, \
                           program_info_ctx->npdm_ctx.acid_header->flags.production ? g_trueString : g_falseString, \
                           program_info_ctx->npdm_ctx.acid_header->flags.unqualified_approval ? g_trueString : g_falseString)) goto end;

    /* MiddlewareList. */
    if (!programInfoAddNsoApiListToAuthoringToolXml(&xml_buf, &xml_buf_size, program_info_ctx, "Middleware", "Module", "SDK MW")) goto end;

    /* DebugApiList. */
    if (!programInfoAddNsoApiListToAuthoringToolXml(&xml_buf, &xml_buf_size, program_info_ctx, "DebugApi", "Api", "SDK Debug")) goto end;

    /* PrivateApiList. */
    if (!programInfoAddNsoApiListToAuthoringToolXml(&xml_buf, &xml_buf_size, program_info_ctx, "PrivateApi", "Api", "SDK Private")) goto end;

    /* GuidelineApiList. */
    if (!programInfoAddNsoApiListToAuthoringToolXml(&xml_buf, &xml_buf_size, program_info_ctx, "GuidelineApi", "Api", "SDK Guideline")) goto end;

    /* UnresolvedApiList. */
    if (!programInfoAddNsoSymbolsToAuthoringToolXml(&xml_buf, &xml_buf_size, program_info_ctx)) goto end;

    /* FsAccessControlData. */
    if (!programInfoAddFsAccessControlDataToAuthoringToolXml(&xml_buf, &xml_buf_size, program_info_ctx)) goto end;

    if (!(success = PI_ADD_FMT_STR_T1("  <EnableGlobalDestructor />\n"          /* Impossible to get. */ \
                                      "  <EnableGlobalDestructorSpecified />\n" /* Impossible to get. */ \
                                      "  <IncludeNssFile />\n"                  /* Impossible to get. */ \
                                      "  <IncludeNssFileSpecified />\n"         /* Impossible to get. */ \
                                      "  <History />\n"                         /* Impossible to get. */ \
                                      "  <TargetTriplet />\n"                   /* Impossible to get. */ \
                                      "</ProgramInfo>"))) goto end;

    /* Update ProgramInfo context. */
    program_info_ctx->authoring_tool_xml = xml_buf;
    program_info_ctx->authoring_tool_xml_size = strlen(xml_buf);

end:
    if (npdm_acid_b64) free(npdm_acid_b64);

    if (build_type) free(build_type);

    if (sdk_version) free(sdk_version);

    if (!success)
    {
        if (xml_buf) free(xml_buf);
        LOG_MSG_ERROR("Failed to generate ProgramInfo AuthoringTool XML!");
    }

    return success;
}

static bool programInfoGetSdkVersionAndBuildTypeFromSdkNso(ProgramInfoContext *program_info_ctx, char **sdk_version, char **build_type)
{
    if (!program_info_ctx || !program_info_ctx->nso_count || !program_info_ctx->nso_ctx || !sdk_version || !build_type)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    NsoContext *nso_ctx = NULL;
    char *sdk_entry = NULL, *sdk_entry_vender = NULL, *sdk_entry_name = NULL, *sdk_entry_version = NULL, *sdk_entry_build_type = NULL;
    size_t sdk_entry_version_len = 0;
    bool success = true;

    /* Set output pointers to NULL beforehand just in case we have no usable nnSdk information. */
    *sdk_version = *build_type = NULL;

    /* Locate "sdk" NSO. */
    for(u32 i = 0; i < program_info_ctx->nso_count; i++)
    {
        nso_ctx = &(program_info_ctx->nso_ctx[i]);
        if (nso_ctx->nso_filename && !strcmp(nso_ctx->nso_filename, "sdk") && nso_ctx->rodata_api_info_section && nso_ctx->rodata_api_info_section_size) break;
        nso_ctx = NULL;
    }

    /* Check if we found the "sdk" NSO. */
    if (!nso_ctx) goto end;

    /* Look for the "nnSdk" entry in .api_info section. */
    for(u64 i = 0; i < nso_ctx->rodata_api_info_section_size; i++)
    {
        sdk_entry = (nso_ctx->rodata_api_info_section + i);

        if (programInfoIsApiInfoEntryValid("SDK ", 4, sdk_entry, &sdk_entry_vender, NULL, &sdk_entry_name, true)) break;

        i += strlen(sdk_entry);
        sdk_entry = sdk_entry_vender = sdk_entry_name = NULL;
    }

    /* Bail out if we couldn't find the "nnSdk" entry. */
    if (!sdk_entry) goto end;

    /* Get the SDK version and build type. */
    sdk_entry_version = strchr(sdk_entry_name, '-');
    if (!sdk_entry_version) goto end;
    sdk_entry_version++;

    sdk_entry_build_type = strchr(sdk_entry_version, '-');
    if (!sdk_entry_build_type) goto end;
    sdk_entry_build_type++;

    sdk_entry_version_len = (sdk_entry_build_type - sdk_entry_version - 1);

    /* Duplicate strings. */
    if (!(*sdk_version = strndup(sdk_entry_version, sdk_entry_version_len)) || !(*build_type = strdup(sdk_entry_build_type)))
    {
        LOG_MSG_ERROR("Failed to allocate memory for output strings!");

        if (*sdk_version)
        {
            free(*sdk_version);
            *sdk_version = NULL;
        }

        if (*build_type)
        {
            free(*build_type);
            *build_type = NULL;
        }

        success = false;
    }

end:
    return success;
}

static bool programInfoAddNsoApiListToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, ProgramInfoContext *program_info_ctx, const char *api_list_tag, const char *api_entry_prefix, \
                                                       const char *sdk_prefix)
{
    size_t sdk_prefix_len = 0;
    int sdk_entry_vender_len = 0;
    char *sdk_entry = NULL, *sdk_entry_vender = NULL, *sdk_entry_name = NULL;
    bool success = false, api_list_exists = false;

    if (!xml_buf || !xml_buf_size || !program_info_ctx || !program_info_ctx->nso_count || !program_info_ctx->nso_ctx || !api_list_tag || !*api_list_tag || !api_entry_prefix || \
        !*api_entry_prefix || !sdk_prefix || !(sdk_prefix_len = strlen(sdk_prefix)))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Check if any entries for this API list exist. */
    for(u32 i = 0; i < program_info_ctx->nso_count; i++)
    {
        NsoContext *nso_ctx = &(program_info_ctx->nso_ctx[i]);
        if (!nso_ctx->nso_filename || !*(nso_ctx->nso_filename) || !nso_ctx->rodata_api_info_section || !nso_ctx->rodata_api_info_section_size) continue;

        for(u64 j = 0; j < nso_ctx->rodata_api_info_section_size; j++)
        {
            sdk_entry = (nso_ctx->rodata_api_info_section + j);

            if (programInfoIsApiInfoEntryValid(sdk_prefix, sdk_prefix_len, sdk_entry, &sdk_entry_vender, NULL, &sdk_entry_name, false))
            {
                api_list_exists = true;
                break;
            }

            j += strlen(sdk_entry);
        }

        if (api_list_exists) break;
    }

    /* Append an empty XML element if no entries for this API list exist. */
    if (!api_list_exists)
    {
        success = PI_ADD_FMT_STR_T2("  <%sList />\n", api_list_tag);
        goto end;
    }

    if (!PI_ADD_FMT_STR_T2("  <%sList>\n", api_list_tag)) goto end;

    /* Retrieve full API list. */
    for(u32 i = 0; i < program_info_ctx->nso_count; i++)
    {
        NsoContext *nso_ctx = &(program_info_ctx->nso_ctx[i]);
        if (!nso_ctx->nso_filename || !*(nso_ctx->nso_filename) || !nso_ctx->rodata_api_info_section || !nso_ctx->rodata_api_info_section_size) continue;

        for(u64 j = 0; j < nso_ctx->rodata_api_info_section_size; j++)
        {
            sdk_entry = (nso_ctx->rodata_api_info_section + j);

            if (programInfoIsApiInfoEntryValid(sdk_prefix, sdk_prefix_len, sdk_entry, &sdk_entry_vender, &sdk_entry_vender_len, &sdk_entry_name, false))
            {
                if (!PI_ADD_FMT_STR_T2("    <%s>\n" \
                                       "      <%sName>%s</%sName>\n" \
                                       "      <VenderName>%.*s</VenderName>\n" \
                                       "      <NsoName>%s</NsoName>\n" \
                                       "    </%s>\n", \
                                       api_list_tag, \
                                       api_entry_prefix, sdk_entry_name, api_entry_prefix, \
                                       sdk_entry_vender_len, sdk_entry_vender, \
                                       nso_ctx->nso_filename, \
                                       api_list_tag)) goto end;
            }

            j += strlen(sdk_entry);
        }
    }

    success = PI_ADD_FMT_STR_T2("  </%sList>\n", api_list_tag);

end:
    return success;
}

static bool programInfoIsApiInfoEntryValid(const char *sdk_prefix, size_t sdk_prefix_len, char *sdk_entry, char **sdk_entry_vender, int *sdk_entry_vender_len, char **sdk_entry_name, bool nnsdk)
{
    if (!sdk_prefix || !sdk_prefix_len || !sdk_entry || !sdk_entry_vender || !sdk_entry_name || strncmp(sdk_entry, sdk_prefix, sdk_prefix_len) != 0) return false;

    *sdk_entry_vender = strchr(sdk_entry, '+');
    if (!*sdk_entry_vender) return false;
    (*sdk_entry_vender)++;

    *sdk_entry_name = strchr(*sdk_entry_vender, '+');
    if (!*sdk_entry_name) return false;
    (*sdk_entry_name)++;

    if (sdk_entry_vender_len) *sdk_entry_vender_len = (*sdk_entry_name - *sdk_entry_vender - 1);

    int res = strncmp(*sdk_entry_name, g_nnSdkString, g_nnSdkStringLength);
    if ((nnsdk && res != 0) || (!nnsdk && res == 0)) return false;

    return true;
}

static bool programInfoAddStringFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const char *value)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !*tag_name)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    return ((value && *value) ? PI_ADD_FMT_STR_T2("  <%s>%s</%s>\n", tag_name, value, tag_name) : PI_ADD_FMT_STR_T2("  <%s />\n", tag_name));
}

static bool programInfoAddNsoSymbolsToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, ProgramInfoContext *program_info_ctx)
{
    if (!xml_buf || !xml_buf_size || !program_info_ctx || !program_info_ctx->npdm_ctx.meta_header || !program_info_ctx->nso_count || !program_info_ctx->nso_ctx)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    NsoContext *nso_ctx = NULL;
    bool success = false, symbols_exist = false, is_64bit = (program_info_ctx->npdm_ctx.meta_header->flags.is_64bit_instruction == 1);

    char *symbol_str = NULL;
    u64 symbol_size = (!is_64bit ? sizeof(Elf32Symbol) : sizeof(Elf64Symbol));

    /* Locate "main" NSO. */
    for(u32 i = 0; i < program_info_ctx->nso_count; i++)
    {
        nso_ctx = &(program_info_ctx->nso_ctx[i]);
        if (nso_ctx->nso_filename && !strcmp(nso_ctx->nso_filename, "main") && nso_ctx->rodata_dynstr_section && nso_ctx->rodata_dynstr_section_size && \
            nso_ctx->rodata_dynsym_section && nso_ctx->rodata_dynsym_section_size) break;
        nso_ctx = NULL;
    }

    /* Check if we found the "main" NSO. */
    if (!nso_ctx) goto end;

    /* Check if any symbols matching the required filters exist. */
    for(u64 i = 0; i < nso_ctx->rodata_dynsym_section_size; i += symbol_size)
    {
        if ((nso_ctx->rodata_dynsym_section_size - i) < symbol_size) break;

        if (programInfoIsElfSymbolValid(nso_ctx->rodata_dynsym_section + i, nso_ctx->rodata_dynstr_section, nso_ctx->rodata_dynstr_section_size, is_64bit, NULL))
        {
            symbols_exist = true;
            break;
        }
    }

    /* Bail out if we couldn't find any valid symbols. */
    if (!symbols_exist) goto end;

    if (!PI_ADD_FMT_STR_T2("  <UnresolvedApiList>\n")) goto end;

    /* Parse ELF dynamic symbol table to retrieve the symbol strings. */
    for(u64 i = 0; i < nso_ctx->rodata_dynsym_section_size; i += symbol_size)
    {
        if ((nso_ctx->rodata_dynsym_section_size - i) < symbol_size) break;

        if (!programInfoIsElfSymbolValid(nso_ctx->rodata_dynsym_section + i, nso_ctx->rodata_dynstr_section, nso_ctx->rodata_dynstr_section_size, is_64bit, &symbol_str)) continue;

        if (!PI_ADD_FMT_STR_T2("    <UnresolvedApi>\n" \
                               "      <ApiName>%s</ApiName>\n" \
                               "      <NsoName>%s</NsoName>\n" \
                               "    </UnresolvedApi>\n", \
                               symbol_str, \
                               nso_ctx->nso_filename)) goto end;
    }

    success = PI_ADD_FMT_STR_T2("  </UnresolvedApiList>\n");

end:
    /* Append an empty XML element if no valid symbols exist. */
    if (!success && (!nso_ctx || !symbols_exist)) success = PI_ADD_FMT_STR_T2("  <UnresolvedApiList />\n");

    return success;
}

static bool programInfoIsElfSymbolValid(u8 *dynsym_ptr, char *dynstr_base_ptr, u64 dynstr_size, bool is_64bit, char **symbol_str)
{
    if (!dynsym_ptr || !dynstr_base_ptr || !dynstr_size) return false;

    u8 st_type = 0;
    bool is_valid = false;

    if (!is_64bit)
    {
        /* Parse 32-bit ELF symbol. */
        Elf32Symbol *elf32_symbol = (Elf32Symbol*)dynsym_ptr;
        st_type = ELF_ST_TYPE(elf32_symbol->st_info);
        is_valid = (elf32_symbol->st_name < dynstr_size && (st_type == STT_NOTYPE || st_type == STT_FUNC) && elf32_symbol->st_shndx == SHN_UNDEF);
        if (is_valid && symbol_str) *symbol_str = (dynstr_base_ptr + elf32_symbol->st_name);
    } else {
        /* Parse 64-bit ELF symbol. */
        Elf64Symbol *elf64_symbol = (Elf64Symbol*)dynsym_ptr;
        st_type = ELF_ST_TYPE(elf64_symbol->st_info);
        is_valid = (elf64_symbol->st_name < dynstr_size && (st_type == STT_NOTYPE || st_type == STT_FUNC) && elf64_symbol->st_shndx == SHN_UNDEF);
        if (is_valid && symbol_str) *symbol_str = (dynstr_base_ptr + elf64_symbol->st_name);
    }

    return is_valid;
}

static bool programInfoAddFsAccessControlDataToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, ProgramInfoContext *program_info_ctx)
{
    NpdmFsAccessControlData *aci_fac_data = NULL;
    NpdmFsAccessControlDataSaveDataOwnerBlock *save_data_owner_block = NULL;
    u64 *save_data_owner_ids = NULL;
    bool success = false, sdo_data_available = false;

    if (!xml_buf || !xml_buf_size || !program_info_ctx || !(aci_fac_data = program_info_ctx->npdm_ctx.aci_fac_data))
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Check if there's save data owner data available in the FS access control data region from the ACI0 section in the NPDM. */
    sdo_data_available = (aci_fac_data->save_data_owner_info_offset >= sizeof(NpdmFsAccessControlData) && aci_fac_data->save_data_owner_info_size);
    if (!sdo_data_available) goto end;

    /* Get save data owner block and check the ID count. */
    save_data_owner_block = (NpdmFsAccessControlDataSaveDataOwnerBlock*)((u8*)aci_fac_data + aci_fac_data->save_data_owner_info_offset);
    if (!save_data_owner_block->save_data_owner_id_count)
    {
        sdo_data_available = false;
        goto end;
    }

    /* Get save data owner IDs. */
    /* Padding to a 0x4-byte boundary is needed. Each accessibility field takes up a single byte, so we can get away with it by aligning the ID count. */
    save_data_owner_ids = (u64*)((u8*)save_data_owner_block + sizeof(NpdmFsAccessControlDataSaveDataOwnerBlock) + ALIGN_UP(save_data_owner_block->save_data_owner_id_count, 0x4));

    if (!PI_ADD_FMT_STR_T2("  <FsAccessControlData>\n")) goto end;

    /* Append save data owner IDs. */
    for(u32 i = 0; i < save_data_owner_block->save_data_owner_id_count; i++)
    {
        if (!PI_ADD_FMT_STR_T2("    <SaveDataOwnerIds>\n" \
                               "      <Accessibility>%s</Accessibility>\n" \
                               "      <Id>0x%016lx</Id>\n" \
                               "    </SaveDataOwnerIds>\n", \
                               g_facAccessibilityStrings[save_data_owner_block->accessibility[i] & 0x3], \
                               save_data_owner_ids[i])) goto end;
    }

    success = PI_ADD_FMT_STR_T2("  </FsAccessControlData>\n");

end:
    /* Append an empty XML element if no FS access control data exists. */
    if (!success && !sdo_data_available) success = PI_ADD_FMT_STR_T2("  <FsAccessControlData />\n");

    return success;
}
