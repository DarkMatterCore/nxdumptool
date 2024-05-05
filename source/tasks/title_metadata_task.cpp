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
        this->PopulateApplicationMetadataVector(true);

        /* Get user metadata entries. */
        this->PopulateApplicationMetadataVector(false);

        /* Start task. */
        brls::RepeatingTask::start();
    }

    TitleMetadataTask::~TitleMetadataTask()
    {
        /* Clear application metadata vectors. */
        this->system_metadata.clear();
        this->user_metadata.clear();

        LOG_MSG_DEBUG("Title metadata task stopped.");
    }

    void TitleMetadataTask::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);

        if (titleIsGameCardInfoUpdated())
        {
            /* Update user metadata vector. */
            this->PopulateApplicationMetadataVector(false);

            /* Fire task event. */
            this->user_title_event.fire(this->user_metadata);

            //brls::Application::notify("tasks/notifications/user_titles"_i18n);
            LOG_MSG_DEBUG("Title info updated.");
        }
    }

    void TitleMetadataTask::PopulateApplicationMetadataVector(bool is_system)
    {
        TitleApplicationMetadata **app_metadata = nullptr;
        u32 app_metadata_count = 0;

        /* Get pointer to output vector. */
        TitleApplicationMetadataVector& vector = (is_system ? this->system_metadata : this->user_metadata);
        vector.clear();

        /* Get application metadata entries. */
        app_metadata = titleGetApplicationMetadataEntries(is_system, &app_metadata_count);
        if (app_metadata)
        {
            /* Fill output vector. */
            for(u32 i = 0; i < app_metadata_count; i++) vector.push_back(app_metadata[i]);

            /* Free application metadata array. */
            free(app_metadata);
        }

        LOG_MSG_DEBUG("Retrieved %u %s metadata %s.", app_metadata_count, is_system ? "system" : "user", app_metadata_count == 1 ? "entry" : "entries");
    }
}
