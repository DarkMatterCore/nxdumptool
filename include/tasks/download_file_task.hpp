/*
 * download_file_task.hpp
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

#ifndef __DOWNLOAD_FILE_TASK_HPP__
#define __DOWNLOAD_FILE_TASK_HPP__

#include "download_task.hpp"

namespace nxdt::tasks
{
    /* Asynchronous task used to download a file using an output path and a URL. */
    class DownloadFileTask: public DownloadTask<bool, std::string, std::string, bool>
    {
        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(DownloadFileTask);
            NON_MOVEABLE(DownloadFileTask);

            /* Runs in the background thread. */
            bool DoInBackground(const std::string& path, const std::string& url, const bool& force_https) override final
            {
                /* If the process fails or if it's cancelled, httpDownloadFile() will take care of closing the incomplete output file and deleting it. */
                return httpDownloadFile(path.c_str(), url.c_str(), force_https, DownloadFileTask::HttpProgressCallback, this);
            }

        public:
            DownloadFileTask() = default;
    };
}

#endif  /* __DOWNLOAD_FILE_TASK_HPP__ */
