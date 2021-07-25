/*
 * options_tab.cpp
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
#include <options_tab.hpp>
#include <title.h>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::views
{
    OptionsTab::OptionsTab(void) : brls::List()
    {
        /* Set custom spacing. */
        this->setSpacing(this->getSpacing() / 2);
        this->setMarginBottom(20);
        
        /* Information about actual dump options. */
        brls::Label *dump_options_info = new brls::Label(brls::LabelStyle::DESCRIPTION, "options_tab/dump_options_info"_i18n, true);
        dump_options_info->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addView(dump_options_info);
        
        /* Overclock. */
        brls::ToggleListItem *overclock = new brls::ToggleListItem("options_tab/overclock/label"_i18n, configGetBoolean("overclock"), \
                                                                   "options_tab/overclock/description"_i18n, "options_tab/overclock/value_enabled"_i18n, \
                                                                   "options_tab/overclock/value_disabled"_i18n);
        overclock->getClickEvent()->subscribe([](brls::View* view) {
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            
            /* Get current value. */
            bool value = item->getToggleState();
            
            /* Change hardware clocks based on the current value. */
            utilsOverclockSystem(value);
            
            /* Update configuration. */
            configSetBoolean("overclock", value);
            
            brls::Logger::debug("Overclock setting changed by user.");
        });
        this->addView(overclock);
        
        /* Naming convention. */
        brls::SelectListItem *naming_convention = new brls::SelectListItem("options_tab/naming_convention/label"_i18n, {
                                                                               "options_tab/naming_convention/value_00"_i18n,
                                                                               "options_tab/naming_convention/value_01"_i18n
                                                                           }, static_cast<unsigned>(configGetInteger("naming_convention")),
                                                                           "options_tab/naming_convention/description"_i18n);
        naming_convention->getValueSelectedEvent()->subscribe([](int selected){
            /* Make sure the current value isn't out of bounds. */
            if (selected < 0 || selected > static_cast<int>(TitleNamingConvention_Count)) return;
            
            /* Update configuration. */
            configSetInteger("naming_convention", selected);
            
            brls::Logger::debug("Naming convention setting changed by user.");
        });
        this->addView(naming_convention);
        
        /* Update NSWDB XML. */
        brls::ListItem *update_nswdb_xml = new brls::ListItem("options_tab/update_nswdb_xml/label"_i18n, "options_tab/update_nswdb_xml/description"_i18n);
        update_nswdb_xml->getClickEvent()->subscribe([this](brls::View* view) {
            this->DisplayNotification("Not implemented.");
        });
        this->addView(update_nswdb_xml);
        
        /* Update application. */
        brls::ListItem *update_app = new brls::ListItem("options_tab/update_app/label"_i18n, "options_tab/update_app/description"_i18n);
        update_app->getClickEvent()->subscribe([this](brls::View* view) {
            if (envIsNso())
            {
                /* Display a notification if we're running as a NSO. */
                this->DisplayNotification("options_tab/notifications/is_nso"_i18n);
                return;
            } else
            if (false)
            {
                /* Display a notification if the application has already been updated. */
                this->DisplayNotification("options_tab/notifications/already_updated"_i18n);
                return;
            }
            
            /*brls::StagedAppletFrame *staged_frame = new brls::StagedAppletFrame();
            staged_frame->setTitle("options_tab/update_app/label"_i18n);
            
            brls::Application::pushView(staged_frame);*/
        });
        this->addView(update_app);
    }
    
    OptionsTab::~OptionsTab(void)
    {
        brls::menu_timer_kill(&(this->notification_timer));
    }
    
    void OptionsTab::DisplayNotification(std::string str)
    {
        if (str == "" || !this->display_notification) return;
        
        brls::Application::notify(str);
        this->display_notification = false;
        
        this->notification_timer_ctx.duration = brls::Application::getStyle()->AnimationDuration.notificationTimeout;
        this->notification_timer_ctx.cb = [this](void *userdata) { this->display_notification = true; };
        this->notification_timer_ctx.tick = [](void*){};
        this->notification_timer_ctx.userdata = nullptr;
        
        brls::menu_timer_start(&(this->notification_timer), &(this->notification_timer_ctx));
    }
}
