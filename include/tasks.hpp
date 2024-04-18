/*
 * tasks.hpp
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include "core/nxdt_includes.h"
#include "core/gamecard.h"
#include "core/title.h"
#include "core/ums.h"
#include "core/usb.h"
#include "download_task.hpp"

#define EVENT_SUBSCRIPTION(event_type, event_name) \
    ALWAYS_INLINE event_type::Subscription RegisterListener(event_type::Callback cb) { return this->event_name.subscribe(cb); } \
    ALWAYS_INLINE void UnregisterListener(event_type::Subscription subscription) { this->event_name.unsubscribe(subscription); }

namespace nxdt::tasks
{
    /* Used to hold status info data. */
    typedef struct {
        struct tm timeinfo;
        u32 charge_percentage;
        PsmChargerType charger_type;
        bool connected;
        NifmInternetConnectionType connection_type;
        char ip_addr[16];
    } StatusInfoData;

    /* Used to hold pointers to application metadata entries. */
    typedef std::vector<TitleApplicationMetadata*> TitleApplicationMetadataVector;

    /* Used to hold information from UMS devices. */
    typedef std::pair<const UsbHsFsDevice*, std::string> UmsDeviceVectorEntry;
    typedef std::vector<UmsDeviceVectorEntry> UmsDeviceVector;

    /* Custom event types. */
    typedef brls::Event<const StatusInfoData&> StatusInfoEvent;
    typedef brls::Event<const GameCardStatus&> GameCardStatusEvent;
    typedef brls::Event<const TitleApplicationMetadataVector&> UserTitleEvent;
    typedef brls::Event<const UmsDeviceVector&> UmsEvent;
    typedef brls::Event<const UsbHostSpeed&> UsbHostEvent;

    /* Status info task. */
    /* Its event returns a reference to a StatusInfoData struct. */
    class StatusInfoTask: public brls::RepeatingTask
    {
        private:
            StatusInfoEvent status_info_event;
            StatusInfoData status_info_data{};

        protected:
            void run(retro_time_t current_time) override;

        public:
            StatusInfoTask(void);
            ~StatusInfoTask(void);

            bool IsInternetConnectionAvailable(void);

            EVENT_SUBSCRIPTION(StatusInfoEvent, status_info_event);
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

            EVENT_SUBSCRIPTION(GameCardStatusEvent, gc_status_event);
    };

    /* Title task. */
    /* Its event returns a reference to a TitleApplicationMetadataVector with metadata for user titles (system titles don't change at runtime). */
    class TitleTask: public brls::RepeatingTask
    {
        private:
            UserTitleEvent user_title_event;

            TitleApplicationMetadataVector system_metadata{};
            TitleApplicationMetadataVector user_metadata{};

            void PopulateApplicationMetadataVector(bool is_system);

        protected:
            void run(retro_time_t current_time) override;

        public:
            TitleTask(void);
            ~TitleTask(void);

            /* Intentionally left here to let views retrieve title metadata on-demand. */
            const TitleApplicationMetadataVector& GetApplicationMetadata(bool is_system);

            EVENT_SUBSCRIPTION(UserTitleEvent, user_title_event);
    };

    /* USB Mass Storage task. */
    /* Its event returns a reference to a UmsDeviceVector. */
    class UmsTask: public brls::RepeatingTask
    {
        private:
            UmsEvent ums_event;

            UsbHsFsDevice *ums_devices = nullptr;
            u32 ums_devices_count = 0;

            UmsDeviceVector ums_devices_vector{};

            void PopulateUmsDeviceVector(void);

        protected:
            void run(retro_time_t current_time) override;

        public:
            UmsTask(void);
            ~UmsTask(void);

            /* Intentionally left here to let views retrieve UMS device info on-demand. */
            const UmsDeviceVector& GetUmsDevices(void);

            EVENT_SUBSCRIPTION(UmsEvent, ums_event);
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

            EVENT_SUBSCRIPTION(UsbHostEvent, usb_host_event);
    };
}

#undef EVENT_SUBSCRIPTION

#endif  /* __TASKS_HPP__ */
