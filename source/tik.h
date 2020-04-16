/*
 * Copyright (c) 2020 DarkMatterCore
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
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

#include <switch.h>
#include "signature.h"

#define TIK_MAX_SIZE    0x400   /* Max ticket entry size in the ES system savefiles */
#define TIK_MIN_SIZE    0x200   /* Equivalent to sizeof(TikSigEcsda240) - assuming no ESv2 records are available */

typedef enum {
    TikType_SigRsa4096  = 0,
    TikType_SigRsa2048  = 1,
    TikType_SigEcsda240 = 2,
    TikType_Invalid     = 255
} TikType;

typedef enum {
    TikTitleKeyType_Common       = 0,
    TikTitleKeyType_Personalized = 1,
    TikTitleKeyType_Invalid      = 255
} TikTitleKeyType;

typedef enum {
    TikLicenseType_Permanent    = 0,
    TikLicenseType_Demo         = 1,
    TikLicenseType_Trial        = 2,
    TikLicenseType_Rental       = 3,
    TikLicenseType_Subscription = 4,
    TikLicenseType_Service      = 5
} TikLicenseType;

typedef struct {
    u8 preinstallation         : 1;
    u8 shared_title            : 1;
    u8 all_contents            : 1;
    u8 device_link_independent : 1;
    u8 _volatile               : 1;
    u8 elicense_required       : 1;
} TikPropertyMask;

typedef enum {
    TikSectionType_Permanent          = 1,
    TikSectionType_Subscription       = 2,
    TikSectionType_Content            = 3,
    TikSectionType_ContentConsumption = 4,
    TikSectionType_AccessTitle        = 5,
    TikSectionType_LimitedResource    = 6
} TikSectionType;

/// Placed after the ticket signature block.
typedef struct {
    char issuer[0x40];
    u8 title_key_block[0x100];
    u8 format_version;
    u8 title_key_type;              ///< TikTitleKeyType.
    u16 ticket_version;
    u8 license_type;                ///< TikLicenseType.
    u8 key_generation;
    TikPropertyMask property_mask;
    u8 reserved_1[0x9];
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
    SignatureBlockRsa4096 sig_block;
    TikCommonBlock tik_common_blk;
} TikSigRsa4096;

typedef struct {
    SignatureBlockRsa2048 sig_block;
    TikCommonBlock tik_common_blk;
} TikSigRsa2048;

typedef struct {
    SignatureBlockEcsda240 sig_block;
    TikCommonBlock tik_common_blk;
} TikSigEcsda240;

/// Section records are placed right after the ticket data. These aren't available in TikTitleKeyType_Common tickets.
/// These are only used if the sect_* fields are non-zero (other than 'sect_hdr_offset').
/// Each section record is followed by a 'record_count' number of Esv1 records, each one of 'record_size' size.
typedef struct {
    u32 sect_offset;
    u32 record_size;
    u32 section_size;
    u16 record_count;
    u16 section_type;   ///< TikSectionType.
} TikEsv2SectionRecord;

/// Used to store ticket type, size and raw data.
typedef struct {
    u8 type;                ///< TikType.
    u64 size;
    u8 data[TIK_MAX_SIZE];
} Ticket;

TikCommonBlock *tikGetTicketCommonBlockFromMemoryBuffer(void *data);

static inline TikCommonBlock *tikGetTicketCommonBlockFromTicket(Ticket *tik)
{
    if (!tik || tik->type > TikType_SigEcsda240 || tik->size < TIK_MIN_SIZE) return NULL;
    return tikGetTicketCommonBlockFromMemoryBuffer(tik->data);
}

bool tikRetrieveTicketByRightsId(Ticket *dst, const FsRightsId *id);

bool tikGetTitleKeyFromTicketCommonBlock(void *dst, TikCommonBlock *tik_common_blk);

static inline bool tikGetTitleKeyFromTicket(void *dst, Ticket *tik)
{
    if (!dst || !tik || tik->type > TikType_SigEcsda240 || tik->size < TIK_MIN_SIZE) return false;
    return tikGetTitleKeyFromTicketCommonBlock(dst, tikGetTicketCommonBlockFromTicket(tik));
}

bool tikGetTitleKekDecryptedTitleKey(void *dst, const void *src, u8 key_generation);

bool tikGetTitleKekDecryptedTitleKeyFromTicket(void *dst, Ticket *tik);

/// This will convert a TikTitleKeyType_Personalized ticket into a TikTitleKeyType_Common ticket.
/// Use the output titlekey from tikGetTitleKeyFromTicket() / tikGetTitleKeyFromTicketCommonBlock() as the second parameter for this function.
/// Bear in mind the 'size' member from the Ticket parameter will be updated by this function to remove any possible references to TikEsv2SectionRecord records.
void tikConvertPersonalizedTicketToCommonTicket(Ticket *tik, const void *titlekey);

#endif /* __TIK_H__ */
