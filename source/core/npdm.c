/*
 * npdm.c
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

#include "nxdt_utils.h"
#include "npdm.h"
#include "rsa.h"

bool npdmInitializeContext(NpdmContext *out, PartitionFileSystemContext *pfs_ctx)
{
    NcaContext *nca_ctx = NULL;
    u64 cur_offset = 0;
    bool success = false, dump_meta_header = false, dump_acid_header = false, dump_aci_header = false;
    PartitionFileSystemEntry *pfs_entry = NULL;

    if (!out || !pfs_ctx || !ncaStorageIsValidContext(&(pfs_ctx->storage_ctx)) || !(nca_ctx = (NcaContext*)pfs_ctx->nca_fs_ctx->nca_ctx) || \
        nca_ctx->content_type != NcmContentType_Program || !pfs_ctx->offset || !pfs_ctx->size || !pfs_ctx->is_exefs || \
        pfs_ctx->header_size <= sizeof(PartitionFileSystemHeader) || !pfs_ctx->header)
    {
        LOG_MSG_ERROR("Invalid parameters!");
        return false;
    }

    /* Free output context beforehand. */
    npdmFreeContext(out);

    /* Get 'main.npdm' file entry. */
    if (!(pfs_entry = pfsGetEntryByName(pfs_ctx, "main.npdm")))
    {
        LOG_MSG_ERROR("'main.npdm' entry unavailable in ExeFS!");
        goto end;
    }

    LOG_MSG_INFO("Found 'main.npdm' entry in Program NCA \"%s\".", nca_ctx->content_id_str);

    /* Check raw NPDM size. */
    if (!pfs_entry->size)
    {
        LOG_MSG_ERROR("Invalid raw NPDM size!");
        goto end;
    }

    /* Allocate memory for the raw NPDM data. */
    out->raw_data_size = pfs_entry->size;
    if (!(out->raw_data = malloc(out->raw_data_size)))
    {
        LOG_MSG_ERROR("Failed to allocate memory for the raw NPDM data!");
        goto end;
    }

    /* Read raw NPDM data into memory buffer. */
    if (!pfsReadEntryData(pfs_ctx, pfs_entry, out->raw_data, out->raw_data_size, 0))
    {
        LOG_MSG_ERROR("Failed to read raw NPDM data!");
        goto end;
    }

    /* Verify meta header. */
    out->meta_header = (NpdmMetaHeader*)out->raw_data;
    cur_offset += sizeof(NpdmMetaHeader);

    if (__builtin_bswap32(out->meta_header->magic) != NPDM_META_MAGIC)
    {
        LOG_MSG_ERROR("Invalid meta header magic word! (0x%08X != 0x%08X).", __builtin_bswap32(out->meta_header->magic), __builtin_bswap32(NPDM_META_MAGIC));
        dump_meta_header = true;
        goto end;
    }

    if (!out->meta_header->flags.is_64bit_instruction && (out->meta_header->flags.process_address_space == NpdmProcessAddressSpace_AddressSpace64BitOld || \
        out->meta_header->flags.process_address_space == NpdmProcessAddressSpace_AddressSpace64Bit))
    {
        LOG_MSG_ERROR("Invalid meta header flags! (0x%02X).", *((u8*)&(out->meta_header->flags)));
        dump_meta_header = true;
        goto end;
    }

    if (out->meta_header->main_thread_priority > NPDM_MAIN_THREAD_MAX_PRIORITY)
    {
        LOG_MSG_ERROR("Invalid main thread priority! (0x%02X).", out->meta_header->main_thread_priority);
        dump_meta_header = true;
        goto end;
    }

    if (out->meta_header->main_thread_core_number > NPDM_MAIN_THREAD_MAX_CORE_NUMBER)
    {
        LOG_MSG_ERROR("Invalid main thread core number! (%u).", out->meta_header->main_thread_core_number);
        dump_meta_header = true;
        goto end;
    }

    if (out->meta_header->system_resource_size > NPDM_SYSTEM_RESOURCE_MAX_SIZE)
    {
        LOG_MSG_ERROR("Invalid system resource size! (0x%X).", out->meta_header->system_resource_size);
        dump_meta_header = true;
        goto end;
    }

    if (!IS_ALIGNED(out->meta_header->main_thread_stack_size, NPDM_MAIN_THREAD_STACK_SIZE_ALIGNMENT))
    {
        LOG_MSG_ERROR("Invalid main thread stack size! (0x%X).", out->meta_header->main_thread_stack_size);
        dump_meta_header = true;
        goto end;
    }

    if (out->meta_header->aci_offset < sizeof(NpdmMetaHeader) || out->meta_header->aci_size < sizeof(NpdmAciHeader) || (out->meta_header->aci_offset + out->meta_header->aci_size) > out->raw_data_size)
    {
        LOG_MSG_ERROR("Invalid ACI0 offset/size! (0x%X, 0x%X).", out->meta_header->aci_offset, out->meta_header->aci_size);
        dump_meta_header = true;
        goto end;
    }

    if (out->meta_header->acid_offset < sizeof(NpdmMetaHeader) || out->meta_header->acid_size < sizeof(NpdmAcidHeader) || (out->meta_header->acid_offset + out->meta_header->acid_size) > out->raw_data_size)
    {
        LOG_MSG_ERROR("Invalid ACID offset/size! (0x%X, 0x%X).", out->meta_header->acid_offset, out->meta_header->acid_size);
        dump_meta_header = true;
        goto end;
    }

    if (out->meta_header->aci_offset == out->meta_header->acid_offset || \
        (out->meta_header->aci_offset > out->meta_header->acid_offset && out->meta_header->aci_offset < (out->meta_header->acid_offset + out->meta_header->acid_size)) || \
        (out->meta_header->acid_offset > out->meta_header->aci_offset && out->meta_header->acid_offset < (out->meta_header->aci_offset + out->meta_header->aci_size)))
    {
        LOG_MSG_ERROR("ACI0/ACID sections overlap! (0x%X, 0x%X | 0x%X, 0x%X).", out->meta_header->aci_offset, out->meta_header->aci_size, out->meta_header->acid_offset, out->meta_header->acid_size);
        dump_meta_header = true;
        goto end;
    }

    /* Verify ACID section. */
    out->acid_header = (NpdmAcidHeader*)(out->raw_data + out->meta_header->acid_offset);
    cur_offset += out->meta_header->acid_size;

    if (__builtin_bswap32(out->acid_header->magic) != NPDM_ACID_MAGIC)
    {
        LOG_MSG_ERROR("Invalid ACID header magic word! (0x%08X != 0x%08X).", __builtin_bswap32(out->acid_header->magic), __builtin_bswap32(NPDM_ACID_MAGIC));
        dump_meta_header = dump_acid_header = true;
        goto end;
    }

    if (out->acid_header->size != (out->meta_header->acid_size - sizeof(out->acid_header->signature)))
    {
        LOG_MSG_ERROR("Invalid ACID header size! (0x%X).", out->acid_header->size);
        dump_meta_header = dump_acid_header = true;
        goto end;
    }

    if (out->acid_header->program_id_min > out->acid_header->program_id_max)
    {
        LOG_MSG_ERROR("Invalid ACID program ID range! (%016lX - %016lX).", out->acid_header->program_id_min, out->acid_header->program_id_max);
        dump_meta_header = dump_acid_header = true;
        goto end;
    }

    if (out->acid_header->fs_access_control_offset < sizeof(NpdmAcidHeader) || out->acid_header->fs_access_control_size < sizeof(NpdmFsAccessControlDescriptor) || \
        (out->acid_header->fs_access_control_offset + out->acid_header->fs_access_control_size) > out->meta_header->acid_size)
    {
        LOG_MSG_ERROR("Invalid ACID FsAccessControl offset/size! (0x%X, 0x%X).", out->acid_header->fs_access_control_offset, out->acid_header->fs_access_control_size);
        dump_meta_header = dump_acid_header = true;
        goto end;
    }

    out->acid_fac_descriptor = (NpdmFsAccessControlDescriptor*)(out->raw_data + out->meta_header->acid_offset + out->acid_header->fs_access_control_offset);

    if (out->acid_header->srv_access_control_size)
    {
        if (out->acid_header->srv_access_control_offset < sizeof(NpdmAcidHeader) || \
            (out->acid_header->srv_access_control_offset + out->acid_header->srv_access_control_size) > out->meta_header->acid_size)
        {
            LOG_MSG_ERROR("Invalid ACID SrvAccessControl offset/size! (0x%X, 0x%X).", out->acid_header->srv_access_control_offset, out->acid_header->srv_access_control_size);
            dump_meta_header = dump_acid_header = true;
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
            LOG_MSG_ERROR("Invalid ACID KernelCapability offset/size! (0x%X, 0x%X).", out->acid_header->kernel_capability_offset, out->acid_header->kernel_capability_size);
            dump_meta_header = dump_acid_header = true;
            goto end;
        }

        out->acid_kc_descriptor = (NpdmKernelCapabilityDescriptorEntry*)(out->raw_data + out->meta_header->acid_offset + out->acid_header->kernel_capability_offset);
    }

    /* Verify ACI0 section. */
    out->aci_header = (NpdmAciHeader*)(out->raw_data + out->meta_header->aci_offset);
    cur_offset += out->meta_header->aci_size;

    if (__builtin_bswap32(out->aci_header->magic) != NPDM_ACI0_MAGIC)
    {
        LOG_MSG_ERROR("Invalid ACI0 header magic word! (0x%08X != 0x%08X).", __builtin_bswap32(out->aci_header->magic), __builtin_bswap32(NPDM_ACI0_MAGIC));
        dump_meta_header = dump_acid_header = dump_aci_header = true;
        goto end;
    }

    if (out->aci_header->program_id != nca_ctx->header.program_id)
    {
        LOG_MSG_ERROR("ACI0 program ID mismatch! (%016lX != %016lX).", out->aci_header->program_id, nca_ctx->header.program_id);
        dump_meta_header = dump_acid_header = dump_aci_header = true;
        goto end;
    }

    if (out->aci_header->program_id < out->acid_header->program_id_min || out->aci_header->program_id > out->acid_header->program_id_max)
    {
        LOG_MSG_ERROR("ACI0 program ID out of ACID program ID range! (%016lX, %016lX - %016lX).", out->aci_header->program_id, out->acid_header->program_id_min, out->acid_header->program_id_max);
        dump_meta_header = dump_acid_header = dump_aci_header = true;
        goto end;
    }

    if (out->aci_header->fs_access_control_offset < sizeof(NpdmAciHeader) || out->aci_header->fs_access_control_size < sizeof(NpdmFsAccessControlData) || \
        (out->aci_header->fs_access_control_offset + out->aci_header->fs_access_control_size) > out->meta_header->aci_size)
    {
        LOG_MSG_ERROR("Invalid ACI0 FsAccessControl offset/size! (0x%X, 0x%X).", out->aci_header->fs_access_control_offset, out->aci_header->fs_access_control_size);
        dump_meta_header = dump_acid_header = dump_aci_header = true;
        goto end;
    }

    out->aci_fac_data = (NpdmFsAccessControlData*)(out->raw_data + out->meta_header->aci_offset + out->aci_header->fs_access_control_offset);

    if (out->aci_header->srv_access_control_size)
    {
        if (out->aci_header->srv_access_control_offset < sizeof(NpdmAciHeader) || \
            (out->aci_header->srv_access_control_offset + out->aci_header->srv_access_control_size) > out->meta_header->aci_size)
        {
            LOG_MSG_ERROR("Invalid ACI0 SrvAccessControl offset/size! (0x%X, 0x%X).", out->aci_header->srv_access_control_offset, out->aci_header->srv_access_control_size);
            dump_meta_header = dump_acid_header = dump_aci_header = true;
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
            LOG_MSG_ERROR("Invalid ACI0 KernelCapability offset/size! (0x%X, 0x%X).", out->aci_header->kernel_capability_offset, out->aci_header->kernel_capability_size);
            dump_meta_header = dump_acid_header = dump_aci_header = true;
            goto end;
        }

        out->aci_kc_descriptor = (NpdmKernelCapabilityDescriptorEntry*)(out->raw_data + out->meta_header->aci_offset + out->aci_header->kernel_capability_offset);
    }

    /* Safety check: verify raw NPDM size. */
    if (out->raw_data_size < cur_offset)
    {
        LOG_MSG_ERROR("Invalid raw NPDM size! (0x%lX < 0x%lX).", out->raw_data_size, cur_offset);
        goto end;
    }

    success = true;

end:
    if (!success)
    {
        if (dump_aci_header) LOG_DATA_DEBUG(out->aci_header, sizeof(NpdmAciHeader), "NPDM ACI0 Header dump:");
        if (dump_acid_header) LOG_DATA_DEBUG(out->acid_header, sizeof(NpdmAcidHeader), "NPDM ACID Header dump:");
        if (dump_meta_header) LOG_DATA_DEBUG(out->meta_header, sizeof(NpdmMetaHeader), "NPDM Meta Header dump:");

        npdmFreeContext(out);
    }

    return success;
}
