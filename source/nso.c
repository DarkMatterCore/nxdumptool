#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include "nso.h"
#include "lz4.h"
#include "util.h"
#include "ui.h"

/* Extern variables */

extern int breaks;
extern int font_height;

/* Statically allocated variables */

static u8 *nsoBinaryData = NULL;
static u64 nsoBinaryDataSize = 0;

static u64 nsoBinaryTextSectionOffset = 0;
static u64 nsoBinaryTextSectionSize = 0;

static u64 nsoBinaryRodataSectionOffset = 0;
static u64 nsoBinaryRodataSectionSize = 0;

static u64 nsoBinaryDataSectionOffset = 0;
static u64 nsoBinaryDataSectionSize = 0;

void freeNsoBinaryData()
{
    if (nsoBinaryData)
    {
        free(nsoBinaryData);
        nsoBinaryData = NULL;
    }
    
    nsoBinaryDataSize = 0;
    
    nsoBinaryTextSectionOffset = 0;
    nsoBinaryTextSectionSize = 0;
    
    nsoBinaryRodataSectionOffset = 0;
    nsoBinaryRodataSectionSize = 0;
    
    nsoBinaryDataSectionOffset = 0;
    nsoBinaryDataSectionSize = 0;
}

bool loadNsoBinaryData(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, Aes128CtrContext *aes_ctx, u64 nso_base_offset, nso_header_t *nsoHeader)
{
    if (!ncmStorage || !ncaId || !aes_ctx || !nso_base_offset || !nsoHeader)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to load .text, .rodata and .data sections from NSO in Program NCA!", __func__);
        return false;
    }
    
    u8 i;
    
    u8 *nsoTextSection = NULL;
    u64 nsoTextSectionSize = 0;
    
    u8 *nsoRodataSection = NULL;
    u64 nsoRodataSectionSize = 0;
    
    u8 *nsoDataSection = NULL;
    u64 nsoDataSectionSize = 0;
    
    u8 *curCompressedSection;
    u64 curCompressedSectionSize;
    u64 curCompressedSectionOffset;
    u8 curSectionFlag;
    
    u8 *curDecompressedSection;
    u64 curDecompressedSectionSize;
    
    bool success = true;
    
    freeNsoBinaryData();
    
    for(i = 0; i < 3; i++)
    {
        curCompressedSection = NULL;
        curCompressedSectionSize = (i == 0 ? (u64)nsoHeader->text_compressed_size : (i == 1 ? (u64)nsoHeader->rodata_compressed_size : (u64)nsoHeader->data_compressed_size));
        curCompressedSectionOffset = (nso_base_offset + (i == 0 ? (u64)nsoHeader->text_segment_header.file_offset : (i == 1 ? (u64)nsoHeader->rodata_segment_header.file_offset : (u64)nsoHeader->data_segment_header.file_offset)));
        curSectionFlag = (1 << i);
        
        curDecompressedSection = NULL;
        curDecompressedSectionSize = (i == 0 ? (u64)nsoHeader->text_segment_header.decompressed_size : (i == 1 ? (u64)nsoHeader->rodata_segment_header.decompressed_size : (u64)nsoHeader->data_segment_header.decompressed_size));
        
        // Load section
        curCompressedSection = malloc(curCompressedSectionSize);
        if (!curCompressedSection)
        {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for the compressed %s section from NSO in Program NCA!", __func__, (i == 0 ? ".text" : (i == 1 ? ".rodata" : ".data")));
            success = false;
            break;
        }
        
        if (!processNcaCtrSectionBlock(ncmStorage, ncaId, aes_ctx, curCompressedSectionOffset, curCompressedSection, curCompressedSectionSize, false))
        {
            breaks++;
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: failed to read 0x%016lX bytes %s section from NSO in Program NCA!", __func__, curCompressedSectionSize, (i == 0 ? ".text" : (i == 1 ? ".rodata" : ".data")));
            free(curCompressedSection);
            success = false;
            break;
        }
        
        if (nsoHeader->flags & curSectionFlag)
        {
            if (curDecompressedSectionSize <= curCompressedSectionSize)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid decompressed size for %s section from NSO in Program NCA!", __func__, (i == 0 ? ".text" : (i == 1 ? ".rodata" : ".data")));
                free(curCompressedSection);
                success = false;
                break;
            }
            
            // Uncompress section
            curDecompressedSection = malloc(curDecompressedSectionSize);
            if (!curDecompressedSection)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate memory for the decompressed %s section from NSO in Program NCA!", __func__, (i == 0 ? ".text" : (i == 1 ? ".rodata" : ".data")));
                free(curCompressedSection);
                success = false;
                break;
            }
            
            if (LZ4_decompress_safe((const char*)curCompressedSection, (char*)curDecompressedSection, (int)curCompressedSectionSize, (int)curDecompressedSectionSize) != (int)curDecompressedSectionSize)
            {
                uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to decompress %s section from NSO in Program NCA!", __func__, (i == 0 ? ".text" : (i == 1 ? ".rodata" : ".data")));
                free(curDecompressedSection);
                free(curCompressedSection);
                success = false;
                break;
            }
            
            free(curCompressedSection);
            
            switch(i)
            {
                case 0:
                    nsoTextSection = curDecompressedSection;
                    nsoTextSectionSize = curDecompressedSectionSize;
                    break;
                case 1:
                    nsoRodataSection = curDecompressedSection;
                    nsoRodataSectionSize = curDecompressedSectionSize;
                    break;
                case 2:
                    nsoDataSection = curDecompressedSection;
                    nsoDataSectionSize = curDecompressedSectionSize;
                    break;
                default:
                    break;
            }
        } else {
            switch(i)
            {
                case 0:
                    nsoTextSection = curCompressedSection;
                    nsoTextSectionSize = curCompressedSectionSize;
                    break;
                case 1:
                    nsoRodataSection = curCompressedSection;
                    nsoRodataSectionSize = curCompressedSectionSize;
                    break;
                case 2:
                    nsoDataSection = curCompressedSection;
                    nsoDataSectionSize = curCompressedSectionSize;
                    break;
                default:
                    break;
            }
        }
    }
    
    curCompressedSection = curDecompressedSection = NULL;
    
    if (success)
    {
        // Calculate full binary size
        u64 finalTextSectionSize = nsoTextSectionSize;
        u64 finalRodataSectionSize = nsoRodataSectionSize;
        
        nsoBinaryDataSize = nsoTextSectionSize;
        
        if ((u64)nsoHeader->rodata_segment_header.memory_offset > nsoBinaryDataSize)
        {
            nsoBinaryDataSize += ((u64)nsoHeader->rodata_segment_header.memory_offset - nsoBinaryDataSize);
        } else
        if ((u64)nsoHeader->rodata_segment_header.memory_offset < nsoBinaryDataSize)
        {
            finalTextSectionSize -= (nsoBinaryDataSize - (u64)nsoHeader->rodata_segment_header.memory_offset);
            nsoBinaryDataSize -= (nsoBinaryDataSize - (u64)nsoHeader->rodata_segment_header.memory_offset);
        }
        
        nsoBinaryDataSize += nsoRodataSectionSize;
        
        if ((u64)nsoHeader->data_segment_header.memory_offset > nsoBinaryDataSize)
        {
            nsoBinaryDataSize += ((u64)nsoHeader->data_segment_header.memory_offset - nsoBinaryDataSize);
        } else
        if ((u64)nsoHeader->data_segment_header.memory_offset < nsoBinaryDataSize)
        {
            finalRodataSectionSize -= (nsoBinaryDataSize - (u64)nsoHeader->data_segment_header.memory_offset);
            nsoBinaryDataSize -= (nsoBinaryDataSize - (u64)nsoHeader->data_segment_header.memory_offset);
        }
        
        nsoBinaryDataSize += nsoDataSectionSize;
        
        nsoBinaryData = calloc(nsoBinaryDataSize, sizeof(u8));
        if (nsoBinaryData)
        {
            memcpy(nsoBinaryData, nsoTextSection, finalTextSectionSize);
            memcpy(nsoBinaryData + (u64)nsoHeader->rodata_segment_header.memory_offset, nsoRodataSection, finalRodataSectionSize);
            memcpy(nsoBinaryData + (u64)nsoHeader->data_segment_header.memory_offset, nsoDataSection, nsoDataSectionSize);
            
            nsoBinaryTextSectionOffset = 0;
            nsoBinaryTextSectionSize = finalTextSectionSize;
            
            nsoBinaryRodataSectionOffset = (u64)nsoHeader->rodata_segment_header.memory_offset;
            nsoBinaryRodataSectionSize = finalRodataSectionSize;
            
            nsoBinaryDataSectionOffset = (u64)nsoHeader->data_segment_header.memory_offset;
            nsoBinaryDataSectionSize = nsoDataSectionSize;
        } else {
            uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: unable to allocate %lu bytes for full decompressed NSO in Program NCA!", __func__, nsoBinaryDataSize);
            nsoBinaryDataSize = 0;
            success = false;
        }
    }
    
    if (nsoTextSection) free(nsoTextSection);
    if (nsoRodataSection) free(nsoRodataSection);
    if (nsoDataSection) free(nsoDataSection);
    
    return success;
}

bool retrieveMiddlewareListFromNso(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, Aes128CtrContext *aes_ctx, const char *nso_filename, u64 nso_base_offset, nso_header_t *nsoHeader, char *programInfoXml)
{
    if (!ncmStorage || !ncaId || !aes_ctx || !nso_filename || !strlen(nso_filename) || !nso_base_offset || !nsoHeader || !programInfoXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve middleware list from NSO in Program NCA!", __func__);
        return false;
    }
    
    u64 i;
    
    char tmp[NAME_BUF_LEN] = {'\0'};
    
    if (!loadNsoBinaryData(ncmStorage, ncaId, aes_ctx, nso_base_offset, nsoHeader)) return false;
    
    for(i = 0; i < nsoBinaryRodataSectionSize; i++)
    {
        char *curStr = ((char*)nsoBinaryData + nsoBinaryRodataSectionOffset + i);
        
        if (strncmp(curStr, "SDK MW+", 7) != 0) continue;
        
        // Found a match
        char *mwDev = (curStr + 7);
        char *mwName = (strchr(mwDev, '+') + 1);
        
        // Filter nnSdk entries
        if (!strncasecmp(mwName, "NintendoSdk_nnSdk", 17))
        {
            i += strlen(curStr);
            continue;
        }
        
        sprintf(tmp, "    <Middleware>\n" \
                     "      <ModuleName>%s</ModuleName>\n" \
                     "      <VenderName>%.*s</VenderName>\n" \
                     "      <NsoName>%s</NsoName>\n" \
                     "    </Middleware>\n", \
                     mwName, \
                     (int)(mwName - mwDev - 1), mwDev, \
                     nso_filename);
        
        strcat(programInfoXml, tmp);
        
        // Update counter
        i += strlen(curStr);
    }
    
    freeNsoBinaryData();
    
    return true;
}

bool retrieveSymbolsListFromNso(NcmContentStorage *ncmStorage, const NcmContentId *ncaId, Aes128CtrContext *aes_ctx, const char *nso_filename, u64 nso_base_offset, nso_header_t *nsoHeader, char *programInfoXml)
{
    if (!ncmStorage || !ncaId || !aes_ctx || !nso_filename || !strlen(nso_filename) || !nso_base_offset || !nsoHeader || !programInfoXml)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid parameters to retrieve symbols list from NSO in Program NCA!", __func__);
        return false;
    }
    
    u64 i;
    
    bool success = false;
    
    char tmp[NAME_BUF_LEN] = {'\0'};
    
    u32 mod_magic_offset;
    u32 mod_magic;
    s32 dynamic_section_offset;
    
    bool armv7;
    
    u64 dynamic_block_size;
    u64 dynamic_block_cnt;
    
    bool found_strtab = false;
    u64 symbol_str_table_offset = 0;
    
    bool found_symtab = false;
    u64 symbol_table_offset = 0;
    
    bool found_strsz = false;
    u64 symbol_str_table_size = 0;
    
    char *symbol_str_table = NULL;
    
    u64 cur_symbol_table_offset = 0;
    
    if (!loadNsoBinaryData(ncmStorage, ncaId, aes_ctx, nso_base_offset, nsoHeader)) return false;
    
    mod_magic_offset = *((u32*)(&(nsoBinaryData[0x04])));
    mod_magic = *((u32*)(&(nsoBinaryData[mod_magic_offset])));
    dynamic_section_offset = ((s32)mod_magic_offset + *((s32*)(&(nsoBinaryData[mod_magic_offset + 0x04]))));
    
    if (bswap_32(mod_magic) != MOD_MAGIC)
    {
        uiDrawString(STRING_X_POS, STRING_Y_POS(breaks), FONT_COLOR_ERROR_RGB, "%s: invalid MOD0 magic word in decompressed NSO from Program NCA! (0x%08X)", __func__, bswap_32(mod_magic));
        goto out;
    }
    
    armv7 = (*((u64*)(&(nsoBinaryData[dynamic_section_offset]))) > (u64)0xFFFFFFFF || *((u64*)(&(nsoBinaryData[dynamic_section_offset + 0x10]))) > (u64)0xFFFFFFFF);
    
    // Read dynamic section
    dynamic_block_size = (armv7 ? 0x08 : 0x10);
    dynamic_block_cnt = ((nsoBinaryDataSize - dynamic_section_offset) / dynamic_block_size);
    
    for(i = 0; i < dynamic_block_cnt; i++)
    {
        if ((nsoBinaryDataSize - dynamic_section_offset - (i * dynamic_block_size)) < dynamic_block_size) break;
        
        u64 tag = (armv7 ? (u64)(*((u32*)(&(nsoBinaryData[dynamic_section_offset + (i * dynamic_block_size)])))) : *((u64*)(&(nsoBinaryData[dynamic_section_offset + (i * dynamic_block_size)]))));
        u64 val = (armv7 ? (u64)(*((u32*)(&(nsoBinaryData[dynamic_section_offset + (i * dynamic_block_size) + 0x04])))) : *((u64*)(&(nsoBinaryData[dynamic_section_offset + (i * dynamic_block_size) + 0x08]))));
        
        if (!tag) break;
        
        if (tag == DT_STRTAB && !found_strtab)
        {
            // Retrieve symbol string table offset
            symbol_str_table_offset = val;
            found_strtab = true;
        }
        
        if (tag == DT_SYMTAB && !found_symtab)
        {
            // Retrieve symbol table offset
            symbol_table_offset = val;
            found_symtab = true;
        }
        
        if (tag == DT_STRSZ && !found_strsz)
        {
            // Retrieve symbol string table size
            symbol_str_table_size = val;
            found_strsz = true;
        }
        
        if (found_strtab && found_symtab && found_strsz) break;
    }
    
    if (!found_strtab || !found_symtab || !found_strsz)
    {
        // Nothing to do here if we can't find what we need
        success = true;
        goto out;
    }
    
    // Point to the symbol string table
    symbol_str_table = ((char*)nsoBinaryData + symbol_str_table_offset);
    
    // Retrieve symbol list
    cur_symbol_table_offset = symbol_table_offset;
    while(true)
    {
        if (symbol_table_offset < symbol_str_table_offset && cur_symbol_table_offset >= symbol_str_table_offset) break;
        
        u32 st_name = *((u32*)(&(nsoBinaryData[cur_symbol_table_offset])));
        u8 st_info = (armv7 ? nsoBinaryData[cur_symbol_table_offset + 0x0C] : nsoBinaryData[cur_symbol_table_offset + 0x04]);
        //u8 st_other = (armv7 ? nsoBinaryData[cur_symbol_table_offset + 0x0D] : nsoBinaryData[cur_symbol_table_offset + 0x05]);
        u16 st_shndx = (armv7 ? *((u16*)(&(nsoBinaryData[cur_symbol_table_offset + 0x0E]))) : *((u16*)(&(nsoBinaryData[cur_symbol_table_offset + 0x06]))));
        u64 st_value = (armv7 ? (u64)(*((u32*)(&(nsoBinaryData[cur_symbol_table_offset + 0x04])))) : *((u64*)(&(nsoBinaryData[cur_symbol_table_offset + 0x08]))));
        //u64 st_size = (armv7 ? (u64)(*((u32*)(&(nsoBinaryData[cur_symbol_table_offset + 0x08])))) : *((u64*)(&(nsoBinaryData[cur_symbol_table_offset + 0x10]))));
        
        //u8 st_vis = (st_other & 0x03);
        u8 st_type = (st_info & 0x0F);
        //u8 st_bind = (st_info >> 0x04);
        
        if (st_name >= symbol_str_table_size) break;
        
        cur_symbol_table_offset += (armv7 ? 0x10 : 0x18);
        
        // TO-DO: Add more filters?
        if (!st_shndx && !st_value && st_type != ST_OBJECT)
        {
            sprintf(tmp, "    <UnresolvedApi>\n" \
                         "      <ApiName>%s</ApiName>\n" \
                         "      <NsoName>%s</NsoName>\n" \
                         "    </UnresolvedApi>\n", \
                         symbol_str_table + st_name, \
                         nso_filename);
            
            strcat(programInfoXml, tmp);
        }
    }
    
    success = true;
    
out:
    freeNsoBinaryData();
    
    return success;
}
