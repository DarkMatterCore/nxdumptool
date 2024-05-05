/*
 * ums_task.cpp
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

#include <tasks/ums_task.hpp>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::tasks
{
    UmsTask::UmsTask() : brls::RepeatingTask(REPEATING_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();
        LOG_MSG_DEBUG("UMS task started.");
    }

    UmsTask::~UmsTask()
    {
        /* Clear UMS device vector. */
        this->ums_devices_vector.clear();

        /* Free UMS devices buffer. */
        if (this->ums_devices) free(this->ums_devices);

        LOG_MSG_DEBUG("UMS task stopped.");
    }

    void UmsTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);

        if (umsIsDeviceInfoUpdated())
        {
            LOG_MSG_DEBUG("UMS device info updated.");
            brls::Application::notify("tasks/notifications/ums_device"_i18n);

            /* Update UMS device vector. */
            this->PopulateUmsDeviceVector();

            /* Fire task event. */
            this->ums_event.fire(this->ums_devices_vector);
        }
    }

    void UmsTask::PopulateUmsDeviceVector(void)
    {
        /* Clear UMS device vector. */
        this->ums_devices_vector.clear();

        /* Free UMS devices buffer. */
        if (this->ums_devices) free(this->ums_devices);

        /* Reset UMS devices counter. */
        this->ums_devices_count = 0;

        /* Get UMS devices. */
        this->ums_devices = umsGetDevices(&(this->ums_devices_count));
        if (this->ums_devices)
        {
            /* Fill UMS device vector. */
            for(u32 i = 0; i < this->ums_devices_count; i++)
            {
                const UsbHsFsDevice *cur_ums_device = &(this->ums_devices[i]);
                int name_len = static_cast<int>(strlen(cur_ums_device->name) - 1);
                std::string ums_info{};

                if (cur_ums_device->product_name[0])
                {
                    ums_info = fmt::format("{1:.{0}} ({2}, LUN #{3}, FS#{4}, {5})", name_len, cur_ums_device->name, cur_ums_device->product_name, cur_ums_device->lun, cur_ums_device->fs_idx, LIBUSBHSFS_FS_TYPE_STR(cur_ums_device->fs_type));
                } else {
                    ums_info = fmt::format("{1:.{0}} (LUN #{2}, FS#{3}, {4})", name_len, cur_ums_device->name, cur_ums_device->lun, cur_ums_device->fs_idx, LIBUSBHSFS_FS_TYPE_STR(cur_ums_device->fs_type));
                }

                this->ums_devices_vector.push_back(std::make_pair(cur_ums_device, ums_info));
            }
        }

        LOG_MSG_DEBUG("Retrieved info for %u UMS %s.", this->ums_devices_count, this->ums_devices_count == 1 ? "device" : "devices");
    }
}
