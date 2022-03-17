/*
 * root_view.cpp
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

#include <nxdt_utils.h>
#include <root_view.hpp>
#include <gamecard_tab.hpp>
#include <titles_tab.hpp>
#include <options_tab.hpp>
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
        
        /* Check if we're running under applet mode. */
        this->applet_mode = utilsAppletModeCheck();
        
        /* Create labels. */
        this->applet_mode_lbl = new brls::Label(brls::LabelStyle::HINT, "root_view/applet_mode"_i18n);
        this->applet_mode_lbl->setColor(nvgRGB(255, 0, 0));
        this->applet_mode_lbl->setFontSize(brls::Application::getStyle()->AppletFrame.titleSize);
        this->applet_mode_lbl->setParent(this);
        
        this->time_lbl = new brls::Label(brls::LabelStyle::SMALL, "");
        this->time_lbl->setHorizontalAlign(NVG_ALIGN_RIGHT);
        this->time_lbl->setVerticalAlign(NVG_ALIGN_TOP);
        this->time_lbl->setParent(this);
        
        this->battery_icon = new brls::Label(brls::LabelStyle::SMALL, "");
        this->battery_icon->setFont(material);
        this->battery_icon->setHorizontalAlign(NVG_ALIGN_RIGHT);
        this->battery_icon->setVerticalAlign(NVG_ALIGN_TOP);
        this->battery_icon->setParent(this);
        
        this->battery_percentage = new brls::Label(brls::LabelStyle::SMALL, "");
        this->battery_percentage->setHorizontalAlign(NVG_ALIGN_RIGHT);
        this->battery_percentage->setVerticalAlign(NVG_ALIGN_TOP);
        this->battery_percentage->setParent(this);
        
        this->connection_icon = new brls::Label(brls::LabelStyle::SMALL, "");
        this->connection_icon->setFont(material);
        this->connection_icon->setHorizontalAlign(NVG_ALIGN_RIGHT);
        this->connection_icon->setVerticalAlign(NVG_ALIGN_TOP);
        this->connection_icon->setParent(this);
        
        this->connection_status_lbl = new brls::Label(brls::LabelStyle::SMALL, "");
        this->connection_status_lbl->setHorizontalAlign(NVG_ALIGN_RIGHT);
        this->connection_status_lbl->setVerticalAlign(NVG_ALIGN_TOP);
        this->connection_status_lbl->setParent(this);
        
        this->usb_icon = new brls::Label(brls::LabelStyle::SMALL, "\uE1E0");
        this->usb_icon->setFont(material);
        this->usb_icon->setHorizontalAlign(NVG_ALIGN_RIGHT);
        this->usb_icon->setVerticalAlign(NVG_ALIGN_TOP);
        this->usb_icon->setParent(this);
        
        this->usb_host_speed_lbl = new brls::Label(brls::LabelStyle::SMALL, "root_view/not_connected"_i18n);
        this->usb_host_speed_lbl->setHorizontalAlign(NVG_ALIGN_RIGHT);
        this->usb_host_speed_lbl->setVerticalAlign(NVG_ALIGN_TOP);
        this->usb_host_speed_lbl->setParent(this);
        
        /* Start background tasks. */
        this->status_info_task = new nxdt::tasks::StatusInfoTask();
        this->gc_status_task = new nxdt::tasks::GameCardTask();
        this->title_task = new nxdt::tasks::TitleTask();
        this->ums_task = new nxdt::tasks::UmsTask();
        this->usb_host_task = new nxdt::tasks::UsbHostTask();
        
        /* Add tabs. */
        GameCardTab *gamecard_tab = new GameCardTab(this);
        this->addTab("root_view/tabs/gamecard"_i18n, gamecard_tab);
        gamecard_tab->SetParentSidebarItem(static_cast<brls::SidebarItem*>(this->sidebar->getChild(this->sidebar->getViewsCount() - 1)));
        
        this->addSeparator();
        
        TitlesTab *user_titles_tab = new TitlesTab(this, false);
        this->addTab("root_view/tabs/user_titles"_i18n, user_titles_tab);
        user_titles_tab->SetParentSidebarItem(static_cast<brls::SidebarItem*>(this->sidebar->getChild(this->sidebar->getViewsCount() - 1)));
        
        TitlesTab *system_titles_tab = new TitlesTab(this, true);
        this->addTab("root_view/tabs/system_titles"_i18n, system_titles_tab);
        system_titles_tab->SetParentSidebarItem(static_cast<brls::SidebarItem*>(this->sidebar->getChild(this->sidebar->getViewsCount() - 1)));
        
        this->addSeparator();
        
        this->addTab("root_view/tabs/options"_i18n, new OptionsTab(this));
        
        this->addSeparator();
        
        this->addTab("root_view/tabs/about"_i18n, new AboutTab());
        
        /* Subscribe to status info event. */
        this->status_info_task_sub = this->status_info_task->RegisterListener([this](const nxdt::tasks::StatusInfoData *status_info_data) {
            u32 charge_percentage = status_info_data->charge_percentage;
            PsmChargerType charger_type = status_info_data->charger_type;
            
            NifmInternetConnectionType connection_type = status_info_data->connection_type;
            char *ip_addr = status_info_data->ip_addr;
            
            /* Update time label. */
            this->time_lbl->setText(this->GetFormattedDateString(status_info_data->timeinfo));
            
            /* Update battery labels. */
            this->battery_icon->setText(charger_type != PsmChargerType_Unconnected ? "\uE1A3" : (charge_percentage <= 15 ? "\uE19C" : "\uE1A4"));
            this->battery_icon->setColor(charger_type != PsmChargerType_Unconnected ? nvgRGB(0, 255, 0) : (charge_percentage <= 15 ? nvgRGB(255, 0, 0) : brls::Application::getTheme()->textColor));
            
            this->battery_percentage->setText(fmt::format("{}%", charge_percentage));
            
            /* Update network labels. */
            this->connection_icon->setText(!connection_type ? "\uE195" : (connection_type == NifmInternetConnectionType_WiFi ? "\uE63E" : "\uE8BE"));
            this->connection_status_lbl->setText(ip_addr ? std::string(ip_addr) : "root_view/not_connected"_i18n);
        });
        
        /* Subscribe to USB host event. */
        this->usb_host_task_sub = this->usb_host_task->RegisterListener([this](UsbHostSpeed usb_host_speed) {
            /* Update USB host speed label. */
            this->usb_host_speed_lbl->setText(usb_host_speed ? fmt::format("USB {}.0", usb_host_speed) : "root_view/not_connected"_i18n);
        });
    }
    
    RootView::~RootView(void)
    {
        /* Unregister USB host task listener. */
        this->usb_host_task->UnregisterListener(this->usb_host_task_sub);
        
        /* Unregister status info task listener. */
        this->status_info_task->UnregisterListener(this->status_info_task_sub);
        
        /* Stop background tasks. */
        this->status_info_task->stop();
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
        delete this->usb_icon;
        delete this->usb_host_speed_lbl;
    }
    
    std::string RootView::GetFormattedDateString(const struct tm& timeinfo)
    {
        bool is_am = true;
        struct tm ts = timeinfo;
        
        /* Update time label. */
        ts.tm_mon++;
        ts.tm_year += 1900;
        
        if ("generic/time_format"_i18n.compare("12") == 0)
        {
            /* Adjust time for 12-hour clock. */
            if (ts.tm_hour > 12)
            {
                ts.tm_hour -= 12;
                is_am = false;
            } else
            if (!ts.tm_hour)
            {
                ts.tm_hour = 12;
            }
        }
        
        return i18n::getStr("generic/date"_i18n, ts.tm_year, ts.tm_mon, ts.tm_mday, ts.tm_hour, ts.tm_min, ts.tm_sec, is_am ? "AM" : "PM");
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
        
        this->usb_icon->frame(ctx);
        this->usb_host_speed_lbl->frame(ctx);
    }
    
    void RootView::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        int x_pos = 0, y_pos = 0;
        
        brls::AppletFrame::layout(vg, style, stash);
        
        if (this->applet_mode)
        {
            /* Applet mode label. */
            x_pos = (this->x + (this->width - this->applet_mode_lbl->getTextWidth()) / 2);
            y_pos = (this->y + (style->AppletFrame.headerHeightRegular / 2) + style->AppletFrame.titleOffset);
            
            this->applet_mode_lbl->setBoundaries(x_pos, y_pos, 0, 0);
            this->applet_mode_lbl->invalidate();
        }
        
        /* Time label. */
        x_pos = (this->x + this->width - style->AppletFrame.separatorSpacing - style->AppletFrame.footerTextSpacing);
        y_pos = this->y + style->AppletFrame.imageTopPadding;
        
        this->time_lbl->setBoundaries(x_pos, y_pos, 0, 0);
        this->time_lbl->invalidate();
        
        /* Battery stats and network connection labels. */
        y_pos += (this->time_lbl->getTextHeight() + 5);
        
        this->connection_status_lbl->setBoundaries(x_pos, y_pos, 0, 0);
        this->connection_status_lbl->invalidate();
        
        x_pos -= (5 + this->connection_status_lbl->getTextWidth());
        
        this->connection_icon->setBoundaries(x_pos, y_pos, 0, 0);
        this->connection_icon->invalidate();
        
        x_pos -= (10 + this->connection_icon->getTextWidth());
        
        this->battery_percentage->setBoundaries(x_pos, y_pos, 0, 0);
        this->battery_percentage->invalidate();
        
        x_pos -= (5 + this->battery_percentage->getTextWidth());
        
        this->battery_icon->setBoundaries(x_pos, y_pos, 0, 0);
        this->battery_icon->invalidate();
        
        /* USB host speed labels. */
        x_pos = (this->x + this->width - style->AppletFrame.separatorSpacing - style->AppletFrame.footerTextSpacing);
        y_pos += (this->connection_status_lbl->getTextHeight() + 5);
        
        this->usb_host_speed_lbl->setBoundaries(x_pos, y_pos, 0, 0);
        this->usb_host_speed_lbl->invalidate();
        
        x_pos -= (5 + this->usb_host_speed_lbl->getTextWidth());
        
        this->usb_icon->setBoundaries(x_pos, y_pos, 0, 0);
        this->usb_icon->invalidate();
    }
    
    brls::View* RootView::getDefaultFocus(void)
    {
        return this->sidebar->getChild(0);
    }
}
