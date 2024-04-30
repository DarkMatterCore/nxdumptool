/*
 * gamecard_status_task.hpp
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

#ifndef __GAMECARD_STATUS_TASK_HPP__
#define __GAMECARD_STATUS_TASK_HPP__

#include <borealis.hpp>

#include "../core/nxdt_utils.h"
#include "../core/gamecard.h"

namespace nxdt::tasks
{
    /* Custom event type. */
    typedef brls::Event<const GameCardStatus&> GameCardStatusEvent;

    /* Gamecard status task. */
    /* Its event provides a const reference to a GameCardStatus value. */
    class GameCardStatusTask: public brls::RepeatingTask
    {
        private:
            GameCardStatusEvent gc_status_event;
            GameCardStatus cur_gc_status = GameCardStatus_NotInserted;
            GameCardStatus prev_gc_status = GameCardStatus_NotInserted;
            bool first_notification = true;

        protected:
            void run(retro_time_t current_time) override;

        public:
            GameCardStatusTask();
            ~GameCardStatusTask();

            ALWAYS_INLINE GameCardStatusEvent::Subscription RegisterListener(GameCardStatusEvent::Callback cb)
            {
                return this->gc_status_event.subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterListener(GameCardStatusEvent::Subscription subscription)
            {
                this->gc_status_event.unsubscribe(subscription);
            }
    };
}

#endif  /* __GAMECARD_STATUS_TASK_HPP__ */
