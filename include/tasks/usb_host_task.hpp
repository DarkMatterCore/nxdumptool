/*
 * usb_host_task.hpp
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

#ifndef __USB_HOST_TASK_HPP__
#define __USB_HOST_TASK_HPP__

#include <borealis.hpp>

#include "../core/nxdt_utils.h"
#include "../core/usb.h"

namespace nxdt::tasks
{
    /* Custom event type. */
    typedef brls::Event<const UsbHostSpeed&> UsbHostEvent;

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
            UsbHostTask();
            ~UsbHostTask();

            /* Intentionally left here to let views retrieve USB host connection speed on-demand. */
            const UsbHostSpeed& GetUsbHostSpeed(void);

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

#endif  /* __USB_HOST_TASK_HPP__ */
