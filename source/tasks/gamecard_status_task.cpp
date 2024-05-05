/*
 * gamecard_status_task.cpp
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

#include <tasks/gamecard_status_task.hpp>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::tasks
{
    GameCardStatusTask::GameCardStatusTask() : brls::RepeatingTask(REPEATING_TASK_INTERVAL)
    {
        brls::RepeatingTask::start();

        this->first_notification = (gamecardGetStatus() != GameCardStatus_NotInserted);

        LOG_MSG_DEBUG("Gamecard task started with first_notification = %u.", this->first_notification);
    }

    GameCardStatusTask::~GameCardStatusTask()
    {
        LOG_MSG_DEBUG("Gamecard task stopped.");
    }

    void GameCardStatusTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);

        this->cur_gc_status = static_cast<GameCardStatus>(gamecardGetStatus());
        if (this->cur_gc_status != this->prev_gc_status)
        {
            LOG_MSG_DEBUG("Gamecard status change triggered: %u.", this->cur_gc_status);

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
}
