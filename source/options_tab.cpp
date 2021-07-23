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
        
        /* Overclock. */
        brls::ToggleListItem *overclock = new brls::ToggleListItem("options_tab/overclock/label"_i18n, configGetBoolean("overclock"), \
                                                                   "options_tab/overclock/description"_i18n, "options_tab/overclock/value_enabled"_i18n, \
                                                                   "options_tab/overclock/value_disabled"_i18n);
        overclock->getClickEvent()->subscribe([](brls::View* view) {
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            bool value = item->getToggleState();
            utilsOverclockSystem(value);
            configSetBoolean("overclock", value);
        });
        this->addView(overclock);
        
        /* Naming convention. */
        brls::SelectListItem *naming_convention = new brls::SelectListItem("options_tab/naming_convention/label"_i18n, {
                                                                               "options_tab/naming_convention/value_00"_i18n,
                                                                               "options_tab/naming_convention/value_01"_i18n
                                                                           }, static_cast<unsigned>(configGetInteger("naming_convention")),
                                                                           "options_tab/naming_convention/description"_i18n);
        naming_convention->getValueSelectedEvent()->subscribe([](int selected){
            if (selected < 0 || selected > static_cast<int>(TitleNamingConvention_Count)) return;
            configSetInteger("naming_convention", selected);
        });
        this->addView(naming_convention);
        
        /* Update application. */
        if (!envIsNso())
        {
            brls::ListItem *update_app = new brls::ListItem("options_tab/update_app/label"_i18n, "options_tab/update_app/description"_i18n);
            this->addView(update_app);
        }
    }
}
