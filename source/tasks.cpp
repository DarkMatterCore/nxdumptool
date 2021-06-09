/*
 * tasks.cpp
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

#include <nxdt_includes.h>
#include <tasks.hpp>

#define TASK_INTERVAL   100 /* 100 ms. */

namespace nxdt::tasks
{
    /* Gamecard task. */
    
    GameCardTask::GameCardTask(GameCardStatusEvent *gc_status_event) : brls::RepeatingTask(TASK_INTERVAL)
    {
        this->gc_status_event = gc_status_event;
    }
    
    void GameCardTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        this->cur_gc_status = (GameCardStatus)gamecardGetStatus();
        if (this->cur_gc_status != this->prev_gc_status)
        {
            this->gc_status_event->fire(this->cur_gc_status);
            this->prev_gc_status = this->cur_gc_status;
            brls::Logger::debug("Gamecard status change triggered: {}.", this->cur_gc_status);
        }
    }
    
    /* Gamecard title task. */
    
    GameCardTitleTask::GameCardTitleTask(VoidEvent *gc_title_event) : brls::RepeatingTask(TASK_INTERVAL)
    {
        this->gc_title_event = gc_title_event;
    }
    
    void GameCardTitleTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        if (titleIsGameCardInfoUpdated())
        {
            this->gc_title_event->fire();
            brls::Logger::debug("Gamecard title info updated.");
        }
    }
    
    /* USB Mass Storage task. */
    
    UmsTask::UmsTask(VoidEvent *ums_event) : brls::RepeatingTask(TASK_INTERVAL)
    {
        this->ums_event = ums_event;
    }
    
    void UmsTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        if (umsIsDeviceInfoUpdated())
        {
            this->ums_event->fire();
            brls::Logger::debug("UMS device info updated.");
        }
    }
    
    /* USB host device connection task. */
    
    UsbHostTask::UsbHostTask(BooleanEvent *usb_host_event) : brls::RepeatingTask(TASK_INTERVAL)
    {
        this->usb_host_event = usb_host_event;
    }
    
    void UsbHostTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        this->cur_usb_host_status = usbIsReady();
        if (this->cur_usb_host_status != this->prev_usb_host_status)
        {
            this->usb_host_event->fire(this->cur_usb_host_status);
            this->prev_usb_host_status = this->cur_usb_host_status;
            brls::Logger::debug("USB host status change triggered: {}.", this->cur_usb_host_status);
        }
    }
}
