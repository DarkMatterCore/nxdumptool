/*
 * titles_tab.hpp
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

#ifndef __TITLES_TAB_HPP__
#define __TITLES_TAB_HPP__

#include "tasks.hpp"
#include "layered_error_frame.hpp"

namespace nxdt::views
{
    /* Expanded ListItem class to hold a title ID. */
    class TitlesTabItem: public brls::ListItem
    {
        private:
            u64 title_id = 0;
            bool is_system = false;
        
        public:
            TitlesTabItem(TitleApplicationMetadata *app_metadata, bool is_system);
    };
    
    class TitlesTab: public LayeredErrorFrame
    {
        private:
            nxdt::tasks::TitleTask *title_task = nullptr;
            nxdt::tasks::TitleEvent::Subscription title_task_sub;
            
            bool is_system = false;
            
            void PopulateList(const nxdt::tasks::TitleApplicationMetadataVector* app_metadata);
        
        public:
            TitlesTab(nxdt::tasks::TitleTask *title_task, bool is_system);
            ~TitlesTab(void);
    };
}

#endif  /* __TITLES_TAB_HPP__ */
