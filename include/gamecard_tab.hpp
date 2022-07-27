/*
 * gamecard_tab.hpp
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

#ifndef __GAMECARD_TAB_HPP__
#define __GAMECARD_TAB_HPP__

#include "root_view.hpp"
#include "layered_error_frame.hpp"
#include "focusable_item.hpp"

namespace nxdt::views
{
    class GameCardTab: public LayeredErrorFrame
    {
        typedef bool (*GameCardSizeFunc)(u64 *out_size);

        private:
            RootView *root_view = nullptr;

            nxdt::tasks::GameCardStatusEvent::Subscription gc_status_task_sub;
            GameCardStatus gc_status = GameCardStatus_NotInserted;

            void ProcessGameCardStatus(GameCardStatus gc_status);
            std::string GetFormattedSizeString(GameCardSizeFunc func);
            void PopulateList(void);

        public:
            GameCardTab(RootView *root_view);
            ~GameCardTab(void);
    };
}

#endif  /* __GAMECARD_TAB_HPP__ */
