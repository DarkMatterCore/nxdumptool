/*
 * titles_tab.cpp
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
#include <titles_tab.hpp>

using namespace brls::i18n::literals;   /* For _i18n. */

namespace nxdt::views
{
    TitlesTabItem::TitlesTabItem(TitleApplicationMetadata *app_metadata, bool is_system) : brls::ListItem(std::string(app_metadata->lang_entry.name), "", ""), \
                                                                                           title_id(app_metadata->title_id),
                                                                                           is_system(is_system)
    {
        brls::Style* style = brls::Application::getStyle();
        
        /* Set sublabel. */
        this->subLabel = (!this->is_system ? std::string(app_metadata->lang_entry.author) : fmt::format("{:016X}", this->title_id));
        this->setHeight(style->List.Item.heightWithSubLabel);
        
        /* Set thumbnail if we're dealing with user metadata. */
        if (!this->is_system && app_metadata->icon && app_metadata->icon_size) this->setThumbnail(app_metadata->icon, app_metadata->icon_size);
    }
    
    TitlesTab::TitlesTab(nxdt::tasks::TitleTask *title_task, bool is_system) : LayeredErrorFrame("titles_tab/no_titles_available"_i18n), title_task(title_task), is_system(is_system)
    {
        /* Populate list. */
        this->PopulateList(this->title_task->GetApplicationMetadata(this->is_system));
        
        /* Subscribe to title event if this is the user titles tab. */
        if (!this->is_system)
        {
            this->title_task_sub = this->title_task->RegisterListener([this](const nxdt::tasks::TitleApplicationMetadataVector* app_metadata) {
                /* Update list. */
                this->PopulateList(app_metadata);
                brls::Application::notify("titles_tab/user_titles_notification"_i18n);
            });
        }
    }
    
    TitlesTab::~TitlesTab(void)
    {
        /* Unregister task listener if this is the user titles tab. */
        if (!this->is_system) this->title_task->UnregisterListener(this->title_task_sub);
    }
    
    void TitlesTab::PopulateList(const nxdt::tasks::TitleApplicationMetadataVector* app_metadata)
    {
        if (!app_metadata) return;
        
        bool refocus = false;
        size_t app_metadata_count = app_metadata->size();
        
        if (app_metadata_count)
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
        if (!app_metadata_count) return;
        
        /* Populate list. */
        for(TitleApplicationMetadata *cur_app_metadata : *app_metadata) this->list->addView(new TitlesTabItem(cur_app_metadata, this->is_system));
        
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
