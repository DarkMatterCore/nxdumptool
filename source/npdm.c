/*
 * npdm.c
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
#include "npdm.h"
#include "rsa.h"

bool npdmInitializeContext(NpdmContext *out, PartitionFileSystemContext *pfs_ctx)
{
    NcaContext *nca_ctx = NULL;
    u64 cur_offset = 0;
    bool success = false;
    
    if (!out || !pfs_ctx || !pfs_ctx->nca_fs_ctx || !(nca_ctx = (NcaContext*)pfs_ctx->nca_fs_ctx->nca_ctx) || nca_ctx->content_type != NcmContentType_Program || !pfs_ctx->offset || !pfs_ctx->size || \
        !pfs_ctx->is_exefs || pfs_ctx->header_size <= sizeof(PartitionFileSystemHeader) || !pfs_ctx->header)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Free output context beforehand. */
    npdmFreeContext(out);
    
    /* Get 'main.npdm' file entry. */
    out->pfs_ctx = pfs_ctx;
    if (!(out->pfs_entry = pfsGetEntryByName(out->pfs_ctx, "main.npdm")))
    {
        LOGFILE("'main.npdm' entry unavailable in ExeFS!");
        goto end;
    }
    
    //LOGFILE("Found 'main.npdm' entry in Program NCA \"%s\".", nca_ctx->content_id_str);
    
    /* Check raw NPDM size. */
    if (!out->pfs_entry->size)
    {
        LOGFILE("Invalid raw NPDM size!");
        goto end;
    }
    
    /* Allocate memory for the raw NPDM data. */
    out->raw_data_size = out->pfs_entry->size;
    if (!(out->raw_data = malloc(out->raw_data_size)))
    {
        LOGFILE("Failed to allocate memory for the raw NPDM data!");
        goto end;
    }
    
    /* Read raw NPDM data into memory buffer. */
    if (!pfsReadEntryData(out->pfs_ctx, out->pfs_entry, out->raw_data, out->raw_data_size, 0))
    {
        LOGFILE("Failed to read raw NPDM data!");
        goto end;
    }
    
    /* Calculate SHA-256 checksum for the whole raw NPDM. */
    sha256CalculateHash(out->raw_data_hash, out->raw_data, out->raw_data_size);
    
    /* Verify meta header. */
    out->meta_header = (NpdmMetaHeader*)out->raw_data;
    cur_offset += sizeof(NpdmMetaHeader);
    
    if (__builtin_bswap32(out->meta_header->magic) != NPDM_META_MAGIC)
    {
        LOGFILE("Invalid meta header magic word! (0x%08X != 0x%08X).", __builtin_bswap32(out->meta_header->magic), __builtin_bswap32(NPDM_META_MAGIC));
        goto end;
    }
    
    if (!out->meta_header->flags.is_64bit_instruction && (out->meta_header->flags.process_address_space == NpdmProcessAddressSpace_AddressSpace64BitOld || \
        out->meta_header->flags.process_address_space == NpdmProcessAddressSpace_AddressSpace64Bit))
    {
        LOGFILE("Invalid meta header flags! (0x%02X).", *((u8*)&(out->meta_header->flags)));
        goto end;
    }
    
    if (out->meta_header->main_thread_priority > NPDM_MAIN_THREAD_MAX_PRIORITY)
    {
        LOGFILE("Invalid main thread priority! (0x%02X).", out->meta_header->main_thread_priority);
        goto end;
    }
    
    if (out->meta_header->main_thread_core_number > NPDM_MAIN_THREAD_MAX_CORE_NUMBER)
    {
        LOGFILE("Invalid main thread core number! (%u).", out->meta_header->main_thread_core_number);
        goto end;
    }
    
    if (out->meta_header->system_resource_size > NPDM_SYSTEM_RESOURCE_MAX_SIZE)
    {
        LOGFILE("Invalid system resource size! (0x%08X).", out->meta_header->system_resource_size);
        goto end;
    }
    
    if (!IS_ALIGNED(out->meta_header->main_thread_stack_size, NPDM_MAIN_THREAD_STACK_SIZE_ALIGNMENT))
    {
        LOGFILE("Invalid main thread stack size! (0x%08X).", out->meta_header->main_thread_stack_size);
        goto end;
    }
    
    if (out->meta_header->aci_offset < sizeof(NpdmMetaHeader) || out->meta_header->aci_size < sizeof(NpdmAciHeader) || (out->meta_header->aci_offset + out->meta_header->aci_size) > out->raw_data_size)
    {
        LOGFILE("Invalid ACI0 offset/size! (0x%08X, 0x%08X).", out->meta_header->aci_offset, out->meta_header->aci_size);
        goto end;
    }
    
    if (out->meta_header->acid_offset < sizeof(NpdmMetaHeader) || out->meta_header->acid_size < sizeof(NpdmAcidHeader) || (out->meta_header->acid_offset + out->meta_header->acid_size) > out->raw_data_size)
    {
        LOGFILE("Invalid ACID offset/size! (0x%08X, 0x%08X).", out->meta_header->acid_offset, out->meta_header->acid_size);
        goto end;
    }
    
    if (out->meta_header->aci_offset == out->meta_header->acid_offset || \
        (out->meta_header->aci_offset > out->meta_header->acid_offset && out->meta_header->aci_offset < (out->meta_header->acid_offset + out->meta_header->acid_size)) || \
        (out->meta_header->acid_offset > out->meta_header->aci_offset && out->meta_header->acid_offset < (out->meta_header->aci_offset + out->meta_header->aci_size)))
    {
        LOGFILE("ACI0/ACID sections overlap! (0x%08X, 0x%08X | 0x%08X, 0x%08X).", out->meta_header->aci_offset, out->meta_header->aci_size, out->meta_header->acid_offset, out->meta_header->acid_size);
        goto end;
    }
    
    /* Verify ACID section. */
    out->acid_header = (NpdmAcidHeader*)(out->raw_data + out->meta_header->acid_offset);
    cur_offset += out->meta_header->acid_size;
    
    if (__builtin_bswap32(out->acid_header->magic) != NPDM_ACID_MAGIC)
    {
        LOGFILE("Invalid ACID header magic word! (0x%08X != 0x%08X).", __builtin_bswap32(out->acid_header->magic), __builtin_bswap32(NPDM_ACID_MAGIC));
        goto end;
    }
    
    if (out->acid_header->size != (out->meta_header->acid_size - sizeof(out->acid_header->signature)))
    {
        LOGFILE("Invalid ACID header size! (0x%08X).", out->acid_header->size);
        goto end;
    }
    
    if (out->acid_header->program_id_min > out->acid_header->program_id_max)
    {
        LOGFILE("Invalid ACID program ID range! (%016lX - %016lX).", out->acid_header->program_id_min, out->acid_header->program_id_max);
        goto end;
    }
    
    if (out->acid_header->fs_access_control_offset < sizeof(NpdmAcidHeader) || out->acid_header->fs_access_control_size < sizeof(NpdmAcidFsAccessControlDescriptor) || \
        (out->acid_header->fs_access_control_offset + out->acid_header->fs_access_control_size) > out->meta_header->acid_size)
    {
        LOGFILE("Invalid ACID FsAccessControl offset/size! (0x%08X, 0x%08X).", out->acid_header->fs_access_control_offset, out->acid_header->fs_access_control_size);
        goto end;
    }
    
    out->acid_fac_descriptor = (NpdmAcidFsAccessControlDescriptor*)(out->raw_data + out->meta_header->acid_offset + out->acid_header->fs_access_control_offset);
    
    if (out->acid_header->srv_access_control_size)
    {
        if (out->acid_header->srv_access_control_offset < sizeof(NpdmAcidHeader) || \
            (out->acid_header->srv_access_control_offset + out->acid_header->srv_access_control_size) > out->meta_header->acid_size)
        {
            LOGFILE("Invalid ACID SrvAccessControl offset/size! (0x%08X, 0x%08X).", out->acid_header->srv_access_control_offset, out->acid_header->srv_access_control_size);
            goto end;
        }
        
        out->acid_sac_descriptor = (NpdmSrvAccessControlDescriptorEntry*)(out->raw_data + out->meta_header->acid_offset + out->acid_header->srv_access_control_offset);
    }
    
    if (out->acid_header->kernel_capability_size)
    {
        if (!IS_ALIGNED(out->acid_header->kernel_capability_size, sizeof(NpdmKernelCapabilityDescriptorEntry)) || \
            out->acid_header->kernel_capability_offset < sizeof(NpdmAcidHeader) || \
            (out->acid_header->kernel_capability_offset + out->acid_header->kernel_capability_size) > out->meta_header->acid_size)
        {
            LOGFILE("Invalid ACID KernelCapability offset/size! (0x%08X, 0x%08X).", out->acid_header->kernel_capability_offset, out->acid_header->kernel_capability_size);
            goto end;
        }
        
        out->acid_kc_descriptor = (NpdmKernelCapabilityDescriptorEntry*)(out->raw_data + out->meta_header->acid_offset + out->acid_header->kernel_capability_offset);
    }
    
    /* Verify ACI0 section. */
    out->aci_header = (NpdmAciHeader*)(out->raw_data + out->meta_header->aci_offset);
    cur_offset += out->meta_header->aci_size;
    
    if (__builtin_bswap32(out->aci_header->magic) != NPDM_ACI0_MAGIC)
    {
        LOGFILE("Invalid ACI0 header magic word! (0x%08X != 0x%08X).", __builtin_bswap32(out->aci_header->magic), __builtin_bswap32(NPDM_ACI0_MAGIC));
        goto end;
    }
    
    if (out->aci_header->program_id != nca_ctx->header.program_id)
    {
        LOGFILE("ACI0 program ID mismatch! (%016lX != %016lX).", out->aci_header->program_id, nca_ctx->header.program_id);
        goto end;
    }
    
    if (out->aci_header->program_id < out->acid_header->program_id_min || out->aci_header->program_id > out->acid_header->program_id_max)
    {
        LOGFILE("ACI0 program ID out of ACID program ID range! (%016lX, %016lX - %016lX).", out->aci_header->program_id, out->acid_header->program_id_min, out->acid_header->program_id_max);
        goto end;
    }
    
    if (out->aci_header->fs_access_control_offset < sizeof(NpdmAciHeader) || out->aci_header->fs_access_control_size < sizeof(NpdmAciFsAccessControlDescriptor) || \
        (out->aci_header->fs_access_control_offset + out->aci_header->fs_access_control_size) > out->meta_header->aci_size)
    {
        LOGFILE("Invalid ACI0 FsAccessControl offset/size! (0x%08X, 0x%08X).", out->aci_header->fs_access_control_offset, out->aci_header->fs_access_control_size);
        goto end;
    }
    
    out->aci_fac_descriptor = (NpdmAciFsAccessControlDescriptor*)(out->raw_data + out->meta_header->aci_offset + out->aci_header->fs_access_control_offset);
    
    if (out->aci_header->srv_access_control_size)
    {
        if (out->aci_header->srv_access_control_offset < sizeof(NpdmAciHeader) || \
            (out->aci_header->srv_access_control_offset + out->aci_header->srv_access_control_size) > out->meta_header->aci_size)
        {
            LOGFILE("Invalid ACI0 SrvAccessControl offset/size! (0x%08X, 0x%08X).", out->aci_header->srv_access_control_offset, out->aci_header->srv_access_control_size);
            goto end;
        }
        
        out->aci_sac_descriptor = (NpdmSrvAccessControlDescriptorEntry*)(out->raw_data + out->meta_header->aci_offset + out->aci_header->srv_access_control_offset);
    }
    
    if (out->aci_header->kernel_capability_size)
    {
        if (!IS_ALIGNED(out->aci_header->kernel_capability_size, sizeof(NpdmKernelCapabilityDescriptorEntry)) || \
            out->aci_header->kernel_capability_offset < sizeof(NpdmAciHeader) || \
            (out->aci_header->kernel_capability_offset + out->aci_header->kernel_capability_size) > out->meta_header->aci_size)
        {
            LOGFILE("Invalid ACI0 KernelCapability offset/size! (0x%08X, 0x%08X).", out->aci_header->kernel_capability_offset, out->aci_header->kernel_capability_size);
            goto end;
        }
        
        out->aci_kc_descriptor = (NpdmKernelCapabilityDescriptorEntry*)(out->raw_data + out->meta_header->aci_offset + out->aci_header->kernel_capability_offset);
    }
    
    /* Safety check: verify raw NPDM size. */
    if (out->raw_data_size < cur_offset)
    {
        LOGFILE("Invalid raw NPDM size! (0x%lX < 0x%lX).", out->raw_data_size, cur_offset);
        goto end;
    }
    
    success = true;
    
end:
    if (!success) npdmFreeContext(out);
    
    return success;
}

bool npdmChangeAcidPublicKeyAndNcaSignature(NpdmContext *npdm_ctx)
{
    NcaContext *nca_ctx = NULL;
    
    if (!npdmIsValidContext(npdm_ctx) || !(nca_ctx = (NcaContext*)npdm_ctx->pfs_ctx->nca_fs_ctx->nca_ctx) || nca_ctx->content_type != NcmContentType_Program)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    /* Update NPDM ACID public key. */
    memcpy(npdm_ctx->acid_header->public_key, rsa2048GetCustomPublicKey(), RSA2048_PUBKEY_SIZE);
    
    /* Update NCA ACID signature. */
    if (!rsa2048GenerateSha256BasedPssSignature(nca_ctx->header.acid_signature, &(nca_ctx->header.magic), NCA_ACID_SIGNATURE_AREA_SIZE))
    {
        LOGFILE("Failed to generate RSA-2048-PSS NCA ACID signature!");
        return false;
    }
    
    return true;
}
