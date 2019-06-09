#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "keys.h"
#include "util.h"
#include "ui.h"
#include "rsa.h"

/* Extern variables */

extern int breaks;
extern int font_height;

extern exefs_ctx_t exeFsContext;
extern romfs_ctx_t romFsContext;

extern char strbuf[NAME_BUF_LEN * 4];

extern nca_keyset_t nca_keyset;

char *getTitleType(u8 type)
{
    char *out = NULL;
    
    switch(type)
    {
        case META_DB_REGULAR_APPLICATION:
            out = "Application";
            break;
        case META_DB_PATCH:
            out = "Patch";
            break;
        case META_DB_ADDON:
            out = "AddOnContent";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getContentType(u8 type)
{
    char *out = NULL;
    
    switch(type)
    {
        case NcmContentType_CNMT:
            out = "Meta";
            break;
        case NcmContentType_Program:
            out = "Program";
            break;
        case NcmContentType_Data:
            out = "Data";
            break;
        case NcmContentType_Icon:
            out = "Control";
            break;
        case NcmContentType_Doc:
            out = "HtmlDocument";
            break;
        case NcmContentType_Info:
            out = "LegalInformation";
            break;
        case NCA_CONTENT_TYPE_DELTA:
            out = "DeltaFragment";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getRequiredMinTitleType(u8 type)
{
    char *out = NULL;
    
    switch(type)
    {
        case META_DB_REGULAR_APPLICATION:
        case META_DB_PATCH:
            out = "RequiredSystemVersion";
            break;
        case META_DB_ADDON:
            out = "RequiredApplicationVersion";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getReferenceTitleIDType(u8 type)
{
    char *out = NULL;
    
    switch(type)
    {
        case META_DB_REGULAR_APPLICATION:
            out = "PatchId";
            break;
        case META_DB_PATCH:
            out = "OriginalId";
            break;
        case META_DB_ADDON:
            out = "ApplicationId";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

void generateCnmtXml(cnmt_xml_program_info *xml_program_info, cnmt_xml_content_info *xml_content_info, char *out)
{
    if (!xml_program_info || !xml_content_info || !xml_program_info->nca_cnt || !out) return;
    
    u32 i;
    char tmp[NAME_BUF_LEN] = {'\0'};
    
    sprintf(out, "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" \
                 "<ContentMeta>\r\n" \
                 "    <Type>%s</Type>\r\n" \
                 "    <Id>0x%016lx</Id>\r\n" \
                 "    <Version>%u</Version>\r\n" \
                 "    <RequiredDownloadSystemVersion>%u</RequiredDownloadSystemVersion>\r\n", \
                 getTitleType(xml_program_info->type), \
                 xml_program_info->title_id, \
                 xml_program_info->version, \
                 xml_program_info->required_dl_sysver);
    
    for(i = 0; i < xml_program_info->nca_cnt; i++)
    {
        sprintf(tmp, "    <Content>\r\n" \
                     "        <Type>%s</Type>\r\n" \
                     "        <Id>%s</Id>\r\n" \
                     "        <Size>%lu</Size>\r\n" \
                     "        <Hash>%s</Hash>\r\n" \
                     "        <KeyGeneration>%u</KeyGeneration>\r\n" \
                     "    </Content>\r\n", \
                     getContentType(xml_content_info[i].type), \
                     xml_content_info[i].nca_id_str, \
                     xml_content_info[i].size, \
                     xml_content_info[i].hash_str, \
                     xml_content_info[i].keyblob); \
        
        strcat(out, tmp);
    }
    
    sprintf(tmp, "    <Digest>%s</Digest>\r\n" \
                 "    <KeyGenerationMin>%u</KeyGenerationMin>\r\n" \
                 "    <%s>%u</%s>\r\n" \
                 "    <%s>0x%016lx</%s>\r\n" \
                 "</ContentMeta>\r\n", \
                 xml_program_info->digest_str, \
                 xml_program_info->min_keyblob, \
                 getRequiredMinTitleType(xml_program_info->type), \
                 xml_program_info->min_sysver, \
                 getRequiredMinTitleType(xml_program_info->type), \
                 getReferenceTitleIDType(xml_program_info->type), \
                 xml_program_info->patch_tid, \
                 getReferenceTitleIDType(xml_program_info->type));
    
    strcat(out, tmp);
}

void convertNcaSizeToU64(const u8 size[0x6], u64 *out)
{
    if (!size || !out) return;
    
    u64 tmp = 0;
    
    tmp |= (((u64)size[5] << 40) & (u64)0xFF0000000000);
    tmp |= (((u64)size[4] << 32) & (u64)0x00FF00000000);
    tmp |= (((u64)size[3] << 24) & (u64)0x0000FF000000);
    tmp |= (((u64)size[2] << 16) & (u64)0x000000FF0000);
    tmp |= (((u64)size[1] << 8)  & (u64)0x00000000FF00);
    tmp |= ((u64)size[0]         & (u64)0x0000000000FF);
    
    *out = tmp;
}

void convertU64ToNcaSize(const u64 size, u8 out[0x6])
{
    if (!size || !out) return;
    
    u8 tmp[0x6];
    
    tmp[5] = (u8)(size >> 40);
    tmp[4] = (u8)(size >> 32);
    tmp[3] = (u8)(size >> 24);
    tmp[2] = (u8)(size >> 16);
    tmp[1] = (u8)(size >> 8);
    tmp[0] = (u8)size;
    
    memcpy(out, tmp, 6);
}

bool loadNcaKeyset()
{
    // Keyset already loaded
    if (nca_keyset.total_key_cnt > 0) return true;
    
    if (!(envIsSyscallHinted(0x60) &&   // svcDebugActiveProcess
          envIsSyscallHinted(0x63) &&   // svcGetDebugEvent
          envIsSyscallHinted(0x65) &&   // svcGetProcessList
          envIsSyscallHinted(0x69) &&   // svcQueryDebugProcessMemory
          envIsSyscallHinted(0x6a)))    // svcReadDebugProcessMemory
    {    
        uiDrawString("Error: please run the application with debug svc permissions!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    return getNcaKeys();
}

size_t aes128XtsNintendoCrypt(Aes128XtsContext *ctx, void *dst, const void *src, size_t size, u32 sector, bool encrypt)
{
    if (!ctx || !dst || !src || !size || (size % NCA_AES_XTS_SECTOR_SIZE) != 0) return 0;
    
    u32 i;
    size_t crypt_res = 0, out = 0;
    u32 cur_sector = sector;
    
    for(i = 0; i < size; i += NCA_AES_XTS_SECTOR_SIZE, cur_sector++)
    {
        // We have to force a sector reset on each new sector to actually enable Nintendo AES-XTS cipher tweak
        aes128XtsContextResetSector(ctx, cur_sector, true);
        
        if (encrypt)
        {
            crypt_res = aes128XtsEncrypt(ctx, (u8*)dst + i, (const u8*)src + i, NCA_AES_XTS_SECTOR_SIZE);
        } else {
            crypt_res = aes128XtsDecrypt(ctx, (u8*)dst + i, (const u8*)src + i, NCA_AES_XTS_SECTOR_SIZE);
        }
        
        if (crypt_res != NCA_AES_XTS_SECTOR_SIZE) break;
        
        out += crypt_res;
    }
    
    return out;
}

/* Updates the CTR for an offset. */
static void nca_update_ctr(unsigned char *ctr, u64 ofs)
{
    ofs >>= 4;
    unsigned int i;
    
    for(i = 0; i < 0x8; i++)
    {
        ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
        ofs >>= 8;
    }
}

bool processNcaCtrSectionBlock(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, u64 offset, void *outBuf, size_t bufSize, Aes128CtrContext *ctx, bool encrypt)
{
    if (!ncmStorage || !ncaId || !outBuf || !bufSize || !ctx)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid parameters to process %s NCA section block!", encrypt ? "decrypted" : "encrypted");
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    Result result;
    unsigned char ctr[0x10];
    u8 *tmp_buf = NULL;
    
    char nca_id[33] = {'\0'};
    convertDataToHexString(ncaId->c, 16, nca_id, 33);
    
    u64 block_start_offset = (offset - (offset % 0x10));
    u64 block_end_offset = (u64)round_up(offset + bufSize, 0x10);
    u64 block_size = (block_end_offset - block_start_offset);
    
    tmp_buf = malloc(block_size);
    if (!tmp_buf)
    {
        uiDrawString("Error: unable to allocate memory for the temporary NCA section block read buffer!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (R_FAILED(result = ncmContentStorageReadContentIdFile(ncmStorage, ncaId, block_start_offset, tmp_buf, block_size)))
    {
        free(tmp_buf);
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: ncmContentStorageReadContentIdFile failed for %lu bytes block at offset 0x%016lX from NCA \"%s\"! (0x%08X)", block_size, block_start_offset, nca_id, result);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    memcpy(ctr, ctx->ctr, 0x10);
    nca_update_ctr(ctr, block_start_offset);
    
    // Decrypt
    aes128CtrContextResetCtr(ctx, ctr);
    aes128CtrCrypt(ctx, tmp_buf, tmp_buf, block_size);
    
    if (encrypt)
    {
        memcpy(tmp_buf + (offset - block_start_offset), outBuf, bufSize);
        
        // Encrypt
        aes128CtrContextResetCtr(ctx, ctr);
        aes128CtrCrypt(ctx, tmp_buf, tmp_buf, block_size);
    }
    
    memcpy(outBuf, tmp_buf + (offset - block_start_offset), bufSize);
    
    free(tmp_buf);
    
    return true;
}

bool encryptNcaHeader(nca_header_t *input, u8 *outBuf, u64 outBufSize)
{
    if (!input || !outBuf || !outBufSize || outBufSize < NCA_FULL_HEADER_LENGTH || (bswap_32(input->magic) != NCA3_MAGIC && bswap_32(input->magic) != NCA2_MAGIC))
    {
        uiDrawString("Error: invalid NCA header encryption parameters.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    u32 i;
    size_t crypt_res;
    Aes128XtsContext hdr_aes_ctx;
    
    u8 header_key_0[16];
    u8 header_key_1[16];
    
    memcpy(header_key_0, nca_keyset.header_key, 16);
    memcpy(header_key_1, nca_keyset.header_key + 16, 16);
    
    aes128XtsContextCreate(&hdr_aes_ctx, header_key_0, header_key_1, true);
    
    if (bswap_32(input->magic) == NCA3_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf, input, NCA_FULL_HEADER_LENGTH, 0, true);
        if (crypt_res != NCA_FULL_HEADER_LENGTH)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid output length for encrypted NCA header! (%u != %lu)", NCA_FULL_HEADER_LENGTH, crypt_res);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
    } else
    if (bswap_32(input->magic) == NCA2_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf, input, NCA_HEADER_LENGTH, 0, true);
        if (crypt_res != NCA_HEADER_LENGTH)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid output length for encrypted NCA header! (%u != %lu)", NCA_HEADER_LENGTH, crypt_res);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
        
        for(i = 0; i < NCA_SECTION_HEADER_CNT; i++)
        {
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf + NCA_HEADER_LENGTH + (i * NCA_SECTION_HEADER_LENGTH), &(input->fs_headers[i]), NCA_SECTION_HEADER_LENGTH, 0, true);
            if (crypt_res != NCA_SECTION_HEADER_LENGTH)
            {
                snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid output length for encrypted NCA header section #%u! (%u != %lu)", i, NCA_SECTION_HEADER_LENGTH, crypt_res);
                uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                return false;
            }
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid decrypted NCA magic word! (0x%08X)", bswap_32(input->magic));
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    return true;
}

bool decryptNcaHeader(const u8 *ncaBuf, u64 ncaBufSize, nca_header_t *out, title_rights_ctx *rights_info, u8 *decrypted_nca_keys, bool retrieveTitleKeyData)
{
    if (!ncaBuf || !ncaBufSize || ncaBufSize < NCA_FULL_HEADER_LENGTH || !out || !decrypted_nca_keys)
    {
        uiDrawString("Error: invalid NCA header decryption parameters.", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    u32 i;
    size_t crypt_res;
    Aes128XtsContext hdr_aes_ctx;
    
    u8 header_key_0[16];
    u8 header_key_1[16];
    
    bool has_rights_id = false;
    
    memcpy(header_key_0, nca_keyset.header_key, 16);
    memcpy(header_key_1, nca_keyset.header_key + 16, 16);
    
    aes128XtsContextCreate(&hdr_aes_ctx, header_key_0, header_key_1, false);
    
    crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, out, ncaBuf, NCA_HEADER_LENGTH, 0, false);
    if (crypt_res != NCA_HEADER_LENGTH)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid output length for decrypted NCA header! (%u != %lu)", NCA_HEADER_LENGTH, crypt_res);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (bswap_32(out->magic) == NCA3_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, out, ncaBuf, NCA_FULL_HEADER_LENGTH, 0, false);
        if (crypt_res != NCA_FULL_HEADER_LENGTH)
        {
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid output length for decrypted NCA header! (%u != %lu)", NCA_FULL_HEADER_LENGTH, crypt_res);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
    } else
    if (bswap_32(out->magic) == NCA2_MAGIC)
    {
        for(i = 0; i < NCA_SECTION_HEADER_CNT; i++)
        {
            if (out->fs_headers[i]._0x148[0] != 0 || memcmp(out->fs_headers[i]._0x148, out->fs_headers[i]._0x148 + 1, 0xB7))
            {
                crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(out->fs_headers[i]), ncaBuf + NCA_HEADER_LENGTH + (i * NCA_SECTION_HEADER_LENGTH), NCA_SECTION_HEADER_LENGTH, 0, false);
                if (crypt_res != NCA_SECTION_HEADER_LENGTH)
                {
                    snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid output length for decrypted NCA header section #%u! (%u != %lu)", i, NCA_SECTION_HEADER_LENGTH, crypt_res);
                    uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
                    return false;
                }
            } else {
                memset(&(out->fs_headers[i]), 0, sizeof(nca_fs_header_t));
            }
        }
    } else {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid NCA magic word! Wrong header key? (0x%08X)", bswap_32(out->magic));
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    for(i = 0; i < 0x10; i++)
    {
        if (out->rights_id[i] != 0)
        {
            has_rights_id = true;
            break;
        }
    }
    
    if (has_rights_id)
    {
        if (rights_info != NULL)
        {
            // If we're dealing with a rights info context, retrieve the ticket for the current title
            
            if (!rights_info->has_rights_id)
            {
                rights_info->has_rights_id = true;
                
                memcpy(rights_info->rights_id, out->rights_id, 16);
                convertDataToHexString(out->rights_id, 16, rights_info->rights_id_str, 33);
                sprintf(rights_info->tik_filename, "%s.tik", rights_info->rights_id_str);
                sprintf(rights_info->cert_filename, "%s.cert", rights_info->rights_id_str);
                
                if (retrieveTitleKeyData)
                {
                    if (!retrieveNcaTikTitleKey(out, (u8*)(&(rights_info->tik_data)), rights_info->enc_titlekey, rights_info->dec_titlekey)) return false;
                    
                    memset(decrypted_nca_keys, 0, NCA_KEY_AREA_SIZE);
                    memcpy(decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), rights_info->dec_titlekey, 0x10);
                }
            } else {
                // Copy what we already have
                if (retrieveTitleKeyData)
                {
                    memset(decrypted_nca_keys, 0, NCA_KEY_AREA_SIZE);
                    memcpy(decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), rights_info->dec_titlekey, 0x10);
                }
            }
        } else {
            // Otherwise, only retrieve the decrypted titlekey. This is used with ExeFS/RomFS section parsing for SD/eMMC titles
            if (retrieveTitleKeyData)
            {
                u8 tmp_dec_titlekey[0x10];
                
                if (!retrieveNcaTikTitleKey(out, NULL, NULL, tmp_dec_titlekey)) return false;
                
                memset(decrypted_nca_keys, 0, NCA_KEY_AREA_SIZE);
                memcpy(decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), tmp_dec_titlekey, 0x10);
            }
        }
    } else {
        if (!decryptNcaKeyArea(out, decrypted_nca_keys)) return false;
    }
    
    return true;
}

bool processProgramNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, cnmt_xml_content_info *xml_content_info, nca_program_mod_data *output)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !xml_content_info || !output)
    {
        uiDrawString("Error: invalid parameters to process Program NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_nca_header->fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString("Error: Program NCA section #0 doesn't hold a PFS0 partition!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString("Error: invalid size for PFS0 partition in Program NCA section #0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid AES crypt type for Program NCA section #0! (0x%02X)", dec_nca_header->fs_headers[0].crypt_type);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u32 i;
    
    u64 section_offset;
    u64 hash_table_offset;
    u64 nca_pfs0_offset;
    
    pfs0_header nca_pfs0_header;
    pfs0_entry_table *nca_pfs0_entries = NULL;
    u64 nca_pfs0_data_offset;
    
    npdm_t npdm_header;
    bool found_meta = false;
    u64 meta_offset;
    u64 acid_pubkey_offset;
    
    u64 block_hash_table_offset;
    u64 block_hash_table_end_offset;
    u64 block_start_offset[2] = { 0, 0 };
    u64 block_size[2] = { 0, 0 };
    u8 block_hash[2][SHA256_HASH_LENGTH];
    u8 *block_data[2] = { NULL, NULL };
    
    u64 sig_write_size[2] = { 0, 0 };
    
    u8 *hash_table = NULL;
    
    Aes128CtrContext aes_ctx;
    
    section_offset = ((u64)dec_nca_header->section_entries[0].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    hash_table_offset = (section_offset + dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_offset);
    nca_pfs0_offset = (section_offset + dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !hash_table_offset || hash_table_offset < section_offset || !nca_pfs0_offset || nca_pfs0_offset <= hash_table_offset)
    {
        uiDrawString("Error: invalid offsets for Program NCA section #0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Generate initial CTR
    unsigned char ctr[0x10];
    u64 ofs = (section_offset >> 4);
    
    for(i = 0; i < 0x8; i++)
    {
        ctr[i] = dec_nca_header->fs_headers[0].section_ctr[0x08 - i - 1];
        ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
        ofs >>= 8;
    }
    
    u8 ctr_key[NCA_KEY_AREA_KEY_SIZE];
    memcpy(ctr_key, xml_content_info->decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    aes128CtrContextCreate(&aes_ctx, ctr_key, ctr);
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, nca_pfs0_offset, &nca_pfs0_header, sizeof(pfs0_header), &aes_ctx, false))
    {
        breaks++;
        uiDrawString("Failed to read Program NCA section #0 PFS0 partition header!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid magic word for Program NCA section #0 PFS0 partition! Wrong KAEK? (0x%08X)", bswap_32(nca_pfs0_header.magic));
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString("Error: Program NCA section #0 PFS0 partition is empty! Wrong KAEK?", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
    if (!nca_pfs0_entries)
    {
        uiDrawString("Error: unable to allocate memory for Program NCA section #0 PFS0 partition entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, nca_pfs0_offset + sizeof(pfs0_header), nca_pfs0_entries, (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table), &aes_ctx, false))
    {
        breaks++;
        uiDrawString("Failed to read Program NCA section #0 PFS0 partition entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(nca_pfs0_entries);
        return false;
    }
    
    nca_pfs0_data_offset = (nca_pfs0_offset + sizeof(pfs0_header) + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table)) + (u64)nca_pfs0_header.str_table_size);
    
    // Looking for META magic
    for(i = 0; i < nca_pfs0_header.file_cnt; i++)
    {
        u64 nca_pfs0_cur_file_offset = (nca_pfs0_data_offset + nca_pfs0_entries[i].file_offset);
        
        // Read and decrypt NPDM header
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, nca_pfs0_cur_file_offset, &npdm_header, sizeof(npdm_t), &aes_ctx, false))
        {
            breaks++;
            snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Failed to read Program NCA section #0 PFS0 entry #%u!", i);
            uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            free(nca_pfs0_entries);
            return false;
        }
        
        if (bswap_32(npdm_header.magic) == META_MAGIC)
        {
            found_meta = true;
            meta_offset = nca_pfs0_cur_file_offset;
            acid_pubkey_offset = (meta_offset + (u64)npdm_header.acid_offset + (u64)NPDM_SIGNATURE_SIZE);
            break;
        }
    }
    
    free(nca_pfs0_entries);
    
    if (!found_meta)
    {
        uiDrawString("Error: unable to find NPDM entry in Program NCA section #0 PFS0 partition!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Calculate block offsets
    block_hash_table_offset = (hash_table_offset + (((acid_pubkey_offset - nca_pfs0_offset) / (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size)) * (u64)SHA256_HASH_LENGTH);
    block_hash_table_end_offset = (hash_table_offset + (((acid_pubkey_offset + (u64)NPDM_SIGNATURE_SIZE - nca_pfs0_offset) / (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size) * (u64)SHA256_HASH_LENGTH));
    block_start_offset[0] = (nca_pfs0_offset + (((acid_pubkey_offset - nca_pfs0_offset) / (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size) * (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size));
    
    // Make sure our block doesn't pass PFS0 end offset
    if ((block_start_offset[0] - nca_pfs0_offset + dec_nca_header->fs_headers[0].pfs0_superblock.block_size) > dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size)
    {
        block_size[0] = (dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size - (block_start_offset[0] - nca_pfs0_offset));
    } else {
        block_size[0] = (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size;
    }
    
    block_data[0] = malloc(block_size[0]);
    if (!block_data[0])
    {
        uiDrawString("Error: unable to allocate memory for Program NCA section #0 PFS0 NPDM block 0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Read and decrypt block
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, block_start_offset[0], block_data[0], block_size[0], &aes_ctx, false))
    {
        breaks++;
        uiDrawString("Failed to read Program NCA section #0 PFS0 NPDM block 0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(block_data[0]);
        return false;
    }
    
    // Make sure that 1 block will cover all patched bytes, otherwise we'll have to recalculate another hash block
    if (block_hash_table_offset != block_hash_table_end_offset)
    {
        sig_write_size[1] = (acid_pubkey_offset - block_start_offset[0] + (u64)NPDM_SIGNATURE_SIZE - block_size[0]);
        sig_write_size[0] = ((u64)NPDM_SIGNATURE_SIZE - sig_write_size[1]);
    } else {
        sig_write_size[0] = (u64)NPDM_SIGNATURE_SIZE;
    }
    
    // Patch acid pubkey changing it to a self-generated pubkey
    memcpy(block_data[0] + (acid_pubkey_offset - block_start_offset[0]), rsa_get_public_key(), sig_write_size[0]);
    
    // Calculate new block hash
    sha256CalculateHash(block_hash[0], block_data[0], block_size[0]);
    
    if (block_hash_table_offset != block_hash_table_end_offset)
    {
        block_start_offset[1] = (nca_pfs0_offset + (((acid_pubkey_offset + (u64)NPDM_SIGNATURE_SIZE - nca_pfs0_offset) / (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size) * (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size));
        
        if ((block_start_offset[1] - nca_pfs0_offset + dec_nca_header->fs_headers[0].pfs0_superblock.block_size) > dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size)
        {
            block_size[1] = (dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size - (block_start_offset[1] - nca_pfs0_offset));
        } else {
            block_size[1] = (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size;
        }
        
        block_data[1] = malloc(block_size[1]);
        if (!block_data[1])
        {
            uiDrawString("Error: unable to allocate memory for Program NCA section #0 PFS0 NPDM block 1!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            free(block_data[0]);
            return false;
        }
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, block_start_offset[1], block_data[1], block_size[1], &aes_ctx, false))
        {
            breaks++;
            uiDrawString("Failed to read Program NCA section #0 PFS0 NPDM block 1!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            free(block_data[0]);
            free(block_data[1]);
            return false;
        }
        
        memcpy(block_data[1], rsa_get_public_key() + sig_write_size[0], sig_write_size[1]);
        
        sha256CalculateHash(block_hash[1], block_data[1], block_size[1]);
    }
    
    hash_table = malloc(dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size);
    if (!hash_table)
    {
        uiDrawString("Error: unable to allocate memory for Program NCA section #0 PFS0 hash table!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, hash_table_offset, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size, &aes_ctx, false))
    {
        breaks++;
        uiDrawString("Failed to read Program NCA section #0 PFS0 hash table!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    // Update block hashes
    memcpy(hash_table + (block_hash_table_offset - hash_table_offset), block_hash[0], SHA256_HASH_LENGTH);
    if (block_hash_table_offset != block_hash_table_end_offset) memcpy(hash_table + (block_hash_table_end_offset - hash_table_offset), block_hash[1], SHA256_HASH_LENGTH);
    
    // Calculate PFS0 superblock master hash
    sha256CalculateHash(dec_nca_header->fs_headers[0].pfs0_superblock.master_hash, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size);
    
    // Calculate section hash
    sha256CalculateHash(dec_nca_header->section_hashes[0], &(dec_nca_header->fs_headers[0]), sizeof(nca_fs_header_t));
    
    // Recreate NPDM signature
    if (!rsa_sign(&(dec_nca_header->magic), NPDM_SIGNATURE_AREA_SIZE, dec_nca_header->npdm_key_sig, NPDM_SIGNATURE_SIZE))
    {
        breaks++;
        uiDrawString("Failed to recreate Program NCA NPDM signature!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    // Reencrypt relevant data blocks
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, block_start_offset[0], block_data[0], block_size[0], &aes_ctx, true))
    {
        breaks++;
        uiDrawString("Failed to encrypt Program NCA section #0 PFS0 NPDM block 0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    if (block_hash_table_offset != block_hash_table_end_offset)
    {
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, block_start_offset[1], block_data[1], block_size[1], &aes_ctx, true))
        {
            breaks++;
            uiDrawString("Failed to encrypt Program NCA section #0 PFS0 NPDM block 1!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            free(block_data[0]);
            free(block_data[1]);
            free(hash_table);
            return false;
        }
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, hash_table_offset, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size, &aes_ctx, true))
    {
        breaks++;
        uiDrawString("Failed to encrypt Program NCA section #0 PFS0 hash table!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    // Save data to the output struct so we can write it later
    // The caller function must free these data pointers
    output->hash_table = hash_table;
    output->hash_table_offset = hash_table_offset;
    output->hash_table_size = dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size;
    
    output->block_mod_cnt = (block_hash_table_offset != block_hash_table_end_offset ? 2 : 1);
    
    output->block_data[0] = block_data[0];
    output->block_offset[0] = block_start_offset[0];
    output->block_size[0] = block_size[0];
    
    if (block_hash_table_offset != block_hash_table_end_offset)
    {
        output->block_data[1] = block_data[1];
        output->block_offset[1] = block_start_offset[1];
        output->block_size[1] = block_size[1];
    }
    
    return true;
}

bool retrieveCnmtNcaData(FsStorageId curStorageId, nspDumpType selectedNspDumpType, u8 *ncaBuf, cnmt_xml_program_info *xml_program_info, cnmt_xml_content_info *xml_content_info, nca_cnmt_mod_data *output, title_rights_ctx *rights_info, bool replaceKeyArea)
{
    if (!ncaBuf || !xml_program_info || !xml_content_info || !output || !rights_info)
    {
        uiDrawString("Error: invalid parameters to retrieve CNMT NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    nca_header_t dec_header;
    
    u32 i;
    
    u64 section_offset;
    u64 section_size;
    u8 *section_data = NULL;
    
    Aes128CtrContext aes_ctx;
    
    u64 nca_pfs0_offset;
    u64 nca_pfs0_str_table_offset;
    u64 nca_pfs0_data_offset;
    pfs0_header nca_pfs0_header;
    pfs0_entry_table *nca_pfs0_entries = NULL;
    
    bool found_cnmt = false;
    
    u64 title_cnmt_offset;
    u64 title_cnmt_size;
    
    cnmt_header title_cnmt_header;
    cnmt_extended_header title_cnmt_extended_header;
    
    u64 digest_offset;
    
    // Generate filename for our required CNMT file
    char cnmtFileName[50] = {'\0'};
    snprintf(cnmtFileName, sizeof(cnmtFileName) / sizeof(cnmtFileName[0]), "%s_%016lx.cnmt", getTitleType(xml_program_info->type), xml_program_info->title_id);
    
    // Decrypt the NCA header
    // Don't retrieve the ticket and/or titlekey if we're dealing with a Patch with titlekey crypto bundled with the inserted gamecard
    if (!decryptNcaHeader(ncaBuf, xml_content_info->size, &dec_header, rights_info, xml_content_info->decrypted_nca_keys, (curStorageId != FsStorageId_GameCard))) return false;
    
    if (dec_header.fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_header.fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString("Error: CNMT NCA section #0 doesn't hold a PFS0 partition!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!dec_header.fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString("Error: invalid size for PFS0 partition in CNMT NCA section #0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (dec_header.fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid AES crypt type for CNMT NCA section #0! (0x%02X)", dec_header.fs_headers[0].crypt_type);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    bool has_rights_id = false;
    
    for(i = 0; i < 0x10; i++)
    {
        if (dec_header.rights_id[i] != 0)
        {
            has_rights_id = true;
            break;
        }
    }
    
    if (curStorageId == FsStorageId_GameCard)
    {
        if (has_rights_id)
        {
            uiDrawString("Error: Rights ID field in NCA header not empty!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
            return false;
        }
        
        // Modify distribution type
        if (selectedNspDumpType != DUMP_PATCH_NSP) dec_header.distribution = 0;
    } else
    if (curStorageId == FsStorageId_SdCard || curStorageId == FsStorageId_NandUser)
    {
        if (has_rights_id && replaceKeyArea)
        {
            // Generate new encrypted NCA key area using titlekey
            if (!generateEncryptedNcaKeyAreaWithTitlekey(&dec_header, xml_content_info->decrypted_nca_keys)) return false;
            
            // Remove rights ID from NCA
            memset(dec_header.rights_id, 0, 0x10);
        }
    }
    
    section_offset = ((u64)dec_header.section_entries[0].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    section_size = (((u64)dec_header.section_entries[0].media_end_offset * (u64)MEDIA_UNIT_SIZE) - section_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !section_size)
    {
        uiDrawString("Error: invalid offset/size for CNMT NCA section #0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Generate initial CTR
    unsigned char ctr[0x10];
    u64 ofs = (section_offset >> 4);
    
    for(i = 0; i < 0x8; i++)
    {
        ctr[i] = dec_header.fs_headers[0].section_ctr[0x08 - i - 1];
        ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
        ofs >>= 8;
    }
    
    u8 ctr_key[NCA_KEY_AREA_KEY_SIZE];
    memcpy(ctr_key, xml_content_info->decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    aes128CtrContextCreate(&aes_ctx, ctr_key, ctr);
    
    section_data = malloc(section_size);
    if (!section_data)
    {
        uiDrawString("Error: unable to allocate memory for the decrypted CNMT NCA section #0!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    aes128CtrCrypt(&aes_ctx, section_data, ncaBuf + section_offset, section_size);
    
    nca_pfs0_offset = dec_header.fs_headers[0].pfs0_superblock.pfs0_offset;
    memcpy(&nca_pfs0_header, section_data + nca_pfs0_offset, sizeof(pfs0_header));
    
    if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid magic word for CNMT NCA section #0 PFS0 partition! Wrong KAEK? (0x%08X)", bswap_32(nca_pfs0_header.magic));
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(section_data);
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString("Error: CNMT NCA section #0 PFS0 partition is empty! Wrong KAEK?", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(section_data);
        return false;
    }
    
    nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
    if (!nca_pfs0_entries)
    {
        uiDrawString("Error: unable to allocate memory for CNMT NCA section #0 PFS0 partition entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(section_data);
        return false;
    }
    
    memcpy(nca_pfs0_entries, section_data + nca_pfs0_offset + sizeof(pfs0_header), (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table));
    
    nca_pfs0_str_table_offset = (nca_pfs0_offset + sizeof(pfs0_header) + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table)));
    nca_pfs0_data_offset = (nca_pfs0_str_table_offset + (u64)nca_pfs0_header.str_table_size);
    
    // Looking for the CNMT
    for(i = 0; i < nca_pfs0_header.file_cnt; i++)
    {
        u64 filename_offset = (nca_pfs0_str_table_offset + nca_pfs0_entries[i].filename_offset);
        if (!strncasecmp((char*)section_data + filename_offset, cnmtFileName, strlen(cnmtFileName)))
        {
            found_cnmt = true;
            title_cnmt_offset = (nca_pfs0_data_offset + nca_pfs0_entries[i].file_offset);
            title_cnmt_size = nca_pfs0_entries[i].file_size;
            break;
        }
    }
    
    free(nca_pfs0_entries);
    
    if (!found_cnmt)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: unable to find file \"%s\" in PFS0 partition from CNMT NCA section #0!", cnmtFileName);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(section_data);
        return false;
    }
    
    memcpy(&title_cnmt_header, section_data + title_cnmt_offset, sizeof(cnmt_header));
    memcpy(&title_cnmt_extended_header, section_data + title_cnmt_offset + sizeof(cnmt_header), sizeof(cnmt_extended_header));
    
    // Fill information for our CNMT XML
    digest_offset = (title_cnmt_offset + title_cnmt_size - (u64)SHA256_HASH_LENGTH);
    memcpy(xml_program_info->digest, section_data + digest_offset, SHA256_HASH_LENGTH);
    convertDataToHexString(xml_program_info->digest, 32, xml_program_info->digest_str, 65);
    xml_content_info->keyblob = (dec_header.crypto_type2 > dec_header.crypto_type ? dec_header.crypto_type2 : dec_header.crypto_type);
    xml_program_info->min_keyblob = (rights_info->has_rights_id ? rights_info->rights_id[15] : xml_content_info->keyblob);
    xml_program_info->min_sysver = title_cnmt_extended_header.min_sysver;
    xml_program_info->patch_tid = title_cnmt_extended_header.patch_tid;
    
    // Empty CNMT content records
    memset(section_data + title_cnmt_offset + sizeof(cnmt_header) + (u64)title_cnmt_header.table_offset, 0, title_cnmt_size - sizeof(cnmt_header) - (u64)title_cnmt_header.table_offset - (u64)SHA256_HASH_LENGTH);
    
    // Replace input buffer data in-place
    memcpy(ncaBuf, &dec_header, NCA_FULL_HEADER_LENGTH);
    memcpy(ncaBuf + section_offset, section_data, section_size);
    
    free(section_data);
    
    // Update offsets
    nca_pfs0_offset += section_offset;
    title_cnmt_offset += section_offset;
    
    // Save data to output struct
    output->section_offset = section_offset;
    output->section_size = section_size;
    output->hash_table_offset = (section_offset + dec_header.fs_headers[0].pfs0_superblock.hash_table_offset);
    output->pfs0_offset = nca_pfs0_offset;
    output->pfs0_size = dec_header.fs_headers[0].pfs0_superblock.pfs0_size;
    output->title_cnmt_offset = title_cnmt_offset;
    output->title_cnmt_size = title_cnmt_size;
    
    return true;
}

bool patchCnmtNca(u8 *ncaBuf, u64 ncaBufSize, cnmt_xml_program_info *xml_program_info, cnmt_xml_content_info *xml_content_info, nca_cnmt_mod_data *cnmt_mod)
{
    if (!ncaBuf || !ncaBufSize || !xml_program_info || xml_program_info->nca_cnt <= 1 || !xml_content_info || !cnmt_mod)
    {
        uiDrawString("Error: invalid parameters to patch CNMT NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u32 i;
    u32 nca_cnt;
    
    cnmt_header title_cnmt_header;
    cnmt_content_record *title_cnmt_content_records = NULL;
    u64 title_cnmt_content_records_offset;
    
    u8 pfs0_block_hash[SHA256_HASH_LENGTH];
    
    nca_header_t dec_header;
    
    Aes128CtrContext aes_ctx;
    
    // Update number of content records
    nca_cnt = (xml_program_info->nca_cnt - 1); // Discard CNMT NCA
    memcpy(&title_cnmt_header, ncaBuf + cnmt_mod->title_cnmt_offset, sizeof(cnmt_header));
    title_cnmt_header.content_records_cnt = (u16)nca_cnt;
    memcpy(ncaBuf + cnmt_mod->title_cnmt_offset, &title_cnmt_header, sizeof(cnmt_header));
    
    // Allocate memory for our content records
    title_cnmt_content_records = calloc(nca_cnt, sizeof(cnmt_content_record));
    if (!title_cnmt_content_records)
    {
        uiDrawString("Error: unable to allocate memory for CNMT NCA content records!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    title_cnmt_content_records_offset = (cnmt_mod->title_cnmt_offset + sizeof(cnmt_header) + (u64)title_cnmt_header.table_offset);
    memcpy(title_cnmt_content_records, ncaBuf + title_cnmt_content_records_offset, (u64)nca_cnt * sizeof(cnmt_content_record));
    
    // Write content records
    for(i = 0; i < nca_cnt; i++)
    {
        memcpy(title_cnmt_content_records[i].hash, xml_content_info[i].hash, 32);
        memcpy(title_cnmt_content_records[i].nca_id, xml_content_info[i].nca_id, 16);
        convertU64ToNcaSize(xml_content_info[i].size, title_cnmt_content_records[i].size);
        title_cnmt_content_records[i].type = xml_content_info[i].type;
        
        u64 cur_content_record = (title_cnmt_content_records_offset + ((u64)i * sizeof(cnmt_content_record)));
        memcpy(ncaBuf + cur_content_record, &(title_cnmt_content_records[i]), sizeof(cnmt_content_record));
    }
    
    free(title_cnmt_content_records);
    
    // Calculate block hash
    sha256CalculateHash(pfs0_block_hash, ncaBuf + cnmt_mod->pfs0_offset, cnmt_mod->pfs0_size);
    memcpy(ncaBuf + cnmt_mod->hash_table_offset, pfs0_block_hash, SHA256_HASH_LENGTH);
    
    // Copy header to struct
    memcpy(&dec_header, ncaBuf, sizeof(nca_header_t));
    
    // Calculate PFS0 superblock master hash
    sha256CalculateHash(dec_header.fs_headers[0].pfs0_superblock.master_hash, ncaBuf + cnmt_mod->hash_table_offset, dec_header.fs_headers[0].pfs0_superblock.hash_table_size);
    
    // Calculate section hash
    sha256CalculateHash(dec_header.section_hashes[0], &(dec_header.fs_headers[0]), sizeof(nca_fs_header_t));
    
    // Generate initial CTR
    unsigned char ctr[0x10];
    u64 ofs = (cnmt_mod->section_offset >> 4);
    
    for(i = 0; i < 0x8; i++)
    {
        ctr[i] = dec_header.fs_headers[0].section_ctr[0x08 - i - 1];
        ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
        ofs >>= 8;
    }
    
    u8 ctr_key[NCA_KEY_AREA_KEY_SIZE];
    memcpy(ctr_key, xml_content_info[xml_program_info->nca_cnt - 1].decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    aes128CtrContextCreate(&aes_ctx, ctr_key, ctr);
    
    // Reencrypt CNMT NCA
    if (!encryptNcaHeader(&dec_header, ncaBuf, ncaBufSize))
    {
        breaks++;
        uiDrawString("Failed to encrypt modified CNMT NCA header!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    aes128CtrCrypt(&aes_ctx, ncaBuf + cnmt_mod->section_offset, ncaBuf + cnmt_mod->section_offset, cnmt_mod->section_size);
    
    // Calculate CNMT NCA SHA-256 checksum and fill information for our CNMT XML
    sha256CalculateHash(xml_content_info[xml_program_info->nca_cnt - 1].hash, ncaBuf, ncaBufSize);
    convertDataToHexString(xml_content_info[xml_program_info->nca_cnt - 1].hash, 32, xml_content_info[xml_program_info->nca_cnt - 1].hash_str, 65);
    memcpy(xml_content_info[xml_program_info->nca_cnt - 1].nca_id, xml_content_info[xml_program_info->nca_cnt - 1].hash, 16);
    convertDataToHexString(xml_content_info[xml_program_info->nca_cnt - 1].nca_id, 16, xml_content_info[xml_program_info->nca_cnt - 1].nca_id_str, 33);
    
    return true;
}

bool readExeFsEntryFromNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys)
    {
        uiDrawString("Error: invalid parameters to read RomFS section from NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u8 exefs_index;
    bool found_exefs = false;
    
    u32 i;
    
    u64 section_offset;
    u64 section_size;
    
    unsigned char ctr[0x10];
    memset(ctr, 0, 0x10);
    
    u64 ofs;
    
    u8 ctr_key[NCA_KEY_AREA_KEY_SIZE];
    memcpy(ctr_key, decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    
    Aes128CtrContext aes_ctx;
    aes128CtrContextCreate(&aes_ctx, ctr_key, ctr);
    
    u64 nca_pfs0_offset;
    pfs0_header nca_pfs0_header;
    
    u64 nca_pfs0_entries_offset;
    pfs0_entry_table *nca_pfs0_entries = NULL;
    
    u64 nca_pfs0_str_table_offset;
    char *nca_pfs0_str_table = NULL;
    
    u64 nca_pfs0_data_offset;
    
    initExeFsContext();
    
    for(exefs_index = 0; exefs_index < 4; exefs_index++)
    {
        if (dec_nca_header->fs_headers[exefs_index].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_nca_header->fs_headers[exefs_index].fs_type != NCA_FS_HEADER_FSTYPE_PFS0 || !dec_nca_header->fs_headers[exefs_index].pfs0_superblock.pfs0_size || dec_nca_header->fs_headers[exefs_index].crypt_type != NCA_FS_HEADER_CRYPT_CTR) continue;
        
        section_offset = ((u64)dec_nca_header->section_entries[exefs_index].media_start_offset * (u64)MEDIA_UNIT_SIZE);
        section_size = (((u64)dec_nca_header->section_entries[exefs_index].media_end_offset * (u64)MEDIA_UNIT_SIZE) - section_offset);
        
        if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !section_size) continue;
        
        // Generate initial CTR
        ofs = (section_offset >> 4);
        
        for(i = 0; i < 0x8; i++)
        {
            ctr[i] = dec_nca_header->fs_headers[exefs_index].section_ctr[0x08 - i - 1];
            ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
            ofs >>= 8;
        }
        
        aes128CtrContextResetCtr(&aes_ctx, ctr);
        
        nca_pfs0_offset = (section_offset + dec_nca_header->fs_headers[exefs_index].pfs0_superblock.pfs0_offset);
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, nca_pfs0_offset, &nca_pfs0_header, sizeof(pfs0_header), &aes_ctx, false)) return false;
        
        if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC || !nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size) continue;
        
        nca_pfs0_entries_offset = (nca_pfs0_offset + sizeof(pfs0_header));
        
        nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
        if (!nca_pfs0_entries) continue;
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, nca_pfs0_entries_offset, nca_pfs0_entries, (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table), &aes_ctx, false))
        {
            free(nca_pfs0_entries);
            return false;
        }
        
        nca_pfs0_str_table_offset = (nca_pfs0_entries_offset + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table)));
        
        nca_pfs0_str_table = calloc(nca_pfs0_header.str_table_size, sizeof(char));
        if (!nca_pfs0_str_table)
        {
            free(nca_pfs0_entries);
            nca_pfs0_entries = NULL;
            continue;
        }
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, nca_pfs0_str_table_offset, nca_pfs0_str_table, (u64)nca_pfs0_header.str_table_size, &aes_ctx, false))
        {
            free(nca_pfs0_str_table);
            free(nca_pfs0_entries);
            return false;
        }
        
        for(i = 0; i < nca_pfs0_header.file_cnt; i++)
        {
            char *cur_filename = (nca_pfs0_str_table + nca_pfs0_entries[i].filename_offset);
            
            if (!strncasecmp(cur_filename, "main.npdm", 9))
            {
                found_exefs = true;
                break;
            }
        }
        
        if (found_exefs) break;
        
        free(nca_pfs0_str_table);
        nca_pfs0_str_table = NULL;
        
        free(nca_pfs0_entries);
        nca_pfs0_entries = NULL;
    }
    
    if (!found_exefs)
    {
        uiDrawString("Error: NCA doesn't hold an ExeFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    nca_pfs0_data_offset = (nca_pfs0_str_table_offset + (u64)nca_pfs0_header.str_table_size);
    
    // Save data to output struct
    // The caller function must free these data pointers
    memcpy(&(exeFsContext.ncmStorage), ncmStorage, sizeof(NcmContentStorage));
    memcpy(&(exeFsContext.ncaId), ncaId, sizeof(NcmNcaId));
    memcpy(&(exeFsContext.aes_ctx), &aes_ctx, sizeof(Aes128CtrContext));
    exeFsContext.exefs_offset = nca_pfs0_offset;
    exeFsContext.exefs_size = dec_nca_header->fs_headers[exefs_index].pfs0_superblock.pfs0_size;
    memcpy(&(exeFsContext.exefs_header), &nca_pfs0_header, sizeof(pfs0_header));
    exeFsContext.exefs_entries_offset = nca_pfs0_entries_offset;
    exeFsContext.exefs_entries = nca_pfs0_entries;
    exeFsContext.exefs_str_table_offset = nca_pfs0_str_table_offset;
    exeFsContext.exefs_str_table = nca_pfs0_str_table;
    exeFsContext.exefs_data_offset = nca_pfs0_data_offset;
    
    return true;
}

bool readRomFsEntryFromNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys)
    {
        uiDrawString("Error: invalid parameters to read RomFS section from NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u8 romfs_index;
    bool found_romfs = false;
    
    u32 i;
    
    u64 section_offset;
    u64 section_size;
    
    Aes128CtrContext aes_ctx;
    
    u64 romfs_offset;
    u64 romfs_size;
    romfs_header romFsHeader;
    
    u64 romfs_dirtable_offset;
    u64 romfs_dirtable_size;
    
    u64 romfs_filetable_offset;
    u64 romfs_filetable_size;
    
    u64 romfs_filedata_offset;
    
    romfs_dir *romfs_dir_entries = NULL;
    romfs_file *romfs_file_entries = NULL;
    
    initRomFsContext();
    
    for(romfs_index = 0; romfs_index < 4; romfs_index++)
    {
        if (dec_nca_header->fs_headers[romfs_index].partition_type == NCA_FS_HEADER_PARTITION_ROMFS && dec_nca_header->fs_headers[romfs_index].fs_type == NCA_FS_HEADER_FSTYPE_ROMFS)
        {
            found_romfs = true;
            break;
        }
    }
    
    if (!found_romfs)
    {
        uiDrawString("Error: NCA doesn't hold a RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    section_offset = ((u64)dec_nca_header->section_entries[romfs_index].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    section_size = (((u64)dec_nca_header->section_entries[romfs_index].media_end_offset * (u64)MEDIA_UNIT_SIZE) - section_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !section_size)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid offset/size for NCA RomFS section! (#%u)", romfs_index);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // Generate initial CTR
    unsigned char ctr[0x10];
    u64 ofs = (section_offset >> 4);
    
    for(i = 0; i < 0x8; i++)
    {
        ctr[i] = dec_nca_header->fs_headers[romfs_index].section_ctr[0x08 - i - 1];
        ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
        ofs >>= 8;
    }
    
    u8 ctr_key[NCA_KEY_AREA_KEY_SIZE];
    memcpy(ctr_key, decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    aes128CtrContextCreate(&aes_ctx, ctr_key, ctr);
    
    if (bswap_32(dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.magic) != IVFC_MAGIC)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid magic word for NCA RomFS section! Wrong KAEK? (0x%08X)", bswap_32(dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.magic));
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (dec_nca_header->fs_headers[romfs_index].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid AES crypt type for NCA RomFS section! (0x%02X)", dec_nca_header->fs_headers[0].crypt_type);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    romfs_offset = (section_offset + dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].logical_offset);
    romfs_size = dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].hash_data_size;
    
    if (romfs_offset < section_offset || romfs_offset >= (section_offset + section_size) || romfs_size < ROMFS_HEADER_SIZE || (romfs_offset + romfs_size) > (section_offset + section_size))
    {
        uiDrawString("Error: invalid offset/size for NCA RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    // First read the RomFS header
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, romfs_offset, &romFsHeader, sizeof(romfs_header), &aes_ctx, false))
    {
        breaks++;
        uiDrawString("Failed to read NCA RomFS section header!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (romFsHeader.headerSize != ROMFS_HEADER_SIZE)
    {
        snprintf(strbuf, sizeof(strbuf) / sizeof(strbuf[0]), "Error: invalid header size for NCA RomFS section! (0x%016lX at 0x%016lX)", romFsHeader.headerSize, romfs_offset);
        uiDrawString(strbuf, 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    romfs_dirtable_offset = (romfs_offset + romFsHeader.dirTableOff);
    romfs_dirtable_size = romFsHeader.dirTableSize;
    
    romfs_filetable_offset = (romfs_offset + romFsHeader.fileTableOff);
    romfs_filetable_size = romFsHeader.fileTableSize;
    
    if (romfs_dirtable_offset >= (section_offset + section_size) || !romfs_dirtable_size || (romfs_dirtable_offset + romfs_dirtable_size) >= (section_offset + section_size) || romfs_filetable_offset >= (section_offset + section_size) || !romfs_filetable_size || (romfs_filetable_offset + romfs_filetable_size) >= (section_offset + section_size))
    {
        uiDrawString("Error: invalid directory/file table for NCA RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    romfs_filedata_offset = (romfs_offset + romFsHeader.fileDataOff);
    
    if (romfs_filedata_offset >= (section_offset + section_size))
    {
        uiDrawString("Error: invalid file data block offset for NCA RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    romfs_dir_entries = calloc(1, romfs_dirtable_size);
    if (!romfs_dir_entries)
    {
        uiDrawString("Error: unable to allocate memory for NCA RomFS section directory entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, romfs_dirtable_offset, romfs_dir_entries, romfs_dirtable_size, &aes_ctx, false))
    {
        breaks++;
        uiDrawString("Failed to read NCA RomFS section directory entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(romfs_dir_entries);
        return false;
    }
    
    romfs_file_entries = calloc(1, romfs_filetable_size);
    if (!romfs_file_entries)
    {
        uiDrawString("Error: unable to allocate memory for NCA RomFS section file entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(romfs_dir_entries);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, romfs_filetable_offset, romfs_file_entries, romfs_filetable_size, &aes_ctx, false))
    {
        breaks++;
        uiDrawString("Failed to read NCA RomFS section file entries!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        free(romfs_file_entries);
        free(romfs_dir_entries);
        return false;
    }
    
    // Save data to output struct
    // The caller function must free these data pointers
    memcpy(&(romFsContext.ncmStorage), ncmStorage, sizeof(NcmContentStorage));
    memcpy(&(romFsContext.ncaId), ncaId, sizeof(NcmNcaId));
    memcpy(&(romFsContext.aes_ctx), &aes_ctx, sizeof(Aes128CtrContext));
    romFsContext.romfs_offset = romfs_offset;
    romFsContext.romfs_size = romfs_size;
    romFsContext.romfs_dirtable_offset = romfs_dirtable_offset;
    romFsContext.romfs_dirtable_size = romfs_dirtable_size;
    romFsContext.romfs_dir_entries = romfs_dir_entries;
    romFsContext.romfs_filetable_offset = romfs_filetable_offset;
    romFsContext.romfs_filetable_size = romfs_filetable_size;
    romFsContext.romfs_file_entries = romfs_file_entries;
    romFsContext.romfs_filedata_offset = romfs_filedata_offset;
    
    return true;
}

char *getNacpLangName(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "AmericanEnglish";
            break;
        case 1:
            out = "BritishEnglish";
            break;
        case 2:
            out = "Japanese";
            break;
        case 3:
            out = "French";
            break;
        case 4:
            out = "German";
            break;
        case 5:
            out = "LatinAmericanSpanish";
            break;
        case 6:
            out = "Spanish";
            break;
        case 7:
            out = "Italian";
            break;
        case 8:
            out = "Dutch";
            break;
        case 9:
            out = "CanadianFrench";
            break;
        case 10:
            out = "Portuguese";
            break;
        case 11:
            out = "Russian";
            break;
        case 12:
            out = "Korean";
            break;
        case 13:
            out = "TraditionalChinese";
            break;
        case 14:
            out = "SimplifiedChinese";
            break;
        case 15: // Unknown
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpStartupUserAccount(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "Required";
            break;
        case 2:
            out = "RequiredWithNetworkServiceAccountAvailable";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpUserAccountSwitchLock(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "Disable";
            break;
        case 1:
            out = "Enable";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpParentalControlFlag(u32 flag)
{
    char *out = NULL;
    
    switch(flag)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "FreeCommunication";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpScreenshot(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "Allow";
            break;
        case 1:
            out = "Deny";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpVideoCapture(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "Disable";
            break;
        case 1:
            out = "Manual";
            break;
        case 2:
            out = "Enable";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpRatingAgeOrganizationName(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "CERO";
            break;
        case 1:
            out = "GRACGCRB";
            break;
        case 2:
            out = "GSRMR";
            break;
        case 3:
            out = "ESRB";
            break;
        case 4:
            out = "ClassInd";
            break;
        case 5:
            out = "USK";
            break;
        case 6:
            out = "PEGI";
            break;
        case 7:
            out = "PEGIPortugal";
            break;
        case 8:
            out = "PEGIBBFC";
            break;
        case 9:
            out = "Russian";
            break;
        case 10:
            out = "ACB";
            break;
        case 11:
            out = "OFLC";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpDataLossConfirmation(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "Required";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpPlayLogPolicy(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "All";
            break;
        case 1:
            out = "LogOnly";
            break;
        case 2:
            out = "None";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpLogoType(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "LicensedByNintendo";
            break;
        case 2:
            out = "Nintendo";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpLogoHandling(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "Auto";
            break;
        case 1:
            out = "Manual";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpStartupUserAccountOptionFlag(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "IsOptional";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpAddOnContentRegistrationType(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "AllOnLaunch";
            break;
        case 1:
            out = "OnDemand";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpHdcp(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "Required";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpCrashReport(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "Deny";
            break;
        case 1:
            out = "Allow";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpRuntimeAddOnContentInstall(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "Deny";
            break;
        case 1:
            out = "AllowAppend";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpPlayLogQueryCapability(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "WhiteList";
            break;
        case 2:
            out = "All";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpRepairFlag(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "SuppressGameCardAccess";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpAttributeFlag(u32 flag)
{
    char *out = NULL;
    
    switch(flag)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "Demo";
            break;
        case 2:
            out = "RetailInteractiveDisplay";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

char *getNacpRequiredNetworkServiceLicenseOnLaunchFlag(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "Common";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

bool generateNacpXmlFromNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys, char **outBuf)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys || !outBuf)
    {
        uiDrawString("Error: invalid parameters to generate NACP XML!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        return false;
    }
    
    u64 entryOffset = 0;
    romfs_file *entry = NULL;
    bool found_nacp = false, success = false;
    
    nacp_t controlNacp;
    char *nacpXml = NULL;
    
    u8 i;
    char tmp[NAME_BUF_LEN] = {'\0'};
    
    if (!readRomFsEntryFromNca(ncmStorage, ncaId, dec_nca_header, decrypted_nca_keys)) return false;
    
    // Look for the control.nacp file
    while(entryOffset < romFsContext.romfs_filetable_size)
    {
        entry = (romfs_file*)((u8*)romFsContext.romfs_file_entries + entryOffset);
        
        if (entry->parent == 0 && entry->nameLen == 12 && !strncasecmp((char*)entry->name, "control.nacp", 12))
        {
            found_nacp = true;
            break;
        }
        
        entryOffset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
    }
    
    if (!found_nacp)
    {
        uiDrawString("Error: unable to find control.nacp file in Control NCA RomFS section!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, romFsContext.romfs_filedata_offset + entry->dataOff, &controlNacp, sizeof(nacp_t), &(romFsContext.aes_ctx), false))
    {
        breaks++;
        uiDrawString("Failed to read Control.nacp from RomFS section in Control NCA!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    // Make sure that the output buffer for our NACP XML is big enough
    nacpXml = calloc(NAME_BUF_LEN * 4, sizeof(char));
    if (!nacpXml)
    {
        uiDrawString("Error: unable to allocate memory for the NACP XML!", 8, (breaks * (font_height + (font_height / 4))) + (font_height / 8), 255, 0, 0);
        goto out;
    }
    
    sprintf(nacpXml, "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n" \
                     "<Application>\r\n");
    
    for(i = 0; i < 16; i++)
    {
        if (strlen(controlNacp.lang[i].name) || strlen(controlNacp.lang[i].author))
        {
            sprintf(tmp, "    <Title>\r\n" \
                         "        <Language>%s</Language>\r\n" \
                         "        <Name>%s</Name>\r\n" \
                         "        <Publisher>%s</Publisher>\r\n" \
                         "    </Title>\r\n", \
                         getNacpLangName(i), \
                         controlNacp.lang[i].name, \
                         controlNacp.lang[i].author);
            
            strcat(nacpXml, tmp);
        }
    }
    
    if (strlen(controlNacp.Isbn))
    {
        sprintf(tmp, "    <Isbn>%s</Isbn>\r\n", controlNacp.Isbn);
        strcat(nacpXml, tmp);
    } else {
        strcat(nacpXml, "    <Isbn/>\r\n");
    }
    
    sprintf(tmp, "    <StartupUserAccount>%s</StartupUserAccount>\r\n", getNacpStartupUserAccount(controlNacp.StartupUserAccount));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <UserAccountSwitchLock>%s</UserAccountSwitchLock>\r\n", getNacpUserAccountSwitchLock(controlNacp.UserAccountSwitchLock));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <ParentalControl>%s</ParentalControl>\r\n", getNacpParentalControlFlag(controlNacp.ParentalControlFlag));
    strcat(nacpXml, tmp);
    
    for(i = 0; i < 16; i++)
    {
        u8 bit = (u8)((controlNacp.SupportedLanguageFlag >> i) & 0x1);
        if (bit)
        {
            sprintf(tmp, "    <SupportedLanguage>%s</SupportedLanguage>\r\n", getNacpLangName(i));
            strcat(nacpXml, tmp);
        }
    }
    
    sprintf(tmp, "    <Screenshot>%s</Screenshot>\r\n", getNacpScreenshot(controlNacp.Screenshot));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <VideoCapture>%s</VideoCapture>\r\n", getNacpVideoCapture(controlNacp.VideoCapture));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <PresenceGroupId>0x%016lx</PresenceGroupId>\r\n", controlNacp.PresenceGroupId);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <DisplayVersion>%s</DisplayVersion>\r\n", controlNacp.DisplayVersion);
    strcat(nacpXml, tmp);
    
    for(i = 0; i < 32; i++)
    {
        if (controlNacp.RatingAge[i] != 0xFF)
        {
            sprintf(tmp, "    <Rating>\r\n" \
                         "        <Organization>%s</Organization>\r\n" \
                         "        <Age>%u</Age>\r\n" \
                         "    </Rating>\r\n", \
                         getNacpRatingAgeOrganizationName(i), \
                         controlNacp.RatingAge[i]);
            
            strcat(nacpXml, tmp);
        }
    }
    
    sprintf(tmp, "    <DataLossConfirmation>%s</DataLossConfirmation>\r\n", getNacpDataLossConfirmation(controlNacp.DataLossConfirmation));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <PlayLogPolicy>%s</PlayLogPolicy>\r\n", getNacpPlayLogPolicy(controlNacp.PlayLogPolicy));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <SaveDataOwnerId>0x%016lx</SaveDataOwnerId>\r\n", controlNacp.SaveDataOwnerId);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <UserAccountSaveDataSize>0x%016lx</UserAccountSaveDataSize>\r\n", controlNacp.UserAccountSaveDataSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <UserAccountSaveDataJournalSize>0x%016lx</UserAccountSaveDataJournalSize>\r\n", controlNacp.UserAccountSaveDataJournalSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <DeviceSaveDataSize>0x%016lx</DeviceSaveDataSize>\r\n", controlNacp.DeviceSaveDataSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <DeviceSaveDataJournalSize>0x%016lx</DeviceSaveDataJournalSize>\r\n", controlNacp.DeviceSaveDataJournalSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <BcatDeliveryCacheStorageSize>0x%016lx</BcatDeliveryCacheStorageSize>\r\n", controlNacp.BcatDeliveryCacheStorageSize);
    strcat(nacpXml, tmp);
    
    if (strlen(controlNacp.ApplicationErrorCodeCategory))
    {
        sprintf(tmp, "    <ApplicationErrorCodeCategory>%s</ApplicationErrorCodeCategory>\r\n", controlNacp.ApplicationErrorCodeCategory);
        strcat(nacpXml, tmp);
    } else {
        strcat(nacpXml, "    <ApplicationErrorCodeCategory/>\r\n");
    }
    
    sprintf(tmp, "    <AddOnContentBaseId>0x%016lx</AddOnContentBaseId>\r\n", controlNacp.AddOnContentBaseId);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <LogoType>%s</LogoType>\r\n", getNacpLogoType(controlNacp.LogoType));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <LocalCommunicationId>0x%016lx</LocalCommunicationId>\r\n", controlNacp.LocalCommunicationId[0]);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <LogoHandling>%s</LogoHandling>\r\n", getNacpLogoHandling(controlNacp.LogoHandling));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <SeedForPseudoDeviceId>0x%016lx</SeedForPseudoDeviceId>\r\n", controlNacp.SeedForPseudoDeviceId);
    strcat(nacpXml, tmp);
    
    if (strlen(controlNacp.BcatPassphrase))
    {
        sprintf(tmp, "    <BcatPassphrase>%s</BcatPassphrase>\r\n", controlNacp.BcatPassphrase);
        strcat(nacpXml, tmp);
    } else {
        strcat(nacpXml, "    <BcatPassphrase/>\r\n");
    }
    
    sprintf(tmp, "    <StartupUserAccountOption>%s</StartupUserAccountOption>\r\n", getNacpStartupUserAccountOptionFlag(controlNacp.StartupUserAccountOptionFlag));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <AddOnContentRegistrationType>%s</AddOnContentRegistrationType>\r\n", getNacpAddOnContentRegistrationType(controlNacp.AddOnContentRegistrationType));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <UserAccountSaveDataSizeMax>0x%016lx</UserAccountSaveDataSizeMax>\r\n", controlNacp.UserAccountSaveDataSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <UserAccountSaveDataJournalSizeMax>0x%016lx</UserAccountSaveDataJournalSizeMax>\r\n", controlNacp.UserAccountSaveDataJournalSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <DeviceSaveDataSizeMax>0x%016lx</DeviceSaveDataSizeMax>\r\n", controlNacp.DeviceSaveDataSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <DeviceSaveDataJournalSizeMax>0x%016lx</DeviceSaveDataJournalSizeMax>\r\n", controlNacp.DeviceSaveDataJournalSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <TemporaryStorageSize>0x%016lx</TemporaryStorageSize>\r\n", controlNacp.TemporaryStorageSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <CacheStorageSize>0x%016lx</CacheStorageSize>\r\n", controlNacp.CacheStorageSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <CacheStorageJournalSize>0x%016lx</CacheStorageJournalSize>\r\n", controlNacp.CacheStorageJournalSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <CacheStorageDataAndJournalSizeMax>0x%016lx</CacheStorageDataAndJournalSizeMax>\r\n", controlNacp.CacheStorageDataAndJournalSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <CacheStorageIndexMax>0x%04x</CacheStorageIndexMax>\r\n", controlNacp.CacheStorageIndexMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <Hdcp>%s</Hdcp>\r\n", getNacpHdcp(controlNacp.Hdcp));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <CrashReport>%s</CrashReport>\r\n", getNacpCrashReport(controlNacp.CrashReport));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <RuntimeAddOnContentInstall>%s</RuntimeAddOnContentInstall>\r\n", getNacpRuntimeAddOnContentInstall(controlNacp.RuntimeAddOnContentInstall));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <PlayLogQueryableApplicationId>0x%016lx</PlayLogQueryableApplicationId>\r\n", controlNacp.PlayLogQueryableApplicationId[0]);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <PlayLogQueryCapability>%s</PlayLogQueryCapability>\r\n", getNacpPlayLogQueryCapability(controlNacp.PlayLogQueryCapability));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <Repair>%s</Repair>\r\n", getNacpRepairFlag(controlNacp.RepairFlag));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <Attribute>%s</Attribute>\r\n", getNacpAttributeFlag(controlNacp.AttributeFlag));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <ProgramIndex>%u</ProgramIndex>\r\n", controlNacp.ProgramIndex);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "    <RequiredNetworkServiceLicenseOnLaunch>%s</RequiredNetworkServiceLicenseOnLaunch>\r\n", getNacpRequiredNetworkServiceLicenseOnLaunchFlag(controlNacp.RequiredNetworkServiceLicenseOnLaunchFlag));
    strcat(nacpXml, tmp);
    
    strcat(nacpXml, "</Application>\r\n");
    
    *outBuf = nacpXml;
    
    success = true;
    
out:
    // Manually free these pointers
    // Calling freeRomFsContext() would also close the ncmStorage handle
    free(romFsContext.romfs_dir_entries);
    romFsContext.romfs_dir_entries = NULL;
    
    free(romFsContext.romfs_file_entries);
    romFsContext.romfs_file_entries = NULL;
    
    return success;
}
