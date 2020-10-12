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
