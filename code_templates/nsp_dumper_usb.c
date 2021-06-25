/*
 * main.c
 *
 * Copyright (c) 2020, DarkMatterCore <pabloacurielz@gmail.com>.
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
#include "gamecard.h"
#include "title.h"
#include "cnmt.h"
#include "program_info.h"
#include "nacp.h"
#include "legal_info.h"
#include "cert.h"
#include "usb.h"

#define BLOCK_SIZE  0x800000

static PadState g_padState = {0};

typedef struct
{
    void *data;
    size_t data_written;
    size_t total_size;
    bool error;
    bool transfer_cancelled;
} ThreadSharedData;

static const char *dump_type_strings[] = {
    "dump base application",
    "dump update",
    "dump dlc"
};

static const u32 dump_type_strings_count = MAX_ELEMENTS(dump_type_strings);

typedef struct {
    char str[64];
    bool val;
} options_t;

static options_t options[] = {
    { "set download distribution type", false },
    { "remove console specific data", false },
    { "remove titlekey crypto (overrides previous option)", false },
    { "change acid rsa key/sig", false },
    { "disable linked account requirement", false },
    { "enable screenshots", false },
    { "enable video capture", false },
    { "disable hdcp", false }
};

static const u32 options_count = MAX_ELEMENTS(options);

static Mutex g_conMutex = 0;

static void utilsScanPads(void)
{
    padUpdate(&g_padState);
}

static u64 utilsGetButtonsDown(void)
{
    return padGetButtonsDown(&g_padState);
}

static u64 utilsGetButtonsHeld(void)
{
    return padGetButtons(&g_padState);
}

static void utilsWaitForButtonPress(u64 flag)
{
    /* Don't consider stick movement as button inputs. */
    if (!flag) flag = ~(HidNpadButton_StickLLeft | HidNpadButton_StickLRight | HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickRLeft | HidNpadButton_StickRRight | \
                        HidNpadButton_StickRUp | HidNpadButton_StickRDown);
    
    while(appletMainLoop())
    {
        utilsScanPads();
        if (utilsGetButtonsDown() & flag) break;
    }
}

static void consolePrint(const char *text, ...)
{
    mutexLock(&g_conMutex);
    va_list v;
    va_start(v, text);
    vfprintf(stdout, text, v);
    va_end(v);
    mutexUnlock(&g_conMutex);
}

static void consoleRefresh(void)
{
    mutexLock(&g_conMutex);
    consoleUpdate(NULL);
    mutexUnlock(&g_conMutex);
}

static void dump_thread_func(void *arg)
{
    ThreadSharedData *shared_data = (ThreadSharedData*)arg;
    
    TitleInfo *title_info = NULL;
    
    bool set_download_type = options[0].val;
    bool remove_console_data = options[1].val;
    bool remove_titlekey_crypto = options[2].val;
    bool change_acid_rsa = options[3].val;
    bool patch_sua = options[4].val;
    bool patch_screenshot = options[5].val;
    bool patch_video_capture = options[6].val;
    bool patch_hdcp = options[7].val;
    bool success = false;
    
    u8 *buf = NULL;
    char *dump_name = NULL, *path = NULL;
    
    NcaContext *nca_ctx = NULL;
    
    NcaContext *meta_nca_ctx = NULL;
    ContentMetaContext cnmt_ctx = {0};
    
    ProgramInfoContext *program_info_ctx = NULL;
    u32 program_idx = 0, program_count = 0;
    
    NacpContext *nacp_ctx = NULL;
    u32 control_idx = 0, control_count = 0;
    
    LegalInfoContext *legal_info_ctx = NULL;
    u32 legal_info_idx = 0, legal_info_count = 0;
    
    Ticket tik = {0};
    TikCommonBlock *tik_common_block = NULL;
    
    u8 *raw_cert_chain = NULL;
    u64 raw_cert_chain_size = 0;
    
    PartitionFileSystemFileContext pfs_file_ctx = {0};
    pfsInitializeFileContext(&pfs_file_ctx);
    
    char entry_name[64] = {0};
    u64 nsp_header_size = 0, nsp_size = 0, nsp_offset = 0;
    char *tmp_name = NULL;
    
    Sha256Context sha256_ctx = {0};
    u8 sha256_hash[SHA256_HASH_SIZE] = {0};
    
    if (!shared_data || !(title_info = (TitleInfo*)shared_data->data) || !title_info->content_count || !title_info->content_infos) goto end;
    
    program_count = titleGetContentCountByType(title_info, NcmContentType_Program);
    control_count = titleGetContentCountByType(title_info, NcmContentType_Control);
    legal_info_count = titleGetContentCountByType(title_info, NcmContentType_LegalInformation);
    
    /* Allocate memory for the dump process. */
    if (!(buf = usbAllocatePageAlignedBuffer(BLOCK_SIZE)))
    {
        consolePrint("buf alloc failed\n");
        goto end;
    }
    
    /* Generate output path. */
    if (!(dump_name = titleGenerateFileName(title_info, TitleFileNameConvention_Full, TitleFileNameIllegalCharReplaceType_IllegalFsChars)))
    {
        consolePrint("title generate file name failed\n");
        goto end;
    }
    
    if (!(path = utilsGeneratePath(NULL, dump_name, ".nsp")))
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
        // TODO: add preload warning
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
        if (remove_titlekey_crypto && !ncaRemoveTitleKeyCrypto(cur_nca_ctx))
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
                
                if (!nacpGenerateNcaPatch(cur_nacp_ctx, patch_sua, patch_screenshot, patch_video_capture, patch_hdcp))
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
                ret = pfsAddEntryInformationToFileContext(&pfs_file_ctx, entry_name, cur_nacp_ctx->authoring_tool_xml_size, !cur_nacp_ctx->icon_count ? &(cur_nca_ctx->content_type_ctx_data_idx) : NULL);
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
    
    consolePrint("dump process started, please wait. hold b to cancel.\n");
    
    if (!usbSendFileProperties(nsp_size, path, (u32)nsp_header_size))
    {
        consolePrint("usb send file properties (header) failed\n");
        goto end;
    }
    
    nsp_offset += nsp_header_size;
    
    // set nsp size
    shared_data->total_size = nsp_size;
    
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
        
        tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, i);
        if (!usbSendFilePropertiesCommon(cur_nca_ctx->content_size, tmp_name))
        {
            consolePrint("usb send file properties \"%s\" failed\n", tmp_name);
            goto end;
        }
        
        for(u64 offset = 0; offset < cur_nca_ctx->content_size; offset += blksize, nsp_offset += blksize, shared_data->data_written += blksize)
        {
            if (shared_data->transfer_cancelled)
            {
                usbCancelFileTransfer();
                goto end;
            }
            
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
            if (!usbSendFileData(buf, blksize))
            {
                consolePrint("send file data failed\n");
                goto end;
            }
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
    tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, meta_nca_ctx->content_type_ctx_data_idx);
    if (!usbSendFilePropertiesCommon(cnmt_ctx.authoring_tool_xml_size, tmp_name) || !usbSendFileData(cnmt_ctx.authoring_tool_xml, cnmt_ctx.authoring_tool_xml_size))
    {
        consolePrint("send \"%s\" failed\n", tmp_name);
        goto end;
    }
    
    nsp_offset += cnmt_ctx.authoring_tool_xml_size;
    shared_data->data_written += cnmt_ctx.authoring_tool_xml_size;
    
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
                    tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, data_idx);
                    if (!usbSendFilePropertiesCommon(icon_ctx->icon_size, tmp_name) || !usbSendFileData(icon_ctx->icon_data, icon_ctx->icon_size))
                    {
                        consolePrint("send \"%s\" failed\n", tmp_name);
                        goto end;
                    }
                    
                    nsp_offset += icon_ctx->icon_size;
                    shared_data->data_written += icon_ctx->icon_size;
                    
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
        tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, data_idx);
        if (!usbSendFilePropertiesCommon(authoring_tool_xml_size, tmp_name) || !usbSendFileData(authoring_tool_xml, authoring_tool_xml_size))
        {
            consolePrint("send \"%s\" failed\n", tmp_name);
            goto end;
        }
        
        nsp_offset += authoring_tool_xml_size;
        shared_data->data_written += authoring_tool_xml_size;
        
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
        tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, pfs_file_ctx.header.entry_count - 2);
        if (!usbSendFilePropertiesCommon(tik.size, tmp_name) || !usbSendFileData(tik.data, tik.size))
        {
            consolePrint("send \"%s\" failed\n", tmp_name);
            goto end;
        }
        
        nsp_offset += tik.size;
        shared_data->data_written += tik.size;
        
        // write cert
        tmp_name = pfsGetEntryNameByIndexFromFileContext(&pfs_file_ctx, pfs_file_ctx.header.entry_count - 1);
        if (!usbSendFilePropertiesCommon(raw_cert_chain_size, tmp_name) || !usbSendFileData(raw_cert_chain, raw_cert_chain_size))
        {
            consolePrint("send \"%s\" failed\n", tmp_name);
            goto end;
        }
        
        nsp_offset += raw_cert_chain_size;
        shared_data->data_written += raw_cert_chain_size;
    }
    
    // write new pfs0 header
    if (!pfsWriteFileContextHeaderToMemoryBuffer(&pfs_file_ctx, buf, BLOCK_SIZE, &nsp_header_size))
    {
        consolePrint("pfs write header to mem #2 failed\n");
        goto end;
    }
    
    if (!usbSendNspHeader(buf, (u32)nsp_header_size))
    {
        consolePrint("send nsp header failed\n");
        goto end;
    }
    
    shared_data->data_written += nsp_header_size;
    
    success = true;
    
end:
    consoleRefresh();
    
    if (!success && !shared_data->transfer_cancelled) shared_data->error = true;
    
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
    
    threadExit();
}

static void nspDump(TitleInfo *title_info)
{
    if (!title_info) return;
    
    TitleApplicationMetadata *app_metadata = title_info->app_metadata;
    
    ThreadSharedData shared_data = {0};
    Thread dump_thread = {0};
    
    time_t start = 0, btn_cancel_start_tmr = 0, btn_cancel_end_tmr = 0;
    bool btn_cancel_cur_state = false, btn_cancel_prev_state = false;
    u8 usb_host_speed = UsbHostSpeed_None;
    
    u64 prev_size = 0;
    u8 prev_time = 0, percent = 0;
    
    consoleClear();
    
    consolePrint("%s info:\n\n", title_info->meta_key.type == NcmContentMetaType_Application ? "base application" : \
                           (title_info->meta_key.type == NcmContentMetaType_Patch ? "update" : "dlc"));
    
    if (app_metadata)
    {
        consolePrint("name: %s\n", app_metadata->lang_entry.name);
        consolePrint("publisher: %s\n", app_metadata->lang_entry.author);
    }
    
    consolePrint("source storage: %s\n", title_info->storage_id == NcmStorageId_GameCard ? "gamecard" : (title_info->storage_id == NcmStorageId_BuiltInUser ? "emmc" : "sd card"));
    consolePrint("title id: %016lX\n", title_info->meta_key.id);
    consolePrint("version: %u (%u.%u.%u-%u.%u)\n", title_info->version.value, title_info->version.major, title_info->version.minor, title_info->version.micro, title_info->version.major_relstep, \
                                             title_info->version.minor_relstep);
    consolePrint("content count: %u\n", title_info->content_count);
    consolePrint("size: %s\n", title_info->size_str);
    consolePrint("______________________________\n\n");
    consolePrint("dump options:\n\n");
    for(u32 i = 0; i < options_count; i++) consolePrint("%s: %s\n", options[i].str, options[i].val ? "yes" : "no");
    consolePrint("______________________________\n\n");
    
    // make sure we have a valid usb session
    consolePrint("waiting for usb connection... ");
    
    start = time(NULL);
    
    while(true)
    {
        time_t now = time(NULL);
        if ((now - start) >= 10) break;
        
        consolePrint("%lu ", now - start);
        consoleRefresh();
        
        if ((usb_host_speed = usbIsReady())) break;
        utilsSleep(1);
    }
    
    consolePrint("\n");
    
    if (!usb_host_speed)
    {
        consolePrint("usb connection failed\n");
        return;
    }
    
    // create dump thread
    shared_data.data = title_info;
    utilsCreateThread(&dump_thread, dump_thread_func, &shared_data, 2);
    
    while(!shared_data.total_size && !shared_data.error) svcSleepThread(10000000); // 10 ms
    
    if (shared_data.error)
    {
        utilsJoinThread(&dump_thread);
        return;
    }
    
    // start dump
    start = time(NULL);
    
    while(shared_data.data_written < shared_data.total_size)
    {
        if (shared_data.error) break;
        
        time_t now = time(NULL);
        struct tm *ts = localtime(&now);
        size_t size = shared_data.data_written;
        
        utilsScanPads();
        btn_cancel_cur_state = (utilsGetButtonsHeld() & HidNpadButton_B);
        
        if (btn_cancel_cur_state && btn_cancel_cur_state != btn_cancel_prev_state)
        {
            btn_cancel_start_tmr = now;
        } else
        if (btn_cancel_cur_state && btn_cancel_cur_state == btn_cancel_prev_state)
        {
            btn_cancel_end_tmr = now;
            if ((btn_cancel_end_tmr - btn_cancel_start_tmr) >= 3)
            {
                shared_data.transfer_cancelled = true;
                break;
            }
        } else {
            btn_cancel_start_tmr = btn_cancel_end_tmr = 0;
        }
        
        btn_cancel_prev_state = btn_cancel_cur_state;
        
        if (prev_time == ts->tm_sec || prev_size == size) continue;
        
        percent = (u8)((size * 100) / shared_data.total_size);
        
        prev_time = ts->tm_sec;
        prev_size = size;
        
        consolePrint("%lu / %lu (%u%%) | Time elapsed: %lu\n", size, shared_data.total_size, percent, (now - start));
        consoleRefresh();
    }
    
    start = (time(NULL) - start);
    
    consolePrint("\nwaiting for thread to join\n");
    utilsJoinThread(&dump_thread);
    consolePrint("dump_thread done: %lu\n", time(NULL));
    
    if (shared_data.error)
    {
        consolePrint("usb transfer error\n");
        return;
    }
    
    if (shared_data.transfer_cancelled)
    {
        consolePrint("process cancelled\n");
        return;
    }
    
    consolePrint("process completed in %lu seconds\n", start);
}

int main(int argc, char *argv[])
{
    int ret = 0;
    
    if (!utilsInitializeResources(argc, (const char**)argv))
    {
        ret = -1;
        goto out;
    }
    
    /* Configure input. */
    /* Up to 8 different, full controller inputs. */
    /* Individual Joy-Cons not supported. */
    padConfigureInput(8, HidNpadStyleSet_NpadFullCtrl);
    padInitializeWithMask(&g_padState, 0x1000000FFUL);
    
    consoleInit(NULL);
    
    u32 app_count = 0;
    TitleApplicationMetadata **app_metadata = NULL;
    TitleUserApplicationData user_app_data = {0};
    TitleInfo *title_info = NULL;
    
    u32 menu = 0, selected_idx = 0, scroll = 0, page_size = 30;
    
    u32 title_idx = 0, title_scroll = 0;
    u32 type_idx = 0, type_scroll = 0;
    u32 list_count = 0, list_idx = 0;
    
    app_metadata = titleGetApplicationMetadataEntries(false, &app_count);
    if (!app_metadata || !app_count)
    {
        consolePrint("app metadata failed\n");
        goto out2;
    }
    
    consolePrint("app metadata succeeded\n");
    consoleRefresh();
    
    utilsSleep(1);
    
    while(true)
    {
        consoleClear();
        
        consolePrint("press b to %s.\n", menu == 0 ? "exit" : "go back");
        consolePrint("______________________________\n\n");
        
        if (menu == 0)
        {
            consolePrint("title: %u / %u\n", selected_idx + 1, app_count);
            consolePrint("selected title: %016lX - %s\n", app_metadata[selected_idx]->title_id, app_metadata[selected_idx]->lang_entry.name);
        } else {
            consolePrint("title info:\n\n");
            consolePrint("name: %s\n", app_metadata[title_idx]->lang_entry.name);
            consolePrint("publisher: %s\n", app_metadata[title_idx]->lang_entry.author);
            consolePrint("title id: %016lX\n", app_metadata[title_idx]->title_id);
            
            if (menu == 2)
            {
                consolePrint("______________________________\n\n");
                
                if (title_info->previous || title_info->next)
                {
                    consolePrint("press zl/l and/or zr/r to change the selected title\n");
                    consolePrint("title: %u / %u\n", list_idx, list_count);
                    consolePrint("______________________________\n\n");
                }
                
                consolePrint("selected %s info:\n\n", title_info->meta_key.type == NcmContentMetaType_Application ? "base application" : \
                                                (title_info->meta_key.type == NcmContentMetaType_Patch ? "update" : "dlc"));
                consolePrint("source storage: %s\n", title_info->storage_id == NcmStorageId_GameCard ? "gamecard" : (title_info->storage_id == NcmStorageId_BuiltInUser ? "emmc" : "sd card"));
                if (title_info->meta_key.type != NcmContentMetaType_Application) consolePrint("title id: %016lX\n", title_info->meta_key.id);
                consolePrint("version: %u (%u.%u.%u-%u.%u)\n", title_info->version.value, title_info->version.major, title_info->version.minor, title_info->version.micro, title_info->version.major_relstep, \
                                                         title_info->version.minor_relstep);
                consolePrint("content count: %u\n", title_info->content_count);
                consolePrint("size: %s\n", title_info->size_str);
            }
        }
        
        consolePrint("______________________________\n\n");
        
        u32 max_val = (menu == 0 ? app_count : (menu == 1 ? dump_type_strings_count : (1 + options_count)));
        for(u32 i = scroll; i < max_val; i++)
        {
            if (i >= (scroll + page_size)) break;
            
            consolePrint("%s", i == selected_idx ? " -> " : "    ");
            
            if (menu == 0)
            {
                consolePrint("%016lX - %s\n", app_metadata[i]->title_id, app_metadata[i]->lang_entry.name);
            } else
            if (menu == 1)
            {
                consolePrint("%s\n", dump_type_strings[i]);
            } else
            if (menu == 2)
            {
                if (i == 0)
                {
                    consolePrint("start nsp dump\n");
                } else {
                    consolePrint("%s: < %s >\n", options[i - 1].str, options[i - 1].val ? "yes" : "no");
                }
            }
        }
        
        consolePrint("\n");
        
        consoleRefresh();
        
        bool gc_update = false;
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
                
                gc_update = true;
                
                break;
            }
        }
        
        if (gc_update) continue;
        
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
                utilsChangeHomeButtonBlockStatus(true);
                nspDump(title_info);
                utilsChangeHomeButtonBlockStatus(false);
            }
            
            if (error || menu >= 3)
            {
                consolePrint("press any button to continue\n");
                consoleRefresh();
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
                if (menu == 0) titleFreeUserApplicationData(&user_app_data);
            }
        } else
        if ((btn_down & (HidNpadButton_Left | HidNpadButton_Right)) && menu == 2 && selected_idx != 0)
        {
            options[selected_idx - 1].val ^= 1;
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
        }
        
        if (btn_held & (HidNpadButton_StickLDown | HidNpadButton_StickRDown | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) svcSleepThread(50000000); // 50 ms
    }
    
out2:
    consoleRefresh();
    
    if (menu != UINT32_MAX)
    {
        consolePrint("press any button to exit\n");
        utilsWaitForButtonPress(0);
    }
    
    if (app_metadata) free(app_metadata);
    
out:
    utilsCloseResources();
    
    consoleExit(NULL);
    
    return ret;
}
