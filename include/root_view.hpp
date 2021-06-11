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

#include <tasks.hpp>

namespace nxdt::views
{
    class RootView: public brls::TabFrame
    {
        private:
            nxdt::tasks::GameCardTask *gc_status_task = nullptr;
            nxdt::tasks::TitleTask *title_task = nullptr;
            nxdt::tasks::UmsTask *ums_task = nullptr;
            nxdt::tasks::UsbHostTask *usb_host_task = nullptr;
        
        public:
            RootView(void);
    };
}
