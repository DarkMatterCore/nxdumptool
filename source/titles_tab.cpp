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
#include <scope_guard.hpp>

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
        
        /* Make sure we got title information. */
        if ((!this->is_system && !user_ret) || (this->is_system && !this->system_title_info)) throw fmt::format("Failed to retrieve title information for {:016X}.", title_id);
        
        /* Add tabs. */
        this->addTab("Red", new brls::Rectangle(nvgRGB(255, 0, 0)));
        this->addTab("Green", new brls::Rectangle(nvgRGB(0, 255, 0)));
        this->addTab("Blue", new brls::Rectangle(nvgRGB(0, 0, 255)));
    }
    
    TitlesTabPopup::~TitlesTabPopup(void)
    {
        /* Free title information. */
        if (!this->is_system)
        {
            titleFreeUserApplicationData(&(this->user_app_data));
        } else {
            titleFreeTitleInfo(&(this->system_title_info));
        }
    }
    
    TitlesTabItem::TitlesTabItem(const TitleApplicationMetadata *app_metadata, bool is_system) : brls::ListItem(std::string(app_metadata->lang_entry.name), "", ""), \
                                                                                                 app_metadata(app_metadata),
                                                                                                 is_system(is_system)
    {
        brls::Style* style = brls::Application::getStyle();
        
        /* Set sublabel. */
        this->subLabel = (!this->is_system ? std::string(app_metadata->lang_entry.author) : fmt::format("{:016X}", this->app_metadata->title_id));
        this->setHeight(style->List.Item.heightWithSubLabel);
        
        /* Set thumbnail (if needed). */
        if (app_metadata->icon && app_metadata->icon_size) this->setThumbnail(app_metadata->icon, app_metadata->icon_size);
    }
    
    TitlesTab::TitlesTab(nxdt::tasks::TitleTask *title_task, bool is_system) : LayeredErrorFrame("titles_tab/no_titles_available"_i18n), title_task(title_task), is_system(is_system)
    {
        /* Populate list. */
        this->PopulateList(this->title_task->GetApplicationMetadata(this->is_system));
        
        /* Subscribe to the title event if this is the user titles tab. */
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
        /* Block inputs while we're doing our thing. */
        brls::Application::blockInputs();
        ON_SCOPE_EXIT { brls::Application::unblockInputs(); };
        
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
        for(TitleApplicationMetadata *cur_app_metadata : *app_metadata)
        {
            /* Create list item. */
            TitlesTabItem *title = new TitlesTabItem(cur_app_metadata, this->is_system);
            
            /* Register click event. */
            title->getClickEvent()->subscribe([](brls::View *view){
                TitlesTabItem *item = static_cast<TitlesTabItem*>(view);
                const TitleApplicationMetadata *app_metadata = item->GetApplicationMetadata();
                bool is_system = item->IsSystemTitle();
                
                /* Create popup. */
                TitlesTabPopup *popup = nullptr;
                
                try {
                    popup = new TitlesTabPopup(app_metadata, is_system);
                } catch(const std::string& msg) {
                    brls::Logger::debug(msg);
                    if (popup) delete popup;
                    return;
                }
                
                /* Display popup. */
                std::string name = std::string(app_metadata->lang_entry.name);
                std::string tid = fmt::format("{:016X}", app_metadata->title_id);
                std::string sub_left = (!is_system ? std::string(app_metadata->lang_entry.author) : tid);
                std::string sub_right = (!is_system ? tid : "");
                
                if (app_metadata->icon && app_metadata->icon_size)
                {
                    brls::PopupFrame::open(name, app_metadata->icon, app_metadata->icon_size, popup, sub_left, sub_right);
                } else {
                    brls::PopupFrame::open(name, popup, sub_left, sub_right);
                }
            });
            
            /* Add list item to our view. */
            this->list->addView(title);
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
