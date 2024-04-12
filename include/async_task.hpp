/*
 * async_task.hpp
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * Based on attcs' C++ implementation at:
 * https://github.com/attcs/AsyncTask/blob/master/asynctask.h.
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

#ifndef __ASYNC_TASK_HPP__
#define __ASYNC_TASK_HPP__

#include <exception>
#include <future>
#include <mutex>

namespace nxdt::tasks
{
    /* Used by AsyncTask to throw exceptions whenever required. */
    class AsyncTaskException : std::exception
    {
        public:
            enum class eEx : int
            {
                TaskIsAlreadyRunning,   ///< Task is already running.
                TaskIsAlreadyFinished,  ///< Task is already finished.
                TaskIsPending,          ///< Task hasn't been executed.
                TaskIsCancelled,        ///< Task has been cancelled.
                TaskWaitTimeout         ///< Timed out while waiting for the task to finish.
            };

            eEx e;

            AsyncTaskException() = default;
            AsyncTaskException(eEx e) : e(e) { }
    };

    /* Used by AsyncTask to indicate the current status of the asynchronous task. */
    enum class AsyncTaskStatus : int
    {
        PENDING,    ///< The task hasn't been executed yet.
        RUNNING,    ///< The task is currently running.
        FINISHED    ///< The task is finished.
    };

    /* Asynchronous task handler class. */
    template<typename Progress, typename Result, typename... Params>
    class AsyncTask
    {
        private:
            std::recursive_mutex m_mtx{};
            AsyncTaskStatus m_status = AsyncTaskStatus::PENDING;
            Result m_result{};
            std::future<Result> m_future{};
            Progress m_progress{};
            bool m_cancelled = false, m_rethrowException = false;
            std::exception_ptr m_exceptionPtr{};

            /* Runs on the calling thread after doInBackground() finishes execution. */
            void finish(Result&& result)
            {
                std::lock_guard<std::recursive_mutex> lock(this->m_mtx);

                /* Copy result. */
                this->m_result = result;

                /* Update status. */
                this->m_status = AsyncTaskStatus::FINISHED;

                /* Call appropiate post-execution function. */
                if (this->isCancelled())
                {
                    this->onCancelled(this->m_result);
                } else {
                    this->onPostExecute(this->m_result);
                }

                /* Rethrow asynchronous task exception (if available). */
                if (this->m_rethrowException && this->m_exceptionPtr) std::rethrow_exception(this->m_exceptionPtr);
            }

        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(AsyncTask);
            NON_MOVEABLE(AsyncTask);

            virtual ~AsyncTask(void) noexcept
            {
                /* Return right away if the task isn't running. */
                if (this->getStatus() != AsyncTaskStatus::RUNNING) return;

                /* Cancel task. This won't do anything if it has already been cancelled. */
                this->cancel();

                /* Return right away if the result was already retrieved. */
                if (!this->m_future.valid()) return;

                /* Wait until a result is provided by the task thread. */
                /* Avoid rethrowing any exceptions here - program execution could end if another exception has already been rethrown. */
                m_future.wait();
            }

            /* Asynchronous task function. */
            /* This function should periodically call isCancelled() to determine if it should end prematurely. */
            virtual Result doInBackground(const Params&... params) = 0;

            /* Posts asynchronous task result. Runs on the asynchronous task thread. */
            virtual Result postResult(Result&& result)
            {
                return std::move(result);
            }

            /* Cleanup function called if the task is cancelled. Runs on the calling thread. */
            virtual void onCancelled(const Result& result) { }

            /* Post-execution function called right after the task finishes. Runs on the calling thread. */
            virtual void onPostExecute(const Result& result) { }

            /* Pre-execution function called right before the task starts. Runs on the calling thread. */
            virtual void onPreExecute(void) { }

            /* Progress update function. Runs on the calling thread. */
            virtual void onProgressUpdate(const Progress& progress) { }

            /* Stores the current progress inside the class. Runs on the asynchronous task thread. */
            virtual void publishProgress(const Progress& progress)
            {
                std::lock_guard<std::recursive_mutex> lock(this->m_mtx);

                /* Don't proceed if the task isn't running. */
                if (this->getStatus() != AsyncTaskStatus::RUNNING || this->isCancelled()) return;

                /* Update progress. */
                this->m_progress = progress;
            }

            /* Returns the current progress. May run on both threads. */
            Progress getProgress(void)
            {
                std::lock_guard<std::recursive_mutex> lock(this->m_mtx);
                return this->m_progress;
            }

        public:
            AsyncTask(void) = default;

            /* Cancels the task. Runs on the calling thread. */
            void cancel(void) noexcept
            {
                std::lock_guard<std::recursive_mutex> lock(this->m_mtx);

                /* Return right away if the task has already completed, or if it has already been cancelled. */
                if (this->getStatus() == AsyncTaskStatus::FINISHED || this->isCancelled()) return;

                /* Update cancel flag. */
                this->m_cancelled = true;
            }

            /* Starts the asynchronous task. Runs on the calling thread. */
            AsyncTask<Progress, Result, Params...>& execute(const Params&... params)
            {
                /* Return right away if the task was cancelled before starting. */
                if (this->isCancelled()) return *this;

                /* Verify task status. */
                switch(this->getStatus())
                {
                    case AsyncTaskStatus::RUNNING:
                        throw AsyncTaskException(AsyncTaskException::eEx::TaskIsAlreadyRunning);
                    case AsyncTaskStatus::FINISHED:
                        throw AsyncTaskException(AsyncTaskException::eEx::TaskIsAlreadyFinished);
                    default:
                        break;
                }

                /* Update task status. */
                this->m_status = AsyncTaskStatus::RUNNING;

                /* Run onPreExecute() callback. */
                this->onPreExecute();

                /* Start asynchronous task on a new thread. */
                this->m_future = std::async(std::launch::async, [this](const Params&... params) -> Result {
                    /* Catch any exceptions thrown by the asynchronous task. */
                    try {
                        return this->postResult(this->doInBackground(params...));
                    } catch(...) {
                        std::lock_guard<std::recursive_mutex> lock(this->m_mtx);
                        this->cancel();
                        this->m_rethrowException = true;
                        this->m_exceptionPtr = std::current_exception();
                    }

                    return {};
                }, params...);

                return *this;
            }

            /* Waits for the asynchronous task to complete, then returns its result. Runs on the calling thread. */
            /* If an exception is thrown by the asynchronous task, it will be rethrown by this function. */
            Result get(void)
            {
                auto status = this->getStatus();

                /* Throw an exception if the asynchronous task hasn't been executed. */
                if (status == AsyncTaskStatus::PENDING) throw AsyncTaskException(AsyncTaskException::eEx::TaskIsPending);

                /* If the task is still running, wait until it finishes. */
                /* get() calls wait() on its own if the result hasn't been retrieved. */
                /* finish() takes care of rethrowing any exceptions thrown by the asynchronous task. */
                if (status == AsyncTaskStatus::RUNNING) this->finish(this->m_future.get());

                /* Throw an exception if the asynchronous task was cancelled. */
                if (this->isCancelled()) throw AsyncTaskException(AsyncTaskException::eEx::TaskIsCancelled);

                /* Return result. */
                return this->m_result;
            }

            /* Waits for at most the given time for the asynchronous task to complete, then returns its result. Runs on the calling thread. */
            /* If an exception is thrown by the asynchronous task, it will be rethrown by this function. */
            template<typename Rep, typename Period>
            Result get(const std::chrono::duration<Rep, Period>& timeout)
            {
                auto status = this->getStatus();

                /* Throw an exception if the asynchronous task hasn't been executed. */
                if (status == AsyncTaskStatus::PENDING) throw AsyncTaskException(AsyncTaskException::eEx::TaskIsPending);

                /* Check if the task is still running. */
                if (status == AsyncTaskStatus::RUNNING)
                {
                    /* Wait for at most the given time for the asynchronous task to complete. */
                    auto thread_status = this->m_future.wait_for(timeout);
                    switch(thread_status)
                    {
                        case std::future_status::timeout:
                            /* Throw an exception if we timed out while waiting for the task to finish. */
                            throw AsyncTaskException(AsyncTaskException::eEx::TaskWaitTimeout);
                        case std::future_status::ready:
                            /* Retrieve the task result. */
                            /* finish() takes care of rethrowing any exceptions thrown by the asynchronous task. */
                            this->finish(this->m_future.get());

                            /* Throw an exception if the asynchronous task was cancelled. */
                            if (this->isCancelled()) throw AsyncTaskException(AsyncTaskException::eEx::TaskIsCancelled);

                            break;
                        default:
                            break;
                    }
                }

                /* Return result. */
                return this->m_result;
            }

            /* Returns the current task status. Runs on both threads. */
            AsyncTaskStatus getStatus(void) noexcept
            {
                return this->m_status;
            }

            /* Returns true if the task was cancelled before it completed normally. May be used on both threads. */
            /* Can be used by the asynchronous task to return prematurely. */
            bool isCancelled(void) noexcept
            {
                std::lock_guard<std::recursive_mutex> lock(this->m_mtx);
                return this->m_cancelled;
            }

            /* Used by the calling thread to refresh the task progress, preferrably inside a loop. Returns true if the task finished. */
            /* If an exception is thrown by the asynchronous task, it will be rethrown by this function. */
            bool loopCallback(void)
            {
                std::lock_guard<std::recursive_mutex> lock(this->m_mtx);

                auto status = this->getStatus();

                /* Return immediately if the task already finished. */
                if (status == AsyncTaskStatus::FINISHED) return true;

                /* Return immediately if the task hasn't started, or if its result was already retrieved. */
                if (status == AsyncTaskStatus::PENDING || !this->m_future.valid()) return false;

                /* Get task thread status without waiting. */
                auto thread_status = this->m_future.wait_for(std::chrono::seconds(0));
                switch(thread_status)
                {
                    case std::future_status::timeout:
                        /* Update progress. */
                        this->onProgressUpdate(this->m_progress);
                        break;
                    case std::future_status::ready:
                        /* Finish task. */
                        this->finish(this->m_future.get());
                        return true;
                    default:
                        break;
                }

                return false;
            }
    };
}

#endif  /* __ASYNC_TASK_HPP__ */
