/*
 * root_view.hpp
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

#ifndef __ROOT_VIEW_HPP__
#define __ROOT_VIEW_HPP__

#include "tasks.hpp"

#define EVENT_SUBSCRIPTION(func_name, event_type, task_name) \
    ALWAYS_INLINE nxdt::tasks::event_type::Subscription Register##func_name##Listener(nxdt::tasks::event_type::Callback cb) { return this->task_name->RegisterListener(cb); } \
    ALWAYS_INLINE void Unregister##func_name##Listener(nxdt::tasks::event_type::Subscription subscription) { this->task_name->UnregisterListener(subscription); }

namespace nxdt::views
{
    class RootView: public brls::TabFrame
    {
        private:
            bool applet_mode = false;

            int output_storage = ConfigOutputStorage_SdCard;

            brls::Label *applet_mode_lbl = nullptr;
            brls::Label *time_lbl = nullptr;
            brls::Label *battery_icon = nullptr, *battery_percentage = nullptr;
            brls::Label *connection_icon = nullptr, *connection_status_lbl = nullptr;
            brls::Label *usb_icon = nullptr, *ums_counter_lbl = nullptr;
            brls::Label *cable_icon = nullptr, *usb_host_speed_lbl = nullptr;

            nxdt::tasks::StatusInfoTask *status_info_task = nullptr;
            nxdt::tasks::GameCardTask *gc_status_task = nullptr;
            nxdt::tasks::TitleTask *title_task = nullptr;
            nxdt::tasks::UmsTask *ums_task = nullptr;
            nxdt::tasks::UsbHostTask *usb_host_task = nullptr;

            nxdt::tasks::StatusInfoEvent::Subscription status_info_task_sub;
            nxdt::tasks::UmsEvent::Subscription ums_task_sub;
            nxdt::tasks::UsbHostEvent::Subscription usb_host_task_sub;

        protected:
            void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
            void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
            brls::View *getDefaultFocus(void) override;

        public:
            RootView();
            ~RootView();

            static std::string GetFormattedDateString(const struct tm& timeinfo);

            /* Helpers used to propagate the selected output storage throughout different parts of the UI. */

            ALWAYS_INLINE int GetOutputStorage(void)
            {
                return this->output_storage;
            }

            ALWAYS_INLINE void SetOutputStorage(int value)
            {
                this->output_storage = value;
            }

            /* Wrappers for task methods. */

            ALWAYS_INLINE bool IsInternetConnectionAvailable(void)
            {
                return this->status_info_task->IsInternetConnectionAvailable();
            }

            ALWAYS_INLINE const nxdt::tasks::TitleApplicationMetadataVector& GetApplicationMetadata(bool is_system)
            {
                return this->title_task->GetApplicationMetadata(is_system);
            }

            ALWAYS_INLINE const nxdt::tasks::UmsDeviceVector& GetUmsDevices(void)
            {
                return this->ums_task->GetUmsDevices();
            }

            ALWAYS_INLINE const UsbHostSpeed& GetUsbHostSpeed(void)
            {
                return this->usb_host_task->GetUsbHostSpeed();
            }

            EVENT_SUBSCRIPTION(StatusInfoTask, StatusInfoEvent, status_info_task);
            EVENT_SUBSCRIPTION(GameCardTask, GameCardStatusEvent, gc_status_task);
            EVENT_SUBSCRIPTION(TitleTask, UserTitleEvent, title_task);
            EVENT_SUBSCRIPTION(UmsTask, UmsEvent, ums_task);
            EVENT_SUBSCRIPTION(UsbHostTask, UsbHostEvent, usb_host_task);
    };
}

#undef EVENT_SUBSCRIPTION

#endif  /* __ROOT_VIEW_HPP__ */
