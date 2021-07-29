/*
 * options_tab.cpp
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

#include <nxdt_utils.h>
#include <options_tab.hpp>
#include <title.h>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::views
{
    OptionsTabUpdateFileDialogContent::OptionsTabUpdateFileDialogContent(void)
    {
        this->progress_display = new brls::ProgressDisplay();
        this->progress_display->setParent(this);
        
        this->size_label = new brls::Label(brls::LabelStyle::MEDIUM, "", false);
        this->size_label->setVerticalAlign(NVG_ALIGN_BOTTOM);
        this->size_label->setParent(this);
        
        this->speed_eta_label = new brls::Label(brls::LabelStyle::MEDIUM, "", false);
        this->speed_eta_label->setVerticalAlign(NVG_ALIGN_TOP);
        this->speed_eta_label->setParent(this);
    }
    
    OptionsTabUpdateFileDialogContent::~OptionsTabUpdateFileDialogContent(void)
    {
        delete this->progress_display;
        delete this->size_label;
        delete this->speed_eta_label;
    }
    
    void OptionsTabUpdateFileDialogContent::SetProgress(const nxdt::tasks::DownloadTaskProgress& progress)
    {
        /* Update progress percentage. */
        this->progress_display->setProgress(progress.size ? progress.percentage : 0, 100);
        
        /* Update size string. */
        this->size_label->setText(fmt::format("{} / {}", this->GetFormattedSizeString(progress.current), progress.size ? this->GetFormattedSizeString(progress.size) : "?"));
        
        /* Update speed / ETA string. */
        if (progress.eta != "")
        {
            this->speed_eta_label->setText(fmt::format("{}/s - ETA: {}", this->GetFormattedSizeString(progress.speed), progress.eta));
        } else {
            this->speed_eta_label->setText(fmt::format("{}/s", this->GetFormattedSizeString(progress.speed)));
        }
        
        this->invalidate();
    }
    
    void OptionsTabUpdateFileDialogContent::willAppear(bool resetState)
    {
        this->progress_display->willAppear(resetState);
    }
    
    void OptionsTabUpdateFileDialogContent::willDisappear(bool resetState)
    {
        this->progress_display->willDisappear(resetState);
    }
    
    void OptionsTabUpdateFileDialogContent::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
    {
        /* Progress display. */
        this->progress_display->frame(ctx);
        
        /* Size label. */
        this->size_label->frame(ctx);
        
        /* Speed / ETA label. */
        this->speed_eta_label->frame(ctx);
    }
    
    void OptionsTabUpdateFileDialogContent::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        unsigned elem_width = roundf(static_cast<float>(this->width) * 0.90f);
        
        /* Progress display. */
        this->progress_display->setBoundaries(
            this->x + (this->width - elem_width) / 2,
            this->y + (this->height - style->CrashFrame.buttonHeight) / 2,
            elem_width,
            style->CrashFrame.buttonHeight);
        
        this->progress_display->invalidate(true);
        
        /* Size label. */
        this->size_label->setWidth(elem_width);
        this->size_label->invalidate(true);
        
        this->size_label->setBoundaries(
            this->x + (this->width - this->size_label->getWidth()) / 2,
            this->progress_display->getY() - this->progress_display->getHeight() / 8,
            this->size_label->getWidth(),
            this->size_label->getHeight());
        
        /* Speed / ETA label. */
        this->speed_eta_label->setWidth(elem_width);
        this->speed_eta_label->invalidate(true);
        
        this->speed_eta_label->setBoundaries(
            this->x + (this->width - this->speed_eta_label->getWidth()) / 2,
            this->progress_display->getY() + this->progress_display->getHeight() + this->progress_display->getHeight() / 8,
            this->speed_eta_label->getWidth(),
            this->speed_eta_label->getHeight());
    }
    
    std::string OptionsTabUpdateFileDialogContent::GetFormattedSizeString(size_t size)
    {
        char strbuf[0x40] = {0};
        utilsGenerateFormattedSizeString(static_cast<double>(size), strbuf, sizeof(strbuf));
        return std::string(strbuf);
    }
    
    std::string OptionsTabUpdateFileDialogContent::GetFormattedSizeString(double size)
    {
        char strbuf[0x40] = {0};
        utilsGenerateFormattedSizeString(size, strbuf, sizeof(strbuf));
        return std::string(strbuf);
    }
    
    OptionsTabUpdateFileDialog::OptionsTabUpdateFileDialog(std::string path, std::string url, bool force_https, std::string success_str) : brls::Dialog(), success_str(success_str)
    {
        /* Set content view. */
        OptionsTabUpdateFileDialogContent *content = new OptionsTabUpdateFileDialogContent();
        this->setContentView(content);
        
        /* Add cancel button. */
        this->addButton("options_tab/update_dialog/cancel"_i18n, [this](brls::View* view) {
            this->onCancel();
        });
        
        /* Disable cancelling with B button. */
        this->setCancelable(false);
        
        /* Subscribe to the download task. */
        this->download_task.RegisterListener([this, content](const nxdt::tasks::DownloadTaskProgress& progress) {
            /* Update progress. */
            content->SetProgress(progress);
            
            /* Check if the download task has finished. */
            if (this->download_task.isFinished())
            {
                /* Stop spinner. */
                content->willDisappear();
                
                /* Update button label. */
                this->setButtonText(0, "options_tab/update_dialog/close"_i18n);
                
                /* Display notification. */
                brls::Application::notify(this->download_task.get() ? this->success_str : "options_tab/notifications/update_failed"_i18n);
            }
        });
        
        /* Start download task. */
        this->download_task.execute(path, url, force_https);
    }
    
    bool OptionsTabUpdateFileDialog::onCancel(void)
    {
        /* Cancel download task. */
        this->download_task.cancel();
        
        /* Close dialog. */
        this->close();
        
        return true;
    }
    
    OptionsTab::OptionsTab(void) : brls::List()
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
                                                                   "options_tab/overclock/description"_i18n, "options_tab/overclock/value_enabled"_i18n, \
                                                                   "options_tab/overclock/value_disabled"_i18n);
        
        overclock->getClickEvent()->subscribe([](brls::View* view) {
            brls::ToggleListItem *item = static_cast<brls::ToggleListItem*>(view);
            
            /* Get current value. */
            bool value = item->getToggleState();
            
            /* Change hardware clocks based on the current value. */
            utilsOverclockSystem(value);
            
            /* Update configuration. */
            configSetBoolean("overclock", value);
            
            brls::Logger::debug("Overclock setting changed by user.");
        });
        
        this->addView(overclock);
        
        /* Naming convention. */
        brls::SelectListItem *naming_convention = new brls::SelectListItem("options_tab/naming_convention/label"_i18n, {
                                                                               "options_tab/naming_convention/value_00"_i18n,
                                                                               "options_tab/naming_convention/value_01"_i18n
                                                                           }, static_cast<unsigned>(configGetInteger("naming_convention")),
                                                                           "options_tab/naming_convention/description"_i18n);
        
        naming_convention->getValueSelectedEvent()->subscribe([](int selected){
            /* Make sure the current value isn't out of bounds. */
            if (selected < 0 || selected > static_cast<int>(TitleNamingConvention_Count)) return;
            
            /* Update configuration. */
            configSetInteger("naming_convention", selected);
            
            brls::Logger::debug("Naming convention setting changed by user.");
        });
        
        this->addView(naming_convention);
        
        /* Update NSWDB XML. */
        brls::ListItem *update_nswdb_xml = new brls::ListItem("options_tab/update_nswdb_xml/label"_i18n, "options_tab/update_nswdb_xml/description"_i18n);
        
        update_nswdb_xml->getClickEvent()->subscribe([this](brls::View* view) {
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
            if (false)
            {
                /* Display a notification if the application has already been updated. */
                this->DisplayNotification("options_tab/notifications/already_updated"_i18n);
                return;
            }
            
            /*brls::StagedAppletFrame *staged_frame = new brls::StagedAppletFrame();
            staged_frame->setTitle("options_tab/update_app/label"_i18n);
            
            brls::Application::pushView(staged_frame);*/
        });
        
        this->addView(update_app);
    }
    
    OptionsTab::~OptionsTab(void)
    {
        brls::menu_timer_kill(&(this->notification_timer));
    }
    
    void OptionsTab::DisplayNotification(std::string str)
    {
        if (str == "" || !this->display_notification) return;
        
        brls::Application::notify(str);
        this->display_notification = false;
        
        this->notification_timer_ctx.duration = brls::Application::getStyle()->AnimationDuration.notificationTimeout;
        this->notification_timer_ctx.cb = [this](void *userdata) { this->display_notification = true; };
        this->notification_timer_ctx.tick = [](void*){};
        this->notification_timer_ctx.userdata = nullptr;
        
        brls::menu_timer_start(&(this->notification_timer), &(this->notification_timer_ctx));
    }
}
