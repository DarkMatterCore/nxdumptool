/*
 * options_tab.cpp
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

#include <options_tab.hpp>
#include <focusable_item.hpp>
#include <title.h>
#include <sstream>

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    OptionsTabUpdateFileDialog::OptionsTabUpdateFileDialog(std::string path, std::string url, bool force_https, std::string success_str) : brls::Dialog(), success_str(success_str)
    {
        /* Set content view. */
        this->update_progress = new DataTransferProgressDisplay();
        this->setContentView(this->update_progress);

        /* Add cancel button. */
        this->addButton("options_tab/update_dialog/cancel"_i18n, [&](brls::View* view) {
            /* Cancel download task. */
            this->download_task.cancel();

            /* Close dialog. */
            this->close();
        });

        /* Disable cancelling with B button. */
        this->setCancelable(false);

        /* Subscribe to the download task. */
        this->download_task.RegisterListener([&](const nxdt::tasks::DataTransferProgress& progress) {
            /* Update progress. */
            this->update_progress->setProgress(progress);

            /* Check if the download task has finished. */
            if (this->download_task.IsFinished())
            {
                /* Stop spinner. */
                this->update_progress->willDisappear();

                /* Update button label. */
                this->setButtonText(0, "options_tab/update_dialog/close"_i18n);

                /* Display notification. */
                brls::Application::notify(this->download_task.get() ? this->success_str : "options_tab/notifications/update_failed"_i18n);
            }
        });

        /* Start download task. */
        this->download_task.execute(path, url, force_https);
    }

    OptionsTabUpdateApplicationFrame::OptionsTabUpdateApplicationFrame(void) : brls::StagedAppletFrame(false)
    {
        /* Set UI properties. */
        this->setTitle("options_tab/update_app/label"_i18n);
        this->setIcon(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));

        /* Add first stage. */
        this->wait_lbl = new brls::Label(brls::LabelStyle::DIALOG, "options_tab/update_app/frame/please_wait"_i18n, false);
        this->wait_lbl->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addStage(this->wait_lbl);

        /* Add second stage. */
        this->changelog_list = new brls::List();
        this->changelog_list->setSpacing(this->changelog_list->getSpacing() / 2);
        this->changelog_list->setMarginBottom(20);
        this->addStage(this->changelog_list);

        /* Add third stage. */
        this->update_progress = new DataTransferProgressDisplay();
        this->addStage(this->update_progress);

        /* Subscribe to the JSON task. */
        this->json_task.RegisterListener([&](const nxdt::tasks::DataTransferProgress& progress) {
            /* Return immediately if the JSON task hasn't finished. */
            if (!this->json_task.IsFinished()) return;

            std::string notification = "";

            /* Retrieve task result. */
            nxdt::tasks::DownloadDataResult json_task_result = this->json_task.get();
            this->json_buf = json_task_result.first;
            this->json_buf_size = json_task_result.second;

            /* Parse downloaded JSON object. */
            if (utilsParseGitHubReleaseJsonData(this->json_buf, this->json_buf_size, &(this->json_data)))
            {
                /* Check if the application can be updated. */
                if (utilsIsApplicationUpdatable(this->json_data.version, this->json_data.commit_hash))
                {
                    /* Display changelog. */
                    this->DisplayChangelog();
                } else {
                    /* Set notification string. */
                    notification = "options_tab/notifications/up_to_date"_i18n;
                }
            } else {
                /* Log downloaded data. */
                LOG_DATA_ERROR(this->json_buf, this->json_buf_size, "Failed to parse GitHub release JSON. Downloaded data:");

                /* Set notification string. */
                notification = "options_tab/notifications/github_json_failed"_i18n;
            }

            /* Pop view (if needed). */
            if (!notification.empty())
            {
                /* Display notification. */
                brls::Application::notify(notification);

                /* Pop view. */
                this->onCancel();
            }
        });

        /* Start JSON task. */
        this->json_task.execute(GITHUB_API_RELEASE_URL, true);
    }

    OptionsTabUpdateApplicationFrame::~OptionsTabUpdateApplicationFrame(void)
    {
        /* Free parsed JSON data. */
        utilsFreeGitHubReleaseJsonData(&(this->json_data));

        /* Free JSON buffer. */
        if (this->json_buf) free(this->json_buf);
    }

    void OptionsTabUpdateApplicationFrame::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        brls::StagedAppletFrame::layout(vg, style, stash);

        if (this->getCurrentStage() == 0)
        {
            /* Center wait label. */
            this->wait_lbl->setBoundaries(this->x + (this->width / 2), this->y, this->width, this->height);
            this->wait_lbl->invalidate();
        }
    }

    bool OptionsTabUpdateApplicationFrame::onCancel(void)
    {
        /* Cancel NRO task. */
        this->nro_task.cancel();

        /* Cancel JSON task. */
        this->json_task.cancel();

        /* Pop view. */
        brls::Application::popView(brls::ViewAnimation::SLIDE_RIGHT);

        return true;
    }

    void OptionsTabUpdateApplicationFrame::DisplayChangelog(void)
    {
        int line = 0;
        std::string item;
        std::stringstream ss(std::string(this->json_data.changelog));

        /* Display version string at the top. */
        FocusableLabel *version_lbl = new FocusableLabel(false, false, brls::LabelStyle::CRASH, std::string(this->json_data.version), true);
        version_lbl->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->changelog_list->addView(version_lbl);

        /* Display release date and commit hash. */
        brls::Label *release_details_lbl = new brls::Label(brls::LabelStyle::DESCRIPTION, i18n::getStr("options_tab/update_app/frame/release_details"_i18n, \
                                                           this->json_data.commit_hash, RootView::GetFormattedDateString(this->json_data.date)), true);
        release_details_lbl->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->changelog_list->addView(release_details_lbl);

        /* Add changelog header. */
        this->changelog_list->addView(new brls::Header("options_tab/update_app/frame/changelog_header"_i18n));

        /* Split changelog string and fill list. */
        while(std::getline(ss, item))
        {
            /* Don't proceed if this is an empty line. */
            /* Make sure to remove any possible carriage returns. */
            size_t item_len = item.length();
            if (!item_len) continue;

            if (item.back() == '\r')
            {
                if (item_len > 1)
                {
                    item.pop_back();
                } else {
                    continue;
                }
            }

            /* Add line to the changelog view. */
            if (!(line % 2))
            {
                this->changelog_list->addView(new FocusableLabel(false, false, brls::LabelStyle::SMALL, item, true));
            } else {
                this->changelog_list->addView(new brls::Label(brls::LabelStyle::SMALL, item, true));
            }

            line++;
        }

        /* Register update action. */
        this->registerAction("options_tab/update_app/frame/update_action"_i18n, brls::Key::PLUS, [this](void) {
            /* Display update progress. */
            this->DisplayUpdateProgress();

            return true;
        });

        /* Go to the next stage. */
        this->nextStage();
    }

    void OptionsTabUpdateApplicationFrame::DisplayUpdateProgress(void)
    {
        /* Unregister update action. */
        this->unregisterAction(brls::Key::PLUS);

        /* Update cancel action label. */
        this->updateActionHint(brls::Key::B, "options_tab/update_dialog/cancel"_i18n);

        /* Subscribe to the NRO task. */
        this->nro_task.RegisterListener([&](const nxdt::tasks::DataTransferProgress& progress) {
            /* Update progress. */
            this->update_progress->setProgress(progress);

            /* Check if the download task has finished. */
            if (this->nro_task.IsFinished())
            {
                /* Get NRO task result and immediately set application updated state if the task succeeded. */
                bool ret = this->nro_task.get();
                if (ret) utilsSetApplicationUpdatedState();

                /* Display notification. */
                brls::Application::notify(ret ? "options_tab/notifications/app_updated"_i18n : "options_tab/notifications/update_failed"_i18n);

                /* Pop view */
                this->onCancel();
            }
        });

        /* Start NRO task. */
        this->nro_task.execute(NRO_TMP_PATH, std::string(this->json_data.download_url), true);

        /* Go to the next stage. */
        this->nextStage();
    }

    OptionsTab::OptionsTab(RootView *root_view) : brls::List(), root_view(root_view)
    {
        /* Set custom spacing. */
        this->setSpacing(this->getSpacing() / 2);
        this->setMarginBottom(20);

        /* Information about actual dump options. */
        brls::Label *dump_options_info = new brls::Label(brls::LabelStyle::DESCRIPTION, "options_tab/dump_options_info"_i18n, true);
        dump_options_info->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addView(dump_options_info);

        /* Overclock. */
        brls::ToggleListItem *overclock = new brls::ToggleListItem("options_tab/overclock/label"_i18n, configGetBoolean("overclock"), \
                                                                   "options_tab/overclock/description"_i18n, "generic/value_enabled"_i18n, \
                                                                   "generic/value_disabled"_i18n);

        overclock->getClickEvent()->subscribe([](brls::View* view) {
            /* Get current value. */
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            bool value = item->getToggleState();

            /* Update configuration. */
            configSetBoolean("overclock", value);

            LOG_MSG_DEBUG("Overclock setting changed by user.");
        });

        this->addView(overclock);

        /* Naming convention. */
        brls::SelectListItem *naming_convention = new brls::SelectListItem("options_tab/naming_convention/label"_i18n, {
                                                                               "options_tab/naming_convention/value_00"_i18n,
                                                                               "options_tab/naming_convention/value_01"_i18n
                                                                           }, static_cast<unsigned>(configGetInteger("naming_convention")),
                                                                           "options_tab/naming_convention/description"_i18n);

        naming_convention->getValueSelectedEvent()->subscribe([](int selected) {
            /* Make sure the current value isn't out of bounds. */
            if (selected < 0 || selected > static_cast<int>(TitleNamingConvention_Count)) return;

            /* Update configuration. */
            configSetInteger("naming_convention", selected);

            LOG_MSG_DEBUG("Naming convention setting changed by user.");
        });

        this->addView(naming_convention);

        /* Unmount UMS devices. */
        /* We will replace its default click event with a new one that will: */
        /*     1. Check if any UMS devices are available before displaying the dropdown and display a notification if there are none. */
        /*     2. Generate the string vector required by the dropdown. */
        /*     3. Initialize the dropdown and pass a custom callback that will take care of unmounting the selected device. */
        this->unmount_ums_device = new brls::SelectListItem("options_tab/unmount_ums_device/label"_i18n, { "dummy" }, 0,
                                                            i18n::getStr("options_tab/unmount_ums_device/description"_i18n, APP_TITLE), false);

        this->unmount_ums_device->getClickEvent()->unsubscribeAll();

        this->unmount_ums_device->getClickEvent()->subscribe([this](brls::View* view) {
            if (this->ums_devices.empty())
            {
                /* Display a notification if we haven't mounted any UMS devices at all. */
                this->DisplayNotification("options_tab/notifications/no_ums_devices"_i18n);
                return;
            }

            /* Generate values vector for the dropdown. */
            std::vector<std::string> values{};
            for(nxdt::tasks::UmsDeviceVectorEntry ums_device_entry : this->ums_devices) values.push_back(ums_device_entry.second);

            /* Display dropdown. */
            brls::Dropdown::open(this->unmount_ums_device->getLabel(), values, [this](int idx) {
                /* Make sure the current value isn't out of bounds. */
                if (idx < 0 || idx >= static_cast<int>(this->ums_devices.size())) return;

                /* Unmount UMS device. */
                if (umsUnmountDevice(this->ums_devices.at(idx).first))
                {
                    this->DisplayNotification("options_tab/notifications/ums_device_unmount_success"_i18n);
                } else {
                    this->DisplayNotification("options_tab/notifications/ums_device_unmount_failure"_i18n);
                }
            });
        });

        /* Update UMS devices vector. */
        this->ums_devices = this->root_view->GetUmsDevices();

        /* Subscribe to the UMS device event. */
        this->ums_task_sub = this->root_view->RegisterUmsTaskListener([this](const nxdt::tasks::UmsDeviceVector& ums_devices) {
            /* Update UMS devices vector. */
            this->ums_devices = this->root_view->GetUmsDevices();

            /* Generate values vector for the dropdown. */
            std::vector<std::string> values{};
            for(nxdt::tasks::UmsDeviceVectorEntry ums_device_entry : this->ums_devices) values.push_back(ums_device_entry.second);

            /* Update SelectListItem values. */
            /* If the dropdown menu is already being displayed, it'll be reloaded or popped from the view stack, depending on whether the provided vector is empty or not. */
            this->unmount_ums_device->updateValues(values);
        });

        this->addView(unmount_ums_device);

        /* Update NSWDB XML. */
        brls::ListItem *update_nswdb_xml = new brls::ListItem("options_tab/update_nswdb_xml/label"_i18n, "options_tab/update_nswdb_xml/description"_i18n);

        update_nswdb_xml->getClickEvent()->subscribe([this](brls::View* view) {
            if (!this->root_view->IsInternetConnectionAvailable())
            {
                /* Display a notification if no Internet connection is available. */
                this->DisplayNotification("options_tab/notifications/no_internet_connection"_i18n);
                return;
            }

            /* Open update dialog. */
            OptionsTabUpdateFileDialog *dialog = new OptionsTabUpdateFileDialog(NSWDB_XML_PATH, NSWDB_XML_URL, false, "options_tab/notifications/nswdb_xml_updated"_i18n);
            dialog->open(false);
        });

        this->addView(update_nswdb_xml);

        /* Update application. */
        brls::ListItem *update_app = new brls::ListItem("options_tab/update_app/label"_i18n, "options_tab/update_app/description"_i18n);

        update_app->getClickEvent()->subscribe([this](brls::View* view) {
            if (envIsNso())
            {
                /* Display a notification if we're running as a NSO. */
                this->DisplayNotification("options_tab/notifications/is_nso"_i18n);
                return;
            } else
            if (!this->root_view->IsInternetConnectionAvailable())
            {
                /* Display a notification if no Internet connection is available. */
                this->DisplayNotification("options_tab/notifications/no_internet_connection"_i18n);
                return;
            } else
            if (utilsGetApplicationUpdatedState())
            {
                /* Display a notification if the application has already been updated. */
                this->DisplayNotification("options_tab/notifications/already_updated"_i18n);
                return;
            }

            /* Display update frame. */
            brls::Application::pushView(new OptionsTabUpdateApplicationFrame(), brls::ViewAnimation::SLIDE_LEFT, false);
        });

        this->addView(update_app);
    }

    OptionsTab::~OptionsTab(void)
    {
        this->root_view->UnregisterUmsTaskListener(this->ums_task_sub);

        this->ums_devices.clear();

        brls::menu_timer_kill(&(this->notification_timer));
    }

    void OptionsTab::DisplayNotification(const std::string& str)
    {
        if (str.empty() || !this->display_notification) return;

        brls::Application::notify(str);
        this->display_notification = false;

        this->notification_timer_ctx.duration = brls::Application::getStyle()->AnimationDuration.notificationTimeout;
        this->notification_timer_ctx.cb = [this](void *userdata) { this->display_notification = true; };
        this->notification_timer_ctx.tick = [](void*){};
        this->notification_timer_ctx.userdata = nullptr;

        brls::menu_timer_start(&(this->notification_timer), &(this->notification_timer_ctx));
    }
}
