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
    /* Used to hold a buffer + size pair with downloaded data. */
    typedef std::pair<char*, size_t> DownloadDataResult;

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

    /* Asynchronous task used to store downloaded data into a dynamically allocated buffer using a URL. */
    /* The buffer returned by std::pair::first() must be manually freed by the calling function using free(). */
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

#endif  /* __DOWNLOAD_TASK_HPP__ */
