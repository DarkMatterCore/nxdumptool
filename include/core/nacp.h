/*
 * nacp.h
 *
 * Copyright (c) 2020-2026, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __NACP_H__
#define __NACP_H__

#include "romfs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NACP_MAX_ICON_SIZE  0x20000 /* 128 KiB. */

/// Indexes used to access NACP Title structs.
typedef enum : u8 {
    NacpLanguage_AmericanEnglish        = 0,
    NacpLanguage_BritishEnglish         = 1,
    NacpLanguage_Japanese               = 2,
    NacpLanguage_French                 = 3,
    NacpLanguage_German                 = 4,
    NacpLanguage_LatinAmericanSpanish   = 5,
    NacpLanguage_Spanish                = 6,
    NacpLanguage_Italian                = 7,
    NacpLanguage_Dutch                  = 8,
    NacpLanguage_CanadianFrench         = 9,
    NacpLanguage_Portuguese             = 10,
    NacpLanguage_Russian                = 11,
    NacpLanguage_Korean                 = 12,
    NacpLanguage_TraditionalChinese     = 13,
    NacpLanguage_SimplifiedChinese      = 14,
    NacpLanguage_BrazilianPortuguese    = 15,
    NacpLanguage_Polish                 = 16,
    NacpLanguage_Thai                   = 17,
    NacpLanguage_Count                  = 18,                                 ///< Total values supported by this enum.

    /// Old.
    NacpLanguage_Taiwanese              = NacpLanguage_TraditionalChinese,
    NacpLanguage_Chinese                = NacpLanguage_SimplifiedChinese,

    /// Used exclusively for NacpTitles.
    NacpLanguage_Format0EntryCount      = 16,
    NacpLanguage_Format1EntryCount      = 32
} NacpLanguage;

typedef struct {
    char name[0x200];
    char publisher[0x100];
} NacpTitle;

NXDT_ASSERT(NacpTitle, 0x300);

typedef struct {
    u16 data_size;      ///< Size of the Zlib-compressed blob within 'data'.
    u8 data[0x2FFE];    ///< Zlib-compressed blob, using wbits=-15.
} NacpTitleFormat1;

NXDT_ASSERT(NacpTitleFormat1, 0x3000);

typedef struct {
    union {
        NacpTitle format0[NacpLanguage_Format0EntryCount];  // Used if titles_data_format is set to NacpTitlesDataFormat_Format0.
        NacpTitleFormat1 format1;                           // Used if titles_data_format is set to NacpTitlesDataFormat_Format1. Supports twice as many title entries.
    };
} NacpTitleBlock;

NXDT_ASSERT(NacpTitleBlock, 0x3000);

typedef enum : u8 {
    NacpStartupUserAccount_None                                       = 0,
    NacpStartupUserAccount_Required                                   = 1,
    NacpStartupUserAccount_RequiredWithNetworkServiceAccountAvailable = 2,
    NacpStartupUserAccount_Count                                      = 3   ///< Total values supported by this enum.
} NacpStartupUserAccount;

typedef enum : u8 {
    NacpUserAccountSwitchLock_Disable = 0,
    NacpUserAccountSwitchLock_Enable  = 1,
    NacpUserAccountSwitchLock_Count   = 2,  ///< Total values supported by this enum.

    // Old.
    NacpTouchScreenUsage_None         = 0,
    NacpTouchScreenUsage_Supported    = 1,
    NacpTouchScreenUsage_Required     = 2
} NacpUserAccountSwitchLock;

typedef enum : u8 {
    NacpAddOnContentRegistrationType_AllOnLaunch = 0,
    NacpAddOnContentRegistrationType_OnDemand    = 1,
    NacpAddOnContentRegistrationType_Count       = 2    ///< Total values supported by this enum.
} NacpAddOnContentRegistrationType;

typedef enum : u32 {
    NacpAttribute_None                     = 0,
    NacpAttribute_Demo                     = BIT(0),
    NacpAttribute_RetailInteractiveDisplay = BIT(1),
    NacpAttribute_DownloadPlay             = BIT(2),    ///< Removed.
    NacpAttribute_Count                    = 3          ///< Total values supported by this enum.
} NacpAttribute;

typedef enum : u32 {
    NacpSupportedLanguage_None                 = 0,
    NacpSupportedLanguage_AmericanEnglish      = BIT(NacpLanguage_AmericanEnglish),
    NacpSupportedLanguage_BritishEnglish       = BIT(NacpLanguage_BritishEnglish),
    NacpSupportedLanguage_Japanese             = BIT(NacpLanguage_Japanese),
    NacpSupportedLanguage_French               = BIT(NacpLanguage_French),
    NacpSupportedLanguage_German               = BIT(NacpLanguage_German),
    NacpSupportedLanguage_LatinAmericanSpanish = BIT(NacpLanguage_LatinAmericanSpanish),
    NacpSupportedLanguage_Spanish              = BIT(NacpLanguage_Spanish),
    NacpSupportedLanguage_Italian              = BIT(NacpLanguage_Italian),
    NacpSupportedLanguage_Dutch                = BIT(NacpLanguage_Dutch),
    NacpSupportedLanguage_CanadianFrench       = BIT(NacpLanguage_CanadianFrench),
    NacpSupportedLanguage_Portuguese           = BIT(NacpLanguage_Portuguese),
    NacpSupportedLanguage_Russian              = BIT(NacpLanguage_Russian),
    NacpSupportedLanguage_Korean               = BIT(NacpLanguage_Korean),
    NacpSupportedLanguage_TraditionalChinese   = BIT(NacpLanguage_TraditionalChinese),
    NacpSupportedLanguage_SimplifiedChinese    = BIT(NacpLanguage_SimplifiedChinese),
    NacpSupportedLanguage_BrazilianPortuguese  = BIT(NacpLanguage_BrazilianPortuguese),
    NacpSupportedLanguage_Polish               = BIT(NacpLanguage_Polish),
    NacpSupportedLanguage_Thai                 = BIT(NacpLanguage_Thai),
    NacpSupportedLanguage_Count                = NacpLanguage_Count,                        ///< Total values supported by this enum.

    ///< Old.
    NacpSupportedLanguage_Taiwanese            = NacpSupportedLanguage_TraditionalChinese,
    NacpSupportedLanguage_Chinese              = NacpSupportedLanguage_SimplifiedChinese
} NacpSupportedLanguage;

typedef enum : u32 {
    NacpParentalControl_None              = 0,
    NacpParentalControl_FreeCommunication = BIT(0),
    NacpParentalControl_Count             = 1       ///< Total values supported by this enum.
} NacpParentalControl;

typedef enum : u8 {
    NacpScreenshot_Allow = 0,
    NacpScreenshot_Deny  = 1,
    NacpScreenshot_Count = 2    ///< Total values supported by this enum.
} NacpScreenshot;

typedef enum : u8 {
    NacpVideoCapture_Disable = 0,
    NacpVideoCapture_Manual  = 1,
    NacpVideoCapture_Enable  = 2,
    NacpVideoCapture_Count   = 3,                           ///< Total values supported by this enum.

    /// Old.
    NacpVideoCapture_Deny    = NacpVideoCapture_Disable,
    NacpVideoCapture_Allow   = NacpVideoCapture_Manual
} NacpVideoCapture;

typedef enum : u8 {
    NacpDataLossConfirmation_None     = 0,
    NacpDataLossConfirmation_Required = 1,
    NacpDataLossConfirmation_Count    = 2   ///< Total values supported by this enum.
} NacpDataLossConfirmation;

typedef enum : u8 {
    NacpPlayLogPolicy_Open    = 0,
    NacpPlayLogPolicy_LogOnly = 1,
    NacpPlayLogPolicy_None    = 2,
    NacpPlayLogPolicy_Closed  = 3,
    NacpPlayLogPolicy_Count   = 4,                      ///< Total values supported by this enum.

    /// Old.
    NacpPlayLogPolicy_All     = NacpPlayLogPolicy_Open
} NacpPlayLogPolicy;

/// Indexes used to access NACP RatingAge info.
typedef enum : u8 {
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
    NacpRatingAgeOrganization_Count        = 13     ///< Total values supported by this enum.
} NacpRatingAgeOrganization;

typedef struct {
    s8 cero;
    s8 grac_gcrb;
    s8 gsrmr;
    s8 esrb;
    s8 class_ind;
    s8 usk;
    s8 pegi;
    s8 pegi_portugal;
    s8 pegi_bbfc;
    s8 russian;
    s8 acb;
    s8 oflc;
    s8 iarc_generic;
    s8 reserved[0x13];
} NacpRatingAge;

NXDT_ASSERT(NacpRatingAge, 0x20);

typedef enum : u8 {
    NacpLogoType_LicensedByNintendo    = 0,
    NacpLogoType_DistributedByNintendo = 1, ///< Removed.
    NacpLogoType_Nintendo              = 2,
    NacpLogoType_Count                 = 3  ///< Total values supported by this enum.
} NacpLogoType;

typedef enum : u8 {
    NacpLogoHandling_Auto   = 0,
    NacpLogoHandling_Manual = 1,
    NacpLogoHandling_Count  = 2     ///< Total values supported by this enum.
} NacpLogoHandling;

typedef enum : u8 {
    NacpRuntimeAddOnContentInstall_Deny                                       = 0,
    NacpRuntimeAddOnContentInstall_AllowAppend                                = 1,
    NacpRuntimeAddOnContentInstall_AllowAppendButDontDownloadWhenUsingNetwork = 2,
    NacpRuntimeAddOnContentInstall_Count                                      = 3   ///< Total values supported by this enum.
} NacpRuntimeAddOnContentInstall;

typedef enum : u8 {
    NacpRuntimeParameterDelivery_Always                   = 0,
    NacpRuntimeParameterDelivery_AlwaysIfUserStateMatched = 1,
    NacpRuntimeParameterDelivery_OnRestart                = 2,
    NacpRuntimeParameterDelivery_Count                    = 3   ///< Total values supported by this enum.
} NacpRuntimeParameterDelivery;

typedef enum : u8 {
    NacpAppropriateAgeForChina_None  = 0,
    NacpAppropriateAgeForChina_Age8  = 1,
    NacpAppropriateAgeForChina_Age12 = 2,
    NacpAppropriateAgeForChina_Age16 = 3,
    NacpAppropriateAgeForChina_Count = 4    ///< Total values supported by this enum.
} NacpAppropriateAgeForChina;

typedef enum : u8 {
    NacpUndecidedParameter75b8b_A     = 0,
    NacpUndecidedParameter75b8b_B     = 1,
    NacpUndecidedParameter75b8b_Count = 2   ///< Total values supported by this enum.
} NacpUndecidedParameter75b8b;

typedef enum : u8 {
    NacpCrashReport_Deny  = 0,
    NacpCrashReport_Allow = 1,
    NacpCrashReport_Count = 2   ///< Total values supported by this enum.
} NacpCrashReport;

typedef enum : u8 {
    NacpHdcp_None     = 0,
    NacpHdcp_Required = 1,
    NacpHdcp_Count    = 2   ///< Total values supported by this enum.
} NacpHdcp;

typedef enum : u8 {
    NacpStartupUserAccountOption_None       = 0,
    NacpStartupUserAccountOption_IsOptional = BIT(0),
    NacpStartupUserAccountOption_Count      = 1         ///< Total values supported by this enum.
} NacpStartupUserAccountOption;

typedef enum : u8 {
    NacpRuntimeUpgrade_Deny  = 0,
    NacpRuntimeUpgrade_Allow = 1,
    NacpRuntimeUpgrade_Count = 2    ///< Total values supported by this enum.
} NacpRuntimeUpgrade;

typedef enum : u32 {
    NacpSupportingLimitedApplicationLicenses_None  = 0,
    NacpSupportingLimitedApplicationLicenses_Demo  = BIT(0),
    NacpSupportingLimitedApplicationLicenses_Count = 1          ///< Total values supported by this enum.
} NacpSupportingLimitedApplicationLicenses;

typedef enum : u8 {
    NacpPlayLogQueryCapability_None      = 0,
    NacpPlayLogQueryCapability_WhiteList = 1,
    NacpPlayLogQueryCapability_All       = 2,
    NacpPlayLogQueryCapability_Count     = 3    ///< Total values supported by this enum.
} NacpPlayLogQueryCapability;

typedef enum : u8 {
    NacpRepair_None                   = 0,
    NacpRepair_SuppressGameCardAccess = BIT(0),
    NacpRepair_Count                  = 1       ///< Total values supported by this enum.
} NacpRepair;

typedef enum : u8 {
    NacpRequiredNetworkServiceLicenseOnLaunch_None   = 0,
    NacpRequiredNetworkServiceLicenseOnLaunch_Common = BIT(0),
    NacpRequiredNetworkServiceLicenseOnLaunch_Count  = 1        ///< Total values supported by this enum.
} NacpRequiredNetworkServiceLicenseOnLaunch;

typedef enum : u8 {
    NacpApplicationErrorCodePrefix_NX    = 2,
    NacpApplicationErrorCodePrefix_Ounce = 3
} NacpApplicationErrorCodePrefix;

typedef enum : u8 {
    NacpTitlesDataFormat_Format0 = 0,   ///< 16 uncompressed entries.
    NacpTitlesDataFormat_Format1 = 1,   ///< 32 Zlib-compressed (deflate) entries.
    NacpTitlesDataFormat_Count   = 2    ///< Total values supported by this enum.
} NacpTitlesDataFormat;

typedef enum : u8 {
    NacpApparentPlatform_NX    = 0,
    NacpApparentPlatform_Ounce = 1,
    NacpApparentPlatform_Count = 2  ///< Total values supported by this enum.
} NacpApparentPlatform;

typedef enum : u64 {
    NacpJitConfigurationFlag_None      = 0,
    NacpJitConfigurationFlag_IsEnabled = BITL(0),
    NacpJitConfigurationFlag_Count     = 1          ///< Total values supported by this enum.
} NacpJitConfigurationFlag;

typedef struct {
    NacpJitConfigurationFlag jit_configuration_flag;
    u64 memory_size;
} NacpJitConfiguration;

NXDT_ASSERT(NacpJitConfiguration, 0x10);

typedef enum : u16 {
    NacpRequiredAddOnContentsSetDescriptorFlag_None     = 0,
    NacpRequiredAddOnContentsSetDescriptorFlag_Continue = 1
} NacpRequiredAddOnContentsSetDescriptorFlag;

typedef struct {
    u16 index                                       : 15;
    NacpRequiredAddOnContentsSetDescriptorFlag flag : 1;
} NacpRequiredAddOnContentsSetDescriptor;

NXDT_ASSERT(NacpRequiredAddOnContentsSetDescriptor, 0x2);

typedef struct {
    NacpRequiredAddOnContentsSetDescriptor descriptors[0x20];
} NacpRequiredAddOnContentsSetBinaryDescriptor;

NXDT_ASSERT(NacpRequiredAddOnContentsSetBinaryDescriptor, 0x40);

typedef enum : u8 {
    NacpPlayReportPermission_None            = 0,
    NacpPlayReportPermission_TargetMarketing = BIT(0),
    NacpPlayReportPermission_Count           = 1        ///< Total values supported by this enum.
} NacpPlayReportPermission;

typedef enum : u8 {
    NacpCrashScreenshotForProd_Deny  = 0,
    NacpCrashScreenshotForProd_Allow = 1,
    NacpCrashScreenshotForProd_Count = 2    ///< Total values supported by this enum.
} NacpCrashScreenshotForProd;

typedef enum : u8 {
    NacpCrashScreenshotForDev_Deny  = 0,
    NacpCrashScreenshotForDev_Allow = 1,
    NacpCrashScreenshotForDev_Count = 2     ///< Total values supported by this enum.
} NacpCrashScreenshotForDev;

typedef enum : u8 {
    NacpContentsAvailabilityTransitionPolicy_NoPolicy   = 0,
    NacpContentsAvailabilityTransitionPolicy_Stable     = 1,
    NacpContentsAvailabilityTransitionPolicy_Changeable = 2,
    NacpContentsAvailabilityTransitionPolicy_Count      = 3,                                                    ///< Total values supported by this enum.

    // Old.
    NacpContentsAvailabilityTransitionPolicy_Legacy     = NacpContentsAvailabilityTransitionPolicy_NoPolicy
} NacpContentsAvailabilityTransitionPolicy;

typedef struct {
    u64 application_id[8];
} NacpAccessibleLaunchRequiredVersion;

NXDT_ASSERT(NacpAccessibleLaunchRequiredVersion, 0x40);

typedef struct {
    u8 priority;
    u8 reserved_1[0x7];
    u16 aoc_index;
    u8 reserved_2[0x6];
} NacpApplicationControlDataConditionData;

NXDT_ASSERT(NacpApplicationControlDataConditionData, 0x10);

#pragma pack(push, 1)
typedef struct {
    u64 type;                                           ///< TODO: add enum with values.
    NacpApplicationControlDataConditionData data[0x8];
    u8 count;
} NacpApplicationControlDataConditionStruct;
#pragma pack(pop)

NXDT_ASSERT(NacpApplicationControlDataConditionStruct, 0x89);

typedef enum : u8 {
    NacpAlbumFileExport_Allow = 0,
    NacpAlbumFileExport_Deny  = 1,
    NacpAlbumFileExport_Count = 2   ///< Total values supported by this enum.
} NacpAlbumFileExport;

typedef struct {
    NacpTitleBlock title_block;
    char isbn[0x25];
    NacpStartupUserAccount startup_user_account;
    NacpUserAccountSwitchLock user_account_switch_lock;
    NacpAddOnContentRegistrationType add_on_content_registration_type;
    NacpAttribute attribute;
    NacpSupportedLanguage supported_language;
    NacpParentalControl parental_control;
    NacpScreenshot screenshot;
    NacpVideoCapture video_capture;
    NacpDataLossConfirmation data_loss_confirmation;
    NacpPlayLogPolicy play_log_policy;
    u64 presence_group_id;
    NacpRatingAge rating_age;
    char display_version[0x10];
    u64 add_on_content_base_id;
    u64 save_data_owner_id;
    s64 user_account_save_data_size;
    s64 user_account_save_data_journal_size;
    s64 device_save_data_size;
    s64 device_save_data_journal_size;
    s64 bcat_delivery_cache_storage_size;
    char application_error_code_category[0x8];
    u64 local_communication_id[0x8];
    NacpLogoType logo_type;
    NacpLogoHandling logo_handling;
    NacpRuntimeAddOnContentInstall runtime_add_on_content_install;
    NacpRuntimeParameterDelivery runtime_parameter_delivery;
    NacpAppropriateAgeForChina appropriate_age_for_china;
    NacpUndecidedParameter75b8b undecided_parameter_75b8b;
    NacpCrashReport crash_report;
    NacpHdcp hdcp;
    u64 seed_for_pseudo_device_id;
    char bcat_passphrase[0x41];
    NacpStartupUserAccountOption startup_user_account_option;
    u8 reserved_for_user_account_save_data_operation[0x6];
    s64 user_account_save_data_size_max;
    s64 user_account_save_data_journal_size_max;
    s64 device_save_data_size_max;
    s64 device_save_data_journal_size_max;
    s64 temporary_storage_size;
    s64 cache_storage_size;
    s64 cache_storage_journal_size;
    s64 cache_storage_data_and_journal_size_max;
    u16 cache_storage_index_max;
    u8 reserved_1;
    NacpRuntimeUpgrade runtime_upgrade;
    NacpSupportingLimitedApplicationLicenses supporting_limited_application_licenses;
    u64 play_log_queryable_application_id[0x10];
    NacpPlayLogQueryCapability play_log_query_capability;
    NacpRepair repair;
    u8 program_index;
    NacpRequiredNetworkServiceLicenseOnLaunch required_network_service_license_on_launch;
    NacpApplicationErrorCodePrefix application_error_code_prefix;
    NacpTitlesDataFormat titles_data_format;                                                        ///< TODO: add to XML generation.
    u8 acd_index;                                                                                   ///< Application Control Data index. Used to access `Acd_{idx}` subdirectories within the Control NCA RomFS.
    NacpApparentPlatform apparent_platform;
    NacpNeighborDetectionClientConfiguration neighbor_detection_client_configuration;
    NacpJitConfiguration jit_configuration;
    NacpRequiredAddOnContentsSetBinaryDescriptor required_add_on_contents_set_binary_descriptor;
    NacpPlayReportPermission play_report_permission;
    NacpCrashScreenshotForProd crash_screenshot_for_prod;
    NacpCrashScreenshotForDev crash_screenshot_for_dev;
    NacpContentsAvailabilityTransitionPolicy contents_availability_transition_policy;
    NacpSupportedLanguage supported_language_flag_for_nx_addon;                                     ///< TODO: add to XML generation.
    NacpAccessibleLaunchRequiredVersion accessible_launch_required_version;
    NacpApplicationControlDataConditionStruct application_control_data_condition;                         ///< Used for Switch 2 upgrade packs, which are distributed as AddOnContent titles. TODO: add to XML generation.
    u8 initial_program_index;                                                                       ///< TODO: add to XML generation.
    u8 reserved_2[0x2];
    u8 accessible_program_index_flags[0x4];                                                         ///< TODO: add structure / enum / XML generation.
    NacpAlbumFileExport album_file_export;
    u8 reserved_3[0x7];
    u8 save_data_certificate_bytes[0x80];                                                           ///< TODO: add structure / XML generation.
    u8 has_in_game_voice_chat;                                                                      ///< TODO: add enum with values / XML generation.
    u8 reserved_4[0x3];
    u8 supported_extra_add_on_content_flag[0x4];                                                    ///< TODO: add structure / enum / XML generation.
    u8 has_karaoke_feature;                                                                         ///< TODO: add structure / enum / XML generation.
    u8 reserved_5[0x697];
    u8 platform_specific_region[0x400];                                                             ///< TODO: add structure / XML generation.
} NsApplicationControlProperty;

NXDT_ASSERT(NsApplicationControlProperty, 0x4000);

typedef struct {
    NacpLanguage language;
    u64 icon_size;          ///< JPEG icon size. Must not exceed NACP_MAX_ICON_SIZE.
    u8 *icon_data;          ///< Pointer to a dynamically allocated buffer that holds the JPEG icon data.
} NacpIconContext;

typedef struct {
    NcaContext *nca_ctx;                            ///< Pointer to the NCA context for the Control NCA from which NACP data is retrieved.
    RomFileSystemContext romfs_ctx;                 ///< RomFileSystemContext for the Control NCA FS section #0, which is where the NACP is stored.
    const RomFileSystemFileEntry *romfs_file_entry; ///< RomFileSystemFileEntry for the NACP in the Control NCA FS section #0. Used to generate a RomFileSystemFileEntryPatch if needed.
    RomFileSystemFileEntryPatch nca_patch;          ///< RomFileSystemFileEntryPatch generated if NACP modifications are needed. Used to seamlessly replace Control NCA data while writing it.
                                                    ///< Bear in mind that generating a patch modifies the NCA context.
    NsApplicationControlProperty *data;             ///< Pointer to a dynamically allocated buffer that holds the full NACP.
    NacpTitle *titles;                              ///< Pointer to a dynamically allocated buffer that holds the decompressed title entries.
    u8 data_hash[SHA256_HASH_SIZE];                 ///< SHA-256 checksum calculated over the whole NACP. Used to determine if NcaHierarchicalSha256Patch generation is truly needed.
    u8 icon_count;                                  ///< NACP icon count. May be zero if no icons are available.
    NacpIconContext *icon_ctx;                      ///< Pointer to a dynamically allocated buffer that holds 'icon_count' NACP icon contexts. May be NULL if no icons are available.
    char *authoring_tool_xml;                       ///< Pointer to a dynamically allocated, NULL-terminated buffer that holds AuthoringTool-like XML data.
                                                    ///< This is always NULL unless nacpGenerateAuthoringToolXml() is used on this NacpContext.
    u64 authoring_tool_xml_size;                    ///< Size for the AuthoringTool-like XML. This is essentially the same as using strlen() on 'authoring_tool_xml'.
                                                    ///< This is always 0 unless nacpGenerateAuthoringToolXml() is used on this NacpContext.
} NacpContext;

/// Initializes a NacpContext using a previously initialized NcaContext (which must belong to a Control NCA).
bool nacpInitializeContext(NacpContext *out, NcaContext *nca_ctx);

/// Changes flags in the NACP from the input NacpContext and generates a RomFS file entry patch if needed.
/// If 'patch_sua' is true, StartupUserAccount is set to None, the IsOptional bit in StartupUserAccountOption is cleared and UserAccountSwitchLock is set to Disable.
/// If 'patch_screenshot' is true, Screenshot is set to Allow.
/// If 'patch_video_capture' is true, VideoCapture is set to Enable.
/// If 'patch_hdcp' is true, Hdcp is set to None.
bool nacpGenerateNcaPatch(NacpContext *nacp_ctx, bool patch_sua, bool patch_screenshot, bool patch_video_capture, bool patch_hdcp);

/// Writes data from the RomFS file entry patch in the input NacpContext to the provided buffer.
void nacpWriteNcaPatch(NacpContext *nacp_ctx, void *buf, u64 buf_size, u64 buf_offset);

/// Generates an AuthoringTool-like XML using information from a previously initialized NacpContext, as well as the Application/Patch version and the required system version.
/// If the function succeeds, XML data and size will get saved to the 'authoring_tool_xml' and 'authoring_tool_xml_size' members from the NacpContext.
bool nacpGenerateAuthoringToolXml(NacpContext *nacp_ctx, u32 version, u32 required_system_version);

/// These functions return pointers to string representations of the input flag/value index (e.g. nacpGetLanguageString(NacpLanguage_AmericanEnglish) -> "AmericanEnglish").
/// If the input flag/value index is invalid, "Unknown" will be returned.
/// If dealing with a bitflag field such as:
///     * NacpAttribute
///     * NacpSupportedLanguage
///     * NacpParentalControl
///     * NacpStartupUserAccountOption
///     * NacpSupportingLimitedApplicationLicenses
///     * NacpRepair
///     * NacpRequiredNetworkServiceLicenseOnLaunch
///     * NacpJitConfigurationFlag
///     * NacpPlayReportPermission
/// Then, the provided value must be a 0-based index to the desired flag and not a bitmask from its enum (e.g. NacpAttribute_RetailInteractiveDisplay -> use 1 instead).

const char *nacpGetLanguageString(NacpLanguage language); /// Can also be used for NacpSupportedLanguage flags with values from the NacpLanguage enum.
const char *nacpGetStartupUserAccountString(NacpStartupUserAccount startup_user_account);
const char *nacpGetUserAccountSwitchLockString(NacpUserAccountSwitchLock user_account_switch_lock);
const char *nacpGetAddOnContentRegistrationTypeString(NacpAddOnContentRegistrationType add_on_content_registration_type);
const char *nacpGetAttributeString(u8 attribute);
const char *nacpGetParentalControlString(u8 parental_control);
const char *nacpGetScreenshotString(NacpScreenshot screenshot);
const char *nacpGetVideoCaptureString(NacpVideoCapture video_capture);
const char *nacpGetDataLossConfirmationString(NacpDataLossConfirmation data_loss_confirmation);
const char *nacpGetPlayLogPolicyString(NacpPlayLogPolicy play_log_policy);
const char *nacpGetRatingAgeOrganizationString(NacpRatingAgeOrganization rating_age_organization);
const char *nacpGetLogoTypeString(NacpLogoType logo_type);
const char *nacpGetLogoHandlingString(NacpLogoHandling logo_handling);
const char *nacpGetRuntimeAddOnContentInstallString(NacpRuntimeAddOnContentInstall runtime_add_on_content_install);
const char *nacpGetRuntimeParameterDeliveryString(NacpRuntimeParameterDelivery runtime_parameter_delivery);
const char *nacpGetAppropriateAgeForChina(NacpAppropriateAgeForChina appropriate_age_for_china);
const char *nacpGetUndecidedParameter75b8bString(NacpUndecidedParameter75b8b undecided_parameter_75b8b);
const char *nacpGetCrashReportString(NacpCrashReport crash_report);
const char *nacpGetHdcpString(NacpHdcp hdcp);
const char *nacpGetStartupUserAccountOptionString(u8 startup_user_account_option);
const char *nacpGetRuntimeUpgradeString(NacpRuntimeUpgrade runtime_upgrade);
const char *nacpGetSupportingLimitedApplicationLicensesString(u8 supporting_limited_application_licenses);
const char *nacpGetPlayLogQueryCapabilityString(NacpPlayLogQueryCapability play_log_query_capability);
const char *nacpGetRepairString(u8 repair);
const char *nacpGetRequiredNetworkServiceLicenseOnLaunchString(u8 required_network_service_license_on_launch);
const char *nacpGetJitConfigurationFlagString(u8 jit_configuration_flag);
const char *nacpGetPlayReportPermissionString(u8 play_report_permission);
const char *nacpGetCrashScreenshotForProdString(NacpCrashScreenshotForProd crash_screenshot_for_prod);
const char *nacpGetCrashScreenshotForDevString(NacpCrashScreenshotForDev crash_screenshot_for_dev);
const char *nacpGetContentsAvailabilityTransitionPolicyString(NacpContentsAvailabilityTransitionPolicy contents_availability_transition_policy);
const char *nacpGetAlbumFileExportString(NacpAlbumFileExport album_file_export);

/// Helper inline functions.

NX_INLINE void nacpFreeContext(NacpContext *nacp_ctx)
{
    if (!nacp_ctx) return;

    romfsFreeContext(&(nacp_ctx->romfs_ctx));
    romfsFreeFileEntryPatch(&(nacp_ctx->nca_patch));

    if (nacp_ctx->data) free(nacp_ctx->data);

    if (nacp_ctx->titles) free(nacp_ctx->titles);

    if (nacp_ctx->icon_ctx)
    {
        for(u8 i = 0; i < nacp_ctx->icon_count; i++)
        {
            if (nacp_ctx->icon_ctx[i].icon_data) free(nacp_ctx->icon_ctx[i].icon_data);
        }

        free(nacp_ctx->icon_ctx);
    }

    if (nacp_ctx->authoring_tool_xml) free(nacp_ctx->authoring_tool_xml);
    memset(nacp_ctx, 0, sizeof(NacpContext));
}

NX_INLINE bool nacpIsValidIconContext(const NacpIconContext *icon_ctx)
{
    return (icon_ctx && icon_ctx->language < NacpLanguage_Count && icon_ctx->icon_size && icon_ctx->icon_data);
}

NX_INLINE bool nacpIsValidContext(const NacpContext *nacp_ctx)
{
    if (!nacp_ctx || !nacp_ctx->nca_ctx || !nacp_ctx->romfs_file_entry || !nacp_ctx->data || !nacp_ctx->titles || \
        (!nacp_ctx->icon_count && nacp_ctx->icon_ctx) || (nacp_ctx->icon_count && !nacp_ctx->icon_ctx)) return false;

    for(u8 i = 0; i < nacp_ctx->icon_count; i++)
    {
        if (!nacpIsValidIconContext(&(nacp_ctx->icon_ctx[i]))) return false;
    }

    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* __NACP_H__ */
