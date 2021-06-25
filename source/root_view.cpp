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
#include <gamecard_tab.hpp>
#include <titles_tab.hpp>
//#include <options_tab.hpp>
#include <about_tab.hpp>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    RootView::RootView(void) : brls::TabFrame()
    {
        int material = brls::Application::getFontStash()->material;
        
        /* Set UI properties. */
        this->setTitle(APP_TITLE);
        this->setIcon(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
        this->setFooterText("v" APP_VERSION " (" GIT_REV ")");
        
        /* Check if we're running under applet mode. */
        this->applet_mode = utilsAppletModeCheck();
        
        /* Create labels. */
        this->applet_mode_lbl = new brls::Label(brls::LabelStyle::HINT, "root_view/applet_mode"_i18n);
        this->applet_mode_lbl->setColor(nvgRGB(255, 0, 0));
        this->applet_mode_lbl->setFontSize(brls::Application::getStyle()->AppletFrame.titleSize);
        this->applet_mode_lbl->setParent(this);
        
        this->time_lbl = new brls::Label(brls::LabelStyle::SMALL, "");
        this->time_lbl->setParent(this);
        
        this->battery_icon = new brls::Label(brls::LabelStyle::SMALL, "");
        this->battery_icon->setFont(material);
        this->battery_icon->setParent(this);
        
        this->battery_percentage = new brls::Label(brls::LabelStyle::SMALL, "");
        this->battery_percentage->setParent(this);
        
        this->connection_icon = new brls::Label(brls::LabelStyle::SMALL, "");
        this->connection_icon->setFont(material);
        this->connection_icon->setParent(this);
        
        this->connection_status_lbl = new brls::Label(brls::LabelStyle::SMALL, "");
        this->connection_status_lbl->setParent(this);
        
        /* Start background tasks. */
        this->status_info_task = new nxdt::tasks::StatusInfoTask();
        this->gc_status_task = new nxdt::tasks::GameCardTask();
        this->title_task = new nxdt::tasks::TitleTask();
        this->ums_task = new nxdt::tasks::UmsTask();
        this->usb_host_task = new nxdt::tasks::UsbHostTask();
        
        /* Add tabs. */
        GameCardTab *gamecard_tab = new GameCardTab(this->gc_status_task);
        this->addTab("root_view/tabs/gamecard"_i18n, gamecard_tab);
        gamecard_tab->SetParentSidebarItem(static_cast<brls::SidebarItem*>(this->sidebar->getChild(this->sidebar->getViewsCount() - 1)));
        
        this->addSeparator();
        
        TitlesTab *user_titles_tab = new TitlesTab(this->title_task, false);
        this->addTab("root_view/tabs/user_titles"_i18n, user_titles_tab);
        user_titles_tab->SetParentSidebarItem(static_cast<brls::SidebarItem*>(this->sidebar->getChild(this->sidebar->getViewsCount() - 1)));
        
        TitlesTab *system_titles_tab = new TitlesTab(this->title_task, true);
        this->addTab("root_view/tabs/system_titles"_i18n, system_titles_tab);
        system_titles_tab->SetParentSidebarItem(static_cast<brls::SidebarItem*>(this->sidebar->getChild(this->sidebar->getViewsCount() - 1)));
        
        this->addSeparator();
        
        this->addTab("root_view/tabs/options"_i18n, new brls::Rectangle(nvgRGB(255, 255, 0)));
        
        this->addSeparator();
        
        this->addTab("root_view/tabs/about"_i18n, new AboutTab());
        
        /* Subscribe to status info event. */
        this->status_info_task_sub = this->status_info_task->RegisterListener([this](const nxdt::tasks::StatusInfoData *status_info_data) {
            bool is_am = true;
            struct tm *timeinfo = status_info_data->timeinfo;
            
            u32 charge_percentage = status_info_data->charge_percentage;
            PsmChargerType charger_type = status_info_data->charger_type;
            
            NifmInternetConnectionType connection_type = status_info_data->connection_type;
            char *ip_addr = status_info_data->ip_addr;
            
            /* Update time label. */
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
            
            this->time_lbl->setText(i18n::getStr("root_view/date"_i18n, timeinfo->tm_year, timeinfo->tm_mon, timeinfo->tm_mday, timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec, \
                                                 is_am ? "AM" : "PM"));
            
            /* Update battery labels. */
            this->battery_icon->setText(charger_type != PsmChargerType_Unconnected ? "\uE1A3" : (charge_percentage <= 15 ? "\uE19C" : "\uE1A4"));
            this->battery_icon->setColor(charger_type != PsmChargerType_Unconnected ? nvgRGB(0, 255, 0) : (charge_percentage <= 15 ? nvgRGB(255, 0, 0) : brls::Application::getTheme()->textColor));
            
            this->battery_percentage->setText(fmt::format("{}%", charge_percentage));
            
            /* Update network labels. */
            this->connection_icon->setText(!connection_type ? "\uE195" : (connection_type == NifmInternetConnectionType_WiFi ? "\uE63E" : "\uE8BE"));
            this->connection_status_lbl->setText(ip_addr ? std::string(ip_addr) : "root_view/not_connected"_i18n);
            
            /* Update layout. */
            this->invalidate(true);
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
    
    void RootView::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
    {
        brls::AppletFrame::draw(vg, x, y, width, height, style, ctx);
        
        if (this->applet_mode) this->applet_mode_lbl->frame(ctx);
        
        this->time_lbl->frame(ctx);
        
        this->battery_icon->frame(ctx);
        this->battery_percentage->frame(ctx);
        
        this->connection_icon->frame(ctx);
        this->connection_status_lbl->frame(ctx);
    }
    
    void RootView::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        int y_pos = 0;
        
        brls::AppletFrame::layout(vg, style, stash);
        
        if (this->applet_mode)
        {
            /* Applet mode label. */
            this->applet_mode_lbl->invalidate(true);
            this->applet_mode_lbl->setBoundaries(
                this->x + (this->width - this->applet_mode_lbl->getWidth()) / 2,
                this->y + (style->AppletFrame.headerHeightRegular / 2) + style->AppletFrame.titleOffset,
                this->applet_mode_lbl->getWidth(),
                this->applet_mode_lbl->getHeight());
        }
        
        /* Time label. */
        this->time_lbl->invalidate(true);
        y_pos += this->y + 25 + this->time_lbl->getHeight();
        
        this->time_lbl->setBoundaries(
            this->x + this->width - (style->AppletFrame.separatorSpacing * 2) - this->time_lbl->getWidth(),
            y_pos,
            this->time_lbl->getWidth(),
            this->time_lbl->getHeight());
        
        /* Battery stats labels. */
        this->battery_icon->invalidate(true);
        this->battery_percentage->invalidate(true);
        y_pos += (20 + this->battery_icon->getHeight());
        
        this->battery_icon->setBoundaries(
            this->x + this->width - (style->AppletFrame.separatorSpacing * 2) - this->battery_percentage->getWidth() - 5 - this->battery_icon->getWidth(),
            y_pos,
            this->battery_icon->getWidth(),
            this->battery_icon->getHeight());
        
        this->battery_percentage->setBoundaries(
            this->x + this->width - (style->AppletFrame.separatorSpacing * 2) - this->battery_percentage->getWidth(),
            y_pos,
            this->battery_percentage->getWidth(),
            this->battery_percentage->getHeight());
        
        /* Network connection labels. */
        this->connection_icon->invalidate(true);
        this->connection_status_lbl->invalidate(true);
        y_pos += (20 + this->connection_icon->getHeight());
        
        this->connection_icon->setBoundaries(
            this->x + this->width - (style->AppletFrame.separatorSpacing * 2) - this->connection_status_lbl->getWidth() - 5 - this->connection_icon->getWidth(),
            y_pos,
            this->connection_icon->getWidth(),
            this->connection_icon->getHeight());
        
        this->connection_status_lbl->setBoundaries(
            this->x + this->width - (style->AppletFrame.separatorSpacing * 2) - this->connection_status_lbl->getWidth(),
            y_pos,
            this->connection_status_lbl->getWidth(),
            this->connection_status_lbl->getHeight());
    }
}
