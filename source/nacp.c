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

static const char *g_nacpLanguageStrings[] = {
    [NacpLanguage_AmericanEnglish]      = "AmericanEnglish",
    [NacpLanguage_BritishEnglish]       = "BritishEnglish",
    [NacpLanguage_Japanese]             = "Japanese",
    [NacpLanguage_French]               = "French",
    [NacpLanguage_German]               = "German",
    [NacpLanguage_LatinAmericanSpanish] = "LatinAmericanSpanish",
    [NacpLanguage_Spanish]              = "Spanish",
    [NacpLanguage_Italian]              = "Italian",
    [NacpLanguage_Dutch]                = "Dutch",
    [NacpLanguage_CanadianFrench]       = "CanadianFrench",
    [NacpLanguage_Portuguese]           = "Portuguese",
    [NacpLanguage_Russian]              = "Russian",
    [NacpLanguage_Korean]               = "Korean",
    [NacpLanguage_TraditionalChinese]   = "TraditionalChinese",
    [NacpLanguage_SimplifiedChinese]    = "SimplifiedChinese",
    [NacpLanguage_BrazilianPortuguese]  = "BrazilianPortuguese"
};

static const char *g_nacpStartupUserAccountStrings[] = {
    [NacpStartupUserAccount_None]                                       = "None",
    [NacpStartupUserAccount_Required]                                   = "Required",
    [NacpStartupUserAccount_RequiredWithNetworkServiceAccountAvailable] = "RequiredWithNetworkServiceAccountAvailable"
};

static const char *g_nacpUserAccountSwitchLockStrings[] = {
    [NacpUserAccountSwitchLock_Disable] = "Disable",
    [NacpUserAccountSwitchLock_Enable]  = "Enable"
};

static const char *g_nacpAddOnContentRegistrationTypeStrings[] = {
    [NacpAddOnContentRegistrationType_AllOnLaunch] = "AllOnLaunch",
    [NacpAddOnContentRegistrationType_OnDemand]    = "OnDemand"
};

static const char *g_nacpAttributeStrings[] = {
    [NacpAttribute_Demo]                     = "Demo",
    [NacpAttribute_RetailInteractiveDisplay] = "RetailInteractiveDisplay"
};

static const char *g_nacpParentalControlStrings[] = {
    [NacpParentalControl_FreeCommunication] = "FreeCommunication"
};

static const char *g_nacpScreenshotStrings[] = {
    [NacpScreenshot_Allow] = "Allow",
    [NacpScreenshot_Deny]  = "Deny"
};

static const char *g_nacpVideoCaptureStrings[] = {
    [NacpVideoCapture_Disable] = "Disable",
    [NacpVideoCapture_Manual]  = "Manual",
    [NacpVideoCapture_Enable]  = "Enable"
};

static const char *g_nacpDataLossConfirmationStrings[] = {
    [NacpDataLossConfirmation_None]     = "None",
    [NacpDataLossConfirmation_Required] = "Required"
};

static const char *g_nacpPlayLogPolicyStrings[] = {
    [NacpPlayLogPolicy_Open]    = "Open",
    [NacpPlayLogPolicy_LogOnly] = "LogOnly",
    [NacpPlayLogPolicy_None]    = "None",
    [NacpPlayLogPolicy_Closed]  = "Closed"
};

static const char *g_nacpRatingAgeOrganizationStrings[] = {
    [NacpRatingAgeOrganization_CERO]         = "CERO",
    [NacpRatingAgeOrganization_GRACGCRB]     = "GRACGCRB",
    [NacpRatingAgeOrganization_GSRMR]        = "GSRMR",
    [NacpRatingAgeOrganization_ESRB]         = "ESRB",
    [NacpRatingAgeOrganization_ClassInd]     = "ClassInd",
    [NacpRatingAgeOrganization_USK]          = "USK",
    [NacpRatingAgeOrganization_PEGI]         = "PEGI",
    [NacpRatingAgeOrganization_PEGIPortugal] = "PEGIPortugal",
    [NacpRatingAgeOrganization_PEGIBBFC]     = "PEGIBBFC",
    [NacpRatingAgeOrganization_Russian]      = "Russian",
    [NacpRatingAgeOrganization_ACB]          = "ACB",
    [NacpRatingAgeOrganization_OFLC]         = "OFLC",
    [NacpRatingAgeOrganization_IARCGeneric]  = "IARCGeneric"
};













/* Function prototypes. */

static bool nacpAddStringFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const char *value);
static bool nacpAddEnumFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u8 value, NacpStringFunction str_func);
static bool nacpAddBitflagFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const void *flag, u8 max_flag_idx, NacpStringFunction str_func);
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
    out->data = malloc(sizeof(_NacpStruct));
    if (!out->data)
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
    for(u8 i = 0; i < NacpLanguage_Count; i++)
    {
        NacpIconContext *icon_ctx = NULL;
        
        /* Check if the current language is supported. */
        if (!nacpCheckBitflagField(&(out->data->supported_language_flag), i, NacpLanguage_Count)) continue;
        
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

bool nacpGenerateAuthoringToolXml(NacpContext *nacp_ctx)
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
    
    /* UserAccountSwitchLock. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "UserAccountSwitchLock", nacp->user_account_switch_lock, &nacpGetUserAccountSwitchLockString)) goto end;
    
    /* AddOnContentRegistrationType. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "AddOnContentRegistrationType", nacp->add_on_content_registration_type, &nacpGetAddOnContentRegistrationTypeString)) goto end;
    
    /* AttributeFlag. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Attribute", &(nacp->attribute_flag), NacpAttribute_Count, &nacpGetAttributeString)) goto end;
    
    /* SupportedLanguageFlag. */
    /* Even though this is a bitflag field, it doesn't follow the same format as the rest. */
    for(i = 0, count = 0; i < NacpLanguage_Count; i++)
    {
        if (!nacpCheckBitflagField(&(nacp->supported_language_flag), i, NacpLanguage_Count)) continue;
        if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "SupportedLanguage", i, &nacpGetLanguageString)) goto end;
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <SupportedLanguage />\n")) goto end;
    
    /* ParentalControlFlag. */
    if (!nacpAddBitflagFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "ParentalControl", &(nacp->parental_control_flag), NacpParentalControl_Count, &nacpGetParentalControlString)) goto end;
    
    /* Screenshot. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "Screenshot", nacp->screenshot, &nacpGetScreenshotString)) goto end;
    
    /* VideoCapture. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "VideoCapture", nacp->video_capture, &nacpGetVideoCaptureString)) goto end;
    
    /* DataLossConfirmation. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DataLossConfirmation", nacp->data_loss_confirmation, &nacpGetDataLossConfirmationString)) goto end;
    
    /* PlayLogPolicy. */
    if (!nacpAddEnumFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "PlayLogPolicy", nacp->play_log_policy, &nacpGetPlayLogPolicyString)) goto end;
    
    /* PresenceGroupId. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "PresenceGroupId", nacp->presence_group_id)) goto end;
    
    /* RatingAge. */
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
    
    /* DisplayVersion. */
    if (!nacpAddStringFieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "DisplayVersion", nacp->display_version)) goto end;
    
    /* AddOnContentBaseId. */
    if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "AddOnContentBaseId", nacp->add_on_content_base_id)) goto end;
    
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
    
    /* LocalCommunicationId. */
    for(i = 0, count = 0; i < 0x8; i++)
    {
        if (!nacp->local_communication_id[i]) continue;
        if (!nacpAddU64FieldToAuthoringToolXml(&xml_buf, &xml_buf_size, "LocalCommunicationId", nacp->local_communication_id[i])) goto end;
        count++;
    }
    
    if (!count && !utilsAppendFormattedStringToBuffer(&xml_buf, &xml_buf_size, "  <LocalCommunicationId />\n")) goto end;
    
    
    
    
    
    
    
    
    
    
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




















static bool nacpAddStringFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const char *value)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name) || !value) return false;
    return (strlen(value) ? utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>%s</%s>\n", tag_name, value, tag_name) : \
            utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s />\n", tag_name));
}

static bool nacpAddEnumFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u8 value, NacpStringFunction str_func)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name) || !str_func) return false;
    return utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>%s</%s>\n", tag_name, str_func(value), tag_name);
}

static bool nacpAddBitflagFieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, const void *flag, u8 max_flag_idx, NacpStringFunction str_func)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name) || !flag || max_flag_idx >= 0x20 || !str_func) return false;
    
    u8 i = 0, count = 0;
    bool success = false;
    
    if (!utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>", tag_name)) goto end;
    
    if (*((u32*)flag))
    {
        for(i = 0; i < max_flag_idx; i++)
        {
            if (!nacpCheckBitflagField(flag, i, max_flag_idx)) continue;
            if (count && !utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, ",")) goto end;
            if (!utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "%s", str_func(i))) goto end;
            count++;
        }
        
        if (!count && !utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "%s", g_unknownString)) goto end;
    } else {
        if (!utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "None")) goto end;
    }
    
    success = utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "</%s>\n", tag_name);
    
end:
    return success;
}

static bool nacpAddU64FieldToAuthoringToolXml(char **xml_buf, u64 *xml_buf_size, const char *tag_name, u64 value)
{
    if (!xml_buf || !xml_buf_size || !tag_name || !strlen(tag_name)) return false;
    return utilsAppendFormattedStringToBuffer(xml_buf, xml_buf_size, "  <%s>0x%016lx</%s>\n", tag_name, value, tag_name);
}
