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

#define NXDT_TASK_INTERVAL  100 /* 100 ms. */

namespace nxdt::tasks
{
    /* Gamecard task. */
    
    GameCardTask::GameCardTask(void) : brls::RepeatingTask(NXDT_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        brls::Logger::debug("Gamecard task started.");
    }
    
    GameCardTask::~GameCardTask(void)
    {
        brls::Logger::debug("Gamecard task stopped.");
    }
    
    void GameCardTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        this->cur_gc_status = (GameCardStatus)gamecardGetStatus();
        if (this->cur_gc_status != this->prev_gc_status)
        {
            this->gc_status_event.fire(this->cur_gc_status);
            this->prev_gc_status = this->cur_gc_status;
            brls::Logger::debug("Gamecard status change triggered: {}.", this->cur_gc_status);
        }
    }
    
    /* Title task. */
    
    TitleTask::TitleTask(void) : brls::RepeatingTask(NXDT_TASK_INTERVAL)
    {
        /* Get system metadata entries. */
        this->PopulateApplicationMetadataVector(true);
        
        /* Get user metadata entries. */
        this->PopulateApplicationMetadataVector(false);
        
        /* Start task. */
        brls::RepeatingTask::start();
        brls::Logger::debug("Title task started.");
    }
    
    TitleTask::~TitleTask(void)
    {
        /* Clear application metadata vectors. */
        this->system_metadata.clear();
        this->user_metadata.clear();
        
        brls::Logger::debug("Title task stopped.");
    }
    
    void TitleTask::PopulateApplicationMetadataVector(bool is_system)
    {
        TitleApplicationMetadata **app_metadata = NULL;
        u32 app_metadata_count = 0;
        
        /* Get pointer to output vector. */
        TitleApplicationMetadataVector *vector = (is_system ? &(this->system_metadata) : &(this->user_metadata));
        if (vector->size()) vector->clear();
        
        /* Get application metadata entries. */
        app_metadata = titleGetApplicationMetadataEntries(is_system, &app_metadata_count);
        if (!app_metadata) return;
        
        /* Fill output vector. */
        for(u32 i = 0; i < app_metadata_count; i++) vector->push_back(app_metadata[i]);
        
        /* Free application metadata array. */
        free(app_metadata);
        
        brls::Logger::debug("Retrieved {} {} metadata {}.", app_metadata_count, is_system ? "system" : "user", app_metadata_count == 1 ? "entry" : "entries");
    }
    
    void TitleTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        if (titleIsGameCardInfoUpdated())
        {
            /* Update user metadata vector. */
            this->PopulateApplicationMetadataVector(false);
            
            /* Fire task event. */
            this->title_event.fire();
            brls::Logger::debug("Title info updated.");
        }
    }
    
    TitleApplicationMetadataVector* TitleTask::GetApplicationMetadata(bool is_system)
    {
        return (is_system ? &(this->system_metadata) : &(this->user_metadata));
    }
    
    /* USB Mass Storage task. */
    
    UmsTask::UmsTask(void) : brls::RepeatingTask(NXDT_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        brls::Logger::debug("UMS task started.");
    }
    
    UmsTask::~UmsTask(void)
    {
        /* Clear UMS device vector. */
        this->ums_devices.clear();
        
        brls::Logger::debug("UMS task stopped.");
    }
    
    void UmsTask::PopulateUmsDeviceVector(void)
    {
        UsbHsFsDevice *ums_devices = NULL;
        u32 ums_device_count = 0;
        
        /* Clear UMS device vector (if needed). */
        if (this->ums_devices.size()) this->ums_devices.clear();
        
        /* Get UMS devices. */
        ums_devices = umsGetDevices(&ums_device_count);
        if (!ums_devices) return;
        
        /* Fill UMS device vector. */
        for(u32 i = 0; i < ums_device_count; i++) this->ums_devices.push_back(ums_devices[i]);
        
        /* Free UMS devices array. */
        free(ums_devices);
        
        brls::Logger::debug("Retrieved info for {} UMS {}.", ums_device_count, ums_device_count == 1 ? "device" : "devices");
    }
    
    void UmsTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        if (umsIsDeviceInfoUpdated())
        {
            /* Update UMS device vector. */
            this->PopulateUmsDeviceVector();
            
            /* Fire task event. */
            this->ums_event.fire();
            brls::Logger::debug("UMS device info updated.");
        }
    }
    
    UmsDeviceVector* UmsTask::GetUmsDevices(void)
    {
        return &(this->ums_devices);
    }
    
    /* USB host device connection task. */
    
    UsbHostTask::UsbHostTask(void) : brls::RepeatingTask(NXDT_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        brls::Logger::debug("USB host task started.");
    }
    
    UsbHostTask::~UsbHostTask(void)
    {
        brls::Logger::debug("USB host task stopped.");
    }
    
    void UsbHostTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        this->cur_usb_host_status = usbIsReady();
        if (this->cur_usb_host_status != this->prev_usb_host_status)
        {
            this->usb_host_event.fire(this->cur_usb_host_status);
            this->prev_usb_host_status = this->cur_usb_host_status;
            brls::Logger::debug("USB host status change triggered: {}.", this->cur_usb_host_status);
        }
    }
}
