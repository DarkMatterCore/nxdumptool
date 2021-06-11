/*
 * root_view.cpp
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

#include <nxdt_includes.h>
#include <root_view.hpp>
//#include <gamecard_tab.hpp>
//#include <user_titles_tab.hpp>
//#include <system_titles_tab.hpp>
//#include <options_tab.hpp>
//#include <about_tab.hpp>

using namespace brls::i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    RootView::RootView(void) : brls::TabFrame()
    {
        /* Start background tasks. */
        this->gc_status_task = new nxdt::tasks::GameCardTask();
        this->title_task = new nxdt::tasks::TitleTask();
        this->ums_task = new nxdt::tasks::UmsTask();
        this->usb_host_task = new nxdt::tasks::UsbHostTask();
        
        /* Set UI properties. */
        this->setTitle(APP_TITLE);
        this->setIcon(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
        this->setFooterText("v" APP_VERSION);
        
        /* Add tabs. */
        this->addTab("root_view/tabs/gamecard"_i18n, new brls::Rectangle(nvgRGB(255, 0, 0)));
        this->addTab("root_view/tabs/user_titles"_i18n, new brls::Rectangle(nvgRGB(0, 255, 0)));
        this->addTab("root_view/tabs/system_titles"_i18n, new brls::Rectangle(nvgRGB(0, 0, 255)));
        this->addTab("root_view/tabs/options"_i18n, new brls::Rectangle(nvgRGB(255, 255, 0)));
        this->addTab("root_view/tabs/about"_i18n, new brls::Rectangle(nvgRGB(255, 0, 255)));
    }
}
