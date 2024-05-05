/*
 * status_info_task.hpp
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

#ifndef __STATUS_INFO_TASK_HPP__
#define __STATUS_INFO_TASK_HPP__

#include <borealis.hpp>

#include "../core/nxdt_utils.h"

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

    /* Custom event type. */
    typedef brls::Event<const StatusInfoData&> StatusInfoEvent;

    /* Status info task. */
    /* Its event provides a const reference to a StatusInfoData struct. */
    class StatusInfoTask: public brls::RepeatingTask
    {
        private:
            StatusInfoEvent status_info_event;
            StatusInfoData status_info_data{};

        protected:
            void run(retro_time_t current_time) override;

        public:
            StatusInfoTask();
            ~StatusInfoTask();

            ALWAYS_INLINE bool IsInternetConnectionAvailable(void)
            {
                return this->status_info_data.connected;
            }

            ALWAYS_INLINE StatusInfoEvent::Subscription RegisterListener(StatusInfoEvent::Callback cb)
            {
                return this->status_info_event.subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterListener(StatusInfoEvent::Subscription subscription)
            {
                this->status_info_event.unsubscribe(subscription);
            }
    };
}

#endif  /* __STATUS_INFO_TASK_HPP__ */
