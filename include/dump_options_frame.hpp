/*
 * dump_options_frame.hpp
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

#pragma once

#ifndef __DUMP_OPTIONS_FRAME_HPP__
#define __DUMP_OPTIONS_FRAME_HPP__

#include <borealis.hpp>




using namespace brls::i18n::literals;





namespace nxdt::views
{
    class DumpOptionsFrame: public brls::ThumbnailFrame
    {
        private:
            RootView *root_view = nullptr;
            nxdt::tasks::UmsEvent::Subscription ums_task_sub;

            std::string raw_filename = "";
            std::string extension = "";

            brls::List *list = nullptr;
            brls::InputListItem *filename_item = nullptr;
            brls::SelectListItem *output_storage_item = nullptr;
            brls::ToggleListItem *prepend_key_area_item = nullptr;
            brls::ToggleListItem *keep_certificate_item = nullptr;
            brls::ToggleListItem *trim_dump_item = nullptr;
            brls::ToggleListItem *calculate_checksum_item = nullptr;
            brls::SelectListItem *checksum_lookup_method_item = nullptr;

            std::string SanitizeFileName(void)
            {
                char *raw_filename_dup = nullptr;

                if (raw_filename.empty() || !(raw_filename_dup = strdup(this->raw_filename.c_str()))) return "dummy";

                u8 selected = static_cast<u8>(this->output_storage_item ? this->output_storage_item->getSelectedValue() : configGetInteger("output_storage"));
                utilsReplaceIllegalCharacters(raw_filename_dup, selected == ConfigOutputStorage_SdCard);

                std::string output = std::string(raw_filename_dup);
                free(raw_filename_dup);

                return output;
            }

            void UpdateStorages(const nxdt::tasks::UmsDeviceVector* ums_devices)
            {
                if (!this->output_storage_item) return;

                std::vector<std::string> *storages = this->output_storage_item->getValues();
                storages->clear();

                size_t elem_count = (ConfigOutputStorage_Count + ums_devices->size());
                u32 selected = this->output_storage_item->getSelectedValue();

                for(size_t i = 0; i < elem_count; i++)
                {
                    if (i == 1)
                    {
                        storages->push_back("dump_options/output_storage/value_01"_i18n);
                        continue;
                    }

                    u64 total_sz = 0, free_sz = 0;
                    char total_sz_str[64] = {0}, free_sz_str[64] = {0};

                    const UsbHsFsDevice *cur_ums_device = (i >= ConfigOutputStorage_Count ? (ums_devices->data() + (i - ConfigOutputStorage_Count)) : nullptr);

                    sprintf(total_sz_str, "%s/", cur_ums_device ? cur_ums_device->name : DEVOPTAB_SDMC_DEVICE);
                    utilsGetFileSystemStatsByPath(total_sz_str, &total_sz, &free_sz);
                    utilsGenerateFormattedSizeString(total_sz, total_sz_str, sizeof(total_sz_str));
                    utilsGenerateFormattedSizeString(free_sz, free_sz_str, sizeof(free_sz_str));

                    if (cur_ums_device)
                    {
                        std::string ums_extra_info = (cur_ums_device->product_name[0] ? (std::string(cur_ums_device->product_name) + ", ") : "");
                        ums_extra_info += fmt::format("LUN {}, FS #{}, {}", cur_ums_device->lun, cur_ums_device->fs_idx, LIBUSBHSFS_FS_TYPE_STR(cur_ums_device->fs_type));
                        storages->push_back(brls::i18n::getStr("dump_options/output_storage/value_02", static_cast<int>(strlen(cur_ums_device->name + 3) - 1), cur_ums_device->name + 3, free_sz_str, total_sz_str, ums_extra_info));
                    } else {
                        storages->push_back(brls::i18n::getStr("dump_options/output_storage/value_00", free_sz_str, total_sz_str));
                    }
                }

                if (selected > ConfigOutputStorage_UsbHost)
                {
                    /* Set the SD card as the current output storage. */
                    this->output_storage_item->setSelectedValue(ConfigOutputStorage_SdCard);

                    /* Manually trigger selection event. */
                    /* This will take care of both updating the JSON configuration and saniziting the filename provided by the user. */
                    this->output_storage_item->getValueSelectedEvent()->fire(ConfigOutputStorage_SdCard);
                } else {
                    /* Set the current output storage once more. This will make sure the device string gets updated. */
                    this->output_storage_item->setSelectedValue(selected);
                }
            }

        protected:
            bool onCancel(void) override
            {
                /* Pop view. */
                brls::Application::popView(brls::ViewAnimation::SLIDE_RIGHT);

                return true;
            }

        public:
            DumpOptionsFrame(RootView *root_view, std::string title, brls::Image *icon, std::string raw_filename, std::string extension) : brls::ThumbnailFrame(), root_view(root_view), raw_filename(raw_filename), extension(extension)
            {
                /* Set UI properties. */
                this->setTitle(title);
                this->setIcon(icon);

                this->list = new brls::List();
                this->list->setSpacing(this->list->getSpacing() / 2);
                this->list->setMarginBottom(20);








                this->filename_item = new brls::InputListItem("dump_options/filename/label"_i18n, this->SanitizeFileName(), "", "dump_options/filename/description"_i18n, FS_MAX_FILENAME_LENGTH);

                this->filename_item->getClickEvent()->subscribe([this](brls::View *view) {
                    this->raw_filename = this->filename_item->getValue();
                    this->filename_item->setValue(this->SanitizeFileName());
                });

                this->list->addView(this->filename_item);








                this->output_storage_item = new brls::SelectListItem("dump_options/output_storage/label"_i18n, { "dummy0", "dummy1" }, configGetInteger("output_storage"),
                                                                     brls::i18n::getStr("dump_options/output_storage/description", GITHUB_REPOSITORY_URL));

                /* Subscribe to SelectListItem's value selected event. */
                this->output_storage_item->getValueSelectedEvent()->subscribe([this](int selected) {
                    /* Make sure the current value isn't out of bounds. */
                    if (selected < ConfigOutputStorage_SdCard || selected >= static_cast<int>(this->root_view->GetUmsDevices()->size() + ConfigOutputStorage_Count)) return;

                    /* Update configuration. */
                    if (selected == ConfigOutputStorage_SdCard || selected == ConfigOutputStorage_UsbHost) configSetInteger("output_storage", selected);

                    /* Sanitize output filename for the selected storage. */
                    this->filename_item->setValue(this->SanitizeFileName());
                });

                /* Manually update output storages vector. */
                this->UpdateStorages(this->root_view->GetUmsDevices());

                this->list->addView(this->output_storage_item);






                /* Subscribe to the UMS device event. */
                this->ums_task_sub = this->root_view->RegisterUmsTaskListener([this](const nxdt::tasks::UmsDeviceVector* ums_devices) {
                    /* Update output storages vector. */
                    this->UpdateStorages(ums_devices);
                });









                this->prepend_key_area_item = new brls::ToggleListItem("dump_options/prepend_key_area/label"_i18n, configGetBoolean("gamecard/prepend_key_area"),
                                                                       "dump_options/prepend_key_area/description"_i18n, "generic/value_enabled"_i18n,
                                                                       "generic/value_disabled"_i18n);

                this->prepend_key_area_item->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/prepend_key_area", value);

                    LOG_MSG_DEBUG("Prepend Key Area setting changed by user.");
                });

                this->list->addView(this->prepend_key_area_item);







                this->keep_certificate_item = new brls::ToggleListItem("dump_options/keep_certificate/label"_i18n, configGetBoolean("gamecard/keep_certificate"),
                                                                       "dump_options/keep_certificate/description"_i18n, "generic/value_enabled"_i18n,
                                                                       "generic/value_disabled"_i18n);

                this->keep_certificate_item->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/keep_certificate", value);

                    LOG_MSG_DEBUG("Keep certificate setting changed by user.");
                });

                this->list->addView(this->keep_certificate_item);









                this->trim_dump_item = new brls::ToggleListItem("dump_options/trim_dump/label"_i18n, configGetBoolean("gamecard/trim_dump"),
                                                                "dump_options/trim_dump/description"_i18n, "generic/value_enabled"_i18n,
                                                                "generic/value_disabled"_i18n);

                this->trim_dump_item->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/trim_dump", value);

                    LOG_MSG_DEBUG("Trim dump setting changed by user.");
                });

                this->list->addView(this->trim_dump_item);









                this->calculate_checksum_item = new brls::ToggleListItem("dump_options/calculate_checksum/label"_i18n, configGetBoolean("gamecard/calculate_checksum"),
                                                                         "dump_options/calculate_checksum/description"_i18n, "generic/value_enabled"_i18n,
                                                                         "generic/value_disabled"_i18n);

                this->calculate_checksum_item->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/calculate_checksum", value);

                    LOG_MSG_DEBUG("Calculate checksum setting changed by user.");
                });

                this->list->addView(this->calculate_checksum_item);








                this->checksum_lookup_method_item = new brls::SelectListItem("dump_options/checksum_lookup_method/label"_i18n, {
                                                                                 "dump_options/checksum_lookup_method/value_00"_i18n,
                                                                                 "NSWDB",
                                                                                 "No-Intro"
                                                                             }, configGetInteger("gamecard/checksum_lookup_method"),
                                                                             brls::i18n::getStr("dump_options/checksum_lookup_method/description",
                                                                                                "dump_options/calculate_checksum/label"_i18n, "NSWDB", NSWDB_XML_NAME, "No-Intro"));

                /* Subscribe to SelectListItem's value selected event. */
                this->checksum_lookup_method_item->getValueSelectedEvent()->subscribe([this](int selected) {
                    /* Make sure the current value isn't out of bounds. */
                    if (selected < ConfigChecksumLookupMethod_None || selected >= ConfigChecksumLookupMethod_Count) return;

                    /* Update configuration. */
                    configSetInteger("gamecard/checksum_lookup_method", selected);
                });

                this->list->addView(this->checksum_lookup_method_item);







                brls::Button *button = this->getSidebar()->getButton();
                button->setLabel("dump_options/start_dump"_i18n);
                button->getClickEvent()->subscribe([this](brls::View *view) {
                    /* Retrieve configuration values set by the user. */
                    //bool prepend_key_area = this->prepend_key_area_item->getToggleState();
                    //bool keep_certificate = this->keep_certificate_item->getToggleState();
                    bool trim_dump = this->trim_dump_item->getToggleState();
                    //bool calculate_checksum = this->calculate_checksum_item->getToggleState();

                    /* Get gamecard size. */
                    u64 gc_size = 0;
                    if ((!trim_dump && !gamecardGetTotalSize(&gc_size)) || (trim_dump && !gamecardGetTrimmedSize(&gc_size)) || !gc_size)
                    {
                        brls::Application::notify("fail");
                    }






                    /* Display update frame. */
                    //brls::Application::pushView(new OptionsTabUpdateApplicationFrame(), brls::ViewAnimation::SLIDE_LEFT, false);
                    brls::Application::notify(fmt::format("0x{:X}", gc_size));
                });







                this->setContentView(this->list);
            }

            ~DumpOptionsFrame(void)
            {
                /* Unregister task listener. */
                this->root_view->UnregisterUmsTaskListener(this->ums_task_sub);
            }
    };
}

#endif  /* __DUMP_OPTIONS_FRAME_HPP__ */
