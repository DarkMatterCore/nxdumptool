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

#include <gamecard_image_dump_options_frame.hpp>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    GameCardImageDumpOptionsFrame::GameCardImageDumpOptionsFrame(RootView *root_view, std::string title, std::string raw_filename) : DumpOptionsFrame(root_view, title, raw_filename, ".xci")
    {
        /* Prepend KeyArea data. */
        this->prepend_key_area = new brls::ToggleListItem("dump_options/prepend_key_area/label"_i18n, configGetBoolean("gamecard/prepend_key_area"), "dump_options/prepend_key_area/description"_i18n,
                                                          "generic/value_enabled"_i18n, "generic/value_disabled"_i18n);

        this->prepend_key_area->getClickEvent()->subscribe([](brls::View* view) {
            /* Get current value. */
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            bool value = item->getToggleState();

            /* Update configuration. */
            configSetBoolean("gamecard/prepend_key_area", value);

            LOG_MSG_DEBUG("Prepend Key Area setting changed by user.");
        });

        this->addView(this->prepend_key_area);

        /* Keep certificate. */
        this->keep_certificate = new brls::ToggleListItem("dump_options/keep_certificate/label"_i18n, configGetBoolean("gamecard/keep_certificate"), "dump_options/keep_certificate/description"_i18n,
                                                          "generic/value_enabled"_i18n, "generic/value_disabled"_i18n);

        this->keep_certificate->getClickEvent()->subscribe([](brls::View* view) {
            /* Get current value. */
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            bool value = item->getToggleState();

            /* Update configuration. */
            configSetBoolean("gamecard/keep_certificate", value);

            LOG_MSG_DEBUG("Keep certificate setting changed by user.");
        });

        this->addView(this->keep_certificate);

        /* Trim dump. */
        this->trim_dump = new brls::ToggleListItem("dump_options/trim_dump/label"_i18n, configGetBoolean("gamecard/trim_dump"), "dump_options/trim_dump/description"_i18n, "generic/value_enabled"_i18n,
                                                   "generic/value_disabled"_i18n);

        this->trim_dump->getClickEvent()->subscribe([](brls::View* view) {
            /* Get current value. */
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            bool value = item->getToggleState();

            /* Update configuration. */
            configSetBoolean("gamecard/trim_dump", value);

            LOG_MSG_DEBUG("Trim dump setting changed by user.");
        });

        this->addView(this->trim_dump);

        this->calculate_checksum = new brls::ToggleListItem("dump_options/calculate_checksum/label"_i18n, configGetBoolean("gamecard/calculate_checksum"), "dump_options/calculate_checksum/description"_i18n,
                                                            "generic/value_enabled"_i18n, "generic/value_disabled"_i18n);

        this->calculate_checksum->getClickEvent()->subscribe([](brls::View* view) {
            /* Get current value. */
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            bool value = item->getToggleState();

            /* Update configuration. */
            configSetBoolean("gamecard/calculate_checksum", value);

            LOG_MSG_DEBUG("Calculate checksum setting changed by user.");
        });

        this->addView(this->calculate_checksum);

        /* Checksum lookup method. */
        this->checksum_lookup_method = new brls::SelectListItem("dump_options/checksum_lookup_method/label"_i18n, {
                                                                    "dump_options/checksum_lookup_method/value_00"_i18n,
                                                                    "NSWDB",
                                                                    "No-Intro"
                                                                }, configGetInteger("gamecard/checksum_lookup_method"), brls::i18n::getStr("dump_options/checksum_lookup_method/description",
                                                                "dump_options/calculate_checksum/label"_i18n, "NSWDB", NSWDB_XML_NAME, "No-Intro"));

        this->checksum_lookup_method->getValueSelectedEvent()->subscribe([this](int selected) {
            /* Make sure the current value isn't out of bounds. */
            if (selected < ConfigChecksumLookupMethod_None || selected >= ConfigChecksumLookupMethod_Count) return;

            /* Update configuration. */
            configSetInteger("gamecard/checksum_lookup_method", selected);
        });

        this->addView(this->checksum_lookup_method);

        /* Register dump button callback. */
        this->RegisterButtonListener([this](brls::View *view) {
            /* Retrieve configuration values set by the user. */
            //bool prepend_key_area = this->prepend_key_area_item->getToggleState();
            //bool keep_certificate = this->keep_certificate_item->getToggleState();
            bool trim_dump_val = this->trim_dump->getToggleState();
            //bool calculate_checksum = this->calculate_checksum_item->getToggleState();

            /* Get gamecard size. */
            u64 gc_size = 0;
            if ((!trim_dump_val && !gamecardGetTotalSize(&gc_size)) || (trim_dump_val && !gamecardGetTrimmedSize(&gc_size)) || !gc_size)
            {
                brls::Application::notify("fail");
                return;
            }

            /* Display update frame. */
            //brls::Application::pushView(new OptionsTabUpdateApplicationFrame(), brls::ViewAnimation::SLIDE_LEFT, false);
            brls::Application::notify(fmt::format("0x{:X}", gc_size));
        });
    }
}
