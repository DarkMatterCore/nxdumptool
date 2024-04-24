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

#include "root_view.hpp"

namespace nxdt::views
{
    class DumpOptionsFrame: public brls::ThumbnailFrame
    {
        protected:
            RootView *root_view = nullptr;

        private:
            std::string storage_prefix{}, base_output_path{}, raw_filename{};

            brls::List *list = nullptr;
            brls::InputListItem *filename = nullptr;
            brls::SelectListItem *output_storage = nullptr;
            brls::GenericEvent *button_click_event = nullptr;

            nxdt::tasks::UmsEvent::Subscription ums_task_sub;

            bool finalized = false;

            void Initialize(const std::string& title, brls::Image *icon);

            std::string SanitizeUserFileName(void);

            void UpdateOutputStorages(const nxdt::tasks::UmsDeviceVector& ums_devices);
            void UpdateStoragePrefix(u32 selected);

        protected:
            DumpOptionsFrame(RootView *root_view, const std::string& title, const std::string& base_output_path, const std::string& raw_filename);
            DumpOptionsFrame(RootView *root_view, const std::string& title, brls::Image *icon, const std::string& base_output_path, const std::string& raw_filename);
            ~DumpOptionsFrame();

            bool onCancel(void) override final;

            void addView(brls::View *view, bool fill = false);

            bool GetOutputFilePath(const std::string& extension, std::string& output);

            ALWAYS_INLINE brls::GenericEvent::Subscription RegisterButtonListener(brls::GenericEvent::Callback cb)
            {
                return this->button_click_event->subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterButtonListener(brls::GenericEvent::Subscription subscription)
            {
                this->button_click_event->unsubscribe(subscription);
            }
    };
}

#endif  /* __DUMP_OPTIONS_FRAME_HPP__ */
