/*
 * tik.h
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

#pragma once

#ifndef __TIK_H__
#define __TIK_H__

#include "signature.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIGNED_TIK_MAX_SIZE         0x400                   /* Max ticket entry size in the ES ticket system savedata file. */
#define SIGNED_TIK_MIN_SIZE         sizeof(TikSigHmac160)   /* Assuming no ESV1/ESV2 records are available. */

#define GENERATE_TIK_STRUCT(sigtype, tiksize) \
\
typedef struct { \
    SignatureBlock##sigtype sig_block; \
    TikCommonBlock tik_common_block; \
    u8 es_section_record_data[]; \
} TikSig##sigtype; \
\
NXDT_ASSERT(TikSig##sigtype, tiksize);

typedef enum {
    TikTitleKeyType_Common       = 0,
    TikTitleKeyType_Personalized = 1
} TikTitleKeyType;

typedef enum {
    TikLicenseType_Permanent    = 0,
    TikLicenseType_Demo         = 1,
    TikLicenseType_Trial        = 2,
    TikLicenseType_Rental       = 3,
    TikLicenseType_Subscription = 4,
    TikLicenseType_Service      = 5
} TikLicenseType;

typedef enum {
    TikPropertyMask_PreInstallation      = BIT(0),
    TikPropertyMask_SharedTitle          = BIT(1),
    TikPropertyMask_AllContents          = BIT(2),
    TikPropertyMask_DeviceLinkIndepedent = BIT(3),
    TikPropertyMask_Volatile             = BIT(4),  ///< Used to determine if the ticket copy inside ticket.bin should be encrypted or not.
    TikPropertyMask_ELicenseRequired     = BIT(5)   ///< Used to determine if the console should connect to the Internet to perform elicense verification.
} TikPropertyMask;

/// Placed after the ticket signature block.
typedef struct {
    char issuer[0x40];
    u8 titlekey_block[0x100];
    u8 format_version;
    u8 titlekey_type;           ///< TikTitleKeyType.
    u16 ticket_version;
    u8 license_type;            ///< TikLicenseType.
    u8 key_generation;          ///< NcaKeyGeneration.
    u16 property_mask;          ///< TikPropertyMask.
    u8 reserved[0x8];
    u64 ticket_id;
    u64 device_id;
    FsRightsId rights_id;
    u32 account_id;
    u32 sect_total_size;
    u32 sect_hdr_offset;
    u16 sect_hdr_count;
    u16 sect_hdr_entry_size;
} TikCommonBlock;

NXDT_ASSERT(TikCommonBlock, 0x180);

/// ESV1/ESV2 section records are placed right after the ticket data. These aren't available in TikTitleKeyType_Common tickets.
/// These are only used if the sect_* fields from the common block are non-zero (other than 'sect_hdr_offset').
/// Each ESV2 section record is followed by a 'record_count' number of ESV1 records, each one of 'record_size' size.

typedef enum {
    TikSectionType_Permanent          = 1,
    TikSectionType_Subscription       = 2,
    TikSectionType_Content            = 3,
    TikSectionType_ContentConsumption = 4,
    TikSectionType_AccessTitle        = 5,
    TikSectionType_LimitedResource    = 6
} TikSectionType;

typedef struct {
    u32 sect_offset;
    u32 record_size;
    u32 section_size;
    u16 record_count;
    u16 section_type;   ///< TikSectionType.
} TikESV2SectionRecord;

/// Used with TikSectionType_Permanent.
typedef struct {
    u8 ref_id[0x10];
    u32 ref_id_attr;
} TikESV1PermanentRecord;

/// Used with TikSectionType_Subscription.
typedef struct {
    u32 limit;
    u8 ref_id[0x10];
    u32 ref_id_attr;
} TikESV1SubscriptionRecord;

/// Used with TikSectionType_Content.
typedef struct {
    u32 offset;
    u8 access_mask[0x80];
} TikESV1ContentRecord;

/// Used with TikSectionType_ContentConsumption.
typedef struct {
    u16 index;
    u16 code;
    u32 limit;
} TikESV1ContentConsumptionRecord;

/// Used with TikSectionType_AccessTitle.
typedef struct {
    u64 access_title_id;
    u64 access_title_mask;
} TikESV1AccessTitleRecord;

/// Used with TikSectionType_LimitedResource.
typedef struct {
    u32 limit;
    u8 ref_id[0x10];
    u32 ref_id_attr;
} TikESV1LimitedResourceRecord;

/// All tickets generated below use a little endian sig_type field.
GENERATE_TIK_STRUCT(Rsa4096, 0x3C0);    /// RSA-4096 signature.
GENERATE_TIK_STRUCT(Rsa2048, 0x2C0);    /// RSA-2048 signature.
GENERATE_TIK_STRUCT(Ecc480, 0x200);     /// ECC signature.
GENERATE_TIK_STRUCT(Hmac160, 0x1C0);    /// HMAC signature.

/// Ticket type.
typedef enum {
    TikType_None        = 0,
    TikType_SigRsa4096  = 1,
    TikType_SigRsa2048  = 2,
    TikType_SigEcc480   = 3,
    TikType_SigHmac160  = 4
} TikType;

/// Used to store ticket type, size and raw data, as well as titlekey data.
typedef struct {
    u8 type;                        ///< TikType.
    u64 size;                       ///< Raw ticket size.
    u8 data[SIGNED_TIK_MAX_SIZE];   ///< Raw ticket data.
    u8 enc_titlekey[0x10];          ///< Titlekey with titlekek crypto (RSA-OAEP unwrapped if dealing with a TikTitleKeyType_Personalized ticket).
    u8 dec_titlekey[0x10];          ///< Titlekey without titlekek crypto. Ready to use for NCA FS section decryption.
    char rights_id_str[0x21];       ///< Character string representation of the rights ID from the ticket.
} Ticket;

/// Retrieves a ticket from either the ES ticket system savedata file (eMMC BIS System partition) or the secure hash FS partition from an inserted gamecard, using a Rights ID value.
/// Titlekey is also RSA-OAEP unwrapped (if needed) and titlekek decrypted right away.
bool tikRetrieveTicketByRightsId(Ticket *dst, const FsRightsId *id, bool use_gamecard);

/// Converts a TikTitleKeyType_Personalized ticket into a TikTitleKeyType_Common ticket and generates a raw certificate chain for the new signature issuer.
/// Bear in mind the 'size' member from the Ticket parameter will be updated by this function to remove any possible references to ESV1/ESV2 records.
/// Raw certificate chain data will be saved to the provided pointers. certGenerateRawCertificateChainBySignatureIssuer() is used internally, so the output buffer must be freed by the user.
bool tikConvertPersonalizedTicketToCommonTicket(Ticket *tik, u8 **out_raw_cert_chain, u64 *out_raw_cert_chain_size);

/// Helper inline functions.

NX_INLINE TikCommonBlock *tikGetCommonBlock(void *buf)
{
    return (TikCommonBlock*)signatureGetPayload(buf, false);
}

NX_INLINE u64 tikGetTicketSectionRecordsBlockSize(TikCommonBlock *tik_common_block)
{
    if (!tik_common_block) return 0;
    
    u64 offset = sizeof(TikCommonBlock), out_size = 0;
    
    for(u32 i = 0; i < tik_common_block->sect_hdr_count; i++)
    {
        TikESV2SectionRecord *rec = (TikESV2SectionRecord*)((u8*)tik_common_block + offset);
        offset += (sizeof(TikESV2SectionRecord) + ((u64)rec->record_count * (u64)rec->record_size));
        out_size += offset;
    }
    
    return out_size;
}

NX_INLINE bool tikIsValidTicket(void *buf)
{
    u64 ticket_size = (signatureGetBlockSize(signatureGetSigType(buf, false)) + sizeof(TikCommonBlock));
    return (ticket_size > sizeof(TikCommonBlock) && (ticket_size + tikGetTicketSectionRecordsBlockSize(tikGetCommonBlock(buf))) <= SIGNED_TIK_MAX_SIZE);
}

NX_INLINE u64 tikGetSignedTicketSize(void *buf)
{
    return (tikIsValidTicket(buf) ? (signatureGetBlockSize(signatureGetSigType(buf, false)) + sizeof(TikCommonBlock) + tikGetTicketSectionRecordsBlockSize(tikGetCommonBlock(buf))) : 0);
}

NX_INLINE u64 tikGetSignedTicketHashAreaSize(void *buf)
{
    return (tikIsValidTicket(buf) ? (sizeof(TikCommonBlock) + tikGetTicketSectionRecordsBlockSize(tikGetCommonBlock(buf))) : 0);
}

NX_INLINE bool tikIsPersonalizedTicket(Ticket *tik)
{
    TikCommonBlock *tik_common_block = tikGetCommonBlock(tik->data);
    return (tik_common_block != NULL && tik_common_block->titlekey_type == TikTitleKeyType_Personalized);
}

#ifdef __cplusplus
}
#endif

#endif /* __TIK_H__ */
