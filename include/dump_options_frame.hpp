/*
 * dump_options_frame.hpp
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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

            char *raw_filename = NULL;
            const char *extension = NULL;

            brls::List *list = nullptr;
            brls::InputListItem *filename_input = nullptr;
            brls::SelectListItem *output_storage = nullptr;
            brls::ToggleListItem *prepend_key_area = nullptr;
            brls::ToggleListItem *keep_certificate = nullptr;
            brls::ToggleListItem *trim_dump = nullptr;
            brls::ToggleListItem *calculate_checksum = nullptr;
            brls::SelectListItem *checksum_lookup_method = nullptr;

            std::string RegenerateFileName(void)
            {
                if (!this->raw_filename) return "dummy";

                char *raw_filename_dup = strdup(this->raw_filename);
                if (!raw_filename_dup) return "dummy";

                u8 selected = static_cast<u8>(this->output_storage ? this->output_storage->getSelectedValue() : configGetInteger("output_storage"));
                utilsReplaceIllegalCharacters(raw_filename_dup, selected == ConfigOutputStorage_SdCard);

                std::string output = std::string(raw_filename_dup);
                free(raw_filename_dup);

                return output;
            }

            void UpdateRawFileName(void)
            {
                if (raw_filename) free(this->raw_filename);
                this->raw_filename = strdup(this->filename_input->getValue().c_str());
            }

            void UpdateStorages(const nxdt::tasks::UmsDeviceVector* ums_devices)
            {
                if (!this->output_storage) return;

                std::vector<std::string> *storages = this->output_storage->getValues();
                storages->clear();

                storages->push_back("dump_options/output_storage/value_00"_i18n);
                storages->push_back("dump_options/output_storage/value_01"_i18n);

                for(const UsbHsFsDevice& cur_ums_device : *ums_devices)
                {
                    std::string device_str = (std::string(cur_ums_device.name) + ", ");
                    if (cur_ums_device.product_name[0]) device_str += (std::string(cur_ums_device.product_name) + ", ");
                    device_str += fmt::format("LUN {}, FS #{}, {}", cur_ums_device.lun, cur_ums_device.fs_idx, LIBUSBHSFS_FS_TYPE_STR(cur_ums_device.fs_type));
                    storages->push_back(brls::i18n::getStr("dump_options/output_storage/value_02"_i18n, device_str));
                }

                if (this->output_storage->getSelectedValue() > ConfigOutputStorage_UsbHost)
                {
                    /* Set the SD card as the current output storage. */
                    this->output_storage->setSelectedValue(ConfigOutputStorage_SdCard);

                    /* Regenerate filename. */
                    this->output_storage->getValueSelectedEvent()->fire(ConfigOutputStorage_SdCard);
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
            DumpOptionsFrame(RootView *root_view, std::string title, brls::Image *icon, char *raw_filename, const char *extension) : brls::ThumbnailFrame(), root_view(root_view), raw_filename(raw_filename), extension(extension)
            {
                /* Set UI properties. */
                this->setTitle(title);
                this->setIcon(icon);

                this->list = new brls::List();
                this->list->setSpacing(this->list->getSpacing() / 2);
                this->list->setMarginBottom(20);








                this->filename_input = new brls::InputListItem("dump_options/filename/label"_i18n, this->RegenerateFileName(), "", "dump_options/filename/description"_i18n, 255);

                this->filename_input->getClickEvent()->subscribe([this](brls::View *view) {
                    this->UpdateRawFileName();
                    this->filename_input->setValue(this->RegenerateFileName());
                });

                this->list->addView(this->filename_input);








                this->output_storage = new brls::SelectListItem("dump_options/output_storage/label"_i18n, {
                                                                    "dump_options/output_storage/value_00"_i18n,
                                                                    "dump_options/output_storage/value_01"_i18n
                                                                }, configGetInteger("output_storage"),
                                                                brls::i18n::getStr("dump_options/output_storage/description"_i18n, GITHUB_REPOSITORY_URL));

                /* Subscribe to SelectListItem's value selected event. */
                this->output_storage->getValueSelectedEvent()->subscribe([this](int selected) {
                    /* Make sure the current value isn't out of bounds. */
                    if (selected < ConfigOutputStorage_SdCard || selected >= static_cast<int>(this->root_view->GetUmsDevices()->size() + ConfigOutputStorage_Count)) return;

                    /* Update configuration. */
                    if (selected == ConfigOutputStorage_SdCard || selected == ConfigOutputStorage_UsbHost) configSetInteger("output_storage", selected);

                    /* Update output filename. */
                    this->filename_input->setValue(this->RegenerateFileName());
                });

                /* Update output storages vector. */
                this->UpdateStorages(this->root_view->GetUmsDevices());

                this->list->addView(this->output_storage);






                /* Subscribe to the UMS device event. */
                this->ums_task_sub = this->root_view->RegisterUmsTaskListener([this](const nxdt::tasks::UmsDeviceVector* ums_devices) {
                    /* Update output storages vector. */
                    this->UpdateStorages(ums_devices);
                });









                this->prepend_key_area = new brls::ToggleListItem("dump_options/prepend_key_area/label"_i18n, configGetBoolean("gamecard/prepend_key_area"), \
                                                                  "dump_options/prepend_key_area/description"_i18n, "generic/value_enabled"_i18n, \
                                                                  "generic/value_disabled"_i18n);

                this->prepend_key_area->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/prepend_key_area", value);

                    LOG_MSG_DEBUG("Prepend Key Area setting changed by user.");
                });

                this->list->addView(this->prepend_key_area);







                this->keep_certificate = new brls::ToggleListItem("dump_options/keep_certificate/label"_i18n, configGetBoolean("gamecard/keep_certificate"), \
                                                                  "dump_options/keep_certificate/description"_i18n, "generic/value_enabled"_i18n, \
                                                                  "generic/value_disabled"_i18n);

                this->keep_certificate->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/keep_certificate", value);

                    LOG_MSG_DEBUG("Keep certificate setting changed by user.");
                });

                this->list->addView(this->keep_certificate);









                this->trim_dump = new brls::ToggleListItem("dump_options/trim_dump/label"_i18n, configGetBoolean("gamecard/trim_dump"), \
                                                           "dump_options/trim_dump/description"_i18n, "generic/value_enabled"_i18n, \
                                                           "generic/value_disabled"_i18n);

                this->trim_dump->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/trim_dump", value);

                    LOG_MSG_DEBUG("Trim dump setting changed by user.");
                });

                this->list->addView(this->trim_dump);









                this->calculate_checksum = new brls::ToggleListItem("dump_options/calculate_checksum/label"_i18n, configGetBoolean("gamecard/calculate_checksum"), \
                                                                    "dump_options/calculate_checksum/description"_i18n, "generic/value_enabled"_i18n, \
                                                                    "generic/value_disabled"_i18n);

                this->calculate_checksum->getClickEvent()->subscribe([](brls::View* view) {
                    /* Get current value. */
                    brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
                    bool value = item->getToggleState();

                    /* Update configuration. */
                    configSetBoolean("gamecard/calculate_checksum", value);

                    LOG_MSG_DEBUG("Calculate checksum setting changed by user.");
                });

                this->list->addView(this->calculate_checksum);








                this->checksum_lookup_method = new brls::SelectListItem("dump_options/checksum_lookup_method/label"_i18n, {
                                                                            "dump_options/checksum_lookup_method/value_00"_i18n,
                                                                            "NSWDB",
                                                                            "No-Intro"
                                                                        }, configGetInteger("gamecard/checksum_lookup_method"),
                                                                        brls::i18n::getStr("dump_options/checksum_lookup_method/description"_i18n,
                                                                                           "dump_options/calculate_checksum/label"_i18n, "NSWDB", NSWDB_XML_NAME, "No-Intro"));

                /* Subscribe to SelectListItem's value selected event. */
                this->checksum_lookup_method->getValueSelectedEvent()->subscribe([this](int selected) {
                    /* Make sure the current value isn't out of bounds. */
                    if (selected < ConfigChecksumLookupMethod_None || selected >= ConfigChecksumLookupMethod_Count) return;

                    /* Update configuration. */
                    configSetInteger("gamecard/checksum_lookup_method", selected);
                });

                this->list->addView(this->checksum_lookup_method);







                brls::Button *button = this->getSidebar()->getButton();
                button->setLabel("dump_options/start_dump"_i18n);
                button->getClickEvent()->subscribe([](brls::View *view) {
                    brls::Application::notify("test");
                });







                this->setContentView(this->list);
            }

            ~DumpOptionsFrame(void)
            {
                /* Unregister task listener. */
                this->root_view->UnregisterUmsTaskListener(this->ums_task_sub);

                if (this->raw_filename) free(this->raw_filename);
            }
    };
}

#endif  /* __DUMP_OPTIONS_FRAME_HPP__ */
