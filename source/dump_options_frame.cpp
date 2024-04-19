/*
 * dump_options_frame.cpp
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

#include <dump_options_frame.hpp>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    DumpOptionsFrame::DumpOptionsFrame(RootView *root_view, std::string& title, std::string& raw_filename, std::string extension) : brls::ThumbnailFrame(), root_view(root_view), raw_filename(raw_filename), extension(extension)
    {
        /* Generate icon using the default image. */
        brls::Image *icon = new brls::Image();
        icon->setImage(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
        icon->setScaleType(brls::ImageScaleType::SCALE);

        /* Initialize the rest of the elements. */
        this->Initialize(title, icon);
    }

    DumpOptionsFrame::DumpOptionsFrame(RootView *root_view, std::string& title, brls::Image *icon, std::string& raw_filename, std::string extension) : brls::ThumbnailFrame(), root_view(root_view), raw_filename(raw_filename), extension(extension)
    {
        /* Initialize the rest of the elements. */
        this->Initialize(title, icon);
    }

    DumpOptionsFrame::~DumpOptionsFrame()
    {
        /* Unregister all button click event listeners. */
        this->button_click_event->unsubscribeAll();

        /* Unregister task listener. */
        this->root_view->UnregisterUmsTaskListener(this->ums_task_sub);
    }

    void DumpOptionsFrame::Initialize(std::string& title, brls::Image *icon)
    {
        /* Set UI properties. */
        this->setTitle(title);
        this->setIcon(icon);

        /* Generate List view. */
        this->list = new brls::List();
        this->list->setSpacing(this->list->getSpacing() / 2);
        this->list->setMarginBottom(20);

        /* Filename. */
        this->filename = new brls::InputListItem("dump_options/filename/label"_i18n, this->SanitizeUserFileName(), "", "dump_options/filename/description"_i18n, FS_MAX_FILENAME_LENGTH);

        this->filename->getClickEvent()->subscribe([this](brls::View *view) {
            /* Sanitize the string entered by the user. */
            this->raw_filename = this->filename->getValue();
            this->filename->setValue(this->SanitizeUserFileName());
        });

        this->list->addView(this->filename);

        /* Output storage. */
        this->output_storage = new brls::SelectListItem("dump_options/output_storage/label"_i18n, { "dummy0", "dummy1" }, configGetInteger("output_storage"), brls::i18n::getStr("dump_options/output_storage/description", GITHUB_REPOSITORY_URL));

        this->output_storage->getValueSelectedEvent()->subscribe([this](int selected) {
            /* Make sure the current value isn't out of bounds. */
            if (selected < ConfigOutputStorage_SdCard || selected >= static_cast<int>(this->root_view->GetUmsDevices().size() + ConfigOutputStorage_Count)) return;

            /* Update configuration. */
            if (selected == ConfigOutputStorage_SdCard || selected == ConfigOutputStorage_UsbHost) configSetInteger("output_storage", selected);

            /* Sanitize output filename for the selected storage. */
            this->filename->setValue(this->SanitizeUserFileName());
        });

        /* Manually update output storages vector. */
        this->UpdateOutputStorages(this->root_view->GetUmsDevices());

        this->list->addView(this->output_storage);

        /* Subscribe to the UMS device event. */
        this->ums_task_sub = this->root_view->RegisterUmsTaskListener([this](const nxdt::tasks::UmsDeviceVector& ums_devices) {
            /* Update output storages vector. */
            this->UpdateOutputStorages(ums_devices);
        });

        /* Start dump button. */
        brls::Button *button = this->getSidebar()->getButton();
        button->setLabel("dump_options/start_dump"_i18n);

        /* Retrieve button event pointer. */
        this->button_click_event = button->getClickEvent();

        /* Set content view. */
        this->setContentView(this->list);
    }

    std::string DumpOptionsFrame::SanitizeUserFileName(void)
    {
        char *raw_filename_dup = nullptr;

        if (this->raw_filename.empty() || !(raw_filename_dup = strdup(this->raw_filename.c_str()))) return "dummy";

        u8 selected = static_cast<u8>(this->output_storage ? this->output_storage->getSelectedValue() : configGetInteger("output_storage"));
        utilsReplaceIllegalCharacters(raw_filename_dup, selected == ConfigOutputStorage_SdCard);

        std::string output = std::string(raw_filename_dup);
        free(raw_filename_dup);

        return output;
    }

    void DumpOptionsFrame::UpdateOutputStorages(const nxdt::tasks::UmsDeviceVector& ums_devices)
    {
        if (!this->output_storage) return;

        std::vector<std::string> storages{};

        size_t elem_count = (ConfigOutputStorage_Count + ums_devices.size());
        u32 selected = this->output_storage->getSelectedValue();

        /* Fill storages vector. */
        for(size_t i = 0; i < elem_count; i++)
        {
            if (i == 1)
            {
                storages.push_back("dump_options/output_storage/value_01"_i18n);
                continue;
            }

            u64 total_sz = 0, free_sz = 0;
            char total_sz_str[64] = {0}, free_sz_str[64] = {0};

            const nxdt::tasks::UmsDeviceVectorEntry *ums_device_entry = (i >= ConfigOutputStorage_Count ? &(ums_devices.at(i - ConfigOutputStorage_Count)) : nullptr);
            const UsbHsFsDevice *cur_ums_device = (ums_device_entry ? ums_device_entry->first : nullptr);

            sprintf(total_sz_str, "%s/", cur_ums_device ? cur_ums_device->name : DEVOPTAB_SDMC_DEVICE);
            utilsGetFileSystemStatsByPath(total_sz_str, &total_sz, &free_sz);
            utilsGenerateFormattedSizeString(total_sz, total_sz_str, sizeof(total_sz_str));
            utilsGenerateFormattedSizeString(free_sz, free_sz_str, sizeof(free_sz_str));

            if (cur_ums_device)
            {
                storages.push_back(brls::i18n::getStr("dump_options/output_storage/value_02", ums_device_entry->second, free_sz_str, total_sz_str));
            } else {
                storages.push_back(brls::i18n::getStr("dump_options/output_storage/value_00", free_sz_str, total_sz_str));
            }
        }

        /* Update SelectListItem values. */
        /* If the dropdown menu currently is being displayed, it'll be reloaded. */
        this->output_storage->updateValues(storages);

        if (selected > ConfigOutputStorage_UsbHost)
        {
            /* Set the SD card as the current output storage. */
            this->output_storage->setSelectedValue(ConfigOutputStorage_SdCard);

            /* Manually trigger selection event. */
            /* This will take care of both updating the JSON configuration and saniziting the filename provided by the user. */
            this->output_storage->getValueSelectedEvent()->fire(ConfigOutputStorage_SdCard);
        } else {
            /* Set the current output storage once more. This will make sure the selected device string gets updated. */
            this->output_storage->setSelectedValue(selected);
        }
    }

    bool DumpOptionsFrame::onCancel(void)
    {
        /* Pop view. */
        brls::Application::popView(brls::ViewAnimation::SLIDE_RIGHT);
        return true;
    }

    void DumpOptionsFrame::addView(brls::View *view, bool fill)
    {
        this->list->addView(view, fill);
    }

    std::string DumpOptionsFrame::GetFileName(void)
    {
        return this->filename->getValue();
    }

    std::string DumpOptionsFrame::GetOutputStoragePrefix(void)
    {
        std::string prefix{};

        u8 selected = static_cast<u8>(this->output_storage ? this->output_storage->getSelectedValue() : configGetInteger("output_storage"));

        switch(selected)
        {
            case ConfigOutputStorage_SdCard:
                prefix = DEVOPTAB_SDMC_DEVICE "/";
                break;
            case ConfigOutputStorage_UsbHost:
                prefix = "/";
                break;
            default:
            {
                const nxdt::tasks::UmsDeviceVector& ums_devices = this->root_view->GetUmsDevices();
                prefix = std::string(ums_devices.at(selected - ConfigOutputStorage_Count).first->name);
                break;
            }
        }

        return prefix;
    }
}
