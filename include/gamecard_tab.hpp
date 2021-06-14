/*
 * gamecard_tab.hpp
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

#ifndef __GAMECARD_TAB_HPP__
#define __GAMECARD_TAB_HPP__

#include "tasks.hpp"
#include "error_frame.hpp"

namespace nxdt::views
{
    /* Extended class to display a focusable (but unhighlightable) table. */
    class GameCardTable: public brls::Table
    {
        protected:
            brls::View* getDefaultFocus(void) override;
            void onFocusGained(void) override;
        
        public:
            GameCardTable(void);
    };
    
    /* Extended class to switch between ErrorFrame and List views whenever the gamecard status event is triggered. */
    class GameCardTab: public brls::LayerView
    {
        private:
            nxdt::tasks::GameCardTask *gc_status_task = nullptr;
            nxdt::tasks::GameCardStatusEvent::Subscription gc_status_task_sub;
            GameCardStatus gc_status = GameCardStatus_NotInserted;
            
            ErrorFrame *error_frame = nullptr;
            
            brls::List *list = nullptr;
            
            GameCardTable *properties_table = nullptr;
            brls::TableRow *capacity = nullptr;
            brls::TableRow *total_size = nullptr;
            brls::TableRow *trimmed_size = nullptr;
            brls::TableRow *update_version = nullptr;
            brls::TableRow *lafw_version = nullptr;
            brls::TableRow *sdk_version = nullptr;
            brls::TableRow *compatibility_type = nullptr;
            
            brls::ListItem *dump_card_image = nullptr;
            brls::ListItem *dump_certificate = nullptr;
            brls::ListItem *dump_header = nullptr;
            brls::ListItem *dump_decrypted_cardinfo = nullptr;
            brls::ListItem *dump_initial_data = nullptr;
            brls::ListItem *dump_hfs_partitions = nullptr;
            
            std::vector<brls::View*> views;
            int view_index = -1;
            
            void addLayerWrapper(brls::View* view);
            void changeLayerWrapper(brls::View* view);
        
        public:
            GameCardTab(nxdt::tasks::GameCardTask *gc_status_task);
            ~GameCardTab(void);
    };
}

#endif  /* __GAMECARD_TAB_HPP__ */
