/*
 * nacp.h
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

#ifndef __NACP_H__
#define __NACP_H__

#include "romfs.h"

#define NACP_MAX_ICON_SIZE  0x20000 /* 128 KiB. */

typedef struct {
    char name[0x200];
    char publisher[0x100];
} NacpTitle;

typedef enum {
    NacpStartupUserAccount_None                                       = 0,
    NacpStartupUserAccount_Required                                   = 1,
    NacpStartupUserAccount_RequiredWithNetworkServiceAccountAvailable = 2,
    NacpStartupUserAccount_Count                                      = 3   ///< Not a real value.
} NacpStartupUserAccount;

typedef enum {
    NacpUserAccountSwitchLock_Disable = 0,
    NacpUserAccountSwitchLock_Enable  = 1,
    NacpUserAccountSwitchLock_Count   = 2   ///< Not a real value.
} NacpUserAccountSwitchLock;

typedef enum {
    NacpAddOnContentRegistrationType_AllOnLaunch = 0,
    NacpAddOnContentRegistrationType_OnDemand    = 1,
    NacpAddOnContentRegistrationType_Count       = 2    ///< Not a real value.
} NacpAddOnContentRegistrationType;

/// Indexes used to access NACP attribute info.
typedef enum {
    NacpAttribute_Demo                     = 0,
    NacpAttribute_RetailInteractiveDisplay = 1,
    NacpAttribute_Count                    = 2  ///< Not a real value.
} NacpAttribute;

typedef struct {
    u32 NacpAttributeFlag_Demo                     : 1;
    u32 NacpAttributeFlag_RetailInteractiveDisplay : 1;
    u32 NacpAttributeFlag_Reserved                 : 30;
} NacpAttributeFlag;

/// Indexes used to access NACP language info.
typedef enum {
    NacpLanguage_AmericanEnglish      = 0,
    NacpLanguage_BritishEnglish       = 1,
    NacpLanguage_Japanese             = 2,
    NacpLanguage_French               = 3,
    NacpLanguage_German               = 4,
    NacpLanguage_LatinAmericanSpanish = 5,
    NacpLanguage_Spanish              = 6,
    NacpLanguage_Italian              = 7,
    NacpLanguage_Dutch                = 8,
    NacpLanguage_CanadianFrench       = 9,
    NacpLanguage_Portuguese           = 10,
    NacpLanguage_Russian              = 11,
    NacpLanguage_Korean               = 12,
    NacpLanguage_TraditionalChinese   = 13,
    NacpLanguage_SimplifiedChinese    = 14,
    NacpLanguage_BrazilianPortuguese  = 15,
    NacpLanguage_Count                = 16  ///< Not a real value.
} NacpLanguage;

typedef struct {
    u32 NacpSupportedLanguageFlag_AmericanEnglish      : 1;
    u32 NacpSupportedLanguageFlag_BritishEnglish       : 1;
    u32 NacpSupportedLanguageFlag_Japanese             : 1;
    u32 NacpSupportedLanguageFlag_French               : 1;
    u32 NacpSupportedLanguageFlag_German               : 1;
    u32 NacpSupportedLanguageFlag_LatinAmericanSpanish : 1;
    u32 NacpSupportedLanguageFlag_Spanish              : 1;
    u32 NacpSupportedLanguageFlag_Italian              : 1;
    u32 NacpSupportedLanguageFlag_Dutch                : 1;
    u32 NacpSupportedLanguageFlag_CanadianFrench       : 1;
    u32 NacpSupportedLanguageFlag_Portuguese           : 1;
    u32 NacpSupportedLanguageFlag_Russian              : 1;
    u32 NacpSupportedLanguageFlag_Korean               : 1;
    u32 NacpSupportedLanguageFlag_TraditionalChinese   : 1;     ///< Old: NacpSupportedLanguageFlag_Taiwanese.
    u32 NacpSupportedLanguageFlag_SimplifiedChinese    : 1;     ///< Old: NacpSupportedLanguageFlag_Chinese.
    u32 NacpSupportedLanguageFlag_BrazilianPortuguese  : 1;
    u32 NacpSupportedLanguageFlag_Reserved             : 16;
} NacpSupportedLanguageFlag;

/// Indexes used to access NACP parental control info.
typedef enum {
    NacpParentalControl_FreeCommunication = 0,
    NacpParentalControl_Count             = 1   ///< Not a real value.
} NacpParentalControl;

typedef struct {
    u32 NacpParentalControlFlag_FreeCommunication : 1;
    u32 NacpParentalControlFlag_Reserved          : 31;
} NacpParentalControlFlag;

typedef enum {
    NacpScreenshot_Allow = 0,
    NacpScreenshot_Deny  = 1,
    NacpScreenshot_Count = 2    ///< Not a real value.
} NacpScreenshot;

typedef enum {
    NacpVideoCapture_Disable = 0,
    NacpVideoCapture_Manual  = 1,
    NacpVideoCapture_Enable  = 2,
    NacpVideoCapture_Count   = 3,                           ///< Not a real value.
    
    /// Old.
    NacpVideoCapture_Deny    = NacpVideoCapture_Disable,
    NacpVideoCapture_Allow   = NacpVideoCapture_Manual
} NacpVideoCapture;

typedef enum {
    NacpDataLossConfirmation_None     = 0,
    NacpDataLossConfirmation_Required = 1,
    NacpDataLossConfirmation_Count    = 2   ///< Not a real value.
} NacpDataLossConfirmation;

typedef enum {
    NacpPlayLogPolicy_Open    = 0,
    NacpPlayLogPolicy_LogOnly = 1,
    NacpPlayLogPolicy_None    = 2,
    NacpPlayLogPolicy_Closed  = 3,
    NacpPlayLogPolicy_Count   = 4,                      ///< Not a real value.
    
    /// Old.
    NacpPlayLogPolicy_All     = NacpPlayLogPolicy_Open
} NacpPlayLogPolicy;

/// Indexes used to access NACP rating age info.
typedef enum {
    NacpRatingAgeOrganization_CERO         = 0,
    NacpRatingAgeOrganization_GRACGCRB     = 1,
    NacpRatingAgeOrganization_GSRMR        = 2,
    NacpRatingAgeOrganization_ESRB         = 3,
    NacpRatingAgeOrganization_ClassInd     = 4,
    NacpRatingAgeOrganization_USK          = 5,
    NacpRatingAgeOrganization_PEGI         = 6,
    NacpRatingAgeOrganization_PEGIPortugal = 7,
    NacpRatingAgeOrganization_PEGIBBFC     = 8,
    NacpRatingAgeOrganization_Russian      = 9,
    NacpRatingAgeOrganization_ACB          = 10,
    NacpRatingAgeOrganization_OFLC         = 11,
    NacpRatingAgeOrganization_IARCGeneric  = 12,
    NacpRatingAgeOrganization_Count        = 13
} NacpRatingAgeOrganization;

typedef struct {
    u8 cero;
    u8 gracgcrb;
    u8 gsrmr;
    u8 esrb;
    u8 class_ind;
    u8 usk;
    u8 pegi;
    u8 pegi_portugal;
    u8 pegibbfc;
    u8 russian;
    u8 acb;
    u8 oflc;
    u8 iarc_generic;
    u8 reserved[0x13];
} NacpRatingAge;

typedef enum {
    NacpLogoType_LicensedByNintendo    = 0,
    NacpLogoType_DistributedByNintendo = 1, ///< Removed.
    NacpLogoType_Nintendo              = 2
} NacpLogoType;

typedef enum {
    NacpLogoHandling_Auto   = 0,
    NacpLogoHandling_Manual = 1
} NacpLogoHandling;

typedef enum {
    NacpRuntimeAddOnContentInstall_Deny                                       = 0,
    NacpRuntimeAddOnContentInstall_AllowAppend                                = 1,
    NacpRuntimeAddOnContentInstall_AllowAppendButDontDownloadWhenUsingNetwork = 2
} NacpRuntimeAddOnContentInstall;

typedef enum {
    NacpRuntimeParameterDelivery_Always                   = 0,
    NacpRuntimeParameterDelivery_AlwaysIfUserStateMatched = 1,
    NacpRuntimeParameterDelivery_OnRestart                = 2
} NacpRuntimeParameterDelivery;

typedef enum {
    NacpCrashReport_Deny  = 0,
    NacpCrashReport_Allow = 1
} NacpCrashReport;

typedef enum {
    NacpHdcp_None     = 0,
    NacpHdcp_Required = 1
} NacpHdcp;

typedef struct {
    u8 NacpStartupUserAccountOption_IsOptional : 1;
    u8 NacpStartupUserAccountOption_Reserved   : 7;
} NacpStartupUserAccountOption;

typedef enum {
    NacpPlayLogQueryCapability_None      = 0,
    NacpPlayLogQueryCapability_WhiteList = 1,
    NacpPlayLogQueryCapability_All       = 2
} NacpPlayLogQueryCapability;

typedef struct {
    u8 NacpRepairFlag_SuppressGameCardAccess : 1;
    u8 NacpRepairFlag_Reserved               : 7;
} NacpRepairFlag;

typedef struct {
    u8 NacpRequiredNetworkServiceLicenseOnLaunchFlag_Common   : 1;
    u8 NacpRequiredNetworkServiceLicenseOnLaunchFlag_Reserved : 7;
} NacpRequiredNetworkServiceLicenseOnLaunchFlag;

typedef enum {
	NacpJitConfigurationFlag_None    = 0,
	NacpJitConfigurationFlag_Enabled = 1
} NacpJitConfigurationFlag;

typedef struct {
    u64 jit_configuration_flag; ///< NacpJitConfigurationFlag.
    u64 memory_size;
} NacpJitConfiguration;

typedef struct {
    u16 NacpDescriptors_Index       : 15;
    u16 NacpDescriptors_ContinueSet : 1;    ///< Not given a name by Nintendo.
} NacpDescriptors;

typedef struct {
    NacpDescriptors descriptors[0x20];
} NacpRequiredAddOnContentsSetBinaryDescriptor;

typedef enum {
    NacpPlayReportPermission_None            = 0,
    NacpPlayReportPermission_TargetMarketing = 1
} NacpPlayReportPermission;

typedef enum {
    NacpCrashScreenshotForProd_Deny  = 0,
    NacpCrashScreenshotForProd_Allow = 1
} NacpCrashScreenshotForProd;

typedef enum {
    NacpCrashScreenshotForDev_Deny  = 0,
    NacpCrashScreenshotForDev_Allow = 1
} NacpCrashScreenshotForDev;

typedef struct {
    NacpTitle title[0x10];
    char isbn[0x25];
    u8 startup_user_account;                                                                        ///< NacpStartupUserAccount.
    u8 user_account_switch_lock;                                                                    ///< NacpUserAccountSwitchLock. Old: touch_screen_usage (None, Supported, Required).
    u8 add_on_content_registration_type;                                                            ///< NacpAddOnContentRegistrationType.
    NacpAttributeFlag attribute_flag;
    NacpSupportedLanguageFlag supported_language_flag;
    NacpParentalControlFlag parental_control_flag;
    u8 screenshot;                                                                                  ///< NacpScreenshot.
    u8 video_capture;                                                                               ///< NacpVideoCapture.
    u8 data_loss_confirmation;                                                                      ///< NacpDataLossConfirmation.
    u8 play_log_policy;                                                                             ///< NacpPlayLogPolicy.
    u64 presence_group_id;
    NacpRatingAge rating_age;
    char display_version[0x10];
    u64 add_on_content_base_id;
    u64 save_data_owner_id;
    u64 user_account_save_data_size;
    u64 user_account_save_data_journal_size;
    u64 device_save_data_size;
    u64 device_save_data_journal_size;
    u64 bcat_delivery_cache_storage_size;
    char application_error_code_category[0x8];
    u64 local_communication_id[8];
    u8 logo_type;                                                                                   ///< NacpLogoType.
    u8 logo_handling;                                                                               ///< NacpLogoHandling.
    u8 runtime_add_on_content_install;                                                              ///< NacpRuntimeAddOnContentInstall.
    u8 runtime_parameter_delivery;                                                                  ///< NacpRuntimeParameterDelivery.
    u8 reserved_1[0x2];
    u8 crash_report;                                                                                ///< NacpCrashReport.
    u8 hdcp;                                                                                        ///< NacpHdcp.
    u64 seed_for_pseudo_device_id;
    char bcat_passphrase[0x41];
    NacpStartupUserAccountOption startup_user_account_option;
    u8 reserved_2[0x6];
    u64 user_account_save_data_size_max;
    u64 user_account_save_data_journal_size_max;
    u64 device_save_data_size_max;
    u64 device_save_data_journal_size_max;
    u64 temporary_storage_size;
    u64 cache_storage_size;
    u64 cache_storage_journal_size;
    u64 cache_storage_data_and_journal_size_max;
    u16 cache_storage_index_max;
    u8 reserved_3[0x6];
    u64 play_log_queryable_application_id[0x10];
    u8 play_log_query_capability;                                                                   ///< NacpPlayLogQueryCapability.
    NacpRepairFlag repair_flag;
    u8 program_index;
    NacpRequiredNetworkServiceLicenseOnLaunchFlag required_network_service_license_on_launch_flag;
    u8 reserved_4[0x4];
    NacpNeighborDetectionClientConfiguration neighbor_detection_client_configuration;
    NacpJitConfiguration jit_configuration;
    NacpRequiredAddOnContentsSetBinaryDescriptor required_add_on_contents_set_binary_descriptor;
    u8 play_report_permission;                                                                      ///< NacpPlayReportPermission.
    u8 crash_screenshot_for_prod;                                                                   ///< NacpCrashScreenshotForProd.
    u8 crash_screenshot_for_dev;                                                                    ///< NacpCrashScreenshotForDev.
    u8 reserved_5[0xBFD];
} _NacpStruct;

typedef struct {
    u8 language;    ///< NacpLanguage.
    u64 icon_size;  ///< JPG icon size. Must not exceed NACP_MAX_ICON_SIZE.
    u8 *icon_data;  ///< Pointer to a dynamically allocated buffer that holds the JPG icon data.
} NacpIconContext;

typedef struct {
    NcaContext *nca_ctx;                        ///< Pointer to the NCA context for the Control NCA from which NACP data is retrieved.
    RomFileSystemContext romfs_ctx;             ///< RomFileSystemContext for the Control NCA FS section #0, which is where the NACP is stored.
    RomFileSystemFileEntry *romfs_file_entry;   ///< RomFileSystemFileEntry for the NACP in the Control NCA FS section #0. Used to generate a RomFileSystemFileEntryPatch if needed.
    RomFileSystemFileEntryPatch nca_patch;      ///< RomFileSystemFileEntryPatch generated if NACP modifications are needed. Used to seamlessly replace Control NCA data while writing it.
                                                ///< Bear in mind that generating a patch modifies the NCA context.
    _NacpStruct *data;                          ///< Pointer to a dynamically allocated buffer that holds the full NACP.
    u8 data_hash[SHA256_HASH_SIZE];             ///< SHA-256 checksum calculated over the whole NACP. Used to determine if NcaHierarchicalSha256Patch generation is truly needed.
    u32 icon_count;                             ///< NACP icon count. May be zero if no icons are available.
    NacpIconContext *icon_ctx;                  ///< Pointer to a dynamically allocated buffer that holds 'icon_count' NACP icon contexts. May be NULL if no icons are available.
    char *authoring_tool_xml;                   ///< Pointer to a dynamically allocated, NULL-terminated buffer that holds AuthoringTool-like XML data. 
                                                ///< This is always NULL unless nacpGenerateAuthoringToolXml() is used on this NacpContext.
    u64 authoring_tool_xml_size;                ///< Size for the AuthoringTool-like XML. This is essentially the same as using strlen() on 'authoring_tool_xml'.
                                                ///< This is always 0 unless nacpGenerateAuthoringToolXml() is used on this NacpContext.
} NacpContext;

/// Initializes a NacpContext using a previously initialized NcaContext (which must belong to a Control NCA).
bool nacpInitializeContext(NacpContext *out, NcaContext *nca_ctx);

/// Generates an AuthoringTool-like XML using information from a previously initialized NacpContext.
/// If the function succeeds, XML data and size will get saved to the 'authoring_tool_xml' and 'authoring_tool_xml_size' members from the NacpContext.
bool nacpGenerateAuthoringToolXml(NacpContext *nacp_ctx);














/// Returns a pointer to a string holding the name of the provided NacpLanguage value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetLanguageString(u8 language);

/// Returns a pointer to a string holding the name of the provided NacpStartupUserAccount value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetStartupUserAccountString(u8 startup_user_account);

/// Returns a pointer to a string holding the name of the provided NacpUserAccountSwitchLock value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetUserAccountSwitchLockString(u8 user_account_switch_lock);

/// Returns a pointer to a string holding the name of the provided NacpAddOnContentRegistrationType value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetAddOnContentRegistrationTypeString(u8 add_on_content_registration_type);

/// Returns a pointer to a string holding the name of the provided NacpAttribute value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetAttributeString(u8 attribute);

/// Returns a pointer to a string holding the name of the provided NacpParentalControl value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetParentalControlString(u8 parental_control);

/// Returns a pointer to a string holding the name of the provided NacpScreenshot value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetScreenshotString(u8 screenshot);

/// Returns a pointer to a string holding the name of the provided NacpVideoCapture value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetVideoCaptureString(u8 video_capture);

/// Returns a pointer to a string holding the name of the provided NacpDataLossConfirmation value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetDataLossConfirmationString(u8 data_loss_confirmation);

/// Returns a pointer to a string holding the name of the provided NacpPlayLogPolicy value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetPlayLogPolicyString(u8 play_log_policy);

/// Returns a pointer to a string holding the name of the provided NacpRatingAgeOrganization value. Returns "Unknown" if the provided value is invalid.
const char *nacpGetRatingAgeOrganizationString(u8 rating_age_organization);


















/// Helper inline functions.

NX_INLINE void nacpFreeContext(NacpContext *nacp_ctx)
{
    if (!nacp_ctx) return;
    
    romfsFreeContext(&(nacp_ctx->romfs_ctx));
    romfsFreeFileEntryPatch(&(nacp_ctx->nca_patch));
    if (nacp_ctx->data) free(nacp_ctx->data);
    
    if (nacp_ctx->icon_ctx)
    {
        for(u32 i = 0; i < nacp_ctx->icon_count; i++)
        {
            if (nacp_ctx->icon_ctx[i].icon_data) free(nacp_ctx->icon_ctx[i].icon_data);
        }
        
        free(nacp_ctx->icon_ctx);
    }
    
    if (nacp_ctx->authoring_tool_xml) free(nacp_ctx->authoring_tool_xml);
    memset(nacp_ctx, 0, sizeof(NacpContext));
}

NX_INLINE bool nacpIsValidIconContext(NacpIconContext *icon_ctx)
{
    return (icon_ctx && icon_ctx->language < NacpLanguage_Count && icon_ctx->icon_size && icon_ctx->icon_data);
}

NX_INLINE bool nacpIsValidContext(NacpContext *nacp_ctx)
{
    if (!nacp_ctx || !nacp_ctx->nca_ctx || !nacp_ctx->romfs_file_entry || !nacp_ctx->data || (!nacp_ctx->icon_count && nacp_ctx->icon_ctx) || (nacp_ctx->icon_count && !nacp_ctx->icon_ctx)) return false;
    
    for(u32 i = 0; i < nacp_ctx->icon_count; i++)
    {
        if (!nacpIsValidIconContext(&(nacp_ctx->icon_ctx[i]))) return false;
    }
    
    return true;
}

NX_INLINE bool nacpIsNcaPatchRequired(NacpContext *nacp_ctx)
{
    if (!nacpIsValidContext(nacp_ctx)) return false;
    u8 tmp_hash[SHA256_HASH_SIZE] = {0};
    sha256CalculateHash(tmp_hash, nacp_ctx->data, sizeof(_NacpStruct));
    return (memcmp(tmp_hash, nacp_ctx->data_hash, SHA256_HASH_SIZE) != 0);
}

NX_INLINE bool nacpGenerateNcaPatch(NacpContext *nacp_ctx)
{
    return (nacpIsValidContext(nacp_ctx) && romfsGenerateFileEntryPatch(&(nacp_ctx->romfs_ctx), nacp_ctx->romfs_file_entry, nacp_ctx->data, sizeof(_NacpStruct), 0, &(nacp_ctx->nca_patch)));
}

NX_INLINE bool nacpCheckBitflagField(const void *flag, u8 idx, u8 max_flag_idx)
{
    return (flag && idx < 0x20 && idx < max_flag_idx && ((*((const u32*)flag) >> idx) & 0x1));
}

#endif /* __NACP_H__ */
