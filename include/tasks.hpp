/*
 * tasks.hpp
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

#ifndef __TASKS_HPP__
#define __TASKS_HPP__

#include <borealis.hpp>

#include "core/gamecard.h"
#include "core/title.h"
#include "core/ums.h"
#include "core/usb.h"

namespace nxdt::tasks
{
    /* Custom event types used by the tasks defined below. */
    typedef brls::Event<GameCardStatus> GameCardStatusEvent;
    typedef brls::VoidEvent VoidEvent;
    typedef brls::Event<bool> BooleanEvent;
    
    /* Gamecard task. */
    class GameCardTask: public brls::RepeatingTask
    {
        private:
            GameCardStatus cur_gc_status = GameCardStatus_NotInserted;
            GameCardStatus prev_gc_status = GameCardStatus_NotInserted;
            GameCardStatusEvent *gc_status_event = nullptr;
        public:
            GameCardTask(GameCardStatusEvent *gc_status_event);
            void run(retro_time_t current_time) override;
    };
    
    /* Gamecard title task. */
    class GameCardTitleTask: public brls::RepeatingTask
    {
        private:
            VoidEvent *gc_title_event = nullptr;
        public:
            GameCardTitleTask(VoidEvent *gc_title_event);
            void run(retro_time_t current_time) override;
    };
    
    /* USB Mass Storage task. */
    class UmsTask: public brls::RepeatingTask
    {
        private:
            VoidEvent *ums_event = nullptr;
        public:
            UmsTask(VoidEvent *ums_event);
            void run(retro_time_t current_time) override;
    };
    
    /* USB host device connection task. */
    class UsbHostTask: public brls::RepeatingTask
    {
        private:
            bool cur_usb_host_status = false;
            bool prev_usb_host_status = false;
            BooleanEvent *usb_host_event = nullptr;
        public:
            UsbHostTask(BooleanEvent *usb_host_event);
            void run(retro_time_t current_time) override;
    };
}

#endif  /* __TASKS_HPP__ */
