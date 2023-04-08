/*
 * download_task.hpp
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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

#include <borealis.hpp>

#include "core/nxdt_utils.h"
#include "async_task.hpp"

namespace nxdt::tasks
{
    /* Used to hold download progress info. */
    typedef struct {
        /// Fields set by DownloadTask::HttpProgressCallback().
        size_t size;            ///< Total download size.
        size_t current;         ///< Number of bytes downloaded thus far.
        int percentage;         ///< Progress percentage.

        /// Fields set by DownloadTask::onProgressUpdate().
        double speed;           ///< Download speed expressed in bytes per second.
        std::string eta;        ///< Formatted ETA string.
    } DownloadTaskProgress;

    /* Custom event type used to push download progress updates. */
    typedef brls::Event<const DownloadTaskProgress&> DownloadProgressEvent;

    /* Used to hold a buffer + size pair with downloaded data. */
    typedef std::pair<char*, size_t> DownloadDataResult;

    /* Class template to asynchronously download data on a background thread. */
    /* Automatically allocates and registers a RepeatingTask on its own, which is started along with the actual task when execute() is called. */
    /* This internal RepeatingTask is guaranteed to work on the UI thread, and it is also automatically unregistered on object destruction. */
    /* Progress updates are pushed through a DownloadProgressEvent. Make sure to register all event listeners before executing the task. */
    template<typename Result, typename... Params>
    class DownloadTask: public AsyncTask<DownloadTaskProgress, Result, Params...>
    {
        public:
            /* Handles task progress updates on the calling thread. */
            class DownloadTaskHandler: public brls::RepeatingTask
            {
                private:
                    bool finished = false;
                    DownloadTask<Result, Params...>* task = nullptr;

                protected:
                    void run(retro_time_t current_time) override final;

                public:
                    DownloadTaskHandler(retro_time_t interval, DownloadTask<Result, Params...>* task);

                    ALWAYS_INLINE bool isFinished(void)
                    {
                        return this->finished;
                    }
            };

        private:
            DownloadProgressEvent progress_event;
            DownloadTaskHandler *task_handler = nullptr;
            std::chrono::time_point<std::chrono::steady_clock> start_time{}, prev_time{};
            size_t prev_current = 0;

        protected:
            /* Make the background function overridable. */
            virtual Result doInBackground(const Params&... params) override = 0;

            /* These functions run on the calling thread. */
            void onCancelled(const Result& result) override final;
            void onPostExecute(const Result& result) override final;
            void onPreExecute(void) override final;
            void onProgressUpdate(const DownloadTaskProgress& progress) override final;

        public:
            DownloadTask(void);
            ~DownloadTask(void);

            /* Runs on the asynchronous task thread. Required by CURL. */
            /* Make sure to pass it to either httpDownloadFile() or httpDownloadData() with 'this' as the user pointer. */
            static int HttpProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);

            /* Returns the last result from loopCallback(). Runs on the calling thread. */
            ALWAYS_INLINE bool isFinished(void)
            {
                return this->task_handler->isFinished();
            }

            ALWAYS_INLINE DownloadProgressEvent::Subscription RegisterListener(DownloadProgressEvent::Callback cb)
            {
                return this->progress_event.subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterListener(DownloadProgressEvent::Subscription subscription)
            {
                this->progress_event.unsubscribe(subscription);
            }
    };

    template<typename Result, typename... Params>
    DownloadTask<Result, Params...>::DownloadTaskHandler::DownloadTaskHandler(retro_time_t interval, DownloadTask<Result, Params...>* task) : brls::RepeatingTask(interval), task(task)
    {
        /* Do nothing. */
    }

    template<typename Result, typename... Params>
    void DownloadTask<Result, Params...>::DownloadTaskHandler::run(retro_time_t current_time)
    {
        brls::RepeatingTask::run(current_time);
        if (this->task && !this->finished) this->finished = this->task->loopCallback();
    }

    template<typename Result, typename... Params>
    DownloadTask<Result, Params...>::DownloadTask(void)
    {
        /* Create task handler. */
        this->task_handler = new DownloadTaskHandler(DOWNLOAD_TASK_INTERVAL, this);
    }

    template<typename Result, typename... Params>
    DownloadTask<Result, Params...>::~DownloadTask(void)
    {
        /* Stop task handler. Borealis' task manager will take care of deleting it. */
        this->task_handler->stop();

        /* Unregister all event listeners. */
        this->progress_event.unsubscribeAll();
    }

    template<typename Result, typename... Params>
    int DownloadTask<Result, Params...>::HttpProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
    {
        (void)ultotal;
        (void)ulnow;

        DownloadTaskProgress progress = {0};
        DownloadTask<Result, Params...>* task = static_cast<DownloadTask<Result, Params...>*>(clientp);

        /* Don't proceed if we're dealing with an invalid task pointer, or if the task has been cancelled. */
        if (!task || task->isCancelled()) return 1;

        /* Fill struct. */
        progress.size = static_cast<size_t>(dltotal);
        progress.current = static_cast<size_t>(dlnow);
        progress.percentage = (progress.size ? static_cast<int>((progress.current * 100) / progress.size) : 0);

        /* Push progress onto the class. */
        task->publishProgress(progress);

        return 0;
    }

    template<typename Result, typename... Params>
    void DownloadTask<Result, Params...>::onCancelled(const Result& result)
    {
        (void)result;

        /* Pause task handler. */
        this->task_handler->pause();

        /* Unset long running process state. */
        utilsSetLongRunningProcessState(false);
    }

    template<typename Result, typename... Params>
    void DownloadTask<Result, Params...>::onPostExecute(const Result& result)
    {
        (void)result;

        /* Fire task handler immediately to get the last result from loopCallback(), then pause it. */
        this->task_handler->fireNow();
        this->task_handler->pause();

        /* Update progress one last time. */
        this->onProgressUpdate(this->getProgress());

        /* Unset long running process state. */
        utilsSetLongRunningProcessState(false);
    }

    template<typename Result, typename... Params>
    void DownloadTask<Result, Params...>::onPreExecute(void)
    {
        /* Set long running process state. */
        utilsSetLongRunningProcessState(true);

        /* Start task handler. */
        this->task_handler->start();

        /* Set start time. */
        this->start_time = this->prev_time = std::chrono::steady_clock::now();
    }

    template<typename Result, typename... Params>
    void DownloadTask<Result, Params...>::onProgressUpdate(const DownloadTaskProgress& progress)
    {
        AsyncTaskStatus status = this->getStatus();

        /* Return immediately if there has been no progress at all, or if it the task has been cancelled. */
        bool proceed = (progress.current > prev_current || (progress.current == prev_current && (!progress.size || progress.current >= progress.size)));
        if (!proceed || this->isCancelled()) return;

        /* Calculate time difference between the last progress update and the current one. */
        /* Return immediately if it's less than 1 second, but only if this isn't the last chunk (or if the task is still running, if we don't know the download size). */
        std::chrono::time_point<std::chrono::steady_clock> cur_time = std::chrono::steady_clock::now();
        std::chrono::duration<double> diff_time = (cur_time - this->prev_time);

        double diff_time_conv = diff_time.count();
        if (diff_time_conv < 1.0 && ((progress.size && progress.current < progress.size) || status == AsyncTaskStatus::RUNNING)) return;

        /* Calculate transferred data size difference between the last progress update and the current one. */
        double diff_current = static_cast<double>(progress.current - prev_current);

        /* Calculate download speed in bytes per second. */
        double speed = (diff_current / diff_time_conv);

        /* Fill struct. */
        DownloadTaskProgress new_progress = progress;
        new_progress.speed = speed;

        if (progress.size)
        {
            /* Calculate remaining data size and ETA if we know the download size. */
            double remaining = static_cast<double>(progress.size - progress.current);
            double eta = (remaining / speed);
            new_progress.eta = fmt::format("{:02.0F}H{:02.0F}M{:02.0F}S", std::fmod(eta, 86400.0) / 3600.0, std::fmod(eta, 3600.0) / 60.0, std::fmod(eta, 60.0));
        } else {
            /* No download size means no ETA calculation, sadly. */
            new_progress.eta = "";
        }

        /* Set download size if we don't know it and if this is the final chunk. */
        if (!new_progress.size && status == AsyncTaskStatus::FINISHED)
        {
            new_progress.size = new_progress.current;
            new_progress.percentage = 100;
        }

        /* Update class variables. */
        this->prev_time = cur_time;
        this->prev_current = progress.current;

        /* Send updated progress to all listeners. */
        this->progress_event.fire(new_progress);
    }

    /* Asynchronous task to download a file using an output path and a URL. */
    class DownloadFileTask: public DownloadTask<bool, std::string, std::string, bool>
    {
        protected:
            /* Runs in the background thread. */
            bool doInBackground(const std::string& path, const std::string& url, const bool& force_https) override final
            {
                /* If the process fails or if it's cancelled, httpDownloadFile() will take care of closing the incomplete output file and delete it. */
                return httpDownloadFile(path.c_str(), url.c_str(), force_https, DownloadFileTask::HttpProgressCallback, this);
            }
    };

    /* Asynchronous task to store downloaded data into a dynamically allocated buffer using a URL. */
    class DownloadDataTask: public DownloadTask<DownloadDataResult, std::string, bool>
    {
        protected:
            DownloadDataResult doInBackground(const std::string& url, const bool& force_https) override final
            {
                char *buf = NULL;
                size_t buf_size = 0;

                /* If the process fails or if it's cancelled, httpDownloadData() will take care of freeing up the allocated memory and return NULL. */
                buf = httpDownloadData(&buf_size, url.c_str(), force_https, DownloadDataTask::HttpProgressCallback, this);

                return std::make_pair(buf, buf_size);
            }
    };
}

#endif  /* __DOWNLOAD_TASK_HPP__ */
