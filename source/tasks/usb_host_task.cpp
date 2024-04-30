/*
 * usb_host_task.cpp
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

#include <tasks/usb_host_task.hpp>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::tasks
{
    UsbHostTask::UsbHostTask() : brls::RepeatingTask(REPEATING_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        LOG_MSG_DEBUG("USB host task started.");
    }

    UsbHostTask::~UsbHostTask()
    {
        LOG_MSG_DEBUG("USB host task stopped.");
    }

    void UsbHostTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);

        this->cur_usb_host_speed = static_cast<UsbHostSpeed>(usbIsReady());
        if (this->cur_usb_host_speed != this->prev_usb_host_speed)
        {
            LOG_MSG_DEBUG("USB host speed changed: %u.", this->cur_usb_host_speed);
            brls::Application::notify(this->cur_usb_host_speed ? "tasks/notifications/usb_host_connected"_i18n : "tasks/notifications/usb_host_disconnected"_i18n);

            /* Update previous USB host speed. */
            this->prev_usb_host_speed = this->cur_usb_host_speed;

            /* Fire task event. */
            this->usb_host_event.fire(this->cur_usb_host_speed);
        }
    }

    const UsbHostSpeed& UsbHostTask::GetUsbHostSpeed(void)
    {
        return this->cur_usb_host_speed;
    }
}
