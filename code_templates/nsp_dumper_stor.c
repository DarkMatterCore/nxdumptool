/*
 * main.c
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

#include "utils.h"
#include "gamecard.h"
#include "title.h"
#include "cnmt.h"
#include "program_info.h"
#include "nacp.h"
#include "legal_info.h"
#include "cert.h"
#include "usb.h"

#define BLOCK_SIZE  0x800000
#define OUTPATH     "/nsp/"

static const char *dump_type_strings[] = {
    "dump base application",
    "dump update",
    "dump dlc"
};

static const u32 dump_type_strings_count = MAX_ELEMENTS(dump_type_strings);

typedef struct {
    char str[64];
    u32 val;
} options_t;

static options_t options[] = {
    { "set download distribution type", 0 },
    { "remove console specific data", 0 },
    { "remove titlekey crypto (overrides previous option)", 0 },
    { "change acid rsa key/sig", 0 },
    { "disable linked account requirement", 0 },
    { "enable screenshots", 0 },
    { "enable video capture", 0 },
    { "output device", 0 }
};

static const u32 options_count = MAX_ELEMENTS(options);

static UsbHsFsDevice *ums_devices = NULL;
static u32 ums_device_count = 0;

static void consolePrint(const char *text, ...)
{
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    consoleUpdate(NULL);
}

static void nspDump(TitleInfo *title_info, u64 free_space)
{
    if (!title_info || !title_info->content_count || !title_info->content_infos) return;
    
    consoleClear();
    
    TitleApplicationMetadata *app_metadata = title_info->app_metadata;
    
    printf("%s info:\n\n", title_info->meta_key.type == NcmContentMetaType_Application ? "base application" : \
                           (title_info->meta_key.type == NcmContentMetaType_Patch ? "update" : "dlc"));
    
    if (app_metadata)
    {
        printf("name: %s\n", app_metadata->lang_entry.name);
        printf("publisher: %s\n", app_metadata->lang_entry.author);
    }
    
    printf("source storage: %s\n", title_info->storage_id == NcmStorageId_GameCard ? "gamecard" : (title_info->storage_id == NcmStorageId_BuiltInUser ? "emmc" : "sd card"));
    printf("title id: %016lX\n", title_info->meta_key.id);
    printf("version: %u (%u.%u.%u-%u.%u)\n", title_info->version.value, title_info->version.major, title_info->version.minor, title_info->version.micro, title_info->version.major_relstep, \
                                             title_info->version.minor_relstep);
    printf("content count: %u\n", title_info->content_count);
    printf("size: %s\n", title_info->size_str);
    printf("______________________________\n\n");
    printf("dump options:\n\n");
    for(u32 i = 0; i < options_count; i++) printf("%s: %s\n", options[i].str, options[i].val ? "yes" : "no");
    printf("______________________________\n\n");
    
    bool set_download_type = (options[0].val == 1);
    bool remove_console_data = (options[1].val == 1);
    bool remove_titlekey_crypto = (options[2].val == 1);
    bool change_acid_rsa = (options[3].val == 1);
    bool patch_sua = (options[4].val == 1);
    bool patch_screenshot = (options[5].val == 1);
    bool patch_video_capture = (options[6].val == 1);
    UsbHsFsDevice *ums_device = (options[7].val == 0 ? NULL : &(ums_devices[options[7].val - 1]));
    bool success = false;
    
    if (ums_device && ums_device->write_protect)
    {
        printf("device \"%s\" has write protection enabled!\n", ums_device->name);
        return;
    }
    
    u8 *buf = NULL;
    char *dump_name = NULL, *path = NULL;
    FILE *fd = NULL;
    
    NcaContext *nca_ctx = NULL;
    
    NcaContext *meta_nca_ctx = NULL;
    ContentMetaContext cnmt_ctx = {0};
    
    ProgramInfoContext *program_info_ctx = NULL;
    u32 program_idx = 0, program_count = titleGetContentCountByType(title_info, NcmContentType_Program);
    
    NacpContext *nacp_ctx = NULL;
    u32 control_idx = 0, control_count = titleGetContentCountByType(title_info, NcmContentType_Control);
    
    LegalInfoContext *legal_info_ctx = NULL;
    u32 legal_info_idx = 0, legal_info_count = titleGetContentCountByType(title_info, NcmContentType_LegalInformation);
    
    Ticket tik = {0};
    TikCommonBlock *tik_common_block = NULL;
    
    u8 *raw_cert_chain = NULL;
    u64 raw_cert_chain_size = 0;
    
    PartitionFileSystemFileContext pfs_file_ctx = {0};
    pfsInitializeFileContext(&pfs_file_ctx);
    
    char entry_name[64] = {0};
    u64 nsp_header_size = 0, nsp_size = 0, nsp_offset = 0;
    
    Sha256Context sha256_ctx = {0};
    u8 sha256_hash[SHA256_HASH_SIZE] = {0};
    
    /* Allocate memory for the dump process. */
    if (!(buf = usbAllocatePageAlignedBuffer(BLOCK_SIZE)))
    {
        consolePrint("buf alloc failed\n");
        goto end;
    }
    
    /* Generate output path. */
    if (!(dump_name = titleGenerateFileName(title_info, TitleFileNameConvention_Full, ums_device ? TitleFileNameIllegalCharReplaceType_IllegalFsChars : TitleFileNameIllegalCharReplaceType_KeepAsciiCharsOnly)))
    {
        consolePrint("title generate file name failed\n");
        goto end;
    }
    
    sprintf(entry_name, "%s" OUTPATH, ums_device ? ums_device->name : "sdmc:");
    if (!(path = utilsGeneratePath(entry_name, dump_name, ".nsp")))
    {
        consolePrint("generate path failed\n");
        goto end;
    }
    
    if (!(nca_ctx = calloc(title_info->content_count, sizeof(NcaContext))))
    {
        consolePrint("nca ctx calloc failed\n");
        goto end;
    }
    
    if (program_count && !(program_info_ctx = calloc(program_count, sizeof(ProgramInfoContext))))
    {
        consolePrint("program info ctx calloc failed\n");
        goto end;
    }
    
    if (control_count && !(nacp_ctx = calloc(control_count, sizeof(NacpContext))))
    {
        consolePrint("nacp ctx calloc failed\n");
        goto end;
    }
    
    if (legal_info_count && !(legal_info_ctx = calloc(legal_info_count, sizeof(LegalInfoContext))))
    {
        consolePrint("legal info ctx calloc failed\n");
        goto end;
    }
    
    // set meta nca as the last nca
    meta_nca_ctx = &(nca_ctx[title_info->content_count - 1]);
    
    if (!ncaInitializeContext(meta_nca_ctx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), \
        titleGetContentInfoByTypeAndIdOffset(title_info, NcmContentType_Meta, 0), &tik))
    {
        consolePrint("Meta nca initialize ctx failed\n");
        goto end;
    }
    
    consolePrint("Meta nca initialize ctx succeeded\n");
    
    if (!cnmtInitializeContext(&cnmt_ctx, meta_nca_ctx))
    {
        consolePrint("cnmt initialize ctx failed\n");
        goto end;
    }
    
    consolePrint("cnmt initialize ctx succeeded (%s)\n", meta_nca_ctx->content_id_str);
    
    // initialize nca context
    // initialize content type context
    // generate nca patches (if needed)
    // generate content type xml
    for(u32 i = 0, j = 0; i < title_info->content_count; i++)
    {
        // skip meta nca since we already initialized it
        NcmContentInfo *content_info = &(title_info->content_infos[i]);
        if (content_info->content_type == NcmContentType_Meta) continue;
        
        NcaContext *cur_nca_ctx = &(nca_ctx[j]);
        if (!ncaInitializeContext(cur_nca_ctx, title_info->storage_id, (title_info->storage_id == NcmStorageId_GameCard ? GameCardHashFileSystemPartitionType_Secure : 0), content_info, &tik))
        {
            consolePrint("%s #%u initialize nca ctx failed\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
            goto end;
        }
        
        consolePrint("%s #%u initialize nca ctx succeeded\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
        
        // don't go any further with this nca if we can't access its fs data because it's pointless
        // to do: add preload warning
        if (cur_nca_ctx->rights_id_available && !cur_nca_ctx->titlekey_retrieved)
        {
            j++;
            continue;
        }
        
        // set download distribution type
        // has no effect if this nca uses NcaDistributionType_Download
        if (set_download_type) ncaSetDownloadDistributionType(cur_nca_ctx);
        
        // remove titlekey crypto
        // has no effect if this nca doesn't use titlekey crypto
        if (remove_titlekey_crypto && !ncaRemoveTitlekeyCrypto(cur_nca_ctx))
        {
            consolePrint("nca remove titlekey crypto failed\n");
            goto end;
        }
        
        switch(content_info->content_type)
        {
            case NcmContentType_Program:
            {
                ProgramInfoContext *cur_program_info_ctx = &(program_info_ctx[program_idx]);
                
                if (!programInfoInitializeContext(cur_program_info_ctx, cur_nca_ctx))
                {
                    consolePrint("initialize program info ctx failed (%s)\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
                
                if (change_acid_rsa && !programInfoGenerateNcaPatch(cur_program_info_ctx))
                {
                    consolePrint("program info nca patch failed (%s)\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
                
                if (!programInfoGenerateAuthoringToolXml(cur_program_info_ctx))
                {
                    consolePrint("program info xml failed (%s)\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
                
                program_idx++;
                
                consolePrint("initialize program info ctx succeeded (%s)\n", cur_nca_ctx->content_id_str);
                
                break;
            }
            case NcmContentType_Control:
            {
                NacpContext *cur_nacp_ctx = &(nacp_ctx[control_idx]);
                
                if (!nacpInitializeContext(cur_nacp_ctx, cur_nca_ctx))
                {
                    consolePrint("initialize nacp ctx failed (%s)\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
                
                if (!nacpGenerateNcaPatch(cur_nacp_ctx, patch_sua, patch_screenshot, patch_video_capture))
                {
                    consolePrint("nacp nca patch failed (%s)\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
                
                if (!nacpGenerateAuthoringToolXml(cur_nacp_ctx, title_info->version.value, cnmtGetRequiredTitleVersion(&cnmt_ctx)))
                {
                    consolePrint("nacp xml failed (%s)\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
                
                control_idx++;
                
                consolePrint("initialize nacp ctx succeeded (%s)\n", cur_nca_ctx->content_id_str);
                
                break;
            }
            case NcmContentType_LegalInformation:
            {
                LegalInfoContext *cur_legal_info_ctx = &(legal_info_ctx[legal_info_idx]);
                
                if (!legalInfoInitializeContext(cur_legal_info_ctx, cur_nca_ctx))
                {
                    consolePrint("initialize legal info ctx failed (%s)\n", cur_nca_ctx->content_id_str);
                    goto end;
                }
                
                legal_info_idx++;
                
                consolePrint("initialize legal info ctx succeeded (%s)\n", cur_nca_ctx->content_id_str);
                
                break;
            }
            default:
                break;
        }
        
        if (!ncaEncryptHeader(cur_nca_ctx))
        {
            consolePrint("%s #%u encrypt nca header failed\n", titleGetNcmContentTypeName(content_info->content_type), content_info->id_offset);
            goto end;
        }
        
        j++;
    }
    
    // generate cnmt xml right away even though we don't yet have all the data we need
    // This is because we need its size to calculate the full nsp size
    if (!cnmtGenerateAuthoringToolXml(&cnmt_ctx, nca_ctx, title_info->content_count))
    {
        consolePrint("cnmt xml #1 failed\n");
        goto end;
    }
    
    bool retrieve_tik_cert = (!remove_titlekey_crypto && tik.size > 0);
    if (retrieve_tik_cert)
    {
        if (!(tik_common_block = tikGetCommonBlock(tik.data)))
        {
            consolePrint("tik common block failed");
            goto end;
        }
        
        if (remove_console_data && tik_common_block->titlekey_type == TikTitleKeyType_Personalized)
        {
            if (!tikConvertPersonalizedTicketToCommonTicket(&tik, &raw_cert_chain, &raw_cert_chain_size))
            {
                consolePrint("tik convert failed\n");
                goto end;
            }
        } else {
            raw_cert_chain = (title_info->storage_id == NcmStorageId_GameCard ? certRetrieveRawCertificateChainFromGameCardByRightsId(&(tik_common_block->rights_id), &raw_cert_chain_size) : \
                                                                                certGenerateRawCertificateChainBySignatureIssuer(tik_common_block->issuer, &raw_cert_chain_size));
            if (!raw_cert_chain)
            {
                consolePrint("cert failed\n");
                goto end;
            }
        }
    }
    
    // add nca info
    for(u32 i = 0; i < title_info->content_count; i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        sprintf(entry_name, "%s.%s", cur_nca_ctx->content_id_str, cur_nca_ctx->content_type == NcmContentType_Meta ? "cnmt.nca" : "nca");
        
        if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_nca_ctx->content_size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }
    
    // add cnmt xml info
    sprintf(entry_name, "%s.cnmt.xml", meta_nca_ctx->content_id_str);
    if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cnmt_ctx.authoring_tool_xml_size, &(meta_nca_ctx->content_type_ctx_data_idx)))
    {
        consolePrint("pfs add entry failed: %s\n", entry_name);
        goto end;
    }
    
    // add content type ctx data info
    for(u32 i = 0; i < (title_info->content_count - 1); i++)
    {
        bool ret = false;
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        if (!cur_nca_ctx->content_type_ctx) continue;
        
        switch(cur_nca_ctx->content_type)
        {
            case NcmContentType_Program:
            {
                ProgramInfoContext *cur_program_info_ctx = (ProgramInfoContext*)cur_nca_ctx->content_type_ctx;
                sprintf(entry_name, "%s.programinfo.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_program_info_ctx->authoring_tool_xml_size, &(cur_nca_ctx->content_type_ctx_data_idx));
                break;
            }
            case NcmContentType_Control:
            {
                NacpContext *cur_nacp_ctx = (NacpContext*)cur_nca_ctx->content_type_ctx;
                
                for(u8 j = 0; j < cur_nacp_ctx->icon_count; j++)
                {
                    NacpIconContext *icon_ctx = &(cur_nacp_ctx->icon_ctx[j]);
                    sprintf(entry_name, "%s.nx.%s.jpg", cur_nca_ctx->content_id_str, nacpGetLanguageString(icon_ctx->language));
                    if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, icon_ctx->icon_size, j == 0 ? &(cur_nca_ctx->content_type_ctx_data_idx) : NULL))
                    {
                        consolePrint("pfs add entry failed: %s\n", entry_name);
                        goto end;
                    }
                }
                
                sprintf(entry_name, "%s.nacp.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_nacp_ctx->authoring_tool_xml_size, NULL);
                break;
            }
            case NcmContentType_LegalInformation:
            {
                LegalInfoContext *cur_legal_info_ctx = (LegalInfoContext*)cur_nca_ctx->content_type_ctx;
                sprintf(entry_name, "%s.legalinfo.xml", cur_nca_ctx->content_id_str);
                ret = pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_legal_info_ctx->authoring_tool_xml_size, &(cur_nca_ctx->content_type_ctx_data_idx));
                break;
            }
            default:
                break;
        }
        
        if (!ret)
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }
    
    // add ticket and cert info
    if (retrieve_tik_cert)
    {
        sprintf(entry_name, "%s.tik", tik.rights_id_str);
        if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, tik.size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
        
        sprintf(entry_name, "%s.cert", tik.rights_id_str);
        if (!pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, raw_cert_chain_size, NULL))
        {
            consolePrint("pfs add entry failed: %s\n", entry_name);
            goto end;
        }
    }
    
    // write buffer to memory buffer
    if (!pfsWriteFileContextHeaderToMemoryBuffer(&pfs_file_ctx, buf, BLOCK_SIZE, &nsp_header_size))
    {
        consolePrint("pfs write header to mem #1 failed\n");
        goto end;
    }
    
    nsp_size = (nsp_header_size + pfs_file_ctx.fs_size);
    consolePrint("nsp header size: 0x%lX | nsp size: 0x%lX\n", nsp_header_size, nsp_size);
    
    if (nsp_size >= free_space)
    {
        consolePrint("nsp size exceeds free space\n");
        goto end;
    }
    
    if (ums_device && ums_device->fs_type < UsbHsFsDeviceFileSystemType_exFAT && nsp_size > FAT32_FILESIZE_LIMIT)
    {
        consolePrint("split dumps not supported for FAT12/16/32 volumes in UMS devices (yet)\n");
        goto end;
    }
    
    utilsCreateDirectoryTree(path, false);
    
    if (nsp_size > FAT32_FILESIZE_LIMIT && !utilsCreateConcatenationFile(path))
    {
        consolePrint("create concatenation file failed\n");
        goto end;
    }
    
    if (!(fd = fopen(path, "wb")))
    {
        consolePrint("fopen failed\n");
        goto end;
    }
    
    consolePrint("dump process started. please wait...\n");
    
    time_t start = time(NULL);
    
    // write placeholder header
    memset(buf, 0, nsp_header_size);
    fwrite(buf, 1, nsp_header_size, fd);
    nsp_offset += nsp_header_size;
    
    // write ncas
    for(u32 i = 0; i < title_info->content_count; i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        u64 blksize = BLOCK_SIZE;
        
        memset(&sha256_ctx, 0, sizeof(Sha256Context));
        sha256ContextCreate(&sha256_ctx);
        
        if (cur_nca_ctx->content_type == NcmContentType_Meta && (!cnmtGenerateNcaPatch(&cnmt_ctx) || !ncaEncryptHeader(cur_nca_ctx)))
        {
            consolePrint("cnmt generate patch failed\n");
            goto end;
        }
        
        bool dirty_header = ncaIsHeaderDirty(cur_nca_ctx);
        
        for(u64 offset = 0; offset < cur_nca_ctx->content_size; offset += blksize, nsp_offset += blksize)
        {
            if ((cur_nca_ctx->content_size - offset) < blksize) blksize = (cur_nca_ctx->content_size - offset);
            
            // read nca chunk
            if (!ncaReadContentFile(cur_nca_ctx, buf, blksize, offset))
            {
                consolePrint("nca read failed at 0x%lX for \"%s\"\n", offset, cur_nca_ctx->content_id_str);
                goto end;
            }
            
            if (dirty_header)
            {
                // write re-encrypted headers
                if (!cur_nca_ctx->header_written) ncaWriteEncryptedHeaderDataToMemoryBuffer(cur_nca_ctx, buf, blksize, offset);
                
                if (cur_nca_ctx->content_type_ctx_patch)
                {
                    // write content type context patch
                    switch(cur_nca_ctx->content_type)
                    {
                        case NcmContentType_Meta:
                            cnmtWriteNcaPatch(&cnmt_ctx, buf, blksize, offset);
                            break;
                        case NcmContentType_Program:
                            programInfoWriteNcaPatch((ProgramInfoContext*)cur_nca_ctx->content_type_ctx, buf, blksize, offset);
                            break;
                        case NcmContentType_Control:
                            nacpWriteNcaPatch((NacpContext*)cur_nca_ctx->content_type_ctx, buf, blksize, offset);
                            break;
                        default:
                            break;
                    }
                }
                
                // update flag to avoid entering this code block if it's not needed anymore
                dirty_header = (!cur_nca_ctx->header_written || cur_nca_ctx->content_type_ctx_patch);
            }
            
            // update hash calculation
            sha256ContextUpdate(&sha256_ctx, buf, blksize);
            
            // write nca chunk
            fwrite(buf, 1, blksize, fd);
        }
        
        // get hash
        sha256ContextGetHash(&sha256_ctx, sha256_hash);
        
        // update content id and hash
        ncaUpdateContentIdAndHash(cur_nca_ctx, sha256_hash);
        
        // update cnmt
        if (!cnmtUpdateContentInfo(&cnmt_ctx, cur_nca_ctx))
        {
            consolePrint("cnmt update content info failed\n");
            goto end;
        }
        
        // update pfs entry name
        if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, i, cur_nca_ctx->content_id_str))
        {
            consolePrint("pfs update entry name failed for nca \"%s\"\n", cur_nca_ctx->content_id_str);
            goto end;
        }
    }
    
    // regenerate cnmt xml
    if (!cnmtGenerateAuthoringToolXml(&cnmt_ctx, nca_ctx, title_info->content_count))
    {
        consolePrint("cnmt xml #2 failed\n");
        goto end;
    }
    
    // write cnmt xml
    fwrite(cnmt_ctx.authoring_tool_xml, 1, cnmt_ctx.authoring_tool_xml_size, fd);
    nsp_offset += cnmt_ctx.authoring_tool_xml_size;
    
    // update cnmt xml pfs entry name
    if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, meta_nca_ctx->content_type_ctx_data_idx, meta_nca_ctx->content_id_str))
    {
        consolePrint("pfs update entry name cnmt xml failed\n");
        goto end;
    }
    
    // write content type ctx data
    for(u32 i = 0; i < (title_info->content_count - 1); i++)
    {
        NcaContext *cur_nca_ctx = &(nca_ctx[i]);
        if (!cur_nca_ctx->content_type_ctx) continue;
        
        char *authoring_tool_xml = NULL;
        u64 authoring_tool_xml_size = 0;
        u32 data_idx = cur_nca_ctx->content_type_ctx_data_idx;
        
        switch(cur_nca_ctx->content_type)
        {
            case NcmContentType_Program:
            {
                ProgramInfoContext *cur_program_info_ctx = (ProgramInfoContext*)cur_nca_ctx->content_type_ctx;
                authoring_tool_xml = cur_program_info_ctx->authoring_tool_xml;
                authoring_tool_xml_size = cur_program_info_ctx->authoring_tool_xml_size;
                break;
            }
            case NcmContentType_Control:
            {
                NacpContext *cur_nacp_ctx = (NacpContext*)cur_nca_ctx->content_type_ctx;
                authoring_tool_xml = cur_nacp_ctx->authoring_tool_xml;
                authoring_tool_xml_size = cur_nacp_ctx->authoring_tool_xml_size;
                
                // loop through available icons
                for(u8 j = 0; j < cur_nacp_ctx->icon_count; j++)
                {
                    NacpIconContext *icon_ctx = &(cur_nacp_ctx->icon_ctx[j]);
                    
                    // write icon
                    fwrite(icon_ctx->icon_data, 1, icon_ctx->icon_size, fd);
                    nsp_offset += icon_ctx->icon_size;
                    
                    // update pfs entry name
                    if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, data_idx++, cur_nca_ctx->content_id_str))
                    {
                        consolePrint("pfs update entry name failed for icon \"%s\" (%u)\n", cur_nca_ctx->content_id_str, icon_ctx->language);
                        goto end;
                    }
                }
                
                break;
            }
            case NcmContentType_LegalInformation:
            {
                LegalInfoContext *cur_legal_info_ctx = (LegalInfoContext*)cur_nca_ctx->content_type_ctx;
                authoring_tool_xml = cur_legal_info_ctx->authoring_tool_xml;
                authoring_tool_xml_size = cur_legal_info_ctx->authoring_tool_xml_size;
                break;
            }
            default:
                break;
        }
        
        // write xml
        fwrite(authoring_tool_xml, 1, authoring_tool_xml_size, fd);
        nsp_offset += authoring_tool_xml_size;
        
        // update pfs entry name
        if (!pfsUpdateEntryNameFromFileContext(&pfs_file_ctx, data_idx, cur_nca_ctx->content_id_str))
        {
            consolePrint("pfs update entry name failed for xml \"%s\"\n", cur_nca_ctx->content_id_str);
            goto end;
        }
    }
    
    if (retrieve_tik_cert)
    {
        // write ticket
        fwrite(tik.data, 1, tik.size, fd);
        nsp_offset += tik.size;
        
        // write cert
        fwrite(raw_cert_chain, 1, raw_cert_chain_size, fd);
        nsp_offset += raw_cert_chain_size;
    }
    
    // write new pfs0 header
    rewind(fd);
    
    if (!pfsWriteFileContextHeaderToMemoryBuffer(&pfs_file_ctx, buf, BLOCK_SIZE, &nsp_header_size))
    {
        consolePrint("pfs write header to mem #2 failed\n");
        goto end;
    }
    
    fwrite(buf, 1, nsp_header_size, fd);
    
    start = (time(NULL) - start);
    consolePrint("process successfully completed in %lu seconds!\n", start);
    
    success = true;
    
end:
    if (fd)
    {
        fclose(fd);
        if (!success) utilsRemoveConcatenationFile(path);
        utilsCommitSdCardFileSystemChanges();
    }
    
    pfsFreeFileContext(&pfs_file_ctx);
    
    if (raw_cert_chain) free(raw_cert_chain);
    
    if (legal_info_ctx)
    {
        for(u32 i = 0; i < legal_info_count; i++) legalInfoFreeContext(&(legal_info_ctx[i]));
        free(legal_info_ctx);
    }
    
    if (nacp_ctx)
    {
        for(u32 i = 0; i < control_count; i++) nacpFreeContext(&(nacp_ctx[i]));
        free(nacp_ctx);
    }
    
    if (program_info_ctx)
    {
        for(u32 i = 0; i < program_count; i++) programInfoFreeContext(&(program_info_ctx[i]));
        free(program_info_ctx);
    }
    
    cnmtFreeContext(&cnmt_ctx);
    
    if (nca_ctx) free(nca_ctx);
    
    if (path) free(path);
    
    if (dump_name) free(dump_name);
    
    if (buf) free(buf);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    int ret = 0;
    
    consoleInit(NULL);
    
    consolePrint("initializing...\n");
    
    if (!utilsInitializeResources())
    {
        ret = -1;
        goto out;
    }
    
    u32 app_count = 0;
    TitleApplicationMetadata **app_metadata = NULL;
    TitleUserApplicationData user_app_data = {0};
    TitleInfo *title_info = NULL;
    
    u32 menu = 0, selected_idx = 0, scroll = 0, page_size = 30;
    
    u32 title_idx = 0, title_scroll = 0;
    u32 type_idx = 0, type_scroll = 0;
    u32 list_count = 0, list_idx = 0;
    
    u64 device_total_fs_size = 0, device_free_fs_size = 0;
    char device_total_fs_size_str[36] = {0}, device_free_fs_size_str[32] = {0}, device_info[0x40] = {0};
    bool device_retrieved_size = false, device_retrieved_info = false;
    
    app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
    if (!app_metadata || !app_count)
    {
        consolePrint("app metadata failed\n");
        goto out2;
    }
    
    consolePrint("app metadata succeeded\n");
    
    ums_devices = umsGetDevices(&ums_device_count);
    
    utilsSleep(1);
    
    while(true)
    {
        consoleClear();
        
        printf("press b to %s.", menu == 0 ? "exit" : "go back");
        if (ums_device_count) printf(" press x to safely remove all ums devices.");
        printf("\n______________________________\n\n");
        
        if (menu == 0)
        {
            printf("title: %u / %u\n", selected_idx + 1, app_count);
            printf("selected title: %016lX - %s\n", app_metadata[selected_idx]->title_id, app_metadata[selected_idx]->lang_entry.name);
        } else {
            printf("title info:\n\n");
            printf("name: %s\n", app_metadata[title_idx]->lang_entry.name);
            printf("publisher: %s\n", app_metadata[title_idx]->lang_entry.author);
            printf("title id: %016lX\n", app_metadata[title_idx]->title_id);
            
            if (menu == 2)
            {
                printf("______________________________\n\n");
                
                if (title_info->previous || title_info->next)
                {
                    printf("press zl/l and/or zr/r to change the selected title\n");
                    printf("title: %u / %u\n", list_idx, list_count);
                    printf("______________________________\n\n");
                }
                
                printf("selected %s info:\n\n", title_info->meta_key.type == NcmContentMetaType_Application ? "base application" : \
                                                (title_info->meta_key.type == NcmContentMetaType_Patch ? "update" : "dlc"));
                printf("source storage: %s\n", title_info->storage_id == NcmStorageId_GameCard ? "gamecard" : (title_info->storage_id == NcmStorageId_BuiltInUser ? "emmc" : "sd card"));
                if (title_info->meta_key.type != NcmContentMetaType_Application) printf("title id: %016lX\n", title_info->meta_key.id);
                printf("version: %u (%u.%u.%u-%u.%u)\n", title_info->version.value, title_info->version.major, title_info->version.minor, title_info->version.micro, title_info->version.major_relstep, \
                                                         title_info->version.minor_relstep);
                printf("content count: %u\n", title_info->content_count);
                printf("size: %s\n", title_info->size_str);
            }
        }
        
        printf("______________________________\n\n");
        
        u32 max_val = (menu == 0 ? app_count : (menu == 1 ? dump_type_strings_count : (1 + options_count)));
        for(u32 i = scroll; i < max_val; i++)
        {
            if (i >= (scroll + page_size)) break;
            
            printf("%s", i == selected_idx ? " -> " : "    ");
            
            if (menu == 0)
            {
                printf("%016lX - %s\n", app_metadata[i]->title_id, app_metadata[i]->lang_entry.name);
            } else
            if (menu == 1)
            {
                printf("%s\n", dump_type_strings[i]);
            } else
            if (menu == 2)
            {
                if (i == 0)
                {
                    printf("start nsp dump\n");
                } else
                if (i < options_count)
                {
                    printf("%s: < %s >\n", options[i - 1].str, options[i - 1].val ? "yes" : "no");
                } else {
                    u32 device_idx = options[i - 1].val;
                    
                    printf("%s: ", options[i - 1].str);
                    
                    if (!device_retrieved_size)
                    {
                        sprintf(device_total_fs_size_str, "%s/", device_idx == 0 ? "sdmc:" : ums_devices[device_idx - 1].name);
                        utilsGetFileSystemStatsByPath(device_total_fs_size_str, &device_total_fs_size, &device_free_fs_size);
                        utilsGenerateFormattedSizeString(device_total_fs_size, device_total_fs_size_str, sizeof(device_total_fs_size_str));
                        utilsGenerateFormattedSizeString(device_free_fs_size, device_free_fs_size_str, sizeof(device_free_fs_size_str));
                        device_retrieved_size = true;
                    }
                    
                    if (device_idx == 0)
                    {
                        printf("< sdmc: (%s / %s) >\n", device_free_fs_size_str, device_total_fs_size_str);
                    } else {
                        UsbHsFsDevice *ums_device = &(ums_devices[device_idx - 1]);
                        
                        if (!device_retrieved_info)
                        {
                            if (ums_device->product_id[0])
                            {
                                sprintf(device_info, "%s, LUN %u, FS #%u, %s", ums_device->product_id, ums_device->lun, ums_device->fs_idx, LIBUSBHSFS_FS_TYPE_STR(ums_device->fs_type));
                            } else {
                                sprintf(device_info, "LUN %u, FS #%u, %s", ums_device->lun, ums_device->fs_idx, LIBUSBHSFS_FS_TYPE_STR(ums_device->fs_type));
                            }
                            
                            device_retrieved_info = true;
                        }
                        
                        printf("< %s (%s) (%s / %s) >", ums_device->name, device_info, device_free_fs_size_str, device_total_fs_size_str);
                    }
                }
            }
        }
        
        printf("\n");
        
        consoleUpdate(NULL);
        
        bool data_update = false;
        u64 btn_down = 0, btn_held = 0;
        
        while(true)
        {
            utilsScanPads();
            btn_down = utilsGetButtonsDown();
            btn_held = utilsGetButtonsHeld();
            if (btn_down || btn_held) break;
            
            if (titleIsGameCardInfoUpdated())
            {
                free(app_metadata);
                
                app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
                if (!app_metadata)
                {
                    consolePrint("\napp metadata failed\n");
                    goto out2;
                }
                
                menu = selected_idx = scroll = 0;
                
                title_idx = title_scroll = 0;
                type_idx = type_scroll = 0;
                list_count = list_idx = 0;
                
                data_update = true;
                
                break;
            }
            
            if (umsIsDeviceInfoUpdated())
            {
                free(ums_devices);
                
                ums_devices = umsGetDevices(&ums_device_count);
                
                options[options_count - 1].val = 0;
                device_retrieved_size = device_retrieved_info = false;
                
                data_update = true;
                
                break;
            }
        }
        
        if (data_update) continue;
        
        if (btn_down & HidNpadButton_A)
        {
            bool error = false;
            
            if (menu == 0)
            {
                title_idx = selected_idx;
                title_scroll = scroll;
            } else
            if (menu == 1)
            {
                type_idx = selected_idx;
                type_scroll = scroll;
            }
            
            menu++;
            
            if (menu == 3 && selected_idx != 0)
            {
                menu--;
                continue;
            }
            
            if (menu == 1)
            {
                if (!titleGetUserApplicationData(app_metadata[title_idx]->title_id, &user_app_data))
                {
                    consolePrint("\nget user application data failed!\n");
                    error = true;
                }
            } else
            if (menu == 2)
            {
                if ((type_idx == 0 && !user_app_data.app_info) || (type_idx == 1 && !user_app_data.patch_info) || (type_idx == 2 && !user_app_data.aoc_info))
                {
                    consolePrint("\nthe selected title doesn't have available %s data\n", type_idx == 0 ? "base application" : (type_idx == 1 ? "update" : "dlc"));
                    error = true;
                } else {
                    title_info = (type_idx == 0 ? user_app_data.app_info : (type_idx == 1 ? user_app_data.patch_info : user_app_data.aoc_info));
                    list_count = titleGetCountFromInfoBlock(title_info);
                    list_idx = 1;
                }
            } else
            if (menu == 3)
            {
                consoleClear();
                utilsChangeHomeButtonBlockStatus(true);
                nspDump(title_info, device_free_fs_size);
                utilsChangeHomeButtonBlockStatus(false);
            }
            
            if (error || menu >= 3)
            {
                consolePrint("press any button to continue\n");
                utilsWaitForButtonPress(0);
                menu--;
            } else {
                selected_idx = scroll = 0;
            }
        } else
        if ((btn_down & HidNpadButton_Down) || (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown)))
        {
            selected_idx++;
            
            if (selected_idx >= max_val)
            {
                if (btn_down & HidNpadButton_Down)
                {
                    selected_idx = scroll = 0;
                } else {
                    selected_idx = (max_val - 1);
                }
            } else
            if (selected_idx >= (scroll + (page_size / 2)) && max_val > (scroll + page_size))
            {
                scroll++;
            }
        } else
        if ((btn_down & HidNpadButton_Up) || (btn_held & (HidNpadButton_StickLUp | HidNpadButton_StickRUp)))
        {
            selected_idx--;
            
            if (selected_idx == UINT32_MAX)
            {
                if (btn_down & HidNpadButton_Up)
                {
                    selected_idx = (max_val - 1);
                    scroll = (max_val >= page_size ? (max_val - page_size) : 0);
                } else {
                    selected_idx = 0;
                }
            } else
            if (selected_idx < (scroll + (page_size / 2)) && scroll > 0)
            {
                scroll--;
            }
        } else
        if (btn_down & HidNpadButton_B)
        {
            menu--;
            
            if (menu == UINT32_MAX)
            {
                break;
            } else {
                selected_idx = (menu == 0 ? title_idx : type_idx);
                scroll = (menu == 0 ? title_scroll : type_scroll);
            }
        } else
        if ((btn_down & (HidNpadButton_Left | HidNpadButton_Right)) && menu == 2 && selected_idx != 0)
        {
            if (selected_idx < options_count)
            {
                options[selected_idx - 1].val ^= 1;
            } else {
                bool left = (btn_down & HidNpadButton_Left);
                u32 *device_idx = &(options[selected_idx - 1].val), orig_device_idx = *device_idx;
                
                if (left)
                {
                    (*device_idx)--;
                    if (*device_idx == UINT32_MAX) *device_idx = ums_device_count;
                } else {
                    (*device_idx)++;
                    if (*device_idx > ums_device_count) *device_idx = 0;
                }
                
                if (*device_idx != orig_device_idx) device_retrieved_size = device_retrieved_info = false;
            }
        } else
        if ((btn_down & (HidNpadButton_L | HidNpadButton_ZL)) && menu == 2 && title_info->previous)
        {
            title_info = title_info->previous;
            list_idx--;
        } else
        if ((btn_down & (HidNpadButton_R | HidNpadButton_ZR)) && menu == 2 && title_info->next)
        {
            title_info = title_info->next;
            list_idx++;
        } else
        if ((btn_down & HidNpadButton_X) && ums_device_count)
        {
            for(u32 i = 0; i < ums_device_count; i++) usbHsFsUnmountDevice(&(ums_devices[i]), false);
            
            options[options_count - 1].val = 0;
            
            free(ums_devices);
            ums_devices = NULL;
            
            ums_device_count = 0;
        }
        
        if (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) svcSleepThread(50000000); // 50 ms
    }
    
out2:
    if (menu != UINT32_MAX)
    {
        consolePrint("press any button to exit\n");
        utilsWaitForButtonPress(0);
    }
    
    if (ums_devices) free(ums_devices);
    
    if (app_metadata) free(app_metadata);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
