/*
 * gamecard_tab.cpp
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    static const char *GameCardFwVersionStrings[GameCardFwVersion_Count] = {
        [GameCardFwVersion_ForDev]       = "1.0.0+",
        [GameCardFwVersion_Since100NUP]  = "1.0.0+",
        [GameCardFwVersion_Since400NUP]  = "4.0.0+",
        [GameCardFwVersion_Since900NUP]  = "9.0.0+",
        [GameCardFwVersion_Since1100NUP] = "11.0.0+",
        [GameCardFwVersion_Since1200NUP] = "12.0.0+"
    };
    
    static const char *GameCardCompatibilityTypeStrings[GameCardCompatibilityType_Count] = {
        [GameCardCompatibilityType_Normal] = "Normal",
        [GameCardCompatibilityType_Terra]  = "Terra"
    };
    
    GameCardTab::GameCardTab(nxdt::tasks::GameCardTask *gc_status_task) : LayeredErrorFrame("gamecard_tab/error_frame/not_inserted"_i18n), gc_status_task(gc_status_task)
    {
        /* Set custom spacing. */
        this->list->setSpacing(this->list->getSpacing() / 2);
        this->list->setMarginBottom(20);
        
        /* Gamecard properties table. */
        this->list->addView(new brls::Header("gamecard_tab/list/properties_table/header"_i18n));
        
        this->properties_table = new FocusableTable(false);
        this->capacity = this->properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/capacity"_i18n);
        this->total_size = this->properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/total_size"_i18n);
        this->trimmed_size = this->properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/trimmed_size"_i18n);
        this->update_version = this->properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/update_version"_i18n);
        this->lafw_version = this->properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/lafw_version"_i18n);
        this->sdk_version = this->properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/sdk_version"_i18n);
        this->compatibility_type = this->properties_table->addRow(brls::TableRowType::BODY, "gamecard_tab/list/properties_table/compatibility_type"_i18n);
        this->list->addView(this->properties_table);
        
        /* ListItem elements. */
        this->list->addView(new brls::Header("gamecard_tab/list/dump_options"_i18n));
        
        this->dump_card_image = new brls::ListItem("gamecard_tab/list/dump_card_image/label"_i18n, "gamecard_tab/list/dump_card_image/description"_i18n);
        this->list->addView(this->dump_card_image);
        
        this->dump_certificate = new brls::ListItem("gamecard_tab/list/dump_certificate/label"_i18n, "gamecard_tab/list/dump_certificate/description"_i18n);
        this->list->addView(this->dump_certificate);
        
        this->dump_header = new brls::ListItem("gamecard_tab/list/dump_header/label"_i18n, "gamecard_tab/list/dump_header/description"_i18n);
        this->list->addView(this->dump_header);
        
        this->dump_decrypted_cardinfo = new brls::ListItem("gamecard_tab/list/dump_decrypted_cardinfo/label"_i18n, "gamecard_tab/list/dump_decrypted_cardinfo/description"_i18n);
        this->list->addView(this->dump_decrypted_cardinfo);
        
        this->dump_initial_data = new brls::ListItem("gamecard_tab/list/dump_initial_data/label"_i18n, "gamecard_tab/list/dump_initial_data/description"_i18n);
        this->list->addView(this->dump_initial_data);
        
        this->dump_hfs_partitions = new brls::ListItem("gamecard_tab/list/dump_hfs_partitions/label"_i18n, "gamecard_tab/list/dump_hfs_partitions/description"_i18n);
        this->list->addView(this->dump_hfs_partitions);
        
        /* Subscribe to gamecard status event. */
        this->gc_status_task_sub = this->gc_status_task->RegisterListener([this](GameCardStatus gc_status) {
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
                {
                    GameCardInfo card_info = {0};
                    gamecardGetDecryptedCardInfoArea(&card_info);
                    
                    this->capacity->setValue(this->GetFormattedSizeString(&gamecardGetRomCapacity));
                    this->total_size->setValue(this->GetFormattedSizeString(&gamecardGetTotalSize));
                    this->trimmed_size->setValue(this->GetFormattedSizeString(&gamecardGetTrimmedSize));
                    
                    const VersionType1 *upp_version = &(card_info.upp_version);
                    this->update_version->setValue(fmt::format("{}.{}.{}-{}.{} (v{})", upp_version->major, upp_version->minor, upp_version->micro, upp_version->major_relstep, \
                                                                                       upp_version->minor_relstep, upp_version->value));
                    
                    u64 fw_version = card_info.fw_version;
                    this->lafw_version->setValue(fmt::format("{} ({})", fw_version, fw_version >= GameCardFwVersion_Count ? "generic/unknown"_i18n : GameCardFwVersionStrings[fw_version]));
                    
                    const VersionType2 *fw_mode = &(card_info.fw_mode);
                    this->sdk_version->setValue(fmt::format("{}.{}.{}-{} (v{})", fw_mode->major, fw_mode->minor, fw_mode->micro, fw_mode->relstep, fw_mode->value));
                    
                    u8 compatibility_type = card_info.compatibility_type;
                    this->compatibility_type->setValue(fmt::format("{} ({})", \
                                                                   compatibility_type >= GameCardCompatibilityType_Count ? "generic/unknown"_i18n : GameCardCompatibilityTypeStrings[compatibility_type], \
                                                                   compatibility_type));
                    
                    this->SwitchLayerView(false);
                    
                    break;
                }
                default:
                    break;
            }
            
            this->gc_status = gc_status;
        });
    }
    
    GameCardTab::~GameCardTab(void)
    {
        /* Unregister task listener. */
        this->gc_status_task->UnregisterListener(this->gc_status_task_sub);
    }
    
    std::string GameCardTab::GetFormattedSizeString(GameCardSizeFunc func)
    {
        u64 size = 0;
        char strbuf[0x40] = {0};
        
        func(&size);
        utilsGenerateFormattedSizeString(static_cast<double>(size), strbuf, sizeof(strbuf));
        
        return std::string(strbuf);
    }
}
