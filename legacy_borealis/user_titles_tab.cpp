/*
 * user_titles_tab.cpp
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
#include <user_titles_tab.hpp>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::views
{
    UserTitlesTab::UserTitlesTab(nxdt::tasks::TitleTask *title_task) : LayeredErrorFrame("user_titles_tab/no_titles_available"_i18n), title_task(title_task)
    {
        /* Populate list. */
        this->PopulateList();
        
        /* Subscribe to title event. */
        this->title_task_sub = this->title_task->RegisterListener([this](void) {
            /* Update list. */
            this->PopulateList();
            brls::Application::notify("user_titles_tab/notification"_i18n);
        });
    }
    
    UserTitlesTab::~UserTitlesTab(void)
    {
        /* Unregister task listener. */
        this->title_task->UnregisterListener(this->title_task_sub);
    }
    
    void UserTitlesTab::PopulateList(void)
    {
        bool refocus = false;
        
        this->user_app_metadata = this->title_task->GetApplicationMetadata(false);
        size_t user_app_metadata_count = this->user_app_metadata->size();
        
        if (user_app_metadata_count)
        {
            /* Determine if we need to refocus after updating the list. */
            brls::View *cur_view = brls::Application::getCurrentFocus();
            while(cur_view)
            {
                if (cur_view == this->list)
                {
                    refocus = true;
                    break;
                }
                
                cur_view = cur_view->getParent();
            }
        } else {
            /* If we need to, switch to the error frame *before* cleaning up our list. */
            this->SwitchLayerView(true);
        }
        
        /* Clear list. */
        this->list->clear();
        this->list->invalidate(true);
        
        /* Immediately return if we have no user application metadata. */
        if (!user_app_metadata_count) return;
        
        /* Populate list. */
        for(TitleApplicationMetadata *cur_app_metadata : *(this->user_app_metadata))
        {
            brls::ListItem *list_item = new brls::ListItem(std::string(cur_app_metadata->lang_entry.name), "", std::string(cur_app_metadata->lang_entry.author));
            list_item->setThumbnail(cur_app_metadata->icon, cur_app_metadata->icon_size);
            this->list->addView(list_item);
        }
        
        /* Switch to the list. */
        this->list->invalidate(true);
        this->SwitchLayerView(false);
        
        /* Refocus, if needed. */
        if (refocus)
        {
            brls::Application::giveFocus(this->list->getChild(0));
            this->list->willAppear(true);
        }
    }
}
