/*
 * root_view.hpp
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

#pragma once

#ifndef __ROOT_VIEW_HPP__
#define __ROOT_VIEW_HPP__

#include "tasks.hpp"

namespace nxdt::views
{
    class RootView: public brls::TabFrame
    {
        private:
            bool applet_mode = false;
            
            brls::Label *applet_mode_lbl = nullptr;
            brls::Label *time_lbl = nullptr;
            brls::Label *battery_icon = nullptr, *battery_percentage = nullptr;
            brls::Label *connection_icon = nullptr, *connection_status_lbl = nullptr;
            
            nxdt::tasks::StatusInfoTask *status_info_task = nullptr;
            nxdt::tasks::GameCardTask *gc_status_task = nullptr;
            nxdt::tasks::TitleTask *title_task = nullptr;
            nxdt::tasks::UmsTask *ums_task = nullptr;
            nxdt::tasks::UsbHostTask *usb_host_task = nullptr;
            
            nxdt::tasks::StatusInfoEvent::Subscription status_info_task_sub;
        
        protected:
            void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
            void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
        
        public:
            RootView(void);
            ~RootView(void);
    };
}

#endif  /* __ROOT_VIEW_HPP__ */
