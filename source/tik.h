/*
 * tik.h
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

#pragma once

#ifndef __TIK_H__
#define __TIK_H__

#include "signature.h"

#define SIGNED_TIK_MAX_SIZE         0x400                   /* Max ticket entry size in the ES ticket system savedata file. */
#define SIGNED_TIK_MIN_SIZE         sizeof(TikSigHmac160)   /* Assuming no ESV1/ESV2 records are available. */

typedef enum {
    TikType_None        = 0,
    TikType_SigRsa4096  = 1,
    TikType_SigRsa2048  = 2,
    TikType_SigEcc480   = 3,
    TikType_SigHmac160  = 4
} TikType;

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
    TikPropertyMask_Volatile             = BIT(4),
    TikPropertyMask_ELicenseRequired     = BIT(5)
} TikPropertyMask;

/// Placed after the ticket signature block.
typedef struct {
    char issuer[0x40];
    u8 titlekey_block[0x100];
    u8 format_version;
    u8 titlekey_type;               ///< TikTitleKeyType.
    u16 ticket_version;
    u8 license_type;                ///< TikLicenseType.
    u8 key_generation;
    u16 property_mask;              ///< TikPropertyMask.
    u8 reserved_1[0x8];
    u64 ticket_id;
    u64 device_id;
    FsRightsId rights_id;
    u32 account_id;
    u32 sect_total_size;
    u32 sect_hdr_offset;
    u16 sect_hdr_count;
    u16 sect_hdr_entry_size;
} TikCommonBlock;

typedef struct {
    SignatureBlockRsa4096 sig_block;    ///< sig_type field is stored using little endian byte order.
    TikCommonBlock tik_common_block;
} TikSigRsa4096;

typedef struct {
    SignatureBlockRsa2048 sig_block;    ///< sig_type field is stored using little endian byte order.
    TikCommonBlock tik_common_block;
} TikSigRsa2048;

typedef struct {
    SignatureBlockEcc480 sig_block;     ///< sig_type field is stored using little endian byte order.
    TikCommonBlock tik_common_block;
} TikSigEcc480;

typedef struct {
    SignatureBlockHmac160 sig_block;    ///< sig_type field is stored using little endian byte order.
    TikCommonBlock tik_common_block;
} TikSigHmac160;

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

/// Used to store ticket type, size and raw data, as well as titlekey data.
typedef struct {
    u8 type;                        ///< TikType.
    u64 size;                       ///< Raw ticket size.
    u8 data[SIGNED_TIK_MAX_SIZE];   ///< Raw ticket data.
    u8 enc_titlekey[0x10];          ///< Titlekey with titlekek crypto (RSA-OAEP unwrapped if dealing with a TikTitleKeyType_Personalized ticket).
    u8 dec_titlekey[0x10];          ///< Titlekey without titlekek crypto. Ready to use for NCA FS section decryption.
} Ticket;

/// Retrieves a ticket from either the ES ticket system savedata file (eMMC BIS System partition) or the secure hash FS partition from an inserted gamecard, using a Rights ID value.
/// Titlekey is also RSA-OAEP unwrapped (if needed) and titlekek decrypted right away.
bool tikRetrieveTicketByRightsId(Ticket *dst, const FsRightsId *id, bool use_gamecard);

/// This will convert a TikTitleKeyType_Personalized ticket into a TikTitleKeyType_Common ticket.
/// Bear in mind the 'size' member from the Ticket parameter will be updated by this function to remove any possible references to ESV1/ESV2 records.
void tikConvertPersonalizedTicketToCommonTicket(Ticket *tik);

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

#endif /* __TIK_H__ */
