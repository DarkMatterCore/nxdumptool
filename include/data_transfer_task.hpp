/*
 * data_transfer_task.hpp
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

#ifndef __DATA_TRANSFER_TASK_HPP__
#define __DATA_TRANSFER_TASK_HPP__

#include <borealis.hpp>

#include "core/nxdt_utils.h"
#include "async_task.hpp"

namespace nxdt::tasks
{
    /* Used to hold data transfer progress info. */
    typedef struct {
        size_t total_size;  ///< Total size for the data transfer process.
        size_t xfer_size;   ///< Number of bytes transferred thus far.
        int percentage;     ///< Progress percentage.
        double speed;       ///< Current speed expressed in bytes per second.
        std::string eta;    ///< Formatted ETA string.
    } DataTransferProgress;

    /* Custom event type used to push data transfer progress updates. */
    typedef brls::Event<const DataTransferProgress&> DataTransferProgressEvent;

    /* Class template to asynchronously transfer data on a background thread. */
    /* Automatically allocates and registers a RepeatingTask on its own, which is started along with the actual task when AsyncTask::execute() is called. */
    /* This internal RepeatingTask is guaranteed to work on the UI thread, and it is also automatically unregistered on object destruction. */
    /* Progress updates are pushed through a DataTransferProgressEvent. Make sure to register all event listeners before executing the task. */
    template<typename Result, typename... Params>
    class DataTransferTask: public AsyncTask<DataTransferProgress, Result, Params...>
    {
        private:
            /* Handles task progress updates on the calling thread. */
            class Handler: public brls::RepeatingTask
            {
                private:
                    bool finished = false;
                    DataTransferTask<Result, Params...>* task = nullptr;

                protected:
                    void run(retro_time_t current_time) override final
                    {
                        brls::RepeatingTask::run(current_time);
                        if (this->task && !this->finished) this->finished = this->task->loopCallback();
                    }

                public:
                    Handler(retro_time_t interval, DataTransferTask<Result, Params...>* task) : brls::RepeatingTask(interval), task(task) { }

                    ALWAYS_INLINE bool IsFinished(void)
                    {
                        return this->finished;
                    }
            };

            typedef std::chrono::time_point<std::chrono::steady_clock> SteadyTimePoint;
            static constexpr auto &CurrentSteadyTimePoint = std::chrono::steady_clock::now;

            DataTransferProgressEvent progress_event{};
            Handler *task_handler = nullptr;

            SteadyTimePoint start_time{}, prev_time{}, end_time{};
            size_t prev_xfer_size = 0;

        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(DataTransferTask);
            NON_MOVEABLE(DataTransferTask);

            /* Make the background function overridable. */
            virtual Result doInBackground(const Params&... params) override = 0;

            /* Runs on the calling thread. */
            void onCancelled(const Result& result) override final
            {
                NX_IGNORE_ARG(result);

                /* Set end time. */
                this->end_time = CurrentSteadyTimePoint();

                /* Pause task handler. */
                this->task_handler->pause();

                /* Unset long running process state. */
                utilsSetLongRunningProcessState(false);
            }

            /* Runs on the calling thread. */
            void onPostExecute(const Result& result) override final
            {
                NX_IGNORE_ARG(result);

                /* Set end time. */
                this->end_time = CurrentSteadyTimePoint();

                /* Fire task handler immediately to get the last result from AsyncTask::loopCallback(), then pause it. */
                this->task_handler->fireNow();
                this->task_handler->pause();

                /* Update progress one last time. */
                this->onProgressUpdate(this->getProgress());

                /* Unset long running process state. */
                utilsSetLongRunningProcessState(false);
            }

            /* Runs on the calling thread. */
            void onPreExecute(void) override final
            {
                /* Set long running process state. */
                utilsSetLongRunningProcessState(true);

                /* Start task handler. */
                this->task_handler->start();

                /* Set start time. */
                this->start_time = this->prev_time = CurrentSteadyTimePoint();
            }

            /* Runs on the calling thread. */
            void onProgressUpdate(const DataTransferProgress& progress) override final
            {
                AsyncTaskStatus status = this->getStatus();

                /* Return immediately if there has been no progress at all, or if it the task has been cancelled. */
                bool proceed = (progress.xfer_size > prev_xfer_size || (progress.xfer_size == prev_xfer_size && (!progress.total_size || progress.xfer_size >= progress.total_size)));
                if (!proceed || this->isCancelled()) return;

                /* Calculate time difference between the last progress update and the current one. */
                /* Return immediately if it's less than 1 second, but only if this isn't the last chunk; or if we don't know the total size and the task is still running . */
                SteadyTimePoint cur_time = std::chrono::steady_clock::now();
                double diff_time = std::chrono::duration<double>(cur_time - this->prev_time).count();
                if (diff_time < 1.0 && ((progress.total_size && progress.xfer_size < progress.total_size) || status == AsyncTaskStatus::RUNNING)) return;

                /* Calculate transferred data size difference between the last progress update and the current one. */
                double diff_xfer_size = static_cast<double>(progress.xfer_size - prev_xfer_size);

                /* Calculate transfer speed in bytes per second. */
                double speed = (diff_xfer_size / diff_time);

                /* Fill struct. */
                DataTransferProgress new_progress = progress;
                new_progress.speed = speed;

                if (progress.total_size)
                {
                    /* Calculate remaining data size and ETA if we know the total size. */
                    double remaining = static_cast<double>(progress.total_size - progress.xfer_size);
                    double eta = (remaining / speed);
                    new_progress.eta = fmt::format("{:02.0F}H{:02.0F}M{:02.0F}S", std::fmod(eta, 86400.0) / 3600.0, std::fmod(eta, 3600.0) / 60.0, std::fmod(eta, 60.0));
                } else {
                    /* No total size means no ETA calculation, sadly. */
                    new_progress.eta = "";
                }

                /* Set total size if we don't know it and if this is the final chunk. */
                if (!new_progress.total_size && status == AsyncTaskStatus::FINISHED)
                {
                    new_progress.total_size = new_progress.xfer_size;
                    new_progress.percentage = 100;
                }

                /* Update class variables. */
                this->prev_time = cur_time;
                this->prev_xfer_size = progress.xfer_size;

                /* Send updated progress to all listeners. */
                this->progress_event.fire(new_progress);
            }

        public:
            DataTransferTask(void)
            {
                /* Create task handler. */
                this->task_handler = new Handler(DATA_TRANSFER_TASK_INTERVAL, this);
            }

            ~DataTransferTask(void)
            {
                /* Stop task handler. Borealis' task manager will take care of deleting it. */
                this->task_handler->stop();

                /* Unregister all event listeners. */
                this->progress_event.unsubscribeAll();
            }

            /* Returns the last result from AsyncTask::loopCallback(). Runs on the calling thread. */
            ALWAYS_INLINE bool IsFinished(void)
            {
                return this->task_handler->IsFinished();
            }

            /* Returns the task duration expressed in seconds. */
            /* If the task hasn't finished yet, it returns the number of seconds that have passed since the task was started. */
            ALWAYS_INLINE double GetDuration(void)
            {
                return std::chrono::duration<double>(this->IsFinished() ? (this->end_time - this->start_time) : (CurrentSteadyTimePoint() - this->start_time)).count();
            }

            ALWAYS_INLINE DataTransferProgressEvent::Subscription RegisterListener(DataTransferProgressEvent::Callback cb)
            {
                return this->progress_event.subscribe(cb);
            }

            ALWAYS_INLINE void UnregisterListener(DataTransferProgressEvent::Subscription subscription)
            {
                this->progress_event.unsubscribe(subscription);
            }
    };
}

#endif  /* __DATA_TRANSFER_TASK_HPP__ */
