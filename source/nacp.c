/*
 * nacp.c
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
#include "nacp.h"
#include "title.h"

/* Type definitions. */

typedef const char *(*NacpStringFunction)(u8 value);    /* Used while adding fields to the AuthoringTool-like XML. */

/* Global variables. */

static const char *g_unknownString = "Unknown";

static const char *g_nacpLanguageStrings[NacpLanguage_Count] = {
    "AmericanEnglish",
    "BritishEnglish",
    "Japanese",
    "French",
    "German",
    "LatinAmericanSpanish",
    "Spanish",
    "Italian",
    "Dutch",
    "CanadianFrench",
    "Portuguese",
    "Russian",
    "Korean",
    "TraditionalChinese",
    "SimplifiedChinese",
    "BrazilianPortuguese"
};

static const char *g_nacpStartupUserAccountStrings[NacpStartupUserAccount_Count] = {
    "None",
    "Required",
    "RequiredWithNetworkServiceAccountAvailable"
};

static const char *g_nacpUserAccountSwitchLockStrings[NacpUserAccountSwitchLock_Count] = {
    "Disable",
    "Enable"
};

static const char *g_nacpAddOnContentRegistrationTypeStrings[NacpAddOnContentRegistrationType_Count] = {
    "AllOnLaunch",
    "OnDemand"
};

static const char *g_nacpAttributeStrings[NacpAttribute_Count] = {
    "Demo",
    "RetailInteractiveDisplay"
};

static const char *g_nacpParentalControlStrings[NacpParentalControl_Count] = {
    "FreeCommunication"
};

static const char *g_nacpScreenshotStrings[NacpScreenshot_Count] = {
    "Allow",
    "Deny"
};

static const char *g_nacpVideoCaptureStrings[NacpVideoCapture_Count] = {
    "Disable",
    "Manual",
    "Enable"
};

static const char *g_nacpDataLossConfirmationStrings[NacpDataLossConfirmation_Count] = {
    "None",
    "Required"
};

static const char *g_nacpPlayLogPolicyStrings[NacpPlayLogPolicy_Count] = {
    "Open",
    "LogOnly",
    "None",
    "Closed"
};

static const char *g_nacpRatingAgeOrganizationStrings[NacpRatingAgeOrganization_Count] = {
    "CERO",
    "GRACGCRB",
    "GSRMR",
    "ESRB",
    "ClassInd",
    "USK",
    "PEGI",
    "PEGIPortugal",
    "PEGIBBFC",
    "Russian",
    "ACB",
    "OFLC",
    "IARCGeneric"
};

static const char *g_nacpLogoTypeStrings[NacpLogoType_Count] = {
    "LicensedByNintendo",
    "DistributedByNintendo",
    "Nintendo"
};

static const char *g_nacpLogoHandlingStrings[NacpLogoHandling_Count] = {
    "Auto",
    "Manual"
};

static const char *g_nacpRuntimeAddOnContentInstallStrings[NacpRuntimeAddOnContentInstall_Count] = {
    "Deny",
    "AllowAppend",
    "AllowAppendButDontDownloadWhenUsingNetwork"
};

static const char *g_nacpNacpRuntimeParameterDeliveryStrings[NacpRuntimeParameterDelivery_Count] = {
    "Always",
    "AlwaysIfUserStateMatched",
    "OnRestart"
};

static const char *g_nacpCrashReportStrings[NacpCrashReport_Count] = {
    "Deny",
    "Allow"
};

static const char *g_nacpHdcpStrings[NacpHdcp_Count] = {
    "None",
    "Required"
};

static const char *g_nacpStartupUserAccountOptionStrings[NacpStartupUserAccountOption_Count] = {
    "IsOptional"
};

static const char *g_nacpPlayLogQueryCapabilityStrings[NacpPlayLogQueryCapability_Count] = {
    "None",
    "WhiteList",
    "All"
};

static const char *g_nacpRepairStrings[NacpRepair_Count] = {
    "SuppressGameCardAccess"
};

static const char *g_nacpRequiredNetworkServiceLicenseOnLaunchStrings[NacpRequiredNetworkServiceLicenseOnLaunch_Count] = {
    "Common"
};

static const char *g_nacpJitConfigurationFlagStrings[NacpJitConfigurationFlag_Count] = {
    "None",
    "Enabled"
};

static const char *g_nacpPlayReportPermissionStrings[NacpPlayReportPermission_Count] = {
    "None",
    "TargetMarketing"
};

static const char *g_nacpCrashScreenshotForProdStrings[NacpCrashScreenshotForProd_Count] = {
    "Deny",
    "Allow"
};

static const char *g_nacpCrashScreenshotForDevStrings[NacpCrashScreenshotForDev_Count] = {
    "Deny",
    "Allow"
};

/* Function prototypes. */

NX_INLINE bool nacpCheckBitflagField(const void *flag, u8 flag_bitcount, u8 idx);

static bool nacpAddStringFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const char *value);
static bool nacpAddEnumFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u8 value, NacpStringFunction str_func);
static bool nacpAddBitflagFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const void *flag, u8 flag_width, u8 max_flag_idx, NacpStringFunction str_func);
static bool nacpAddU16FieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u16 value, bool hex);
static bool nacpAddU32FieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u32 value);
static bool nacpAddU64FieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u64 value);

bool nacpInitializeContext(NacpContext *out, NcaContext *nca_ctx)
{
    if (!out || !nca_ctx || !strlen(nca_ctx->content_id_str) || nca_ctx->content_type != NcmContentType_Control || nca_ctx->content_size < NCA_FULL_HEADER_LENGTH || \
        (nca_ctx->storage_id != NcmStorageId_GameCard && !nca_ctx->ncm_storage) || (nca_ctx->storage_id == NcmStorageId_GameCard && !nca_ctx->gamecard_offset) || \
        nca_ctx->header.content_type != NcaContentType_Control || !out)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    const char *language_str = NULL;
    char icon_path[0x80] = {0};
    RomFileSystemFileEntry *icon_entry = NULL;
    NacpIconContext *tmp_icon_ctx = NULL;
    
    bool success = false;
    
    /* Free output context beforehand. */
    nacpFreeContext(out);
    
    /* Initialize RomFS context. */
    if (!romfsInitializeContext(&(out->romfs_ctx), &(nca_ctx->fs_contexts[0])))
    {
        LOGFILE("Failed to initialize RomFS context!");
        goto end;
    }
    
    /* Retrieve RomFS file entry for 'control.nacp'. */
    if (!(out->romfs_file_entry = romfsGetFileEntryByPath(&(out->romfs_ctx), "/control.nacp")))
    {
        LOGFILE("Failed to retrieve file entry for \"control.nacp\" from RomFS!");
        goto end;
    }
    
    //LOGFILE("Found 'control.nacp' entry in Control NCA \"%s\".", nca_ctx->content_id_str);
    
    /* Verify NACP size. */
    if (out->romfs_file_entry->size != sizeof(_NacpStruct))
    {
        LOGFILE("Invalid NACP size!");
        goto end;
    }
    
    /* Allocate memory for the NACP data. */
    if (!(out->data = malloc(sizeof(_NacpStruct))))
    {
        LOGFILE("Failed to allocate memory for the NACP data!");
        goto end;
    }
    
    /* Read NACP data into memory buffer. */
    if (!romfsReadFileEntryData(&(out->romfs_ctx), out->romfs_file_entry, out->data, sizeof(_NacpStruct), 0))
    {
        LOGFILE("Failed to read NACP data!");
        goto end;
    }
    
    /* Calculate SHA-256 checksum for the whole NACP. */
    sha256CalculateHash(out->data_hash, out->data, sizeof(_NacpStruct));
    
    /* Save pointer to NCA context to the output NACP context. */
    out->nca_ctx = nca_ctx;
    
    /* Retrieve NACP icon data. */
    for(u8 i = 0; i < NacpSupportedLanguage_Count; i++)
    {
        NacpIconContext *icon_ctx = NULL;
        
        /* Check if the current language is supported. */
        if (!nacpCheckBitflagField(&(out->data->supported_language), sizeof(out->data->supported_language) * 8, i)) continue;
        
        /* Get language string. */
        language_str = nacpGetLanguageString(i);
        
        /* Generate icon path. */
        sprintf(icon_path, "/icon_%s.dat", language_str);
        
        /* Retrieve RomFS file entry for this icon. */
        if (!(icon_entry = romfsGetFileEntryByPath(&(out->romfs_ctx), icon_path))) continue;
        
        /* Check icon size. */
        if (!icon_entry->size || icon_entry->size > NACP_MAX_ICON_SIZE)
        {
            LOGFILE("Invalid NACP icon size!");
            goto end;
        }
        
        /* Reallocate icon context buffer. */
        if (!(tmp_icon_ctx = realloc(out->icon_ctx, (out->icon_count + 1) * sizeof(NacpIconContext))))
        {
            LOGFILE("Failed to reallocate NACP icon context buffer!");
            goto end;
        }
        
        out->icon_ctx = tmp_icon_ctx;
        tmp_icon_ctx = NULL;
        
        icon_ctx = &(out->icon_ctx[out->icon_count]);
        memset(icon_ctx, 0, sizeof(NacpIconContext));
        
        /* Allocate memory for this icon data. */
        if (!(icon_ctx->icon_data = malloc(icon_entry->size)))
        {
            LOGFILE("Failed to allocate memory for NACP icon data!");
            goto end;
        }
        
        /* Read icon data. */
        if (!romfsReadFileEntryData(&(out->romfs_ctx), icon_entry, icon_ctx->icon_data, icon_entry->size, 0))
        {
            LOGFILE("Failed to read NACP icon data!");
            goto end;
        }
        
        /* Fill icon context. */
        icon_ctx->language = i;
        icon_ctx->icon_size = icon_entry->size;
        
        /* Update icon count. */
        out->icon_count++;
    }
    
    success = true;
    
end:
    if (!success) nacpFreeContext(out);
    
    return success;
}

bool nacpGenerateAuthoringToolXml(NacpContext *nacp_ctx, u32 version, u32 required_system_version)
{
    if (!nacpIsValidContext(nacp_ctx))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    _NacpStruct *nacp = nacp_ctx->data;
    
    u8 i = 0, count = 0;
    char *xml_buf = NULL;
    u64 xml_buf_size = 0;
    
    u8 icon_hash[SHA256_HASH_SIZE] = {0};
    char icon_hash_str[SHA256_HASH_SIZE + 1] = {0};
    
    u8 null_key[0x10] = {0};
    char key_str[0x21] = {0};
    bool ndcc_sgc_available = false, ndcc_rgc_available = false;
    NacpNeighborDetectionClientConfiguration *ndcc = &(nacp->neighbor_detection_client_configuration);
    
    NacpRequiredAddOnContentsSetBinaryDescriptor *raocsbd = &(nacp->required_add_on_contents_set_binary_descriptor);
    
    bool success = false;
    
    /* Free AuthoringTool-like XML data if needed. */
    if (nacp_ctx->authoring_tool_xml) free(nacp_ctx->authoring_tool_xml);
    nacp_ctx->authoring_tool_xml = NULL;
    nacp_ctx->authoring_tool_xml_size = 0;
    
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n" \
                                            "<Application>\n")) goto end;
    
    /* Title. */
    for(i = 0, count = 0; i < NacpLanguage_Count; i++)
    {
        if (!strlen(nacp->title[i].name) || !strlen(nacp->title[i].publisher)) continue;
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <Title>\n" \
                                                "    <Language>%s</Language>\n" \
                                                "    <Name>%s</Name>\n" \
                                                "    <Publisher>%s</Publisher>\n" \
                                                "  </Title>\n", \
                                                nacpGetLanguageString(i),
                                                nacp->title[i].name,
                                                nacp->title[i].publisher)) goto end;
        
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <Title />\n")) goto end;
    
    /* Isbn. */
    if (!nacpAddStringFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Isbn", nacp->isbn)) goto end;
    
    /* StartupUserAccount. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "StartupUserAccount", nacp->startup_user_account, &nacpGetStartupUserAccountString)) goto end;
    
    /* StartupUserAccountOption. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "StartupUserAccountOption", &(nacp->startup_user_account_option), sizeof(nacp->startup_user_account_option), \
        NacpStartupUserAccountOption_Count, &nacpGetStartupUserAccountOptionString)) goto end;
    
    /* UserAccountSwitchLock. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "UserAccountSwitchLock", nacp->user_account_switch_lock, &nacpGetUserAccountSwitchLockString)) goto end;
    
    /* Attribute. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Attribute", &(nacp->attribute), sizeof(nacp->attribute), NacpAttribute_Count, &nacpGetAttributeString)) goto end;
    
    /* ParentalControl. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "ParentalControl", &(nacp->parental_control), sizeof(nacp->parental_control), NacpParentalControl_Count, \
        &nacpGetParentalControlString)) goto end;
    
    /* SupportedLanguage. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "SupportedLanguage", &(nacp->supported_language), sizeof(nacp->supported_language), NacpSupportedLanguage_Count, \
        &nacpGetLanguageString)) goto end;
    
    /* Screenshot. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Screenshot", nacp->screenshot, &nacpGetScreenshotString)) goto end;
    
    /* VideoCapture. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "VideoCapture", nacp->video_capture, &nacpGetVideoCaptureString)) goto end;
    
    /* PresenceGroupId. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "PresenceGroupId", nacp->presence_group_id)) goto end;
    
    /* DisplayVersion. */
    if (!nacpAddStringFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DisplayVersion", nacp->display_version)) goto end;
    
    /* Rating. */
    for(i = 0, count = 0; i < NacpRatingAgeOrganization_Count; i++)
    {
        u8 age = *((u8*)&(nacp->rating_age) + i);
        if (age == 0xFF) continue;
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <Rating>\n" \
                                                "    <Organization>%s</Organization>\n" \
                                                "    <Age>%u</Age>\n" \
                                                "  </Rating>\n", \
                                                nacpGetRatingAgeOrganizationString(i),
                                                age)) goto end;
        
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <Rating />\n")) goto end;
    
    /* DataLossConfirmation. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DataLossConfirmation", nacp->data_loss_confirmation, &nacpGetDataLossConfirmationString)) goto end;
    
    /* PlayLogPolicy. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "PlayLogPolicy", nacp->play_log_policy, &nacpGetPlayLogPolicyString)) goto end;
    
    /* SaveDataOwnerId. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "SaveDataOwnerId", nacp->save_data_owner_id)) goto end;
    
    /* UserAccountSaveDataSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "UserAccountSaveDataSize", nacp->user_account_save_data_size)) goto end;
    
    /* UserAccountSaveDataJournalSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "UserAccountSaveDataJournalSize", nacp->user_account_save_data_journal_size)) goto end;
    
    /* DeviceSaveDataSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DeviceSaveDataSize", nacp->device_save_data_size)) goto end;
    
    /* DeviceSaveDataJournalSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DeviceSaveDataJournalSize", nacp->device_save_data_journal_size)) goto end;
    
    /* BcatDeliveryCacheStorageSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "BcatDeliveryCacheStorageSize", nacp->bcat_delivery_cache_storage_size)) goto end;
    
    /* ApplicationErrorCodeCategory. */
    if (!nacpAddStringFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "ApplicationErrorCodeCategory", nacp->application_error_code_category)) goto end;
    
    /* AddOnContentBaseId. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "AddOnContentBaseId", nacp->add_on_content_base_id)) goto end;
    
    /* Version. */
    if (!nacpAddU32FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Version", version)) goto end;
    
    /* ReleaseVersion and PrivateVersion. Unused but kept anyway. */
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "  <ReleaseVersion />\n" \
                                            "  <PrivateVersion />\n")) goto end;
    
    /* LogoType. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "LogoType", nacp->logo_type, &nacpGetLogoTypeString)) goto end;
    
    /* RequiredSystemVersion. */
    if (!nacpAddU32FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "RequiredSystemVersion", required_system_version)) goto end;
    
    /* LocalCommunicationId. */
    for(i = 0, count = 0; i < 0x8; i++)
    {
        if (!nacp->local_communication_id[i]) continue;
        if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "LocalCommunicationId", nacp->local_communication_id[i])) goto end;
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <LocalCommunicationId />\n")) goto end;
    
    /* LogoHandling. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "LogoHandling", nacp->logo_handling, &nacpGetLogoHandlingString)) goto end;
    
    /* Icon. */
    for(i = 0, count = 0; i < nacp_ctx->icon_count; i++)
    {
        NacpIconContext *icon_ctx = &(nacp_ctx->icon_ctx[i]);
        
        /* Calculate icon hash. */
        sha256CalculateHash(icon_hash, icon_ctx->icon_data, icon_ctx->icon_size);
        
        /* Generate icon hash string. Only the first half from the hash is used. */
        utilsGenerateHexStringFromData(icon_hash_str, SHA256_HASH_SIZE + 1, icon_hash, SHA256_HASH_SIZE / 2);
        
        /* Add XML element. */
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <Icon>\n" \
                                                "    <Language>%s</Language>\n" \
                                                "    <IconPath />\n" \
                                                "    <NxIconPath />\n" \
                                                "    <RawIconHash />\n" \
                                                "    <NxIconHash>%s</NxIconHash>\n" \
                                                "  </Icon>\n", \
                                                nacpGetLanguageString(icon_ctx->language), \
                                                icon_hash_str)) goto end;
        
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <Icon />\n")) goto end;
    
    /* HtmlDocumentPath, LegalInformationFilePath and AccessibleUrlsFilePath. Unused but kept anyway. */
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "  <HtmlDocumentPath />\n" \
                                            "  <LegalInformationFilePath />\n" \
                                            "  <AccessibleUrlsFilePath />\n")) goto end;
    
    /* SeedForPseudoDeviceId. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "SeedForPseudoDeviceId", nacp->seed_for_pseudo_device_id)) goto end;
    
    /* BcatPassphrase. */
    if (!nacpAddStringFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "BcatPassphrase", nacp->bcat_passphrase)) goto end;
    
    /* AddOnContentRegistrationType. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "AddOnContentRegistrationType", nacp->add_on_content_registration_type, &nacpGetAddOnContentRegistrationTypeString)) goto end;
    
    /* UserAccountSaveDataSizeMax. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "UserAccountSaveDataSizeMax", nacp->user_account_save_data_size_max)) goto end;
    
    /* UserAccountSaveDataJournalSizeMax. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "UserAccountSaveDataJournalSizeMax", nacp->user_account_save_data_journal_size_max)) goto end;
    
    /* DeviceSaveDataSizeMax. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DeviceSaveDataSizeMax", nacp->device_save_data_size_max)) goto end;
    
    /* DeviceSaveDataJournalSizeMax. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DeviceSaveDataJournalSizeMax", nacp->device_save_data_journal_size_max)) goto end;
    
    /* TemporaryStorageSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "TemporaryStorageSize", nacp->temporary_storage_size)) goto end;
    
    /* CacheStorageSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "CacheStorageSize", nacp->cache_storage_size)) goto end;
    
    /* CacheStorageJournalSize. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "CacheStorageJournalSize", nacp->cache_storage_journal_size)) goto end;
    
    /* CacheStorageDataAndJournalSizeMax. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "CacheStorageDataAndJournalSizeMax", nacp->cache_storage_data_and_journal_size_max)) goto end;
    
    /* CacheStorageIndexMax. */
    if (!nacpAddU16FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "CacheStorageIndexMax", nacp->cache_storage_index_max, true)) goto end;
    
    /* Hdcp. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Hdcp", nacp->hdcp, &nacpGetHdcpString)) goto end;
    
    /* CrashReport. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "CrashReport", nacp->crash_report, &nacpGetCrashReportString)) goto end;
    
    /* RuntimeAddOnContentInstall. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "RuntimeAddOnContentInstall", nacp->runtime_add_on_content_install, &nacpGetRuntimeAddOnContentInstallString)) goto end;
    
    /* PlayLogQueryableApplicationId. */
    for(i = 0, count = 0; i < 0x10; i++)
    {
        if (!nacp->play_log_queryable_application_id[i]) continue;
        if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "PlayLogQueryableApplicationId", nacp->play_log_queryable_application_id[i])) goto end;
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <PlayLogQueryableApplicationId />\n")) goto end;
    
    /* PlayLogQueryCapability. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "PlayLogQueryCapability", nacp->play_log_query_capability, &nacpGetPlayLogQueryCapabilityString)) goto end;
    
    /* Repair. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Repair", &(nacp->repair), sizeof(nacp->repair), NacpRepair_Count, &nacpGetRepairString)) goto end;
    
    /* ProgramIndex. */
    if (!nacpAddU16FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "ProgramIndex", nacp->program_index, false)) goto end;
    
    /* RequiredNetworkServiceLicenseOnLaunch. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "RequiredNetworkServiceLicenseOnLaunch", &(nacp->required_network_service_license_on_launch), \
        sizeof(nacp->required_network_service_license_on_launch), NacpRequiredNetworkServiceLicenseOnLaunch_Count, &nacpGetRequiredNetworkServiceLicenseOnLaunchString)) goto end;
    
    /* NeighborDetectionClientConfiguration. */
    ndcc_sgc_available = (ndcc->send_group_configuration.group_id && memcmp(ndcc->send_group_configuration.key, null_key, sizeof(null_key)));
    
    for(i = 0; i < 0x10; i++)
    {
        ndcc_rgc_available = (ndcc->receivable_group_configurations[i].group_id && memcmp(ndcc->receivable_group_configurations[i].key, null_key, sizeof(null_key)));
        if (ndcc_rgc_available) break;
    }
    
    if (ndcc_sgc_available || ndcc_rgc_available)
    {
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <NeighborDetectionClientConfiguration>\n")) goto end;
        
        /* SendGroupConfiguration. */
        utilsGenerateHexStringFromData(key_str, sizeof(key_str), ndcc->send_group_configuration.key, sizeof(ndcc->send_group_configuration.key));
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "    <SendGroupConfiguration>\n" \
                                                "      <GroupId>0x%016lx</GroupId>\n" \
                                                "      <Key>%s</Key>\n" \
                                                "    </SendGroupConfiguration>\n", \
                                                ndcc->send_group_configuration.group_id,
                                                key_str)) goto end;
        
        /* ReceivableGroupConfiguration. */
        for(i = 0; i < 0x10; i++)
        {
            utilsGenerateHexStringFromData(key_str, sizeof(key_str), ndcc->receivable_group_configurations[i].key, sizeof(ndcc->receivable_group_configurations[i].key));
            
            if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                    "    <ReceivableGroupConfiguration>\n" \
                                                    "      <GroupId>0x%016lx</GroupId>\n" \
                                                    "      <Key>%s</Key>\n" \
                                                    "    </ReceivableGroupConfiguration>\n", \
                                                    ndcc->receivable_group_configurations[i].group_id,
                                                    key_str)) goto end;
        }
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  </NeighborDetectionClientConfiguration>\n")) goto end;
    } else {
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <NeighborDetectionClientConfiguration />\n")) goto end;
    }
    
    /* JitConfiguration. */
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                            "  <JitConfiguration>\n" \
                                            "    <IsEnabled>%s</IsEnabled>\n" \
                                            "    <MemorySize>0x%016lx</MemorySize>\n" \
                                            "  </JitConfiguration>\n", \
                                            nacpGetJitConfigurationFlagString(nacp->jit_configuration.jit_configuration_flag),
                                            nacp->jit_configuration.memory_size)) goto end;
    
    /* RequiredAddOnContentsSet. */
    for(i = 0, count = 0; i < 0x20; i++)
    {
        if (!raocsbd->descriptors[i].index || !raocsbd->descriptors[i].continue_set) continue;
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <RequiredAddOnContentsSet>\n" \
                                                "    <Index>%u</Index>\n" \
                                                "  </RequiredAddOnContentsSet>\n",
                                                raocsbd->descriptors[i].index)) goto end;
        
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <RequiredAddOnContentsSet />\n")) goto end;
    
    /* PlayReportPermission. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "PlayReportPermission", nacp->play_report_permission, &nacpGetPlayReportPermissionString)) goto end;
    
    /* CrashScreenshotForProd. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "CrashScreenshotForProd", nacp->crash_screenshot_for_prod, &nacpGetCrashScreenshotForProdString)) goto end;
    
    /* CrashScreenshotForDev. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "CrashScreenshotForDev", nacp->crash_screenshot_for_dev, &nacpGetCrashScreenshotForDevString)) goto end;
    
    /* AccessibleLaunchRequiredVersion. */
    for(i = 0, count = 0; i < 0x8; i++)
    {
        if (!nacp->accessible_launch_required_version.application_id[i]) continue;
        
        if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, \
                                                "  <AccessibleLaunchRequiredVersion>\n" \
                                                "    <ApplicationId>0x%016lx</ApplicationId>\n" \
                                                "  </AccessibleLaunchRequiredVersion>\n",
                                                nacp->accessible_launch_required_version.application_id[i])) goto end;
        
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <AccessibleLaunchRequiredVersion />\n")) goto end;
    
    /* History. Unused but kept anyway. */
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <History />\n")) goto end;
    
    /* RuntimeParameterDelivery. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "RuntimeParameterDelivery", nacp->runtime_parameter_delivery, &nacpGetRuntimeParameterDeliveryString)) goto end;
    
    /* ApplicationId. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "ApplicationId", nacp_ctx->nca_ctx->header.program_id)) goto end;
    
    /* FilterDescriptionFilePath. Unused but kept anyway. */
    if (!utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <FilterDescriptionFilePath />\n")) goto end;
    
    if (!(success = utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "</Application>"))) goto end;
    
    /* Update NACP context. */
    nacp_ctx->authoring_tool_xml = xml_buf;
    nacp_ctx->authoring_tool_xml_size = strlen(xml_buf);
    
end:
    if (!success)
    {
        if (xml_buf) free(xml_buf);
        LOGFILE("Failed to generate NACP AuthoringTool XML!");
    }
    
    return success;
}

const char *nacpGetLanguageString(u8 language)
{
    return (language < NacpLanguage_Count ? g_nacpLanguageStrings[language] : g_unknownString);
}

const char *nacpGetStartupUserAccountString(u8 startup_user_account)
{
    return (startup_user_account < NacpStartupUserAccount_Count ? g_nacpStartupUserAccountStrings[startup_user_account] : g_unknownString);
}

const char *nacpGetUserAccountSwitchLockString(u8 user_account_switch_lock)
{
    return (user_account_switch_lock < NacpUserAccountSwitchLock_Count ? g_nacpUserAccountSwitchLockStrings[user_account_switch_lock] : g_unknownString);
}

const char *nacpGetAddOnContentRegistrationTypeString(u8 add_on_content_registration_type)
{
    return (add_on_content_registration_type < NacpAddOnContentRegistrationType_Count ? g_nacpAddOnContentRegistrationTypeStrings[add_on_content_registration_type] : g_unknownString);
}

const char *nacpGetAttributeString(u8 attribute)
{
    return (attribute < NacpAttribute_Count ? g_nacpAttributeStrings[attribute] : g_unknownString);
}

const char *nacpGetParentalControlString(u8 parental_control)
{
    return (parental_control < NacpParentalControl_Count ? g_nacpParentalControlStrings[parental_control] : g_unknownString);
}

const char *nacpGetScreenshotString(u8 screenshot)
{
    return (screenshot < NacpScreenshot_Count ? g_nacpScreenshotStrings[screenshot] : g_unknownString);
}

const char *nacpGetVideoCaptureString(u8 video_capture)
{
    return (video_capture < NacpVideoCapture_Count ? g_nacpVideoCaptureStrings[video_capture] : g_unknownString);
}

const char *nacpGetDataLossConfirmationString(u8 data_loss_confirmation)
{
    return (data_loss_confirmation < NacpDataLossConfirmation_Count ? g_nacpDataLossConfirmationStrings[data_loss_confirmation] : g_unknownString);
}

const char *nacpGetPlayLogPolicyString(u8 play_log_policy)
{
    return (play_log_policy < NacpPlayLogPolicy_Count ? g_nacpPlayLogPolicyStrings[play_log_policy] : g_unknownString);
}

const char *nacpGetRatingAgeOrganizationString(u8 rating_age_organization)
{
    return (rating_age_organization < NacpRatingAgeOrganization_Count ? g_nacpRatingAgeOrganizationStrings[rating_age_organization] : g_unknownString);
}

const char *nacpGetLogoTypeString(u8 logo_type)
{
    return (logo_type < NacpLogoType_Count ? g_nacpLogoTypeStrings[logo_type] : g_unknownString);
}

const char *nacpGetLogoHandlingString(u8 logo_handling)
{
    return (logo_handling < NacpLogoHandling_Count ? g_nacpLogoHandlingStrings[logo_handling] : g_unknownString);
}

const char *nacpGetRuntimeAddOnContentInstallString(u8 runtime_add_on_content_install)
{
    return (runtime_add_on_content_install < NacpRuntimeAddOnContentInstall_Count ? g_nacpRuntimeAddOnContentInstallStrings[runtime_add_on_content_install] : g_unknownString);
}

const char *nacpGetRuntimeParameterDeliveryString(u8 runtime_parameter_delivery)
{
    return (runtime_parameter_delivery < NacpRuntimeParameterDelivery_Count ? g_nacpNacpRuntimeParameterDeliveryStrings[runtime_parameter_delivery] : g_unknownString);
}

const char *nacpGetCrashReportString(u8 crash_report)
{
    return (crash_report < NacpCrashReport_Count ? g_nacpCrashReportStrings[crash_report] : g_unknownString);
}

const char *nacpGetHdcpString(u8 hdcp)
{
    return (hdcp < NacpHdcp_Count ? g_nacpHdcpStrings[hdcp] : g_unknownString);
}

const char *nacpGetStartupUserAccountOptionString(u8 startup_user_account_option)
{
    return (startup_user_account_option < NacpStartupUserAccountOption_Count ? g_nacpStartupUserAccountOptionStrings[startup_user_account_option] : g_unknownString);
}

const char *nacpGetPlayLogQueryCapabilityString(u8 play_log_query_capability)
{
    return (play_log_query_capability < NacpPlayLogQueryCapability_Count ? g_nacpPlayLogQueryCapabilityStrings[play_log_query_capability] : g_unknownString);
}

const char *nacpGetRepairString(u8 repair)
{
    return (repair < NacpRepair_Count ? g_nacpRepairStrings[repair] : g_unknownString);
}

const char *nacpGetRequiredNetworkServiceLicenseOnLaunchString(u8 required_network_service_license_on_launch)
{
    return (required_network_service_license_on_launch < NacpRequiredNetworkServiceLicenseOnLaunch_Count ? \
            g_nacpRequiredNetworkServiceLicenseOnLaunchStrings[required_network_service_license_on_launch] : g_unknownString);
}

const char *nacpGetJitConfigurationFlagString(u64 jig_configuration_flag)
{
    return (jig_configuration_flag < NacpJitConfigurationFlag_Count ? g_nacpJitConfigurationFlagStrings[jig_configuration_flag] : g_unknownString);
}

const char *nacpGetPlayReportPermissionString(u8 play_report_permission)
{
    return (play_report_permission < NacpPlayReportPermission_Count ? g_nacpPlayReportPermissionStrings[play_report_permission] : g_unknownString);
}

const char *nacpGetCrashScreenshotForProdString(u8 crash_screenshot_for_prod)
{
    return (crash_screenshot_for_prod < NacpCrashScreenshotForProd_Count ? g_nacpCrashScreenshotForProdStrings[crash_screenshot_for_prod] : g_unknownString);
}

const char *nacpGetCrashScreenshotForDevString(u8 crash_screenshot_for_dev)
{
    return (crash_screenshot_for_dev < NacpCrashScreenshotForDev_Count ? g_nacpCrashScreenshotForDevStrings[crash_screenshot_for_dev] : g_unknownString);
}

NX_INLINE bool nacpCheckBitflagField(const void *flag, u8 flag_bitcount, u8 idx)
{
    if (!flag || !flag_bitcount || !IS_POWER_OF_TWO(flag_bitcount) || idx >= flag_bitcount) return false;
    const u8 *flag_u8 = (const u8*)flag;
    u8 byte_idx = (idx >> 3);
    u8 bitmask = BIT(idx - ALIGN_DOWN(idx, 8));
    return (flag_u8[byte_idx] & bitmask);
}

static bool nacpAddStringFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const char *value)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name) || !value)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    return (strlen(value) ? utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>%s</%s>\n", tag_name, value, tag_name) : \
            utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s />\n", tag_name));
}

static bool nacpAddEnumFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u8 value, NacpStringFunction str_func)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name) || !str_func)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    return utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>%s</%s>\n", tag_name, str_func(value), tag_name);
}

static bool nacpAddBitflagFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const void *flag, u8 flag_width, u8 max_flag_idx, NacpStringFunction str_func)
{
    u8 flag_bitcount = 0, i = 0, count = 0;
    const u8 *flag_u8 = (const u8*)flag;
    bool success = false, empty_flag = true;
    
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name) || !flag || !flag_width || (flag_width > 1 && !IS_POWER_OF_TWO(flag_width)) || flag_width > 0x10 || \
        (flag_bitcount = (flag_width * 8)) < max_flag_idx || !str_func)
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    for(i = 0; i < flag_width; i++)
    {
        if (flag_u8[i])
        {
            empty_flag = false;
            break;
        }
    }
    
    if (!empty_flag)
    {
        for(i = 0; i < max_flag_idx; i++)
        {
            if (!nacpCheckBitflagField(flag, flag_bitcount, i)) continue;
            if (!utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>%s</%s>\n", tag_name, str_func(i), tag_name)) goto end;
            count++;
        }
        
        /* Edge case for new, unsupported flags. */
        if (!count) empty_flag = true;
    }
    
    if (empty_flag && !utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s />\n", tag_name)) goto end;
    
    success = true;
    
end:
    return success;
}

static bool nacpAddU16FieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u16 value, bool hex)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    return (hex ? utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>0x%04x</%s>\n", tag_name, value, tag_name) : \
            utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>%u</%s>\n", tag_name, value, tag_name));
}

static bool nacpAddU32FieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u32 value)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    return utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>%u</%s>\n", tag_name, value, tag_name);
}

static bool nacpAddU64FieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u64 value)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name))
    {
        LOGFILE("Invalid parameters!");
        return false;
    }
    
    return utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>0x%016lx</%s>\n", tag_name, value, tag_name);
}
