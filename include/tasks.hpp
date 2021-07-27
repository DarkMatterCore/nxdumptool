/*
 * tasks.hpp
 *
 * Copyright (c) 2020-2021, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __TASKS_HPP__
#define __TASKS_HPP__

#include <borealis.hpp>

#include "core/gamecard.h"
#include "core/title.h"
#include "core/ums.h"
#include "core/usb.h"

#include "async_task.hpp"

namespace nxdt::tasks
{
    /* Used to hold status info data. */
    typedef struct {
        struct tm *timeinfo;
        u32 charge_percentage;
        PsmChargerType charger_type;
        NifmInternetConnectionType connection_type;
        char *ip_addr;
    } StatusInfoData;
    
    /* Used to hold pointers to application metadata entries. */
    typedef std::vector<TitleApplicationMetadata*> TitleApplicationMetadataVector;
    
    /* Used to hold UMS devices. */
    typedef std::vector<UsbHsFsDevice> UmsDeviceVector;
    
    /* Custom event types. */
    typedef brls::Event<const StatusInfoData*> StatusInfoEvent;
    typedef brls::Event<GameCardStatus> GameCardStatusEvent;
    typedef brls::Event<const TitleApplicationMetadataVector*> TitleEvent;
    typedef brls::Event<const UmsDeviceVector*> UmsEvent;
    typedef brls::Event<UsbHostSpeed> UsbHostEvent;
    
    /* Status info task. */
    /* Its event returns a pointer to a StatusInfoData struct. */
    class StatusInfoTask: public brls::RepeatingTask
    {
        private:
            StatusInfoEvent status_info_event;
            StatusInfoData status_info_data = {0};
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            StatusInfoTask(void);
            ~StatusInfoTask(void);
            
            ALWAYS_INLINE StatusInfoEvent::Subscription RegisterListener(StatusInfoEvent::Callback cb)
            {
                return this->status_info_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(StatusInfoEvent::Subscription subscription)
            {
                this->status_info_event.unsubscribe(subscription);
            }
    };
    
    /* Gamecard task. */
    /* Its event returns a GameCardStatus value. */
    class GameCardTask: public brls::RepeatingTask
    {
        private:
            GameCardStatusEvent gc_status_event;
            GameCardStatus cur_gc_status = GameCardStatus_NotInserted;
            GameCardStatus prev_gc_status = GameCardStatus_NotInserted;
            bool first_notification = true;
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            GameCardTask(void);
            ~GameCardTask(void);
            
            ALWAYS_INLINE GameCardStatusEvent::Subscription RegisterListener(GameCardStatusEvent::Callback cb)
            {
                return this->gc_status_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(GameCardStatusEvent::Subscription subscription)
            {
                this->gc_status_event.unsubscribe(subscription);
            }
    };
    
    /* Title task. */
    /* Its event returns a pointer to a TitleApplicationMetadataVector with metadata for user titles (system titles don't change at runtime). */
    class TitleTask: public brls::RepeatingTask
    {
        private:
            TitleEvent title_event;
            
            TitleApplicationMetadataVector system_metadata;
            TitleApplicationMetadataVector user_metadata;
            
            void PopulateApplicationMetadataVector(bool is_system);
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            TitleTask(void);
            ~TitleTask(void);
            
            /* Intentionally left here to let system titles views retrieve metadata. */
            const TitleApplicationMetadataVector* GetApplicationMetadata(bool is_system);
            
            ALWAYS_INLINE TitleEvent::Subscription RegisterListener(TitleEvent::Callback cb)
            {
                return this->title_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(TitleEvent::Subscription subscription)
            {
                this->title_event.unsubscribe(subscription);
            }
    };
    
    /* USB Mass Storage task. */
    /* Its event returns a pointer to a UmsDeviceVector. */
    class UmsTask: public brls::RepeatingTask
    {
        private:
            UmsEvent ums_event;
            
            UmsDeviceVector ums_devices;
            
            void PopulateUmsDeviceVector(void);
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            UmsTask(void);
            ~UmsTask(void);
            
            ALWAYS_INLINE UmsEvent::Subscription RegisterListener(UmsEvent::Callback cb)
            {
                return this->ums_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(UmsEvent::Subscription subscription)
            {
                this->ums_event.unsubscribe(subscription);
            }
    };
    
    /* USB host device connection task. */
    class UsbHostTask: public brls::RepeatingTask
    {
        private:
            UsbHostEvent usb_host_event;
            UsbHostSpeed cur_usb_host_speed = UsbHostSpeed_None;
            UsbHostSpeed prev_usb_host_speed = UsbHostSpeed_None;
        
        protected:
            void run(retro_time_t current_time) override;
        
        public:
            UsbHostTask(void);
            ~UsbHostTask(void);
            
            ALWAYS_INLINE UsbHostEvent::Subscription RegisterListener(UsbHostEvent::Callback cb)
            {
                return this->usb_host_event.subscribe(cb);
            }
            
            ALWAYS_INLINE void UnregisterListener(UsbHostEvent::Subscription subscription)
            {
                this->usb_host_event.unsubscribe(subscription);
            }
    };



    
    
    
    
    
    typedef struct {
        /// Fields set by DownloadTask::HttpProgressCallback().
        size_t size;            ///< Total download size.
        size_t current;         ///< Number of bytes downloaded thus far.
        int percentage;         ///< Progress percentage.
        
        /// Fields set by DownloadTask::onProgressUpdate().
        double speed;           ///< Download speed expressed in KiB/s.
        std::string eta;        ///< Formatted ETA string.
    } DownloadTaskProgress;
    
    typedef brls::Event<const DownloadTaskProgress&> DownloadProgressEvent;
    
    typedef std::pair<char*, size_t> DownloadDataResult;
    
    /* Class template to asynchronously download data on a background thread. */
    /* Automatically allocates and registers a RepeatingTask on its own, which is started along with the actual task when execute() is called. */
    /* This internal RepeatingTask is guaranteed to work on the UI thread, and it is also automatically unregistered on object destruction. */
    /* Progress updates are pushed through a DownloadProgressEvent. Make sure to register all event listeners before executing the task. */
    template<typename Result, typename... Params>
    class DownloadTask: public nxdt::utils::AsyncTask<DownloadTaskProgress, Result, Params...>
    {
        public:
            /* Handles task progress updates on the calling thread. */
            class DownloadTaskHandler: public brls::RepeatingTask
            {
                private:
                    DownloadTask<Result, Params...>* task = nullptr;
                
                protected:
                    void run(retro_time_t current_time) override final
                    {
                        brls::RepeatingTask::run(current_time);
                        if (this->task) this->task->loopCallback();
                    }
                
                public:
                    DownloadTaskHandler(retro_time_t interval, DownloadTask<Result, Params...>* task) : brls::RepeatingTask(interval), task(task) { }
            };
        
        private:
            DownloadProgressEvent progress_event;
            DownloadTaskHandler *task_handler = nullptr;
            std::chrono::time_point<std::chrono::steady_clock> start_time{}, prev_time{};
            size_t prev_current = 0;
        
        protected:
            /* Runs on the calling thread. */
            void onCancelled(const Result& result) override final
            {
                (void)result;
                
                /* Pause task handler. */
                this->task_handler->pause();
            }
            
            /* Runs on the calling thread. */
            void onPostExecute(const Result& result) override final
            {
                (void)result;
                
                /* Pause task handler. */
                this->task_handler->pause();
                
                /* Update progress one last time. */
                this->onProgressUpdate(this->getProgress());
            }
            
            /* Runs on the calling thread. */
            void onPreExecute(void) override final
            {
                /* Start task handler. */
                this->task_handler->start();
                
                /* Set start time. */
                this->start_time = this->prev_time = std::chrono::steady_clock::now();
            }
            
            /* Runs on the calling thread. */
            void onProgressUpdate(const DownloadTaskProgress& progress) override final
            {
                /* Return immediately if there has been no progress at all, or if it the task has been cancelled. */
                bool proceed = (progress.current > prev_current || (progress.current == prev_current && (!progress.size || progress.current >= progress.size)));
                if (!proceed || this->isCancelled()) return;
                
                /* Calculate time difference between the last progress update and the current one. */
                /* Return immediately if it's less than 1 second, but only if this isn't the last chunk (or if the task is still running, if we don't know the download size). */
                std::chrono::time_point<std::chrono::steady_clock> cur_time = std::chrono::steady_clock::now();
                std::chrono::duration<double> diff_time = (cur_time - this->prev_time);
                
                double diff_time_conv = diff_time.count();
                if (diff_time_conv < 1.0 && ((progress.size && progress.current < progress.size) || this->getStatus() == nxdt::utils::AsyncTaskStatus::RUNNING)) return;
                
                /* Calculate transferred data size difference between the last progress update and the current one. */
                double diff_current = static_cast<double>(progress.current - prev_current);
                
                /* Calculate download speed in kibibytes per second (KiB/s). */
                double speed = ((diff_current / diff_time_conv) / 1024.0);
                
                /* Calculate remaining data size in kibibytes (KiB) and ETA if we know the download size. */
                double eta = 0.0;
                
                if (progress.size)
                {
                    double remaining = (static_cast<double>(progress.size - progress.current) / 1024.0);
                    eta = (remaining / speed);
                }
                
                /* Fill struct. */
                DownloadTaskProgress new_progress = progress;
                new_progress.speed = speed;
                new_progress.eta = (progress.size ? fmt::format("{:02}H{:02}M{:02}S", std::fmod(eta, 86400.0) / 3600.0, std::fmod(eta, 3600.0) / 60.0, std::fmod(eta, 60.0)) : "");
                
                /* Update class variables. */
                this->prev_time = cur_time;
                this->prev_current = progress.current;
                
                /* Send updated progress to all subscribers. */
                this->progress_event.fire(new_progress);
            }
        
        public:
            /* Runs on the calling thread. */
            DownloadTask(retro_time_t interval)
            {
                /* Create task handler. */
                this->task_handler = new DownloadTaskHandler(interval, this);
            }
            
            /* Runs on the calling thread. */
            ~DownloadTask(void)
            {
                /* Stop task handler. Borealis' task manager will take care of deleting it. */
                this->task_handler->stop();
                
                /* Unregister all event listeners. */
                this->progress_event.unsubscribeAll();
            }
            
            /* Runs on the asynchronous task thread. Required by CURL. */
            /* Make sure to pass it to either httpDownloadFile() or httpDownloadData() with 'this' as the user pointer. */
            static int HttpProgressCallback(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
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
                progress.percentage = static_cast<int>((progress.current * 100) / progress.size);
                
                /* Push progress onto the class. */
                task->publishProgress(progress);
                
                return 0;
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
    
    /* Asynchronous task to download a file using an output path and a URL. */
    class DownloadFileTask: public DownloadTask<bool, std::string, std::string, bool>
    {
        protected:
            bool doInBackground(const std::string& path, const std::string& url, const bool& force_https) override final
            {
                /* If the process fails or if it's cancelled, httpDownloadFile() will take care of closing the incomplete output file and delete it. */
                return httpDownloadFile(path.c_str(), url.c_str(), force_https, DownloadFileTask::HttpProgressCallback, this);
            }
        
        public:
            DownloadFileTask(retro_time_t interval) : DownloadTask(interval) { }
    };
    
    /* Asynchronous task to store downloaded data into a dynamically allocated buffer using a URL. */
    class DownloadDataTask: public DownloadTask<DownloadDataResult, std::string, bool>
    {
        protected:
            DownloadDataResult doInBackground(const std::string& url, const bool& force_https)
            {
                char *buf = NULL;
                size_t buf_size = 0;
                
                /* If the process fails or if it's cancelled, httpDownloadData() will take care of freeing up the allocated memory and return NULL. */
                buf = httpDownloadData(&buf_size, url.c_str(), force_https, DownloadDataTask::HttpProgressCallback, this);
                
                return std::make_pair(buf, buf_size);
            }
        
        public:
            DownloadDataTask(retro_time_t interval) : DownloadTask(interval) { }
    };
}

#endif  /* __TASKS_HPP__ */
