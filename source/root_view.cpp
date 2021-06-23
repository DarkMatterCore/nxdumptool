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

#include <nxdt_utils.h>
#include <root_view.hpp>
//#include <gamecard_tab.hpp>
//#include <user_titles_tab.hpp>
//#include <system_titles_tab.hpp>
//#include <options_tab.hpp>
//#include <about_tab.hpp>

using namespace brls::literals; /* For _i18n. */

namespace nxdt::views
{
    RootView::RootView(void) : brls::TabFrame()
    {
        /* Set UI properties. */
        this->setTitle(APP_TITLE);
        this->setIconFromRes("img/" APP_TITLE ".jpg");
        
        /* Check if we're running under applet mode. */
        this->applet_mode = utilsAppletModeCheck();
        
        /* Create labels. */
        this->applet_mode_lbl = new brls::Label();
        this->applet_mode_lbl->setText("root_view/applet_mode"_i18n);
        this->applet_mode_lbl->setFontSize(22.0f);
        this->applet_mode_lbl->setTextColor(nvgRGB(255, 0, 0));
        this->applet_mode_lbl->setSingleLine(true);
        this->applet_mode_lbl->setParent(this);
        
        this->time_lbl = new brls::Label();
        this->time_lbl->setFontSize(16.0f);
        this->time_lbl->setSingleLine(true);
        this->time_lbl->setParent(this);
        
        this->battery_icon = new brls::Label();
        this->battery_icon->setFontSize(16.0f);
        this->battery_icon->setSingleLine(true);
        this->battery_icon->setParent(this);
        
        this->battery_percentage = new brls::Label();
        this->battery_percentage->setFontSize(16.0f);
        this->battery_percentage->setSingleLine(true);
        this->battery_percentage->setParent(this);
        
        this->connection_icon = new brls::Label();
        this->connection_icon->setFontSize(16.0f);
        this->connection_icon->setSingleLine(true);
        this->connection_icon->setParent(this);
        
        this->connection_status_lbl = new brls::Label();
        this->connection_status_lbl->setFontSize(16.0f);
        this->connection_status_lbl->setSingleLine(true);
        this->connection_status_lbl->setParent(this);
        
        /* Add tabs. */
        this->addTab("root_view/tabs/gamecard"_i18n, [this](void){ return new brls::Rectangle(nvgRGB(255, 0, 0));/*return new GameCardTab(this->gc_status_task);*/ });
        this->addSeparator();
        this->addTab("root_view/tabs/user_titles"_i18n, [this](void){ return new brls::Rectangle(nvgRGB(0, 255, 0));/*return new UserTitlesTab(this->title_task);*/ });
        this->addTab("root_view/tabs/system_titles"_i18n, [this](void){ return new brls::Rectangle(nvgRGB(0, 0, 255)); });
        this->addSeparator();
        this->addTab("root_view/tabs/options"_i18n, [this](void){ return new brls::Rectangle(nvgRGB(255, 255, 0)); });
        this->addSeparator();
        this->addTab("root_view/tabs/about"_i18n, [this](void){ return new brls::Rectangle(nvgRGB(0, 255, 255));/*return new AboutTab();*/ });
        
        /* Start background tasks. */
        this->status_info_task = new nxdt::tasks::StatusInfoTask();
        this->gc_status_task = new nxdt::tasks::GameCardTask();
        this->title_task = new nxdt::tasks::TitleTask();
        this->ums_task = new nxdt::tasks::UmsTask();
        this->usb_host_task = new nxdt::tasks::UsbHostTask();
        
        /* Subscribe to status info event. */
        this->status_info_task_sub = this->status_info_task->RegisterListener([this](const nxdt::tasks::StatusInfoData *status_info_data) {
            /* Update time label. */
            bool is_am = true;
            struct tm *timeinfo = status_info_data->timeinfo;
            
            timeinfo->tm_mon++;
            timeinfo->tm_year += 1900;
            
            if ("root_view/time_format"_i18n.compare("12") == 0)
            {
                /* Adjust time for 12-hour clock. */
                if (timeinfo->tm_hour > 12)
                {
                    timeinfo->tm_hour -= 12;
                    is_am = false;
                } else
                if (!timeinfo->tm_hour)
                {
                    timeinfo->tm_hour = 12;
                }
            }
            
            this->time_lbl->setText(brls::getStr("root_view/date"_i18n, timeinfo->tm_year, timeinfo->tm_mon, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, \
                                                 is_am ? "AM" : "PM"));
            
            /* Update battery labels. */
            u32 charge_percentage = status_info_data->charge_percentage;
            PsmChargerType charger_type = status_info_data->charger_type;
            
            this->battery_icon->setText(charger_type != PsmChargerType_Unconnected ? "\uE1A3" : (charge_percentage <= 15 ? "\uE19C" : "\uE1A4"));
            this->battery_icon->setTextColor(charger_type != PsmChargerType_Unconnected ? nvgRGB(0, 255, 0) : (charge_percentage <= 15 ? nvgRGB(255, 0, 0) : brls::Application::getTheme()["brls/text"]));
            
            this->battery_percentage->setText(fmt::format("{}%", charge_percentage));
            
            /* Update network label. */
            NifmInternetConnectionType connection_type = status_info_data->connection_type;
            char *ip_addr = status_info_data->ip_addr;
            
            this->connection_icon->setText(!connection_type ? "\uE195" : (connection_type == NifmInternetConnectionType_WiFi ? "\uE63E" : "\uE8BE"));
            this->connection_status_lbl->setText(ip_addr ? std::string(ip_addr) : "root_view/not_connected"_i18n);
            
            /* Update layout. */
            this->invalidate();
        });
    }
    
    RootView::~RootView(void)
    {
        /* Unregister status info task listener. */
        this->status_info_task->UnregisterListener(this->status_info_task_sub);
        
        /* Stop background tasks. */
        this->gc_status_task->stop();
        this->title_task->stop();
        this->ums_task->stop();
        this->usb_host_task->stop();
        
        /* Destroy labels. */
        delete this->applet_mode_lbl;
        delete this->time_lbl;
        delete this->battery_icon;
        delete this->battery_percentage;
        delete this->connection_icon;
        delete this->connection_status_lbl;
    }
    
    void RootView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx)
    {
        brls::AppletFrame::draw(vg, x, y, width, height, style, ctx);
        
        if (this->applet_mode) this->applet_mode_lbl->frame(ctx);
        
        this->time_lbl->frame(ctx);
        
        this->battery_icon->frame(ctx);
        this->battery_percentage->frame(ctx);
        
        this->connection_icon->frame(ctx);
        this->connection_status_lbl->frame(ctx);
    }
    
    void RootView::onLayout(void)
    {
        brls::AppletFrame::onLayout();
        
        float y_pos = 0;
        brls::Style style = brls::Application::getStyle();
        
        if (this->applet_mode)
        {
            /* Applet mode label. */
            this->applet_mode_lbl->invalidate();
            this->applet_mode_lbl->setPositionLeft((this->getWidth() / 4.0f) - (this->applet_mode_lbl->getWidth() / 2.0f));
            this->applet_mode_lbl->setPositionBottom(-(style["brls/applet_frame/footer_height"] / 2.0f));
        }
        
        /* Time label. */
        this->time_lbl->invalidate();
        y_pos += (this->getY() + 25.0f + this->time_lbl->getLineHeight());
        
        this->time_lbl->setPositionTop(y_pos);
        this->time_lbl->setPositionRight(-style["brls/applet_frame/header_padding_sides"]);
        
        /* Battery stats labels. */
        this->battery_icon->invalidate();
        this->battery_percentage->invalidate();
        y_pos += (20.0f + this->battery_icon->getLineHeight());
        
        this->battery_percentage->setPositionTop(y_pos);
        this->battery_percentage->setPositionRight(-style["brls/applet_frame/header_padding_sides"]);
        
        this->battery_icon->setPositionTop(y_pos);
        this->battery_icon->setPositionRight(-(style["brls/applet_frame/header_padding_sides"] + this->battery_percentage->getWidth() + 5.0f));
        
        /* Network connection labels. */
        this->connection_icon->invalidate();
        this->connection_status_lbl->invalidate();
        y_pos += (20.0f + this->connection_icon->getLineHeight());
        
        this->connection_status_lbl->setPositionTop(y_pos);
        this->connection_status_lbl->setPositionRight(-style["brls/applet_frame/header_padding_sides"]);
        
        this->connection_icon->setPositionTop(y_pos);
        this->connection_icon->setPositionRight(-(style["brls/applet_frame/header_padding_sides"] + this->connection_status_lbl->getWidth() + 5.0f));
    }
}
