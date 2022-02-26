#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <mbedtls/base64.h>

#include "keys.h"
#include "util.h"
#include "ui.h"
#include "rsa.h"

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
        case NcmContentMetaType_Application:
            out = "Application";
            break;
        case NcmContentMetaType_Patch:
            out = "Patch";
            break;
        case NcmContentMetaType_AddOnContent:
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
        case NcmContentType_Meta:
            out = "Meta";
            break;
        case NcmContentType_Program:
            out = "Program";
            break;
        case NcmContentType_Data:
            out = "Data";
            break;
        case NcmContentType_Control:
            out = "Control";
            break;
        case NcmContentType_HtmlDocument:
            out = "HtmlDocument";
            break;
        case NcmContentType_LegalInformation:
            out = "LegalInformation";
            break;
        case NcmContentType_DeltaFragment:
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
        case NcmContentMetaType_Application:
        case NcmContentMetaType_Patch:
            out = "RequiredSystemVersion";
            break;
        case NcmContentMetaType_AddOnContent:
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
        case NcmContentMetaType_Application:
            out = "PatchId";
            break;
        case NcmContentMetaType_Patch:
            out = "OriginalId";
            break;
        case NcmContentMetaType_AddOnContent:
            out = "ApplicationId";
            break;
        default:
            out = "Unknown";
            break;
    }
    
    return out;
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: please run the application with debug svc permissions!", __func__);
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

bool readNcaDataByContentId(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, u64 offset, void *outBuf, size_t bufSize)
{
    if (!ncmStorage || !ncaId || !outBuf || !bufSize)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to read data from NCA!", __func__);
        return false;
    }
    
    Result result = 0;
    bool success = false;
    
    char nca_id[SHA256_HASH_SIZE + 1] = {'\0'}, nca_path[0x301] = {'\0'};
    convertDataToHexString(ncaId->c, SHA256_HASH_SIZE / 2, nca_id, SHA256_HASH_SIZE + 1);
    
    result = ncmContentStorageGetPath(ncmStorage, nca_path, MAX_CHARACTERS(nca_path), ncaId);
    if (R_FAILED(result) || !strlen(nca_path))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to retrieve content path for NCA \"%s\"! (0x%08X)", __func__, nca_id, result);
        return false;
    }
    
    // Check if we're dealing with a gamecard NCA
    if (!strncmp(nca_path, "@Gc", 3))
    {
        // Retrieve NCA data using raw IStorage reads
        // Fixes NCA access problems with gamecards under low HOS versions when using ncmContentStorageReadContentIdFile()
        success = readFileFromSecureHfs0PartitionByName(strrchr(nca_path, '/') + 1, offset, outBuf, bufSize);
        if (!success) breaks++;
    } else {
        // Retrieve NCA data normally
        // This strips NAX0 encryption from SD card NCAs (not used with eMMC NCAs)
        result = ncmContentStorageReadContentIdFile(ncmStorage, outBuf, bufSize, ncaId, offset);
        success = R_SUCCEEDED(result);
    }
    
    if (!success) uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read %lu bytes block at offset 0x%016lX from NCA \"%s\"! (0x%08X)", __func__, bufSize, offset, nca_id, result);
    
    return success;
}

bool processNcaCtrSectionBlock(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, Aes128CtrContext *ctx, u64 offset, void *outBuf, size_t bufSize, bool encrypt)
{
    if (!ncmStorage || !ncaId || !outBuf || !bufSize || !ctx)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to process %s NCA section block!", __func__, (encrypt ? "decrypted" : "encrypted"));
        return false;
    }
    
    if (!loadNcaKeyset()) return false;
    
    unsigned char ctr[0x10];
    
    char nca_id[SHA256_HASH_SIZE + 1] = {'\0'};
    convertDataToHexString(ncaId->c, SHA256_HASH_SIZE / 2, nca_id, SHA256_HASH_SIZE + 1);
    
    u64 block_start_offset = (offset - (offset % 0x10));
    u64 block_end_offset = (u64)round_up(offset + bufSize, 0x10);
    u64 block_size = (block_end_offset - block_start_offset);
    
    u64 block_size_used = (block_size > NCA_CTR_BUFFER_SIZE ? NCA_CTR_BUFFER_SIZE : block_size);
    u64 output_block_size = (block_size > NCA_CTR_BUFFER_SIZE ? (NCA_CTR_BUFFER_SIZE - (offset - block_start_offset)) : bufSize);
    
    if (!readNcaDataByContentId(ncmStorage, ncaId, block_start_offset, ncaCtrBuf, block_size_used))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read encrypted data block from NCA \"%s\"!", __func__, nca_id);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: too big offset looked up in BKTR relocation table!", __func__);
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
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to find offset 0x%016lX in BKTR relocation table!", __func__, offset);
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
    
    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to find offset 0x%016lX in BKTR subsection table!", __func__, offset);
    return NULL;
}

bool bktrSectionSeek(u64 offset)
{
    if (!bktrContext.section_offset || !bktrContext.section_size || !bktrContext.relocation_block || !bktrContext.subsection_block)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to seek within NCA BKTR section!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to perform physical block read from NCA BKTR section!", __func__);
        return false;
    }
    
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
            
            if (!readNcaDataByContentId(&(bktrContext.ncmStorage), &(bktrContext.ncaId), block_start_offset, ncaCtrBuf, block_size_used))
            {
                breaks++;
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read encrypted %lu bytes block at offset 0x%016lX!", __func__, block_size_used, block_start_offset);
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
    if (!bktrContext.section_offset || !bktrContext.section_size || !bktrContext.relocation_block || !bktrContext.subsection_block || (bktrContext.use_base_romfs && (!romFsContext.section_offset || !romFsContext.section_size)) || !outBuf || !bufSize)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to read block from NCA BKTR section!", __func__);
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
            if (!bktrContext.use_base_romfs)
            {
                breaks++;
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: BKTR references non-existent base RomFS section!", __func__);
                return false;
            }
            
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
    if (!input || !outBuf || !outBufSize || outBufSize < NCA_FULL_HEADER_LENGTH || (__builtin_bswap32(input->magic) != NCA3_MAGIC && __builtin_bswap32(input->magic) != NCA2_MAGIC))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA header encryption parameters.", __func__);
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
    
    if (__builtin_bswap32(input->magic) == NCA3_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf, input, NCA_FULL_HEADER_LENGTH, 0, true);
        if (crypt_res != NCA_FULL_HEADER_LENGTH)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid output length for encrypted NCA header! (%u != %lu)", __func__, NCA_FULL_HEADER_LENGTH, crypt_res);
            return false;
        }
    } else
    if (__builtin_bswap32(input->magic) == NCA2_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf, input, NCA_HEADER_LENGTH, 0, true);
        if (crypt_res != NCA_HEADER_LENGTH)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid output length for encrypted NCA header! (%u != %lu)", __func__, NCA_HEADER_LENGTH, crypt_res);
            return false;
        }
        
        for(i = 0; i < NCA_SECTION_HEADER_CNT; i++)
        {
            crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, outBuf + NCA_HEADER_LENGTH + (i * NCA_SECTION_HEADER_LENGTH), &(input->fs_headers[i]), NCA_SECTION_HEADER_LENGTH, 0, true);
            if (crypt_res != NCA_SECTION_HEADER_LENGTH)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid output length for encrypted NCA header section #%u! (%u != %lu)", __func__, i, NCA_SECTION_HEADER_LENGTH, crypt_res);
                return false;
            }
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid decrypted NCA magic word! (0x%08X)", __func__, __builtin_bswap32(input->magic));
        return false;
    }
    
    return true;
}

bool decryptNcaHeader(const u8 *ncaBuf, u64 ncaBufSize, nca_header_t *out, title_rights_ctx *rights_info, u8 *decrypted_nca_keys, bool retrieveTitleKeyData)
{
    if (!ncaBuf || !ncaBufSize || ncaBufSize < NCA_FULL_HEADER_LENGTH || !out || !decrypted_nca_keys)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA header decryption parameters!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid output length for decrypted NCA header! (%u != %lu)", __func__, NCA_HEADER_LENGTH, crypt_res);
        return false;
    }
    
    if (__builtin_bswap32(out->magic) == NCA3_MAGIC)
    {
        crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, out, ncaBuf, NCA_FULL_HEADER_LENGTH, 0, false);
        if (crypt_res != NCA_FULL_HEADER_LENGTH)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid output length for decrypted NCA header! (%u != %lu)", __func__, NCA_FULL_HEADER_LENGTH, crypt_res);
            return false;
        }
    } else
    if (__builtin_bswap32(out->magic) == NCA2_MAGIC)
    {
        for(i = 0; i < NCA_SECTION_HEADER_CNT; i++)
        {
            if (out->fs_headers[i]._0x148[0] != 0 || memcmp(out->fs_headers[i]._0x148, out->fs_headers[i]._0x148 + 1, 0xB7))
            {
                crypt_res = aes128XtsNintendoCrypt(&hdr_aes_ctx, &(out->fs_headers[i]), ncaBuf + NCA_HEADER_LENGTH + (i * NCA_SECTION_HEADER_LENGTH), NCA_SECTION_HEADER_LENGTH, 0, false);
                if (crypt_res != NCA_SECTION_HEADER_LENGTH)
                {
                    uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid output length for decrypted NCA header section #%u! (%u != %lu)", __func__, i, NCA_SECTION_HEADER_LENGTH, crypt_res);
                    return false;
                }
            } else {
                memset(&(out->fs_headers[i]), 0, sizeof(nca_fs_header_t));
            }
        }
    } else {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA magic word! Wrong header key? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__, __builtin_bswap32(out->magic));
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

bool retrieveTitleKeyFromGameCardTicket(title_rights_ctx *rights_info, u8 *decrypted_nca_keys)
{
    if (!rights_info || !rights_info->has_rights_id || !strlen(rights_info->tik_filename) || !decrypted_nca_keys)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve titlekey from gamecard ticket!", __func__);
        return false;
    }
    
    // Check if the ticket has already been retrieved from the HFS0 partition in the gamecard
    if (rights_info->retrieved_tik)
    {
        // Save the decrypted NCA key area keys
        memset(decrypted_nca_keys, 0, NCA_KEY_AREA_SIZE);
        memcpy(decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), rights_info->dec_titlekey, 0x10);
        return true;
    }
    
    // Load external keys
    if (!loadExternalKeys()) return false;
    
    // Retrieve ticket
    if (!readFileFromSecureHfs0PartitionByName(rights_info->tik_filename, 0, &(rights_info->tik_data), ETICKET_TIK_FILE_SIZE)) return false;
    
    // Save encrypted titlekey
    memcpy(rights_info->enc_titlekey, rights_info->tik_data.titlekey_block, 0x10);
    
    // Decrypt titlekey
    u8 crypto_type = rights_info->rights_id[0x0F];
    if (crypto_type) crypto_type--;
    
    if (crypto_type >= 0x20)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA keyblob index.", __func__);
        return false;
    }
    
    Aes128Context titlekey_aes_ctx;
    aes128ContextCreate(&titlekey_aes_ctx, nca_keyset.titlekeks[crypto_type], false);
    aes128DecryptBlock(&titlekey_aes_ctx, rights_info->dec_titlekey, rights_info->enc_titlekey);
    
    // Update retrieved ticket flag
    rights_info->retrieved_tik = true;
    
    // Save the decrypted NCA key area keys
    memset(decrypted_nca_keys, 0, NCA_KEY_AREA_SIZE);
    memcpy(decrypted_nca_keys + (NCA_KEY_AREA_KEY_SIZE * 2), rights_info->dec_titlekey, 0x10);
    
    return true;
}

bool processProgramNca(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, nca_header_t *dec_nca_header, cnmt_xml_content_info *xml_content_info, nca_program_mod_data **output, u32 *cur_mod_cnt, u32 idx)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !xml_content_info || !output || !cur_mod_cnt)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to process Program NCA!", __func__);
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_nca_header->fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: Program NCA section #0 doesn't hold a PFS0 partition!", __func__);
        return false;
    }
    
    if (!dec_nca_header->fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid size for PFS0 partition in Program NCA section #0!", __func__);
        return false;
    }
    
    if (dec_nca_header->fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid AES crypt type for Program NCA section #0! (0x%02X)", __func__, dec_nca_header->fs_headers[0].crypt_type);
        return false;
    }
    
    u32 i;
    
    u64 section_offset;
    u64 hash_table_offset;
    u64 nca_pfs0_offset;
    
    pfs0_header nca_pfs0_header;
    pfs0_file_entry *nca_pfs0_entries = NULL;
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid offsets for Program NCA section #0!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read Program NCA section #0 PFS0 partition header!", __func__);
        return false;
    }
    
    if (__builtin_bswap32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid magic word for Program NCA section #0 PFS0 partition! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__, __builtin_bswap32(nca_pfs0_header.magic));
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: Program NCA section #0 PFS0 partition is empty! Wrong KAEK?\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__);
        return false;
    }
    
    nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_file_entry));
    if (!nca_pfs0_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for Program NCA section #0 PFS0 partition entries!", __func__);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_offset + sizeof(pfs0_header), nca_pfs0_entries, (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_file_entry), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read Program NCA section #0 PFS0 partition entries!", __func__);
        free(nca_pfs0_entries);
        return false;
    }
    
    nca_pfs0_data_offset = (nca_pfs0_offset + sizeof(pfs0_header) + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_file_entry)) + (u64)nca_pfs0_header.str_table_size);
    
    // Looking for META magic
    for(i = 0; i < nca_pfs0_header.file_cnt; i++)
    {
        u64 nca_pfs0_cur_file_offset = (nca_pfs0_data_offset + nca_pfs0_entries[i].file_offset);
        
        // Read and decrypt NPDM header
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_cur_file_offset, &npdm_header, sizeof(npdm_t), false))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read Program NCA section #0 PFS0 entry #%u!", __func__, i);
            free(nca_pfs0_entries);
            return false;
        }
        
        if (__builtin_bswap32(npdm_header.magic) == META_MAGIC)
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find NPDM entry in Program NCA section #0 PFS0 partition!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for Program NCA section #0 PFS0 NPDM block 0!", __func__);
        return false;
    }
    
    // Read and decrypt block
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, block_start_offset[0], block_data[0], block_size[0], false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read Program NCA section #0 PFS0 NPDM block 0!", __func__);
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for Program NCA section #0 PFS0 NPDM block 1!", __func__);
            free(block_data[0]);
            return false;
        }
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, block_start_offset[1], block_data[1], block_size[1], false))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read Program NCA section #0 PFS0 NPDM block 1!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for Program NCA section #0 PFS0 hash table!", __func__);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        return false;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, hash_table_offset, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read Program NCA section #0 PFS0 hash table!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to recreate Program NCA NPDM signature!", __func__);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    // Reencrypt relevant data blocks
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, block_start_offset[0], block_data[0], block_size[0], true))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to encrypt Program NCA section #0 PFS0 NPDM block 0!", __func__);
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
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to encrypt Program NCA section #0 PFS0 NPDM block 1!", __func__);
            free(block_data[0]);
            free(block_data[1]);
            free(hash_table);
            return false;
        }
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, hash_table_offset, hash_table, dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size, true))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to encrypt Program NCA section #0 PFS0 hash table!", __func__);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    // Save data to the output struct so we can write it later
    // The caller function must free these data pointers
    nca_program_mod_data *tmp_mod_data = realloc(*output, (*cur_mod_cnt + 1) * sizeof(nca_program_mod_data));
    if (!tmp_mod_data)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to reallocate Program NCA mod data buffer!", __func__);
        free(block_data[0]);
        if (block_data[1]) free(block_data[1]);
        free(hash_table);
        return false;
    }
    
    memset(&(tmp_mod_data[*cur_mod_cnt]), 0, sizeof(nca_program_mod_data));
    
    tmp_mod_data[*cur_mod_cnt].nca_index = idx;
    
    tmp_mod_data[*cur_mod_cnt].hash_table = hash_table;
    tmp_mod_data[*cur_mod_cnt].hash_table_offset = hash_table_offset;
    tmp_mod_data[*cur_mod_cnt].hash_table_size = dec_nca_header->fs_headers[0].pfs0_superblock.hash_table_size;
    
    tmp_mod_data[*cur_mod_cnt].block_mod_cnt = (block_hash_table_offset != block_hash_table_end_offset ? 2 : 1);
    
    tmp_mod_data[*cur_mod_cnt].block_data[0] = block_data[0];
    tmp_mod_data[*cur_mod_cnt].block_offset[0] = block_start_offset[0];
    tmp_mod_data[*cur_mod_cnt].block_size[0] = block_size[0];
    
    if (block_hash_table_offset != block_hash_table_end_offset)
    {
        tmp_mod_data[*cur_mod_cnt].block_data[1] = block_data[1];
        tmp_mod_data[*cur_mod_cnt].block_offset[1] = block_start_offset[1];
        tmp_mod_data[*cur_mod_cnt].block_size[1] = block_size[1];
    }
    
    *output = tmp_mod_data;
    tmp_mod_data = NULL;
    
    *cur_mod_cnt += 1;
    
    return true;
}

bool retrieveCnmtNcaData(NcmStorageId curStorageId, u8 *ncaBuf, cnmt_xml_program_info *xml_program_info, cnmt_xml_content_info *xml_content_info, u32 cnmtNcaIndex, nca_cnmt_mod_data *output, title_rights_ctx *rights_info)
{
    if (!ncaBuf || !xml_program_info || !xml_content_info || !output || !rights_info)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve CNMT NCA!", __func__);
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
    pfs0_file_entry *nca_pfs0_entries = NULL;
    
    bool found_cnmt = false;
    
    u64 title_cnmt_offset;
    u64 title_cnmt_size;
    
    cnmt_header title_cnmt_header;
    cnmt_extended_header title_cnmt_extended_header;
    
    u64 digest_offset;
    
    // Generate filename for our required CNMT file
    char cnmtFileName[50] = {'\0'};
    snprintf(cnmtFileName, MAX_CHARACTERS(cnmtFileName), "%s_%016lx.cnmt", getTitleType(xml_program_info->type), xml_program_info->title_id);
    
    // Decrypt the NCA header
    // Don't retrieve the ticket and/or titlekey if we're dealing with a Patch with titlekey crypto bundled with the inserted gamecard
    if (!decryptNcaHeader(ncaBuf, xml_content_info[cnmtNcaIndex].size, &dec_header, rights_info, xml_content_info[cnmtNcaIndex].decrypted_nca_keys, (curStorageId != NcmStorageId_GameCard))) return false;
    
    if (dec_header.fs_headers[0].partition_type != NCA_FS_HEADER_PARTITION_PFS0 || dec_header.fs_headers[0].fs_type != NCA_FS_HEADER_FSTYPE_PFS0)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: CNMT NCA section #0 doesn't hold a PFS0 partition!", __func__);
        return false;
    }
    
    if (!dec_header.fs_headers[0].pfs0_superblock.pfs0_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid size for PFS0 partition in CNMT NCA section #0!", __func__);
        return false;
    }
    
    if (dec_header.fs_headers[0].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid AES crypt type for CNMT NCA section #0! (0x%02X)", __func__, dec_header.fs_headers[0].crypt_type);
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
    
    // CNMT NCAs never use titlekey crypto
    if (has_rights_id)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: Rights ID field in CNMT NCA header not empty!", __func__);
        return false;
    }
    
    // Modify distribution type
    if (curStorageId == NcmStorageId_GameCard) dec_header.distribution = 0;
    
    section_offset = ((u64)dec_header.section_entries[0].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    section_size = (((u64)dec_header.section_entries[0].media_end_offset * (u64)MEDIA_UNIT_SIZE) - section_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid offset/size for CNMT NCA section #0!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for the decrypted CNMT NCA section #0!", __func__);
        return false;
    }
    
    aes128CtrCrypt(&aes_ctx, section_data, ncaBuf + section_offset, section_size);
    
    nca_pfs0_offset = dec_header.fs_headers[0].pfs0_superblock.pfs0_offset;
    memcpy(&nca_pfs0_header, section_data + nca_pfs0_offset, sizeof(pfs0_header));
    
    if (__builtin_bswap32(nca_pfs0_header.magic) != PFS0_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid magic word for CNMT NCA section #0 PFS0 partition! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__, __builtin_bswap32(nca_pfs0_header.magic));
        free(section_data);
        return false;
    }
    
    if (!nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: CNMT NCA section #0 PFS0 partition is empty! Wrong KAEK?\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__);
        free(section_data);
        return false;
    }
    
    nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_file_entry));
    if (!nca_pfs0_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for CNMT NCA section #0 PFS0 partition entries!", __func__);
        free(section_data);
        return false;
    }
    
    memcpy(nca_pfs0_entries, section_data + nca_pfs0_offset + sizeof(pfs0_header), (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_file_entry));
    
    nca_pfs0_str_table_offset = (nca_pfs0_offset + sizeof(pfs0_header) + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_file_entry)));
    nca_pfs0_data_offset = (nca_pfs0_str_table_offset + (u64)nca_pfs0_header.str_table_size);
    
    // Look for the CNMT
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to find file \"%s\" in PFS0 partition from CNMT NCA section #0!", __func__, cnmtFileName);
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
    xml_program_info->min_appver = title_cnmt_extended_header.min_appver;
    
    // Retrieve the ID offset and content record offset for each of our NCAs (except the CNMT NCA)
    // Also wipe each of the content records we're gonna replace
    for(i = 0; i < (xml_program_info->nca_cnt - 1); i++) // Discard CNMT NCA
    {
        for(j = 0; j < title_cnmt_header.content_cnt; j++)
        {
            cnmt_content_record cnt_record;
            memcpy(&cnt_record, section_data + title_cnmt_offset + sizeof(cnmt_header) + (u64)title_cnmt_header.extended_header_size + (j * sizeof(cnmt_content_record)), sizeof(cnmt_content_record));
            
            if (memcmp(xml_content_info[i].nca_id, cnt_record.nca_id, SHA256_HASH_SIZE / 2) != 0) continue;
            
            // Save content record offset
            xml_content_info[i].cnt_record_offset = (j * sizeof(cnmt_content_record));
            
            // Empty CNMT content record
            memset(section_data + title_cnmt_offset + sizeof(cnmt_header) + (u64)title_cnmt_header.extended_header_size + (j * sizeof(cnmt_content_record)), 0, sizeof(cnmt_content_record));
            
            // Increase counter
            k++;
            
            break;
        }
    }
    
    // Verify counter
    if (k != (xml_program_info->nca_cnt - 1))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid content record entries in the CNMT NCA!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to patch CNMT NCA!", __func__);
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to encrypt modified CNMT NCA header!", __func__);
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

bool parseExeFsEntryFromNca(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to read RomFS section from NCA!", __func__);
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
    pfs0_file_entry *nca_pfs0_entries = NULL;
    
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
        
        if (__builtin_bswap32(nca_pfs0_header.magic) != PFS0_MAGIC || !nca_pfs0_header.file_cnt || !nca_pfs0_header.str_table_size) continue;
        
        nca_pfs0_entries_offset = (nca_pfs0_offset + sizeof(pfs0_header));
        
        nca_pfs0_entries = calloc(nca_pfs0_header.file_cnt, sizeof(pfs0_file_entry));
        if (!nca_pfs0_entries) continue;
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, nca_pfs0_entries_offset, nca_pfs0_entries, (u64)nca_pfs0_header.file_cnt * sizeof(pfs0_file_entry), false))
        {
            free(nca_pfs0_entries);
            return false;
        }
        
        nca_pfs0_str_table_offset = (nca_pfs0_entries_offset + ((u64)nca_pfs0_header.file_cnt * sizeof(pfs0_file_entry)));
        
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: NCA doesn't hold an ExeFS section! Wrong keys?\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__);
        return false;
    }
    
    nca_pfs0_data_offset = (nca_pfs0_str_table_offset + (u64)nca_pfs0_header.str_table_size);
    
    // Save data to output struct
    // The caller function must free these data pointers
    memcpy(&(exeFsContext.ncmStorage), ncmStorage, sizeof(NcmContentStorage));
    memcpy(&(exeFsContext.ncaId), ncaId, sizeof(NcmContentId));
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

int parseRomFsEntryFromNca(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to read RomFS section from NCA!", __func__);
        return -1;
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: NCA doesn't hold a RomFS section!", __func__);
        return -2;
    }
    
    section_offset = ((u64)dec_nca_header->section_entries[romfs_index].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    section_size = (((u64)dec_nca_header->section_entries[romfs_index].media_end_offset * (u64)MEDIA_UNIT_SIZE) - section_offset);
    
    if (!section_offset || section_offset < NCA_FULL_HEADER_LENGTH || !section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid offset/size for NCA RomFS section! (#%u)", __func__, romfs_index);
        return -1;
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
    
    if (__builtin_bswap32(dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.magic) != IVFC_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid IVFC magic word for NCA RomFS section! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__, __builtin_bswap32(dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.magic));
        return -1;
    }
    
    if (dec_nca_header->fs_headers[romfs_index].crypt_type != NCA_FS_HEADER_CRYPT_CTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid AES crypt type for NCA RomFS section! (0x%02X)", __func__, dec_nca_header->fs_headers[romfs_index].crypt_type);
        return -1;
    }
    
    romfs_offset = (section_offset + dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].logical_offset);
    romfs_size = dec_nca_header->fs_headers[romfs_index].romfs_superblock.ivfc_header.level_headers[IVFC_MAX_LEVEL - 1].hash_data_size;
    
    if (romfs_offset < section_offset || romfs_offset >= (section_offset + section_size) || romfs_size < ROMFS_HEADER_SIZE || (romfs_offset + romfs_size) > (section_offset + section_size))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid offset/size for NCA RomFS section!", __func__);
        return -1;
    }
    
    // First read the RomFS header
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, romfs_offset, &romFsHeader, sizeof(romfs_header), false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA RomFS section header!", __func__);
        return -1;
    }
    
    if (romFsHeader.headerSize != ROMFS_HEADER_SIZE)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid header size for NCA RomFS section! (0x%016lX at 0x%016lX)", __func__, romFsHeader.headerSize, romfs_offset);
        return -1;
    }
    
    romfs_dirtable_offset = (romfs_offset + romFsHeader.dirTableOff);
    romfs_dirtable_size = romFsHeader.dirTableSize;
    
    romfs_filetable_offset = (romfs_offset + romFsHeader.fileTableOff);
    romfs_filetable_size = romFsHeader.fileTableSize;
    
    if (romfs_dirtable_offset >= (section_offset + section_size) || !romfs_dirtable_size || (romfs_dirtable_offset + romfs_dirtable_size) > (section_offset + section_size) || romfs_filetable_offset >= (section_offset + section_size) || !romfs_filetable_size || (romfs_filetable_offset + romfs_filetable_size) > (section_offset + section_size))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid directory/file table for NCA RomFS section!", __func__);
        return -1;
    }
    
    romfs_filedata_offset = (romfs_offset + romFsHeader.fileDataOff);
    
    if (romfs_filedata_offset >= (section_offset + section_size))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid file data block offset for NCA RomFS section!", __func__);
        return -1;
    }
    
    romfs_dir_entries = calloc(1, romfs_dirtable_size);
    if (!romfs_dir_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA RomFS section directory entries!", __func__);
        return -1;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, romfs_dirtable_offset, romfs_dir_entries, romfs_dirtable_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA RomFS section directory entries!", __func__);
        free(romfs_dir_entries);
        return -1;
    }
    
    romfs_file_entries = calloc(1, romfs_filetable_size);
    if (!romfs_file_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA RomFS section file entries!", __func__);
        free(romfs_dir_entries);
        return -1;
    }
    
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &aes_ctx, romfs_filetable_offset, romfs_file_entries, romfs_filetable_size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA RomFS section file entries!", __func__);
        free(romfs_file_entries);
        free(romfs_dir_entries);
        return -1;
    }
    
    // Save data to output struct
    // The caller function must free these data pointers
    memcpy(&(romFsContext.ncmStorage), ncmStorage, sizeof(NcmContentStorage));
    memcpy(&(romFsContext.ncaId), ncaId, sizeof(NcmContentId));
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
    
    return 0;
}

bool parseBktrEntryFromNca(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, nca_header_t *dec_nca_header, u8 *decrypted_nca_keys, bool use_base_romfs)
{
    if (!ncmStorage || !ncaId || !dec_nca_header || !decrypted_nca_keys || (bktrContext.use_base_romfs && (!romFsContext.section_offset || !romFsContext.section_size || !romFsContext.romfs_dir_entries || !romFsContext.romfs_file_entries)))
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to read BKTR section from NCA!", __func__);
        return false;
    }
    
    u32 i;
    
    u8 bktr_index;
    bool found_bktr = false, success = false;
    
    romfs_header romFsHeader;
    
    initBktrContext();
    bktrContext.use_base_romfs = use_base_romfs;
    
    memcpy(&(bktrContext.ncmStorage), ncmStorage, sizeof(NcmContentStorage));
    memcpy(&(bktrContext.ncaId), ncaId, sizeof(NcmContentId));
    
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
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: NCA doesn't hold a BKTR section!", __func__);
        return false;
    }
    
    bktrContext.section_offset = ((u64)dec_nca_header->section_entries[bktr_index].media_start_offset * (u64)MEDIA_UNIT_SIZE);
    bktrContext.section_size = (((u64)dec_nca_header->section_entries[bktr_index].media_end_offset * (u64)MEDIA_UNIT_SIZE) - bktrContext.section_offset);
    
    if (!bktrContext.section_offset || bktrContext.section_offset < NCA_FULL_HEADER_LENGTH || !bktrContext.section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid offset/size for NCA BKTR section! (#%u)", __func__, bktr_index);
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
    
    if (__builtin_bswap32(bktrContext.superblock.ivfc_header.magic) != IVFC_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid IVFC magic word for NCA BKTR section! Wrong KAEK? (0x%08X)\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__, __builtin_bswap32(bktrContext.superblock.ivfc_header.magic));
        return false;
    }
    
    if (dec_nca_header->fs_headers[bktr_index].crypt_type != NCA_FS_HEADER_CRYPT_BKTR)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid AES crypt type for NCA BKTR section! (0x%02X)", __func__, dec_nca_header->fs_headers[bktr_index].crypt_type);
        return false;
    }
    
    if (__builtin_bswap32(bktrContext.superblock.relocation_header.magic) != BKTR_MAGIC || __builtin_bswap32(bktrContext.superblock.subsection_header.magic) != BKTR_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid BKTR magic word for NCA BKTR relocation/subsection header! Wrong KAEK? (0x%02X | 0x%02X)\nTry running Lockpick_RCM to generate the keys file from scratch.", __func__, __builtin_bswap32(bktrContext.superblock.relocation_header.magic), __builtin_bswap32(bktrContext.superblock.subsection_header.magic));
        return false;
    }
    
    if ((bktrContext.superblock.relocation_header.offset + bktrContext.superblock.relocation_header.size) != bktrContext.superblock.subsection_header.offset || (bktrContext.superblock.subsection_header.offset + bktrContext.superblock.subsection_header.size) != bktrContext.section_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid layout for NCA BKTR section!", __func__);
        return false;
    }
    
    // Allocate space for an extra (fake) relocation entry, to simplify our logic
    bktrContext.relocation_block = calloc(1, bktrContext.superblock.relocation_header.size + ((0x3FF0 / sizeof(u64)) * sizeof(bktr_relocation_entry_t)));
    if (!bktrContext.relocation_block)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA BKTR relocation header!", __func__);
        return false;
    }
    
    // Allocate space for an extra (fake) subsection entry, to simplify our logic
    bktrContext.subsection_block = calloc(1, bktrContext.superblock.subsection_header.size + ((0x3FF0 / sizeof(u64)) * sizeof(bktr_subsection_entry_t)) + sizeof(bktr_subsection_entry_t));
    if (!bktrContext.subsection_block)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA BKTR subsection header!", __func__);
        goto out;
    }
    
    // Read the relocation header
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &(bktrContext.aes_ctx), bktrContext.section_offset + bktrContext.superblock.relocation_header.offset, bktrContext.relocation_block, bktrContext.superblock.relocation_header.size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA BKTR relocation header!", __func__);
        goto out;
    }
    
    // Read the subsection header
    if (!processNcaCtrSectionBlock(ncmStorage, ncaId, &(bktrContext.aes_ctx), bktrContext.section_offset + bktrContext.superblock.subsection_header.offset, bktrContext.subsection_block, bktrContext.superblock.subsection_header.size, false))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA BKTR subsection header!", __func__);
        goto out;
    }
    
    if (bktrContext.subsection_block->total_size != bktrContext.superblock.subsection_header.offset)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid NCA BKTR subsection header size!", __func__);
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
    
    // Do not check the RomFS offset/size, because it reflects the full patched RomFS image
    if (!bktrContext.romfs_offset || bktrContext.romfs_size < ROMFS_HEADER_SIZE)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid offset/size for NCA BKTR RomFS section!", __func__);
        goto out;
    }
    
    if (!readBktrSectionBlock(bktrContext.romfs_offset, &romFsHeader, sizeof(romfs_header)))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA BKTR RomFS section header!", __func__);
        goto out;
    }
    
    if (romFsHeader.headerSize != ROMFS_HEADER_SIZE)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid header size for NCA BKTR RomFS section! (0x%016lX at 0x%016lX)", __func__, romFsHeader.headerSize, bktrContext.section_offset + bktrContext.romfs_offset);
        goto out;
    }
    
    bktrContext.romfs_dirtable_offset = (bktrContext.romfs_offset + romFsHeader.dirTableOff);
    bktrContext.romfs_dirtable_size = romFsHeader.dirTableSize;
    
    bktrContext.romfs_filetable_offset = (bktrContext.romfs_offset + romFsHeader.fileTableOff);
    bktrContext.romfs_filetable_size = romFsHeader.fileTableSize;
    
    // Then again, do not check these offsets/sizes, because they reflect the patched RomFS image
    if (!bktrContext.romfs_dirtable_offset || !bktrContext.romfs_dirtable_size || !bktrContext.romfs_filetable_offset || !bktrContext.romfs_filetable_size)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid directory/file table for NCA BKTR RomFS section!", __func__);
        goto out;
    }
    
    bktrContext.romfs_filedata_offset = (bktrContext.romfs_offset + romFsHeader.fileDataOff);
    
    if (!bktrContext.romfs_filedata_offset)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid file data block offset for NCA BKTR RomFS section!", __func__);
        goto out;
    }
    
    bktrContext.romfs_dir_entries = calloc(1, bktrContext.romfs_dirtable_size);
    if (!bktrContext.romfs_dir_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA BKTR RomFS section directory entries!", __func__);
        goto out;
    }
    
    if (!readBktrSectionBlock(bktrContext.romfs_dirtable_offset, bktrContext.romfs_dir_entries, bktrContext.romfs_dirtable_size))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA BKTR RomFS section directory entries!", __func__);
        goto out;
    }
    
    bktrContext.romfs_file_entries = calloc(1, bktrContext.romfs_filetable_size);
    if (!bktrContext.romfs_file_entries)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for NCA BKTR RomFS section file entries!", __func__);
        goto out;
    }
    
    if (!readBktrSectionBlock(bktrContext.romfs_filetable_offset, bktrContext.romfs_file_entries, bktrContext.romfs_filetable_size))
    {
        breaks++;
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read NCA RomFS section file entries!", __func__);
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
