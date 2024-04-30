/*
 * download_data_task.hpp
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

#ifndef __DOWNLOAD_DATA_TASK_HPP__
#define __DOWNLOAD_DATA_TASK_HPP__

#include "download_task.hpp"

namespace nxdt::tasks
{
    /* Used to hold a buffer + size pair with downloaded data. */
    typedef std::pair<char*, size_t> DownloadDataResult;

    /* Asynchronous task used to store downloaded data into a dynamically allocated buffer using a URL. */
    /* The buffer returned by std::pair::first() must be manually freed by the caller using free(). */
    class DownloadDataTask: public DownloadTask<DownloadDataResult, std::string, bool>
    {
        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(DownloadDataTask);
            NON_MOVEABLE(DownloadDataTask);

            /* Runs in the background thread. */
            DownloadDataResult DoInBackground(const std::string& url, const bool& force_https) override final
            {
                char *buf = nullptr;
                size_t buf_size = 0;

                /* If the process fails or if it's cancelled, httpDownloadData() will take care of freeing up the allocated memory and returning NULL. */
                buf = httpDownloadData(&buf_size, url.c_str(), force_https, DownloadDataTask::HttpProgressCallback, this);

                return std::make_pair(buf, buf_size);
            }

        public:
            DownloadDataTask() = default;
    };
}

#endif  /* __DOWNLOAD_DATA_TASK_HPP__ */
