/*
 * titles_tab.hpp
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

#pragma once

#ifndef __TITLES_TAB_HPP__
#define __TITLES_TAB_HPP__

#include "root_view.hpp"
#include "layered_error_frame.hpp"

namespace nxdt::views
{
    /* Expanded TabFrame class used as a PopupFrame for titles. */
    class TitlesTabPopup: public brls::TabFrame
    {
        private:
            const TitleApplicationMetadata *app_metadata = nullptr;
            bool is_system = false;
            
            TitleUserApplicationData user_app_data = {0};
            TitleInfo *system_title_info = NULL;
        
        public:
            TitlesTabPopup(const TitleApplicationMetadata *app_metadata, bool is_system);
            ~TitlesTabPopup(void);
    };
    
    /* Expanded ListItem class to hold application metadata. */
    class TitlesTabItem: public brls::ListItem
    {
        private:
            const TitleApplicationMetadata *app_metadata = nullptr;
            bool is_system = false;
        
        public:
            TitlesTabItem(const TitleApplicationMetadata *app_metadata, bool is_system);
            
            ALWAYS_INLINE const TitleApplicationMetadata *GetApplicationMetadata(void)
            {
                return this->app_metadata;
            }
            
            ALWAYS_INLINE bool IsSystemTitle(void)
            {
                return this->is_system;
            }
    };
    
    class TitlesTab: public LayeredErrorFrame
    {
        private:
            RootView *root_view = nullptr;
            
            nxdt::tasks::TitleEvent::Subscription title_task_sub;
            bool is_system = false;
            
            void PopulateList(const nxdt::tasks::TitleApplicationMetadataVector* app_metadata);
        
        public:
            TitlesTab(RootView *root_view, bool is_system);
            ~TitlesTab(void);
    };
}

#endif  /* __TITLES_TAB_HPP__ */
