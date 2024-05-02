/*
 * gamecard_image_dump_task.hpp
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

#ifndef __GAMECARD_IMAGE_DUMP_TASK_HPP__
#define __GAMECARD_IMAGE_DUMP_TASK_HPP__

#include <optional>
#include <mutex>

#include "data_transfer_task.hpp"

namespace nxdt::tasks
{
    typedef std::optional<std::string> GameCardDumpTaskError;

    /* Generates an image dump out of the inserted gamecard. */
    class GameCardImageDumpTask: public DataTransferTask<GameCardDumpTaskError, std::string, bool, bool, bool, bool, bool>
    {
        private:
            std::mutex task_mtx;
            bool calculate_checksum = false, lookup_checksum = false;
            u32 gc_img_crc = 0, full_gc_img_crc = 0;

        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(GameCardImageDumpTask);
            NON_MOVEABLE(GameCardImageDumpTask);

            /* Runs in the background thread. */
            GameCardDumpTaskError DoInBackground(const std::string& output_path, const bool& prepend_key_area, const bool& keep_certificate, const bool& trim_dump,
                                                 const bool& calculate_checksum, const bool& lookup_checksum) override final;

        public:
            GameCardImageDumpTask() = default;

            /* Returns the CRC32 calculated over the gamecard image. */
            /* Returns zero if checksum calculation wasn't enabled, if the task hasn't finished yet or if the task was cancelled. */
            ALWAYS_INLINE u32 GetImageChecksum(void)
            {
                std::scoped_lock lock(this->task_mtx);
                return ((this->calculate_checksum && this->IsFinished() && !this->IsCancelled()) ? this->gc_img_crc : 0);
            }

            /* Returns the CRC32 calculated over the gamecard image with prepended key area data. */
            /* Returns zero if checksum calculation wasn't enabled, if the task hasn't finished yet or if the task was cancelled. */
            ALWAYS_INLINE u32 GetFullImageChecksum(void)
            {
                std::scoped_lock lock(this->task_mtx);
                return ((this->calculate_checksum && this->IsFinished() && !this->IsCancelled()) ? this->full_gc_img_crc : 0);
            }
    };
}

#endif  /* __GAMECARD_IMAGE_DUMP_TASK_HPP__ */
