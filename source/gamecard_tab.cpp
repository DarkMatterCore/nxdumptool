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

#include <nxdt_utils.h>
#include <gamecard_tab.hpp>
#include <titles_tab.hpp>
#include <dump_options_frame.hpp>

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
        this->gc_status_task_sub = this->root_view->RegisterGameCardTaskListener([this](GameCardStatus gc_status) {
            /* Process gamecard status. */
            this->ProcessGameCardStatus(gc_status);
        });

        /* Process gamecard status. */
        this->ProcessGameCardStatus(GameCardStatus_NotInserted);
    }

    GameCardTab::~GameCardTab(void)
    {
        /* Unregister task listener. */
        this->root_view->UnregisterGameCardTaskListener(this->gc_status_task_sub);
    }

    void GameCardTab::ProcessGameCardStatus(GameCardStatus gc_status)
    {
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
                this->error_frame->SetMessage(i18n::getStr("gamecard_tab/error_frame/info_not_loaded"_i18n, GITHUB_NEW_ISSUE_URL));
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
    }

    std::string GameCardTab::GetFormattedSizeString(GameCardSizeFunc func)
    {
        u64 size = 0;
        char strbuf[0x40] = {0};

        func(&size);
        utilsGenerateFormattedSizeString(static_cast<double>(size), strbuf, sizeof(strbuf));

        return std::string(strbuf);
    }

    std::string GameCardTab::GetCardIdSetString(FsGameCardIdSet *card_id_set)
    {
        char card_id_set_str[0x20] = {0};

        utilsGenerateHexString(card_id_set_str, sizeof(card_id_set_str), &(card_id_set->id1), sizeof(card_id_set->id1), true);

        card_id_set_str[8] = ' ';
        utilsGenerateHexString(card_id_set_str + 9, sizeof(card_id_set_str) - 9, &(card_id_set->id2), sizeof(card_id_set->id2), true);

        card_id_set_str[17] = ' ';
        utilsGenerateHexString(card_id_set_str + 18, sizeof(card_id_set_str) - 18, &(card_id_set->id3), sizeof(card_id_set->id3), true);

        return std::string(card_id_set_str);
    }

    void GameCardTab::PopulateList(void)
    {
        TitleApplicationMetadata **app_metadata = NULL;
        u32 app_metadata_count = 0;
        GameCardHeader card_header = {0};
        GameCardInfo card_info = {0};
        FsGameCardIdSet card_id_set = {0};

        bool update_focused_view = this->IsListItemFocused();
        int focus_stack_index = this->GetFocusStackViewIndex();

        /* Clear list. */
        this->list->clear();
        this->list->invalidate(true);

        /* Information about how to handle HOS launch errors. */
        /* TODO: remove this if we ever find a way to fix this issue. */
        FocusableLabel *launch_error_info = new FocusableLabel(true, false, brls::LabelStyle::DESCRIPTION, "gamecard_tab/list/launch_error_info"_i18n, true);
        launch_error_info->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->list->addView(launch_error_info);

        /* Retrieve gamecard application metadata. */
        app_metadata = titleGetGameCardApplicationMetadataEntries(&app_metadata_count);
        if (app_metadata)
        {
            /* Display the applications that are part of the inserted gamecard. */
            this->list->addView(new brls::Header("gamecard_tab/list/user_titles/header"_i18n));

            /* Information about how to handle user titles. */
            brls::Label *user_titles_info = new brls::Label(brls::LabelStyle::DESCRIPTION, i18n::getStr("gamecard_tab/list/user_titles/info"_i18n, \
                                                            "root_view/tabs/user_titles"_i18n), true);
            user_titles_info->setHorizontalAlign(NVG_ALIGN_CENTER);
            this->list->addView(user_titles_info);

            /* Populate list. */
            for(u32 i = 0; i < app_metadata_count; i++)
            {
                TitlesTabItem *title = new TitlesTabItem(app_metadata[i], false, false);
                title->unregisterAction(brls::Key::A);
                this->list->addView(title);
            }

            /* Free application metadata array. */
            free(app_metadata);
        }

        /* Populate gamecard properties table. */
        this->list->addView(new brls::Header("gamecard_tab/list/properties_table/header"_i18n));

        FocusableTable *properties_table = new FocusableTable(true, false);
        brls::TableRow *capacity = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/capacity"_i18n);
        brls::TableRow *total_size = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/total_size"_i18n);
        brls::TableRow *trimmed_size = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/trimmed_size"_i18n);
        brls::TableRow *update_version = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/update_version"_i18n);
        brls::TableRow *lafw_version = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/lafw_version"_i18n);
        brls::TableRow *sdk_version = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/sdk_version"_i18n);
        brls::TableRow *compatibility_type = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/compatibility_type"_i18n);
        brls::TableRow *package_id = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/package_id"_i18n);
        brls::TableRow *card_id_set_row = properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/card_id_set"_i18n);

        capacity->setValue(this->GetFormattedSizeString(&gamecardGetRomCapacity));
        total_size->setValue(this->GetFormattedSizeString(&gamecardGetTotalSize));
        trimmed_size->setValue(this->GetFormattedSizeString(&gamecardGetTrimmedSize));

        gamecardGetHeader(&card_header);
        gamecardGetPlaintextCardInfoArea(&card_info);
        gamecardGetCardIdSet(&card_id_set);

        const SystemVersion upp_version = card_info.upp_version.system_version;

        /* TODO: move somewhere else? */
        if (upp_version.major == 0 && upp_version.minor == 0)
        {
            std::string upp_version_display = "";

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
                update_version->setValue(fmt::format("{} ({}.{}.{}-{}) (v{})", upp_version_display, upp_version.major, upp_version.minor, upp_version.micro, \
                                                                               upp_version.relstep, upp_version.value));
            } else {
                update_version->setValue(fmt::format("{}.{}.{}-{} (v{})", upp_version.major, upp_version.minor, upp_version.micro, \
                                                                          upp_version.relstep, upp_version.value));
            }
        } else {
            update_version->setValue(fmt::format("{}.{}.{}-{}.{} (v{})", upp_version.major, upp_version.minor, upp_version.micro, \
                                                                         upp_version.major_relstep, upp_version.minor_relstep, upp_version.value));
        }

        u64 fw_version = card_info.fw_version;
        lafw_version->setValue(fmt::format("{} ({})", fw_version, fw_version >= GameCardFwVersion_Count ? "generic/unknown"_i18n : gamecardGetRequiredHosVersionString(fw_version)));

        const SdkAddOnVersion fw_mode = card_info.fw_mode.sdk_addon_version;
        sdk_version->setValue(fmt::format("{}.{}.{}-{} (v{})", fw_mode.major, fw_mode.minor, fw_mode.micro, fw_mode.relstep, fw_mode.value));

        u8 compat_type = card_info.compatibility_type;
        compatibility_type->setValue(fmt::format("{} ({})", \
                                                        compat_type >= GameCardCompatibilityType_Count ? "generic/unknown"_i18n : gamecardGetCompatibilityTypeString(compat_type), \
                                                        compat_type));

        char package_id_str[0x11] = {0};
        utilsGenerateHexString(package_id_str, sizeof(package_id_str), card_header.package_id, sizeof(card_header.package_id), true);
        package_id->setValue(std::string(package_id_str));

        card_id_set_row->setValue(this->GetCardIdSetString(&card_id_set));

        this->list->addView(properties_table);

        /* ListItem elements. */
        this->list->addView(new brls::Header("gamecard_tab/list/dump_options"_i18n));

        brls::ListItem *dump_card_image = new brls::ListItem("gamecard_tab/list/dump_card_image/label"_i18n, "gamecard_tab/list/dump_card_image/description"_i18n);

        dump_card_image->getClickEvent()->subscribe([this](brls::View *view) {
            char *raw_filename = titleGenerateGameCardFileName(configGetInteger("naming_convention"), TitleFileNameIllegalCharReplaceType_None);
            if (!raw_filename) return;

            brls::Image *icon = new brls::Image();
            icon->setImage(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
            icon->setScaleType(brls::ImageScaleType::SCALE);

            brls::Application::pushView(new DumpOptionsFrame(this->root_view, "gamecard_tab/list/dump_card_image/label"_i18n, icon, raw_filename, ".xci"), brls::ViewAnimation::SLIDE_LEFT);
        });

        this->list->addView(dump_card_image);

        this->list->addView(new brls::ListItemGroupSpacing(true));

        brls::Label *advanced_disclaimer = new brls::Label(brls::LabelStyle::DESCRIPTION, "gamecard_tab/list/advanced_disclaimer"_i18n, true);
        advanced_disclaimer->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->list->addView(advanced_disclaimer);

        this->list->addView(new brls::ListItemGroupSpacing(true));

        brls::ListItem *dump_initial_data = new brls::ListItem("gamecard_tab/list/dump_initial_data/label"_i18n, "gamecard_tab/list/dump_initial_data/description"_i18n);
        this->list->addView(dump_initial_data);

        brls::ListItem *dump_certificate = new brls::ListItem("gamecard_tab/list/dump_certificate/label"_i18n, fmt::format("gamecard_tab/list/dump_certificate/description"_i18n, GAMECARD_CERTIFICATE_OFFSET / GAMECARD_PAGE_SIZE));
        this->list->addView(dump_certificate);

        brls::ListItem *dump_card_id_set = new brls::ListItem("gamecard_tab/list/dump_card_id_set/label"_i18n, "gamecard_tab/list/dump_card_id_set/description"_i18n);
        this->list->addView(dump_card_id_set);

        brls::ListItem *dump_card_uid = new brls::ListItem("gamecard_tab/list/dump_card_uid/label"_i18n, "gamecard_tab/list/dump_card_uid/description"_i18n);
        this->list->addView(dump_card_uid);

        brls::ListItem *dump_header = new brls::ListItem("gamecard_tab/list/dump_header/label"_i18n, fmt::format("gamecard_tab/list/dump_header/description"_i18n, 0));
        this->list->addView(dump_header);

        brls::ListItem *dump_plaintext_cardinfo = new brls::ListItem("gamecard_tab/list/dump_plaintext_cardinfo/label"_i18n, "gamecard_tab/list/dump_plaintext_cardinfo/description"_i18n);
        this->list->addView(dump_plaintext_cardinfo);

        brls::ListItem *dump_specific_data = new brls::ListItem("gamecard_tab/list/dump_specific_data/label"_i18n, "gamecard_tab/list/dump_specific_data/description"_i18n);
        this->list->addView(dump_specific_data);

        brls::ListItem *dump_hfs_partitions = new brls::ListItem("gamecard_tab/list/dump_hfs_partitions/label"_i18n, "gamecard_tab/list/dump_hfs_partitions/description"_i18n);
        this->list->addView(dump_hfs_partitions);

        brls::ListItem *browse_hfs_partitions = new brls::ListItem("gamecard_tab/list/browse_hfs_partitions/label"_i18n, "gamecard_tab/list/browse_hfs_partitions/description"_i18n);
        this->list->addView(browse_hfs_partitions);

        brls::ListItem *dump_lafw = new brls::ListItem("gamecard_tab/list/dump_lafw/label"_i18n, "gamecard_tab/list/dump_lafw/description"_i18n);
        this->list->addView(dump_lafw);

        /* Update focus stack, if needed. */
        if (focus_stack_index > -1) this->UpdateFocusStackViewAtIndex(focus_stack_index, this->GetListFirstFocusableChild());

        /* Switch to the list view. */
        this->list->invalidate(true);
        this->SwitchLayerView(false, update_focused_view, focus_stack_index < 0);
    }
}
