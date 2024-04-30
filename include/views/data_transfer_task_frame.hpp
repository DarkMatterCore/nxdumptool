/*
 * data_transfer_task_frame.hpp
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

#ifndef __DATA_TRANSFER_TASK_FRAME_HPP__
#define __DATA_TRANSFER_TASK_FRAME_HPP__

#include "../utils/is_base_of_template.hpp"
#include "error_frame.hpp"
#include "data_transfer_progress_display.hpp"

namespace nxdt::views
{
    template<typename Task>
    class DataTransferTaskFrame: public brls::AppletFrame
    {
        static_assert(nxdt::utils::is_base_of_template_v<nxdt::tasks::DataTransferTask, Task>, "Task must inherit from DataTransferTask");

        protected:
            Task task;

        private:
            DataTransferProgressDisplay *task_progress = nullptr;
            ErrorFrame *error_frame = nullptr;
            bool progress_displayed = false;
            std::string notification{};

            void DisplayProgress(void)
            {
                this->setContentView(this->task_progress);
                this->progress_displayed = true;
            }

            void DisplayError(const std::string& msg)
            {
                this->error_frame->SetMessage(msg);
                this->setContentView(this->error_frame);
                this->progress_displayed = false;
            }

            template<typename... Params>
            void Initialize(const std::string& title, brls::Image *icon, const Params&... params)
            {
                /* Set UI properties. */
                this->setTitle(title);
                this->setIcon(icon);

                /* Update B button label. */
                this->updateActionHint(brls::Key::B, brls::i18n::getStr("generic/cancel"));

                /* Initialize progress display. */
                this->task_progress = new DataTransferProgressDisplay();

                /* Initialize error frame. */
                this->error_frame = new ErrorFrame();

                /* Subscribe to the background task. */
                this->task.RegisterListener([this](const nxdt::tasks::DataTransferProgress& progress) {
                    /* Store notification message and return immediately if the background task was cancelled. */
                    if (this->task.IsCancelled())
                    {
                        this->notification = brls::i18n::getStr("generic/process_cancelled");
                        return;
                    }

                    /* Update progress. */
                    this->task_progress->SetProgress(progress);

                    /* Check if the background task has finished. */
                    if (this->task.IsFinished())
                    {
                        /* Get background task result and error reason. */
                        std::string error_msg{};
                        bool ret = this->GetTaskResult(error_msg);

                        if (ret)
                        {
                            /* Store notification message. */
                            this->notification = brls::i18n::getStr("generic/process_complete");

                            /* Pop view. */
                            this->onCancel();
                        } else {
                            /* Update B button label. */
                            this->updateActionHint(brls::Key::B, brls::i18n::getStr("brls/hints/back"));

                            /* Display error frame. */
                            this->DisplayError(error_msg);
                        }
                    }
                });

                /* Start background task. */
                this->task.Execute(params...);

                /* Set content view. */
                this->DisplayProgress();
            }

        protected:
            /* Set class as non-copyable and non-moveable. */
            NON_COPYABLE(DataTransferTaskFrame);
            NON_MOVEABLE(DataTransferTaskFrame);

            bool onCancel(void) override final
            {
                /* Cancel background task. This will have no effect if the background task already finished or if it was already cancelled. */
                this->task.Cancel();

                /* Pop view. This will invoke this class' destructor. */
                brls::Application::popView(brls::ViewAnimation::SLIDE_RIGHT);

                return true;
            }

            /* Must be implemented by derived classes to determine if the background task succeeded or not by calling GetResult() on their own. */
            /* If the task failed, false shall be returned and `error_msg` shall be updated to reflect the error reason. */
            virtual bool GetTaskResult(std::string& error_msg) = 0;

        public:
            template<typename... Params>
            DataTransferTaskFrame(const std::string& title, const Params&... params) : brls::AppletFrame(true, true)
            {
                /* Generate icon using the default image. */
                brls::Image *icon = new brls::Image();
                icon->setImage(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
                icon->setScaleType(brls::ImageScaleType::SCALE);

                /* Initialize the rest of the elements. */
                this->Initialize(title, icon, params...);
            }

            template<typename... Params>
            DataTransferTaskFrame(const std::string& title, brls::Image *icon, const Params&... params) : brls::AppletFrame(true, true)
            {
                /* Initialize the rest of the elements. */
                this->Initialize(title, icon, params...);
            }

            ~DataTransferTaskFrame()
            {
                /* Delete the view that's not currently being displayed. */
                /* The other one will be taken care of by brls::AppletFrame's destructor. */
                if (this->progress_displayed)
                {
                    delete this->error_frame;
                } else {
                    delete this->task_progress;
                }

                /* Show relevant notification, if needed. */
                /* This is done here to avoid a slowdown issue while attempting to pop the current view and display a notification at the same time. */
                if (!this->notification.empty()) brls::Application::notify(this->notification);
            }
    };
}

#endif  /* __DATA_TRANSFER_TASK_FRAME_HPP__ */
