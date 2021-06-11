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
    
    /* Custom vector type used to hold pointers to application metadata entries. */
    typedef std::vector<TitleApplicationMetadata*> TitleApplicationMetadataVector;
    
    /* Custom vector type used to hold UMS devices. */
    typedef std::vector<UsbHsFsDevice> UmsDeviceVector;
    
    /* Gamecard task. */
    class GameCardTask: public brls::RepeatingTask
    {
        private:
            GameCardStatusEvent gc_status_event;
            
            GameCardStatus cur_gc_status = GameCardStatus_NotInserted;
            GameCardStatus prev_gc_status = GameCardStatus_NotInserted;
        public:
            GameCardTask(void);
            ~GameCardTask(void);
            
            void run(retro_time_t current_time) override;
            
            GameCardStatusEvent* GetTaskEvent(void);
    };
    
    /* Title task. */
    class TitleTask: public brls::RepeatingTask
    {
        private:
            VoidEvent title_event;
            
            TitleApplicationMetadataVector system_metadata;
            TitleApplicationMetadataVector user_metadata;
            
            void PopulateApplicationMetadataVector(bool is_system);
        public:
            TitleTask(void);
            ~TitleTask(void);
            
            void run(retro_time_t current_time) override;
            
            VoidEvent* GetTaskEvent(void);
            
            TitleApplicationMetadataVector* GetApplicationMetadata(bool is_system);
    };
    
    /* USB Mass Storage task. */
    class UmsTask: public brls::RepeatingTask
    {
        private:
            VoidEvent ums_event;
            
            UmsDeviceVector ums_devices;
            
            void PopulateUmsDeviceVector(void);
        public:
            UmsTask(void);
            ~UmsTask(void);
            
            void run(retro_time_t current_time) override;
            
            VoidEvent* GetTaskEvent(void);
            
            UmsDeviceVector* GetUmsDevices(void);
    };
    
    /* USB host device connection task. */
    class UsbHostTask: public brls::RepeatingTask
    {
        private:
            BooleanEvent usb_host_event;
            
            bool cur_usb_host_status = false;
            bool prev_usb_host_status = false;
        public:
            UsbHostTask(void);
            ~UsbHostTask(void);
            
            void run(retro_time_t current_time) override;
            
            BooleanEvent* GetTaskEvent(void);
    };
}

/* Declared in main.cpp. */
extern nxdt::tasks::GameCardTask *g_gamecardTask;
extern nxdt::tasks::TitleTask *g_titleTask;
extern nxdt::tasks::UmsTask *g_umsTask;
extern nxdt::tasks::UsbHostTask *g_usbHostTask;

#endif  /* __TASKS_HPP__ */
