/*
 * file_writer.hpp
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

#ifndef __FILE_WRITER_HPP__
#define __FILE_WRITER_HPP__

#include <borealis.hpp>
#include <optional>

#include "core/nxdt_utils.h"
#include "core/usb.h"

namespace nxdt::utils
{
    /* Writes output files to different storage locations based on the provided input path. */
    /* It also handles file splitting in FAT-based UMS volumes. */
    class FileWriter
    {
        public:
            /* Determines the output storage type used by this file. */
            typedef enum : u8 {
                None      = 0,
                SdCard    = 1,
                UsbHost   = 2,
                UmsDevice = 3
            } StorageType;

        private:
            std::string output_path{};
            size_t total_size = 0, cur_size = 0;

            u32 nsp_header_size = 0;
            bool nsp_header_written = false;

            StorageType storage_type = StorageType::None;

            bool split_file = false, file_created = false, file_closed = false;

            FILE *fp = nullptr;
            u8 split_file_part_cnt = 0, split_file_part_idx = 0;
            size_t split_file_part_size = 0;

            std::optional<std::string> CheckFreeSpace(void);

            void CloseCurrentFile(void);

            bool OpenNextFile(void);

            bool CreateInitialFile(void);

        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(FileWriter);
            NON_MOVEABLE(FileWriter);

        public:
            FileWriter(const std::string& output_path, const size_t& total_size, const u32& nsp_header_size = 0);
            ~FileWriter();

            /* Writes data to the output file. */
            /* Takes care of seamlessly switching to a new part file if needed. */
            bool Write(const void *data, const size_t& data_size);

            /* Writes NSP header data to offset 0. */
            /* Only valid if dealing with a NSP file. */
            bool WriteNspHeader(const void *nsp_header, const u32& nsp_header_size);

            /* Closes the file and deletes it if it's incomplete (or if forcefully requested). */
            void Close(bool force_delete = false);

            /* Returns the storage type for this file. */
            StorageType GetStorageType(void);
    };
}

#endif  /* __FILE_WRITER_HPP__ */
