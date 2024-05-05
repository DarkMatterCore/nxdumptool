/*
 * ums_task.hpp
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

#ifndef __UMS_TASK_HPP__
#define __UMS_TASK_HPP__

#include <borealis.hpp>

#include "../core/nxdt_utils.h"
#include "../core/ums.h"

namespace nxdt::tasks
{
    /* Used to hold information from UMS devices. */
    typedef std::pair<const UsbHsFsDevice*, std::string> UmsDeviceVectorEntry;
    typedef std::vector<UmsDeviceVectorEntry> UmsDeviceVector;

    /* Custom event type. */
    typedef brls::Event<const UmsDeviceVector&> UmsEvent;

    /* USB Mass Storage task. */
    /* Its event provides a const reference to a UmsDeviceVector. */
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
            UmsTask();
            ~UmsTask();

            /* Intentionally left here to let views retrieve UMS device info on-demand. */
            ALWAYS_INLINE const UmsDeviceVector& GetUmsDevices(void)
            {
                return this->ums_devices_vector;
            }

            ALWAYS_INLINE UmsEvent::Subscription RegisterListener(UmsEvent::Callback cb)
            {
                return this->ums_event.subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterListener(UmsEvent::Subscription subscription)
            {
                this->ums_event.unsubscribe(subscription);
            }
    };
}

#endif  /* __UMS_TASK_HPP__ */
