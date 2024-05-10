/*
 * gamecard_tab.cpp
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include <core/nxdt_utils.h>
#include <views/gamecard_tab.hpp>
#include <views/titles_tab.hpp>
#include <views/gamecard_image_dump_options_frame.hpp>
#include <utils/scope_guard.hpp>

#define GAMECARD_TAB_TABLE_PROPERTY(name)           brls::TableRow *name = properties_table->addRow(brls::TableRowType::BODY, i18n::getStr("gamecard_tab/list/properties_table/" #name))

#define GAMECARD_TAB_LISTITEM_ELEMENT(name, ...)    \
brls::ListItem *name = new brls::ListItem(i18n::getStr("gamecard_tab/list/" #name "/label"), i18n::getStr("gamecard_tab/list/" #name "/description", ##__VA_ARGS__)); \
this->list->addView(name)

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    GameCardTab::GameCardTab(RootView *root_view) : LayeredErrorFrame("gamecard_tab/error_frame/not_inserted"_i18n), root_view(root_view)
    {
        /* Set custom spacing. */
        this->list->setSpacing(this->list->getSpacing() / 2);
        this->list->setMarginBottom(20);

        /* Subscribe to the gamecard status event. */
        this->gc_status_task_sub = this->root_view->RegisterGameCardStatusTaskListener([this](const GameCardStatus& gc_status) {
            /* Process gamecard status. */
            this->ProcessGameCardStatus(gc_status);
        });

        /* Process gamecard status. */
        this->ProcessGameCardStatus(GameCardStatus_NotInserted);
    }

    GameCardTab::~GameCardTab()
    {
        /* Unregister task listener. */
        this->root_view->UnregisterGameCardStatusTaskListener(this->gc_status_task_sub);
    }

    void GameCardTab::ProcessGameCardStatus(const GameCardStatus& gc_status)
    {
        LOG_MSG_DEBUG("Processing gamecard status: %u.", gc_status);

        /* Block user inputs. */
        brls::Application::blockInputs();

        /* Switch to the error layer if gamecard info hasn't been loaded. */
        if (gc_status < GameCardStatus_InsertedAndInfoLoaded) this->SwitchLayerView(true);

        switch(gc_status)
        {
            case GameCardStatus_NotInserted:
                this->error_frame->SetMessage("gamecard_tab/error_frame/not_inserted"_i18n);
                break;
            case GameCardStatus_Processing:
                this->error_frame->SetMessage("gamecard_tab/error_frame/processing"_i18n);
                break;
            case GameCardStatus_NoGameCardPatchEnabled:
                this->error_frame->SetMessage("gamecard_tab/error_frame/nogc_enabled"_i18n);
                break;
            case GameCardStatus_LotusAsicFirmwareUpdateRequired:
                this->error_frame->SetMessage("gamecard_tab/error_frame/lafw_update_required"_i18n);
                break;
            case GameCardStatus_InsertedAndInfoNotLoaded:
                this->error_frame->SetMessage(i18n::getStr("gamecard_tab/error_frame/info_not_loaded", GITHUB_NEW_ISSUE_URL));
                break;
            case GameCardStatus_InsertedAndInfoLoaded:
                /* Update list and switch to it. */
                this->PopulateList();
                break;
            default:
                break;
        }

        /* Update internal gamecard status. */
        this->gc_status = gc_status;

        /* Unlock user inputs. */
        brls::Application::unblockInputs();
    }

    void GameCardTab::PopulateList(void)
    {
        bool update_focused_view = this->IsListItemFocused();
        int focus_stack_index = this->GetFocusStackViewIndex();

        /* Clear list. */
        this->list->clear();
        this->list->invalidate(true);

        /* Generate and store raw filenames. */
        this->GenerateRawFilenames();

        /* Information about how to handle HOS launch errors. */
        /* TODO: remove this if we ever find a way to fix this issue. */
        FocusableLabel *launch_error_info = new FocusableLabel(true, false, brls::LabelStyle::DESCRIPTION, "gamecard_tab/list/launch_error_info"_i18n, true);
        launch_error_info->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->list->addView(launch_error_info);

        /* Add gamecard application metadata information. */
        this->AddApplicationMetadataItems();

        /* Add gamecard properties table. */
        this->AddPropertiesTable();

        /* Add ListItem elements. */
        this->list->addView(new brls::Header("gamecard_tab/list/dump_options"_i18n));

        GAMECARD_TAB_LISTITEM_ELEMENT(dump_card_image);

        this->list->addView(new brls::ListItemGroupSpacing(true));

        brls::Label *advanced_disclaimer = new brls::Label(brls::LabelStyle::DESCRIPTION, "gamecard_tab/list/advanced_disclaimer"_i18n, true);
        advanced_disclaimer->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->list->addView(advanced_disclaimer);

        GAMECARD_TAB_LISTITEM_ELEMENT(dump_initial_data);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_certificate, GAMECARD_CERT_OFFSET / GAMECARD_PAGE_SIZE);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_card_id_set);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_card_uid);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_header, 0);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_plaintext_cardinfo);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_specific_data);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_hfs_partitions);
        GAMECARD_TAB_LISTITEM_ELEMENT(browse_hfs_partitions);
        GAMECARD_TAB_LISTITEM_ELEMENT(dump_lafw);

        /* Set ListItem callbacks. */
        dump_card_image->getClickEvent()->subscribe([this](brls::View *view) {
            /* Display gamecard image dump options. */
            std::string& raw_filename = (configGetInteger("naming_convention") == TitleNamingConvention_Full ? raw_filename_full : raw_filename_id_only);
            brls::Application::pushView(new GameCardImageDumpOptionsFrame(this->root_view, raw_filename), brls::ViewAnimation::SLIDE_LEFT);
        });

        /* Update focus stack, if needed. */
        if (focus_stack_index > -1) this->UpdateFocusStackViewAtIndex(focus_stack_index, this->GetListFirstFocusableChild());

        /* Switch to the list view. */
        this->list->invalidate(true);
        this->SwitchLayerView(false, update_focused_view, focus_stack_index < 0);
    }

    void GameCardTab::AddApplicationMetadataItems(void)
    {
        TitleGameCardApplicationMetadata *gc_app_metadata = nullptr;
        u32 gc_app_metadata_count = 0;

        /* Retrieve gamecard application metadata. */
        gc_app_metadata = titleGetGameCardApplicationMetadataEntries(&gc_app_metadata_count);
        if (!gc_app_metadata) return;

        ON_SCOPE_EXIT { free(gc_app_metadata); };

        /* Display the applications that are part of the inserted gamecard. */
        this->list->addView(new brls::Header("gamecard_tab/list/user_titles/header"_i18n));

        /* Add information about how to work with individual user titles. */
        brls::Label *user_titles_info = new brls::Label(brls::LabelStyle::DESCRIPTION, i18n::getStr("gamecard_tab/list/user_titles/info", \
                                                        "root_view/tabs/user_titles"_i18n), true);
        user_titles_info->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->list->addView(user_titles_info);

        /* Add gamecard application metadata items. */
        for(u32 i = 0; i < gc_app_metadata_count; i++)
        {
            TitleGameCardApplicationMetadata *cur_gc_app_metadata = &(gc_app_metadata[i]);

            /* Create item. */
            TitlesTabItem *title = new TitlesTabItem(cur_gc_app_metadata->app_metadata, false, false);

            /* Unregister A button action. */
            title->unregisterAction(brls::Key::A);

            /* Set version information as the item sublabel instead of the title author. */
            std::string sublabel = fmt::format("v{}", cur_gc_app_metadata->version.value);

            if (cur_gc_app_metadata->display_version[0]) sublabel += fmt::format(" ({})", cur_gc_app_metadata->display_version);

            if (cur_gc_app_metadata->dlc_count > 1)
            {
                sublabel += fmt::format(" (+{} DLCs)", cur_gc_app_metadata->dlc_count);
            } else
            if (cur_gc_app_metadata->dlc_count == 1)
            {
                sublabel += " (+1 DLC)";
            }

            title->setSubLabel(sublabel);

            /* Add view to item list. */
            this->list->addView(title);
        }
    }

    void GameCardTab::AddPropertiesTable(void)
    {
        GameCardHeader card_header{};
        GameCardInfo card_info{};
        FsGameCardIdSet card_id_set_data{};

        /* Get gamecard data. */
        gamecardGetHeader(&card_header);
        gamecardGetPlaintextCardInfoArea(&card_info);
        gamecardGetCardIdSet(&card_id_set_data);

        /* Populate gamecard properties table. */
        this->list->addView(new brls::Header("gamecard_tab/list/properties_table/header"_i18n));

        FocusableTable *properties_table = new FocusableTable(true, false);
        GAMECARD_TAB_TABLE_PROPERTY(capacity);
        GAMECARD_TAB_TABLE_PROPERTY(total_size);
        GAMECARD_TAB_TABLE_PROPERTY(trimmed_size);
        GAMECARD_TAB_TABLE_PROPERTY(update_version);
        GAMECARD_TAB_TABLE_PROPERTY(lafw_version);
        GAMECARD_TAB_TABLE_PROPERTY(sdk_version);
        GAMECARD_TAB_TABLE_PROPERTY(compatibility_type);
        GAMECARD_TAB_TABLE_PROPERTY(package_id);
        GAMECARD_TAB_TABLE_PROPERTY(card_id_set);

        /* Set table row values. */
        capacity->setValue(this->GetFormattedSizeString(&gamecardGetRomCapacity));
        total_size->setValue(this->GetFormattedSizeString(&gamecardGetTotalSize));
        trimmed_size->setValue(this->GetFormattedSizeString(&gamecardGetTrimmedSize));

        const SystemVersion upp_version = card_info.upp_version.system_version;

        if (upp_version.major == 0 && upp_version.minor == 0)
        {
            /* Nintendo didn't start matching SystemVersion fields to the system version displayed in the Settings menu until HOS 3.0.0. */
            /* So we need to manually handle those exceptions. */
            std::string upp_version_display{};

            switch(upp_version.micro)
            {
                case 0: /* v450 / 0.0.0-450 */
                    upp_version_display = "1.0.0";
                    break;
                case 1: /* v65796 / 0.0.1-260 */
                    upp_version_display = "2.0.0";
                    break;
                case 2: /* v131162 / 0.0.2-90 */
                    upp_version_display = "2.1.0";
                    break;
                case 3: /* v196628 / 0.0.3-20 */
                    upp_version_display = "2.2.0";
                    break;
                case 4: /* v262164 / 0.0.4-20 */
                    upp_version_display = "2.3.0";
                    break;
                default:
                    break;
            }

            if (upp_version_display != "")
            {
                update_version->setValue(fmt::format("{} ({}.{}.{}-{}) (v{})", upp_version_display, upp_version.major, upp_version.minor, upp_version.micro,
                                                                               upp_version.relstep, upp_version.value));
            } else {
                update_version->setValue(fmt::format("{}.{}.{}-{} (v{})", upp_version.major, upp_version.minor, upp_version.micro,
                                                                          upp_version.relstep, upp_version.value));
            }
        } else {
            update_version->setValue(fmt::format("{}.{}.{}-{}.{} (v{})", upp_version.major, upp_version.minor, upp_version.micro,
                                                                         upp_version.major_relstep, upp_version.minor_relstep, upp_version.value));
        }

        const u64 fw_version = card_info.fw_version;
        lafw_version->setValue(fmt::format("{} ({})", fw_version, fw_version >= GameCardFwVersion_Count ? "generic/unknown"_i18n : gamecardGetRequiredHosVersionString(fw_version)));

        const SdkAddOnVersion fw_mode = card_info.fw_mode.sdk_addon_version;
        sdk_version->setValue(fmt::format("{}.{}.{}-{} (v{})", fw_mode.major, fw_mode.minor, fw_mode.micro, fw_mode.relstep, fw_mode.value));

        u8 compat_type = card_info.compatibility_type;
        compatibility_type->setValue(fmt::format("{} ({})",
                                                 compat_type >= GameCardCompatibilityType_Count ? "generic/unknown"_i18n : gamecardGetCompatibilityTypeString(compat_type), compat_type));

        char package_id_str[0x11] = {0};
        utilsGenerateHexString(package_id_str, sizeof(package_id_str), card_header.package_id, sizeof(card_header.package_id), true);
        package_id->setValue(std::string(package_id_str));

        card_id_set->setValue(this->GetCardIdSetString(card_id_set_data));

        this->list->addView(properties_table);
    }

    void GameCardTab::GenerateRawFilenames(void)
    {
        char *raw_filename = nullptr;

        raw_filename = titleGenerateGameCardFileName(TitleNamingConvention_Full, TitleFileNameIllegalCharReplaceType_None);
        this->raw_filename_full = std::string(raw_filename);
        if (raw_filename) free(raw_filename);

        raw_filename = titleGenerateGameCardFileName(TitleNamingConvention_IdAndVersionOnly, TitleFileNameIllegalCharReplaceType_None);
        this->raw_filename_id_only = std::string(raw_filename);
        if (raw_filename) free(raw_filename);

        raw_filename = nullptr;
    }

    std::string GameCardTab::GetFormattedSizeString(GameCardSizeFunc func)
    {
        u64 size = 0;
        char strbuf[0x40] = {0};

        func(&size);
        utilsGenerateFormattedSizeString(static_cast<double>(size), strbuf, sizeof(strbuf));

        return std::string(strbuf);
    }

    std::string GameCardTab::GetCardIdSetString(const FsGameCardIdSet& card_id_set)
    {
        char card_id_set_str[0x20] = {0};

        utilsGenerateHexString(card_id_set_str, sizeof(card_id_set_str), &(card_id_set.id1), sizeof(card_id_set.id1), true);

        card_id_set_str[8] = ' ';
        utilsGenerateHexString(card_id_set_str + 9, sizeof(card_id_set_str) - 9, &(card_id_set.id2), sizeof(card_id_set.id2), true);

        card_id_set_str[17] = ' ';
        utilsGenerateHexString(card_id_set_str + 18, sizeof(card_id_set_str) - 18, &(card_id_set.id3), sizeof(card_id_set.id3), true);

        return std::string(card_id_set_str);
    }
}
