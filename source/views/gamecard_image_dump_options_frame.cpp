/*
 * gamecard_image_dump_options_frame.cpp
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

#include <views/gamecard_image_dump_options_frame.hpp>
#include <views/gamecard_image_dump_task_frame.hpp>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

#define GAMECARD_TOGGLE_ITEM(name, ...) \
do { \
    this->name = new brls::ToggleListItem("dump_options/gamecard/image/" #name "/label"_i18n, configGetBoolean("gamecard/" #name), \
                                          i18n::getStr("dump_options/gamecard/image/" #name "/description", ##__VA_ARGS__), \
                                          "generic/value_enabled"_i18n, "generic/value_disabled"_i18n); \
    this->name->getClickEvent()->subscribe([](brls::View *view) { \
        brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view); \
        configSetBoolean("gamecard/" #name, item->getToggleState()); \
        LOG_MSG_DEBUG("\"" #name "\" setting changed by user."); \
    }); \
    this->addView(this->name); \
} while(0)

namespace nxdt::views
{
    GameCardImageDumpOptionsFrame::GameCardImageDumpOptionsFrame(RootView *root_view, std::string raw_filename) :
        DumpOptionsFrame(root_view, "gamecard_tab/list/dump_card_image/label"_i18n, std::string(GAMECARD_SUBDIR), raw_filename)
    {
        /* Subscribe to the gamecard task event. */
        this->gc_task_sub = this->root_view->RegisterGameCardStatusTaskListener([this](const GameCardStatus& gc_status) {
            /* Realistically speaking, this should always match a NotInserted status, but it's always better to be safe than sorry. */
            if (gc_status != GameCardStatus_NotInserted) return;

            /* Fire gamecard ejection event. */
            this->gc_ejected_event.fire();

            /* Pop view from stack immediately. */
            brls::Application::popView();
        });

        /* "Prepend KeyArea data" toggle. */
        GAMECARD_TOGGLE_ITEM(prepend_key_area);

        /* "Keep certificate" toggle. */
        GAMECARD_TOGGLE_ITEM(keep_certificate);

        /* "Trim dump" toggle. */
        GAMECARD_TOGGLE_ITEM(trim_dump);

        /* "Calculate checksum" toggle. */
        GAMECARD_TOGGLE_ITEM(calculate_checksum);

        /* "Lookup checksum" toggle. */
        GAMECARD_TOGGLE_ITEM(lookup_checksum, "dump_options/gamecard/image/calculate_checksum/label"_i18n, "No-Intro");

        /* Register dump button callback. */
        this->RegisterButtonListener([this](brls::View *view) {
            /* Retrieve configuration values set by the user. */
            bool prepend_key_area_val = this->prepend_key_area->getToggleState();
            bool keep_certificate_val = this->keep_certificate->getToggleState();
            bool trim_dump_val = this->trim_dump->getToggleState();
            bool calculate_checksum_val = this->calculate_checksum->getToggleState();
            bool lookup_checksum_val = this->lookup_checksum->getToggleState();

            /* Generate file extension. */
            std::string extension = fmt::format(" [{}][{}][{}].xci", prepend_key_area_val ? "KA" : "NKA", keep_certificate_val ? "C" : "NC", trim_dump_val ? "T" : "NT");

            /* Get output path. */
            std::string output_path{};
            if (!this->GetOutputFilePath(extension, output_path)) return;

            /* Display task frame. */
            brls::Application::pushView(new GameCardImageDumpTaskFrame(output_path, prepend_key_area_val, keep_certificate_val, trim_dump_val, calculate_checksum_val,
                                        lookup_checksum_val), brls::ViewAnimation::SLIDE_LEFT, false);
        });
    }

    GameCardImageDumpOptionsFrame::~GameCardImageDumpOptionsFrame()
    {
        /* Unregister all gamecard ejection event listeners. */
        this->gc_ejected_event.unsubscribeAll();

        /* Unregister gamecard task listener. */
        this->root_view->UnregisterGameCardStatusTaskListener(this->gc_task_sub);
    }
}
