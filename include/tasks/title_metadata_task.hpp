/*
 * title_metadata_task.hpp
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

#ifndef __TITLE_METADATA_TASK_HPP__
#define __TITLE_METADATA_TASK_HPP__

#include <borealis.hpp>

#include "../core/nxdt_utils.h"
#include "../core/title.h"

namespace nxdt::tasks
{
    /* Used to hold an application metadata array + its number of elements. */
    typedef struct {
        TitleApplicationMetadata **app_metadata;
        u32 app_metadata_count;
    } TitleApplicationMetadataInfo;

    /* Custom event type. */
    typedef brls::Event<const TitleApplicationMetadataInfo&> UserTitleEvent;

    /* Title metadata task. */
    /* Its event provides a const reference to a TitleApplicationMetadataInfo with metadata for user titles (system titles don't change at runtime). */
    class TitleMetadataTask: public brls::RepeatingTask
    {
        private:
            UserTitleEvent user_title_event;

            TitleApplicationMetadataInfo system_metadata_info{};
            TitleApplicationMetadataInfo user_metadata_info{};

            void PopulateApplicationMetadataInfo(bool is_system);

        protected:
            void run(retro_time_t current_time) override;

        public:
            TitleMetadataTask();
            ~TitleMetadataTask();

            /* Intentionally left here to let views retrieve title metadata on-demand. */
            ALWAYS_INLINE const TitleApplicationMetadataInfo& GetApplicationMetadataInfo(bool is_system)
            {
                return (is_system ? this->system_metadata_info : this->user_metadata_info);
            }

            ALWAYS_INLINE UserTitleEvent::Subscription RegisterListener(UserTitleEvent::Callback cb)
            {
                return this->user_title_event.subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterListener(UserTitleEvent::Subscription subscription)
            {
                this->user_title_event.unsubscribe(subscription);
            }
    };
}

#endif  /* __TITLE_METADATA_TASK_HPP__ */
