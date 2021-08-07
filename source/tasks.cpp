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
#include <arpa/inet.h>

#define NXDT_TASK_INTERVAL  250 /* 250 ms. */

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::tasks
{
    /* Status info task. */
    
    StatusInfoTask::StatusInfoTask(void) : brls::RepeatingTask(NXDT_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        brls::Logger::debug("Status info task started.");
    }
    
    StatusInfoTask::~StatusInfoTask(void)
    {
        brls::Logger::debug("Status info task stopped.");
    }
    
    bool StatusInfoTask::IsInternetConnectionAvailable(void)
    {
        return (this->status_info_data.ip_addr != NULL);
    }
    
    void StatusInfoTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        StatusInfoData *status_info_data = &(this->status_info_data);
        
        /* Get current time. */
        time_t unix_time = time(NULL);
        localtime_r(&unix_time, &(status_info_data->timeinfo));
        
        /* Get battery stats. */
        psmGetBatteryChargePercentage(&(status_info_data->charge_percentage));
        psmGetChargerType(&(status_info_data->charger_type));
        
        /* Get network connection status. */
        u32 signal_strength = 0;
        NifmInternetConnectionStatus connection_status = static_cast<NifmInternetConnectionStatus>(0);
        
        Result rc = nifmGetInternetConnectionStatus(&(status_info_data->connection_type), &signal_strength, &connection_status);
        if (R_SUCCEEDED(rc))
        {
            if (status_info_data->connection_type && connection_status == NifmInternetConnectionStatus_Connected)
            {
                struct in_addr addr = { .s_addr = 0 };
                nifmGetCurrentIpAddress(&(addr.s_addr));
                status_info_data->ip_addr = inet_ntoa(addr);
            } else {
                status_info_data->ip_addr = NULL;
            }
        } else {
            status_info_data->connection_type = static_cast<NifmInternetConnectionType>(0);
            status_info_data->ip_addr = NULL;
        }
        
        /* Fire task event. */
        this->status_info_event.fire(status_info_data);
    }
    
    /* Gamecard task. */
    
    GameCardTask::GameCardTask(void) : brls::RepeatingTask(NXDT_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        brls::Logger::debug("Gamecard task started.");
        
        this->first_notification = (gamecardGetStatus() >= GameCardStatus_Processing);
    }
    
    GameCardTask::~GameCardTask(void)
    {
        brls::Logger::debug("Gamecard task stopped.");
    }
    
    void GameCardTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        this->cur_gc_status = static_cast<GameCardStatus>(gamecardGetStatus());
        if (this->cur_gc_status != this->prev_gc_status)
        {
            brls::Logger::debug("Gamecard status change triggered: {}.", this->cur_gc_status);
            
            if (!this->first_notification)
            {
                if (this->prev_gc_status == GameCardStatus_NotInserted && this->cur_gc_status == GameCardStatus_Processing)
                {
                    brls::Application::notify("gamecard_tab/error_frame/processing"_i18n);
                } else
                if (this->prev_gc_status == GameCardStatus_Processing && this->cur_gc_status > GameCardStatus_Processing)
                {
                    brls::Application::notify("tasks/notifications/gamecard_status_updated"_i18n);
                } else
                if (this->cur_gc_status == GameCardStatus_NotInserted)
                {
                    brls::Application::notify("tasks/notifications/gamecard_ejected"_i18n);
                }
            } else {
                this->first_notification = false;
            }
            
            /* Update previous gamecard status. */
            this->prev_gc_status = this->cur_gc_status;
            
            /* Fire task event. */
            this->gc_status_event.fire(this->cur_gc_status);
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
    
    void TitleTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        if (titleIsGameCardInfoUpdated())
        {
            brls::Logger::debug("Title info updated.");
            //brls::Application::notify("tasks/notifications/user_titles"_i18n);
            
            /* Update user metadata vector. */
            this->PopulateApplicationMetadataVector(false);
            
            /* Fire task event. */
            this->title_event.fire(&(this->user_metadata));
        }
    }
    
    const TitleApplicationMetadataVector* TitleTask::GetApplicationMetadata(bool is_system)
    {
        return (is_system ? &(this->system_metadata) : &(this->user_metadata));
    }
    
    void TitleTask::PopulateApplicationMetadataVector(bool is_system)
    {
        TitleApplicationMetadata **app_metadata = NULL;
        u32 app_metadata_count = 0;
        
        /* Get pointer to output vector. */
        TitleApplicationMetadataVector *vector = (is_system ? &(this->system_metadata) : &(this->user_metadata));
        vector->clear();
        
        /* Get application metadata entries. */
        app_metadata = titleGetApplicationMetadataEntries(is_system, &app_metadata_count);
        if (app_metadata)
        {
            /* Fill output vector. */
            for(u32 i = 0; i < app_metadata_count; i++) vector->push_back(app_metadata[i]);
            
            /* Free application metadata array. */
            free(app_metadata);
        }
        
        brls::Logger::debug("Retrieved {} {} metadata {}.", app_metadata_count, is_system ? "system" : "user", app_metadata_count == 1 ? "entry" : "entries");
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
    
    void UmsTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        
        if (umsIsDeviceInfoUpdated())
        {
            brls::Logger::debug("UMS device info updated.");
            brls::Application::notify("tasks/notifications/ums_device"_i18n);
            
            /* Update UMS device vector. */
            this->PopulateUmsDeviceVector();
            
            /* Fire task event. */
            this->ums_event.fire(&(this->ums_devices));
        }
    }
    
    void UmsTask::PopulateUmsDeviceVector(void)
    {
        UsbHsFsDevice *ums_devices = NULL;
        u32 ums_device_count = 0;
        
        /* Clear UMS device vector. */
        this->ums_devices.clear();
        
        /* Get UMS devices. */
        ums_devices = umsGetDevices(&ums_device_count);
        if (ums_devices)
        {
            /* Fill UMS device vector. */
            for(u32 i = 0; i < ums_device_count; i++) this->ums_devices.push_back(ums_devices[i]);
            
            /* Free UMS devices array. */
            free(ums_devices);
        }
        
        brls::Logger::debug("Retrieved info for {} UMS {}.", ums_device_count, ums_device_count == 1 ? "device" : "devices");
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
        
        this->cur_usb_host_speed = static_cast<UsbHostSpeed>(usbIsReady());
        if (this->cur_usb_host_speed != this->prev_usb_host_speed)
        {
            brls::Logger::debug("USB host speed changed: {}.", this->cur_usb_host_speed);
            brls::Application::notify(this->cur_usb_host_speed ? "tasks/notifications/usb_host_connected"_i18n : "tasks/notifications/usb_host_disconnected"_i18n);
            
            /* Update previous USB host speed. */
            this->prev_usb_host_speed = this->cur_usb_host_speed;
            
            /* Fire task event. */
            this->usb_host_event.fire(this->cur_usb_host_speed);
        }
    }
}
