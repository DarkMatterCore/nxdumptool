/*
 * titles_tab.cpp
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

#include <views/titles_tab.hpp>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::views
{
    TitlesTabPopup::TitlesTabPopup(const TitleApplicationMetadata *app_metadata, bool is_system) : brls::TabFrame(), app_metadata(app_metadata), is_system(is_system)
    {
        u64 title_id = this->app_metadata->title_id;
        bool user_ret = false;

        if (!this->is_system)
        {
            /* Get user application data. */
            user_ret = titleGetUserApplicationData(title_id, &(this->user_app_data));
        } else {
            /* Get system title info. */
            this->system_title_info = titleGetInfoFromStorageByTitleId(NcmStorageId_BuiltInSystem, title_id);
        }

        /* Make sure we got title information. This should never get triggered. */
        if ((!this->is_system && !user_ret) || (this->is_system && !this->system_title_info)) throw fmt::format("Failed to retrieve title information for {:016X}.", title_id);

        /* Add tabs. */
        this->addTab("Red", new brls::Rectangle(nvgRGB(255, 0, 0)));
        this->addTab("Green", new brls::Rectangle(nvgRGB(0, 255, 0)));
        this->addTab("Blue", new brls::Rectangle(nvgRGB(0, 0, 255)));
    }

    TitlesTabPopup::~TitlesTabPopup()
    {
        /* Free title information. */
        if (!this->is_system)
        {
            titleFreeUserApplicationData(&(this->user_app_data));
        } else {
            titleFreeTitleInfo(&(this->system_title_info));
        }
    }

    TitlesTabItem::TitlesTabItem(const TitleApplicationMetadata *app_metadata, bool is_system, bool click_anim) : brls::ListItem(std::string(app_metadata->lang_entry.name), "", ""), \
                                                                                                                  app_metadata(app_metadata), \
                                                                                                                  is_system(is_system), \
                                                                                                                  click_anim(click_anim)
    {
        /* Set sublabel. */
        if (!this->is_system) this->setSubLabel(std::string(app_metadata->lang_entry.author));

        /* Set thumbnail (if needed). */
        if (app_metadata->icon && app_metadata->icon_size) this->setThumbnail(app_metadata->icon, app_metadata->icon_size);

        /* Set value. */
        this->setValue(fmt::format("{:016X}", this->app_metadata->title_id), false, false);
    }

    void TitlesTabItem::playClickAnimation(void)
    {
        if (this->click_anim) brls::View::playClickAnimation();
    }

    TitlesTab::TitlesTab(RootView *root_view, bool is_system) : LayeredErrorFrame("titles_tab/no_titles_available"_i18n), root_view(root_view), is_system(is_system)
    {
        /* Populate list. */
        this->PopulateList(this->root_view->GetApplicationMetadataInfo(this->is_system));

        /* Subscribe to the title event if this is the user titles tab. */
        if (!this->is_system)
        {
            this->title_task_sub = this->root_view->RegisterTitleMetadataTaskListener([this](const nxdt::tasks::TitleApplicationMetadataInfo& app_metadata_info) {
                /* Update list. */
                this->PopulateList(app_metadata_info);
            });
        }
    }

    TitlesTab::~TitlesTab()
    {
        /* Unregister task listener if this is the user titles tab. */
        if (!this->is_system) this->root_view->UnregisterTitleMetadataTaskListener(this->title_task_sub);
    }

    void TitlesTab::PopulateList(const nxdt::tasks::TitleApplicationMetadataInfo& app_metadata_info)
    {
        /* Block user inputs. */
        brls::Application::blockInputs();

        /* Populate variables. */
        bool update_focused_view = this->IsListItemFocused();
        int focus_stack_index = this->GetFocusStackViewIndex();

        /* If needed, switch to the error frame *before* cleaning up our list. */
        if (!app_metadata_info.app_metadata_count) this->SwitchLayerView(true);

        /* Clear list. */
        this->list->clear();
        this->list->invalidate(true);

        /* Return immediately if we have no application metadata. */
        if (!app_metadata_info.app_metadata_count)
        {
            brls::Application::unblockInputs();
            return;
        }

        /* Populate list. */
        for(u32 i = 0; i < app_metadata_info.app_metadata_count; i++)
        {
            /* Create list item. */
            const TitleApplicationMetadata *cur_app_metadata = app_metadata_info.app_metadata[i];
            TitlesTabItem *item = new TitlesTabItem(cur_app_metadata, this->is_system);

            /* Register click event. */
            item->getClickEvent()->subscribe([](brls::View *view) {
                TitlesTabItem *item = static_cast<TitlesTabItem*>(view);
                const TitleApplicationMetadata *item_app_metadata = item->GetApplicationMetadata();
                bool is_system = item->IsSystemTitle();

                /* Create popup. */
                TitlesTabPopup *popup = nullptr;

                try {
                    popup = new TitlesTabPopup(item_app_metadata, is_system);
                } catch(const std::string& msg) {
                    LOG_MSG_DEBUG("%s", msg.c_str());
                    if (popup) delete popup;
                    return;
                }

                /* Display popup. */
                std::string name = std::string(item_app_metadata->lang_entry.name);
                std::string tid = fmt::format("{:016X}", item_app_metadata->title_id);
                std::string sub_left = (!is_system ? std::string(item_app_metadata->lang_entry.author) : tid);
                std::string sub_right = (!is_system ? tid : "");

                if (item_app_metadata->icon && item_app_metadata->icon_size)
                {
                    brls::PopupFrame::open(name, item_app_metadata->icon, item_app_metadata->icon_size, popup, sub_left, sub_right);
                } else {
                    brls::PopupFrame::open(name, popup, sub_left, sub_right);
                }
            });

            /* Add list item to our view. */
            this->list->addView(item);
        }

        /* Update focus stack, if needed. */
        if (focus_stack_index > -1) this->UpdateFocusStackViewAtIndex(focus_stack_index, this->GetListFirstFocusableChild());

        /* Switch to the list. */
        this->list->invalidate(true);
        this->SwitchLayerView(false, update_focused_view, focus_stack_index < 0);

        /* Unblock user inputs. */
        brls::Application::unblockInputs();
    }
}
