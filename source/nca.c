#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mbedtls/base64.h>

#include "keys.h"
#include "util.h"
#include "ui.h"
#include "rsa.h"
#include "nso.h"

/* Extern variables */

extern int breaks;
extern int font_height;

extern exefs_ctx_t exeFsContext;
extern romfs_ctx_t romFsContext;
extern bktr_ctx_t bktrContext;

extern nca_keyset_t nca_keyset;

extern u8 *ncaCtrBuf;

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
    
    sprintf(out, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
                 "<ContentMeta>\n" \
                 "  <Type>%s</Type>\n" \
                 "  <Id>0x%016lx</Id>\n" \
                 "  <Version>%u</Version>\n" \
                 "  <RequiredDownloadSystemVersion>%u</RequiredDownloadSystemVersion>\n", \
                 getTitleType(xml_program_info->type), \
                 xml_program_info->title_id, \
                 xml_program_info->version, \
                 xml_program_info->required_dl_sysver);
    
    for(i = 0; i < xml_program_info->nca_cnt; i++)
    {
        sprintf(tmp, "  <Content>\n" \
                     "    <Type>%s</Type>\n" \
                     "    <Id>%s</Id>\n" \
                     "    <Size>%lu</Size>\n" \
                     "    <Hash>%s</Hash>\n" \
                     "    <KeyGeneration>%u</KeyGeneration>\n" \
                     "    <IdOffset>%u</IdOffset>\n" \
                     "  </Content>\n",
                     getContentType(xml_content_info[i].type), \
                     xml_content_info[i].nca_id_str, \
                     xml_content_info[i].size, \
                     xml_content_info[i].hash_str, \
                     xml_content_info[i].keyblob, \
                     (xml_content_info[i].type != NcmContentType_CNMT ? xml_content_info[i].id_offset : 0));
        
        strcat(out, tmp);
    }
    
    sprintf(tmp, "  <Digest>%s</Digest>\n" \
                 "  <KeyGenerationMin>%u</KeyGenerationMin>\n" \
                 "  <%s>%u</%s>\n" \
                 "  <%s>0x%016lx</%s>\n" \
                 "</ContentMeta>", \
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
    // Check if the keyset has been already loaded
    if (nca_keyset.total_key_cnt > 0) return true;
    
    if (!(envIsSyscallHinted(0x60) &&   // svcDebugActiveProcess
          envIsSyscallHinted(0x63) &&   // svcGetDebugEvent
          envIsSyscallHinted(0x65) &&   // svcGetProcessList
          envIsSyscallHinted(0x69) &&   // svcQueryDebugProcessMemory
          envIsSyscallHinted(0x6a)))    // svcReadDebugProcessMemory
    {    
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: please run the application with debug svc permissions!");
        return false;
    }
    
    return loadMemoryKeys();
}

size_t aes128XtsNintendoCrypt(Aes128XtsContext *ctx, void *dst, const void *src, size_t size, u32 sector, bool encrypt)
{
    if (!ctx || !dst || !src || !size || (size % NCA_AES_XTS_SECTOR_SIZE) != 0) return 0;
    
    size_t i, crypt_res = 0, out = 0;
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

/* Updates the CTR for a bktr offset. */
static void nca_update_bktr_ctr(unsigned char *ctr, u32 ctr_val, u64 ofs)
{
    ofs >>= 4;
    unsigned int i;
    
    for(i = 0; i < 0x8; i++)
    {
        ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
        ofs >>= 8;
    }
    
    for(i = 0; i < 0x4; i++)
    {
        ctr[0x8 - i - 1] = (unsigned char)(ctr_val & 0xFF);
        ctr_val >>= 8;
    }
}

bool processNcaCtrSectionBlock(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, Aes128CtrContext *ctx, u64 offset, void *outBuf, size_t bufSize, bool encrypt)
{
    if (!ncmStorage || !ncaId || !outBuf || !bufSize || !ctx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to process %s NCA section block!", encrypt ? "decrypted" : "encrypted");
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    Result result;
    unsigned char ctr[0x10];
    
    char nca_id[SHA256_HASH_SIZE + 1] = {'\0'};
    convertDataToHexString(ncaId->c, SHA256_HASH_SIZE / 2, nca_id, SHA256_HASH_SIZE + 1);
    
    u64 block_start_offset = (offset - (offset % 0x10));
    u64 block_end_offset = (u64)round_up(offset + bufSize, 0x10);
    u64 block_size = (block_end_offset - block_start_offset);
    
    u64 block_size_used = (block_size > NCA_CTR_BUFFER_SIZE ? NCA_CTR_BUFFER_SIZE : block_size);
    u64 output_block_size = (block_size > NCA_CTR_BUFFER_SIZE ? (NCA_CTR_BUFFER_SIZE - (offset - block_start_offset)) : bufSize);
    
    result = ncmContentStorageReadContentIdFile(ncmStorage, ncaId, block_start_offset, ncaCtrBuf, block_size_used);
    if (R_FAILED(result))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read encrypted %lu bytes block at offset 0x%016lX from NCA \"%s\"! (0x%08X)", block_size_used, block_start_offset, nca_id, result);
        return false;
    }
    
    // Update CTR
    memcpy(ctr, ctx->ctr, 0x10);
    nca_update_ctr(ctr, block_start_offset);
    aes128CtrContextResetCtr(ctx, ctr);
    
    // Decrypt CTR block
    aes128CtrCrypt(ctx, ncaCtrBuf, ncaCtrBuf, block_size_used);
    
    if (encrypt)
    {
        // Copy data to be encrypted
        memcpy(ncaCtrBuf + (offset - block_start_offset), outBuf, output_block_size);
        
        // Reset CTR
        aes128CtrContextResetCtr(ctx, ctr);
        
        // Encrypt CTR block
        aes128CtrCrypt(ctx, ncaCtrBuf, ncaCtrBuf, block_size_used);
    }
    
    memcpy(outBuf, ncaCtrBuf + (offset - block_start_offset), output_block_size);
    
    if (block_size > NCA_CTR_BUFFER_SIZE) return processNcaCtrSectionBlock(ncmStorage, ncaId, ctx, offset + output_block_size, outBuf + output_block_size, bufSize - output_block_size, encrypt);
    
    return true;
}

bktr_relocation_bucket_t *bktr_get_relocation_bucket(bktr_relocation_block_t *block, u32 i)
{
    return (bktr_relocation_bucket_t*)((u8*)block->buckets + ((sizeof(bktr_relocation_bucket_t) + sizeof(bktr_relocation_entry_t)) * (u64)i));
}

// Get a relocation entry from offset and relocation block
bktr_relocation_entry_t *bktr_get_relocation(bktr_relocation_block_t *block, u64 offset)
{
    // Weak check for invalid offset
    if (offset > block->total_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: too big offset looked up in BKTR relocation table!");
        return NULL;
    }
    
    u32 i, bucket_num = 0;
    
    for(i = 1; i < block->num_buckets; i++)
    {
        if (block->bucket_virtual_offsets[i] <= offset) bucket_num++;
    }
    
    bktr_relocation_bucket_t *bucket = bktr_get_relocation_bucket(block, bucket_num);
    
    // Check for edge case, short circuit
    if (bucket->num_entries == 1) return &(bucket->entries[0]);
    
    // Binary search
    u32 low = 0, high = (bucket->num_entries - 1);
    
    while(low <= high)
    {
        u32 mid = ((low + high) / 2);
        
        if (bucket->entries[mid].virt_offset > offset)
        {
            // Too high
            high = (mid - 1);
        } else {
            // block->entries[mid].offset <= offset
            
            // Check for success
            if (mid == (bucket->num_entries - 1) || bucket->entries[mid + 1].virt_offset > offset) return &(bucket->entries[mid]);
            
            low = (mid + 1);
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to find offset 0x%016lX in BKTR relocation table!", offset);
    return NULL;
}

bktr_subsection_bucket_t *bktr_get_subsection_bucket(bktr_subsection_block_t *block, u32 i)
{
    return (bktr_subsection_bucket_t*)((u8*)block->buckets + ((sizeof(bktr_subsection_bucket_t) + sizeof(bktr_subsection_entry_t)) * (u64)i));
}

// Get a subsection entry from offset and subsection block
bktr_subsection_entry_t *bktr_get_subsection(bktr_subsection_block_t *block, u64 offset)
{
    // If offset is past the virtual, we're reading from the BKTR_HEADER subsection
    bktr_subsection_bucket_t *last_bucket = bktr_get_subsection_bucket(block, block->num_buckets - 1);
    if (offset >= last_bucket->entries[last_bucket->num_entries].offset) return &(last_bucket->entries[last_bucket->num_entries]);
    
    u32 i, bucket_num = 0;
    
    for(i = 1; i < block->num_buckets; i++)
    {
        if (block->bucket_physical_offsets[i] <= offset) bucket_num++;
    }
    
    bktr_subsection_bucket_t *bucket = bktr_get_subsection_bucket(block, bucket_num);
    
    // Check for edge case, short circuit
    if (bucket->num_entries == 1) return &(bucket->entries[0]);
    
    // Binary search
    u32 low = 0, high = (bucket->num_entries - 1);
    
    while (low <= high)
    {
        u32 mid = ((low + high) / 2);
        
        if (bucket->entries[mid].offset > offset)
        {
            // Too high
            high = (mid - 1);
        } else {
            // block->entries[mid].offset <= offset
            
            // Check for success
            if (mid == (bucket->num_entries - 1) || bucket->entries[mid + 1].offset > offset) return &(bucket->entries[mid]);
            
            low = (mid + 1);
        }
    }
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: failed to find offset 0x%016lX in BKTR subsection table!", offset);
    return NULL;
}

bool bktrSectionSeek(u64 offset)
{
    if (!bktrContext.section_offset || !bktrContext.section_size || !bktrContext.relocation_block || !bktrContext.subsection_block)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to seek within NCA BKTR section!");
        return false;
    }
    
    bktr_relocation_entry_t *reloc = bktr_get_relocation(bktrContext.relocation_block, offset);
    if (!reloc) return false;
    
    // No better way to do this than to make all BKTR seeking virtual
    bktrContext.virtual_seek = offset;
    
    u64 section_ofs = (offset - reloc->virt_offset + reloc->phys_offset);
    
    if (reloc->is_patch)
    {
        // Seeked within the patch RomFS
        bktrContext.bktr_seek = section_ofs;
        bktrContext.base_seek = 0;
    } else {
        // Seeked within the base RomFS
        bktrContext.bktr_seek = 0;
        bktrContext.base_seek = section_ofs;
    }
    
    return true;
}

bool bktrSectionPhysicalRead(void *outBuf, size_t bufSize)
{
    if (!bktrContext.section_offset || !bktrContext.section_size || !bktrContext.relocation_block || !bktrContext.subsection_block || !outBuf || !bufSize)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to perform physical block read from NCA BKTR section!");
        return false;
    }
    
    Result result;
    unsigned char ctr[0x10];
    
    bktr_subsection_entry_t *subsec = bktr_get_subsection(bktrContext.subsection_block, bktrContext.bktr_seek);
    if (!subsec) return false;
    
    bktr_subsection_entry_t *next_subsec = (subsec + 1);
    
    u64 base_offset = (bktrContext.section_offset + bktrContext.bktr_seek);
    
    u64 virt_seek = bktrContext.virtual_seek;
    
    if ((bktrContext.bktr_seek + bufSize) <= next_subsec->offset)
    {
        // Easy path, reading *only* within the subsection
        u64 block_start_offset = (base_offset - (base_offset % 0x10));
        u64 block_end_offset = (u64)round_up(base_offset + bufSize, 0x10);
        u64 block_size = (block_end_offset - block_start_offset);
        
        u64 output_offset = 0;
        u64 ctr_buf_offset = (base_offset - block_start_offset);
        u64 output_block_size = (block_size > NCA_CTR_BUFFER_SIZE ? (NCA_CTR_BUFFER_SIZE - (base_offset - block_start_offset)) : bufSize);
        
        while(block_size > 0)
        {
            u64 block_size_used = (block_size > NCA_CTR_BUFFER_SIZE ? NCA_CTR_BUFFER_SIZE : block_size);
            
            result = ncmContentStorageReadContentIdFile(&(bktrContext.ncmStorage), &(bktrContext.ncaId), block_start_offset, ncaCtrBuf, block_size_used);
            if (R_FAILED(result))
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "BKTR: failed to read encrypted %lu bytes block at offset 0x%016lX! (0x%08X)", block_size_used, block_start_offset, result);
                return false;
            }
            
            // Update BKTR CTR
            memcpy(ctr, bktrContext.aes_ctx.ctr, 0x10);
            nca_update_bktr_ctr(ctr, subsec->ctr_val, block_start_offset);
            aes128CtrContextResetCtr(&(bktrContext.aes_ctx), ctr);
            
            // Decrypt CTR block
            aes128CtrCrypt(&(bktrContext.aes_ctx), ncaCtrBuf, ncaCtrBuf, block_size_used);
            memcpy(outBuf + output_offset, ncaCtrBuf + ctr_buf_offset, output_block_size);
            
            block_start_offset += block_size_used;
            block_size -= block_size_used;
            
            if (block_size)
            {
                output_offset += output_block_size;
                ctr_buf_offset = 0;
                output_block_size = (block_size > NCA_CTR_BUFFER_SIZE ? NCA_CTR_BUFFER_SIZE : ((base_offset + bufSize) - block_start_offset));
            }
        }
    } else {
        // Sad path
        u64 within_subsection = (next_subsec->offset - bktrContext.bktr_seek);
        
        if (!readBktrSectionBlock(virt_seek, outBuf, within_subsection)) return false;
        
        if (!readBktrSectionBlock(virt_seek + within_subsection, (u8*)outBuf + within_subsection, bufSize - within_subsection)) return false;
    }
    
    return true;
}

bool readBktrSectionBlock(u64 offset, void *outBuf, size_t bufSize)
{
    if (!bktrContext.section_offset || !bktrContext.section_size || !bktrContext.relocation_block || !bktrContext.subsection_block || !romFsContext.section_offset || !romFsContext.section_size || !outBuf || !bufSize)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to read block from NCA BKTR section!");
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    if (!bktrSectionSeek(offset)) return false;
    
    bktr_relocation_entry_t *reloc = bktr_get_relocation(bktrContext.relocation_block, bktrContext.virtual_seek);
    if (!reloc) return false;
    
    bktr_relocation_entry_t *next_reloc = (reloc + 1);
    
    u64 virt_seek = bktrContext.virtual_seek;
    
    // Perform read operation
    if ((bktrContext.virtual_seek + bufSize) <= next_reloc->virt_offset)
    {
        // Easy path: We're reading *only* within the current relocation
        
        if (reloc->is_patch)
        {
            if (!bktrSectionPhysicalRead(outBuf, bufSize)) return false;
        } else {
            // Nice and easy read from the base RomFS
            if (!processNcaCtrSectionBlock(&(romFsContext.ncmStorage), &(romFsContext.ncaId), &(romFsContext.aes_ctx), romFsContext.section_offset + bktrContext.base_seek, outBuf, bufSize, false)) return false;
        }
    } else {
        u64 within_relocation = (next_reloc->virt_offset - bktrContext.virtual_seek);
        
        if (!readBktrSectionBlock(virt_seek, outBuf, within_relocation)) return false;
        
        if (!readBktrSectionBlock(virt_seek + within_relocation, (u8*)outBuf + within_relocation, bufSize - within_relocation)) return false;
    }
    
    return true;
}

bool encryptNcaHeader(nca_header_t *input, u8 *outBuf, u64 outBufSize)
{
    if (!input || !outBuf || !outBufSize || outBufSize < NCA_FULL_HEADER_LENGTH || (bswap_32(input->magic) != NCA3_MAGIC && bswap_32(input->magic) != NCA2_MAGIC))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NCA header encryption parameters.");
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid output length for encrypted NCA header! (%u != %lu)", NCA_FULL_HEADER_LENGTH, crypt_res);
            return false;
        }
    } else
    if (bswap_32(input->magic) == NCA2_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf, input, NCA_HEADER_LENGTH, 0, true);
        if (crypt_res != NCA_HEADER_LENGTH)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid output length for encrypted NCA header! (%u != %lu)", NCA_HEADER_LENGTH, crypt_res);
            return false;
        }
        
        for(i = 0; i < NCA_SECTION_HEADER_CNT; i++)
        {
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf + NCA_HEADER_LENGTH + (i * NCA_SECTION_HEADER_LENGTH), &(input->fs_headers[i]), NCA_SECTION_HEADER_LENGTH, 0, true);
            if (crypt_res != NCA_SECTION_HEADER_LENGTH)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid output length for encrypted NCA header section #%u! (%u != %lu)", i, NCA_SECTION_HEADER_LENGTH, crypt_res);
                return false;
            }
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid decrypted NCA magic word! (0x%08X)", bswap_32(input->magic));
        return false;
    }
    
    return true;
}

bool decryptNcaHeader(const u8 *ncaBuf, u64 ncaBufSize, nca_header_t *out, title_rights_ctx *rights_info, u8 *decrypted_nca_keys, bool retrieveTitleKeyData)
{
    if (!ncaBuf || !ncaBufSize || ncaBufSize < NCA_FULL_HEADER_LENGTH || !out || !decrypted_nca_keys)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NCA header decryption parameters.");
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    int ret;
    
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid output length for decrypted NCA header! (%u != %lu)", NCA_HEADER_LENGTH, crypt_res);
        return false;
    }
    
    if (bswap_32(out->magic) == NCA3_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, out, ncaBuf, NCA_FULL_HEADER_LENGTH, 0, false);
        if (crypt_res != NCA_FULL_HEADER_LENGTH)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid output length for decrypted NCA header! (%u != %lu)", NCA_FULL_HEADER_LENGTH, crypt_res);
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
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid output length for decrypted NCA header section #%u! (%u != %lu)", i, NCA_SECTION_HEADER_LENGTH, crypt_res);
                    return false;
                }
            } else {
                memset(&(out->fs_headers[i]), 0, sizeof(nca_fs_header_t));
            }
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NCA magic word! Wrong header key? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(out->magic));
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
                    ret = retrieveNcaTikTitleKey(out, (u8*)(&(rights_info->tik_data)), rights_info->enc_titlekey, rights_info->dec_titlekey);
                    
                    if (ret >= 0)
                    {
                        memset(decrypted_nca_keys, 0, NCA_KEY_AREA_SIZE);
                        memcpy(decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), rights_info->dec_titlekey, 0x10);
                        
                        rights_info->retrieved_tik = true;
                    } else {
                        if (ret == -2)
                        {
                            // We are probably dealing with a pre-installed title
                            // Let's enable our missing ticket flag - we'll use it to display a prompt asking the user if they want to proceed anyway
                            rights_info->missing_tik = true;
                        } else {
                            return false;
                        }
                    }
                }
            } else {
                // Copy what we already have
                if (retrieveTitleKeyData && rights_info->retrieved_tik)
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
                
                if (retrieveNcaTikTitleKey(out, NULL, NULL, tmp_dec_titlekey) < 0) return false;
                
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to process Program NCA!");
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_nca_header->fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: Program NCA section #0 doesn't hold a PFS0 partition!");
        return false;
    }
    
    if (!dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid size for PFS0 partition in Program NCA section #0!");
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid AES crypt type for Program NCA section #0! (0x%02X)", dec_nca_header->fs_headers[0].crypt_type);
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
    u8 block_hash[2][SHA256_HASH_SIZE];
    u8 *block_data[2] = { NULL, NULL };
    
    u64 sig_write_size[2] = { 0, 0 };
    
    u8 *hash_table = NULL;
    
    Aes128CtrContext aes_ctx;
    
    section_offset = ((u64)dec_nca_header->section_entries[0].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    hash_table_offset = (section_offset + dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_offset);
    nca_pfs0_offset = (section_offset + dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !hash_table_offset || hash_table_offset < section_offset || !nca_pfs0_offset || nca_pfs0_offset <= hash_table_offset)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid offsets for Program NCA section #0!");
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
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_offset, &nca_pfs0_header, sizeof(pfs0_header), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 partition header!");
        return false;
    }
    
    if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid magic word for Program NCA section #0 PFS0 partition! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(nca_pfs0_header.magic));
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: Program NCA section #0 PFS0 partition is empty! Wrong KAEK?\nTry running Lockpick_RCM to generate the keys file from scratch.");
        return false;
    }
    
    nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
    if (!nca_pfs0_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for Program NCA section #0 PFS0 partition entries!");
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_offset + sizeof(pfs0_header), nca_pfs0_entries, (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 partition entries!");
        free(nca_pfs0_entries);
        return false;
    }
    
    nca_pfs0_data_offset = (nca_pfs0_offset + sizeof(pfs0_header) + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table)) + (u64)nca_pfs0_header.str_table_size);
    
    // Looking for META magic
    for(i = 0; i < nca_pfs0_header.file_cnt; i++)
    {
        u64 nca_pfs0_cur_file_offset = (nca_pfs0_data_offset + nca_pfs0_entries[i].file_offset);
        
        // Read and decrypt NPDM header
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_cur_file_offset, &npdm_header, sizeof(npdm_t), false))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 entry #%u!", i);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to find NPDM entry in Program NCA section #0 PFS0 partition!");
        return false;
    }
    
    // Calculate block offsets
    block_hash_table_offset = (hash_table_offset + (((acid_pubkey_offset - nca_pfs0_offset) / (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size)) * (u64)SHA256_HASH_SIZE);
    block_hash_table_end_offset = (hash_table_offset + (((acid_pubkey_offset + (u64)NPDM_SIGNATURE_SIZE - nca_pfs0_offset) / (u64)dec_nca_header->fs_headers[0].pfs0_superblock.block_size) * (u64)SHA256_HASH_SIZE));
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for Program NCA section #0 PFS0 NPDM block 0!");
        return false;
    }
    
    // Read and decrypt block
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, block_start_offset[0], block_data[0], block_size[0], false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 NPDM block 0!");
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
    
    // Patch ACID public key changing it to a self-generated pubkey
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for Program NCA section #0 PFS0 NPDM block 1!");
            free(block_data[0]);
            return false;
        }
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, block_start_offset[1], block_data[1], block_size[1], false))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 NPDM block 1!");
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for Program NCA section #0 PFS0 hash table!");
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, hash_table_offset, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 hash table!");
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    // Update block hashes
    memcpy(hash_table + (block_hash_table_offset - hash_table_offset), block_hash[0], SHA256_HASH_SIZE);
    if (block_hash_table_offset != block_hash_table_end_offset) memcpy(hash_table + (block_hash_table_end_offset - hash_table_offset), block_hash[1], SHA256_HASH_SIZE);
    
    // Calculate PFS0 superblock master hash
    sha256CalculateHash(dec_nca_header->fs_headers[0].pfs0_superblock.master_hash, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size);
    
    // Calculate section hash
    sha256CalculateHash(dec_nca_header->section_hashes[0], &(dec_nca_header->fs_headers[0]), sizeof(nca_fs_header_t));
    
    // Recreate NPDM signature
    if (!rsa_sign(&(dec_nca_header->magic), NPDM_SIGNATURE_AREA_SIZE, dec_nca_header->npdm_key_sig, NPDM_SIGNATURE_SIZE))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to recreate Program NCA NPDM signature!");
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    // Reencrypt relevant data blocks
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, block_start_offset[0], block_data[0], block_size[0], true))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to encrypt Program NCA section #0 PFS0 NPDM block 0!");
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    if (block_hash_table_offset != block_hash_table_end_offset)
    {
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, block_start_offset[1], block_data[1], block_size[1], true))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to encrypt Program NCA section #0 PFS0 NPDM block 1!");
            free(block_data[0]);
            free(block_data[1]);
            free(hash_table);
            return false;
        }
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, hash_table_offset, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size, true))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to encrypt Program NCA section #0 PFS0 hash table!");
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

bool retrieveCnmtNcaData(FsStorageId curStorageId, nspDumpType selectedNspDumpType, u8 *ncaBuf, cnmt_xml_program_info *xml_program_info, cnmt_xml_content_info *xml_content_info, u32 cnmtNcaIndex, nca_cnmt_mod_data *output, title_rights_ctx *rights_info, bool replaceKeyArea)
{
    if (!ncaBuf || !xml_program_info || !xml_content_info || !output || !rights_info)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to retrieve CNMT NCA!");
        return false;
    }
    
    nca_header_t dec_header;
    
    u32 i, j, k = 0;
    
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
    snprintf(cnmtFileName, MAX_ELEMENTS(cnmtFileName), "%s_%016lx.cnmt", getTitleType(xml_program_info->type), xml_program_info->title_id);
    
    // Decrypt the NCA header
    // Don't retrieve the ticket and/or titlekey if we're dealing with a Patch with titlekey crypto bundled with the inserted gamecard
    if (!decryptNcaHeader(ncaBuf, xml_content_info[cnmtNcaIndex].size, &dec_header, rights_info, xml_content_info[cnmtNcaIndex].decrypted_nca_keys, (curStorageId != FsStorageId_GameCard))) return false;
    
    if (dec_header.fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_header.fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: CNMT NCA section #0 doesn't hold a PFS0 partition!");
        return false;
    }
    
    if (!dec_header.fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid size for PFS0 partition in CNMT NCA section #0!");
        return false;
    }
    
    if (dec_header.fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid AES crypt type for CNMT NCA section #0! (0x%02X)", dec_header.fs_headers[0].crypt_type);
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: Rights ID field in NCA header not empty!");
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
            if (!generateEncryptedNcaKeyAreaWithTitlekey(&dec_header, xml_content_info[cnmtNcaIndex].decrypted_nca_keys)) return false;
            
            // Remove rights ID from NCA
            memset(dec_header.rights_id, 0, 0x10);
        }
    }
    
    section_offset = ((u64)dec_header.section_entries[0].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    section_size = (((u64)dec_header.section_entries[0].media_end_offset * (u64)MEDIA_UNIT_SIZE) - section_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid offset/size for CNMT NCA section #0!");
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
    memcpy(ctr_key, xml_content_info[cnmtNcaIndex].decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    aes128CtrContextCreate(&aes_ctx, ctr_key, ctr);
    
    section_data = malloc(section_size);
    if (!section_data)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the decrypted CNMT NCA section #0!");
        return false;
    }
    
    aes128CtrCrypt(&aes_ctx, section_data, ncaBuf + section_offset, section_size);
    
    nca_pfs0_offset = dec_header.fs_headers[0].pfs0_superblock.pfs0_offset;
    memcpy(&nca_pfs0_header, section_data + nca_pfs0_offset, sizeof(pfs0_header));
    
    if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid magic word for CNMT NCA section #0 PFS0 partition! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(nca_pfs0_header.magic));
        free(section_data);
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: CNMT NCA section #0 PFS0 partition is empty! Wrong KAEK?\nTry running Lockpick_RCM to generate the keys file from scratch.");
        free(section_data);
        return false;
    }
    
    nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
    if (!nca_pfs0_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for CNMT NCA section #0 PFS0 partition entries!");
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to find file \"%s\" in PFS0 partition from CNMT NCA section #0!", cnmtFileName);
        free(section_data);
        return false;
    }
    
    memcpy(&title_cnmt_header, section_data + title_cnmt_offset, sizeof(cnmt_header));
    memcpy(&title_cnmt_extended_header, section_data + title_cnmt_offset + sizeof(cnmt_header), sizeof(cnmt_extended_header));
    
    // Fill information for our CNMT XML
    digest_offset = (title_cnmt_offset + title_cnmt_size - (u64)SHA256_HASH_SIZE);
    memcpy(xml_program_info->digest, section_data + digest_offset, SHA256_HASH_SIZE);
    convertDataToHexString(xml_program_info->digest, SHA256_HASH_SIZE, xml_program_info->digest_str, (SHA256_HASH_SIZE * 2) + 1);
    xml_content_info[cnmtNcaIndex].keyblob = (dec_header.crypto_type2 > dec_header.crypto_type ? dec_header.crypto_type2 : dec_header.crypto_type);
    xml_program_info->required_dl_sysver = title_cnmt_header.required_dl_sysver;
    xml_program_info->min_keyblob = (rights_info->has_rights_id ? rights_info->rights_id[15] : xml_content_info[cnmtNcaIndex].keyblob);
    xml_program_info->min_sysver = title_cnmt_extended_header.min_sysver;
    xml_program_info->patch_tid = title_cnmt_extended_header.patch_tid;
    
    // Retrieve the ID offset and content record offset for each of our NCAs (except the CNMT NCA)
    // Also wipe each of the content records we're gonna replace (excluding Delta Fragments)
    for(i = 0; i < (xml_program_info->nca_cnt - 1); i++) // Discard CNMT NCA
    {
        for(j = 0; j < title_cnmt_header.content_cnt; j++)
        {
            cnmt_content_record cnt_record;
            memcpy(&cnt_record, section_data + title_cnmt_offset + sizeof(cnmt_header) + (u64)title_cnmt_header.extended_header_size + (j * sizeof(cnmt_content_record)), sizeof(cnmt_content_record));
            
            if (cnt_record.type == NCA_CONTENT_TYPE_DELTA) continue;
            
            if (!memcmp(xml_content_info[i].nca_id, cnt_record.nca_id, SHA256_HASH_SIZE / 2) || xml_content_info[i].type == cnt_record.type)
            {
                // Save ID offset and content record offset
                xml_content_info[i].id_offset = cnt_record.id_offset;
                xml_content_info[i].cnt_record_offset = (j * sizeof(cnmt_content_record));
                
                // Empty CNMT content record
                memset(section_data + title_cnmt_offset + sizeof(cnmt_header) + (u64)title_cnmt_header.extended_header_size + (j * sizeof(cnmt_content_record)), 0, sizeof(cnmt_content_record));
                
                // Increase counter
                k++;
                
                break;
            }
        }
    }
    
    // Verify counter
    if (k != (xml_program_info->nca_cnt - 1))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid content record entries in the CNMT NCA!");
        free(section_data);
        return false;
    }
    
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
    output->hash_block_size = dec_header.fs_headers[0].pfs0_superblock.block_size;
    output->hash_block_cnt = (dec_header.fs_headers[0].pfs0_superblock.hash_table_size / SHA256_HASH_SIZE);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to patch CNMT NCA!");
        return false;
    }
    
    u32 i;
    
    u32 nca_cnt = (xml_program_info->nca_cnt - 1); // Discard CNMT NCA
    
    cnmt_header title_cnmt_header;
    cnmt_content_record title_cnmt_content_record;
    u64 title_cnmt_content_records_offset;
    
    nca_header_t dec_header;
    
    Aes128CtrContext aes_ctx;
    
    // Copy CNMT header
    memcpy(&title_cnmt_header, ncaBuf + cnmt_mod->title_cnmt_offset, sizeof(cnmt_header));
    
    // Calculate the start offset for the content records
    title_cnmt_content_records_offset = (cnmt_mod->title_cnmt_offset + sizeof(cnmt_header) + (u64)title_cnmt_header.extended_header_size);
    
    // Write content records
    for(i = 0; i < nca_cnt; i++)
    {
        memset(&title_cnmt_content_record, 0, sizeof(cnmt_content_record));
        
        memcpy(title_cnmt_content_record.hash, xml_content_info[i].hash, SHA256_HASH_SIZE);
        memcpy(title_cnmt_content_record.nca_id, xml_content_info[i].nca_id, SHA256_HASH_SIZE / 2);
        convertU64ToNcaSize(xml_content_info[i].size, title_cnmt_content_record.size);
        title_cnmt_content_record.type = xml_content_info[i].type;
        title_cnmt_content_record.id_offset = xml_content_info[i].id_offset;
        
        memcpy(ncaBuf + title_cnmt_content_records_offset + xml_content_info[i].cnt_record_offset, &title_cnmt_content_record, sizeof(cnmt_content_record));
    }
    
    // Recalculate block hashes
    for(i = 0; i < cnmt_mod->hash_block_cnt; i++)
    {
        u64 blk_offset = ((u64)i * cnmt_mod->hash_block_size);
        
        u64 rest_size = (cnmt_mod->pfs0_size - blk_offset);
        u64 blk_size = (rest_size > cnmt_mod->hash_block_size ? cnmt_mod->hash_block_size : rest_size);
        
        sha256CalculateHash(ncaBuf + cnmt_mod->hash_table_offset + (i * SHA256_HASH_SIZE), ncaBuf + cnmt_mod->pfs0_offset + blk_offset, blk_size);
    }
    
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to encrypt modified CNMT NCA header!");
        return false;
    }
    
    aes128CtrCrypt(&aes_ctx, ncaBuf + cnmt_mod->section_offset, ncaBuf + cnmt_mod->section_offset, cnmt_mod->section_size);
    
    // Calculate CNMT NCA SHA-256 checksum and fill information for our CNMT XML
    sha256CalculateHash(xml_content_info[xml_program_info->nca_cnt - 1].hash, ncaBuf, ncaBufSize);
    convertDataToHexString(xml_content_info[xml_program_info->nca_cnt - 1].hash, SHA256_HASH_SIZE, xml_content_info[xml_program_info->nca_cnt - 1].hash_str, (SHA256_HASH_SIZE * 2) + 1);
    memcpy(xml_content_info[xml_program_info->nca_cnt - 1].nca_id, xml_content_info[xml_program_info->nca_cnt - 1].hash, SHA256_HASH_SIZE / 2);
    convertDataToHexString(xml_content_info[xml_program_info->nca_cnt - 1].nca_id, SHA256_HASH_SIZE / 2, xml_content_info[xml_program_info->nca_cnt - 1].nca_id_str, SHA256_HASH_SIZE + 1);
    
    return true;
}

bool readExeFsEntryFromNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to read RomFS section from NCA!");
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
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_offset, &nca_pfs0_header, sizeof(pfs0_header), false)) return false;
        
        if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC || !nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size) continue;
        
        nca_pfs0_entries_offset = (nca_pfs0_offset + sizeof(pfs0_header));
        
        nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
        if (!nca_pfs0_entries) continue;
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_entries_offset, nca_pfs0_entries, (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table), false))
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
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_str_table_offset, nca_pfs0_str_table, (u64)nca_pfs0_header.str_table_size, false))
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: NCA doesn't hold an ExeFS section! Wrong keys?\nTry running Lockpick_RCM to generate the keys file from scratch.");
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to read RomFS section from NCA!");
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: NCA doesn't hold a RomFS section!");
        return false;
    }
    
    section_offset = ((u64)dec_nca_header->section_entries[romfs_index].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    section_size = (((u64)dec_nca_header->section_entries[romfs_index].media_end_offset * (u64)MEDIA_UNIT_SIZE) - section_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid offset/size for NCA RomFS section! (#%u)", romfs_index);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid IVFC magic word for NCA RomFS section! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.magic));
        return false;
    }
    
    if (dec_nca_header->fs_headers[romfs_index].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid AES crypt type for NCA RomFS section! (0x%02X)", dec_nca_header->fs_headers[romfs_index].crypt_type);
        return false;
    }
    
    romfs_offset = (section_offset + dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].logical_offset);
    romfs_size = dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].hash_data_size;
    
    if (romfs_offset < section_offset || romfs_offset >= (section_offset + section_size) || romfs_size < ROMFS_HEADER_SIZE || (romfs_offset + romfs_size) > (section_offset + section_size))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid offset/size for NCA RomFS section!");
        return false;
    }
    
    // First read the RomFS header
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, romfs_offset, &romFsHeader, sizeof(romfs_header), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA RomFS section header!");
        return false;
    }
    
    if (romFsHeader.headerSize != ROMFS_HEADER_SIZE)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid header size for NCA RomFS section! (0x%016lX at 0x%016lX)", romFsHeader.headerSize, romfs_offset);
        return false;
    }
    
    romfs_dirtable_offset = (romfs_offset + romFsHeader.dirTableOff);
    romfs_dirtable_size = romFsHeader.dirTableSize;
    
    romfs_filetable_offset = (romfs_offset + romFsHeader.fileTableOff);
    romfs_filetable_size = romFsHeader.fileTableSize;
    
    if (romfs_dirtable_offset >= (section_offset + section_size) || !romfs_dirtable_size || (romfs_dirtable_offset + romfs_dirtable_size) > (section_offset + section_size) || romfs_filetable_offset >= (section_offset + section_size) || !romfs_filetable_size || (romfs_filetable_offset + romfs_filetable_size) > (section_offset + section_size))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid directory/file table for NCA RomFS section!");
        return false;
    }
    
    romfs_filedata_offset = (romfs_offset + romFsHeader.fileDataOff);
    
    if (romfs_filedata_offset >= (section_offset + section_size))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid file data block offset for NCA RomFS section!");
        return false;
    }
    
    romfs_dir_entries = calloc(1, romfs_dirtable_size);
    if (!romfs_dir_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA RomFS section directory entries!");
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, romfs_dirtable_offset, romfs_dir_entries, romfs_dirtable_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA RomFS section directory entries!");
        free(romfs_dir_entries);
        return false;
    }
    
    romfs_file_entries = calloc(1, romfs_filetable_size);
    if (!romfs_file_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA RomFS section file entries!");
        free(romfs_dir_entries);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, romfs_filetable_offset, romfs_file_entries, romfs_filetable_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA RomFS section file entries!");
        free(romfs_file_entries);
        free(romfs_dir_entries);
        return false;
    }
    
    // Save data to output struct
    // The caller function must free these data pointers
    memcpy(&(romFsContext.ncmStorage), ncmStorage, sizeof(NcmContentStorage));
    memcpy(&(romFsContext.ncaId), ncaId, sizeof(NcmNcaId));
    memcpy(&(romFsContext.aes_ctx), &aes_ctx, sizeof(Aes128CtrContext));
    romFsContext.section_offset = section_offset;
    romFsContext.section_size = section_size;
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

bool readBktrEntryFromNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys || !romFsContext.section_offset || !romFsContext.section_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_file_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to read BKTR section from NCA!");
        return false;
    }
    
    u32 i;
    
    u8 bktr_index;
    bool found_bktr = false, success = false;
    
    romfs_header romFsHeader;
    
    initBktrContext();
    
    memcpy(&(bktrContext.ncmStorage), ncmStorage, sizeof(NcmContentStorage));
    memcpy(&(bktrContext.ncaId), ncaId, sizeof(NcmNcaId));
    
    for(bktr_index = 0; bktr_index < 4; bktr_index++)
    {
        if (dec_nca_header->fs_headers[bktr_index].partition_type == NCA_FS_HEADER_PARTITION_ROMFS && dec_nca_header->fs_headers[bktr_index].fs_type == NCA_FS_HEADER_FSTYPE_ROMFS)
        {
            found_bktr = true;
            break;
        }
    }
    
    if (!found_bktr)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: NCA doesn't hold a BKTR section!");
        return false;
    }
    
    bktrContext.section_offset = ((u64)dec_nca_header->section_entries[bktr_index].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    bktrContext.section_size = (((u64)dec_nca_header->section_entries[bktr_index].media_end_offset * (u64)MEDIA_UNIT_SIZE) - bktrContext.section_offset);
    
    if (!bktrContext.section_offset || bktrContext.section_offset < NCA_FULL_HEADER_LENGTH || !bktrContext.section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid offset/size for NCA BKTR section! (#%u)", bktr_index);
        return false;
    }
    
    // Generate initial CTR
    unsigned char ctr[0x10];
    u64 ofs = (bktrContext.section_offset >> 4);
    
    for(i = 0; i < 0x8; i++)
    {
        ctr[i] = dec_nca_header->fs_headers[bktr_index].section_ctr[0x08 - i - 1];
        ctr[0x10 - i - 1] = (unsigned char)(ofs & 0xFF);
        ofs >>= 8;
    }
    
    u8 ctr_key[NCA_KEY_AREA_KEY_SIZE];
    memcpy(ctr_key, decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    aes128CtrContextCreate(&(bktrContext.aes_ctx), ctr_key, ctr);
    
    memcpy(&(bktrContext.superblock), &(dec_nca_header->fs_headers[bktr_index].bktr_superblock), sizeof(bktr_superblock_t));
    
    if (bswap_32(bktrContext.superblock.ivfc_header.magic) != IVFC_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid IVFC magic word for NCA BKTR section! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(bktrContext.superblock.ivfc_header.magic));
        return false;
    }
    
    if (dec_nca_header->fs_headers[bktr_index].crypt_type != NCA_FS_HEADER_CRYPT_BKTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid AES crypt type for NCA BKTR section! (0x%02X)", dec_nca_header->fs_headers[bktr_index].crypt_type);
        return false;
    }
    
    if (bswap_32(bktrContext.superblock.relocation_header.magic) != BKTR_MAGIC || bswap_32(bktrContext.superblock.subsection_header.magic) != BKTR_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid BKTR magic word for NCA BKTR relocation/subsection header! Wrong KAEK? (0x%02X | 0x%02X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(bktrContext.superblock.relocation_header.magic), bswap_32(bktrContext.superblock.subsection_header.magic));
        return false;
    }
    
    if ((bktrContext.superblock.relocation_header.offset + bktrContext.superblock.relocation_header.size) != bktrContext.superblock.subsection_header.offset || (bktrContext.superblock.subsection_header.offset + bktrContext.superblock.subsection_header.size) != bktrContext.section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid layout for NCA BKTR section!");
        return false;
    }
    
    // Allocate space for an extra (fake) relocation entry, to simplify our logic
    bktrContext.relocation_block = calloc(1, bktrContext.superblock.relocation_header.size + ((0x3FF0 / sizeof(u64)) * sizeof(bktr_relocation_entry_t)));
    if (!bktrContext.relocation_block)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA BKTR relocation header!");
        return false;
    }
    
    // Allocate space for an extra (fake) subsection entry, to simplify our logic
    bktrContext.subsection_block = calloc(1, bktrContext.superblock.subsection_header.size + ((0x3FF0 / sizeof(u64)) * sizeof(bktr_subsection_entry_t)) + sizeof(bktr_subsection_entry_t));
    if (!bktrContext.subsection_block)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA BKTR subsection header!");
        goto out;
    }
    
    // Read the relocation header
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &(bktrContext.aes_ctx), bktrContext.section_offset + bktrContext.superblock.relocation_header.offset, bktrContext.relocation_block, bktrContext.superblock.relocation_header.size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA BKTR relocation header!");
        goto out;
    }
    
    // Read the subsection header
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &(bktrContext.aes_ctx), bktrContext.section_offset + bktrContext.superblock.subsection_header.offset, bktrContext.subsection_block, bktrContext.superblock.subsection_header.size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA BKTR subsection header!");
        goto out;
    }
    
    if (bktrContext.subsection_block->total_size != bktrContext.superblock.subsection_header.offset)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NCA BKTR subsection header size!");
        goto out;
    }
    
    // This simplifies logic greatly...
    for(i = (bktrContext.relocation_block->num_buckets - 1); i > 0; i--)
    {
        bktr_relocation_bucket_t tmp_bucket;
        memcpy(&tmp_bucket, &(bktrContext.relocation_block->buckets[i]), sizeof(bktr_relocation_bucket_t));
        memcpy(bktr_get_relocation_bucket(bktrContext.relocation_block, i), &tmp_bucket, sizeof(bktr_relocation_bucket_t));
    }
    
    for(i = 0; (i + 1) < bktrContext.relocation_block->num_buckets; i++)
    {
        bktr_relocation_bucket_t *cur_bucket = bktr_get_relocation_bucket(bktrContext.relocation_block, i);
        cur_bucket->entries[cur_bucket->num_entries].virt_offset = bktrContext.relocation_block->bucket_virtual_offsets[i + 1];
    }
    
    for(i = (bktrContext.subsection_block->num_buckets - 1); i > 0; i--)
    {
        bktr_subsection_bucket_t tmp_bucket;
        memcpy(&tmp_bucket, &(bktrContext.subsection_block->buckets[i]), sizeof(bktr_subsection_bucket_t));
        memcpy(bktr_get_subsection_bucket(bktrContext.subsection_block, i), &tmp_bucket, sizeof(bktr_subsection_bucket_t));
    }
    
    for(i = 0; (i + 1) < bktrContext.subsection_block->num_buckets; i++)
    {
        bktr_subsection_bucket_t *cur_bucket = bktr_get_subsection_bucket(bktrContext.subsection_block, i);
        bktr_subsection_bucket_t *next_bucket = bktr_get_subsection_bucket(bktrContext.subsection_block, i + 1);
        cur_bucket->entries[cur_bucket->num_entries].offset = next_bucket->entries[0].offset;
        cur_bucket->entries[cur_bucket->num_entries].ctr_val = next_bucket->entries[0].ctr_val;
    }
    
    bktr_relocation_bucket_t *last_reloc_bucket = bktr_get_relocation_bucket(bktrContext.relocation_block, bktrContext.relocation_block->num_buckets - 1);
    bktr_subsection_bucket_t *last_subsec_bucket = bktr_get_subsection_bucket(bktrContext.subsection_block, bktrContext.subsection_block->num_buckets - 1);
    last_reloc_bucket->entries[last_reloc_bucket->num_entries].virt_offset = bktrContext.relocation_block->total_size;
    last_subsec_bucket->entries[last_subsec_bucket->num_entries].offset = bktrContext.superblock.relocation_header.offset;
    last_subsec_bucket->entries[last_subsec_bucket->num_entries].ctr_val = dec_nca_header->fs_headers[bktr_index].section_ctr_low;
    last_subsec_bucket->entries[last_subsec_bucket->num_entries + 1].offset = bktrContext.section_size;
    last_subsec_bucket->entries[last_subsec_bucket->num_entries + 1].ctr_val = 0;
    
    // Parse RomFS section
    bktrContext.romfs_offset = dec_nca_header->fs_headers[bktr_index].bktr_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].logical_offset;
    bktrContext.romfs_size = dec_nca_header->fs_headers[bktr_index].bktr_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].hash_data_size;
    
    // Do not check the RomFS size, because it reflects the full patched RomFS image
    if (!bktrContext.romfs_offset || bktrContext.romfs_size < ROMFS_HEADER_SIZE || (bktrContext.section_offset + bktrContext.romfs_offset) > (bktrContext.section_offset + bktrContext.section_size))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid offset/size for NCA BKTR RomFS section!");
        goto out;
    }
    
    if (!readBktrSectionBlock(bktrContext.romfs_offset, &romFsHeader, sizeof(romfs_header)))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA BKTR RomFS section header!");
        goto out;
    }
    
    if (romFsHeader.headerSize != ROMFS_HEADER_SIZE)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid header size for NCA BKTR RomFS section! (0x%016lX at 0x%016lX)", romFsHeader.headerSize, bktrContext.section_offset + bktrContext.romfs_offset);
        goto out;
    }
    
    bktrContext.romfs_dirtable_offset = (bktrContext.romfs_offset + romFsHeader.dirTableOff);
    bktrContext.romfs_dirtable_size = romFsHeader.dirTableSize;
    
    bktrContext.romfs_filetable_offset = (bktrContext.romfs_offset + romFsHeader.fileTableOff);
    bktrContext.romfs_filetable_size = romFsHeader.fileTableSize;
    
    // Then again, do not check these offsets/sizes, because they reflect the patched RomFS image
    if (!bktrContext.romfs_dirtable_offset || !bktrContext.romfs_dirtable_size || !bktrContext.romfs_filetable_offset || !bktrContext.romfs_filetable_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid directory/file table for NCA BKTR RomFS section!");
        goto out;
    }
    
    bktrContext.romfs_filedata_offset = (bktrContext.romfs_offset + romFsHeader.fileDataOff);
    
    if (!bktrContext.romfs_filedata_offset)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid file data block offset for NCA BKTR RomFS section!");
        goto out;
    }
    
    bktrContext.romfs_dir_entries = calloc(1, bktrContext.romfs_dirtable_size);
    if (!bktrContext.romfs_dir_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA BKTR RomFS section directory entries!");
        goto out;
    }
    
    if (!readBktrSectionBlock(bktrContext.romfs_dirtable_offset, bktrContext.romfs_dir_entries, bktrContext.romfs_dirtable_size))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA BKTR RomFS section directory entries!");
        goto out;
    }
    
    bktrContext.romfs_file_entries = calloc(1, bktrContext.romfs_filetable_size);
    if (!bktrContext.romfs_file_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for NCA BKTR RomFS section file entries!");
        goto out;
    }
    
    if (!readBktrSectionBlock(bktrContext.romfs_filetable_offset, bktrContext.romfs_file_entries, bktrContext.romfs_filetable_size))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NCA RomFS section file entries!");
        goto out;
    }
    
    success = true;
    
out:
    if (!success)
    {
        if (bktrContext.romfs_file_entries != NULL)
        {
            free(bktrContext.romfs_file_entries);
            bktrContext.romfs_file_entries = NULL;
        }
        
        if (bktrContext.romfs_dir_entries != NULL)
        {
            free(bktrContext.romfs_dir_entries);
            bktrContext.romfs_dir_entries = NULL;
        }
        
        if (bktrContext.subsection_block != NULL)
        {
            free(bktrContext.subsection_block);
            bktrContext.subsection_block = NULL;
        }
        
        if (bktrContext.relocation_block != NULL)
        {
            free(bktrContext.relocation_block);
            bktrContext.relocation_block = NULL;
        }
    }
    
    // The caller function must free the data pointers from the bktrContext struct
    
    return success;
}

bool generateProgramInfoXml(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys, nca_program_mod_data *program_mod_data, char **outBuf, u64 *outBufSize)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys || !program_mod_data || !outBuf || !outBufSize)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to generate \"programinfo.xml\"!");
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_nca_header->fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: Program NCA section #0 doesn't hold a PFS0 partition!");
        return false;
    }
    
    if (!dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid size for PFS0 partition in Program NCA section #0!");
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid AES crypt type for Program NCA section #0! (0x%02X)", dec_nca_header->fs_headers[0].crypt_type);
        return false;
    }
    
    u32 i;
    
    bool proceed = true, success = false;
    
    u64 section_offset;
    u64 nca_pfs0_offset;
    
    pfs0_header nca_pfs0_header;
    pfs0_entry_table *nca_pfs0_entries = NULL;
    char *nca_pfs0_str_table = NULL;
    
    u64 nca_pfs0_str_table_offset;
    u64 nca_pfs0_data_offset;
    
    Aes128CtrContext aes_ctx;
    
    char *programInfoXml = NULL;
    char tmp[NAME_BUF_LEN] = {'\0'};
    
    u32 npdmEntry = 0;
    npdm_t npdm_header;
    u8 *npdm_acid_section = NULL;
    
    u64 npdm_acid_section_b64_size = 0;
    char *npdm_acid_section_b64 = NULL;
    
    u32 acid_flags = 0;
    
    section_offset = ((u64)dec_nca_header->section_entries[0].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    nca_pfs0_offset = (section_offset + dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !nca_pfs0_offset)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid offsets for Program NCA section #0!");
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
    memcpy(ctr_key, decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), NCA_KEY_AREA_KEY_SIZE);
    aes128CtrContextCreate(&aes_ctx, ctr_key, ctr);
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_offset, &nca_pfs0_header, sizeof(pfs0_header), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 partition header!");
        return false;
    }
    
    if (bswap_32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid magic word for Program NCA section #0 PFS0 partition! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(nca_pfs0_header.magic));
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: Program NCA section #0 PFS0 partition is empty! Wrong KAEK?\nTry running Lockpick_RCM to generate the keys file from scratch.");
        return false;
    }
    
    nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_entry_table));
    if (!nca_pfs0_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for Program NCA section #0 PFS0 partition entries!");
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_offset + sizeof(pfs0_header), nca_pfs0_entries, (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 partition entries!");
        goto out;
    }
    
    nca_pfs0_str_table_offset = (nca_pfs0_offset + sizeof(pfs0_header) + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_entry_table)));
    
    nca_pfs0_str_table = calloc((u64)nca_pfs0_header.str_table_size, sizeof(char));
    if (!nca_pfs0_str_table)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for Program NCA section #0 PFS0 string table!");
        goto out;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_str_table_offset, nca_pfs0_str_table, (u64)nca_pfs0_header.str_table_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read Program NCA section #0 PFS0 string table!");
        goto out;
    }
    
    nca_pfs0_data_offset = (nca_pfs0_str_table_offset + (u64)nca_pfs0_header.str_table_size);
    
    // Allocate memory for the programinfo.xml contents, making sure there's enough space
    programInfoXml = calloc(NSP_XML_BUFFER_SIZE, sizeof(char));
    if (!programInfoXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the \"programinfo.xml\" contents!");
        goto out;
    }
    
    sprintf(programInfoXml, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
                            "<ProgramInfo>\n" \
                            "  <SdkVersion>%u_%u_%u</SdkVersion>\n", dec_nca_header->sdk_major, dec_nca_header->sdk_minor, dec_nca_header->sdk_micro);
    
    // Retrieve the main.npdm contents
    bool found_npdm = false;
    for(i = 0; i < nca_pfs0_header.file_cnt; i++)
    {
        char *curFilename = (nca_pfs0_str_table + nca_pfs0_entries[i].filename_offset);
        
        if (strlen(curFilename) == 9 && !strncasecmp(curFilename, "main.npdm", 9) && nca_pfs0_entries[i].file_size > 0)
        {
            found_npdm = true;
            npdmEntry = i;
            break;
        }
    }
    
    if (!found_npdm)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the \"programinfo.xml\" contents!");
        goto out;
    }
    
    // Read the META header from the NPDM
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_data_offset + nca_pfs0_entries[npdmEntry].file_offset, &npdm_header, sizeof(npdm_t), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read NPDM entry header from Program NCA section #0 PFS0!");
        goto out;
    }
    
    if (bswap_32(npdm_header.magic) != META_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid NPDM META magic word! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", bswap_32(npdm_header.magic));
        goto out;
    }
    
    // Allocate memory for the ACID section
    npdm_acid_section = malloc(npdm_header.acid_size);
    if (!npdm_acid_section)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the Program NCA NPDM ACID section contents!");
        goto out;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_data_offset + nca_pfs0_entries[npdmEntry].file_offset + (u64)npdm_header.acid_offset, npdm_acid_section, (u64)npdm_header.acid_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read ACID section from Program NCA NPDM!");
        goto out;
    }
    
    // If we're dealing with a gamecard title, replace the ACID public key with the patched one
    if (program_mod_data->block_mod_cnt > 0) memcpy(npdm_acid_section + (u64)NPDM_SIGNATURE_SIZE, rsa_get_public_key(), (u64)NPDM_SIGNATURE_SIZE);
    
    sprintf(tmp, "  <BuildTarget>%u</BuildTarget>\n", ((npdm_header.mmu_flags & 0x01) ? 64 : 32));
    strcat(programInfoXml, tmp);
    
    // Default this one to Release
    strcat(programInfoXml, "  <BuildType>Release</BuildType>\n");
    
    // Retrieve the Base64 conversion length for the whole ACID section
    mbedtls_base64_encode(NULL, 0, &npdm_acid_section_b64_size, npdm_acid_section, (u64)npdm_header.acid_size);
    if (npdm_acid_section_b64_size <= (u64)npdm_header.acid_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid Base64 conversion length for the ACID section from Program NCA NPDM!");
        goto out;
    }
    
    npdm_acid_section_b64 = calloc(npdm_acid_section_b64_size + 1, sizeof(char));
    if (!npdm_acid_section_b64)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the Base64 converted ACID section from Program NCA NPDM!");
        goto out;
    }
    
    // Perform the Base64 conversion
    if (mbedtls_base64_encode((unsigned char*)npdm_acid_section_b64, npdm_acid_section_b64_size + 1, &npdm_acid_section_b64_size, npdm_acid_section, (u64)npdm_header.acid_size) != 0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: Base64 conversion failed for the ACID section from Program NCA NPDM!");
        goto out;
    }
    
    strcat(programInfoXml, "  <Desc>");
    strcat(programInfoXml, npdm_acid_section_b64);
    strcat(programInfoXml, "</Desc>\n");
    
    // TO-DO: Add more ACID flags?
    
    acid_flags = *((u32*)(&(npdm_acid_section[0x20C])));
    
    strcat(programInfoXml, "  <DescFlags>\n");
    
    sprintf(tmp, "    <Production>%s</Production>\n", ((acid_flags & 0x01) ? "true" : "false"));
    strcat(programInfoXml, tmp);
    
    sprintf(tmp, "    <UnqualifiedApproval>%s</UnqualifiedApproval>\n", ((acid_flags & 0x02) ? "true" : "false"));
    strcat(programInfoXml, tmp);
    
    strcat(programInfoXml, "  </DescFlags>\n");
    
    // Middleware list
    strcat(programInfoXml, "  <MiddlewareList>\n");
    
    for(i = 0; i < nca_pfs0_header.file_cnt; i++)
    {
        nso_header_t nsoHeader;
        char *curFilename = (nca_pfs0_str_table + nca_pfs0_entries[i].filename_offset);
        u64 curFileOffset = (nca_pfs0_data_offset + nca_pfs0_entries[i].file_offset);
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, curFileOffset, &nsoHeader, sizeof(nso_header_t), false))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read 0x%016lX bytes from \"%s\" in Program NCA section #0 PFS0 partition!", sizeof(nso_header_t), curFilename);
            proceed = false;
            break;
        }
        
        // Check if we're dealing with a NSO
        if (bswap_32(nsoHeader.magic) != NSO_MAGIC) continue;
        
        // Retrieve middleware list from this NSO
        if (!retrieveMiddlewareListFromNso(ncmStorage, ncaId, &aes_ctx, curFilename, curFileOffset, &nsoHeader, programInfoXml))
        {
            proceed = false;
            break;
        }
    }
    
    if (!proceed) goto out;
    
    strcat(programInfoXml, "  </MiddlewareList>\n");
    
    // Leave these fields empty (for now)
    strcat(programInfoXml, "  <DebugApiList />\n");
    strcat(programInfoXml, "  <PrivateApiList />\n");
    
    // Symbols list from main NSO
    strcat(programInfoXml, "  <UnresolvedApiList>\n");
    
    for(i = 0; i < nca_pfs0_header.file_cnt; i++)
    {
        nso_header_t nsoHeader;
        char *curFilename = (nca_pfs0_str_table + nca_pfs0_entries[i].filename_offset);
        u64 curFileOffset = (nca_pfs0_data_offset + nca_pfs0_entries[i].file_offset);
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, curFileOffset, &nsoHeader, sizeof(nso_header_t), false))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read 0x%016lX bytes from \"%s\" in Program NCA section #0 PFS0 partition!", sizeof(nso_header_t), curFilename);
            proceed = false;
            break;
        }
        
        // Check if we're dealing with the main NSO
        if (strlen(curFilename) != 4 || strncmp(curFilename, "main", 4) != 0 || bswap_32(nsoHeader.magic) != NSO_MAGIC) continue;
        
        // Retrieve symbols list from main NSO
        if (!retrieveSymbolsListFromNso(ncmStorage, ncaId, &aes_ctx, curFilename, curFileOffset, &nsoHeader, programInfoXml)) proceed = false;
        
        break;
    }
    
    if (!proceed) goto out;
    
    strcat(programInfoXml, "  </UnresolvedApiList>\n");
    
    // Leave this field empty (for now)
    strcat(programInfoXml, "  <FsAccessControlData />\n");
    
    strcat(programInfoXml, "</ProgramInfo>");
    
    *outBuf = programInfoXml;
    *outBufSize = strlen(programInfoXml);
    
    success = true;
    
out:
    if (npdm_acid_section_b64) free(npdm_acid_section_b64);
    
    if (npdm_acid_section) free(npdm_acid_section);
    
    if (!success && programInfoXml) free(programInfoXml);
    
    if (nca_pfs0_str_table) free(nca_pfs0_str_table);
    
    if (nca_pfs0_entries) free(nca_pfs0_entries);
    
    return success;
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
        case 12:
            out = "IARCGeneric";
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

char *getNacpRuntimeParameterDelivery(u8 val)
{
    char *out = NULL;
    
    switch(val)
    {
        case 0:
            out = "Always";
            break;
        case 1:
            out = "AlwaysIfUserStateMatched";
            break;
        case 2:
            out = "OnRestart";
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

char *getNacpJitConfigurationFlag(u64 flag)
{
    char *out = NULL;
    
    switch(flag)
    {
        case 0:
            out = "None";
            break;
        case 1:
            out = "Enabled";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
}

bool retrieveNacpDataFromNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys, char **out_nacp_xml, u64 *out_nacp_xml_size, nacp_icons_ctx **out_nacp_icons_ctx, u8 *out_nacp_icons_ctx_cnt)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys || !out_nacp_xml || !out_nacp_xml_size || !out_nacp_icons_ctx || !out_nacp_icons_ctx_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to generate NACP XML!");
        return false;
    }
    
    u64 entryOffset = 0;
    romfs_file *entry = NULL;
    bool found_nacp = false, success = false;
    
    nacp_t controlNacp;
    char *nacpXml = NULL;
    
    u8 i = 0, j = 0;
    char tmp[NAME_BUF_LEN] = {'\0'};
    
    u8 nacpIconCnt = 0;
    nacp_icons_ctx *nacpIcons = NULL;
    
    bool found_icon = false;
    u8 languageIconHash[0x20];
    char languageIconHashStr[0x21];
    
    char ncaIdStr[0x21] = {'\0'};
    convertDataToHexString(ncaId->c, 0x10, ncaIdStr, 0x21);
    
    char dataStr[100] = {'\0'};
    
    u8 null_key[0x10];
    memset(null_key, 0, 0x10);
    
    bool availableSDC = false, availableRDC = false;
    
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to find \"control.nacp\" file in Control NCA RomFS section!");
        goto out;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &(romFsContext.aes_ctx), romFsContext.romfs_filedata_offset + entry->dataOff, &controlNacp, sizeof(nacp_t), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read \"control.nacp\" from RomFS section in Control NCA!");
        goto out;
    }
    
    // Make sure that the output buffer for our NACP XML is big enough
    nacpXml = calloc(NSP_XML_BUFFER_SIZE, sizeof(char));
    if (!nacpXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the NACP XML!");
        goto out;
    }
    
    sprintf(nacpXml, "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
                     "<Application>\n");
    
    for(i = 0; i < 16; i++)
    {
        if (strlen(controlNacp.lang[i].name) || strlen(controlNacp.lang[i].author))
        {
            sprintf(tmp, "  <Title>\n" \
                         "    <Language>%s</Language>\n" \
                         "    <Name>%s</Name>\n" \
                         "    <Publisher>%s</Publisher>\n" \
                         "  </Title>\n", \
                         getNacpLangName(i), \
                         controlNacp.lang[i].name, \
                         controlNacp.lang[i].author);
            
            strcat(nacpXml, tmp);
        }
    }
    
    if (strlen(controlNacp.Isbn))
    {
        sprintf(tmp, "  <Isbn>%s</Isbn>\n", controlNacp.Isbn);
        strcat(nacpXml, tmp);
    } else {
        strcat(nacpXml, "  <Isbn />\n");
    }
    
    sprintf(tmp, "  <StartupUserAccount>%s</StartupUserAccount>\n", getNacpStartupUserAccount(controlNacp.StartupUserAccount));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <UserAccountSwitchLock>%s</UserAccountSwitchLock>\n", getNacpUserAccountSwitchLock(controlNacp.UserAccountSwitchLock));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <ParentalControl>%s</ParentalControl>\n", getNacpParentalControlFlag(controlNacp.ParentalControlFlag));
    strcat(nacpXml, tmp);
    
    for(i = 0; i < 16; i++)
    {
        u8 bit = (u8)((controlNacp.SupportedLanguageFlag >> i) & 0x1);
        if (bit)
        {
            sprintf(tmp, "  <SupportedLanguage>%s</SupportedLanguage>\n", getNacpLangName(i));
            strcat(nacpXml, tmp);
            nacpIconCnt++;
        }
    }
    
    sprintf(tmp, "  <Screenshot>%s</Screenshot>\n", getNacpScreenshot(controlNacp.Screenshot));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <VideoCapture>%s</VideoCapture>\n", getNacpVideoCapture(controlNacp.VideoCapture));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <PresenceGroupId>0x%016lx</PresenceGroupId>\n", controlNacp.PresenceGroupId);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <DisplayVersion>%s</DisplayVersion>\n", controlNacp.DisplayVersion);
    strcat(nacpXml, tmp);
    
    for(i = 0; i < 32; i++)
    {
        if (controlNacp.RatingAge[i] != 0xFF)
        {
            sprintf(tmp, "  <Rating>\n" \
                         "    <Organization>%s</Organization>\n" \
                         "    <Age>%u</Age>\n" \
                         "  </Rating>\n", \
                         getNacpRatingAgeOrganizationName(i), \
                         controlNacp.RatingAge[i]);
            
            strcat(nacpXml, tmp);
        }
    }
    
    sprintf(tmp, "  <DataLossConfirmation>%s</DataLossConfirmation>\n", getNacpDataLossConfirmation(controlNacp.DataLossConfirmation));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <PlayLogPolicy>%s</PlayLogPolicy>\n", getNacpPlayLogPolicy(controlNacp.PlayLogPolicy));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <SaveDataOwnerId>0x%016lx</SaveDataOwnerId>\n", controlNacp.SaveDataOwnerId);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <UserAccountSaveDataSize>0x%016lx</UserAccountSaveDataSize>\n", controlNacp.UserAccountSaveDataSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <UserAccountSaveDataJournalSize>0x%016lx</UserAccountSaveDataJournalSize>\n", controlNacp.UserAccountSaveDataJournalSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <DeviceSaveDataSize>0x%016lx</DeviceSaveDataSize>\n", controlNacp.DeviceSaveDataSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <DeviceSaveDataJournalSize>0x%016lx</DeviceSaveDataJournalSize>\n", controlNacp.DeviceSaveDataJournalSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <BcatDeliveryCacheStorageSize>0x%016lx</BcatDeliveryCacheStorageSize>\n", controlNacp.BcatDeliveryCacheStorageSize);
    strcat(nacpXml, tmp);
    
    if (strlen(controlNacp.ApplicationErrorCodeCategory))
    {
        sprintf(tmp, "  <ApplicationErrorCodeCategory>%s</ApplicationErrorCodeCategory>\n", controlNacp.ApplicationErrorCodeCategory);
        strcat(nacpXml, tmp);
    } else {
        strcat(nacpXml, "  <ApplicationErrorCodeCategory />\n");
    }
    
    sprintf(tmp, "  <AddOnContentBaseId>0x%016lx</AddOnContentBaseId>\n", controlNacp.AddOnContentBaseId);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <LogoType>%s</LogoType>\n", getNacpLogoType(controlNacp.LogoType));
    strcat(nacpXml, tmp);
    
    for(i = 0; i < 8; i++)
    {
        if (controlNacp.LocalCommunicationId[i] != 0)
        {
            sprintf(tmp, "  <LocalCommunicationId>0x%016lx</LocalCommunicationId>\n", controlNacp.LocalCommunicationId[i]);
            strcat(nacpXml, tmp);
        }
    }
    
    sprintf(tmp, "  <LogoHandling>%s</LogoHandling>\n", getNacpLogoHandling(controlNacp.LogoHandling));
    strcat(nacpXml, tmp);
    
    if (nacpIconCnt)
    {
        nacpIcons = calloc(nacpIconCnt, sizeof(nacp_icons_ctx));
        if (!nacpIcons)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the NACP icons!");
            goto out;
        }
        
        for(i = 0; i < 16; i++)
        {
            u8 bit = (u8)((controlNacp.SupportedLanguageFlag >> i) & 0x1);
            if (bit)
            {
                // Retrieve the icon file for this language and calculate its SHA-256 checksum
                found_icon = false;
                
                memset(languageIconHash, 0, 0x20);
                memset(languageIconHashStr, 0, 0x21);
                
                entryOffset = 0;
                sprintf(tmp, "icon_%s.dat", getNacpLangName(i));
                
                while(entryOffset < romFsContext.romfs_filetable_size)
                {
                    entry = (romfs_file*)((u8*)romFsContext.romfs_file_entries + entryOffset);
                    
                    if (entry->parent == 0 && entry->nameLen == strlen(tmp) && !strncasecmp((char*)entry->name, tmp, strlen(tmp)) && entry->dataSize <= 0x20000)
                    {
                        found_icon = true;
                        break;
                    }
                    
                    entryOffset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
                }
                
                if (!found_icon)
                {
                    nacpIconCnt--;
                    continue;
                }
                
                strcat(nacpXml, "  <Icon>\n");
                
                sprintf(tmp, "    <Language>%s</Language>\n", getNacpLangName(i));
                strcat(nacpXml, tmp);
                
                // Fill details for our NACP icon context
                sprintf(nacpIcons[j].filename, "%s.nx.%s.jpg", ncaIdStr, getNacpLangName(i)); // Temporary, the NCA ID is subject to change
                nacpIcons[j].icon_size = entry->dataSize;
                
                if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &(romFsContext.aes_ctx), romFsContext.romfs_filedata_offset + entry->dataOff, nacpIcons[j].icon_data, nacpIcons[j].icon_size, false))
                {
                    breaks++;
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read \"%s\" from RomFS section in Control NCA!", tmp);
                    goto out;
                }
                
                sha256CalculateHash(languageIconHash, nacpIcons[j].icon_data, nacpIcons[j].icon_size);
                
                // Only retrieve the first half from the SHA-256 checksum
                convertDataToHexString(languageIconHash, 0x10, languageIconHashStr, 0x21);
                
                // Now print the hash
                sprintf(tmp, "    <NxIconHash>%s</NxIconHash>\n", languageIconHashStr);
                strcat(nacpXml, tmp);
                
                strcat(nacpXml, "  </Icon>\n");
                
                j++;
            }
        }
    }
    
    sprintf(tmp, "  <SeedForPseudoDeviceId>0x%016lx</SeedForPseudoDeviceId>\n", controlNacp.SeedForPseudoDeviceId);
    strcat(nacpXml, tmp);
    
    if (strlen(controlNacp.BcatPassphrase))
    {
        sprintf(tmp, "  <BcatPassphrase>%s</BcatPassphrase>\n", controlNacp.BcatPassphrase);
        strcat(nacpXml, tmp);
    } else {
        strcat(nacpXml, "  <BcatPassphrase />\n");
    }
    
    sprintf(tmp, "  <StartupUserAccountOption>%s</StartupUserAccountOption>\n", getNacpStartupUserAccountOptionFlag(controlNacp.StartupUserAccountOptionFlag));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <AddOnContentRegistrationType>%s</AddOnContentRegistrationType>\n", getNacpAddOnContentRegistrationType(controlNacp.AddOnContentRegistrationType));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <UserAccountSaveDataSizeMax>0x%016lx</UserAccountSaveDataSizeMax>\n", controlNacp.UserAccountSaveDataSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <UserAccountSaveDataJournalSizeMax>0x%016lx</UserAccountSaveDataJournalSizeMax>\n", controlNacp.UserAccountSaveDataJournalSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <DeviceSaveDataSizeMax>0x%016lx</DeviceSaveDataSizeMax>\n", controlNacp.DeviceSaveDataSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <DeviceSaveDataJournalSizeMax>0x%016lx</DeviceSaveDataJournalSizeMax>\n", controlNacp.DeviceSaveDataJournalSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <TemporaryStorageSize>0x%016lx</TemporaryStorageSize>\n", controlNacp.TemporaryStorageSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <CacheStorageSize>0x%016lx</CacheStorageSize>\n", controlNacp.CacheStorageSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <CacheStorageJournalSize>0x%016lx</CacheStorageJournalSize>\n", controlNacp.CacheStorageJournalSize);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <CacheStorageDataAndJournalSizeMax>0x%016lx</CacheStorageDataAndJournalSizeMax>\n", controlNacp.CacheStorageDataAndJournalSizeMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <CacheStorageIndexMax>0x%04x</CacheStorageIndexMax>\n", controlNacp.CacheStorageIndexMax);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <Hdcp>%s</Hdcp>\n", getNacpHdcp(controlNacp.Hdcp));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <CrashReport>%s</CrashReport>\n", getNacpCrashReport(controlNacp.CrashReport));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <RuntimeAddOnContentInstall>%s</RuntimeAddOnContentInstall>\n", getNacpRuntimeAddOnContentInstall(controlNacp.RuntimeAddOnContentInstall));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <RuntimeParameterDelivery>%s</RuntimeParameterDelivery>\n", getNacpRuntimeParameterDelivery(controlNacp.RuntimeParameterDelivery));
    strcat(nacpXml, tmp);
    
    for(i = 0; i < 16; i++)
    {
        if (controlNacp.PlayLogQueryableApplicationId[i] != 0)
        {
            sprintf(tmp, "  <PlayLogQueryableApplicationId>0x%016lx</PlayLogQueryableApplicationId>\n", controlNacp.PlayLogQueryableApplicationId[i]);
            strcat(nacpXml, tmp);
        }
    }
    
    sprintf(tmp, "  <PlayLogQueryCapability>%s</PlayLogQueryCapability>\n", getNacpPlayLogQueryCapability(controlNacp.PlayLogQueryCapability));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <Repair>%s</Repair>\n", getNacpRepairFlag(controlNacp.RepairFlag));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <Attribute>%s</Attribute>\n", getNacpAttributeFlag(controlNacp.AttributeFlag));
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <ProgramIndex>%u</ProgramIndex>\n", controlNacp.ProgramIndex);
    strcat(nacpXml, tmp);
    
    sprintf(tmp, "  <RequiredNetworkServiceLicenseOnLaunch>%s</RequiredNetworkServiceLicenseOnLaunch>\n", getNacpRequiredNetworkServiceLicenseOnLaunchFlag(controlNacp.RequiredNetworkServiceLicenseOnLaunchFlag));
    strcat(nacpXml, tmp);
    
    // Check if we actually have valid NeighborDetectionClientConfiguration values
    availableSDC = (controlNacp.SendDataConfiguration.id != 0 && memcmp(controlNacp.SendDataConfiguration.key, null_key, 0x10) != 0);
    
    for(i = 0; i < 16; i++)
    {
        if (controlNacp.ReceivableDataConfiguration[i].id != 0 && memcmp(controlNacp.ReceivableDataConfiguration[i].key, null_key, 0x10) != 0)
        {
            availableRDC = true;
            break;
        }
    }
    
    if (availableSDC || availableRDC)
    {
        strcat(nacpXml, "  <NeighborDetectionClientConfiguration>\n");
        
        if (availableSDC)
        {
            convertDataToHexString(controlNacp.SendDataConfiguration.key, 0x10, dataStr, 100);
            
            sprintf(tmp, "    <SendDataConfiguration>\n" \
                         "      <DataId>0x%016lx</DataId>\n" \
                         "      <Key>%s</Key>\n" \
                         "    </SendDataConfiguration>\n", \
                         controlNacp.SendDataConfiguration.id, \
                         dataStr);
            
            strcat(nacpXml, tmp);
        }
        
        if (availableRDC)
        {
            for(i = 0; i < 16; i++)
            {
                if (controlNacp.ReceivableDataConfiguration[i].id != 0 && memcmp(controlNacp.ReceivableDataConfiguration[i].key, null_key, 0x10) != 0)
                {
                    convertDataToHexString(controlNacp.ReceivableDataConfiguration[i].key, 0x10, dataStr, 100);
                    
                    sprintf(tmp, "    <ReceivableDataConfiguration>\n" \
                                 "      <DataId>0x%016lx</DataId>\n" \
                                 "      <Key>%s</Key>\n" \
                                 "    </ReceivableDataConfiguration>\n", \
                                 controlNacp.ReceivableDataConfiguration[i].id, \
                                 dataStr);
                    
                    strcat(nacpXml, tmp);
                }
            }
        }
        
        strcat(nacpXml, "  </NeighborDetectionClientConfiguration>\n");
    }
    
    sprintf(tmp, "  <JitConfiguration>\n" \
                 "    <IsEnabled>%s</IsEnabled>\n" \
                 "    <MemorySize>0x%016lx</MemorySize>\n" \
                 "  </JitConfiguration>\n", \
                 getNacpJitConfigurationFlag(controlNacp.JitConfigurationFlag), \
                 controlNacp.JitMemorySize);
    
    strcat(nacpXml, tmp);
    
    strcat(nacpXml, "</Application>");
    
    *out_nacp_xml = nacpXml;
    *out_nacp_xml_size = strlen(nacpXml);
    
    if (nacpIconCnt)
    {
        *out_nacp_icons_ctx = nacpIcons;
        *out_nacp_icons_ctx_cnt = nacpIconCnt;
    }
    
    success = true;
    
out:
    if (!success)
    {
        if (nacpIcons != NULL) free(nacpIcons);
        
        if (nacpXml != NULL) free(nacpXml);
    }
    
    // Manually free these pointers
    // Calling freeRomFsContext() would also close the ncmStorage handle
    free(romFsContext.romfs_dir_entries);
    romFsContext.romfs_dir_entries = NULL;
    
    free(romFsContext.romfs_file_entries);
    romFsContext.romfs_file_entries = NULL;
    
    return success;
}

bool retrieveLegalInfoXmlFromNca(NcmContentStorage *ncmStorage, const NcmNcaId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys, char **outBuf, u64 *outBufSize)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys || !outBuf)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: invalid parameters to retrieve \"legalinfo.xml\"!");
        return false;
    }
    
    u64 entryOffset = 0;
    romfs_file *entry = NULL;
    bool found_legalinfo = false, success = false;
    
    u64 legalInfoXmlSize = 0;
    char *legalInfoXml = NULL;
    
    if (!readRomFsEntryFromNca(ncmStorage, ncaId, dec_nca_header, decrypted_nca_keys)) return false;
    
    // Look for the legalinfo.xml file
    while(entryOffset < romFsContext.romfs_filetable_size)
    {
        entry = (romfs_file*)((u8*)romFsContext.romfs_file_entries + entryOffset);
        
        if (entry->parent == 0 && entry->nameLen == 13 && !strncasecmp((char*)entry->name, "legalinfo.xml", 13))
        {
            found_legalinfo = true;
            break;
        }
        
        entryOffset += round_up(ROMFS_NONAME_FILEENTRY_SIZE + entry->nameLen, 4);
    }
    
    if (!found_legalinfo)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to find \"legalinfo.xml\" file in Manual NCA RomFS section!");
        goto out;
    }
    
    // Allocate memory for the legalinfo.xml contents
    legalInfoXmlSize = entry->dataSize;
    legalInfoXml = calloc(legalInfoXmlSize, sizeof(char));
    if (!legalInfoXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Error: unable to allocate memory for the \"legalinfo.xml\" contents!");
        goto out;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &(romFsContext.aes_ctx), romFsContext.romfs_filedata_offset + entry->dataOff, legalInfoXml, legalInfoXmlSize, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "Failed to read \"legalinfo.xml\" from RomFS section in Manual NCA!");
        goto out;
    }
    
    *outBuf = legalInfoXml;
    *outBufSize = legalInfoXmlSize;
    
    success = true;
    
out:
    if (!success && legalInfoXml != NULL) free(legalInfoXml);
    
    // Manually free these pointers
    // Calling freeRomFsContext() would also close the ncmStorage handle
    free(romFsContext.romfs_dir_entries);
    romFsContext.romfs_dir_entries = NULL;
    
    free(romFsContext.romfs_file_entries);
    romFsContext.romfs_file_entries = NULL;
    
    return success;
}
