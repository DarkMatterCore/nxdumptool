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

#include "defines.h"
#include "core/gamecard.h"
#include "core/title.h"
#include "core/ums.h"
#include "core/usb.h"
#include "download_task.hpp"

namespace nxdt::tasks
{
    /* Used to hold status info data. */
    typedef struct {
        struct tm timeinfo;
        u32 charge_percentage;
        PsmChargerType charger_type;
        NifmInternetConnectionType connection_type;
        char *ip_addr;
    } StatusInfoData;
    
    /* Used to hold pointers to application metadata entries. */
    typedef std::vector<TitleApplicationMetadata*> TitleApplicationMetadataVector;
    
    /* Used to hold UMS devices. */
    typedef std::vector<UsbHsFsDevice> UmsDeviceVector;
    
    /* Custom event types. */
    typedef brls::Event<const StatusInfoData*> StatusInfoEvent;
    typedef brls::Event<GameCardStatus> GameCardStatusEvent;
    typedef brls::Event<const TitleApplicationMetadataVector*> TitleEvent;
    typedef brls::Event<const UmsDeviceVector*> UmsEvent;
    typedef brls::Event<UsbHostSpeed> UsbHostEvent;
    
    /* Status info task. */
    /* Its event returns a pointer to a StatusInfoData struct. */
    class StatusInfoTask: public brls::RepeatingTask
    {
        private:
            StatusInfoEvent status_info_event;
            StatusInfoData status_info_data = {0};
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            StatusInfoTask(void);
            ~StatusInfoTask(void);
            
            bool IsInternetConnectionAvailable(void);
            
            ALWAYS_INLINE StatusInfoEvent::Subscription RegisterListener(StatusInfoEvent::Callback cb)
            {
                return this->status_info_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(StatusInfoEvent::Subscription subscription)
            {
                this->status_info_event.unsubscribe(subscription);
            }
    };
    
    /* Gamecard task. */
    /* Its event returns a GameCardStatus value. */
    class GameCardTask: public brls::RepeatingTask
    {
        private:
            GameCardStatusEvent gc_status_event;
            GameCardStatus cur_gc_status = GameCardStatus_NotInserted;
            GameCardStatus prev_gc_status = GameCardStatus_NotInserted;
            bool first_notification = true;
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            GameCardTask(void);
            ~GameCardTask(void);
            
            ALWAYS_INLINE GameCardStatusEvent::Subscription RegisterListener(GameCardStatusEvent::Callback cb)
            {
                return this->gc_status_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(GameCardStatusEvent::Subscription subscription)
            {
                this->gc_status_event.unsubscribe(subscription);
            }
    };
    
    /* Title task. */
    /* Its event returns a pointer to a TitleApplicationMetadataVector with metadata for user titles (system titles don't change at runtime). */
    class TitleTask: public brls::RepeatingTask
    {
        private:
            TitleEvent title_event;
            
            TitleApplicationMetadataVector system_metadata;
            TitleApplicationMetadataVector user_metadata;
            
            void PopulateApplicationMetadataVector(bool is_system);
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            TitleTask(void);
            ~TitleTask(void);
            
            /* Intentionally left here to let system titles views retrieve metadata. */
            const TitleApplicationMetadataVector* GetApplicationMetadata(bool is_system);
            
            ALWAYS_INLINE TitleEvent::Subscription RegisterListener(TitleEvent::Callback cb)
            {
                return this->title_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(TitleEvent::Subscription subscription)
            {
                this->title_event.unsubscribe(subscription);
            }
    };
    
    /* USB Mass Storage task. */
    /* Its event returns a pointer to a UmsDeviceVector. */
    class UmsTask: public brls::RepeatingTask
    {
        private:
            UmsEvent ums_event;
            
            UmsDeviceVector ums_devices;
            
            void PopulateUmsDeviceVector(void);
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            UmsTask(void);
            ~UmsTask(void);
            
            ALWAYS_INLINE UmsEvent::Subscription RegisterListener(UmsEvent::Callback cb)
            {
                return this->ums_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(UmsEvent::Subscription subscription)
            {
                this->ums_event.unsubscribe(subscription);
            }
    };
    
    /* USB host device connection task. */
    class UsbHostTask: public brls::RepeatingTask
    {
        private:
            UsbHostEvent usb_host_event;
            UsbHostSpeed cur_usb_host_speed = UsbHostSpeed_None;
            UsbHostSpeed prev_usb_host_speed = UsbHostSpeed_None;
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            UsbHostTask(void);
            ~UsbHostTask(void);
            
            ALWAYS_INLINE UsbHostEvent::Subscription RegisterListener(UsbHostEvent::Callback cb)
            {
                return this->usb_host_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(UsbHostEvent::Subscription subscription)
            {
                this->usb_host_event.unsubscribe(subscription);
            }
    };
}

#endif  /* __TASKS_HPP__ */
