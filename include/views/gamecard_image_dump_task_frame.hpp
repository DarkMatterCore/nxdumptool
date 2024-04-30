/*
 * gamecard_image_dump_task_frame.hpp
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

#ifndef __GAMECARD_IMAGE_DUMP_TASK_FRAME_HPP__
#define __GAMECARD_IMAGE_DUMP_TASK_FRAME_HPP__

#include "data_transfer_task_frame.hpp"
#include "../tasks/gamecard_image_dump_task.hpp"

namespace nxdt::views
{
    class GameCardImageDumpTaskFrame: public DataTransferTaskFrame<nxdt::tasks::GameCardImageDumpTask>
    {
        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(GameCardImageDumpTaskFrame);
            NON_MOVEABLE(GameCardImageDumpTaskFrame);

            bool GetTaskResult(std::string& error_msg) override final
            {
                auto res = this->task.GetResult();
                if (res.has_value())
                {
                    error_msg = res.value();
                    return false;
                }

                return true;
            }

        public:
            template<typename... Params>
            GameCardImageDumpTaskFrame(Params... params) :
                DataTransferTaskFrame<nxdt::tasks::GameCardImageDumpTask>(brls::i18n::getStr("gamecard_tab/list/dump_card_image/label"), params...) { }
    };
}

#endif  /* __GAMECARD_IMAGE_DUMP_TASK_FRAME_HPP__ */
