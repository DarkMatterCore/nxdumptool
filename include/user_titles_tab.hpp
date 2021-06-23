/*
 * user_titles_tab.hpp
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

#ifndef __USER_TITLES_TAB_HPP__
#define __USER_TITLES_TAB_HPP__

#include "tasks.hpp"
#include "layered_error_frame.hpp"

namespace nxdt::views
{
    class UserTitlesTab: public LayeredErrorFrame
    {
        private:
            nxdt::tasks::TitleTask *title_task = nullptr;
            nxdt::tasks::TitleEvent::Subscription title_task_sub;
            std::unordered_map<brls::ListItem*, TitleApplicationMetadata*> list_item_metadata;
            
            void PopulateList(const nxdt::tasks::TitleApplicationMetadataVector* user_app_metadata);
        
        public:
            UserTitlesTab(nxdt::tasks::TitleTask *title_task);
            ~UserTitlesTab(void);
    };
}

#endif  /* __USER_TITLES_TAB_HPP__ */
