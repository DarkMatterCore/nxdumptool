/*
 * title_metadata_task.cpp
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

#include <tasks/title_metadata_task.hpp>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::tasks
{
    TitleMetadataTask::TitleMetadataTask() : brls::RepeatingTask(REPEATING_TASK_INTERVAL)
    {
        LOG_MSG_DEBUG("Title metadata task started.");

        /* Get system metadata entries. */
        this->PopulateApplicationMetadataInfo(true);

        /* Get user metadata entries. */
        this->PopulateApplicationMetadataInfo(false);

        /* Start task. */
        brls::RepeatingTask::start();
    }

    TitleMetadataTask::~TitleMetadataTask()
    {
        /* Free application metadata arrays. */
        if (this->system_metadata_info.app_metadata) free(this->system_metadata_info.app_metadata);
        if (this->user_metadata_info.app_metadata) free(this->user_metadata_info.app_metadata);

        LOG_MSG_DEBUG("Title metadata task stopped.");
    }

    void TitleMetadataTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);

        if (titleIsGameCardInfoUpdated())
        {
            /* Update user metadata array. */
            this->PopulateApplicationMetadataInfo(false);

            /* Fire task event. */
            this->user_title_event.fire(this->user_metadata_info);

            //brls::Application::notify("tasks/notifications/user_titles"_i18n);
            LOG_MSG_DEBUG("Title info updated.");
        }
    }

    void TitleMetadataTask::PopulateApplicationMetadataInfo(bool is_system)
    {
        /* Get reference to output struct. */
        TitleApplicationMetadataInfo& info = (is_system ? this->system_metadata_info : this->user_metadata_info);
        if (info.app_metadata) free(info.app_metadata);
        info.app_metadata_count = 0;

        /* Get application metadata entries. */
        info.app_metadata = titleGetApplicationMetadataEntries(is_system, &(info.app_metadata_count));

        LOG_MSG_DEBUG("Retrieved %u %s metadata %s.", info.app_metadata_count, is_system ? "system" : "user", info.app_metadata_count == 1 ? "entry" : "entries");
    }
}
