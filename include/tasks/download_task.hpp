/*
 * download_task.hpp
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

#ifndef __DOWNLOAD_TASK_HPP__
#define __DOWNLOAD_TASK_HPP__

#include "data_transfer_task.hpp"

namespace nxdt::tasks
{
    /* Class template to asynchronously download data on a background thread. */
    /* Uses both AsyncTask and DataTransferTask class templates. */
    template<typename Result, typename... Params>
    class DownloadTask: public DataTransferTask<Result, Params...>
    {
        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(DownloadTask);
            NON_MOVEABLE(DownloadTask);

        public:
            DownloadTask() = default;

            /* Runs on the asynchronous task thread. Required by cURL. */
            /* Make sure to pass it to either httpDownloadFile() or httpDownloadData() with 'this' as the user pointer. */
            static int HttpProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
            {
                NX_IGNORE_ARG(ultotal);
                NX_IGNORE_ARG(ulnow);

                DataTransferProgress progress{};
                DownloadTask<Result, Params...>* task = static_cast<DownloadTask<Result, Params...>*>(clientp);

                /* Don't proceed if we're dealing with an invalid task pointer, or if the task has been cancelled. */
                if (!task || task->IsCancelled()) return 1;

                /* Fill struct. */
                progress.total_size = static_cast<size_t>(dltotal);
                progress.xfer_size = static_cast<size_t>(dlnow);
                progress.percentage = (progress.total_size ? static_cast<int>((progress.xfer_size * 100) / progress.total_size) : 0);

                /* Push progress onto the class. */
                task->PublishProgress(progress);

                return 0;
            }
    };
}

#endif  /* __DOWNLOAD_TASK_HPP__ */
