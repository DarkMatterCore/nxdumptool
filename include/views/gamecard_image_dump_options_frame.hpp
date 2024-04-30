/*
 * gamecard_image_dump_options_frame.hpp
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

#ifndef __GAMECARD_IMAGE_DUMP_OPTIONS_FRAME_HPP__
#define __GAMECARD_IMAGE_DUMP_OPTIONS_FRAME_HPP__

#include "dump_options_frame.hpp"

namespace nxdt::views
{
    class GameCardImageDumpOptionsFrame: public DumpOptionsFrame
    {
        private:
            nxdt::tasks::GameCardStatusEvent::Subscription gc_task_sub;
            brls::VoidEvent gc_ejected_event;

            brls::ToggleListItem *prepend_key_area = nullptr;
            brls::ToggleListItem *keep_certificate = nullptr;
            brls::ToggleListItem *trim_dump = nullptr;
            brls::ToggleListItem *calculate_checksum = nullptr;
            brls::SelectListItem *checksum_lookup_method = nullptr;

        public:
            GameCardImageDumpOptionsFrame(RootView *root_view, std::string raw_filename);
            ~GameCardImageDumpOptionsFrame();

            ALWAYS_INLINE brls::VoidEvent::Subscription RegisterGameCardEjectionListener(brls::VoidEvent::Callback cb)
            {
                return this->gc_ejected_event.subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterGameCardEjectionListener(brls::VoidEvent::Subscription subscription)
            {
                this->gc_ejected_event.unsubscribe(subscription);
            }
    };
}

#endif  /* __GAMECARD_IMAGE_DUMP_OPTIONS_FRAME_HPP__ */
