/*
 * options_tab.hpp
 *
 * Copyright (c) 2020-2026, DarkMatterCore <pabloacurielz@gmail.com>.
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

#ifndef __OPTIONS_TAB_HPP__
#define __OPTIONS_TAB_HPP__

#include "root_view.hpp"
#include "data_transfer_progress_display.hpp"
#include "../tasks/download_file_task.hpp"
#include "../tasks/download_data_task.hpp"

namespace nxdt::views
{
    class OptionsTab: public brls::List
    {
        public:
            typedef enum : u8 {
                NotificationSource_None                = 0,
                NotificationSource_UnmountUms          = 1,
                NotificationSource_UpdateApplication   = 2,
                NotificationSource_ResetSettings       = 3,
                NotificationSource_WipeLocalTitleCache = 4
            } NotificationSource;

            OptionsTab(RootView *root_view);
            ~OptionsTab();

            void DisplayNotification(const std::string& str, NotificationSource new_notification_src);

        private:
            RootView *root_view = nullptr;

            nxdt::tasks::UmsDeviceVector ums_devices{};
            nxdt::tasks::UmsEvent::Subscription ums_task_sub;

            NotificationSource last_notification_src = NotificationSource_None;
            brls::menu_timer_t notification_timer = 0.0f;
            brls::menu_timer_ctx_entry_t notification_timer_ctx{};
    };

    /* Update application frame. */
    class OptionsTabUpdateApplicationFrame: public brls::StagedAppletFrame
    {
        private:
            OptionsTab *options_tab = nullptr;

            nxdt::tasks::DownloadDataTask json_task;
            char *json_buf = nullptr;
            size_t json_buf_size = 0;
            UtilsGitHubReleaseJsonData json_data = {0};

            brls::Label *wait_lbl = nullptr;                        /// First stage.
            brls::List *changelog_list = nullptr;                   /// Second stage.
            DataTransferProgressDisplay *update_progress = nullptr; /// Third stage.

            nxdt::tasks::DownloadFileTask nro_task;

            void DisplayChangelog(void);
            void DisplayUpdateProgress(void);

        protected:
            void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;
            bool onCancel(void) override;

        public:
            OptionsTabUpdateApplicationFrame(OptionsTab *options_tab);
            ~OptionsTabUpdateApplicationFrame();
    };
}

#endif  /* __OPTIONS_TAB_HPP__ */
