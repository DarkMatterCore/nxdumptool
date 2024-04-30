/*
 * gamecard_image_dump_task.cpp
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

#include <tasks/gamecard_image_dump_task.hpp>
#include <utils/scope_guard.hpp>
#include <utils/file_writer.hpp>
#include <core/gamecard.h>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::tasks
{
    GameCardDumpTaskError GameCardImageDumpTask::DoInBackground(const std::string& output_path, const bool& prepend_key_area, const bool& keep_certificate, const bool& trim_dump,
                                                                const bool& calculate_checksum, const int& checksum_lookup_method)
    {
        std::scoped_lock lock(this->task_mtx);

        GameCardKeyArea gc_key_area{};
        GameCardSecurityInformation gc_security_information{};

        u32 gc_key_area_crc = 0;
        size_t gc_img_size = 0;

        nxdt::utils::FileWriter *file = nullptr;
        void *buf = nullptr;

        DataTransferProgress progress{};

        /* Update private variables. */
        this->calculate_checksum = calculate_checksum;
        this->checksum_lookup_method = checksum_lookup_method;

        LOG_MSG_DEBUG("Starting dump with parameters:\n- Output path: \"%s\".\n- Prepend key area: %u.\n- Keep certificate: %u.\n- Trim dump: %u.\n- Calculate checksum: %u.\n- Checksum lookup method: %d.", \
                      output_path.c_str(), prepend_key_area, keep_certificate, trim_dump, calculate_checksum, checksum_lookup_method);

        /* Retrieve gamecard image size. */
        if ((!trim_dump && !gamecardGetTotalSize(&gc_img_size)) || (trim_dump && !gamecardGetTrimmedSize(&gc_img_size)) || !gc_img_size) return "tasks/gamecard/image/get_size_failed"_i18n;

        /* Check if we're supposed to prepend the key area to the gamecard image. */
        if (prepend_key_area)
        {
            /* Update gamecard image size. */
            gc_img_size += sizeof(GameCardKeyArea);

            /* Retrieve the GameCardSecurityInformation area. */
            if (!gamecardGetSecurityInformation(&gc_security_information)) return "tasks/gamecard/image/get_security_info_failed"_i18n;

            /* Copy the GameCardInitialData area from the GameCardSecurityInformation area to our GameCardKeyArea object. */
            memcpy(&(gc_key_area.initial_data), &(gc_security_information.initial_data), sizeof(GameCardInitialData));

            if (calculate_checksum)
            {
                /* Update gamecard image checksum if we're prepending the key area to it. */
                gc_key_area_crc = crc32Calculate(&gc_key_area, sizeof(GameCardKeyArea));
                this->full_gc_img_crc = gc_key_area_crc;
            }
        }

        /* Push progress onto the class. */
        progress.total_size = gc_img_size;
        this->PublishProgress(progress);

        /* Open output file. */
        try {
            file = new nxdt::utils::FileWriter(output_path, gc_img_size);
        } catch(const std::string& msg) {
            return msg;
        }

        ON_SCOPE_EXIT { delete file; };

        if (prepend_key_area)
        {
            /* Write GameCardKeyArea object. */
            if (!file->Write(&gc_key_area, sizeof(GameCardKeyArea))) return "tasks/gamecard/image/write_key_area_failed"_i18n;

            /* Push progress onto the class. */
            progress.xfer_size += sizeof(GameCardKeyArea);
            this->PublishProgress(progress);
        }

        /* Allocate memory buffer for the dump process. */
        buf = usbAllocatePageAlignedBuffer(USB_TRANSFER_BUFFER_SIZE);
        if (!buf) return "generic/mem_alloc_failed"_i18n;

        ON_SCOPE_EXIT { free(buf); };

        /* Dump gamecard image. */
        for(size_t offset = 0, blksize = USB_TRANSFER_BUFFER_SIZE; offset < gc_img_size; offset += blksize)
        {
            /* Don't proceed if the task has been cancelled. */
            if (this->IsCancelled()) return {};

            /* Adjust current block size, if needed. */
            if (blksize > (gc_img_size - offset)) blksize = (gc_img_size - offset);

            /* Read current block. */
            if (!gamecardReadStorage(buf, blksize, offset)) return i18n::getStr("tasks/gamecard/image/io_failed", "generic/read"_i18n, blksize, offset);

            /* Remove certificate, if needed. */
            if (!keep_certificate && offset == 0) memset(static_cast<u8*>(buf) + GAMECARD_CERT_OFFSET, 0xFF, sizeof(FsGameCardCertificate));

            /* Update image checksum. */
            if (calculate_checksum)
            {
                this->gc_img_crc = crc32CalculateWithSeed(this->gc_img_crc, buf, blksize);
                if (prepend_key_area) this->full_gc_img_crc = crc32CalculateWithSeed(this->full_gc_img_crc, buf, blksize);
            }

            /* Write current block. */
            if (!file->Write(buf, blksize)) return i18n::getStr("tasks/gamecard/image/io_failed", "generic/write"_i18n, blksize, offset);

            /* Push progress onto the class. */
            progress.xfer_size += blksize;
            progress.percentage = static_cast<int>((progress.xfer_size * 100) / progress.total_size);
            this->PublishProgress(progress);
        }

        return {};
    }
}
