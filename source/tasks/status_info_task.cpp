/*
 * status_info_task.cpp
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

#include <tasks/status_info_task.hpp>

namespace nxdt::tasks
{
    StatusInfoTask::StatusInfoTask() : brls::RepeatingTask(REPEATING_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        LOG_MSG_DEBUG("Status info task started.");
    }

    StatusInfoTask::~StatusInfoTask()
    {
        LOG_MSG_DEBUG("Status info task stopped.");
    }

    void StatusInfoTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);

        StatusInfoData *status_info_data = &(this->status_info_data);

        /* Get current time. */
        time_t unix_time = time(nullptr);
        localtime_r(&unix_time, &(status_info_data->timeinfo));

        /* Get battery stats. */
        psmGetBatteryChargePercentage(&(status_info_data->charge_percentage));
        psmGetChargerType(&(status_info_data->charger_type));

        /* Get network connection status. */
        u32 signal_strength = 0;
        NifmInternetConnectionStatus connection_status{};
        char *ip_addr = nullptr;

        status_info_data->connected = false;

        Result rc = nifmGetInternetConnectionStatus(&(status_info_data->connection_type), &signal_strength, &connection_status);
        if (R_SUCCEEDED(rc) && status_info_data->connection_type && connection_status == NifmInternetConnectionStatus_Connected)
        {
            status_info_data->connected = true;

            struct in_addr addr = { .s_addr = INADDR_NONE };
            nifmGetCurrentIpAddress(&(addr.s_addr));

            if (addr.s_addr != INADDR_NONE && (ip_addr = inet_ntoa(addr))) snprintf(status_info_data->ip_addr, MAX_ELEMENTS(status_info_data->ip_addr), "%s", ip_addr);
        }

        /* Fire task event. */
        this->status_info_event.fire(this->status_info_data);
    }
}
