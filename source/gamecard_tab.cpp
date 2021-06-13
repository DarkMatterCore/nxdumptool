/*
 * gamecard_tab.cpp
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

#include <nxdt_includes.h>
#include <gamecard_tab.hpp>

using namespace brls::i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    GameCardTab::GameCardTab(nxdt::tasks::GameCardTask *gc_status_task) : brls::LayerView(), gc_status_task(gc_status_task)
    {
        /* Add error frame. */
        this->error_frame = new ErrorFrame("No gamecard inserted.");
        this->addLayerWrapper(this->error_frame);
        
        /* Add list. */
        this->list = new brls::List();
        this->placeholder = new brls::ListItem("Placeholder");
        this->list->addView(this->placeholder);
        this->addLayerWrapper(this->list);
        
        /* Setup gamecard status task. */
        this->gc_status_task_sub = this->gc_status_task->RegisterListener([this](GameCardStatus gc_status) {
            if (gc_status < GameCardStatus_InsertedAndInfoLoaded) this->changeLayerWrapper(this->error_frame);
            
            switch(gc_status)
            {
                case GameCardStatus_NotInserted:
                    this->error_frame->SetMessage("No gamecard inserted.");
                    break;
                case GameCardStatus_Processing:
                    this->error_frame->SetMessage("Processing gamecard, please wait...");
                    break;
                case GameCardStatus_NoGameCardPatchEnabled:
                    this->error_frame->SetMessage("A gamecard has been inserted, but the \"nogc\" patch is enabled.\n" \
                                                  "Nothing at all can be done with the inserted gamecard.\n" \
                                                  "Disabling this patch *will* update the Lotus ASIC firmware if it's outdated.\n" \
                                                  "Consider disabling this patch if you wish to use gamecard dumping features.");
                    break;
                case GameCardStatus_LotusAsicFirmwareUpdateRequired:
                    this->error_frame->SetMessage("A gamecard has been inserted, but a Lotus ASIC firmware update is required.\n" \
                                                  "Update your console using the inserted gamecard and try again.");
                    break;
                case GameCardStatus_InsertedAndInfoNotLoaded:
                    this->error_frame->SetMessage("A gamecard has been inserted, but an unexpected I/O error occurred.\n" \
                                                  "Please check the logfile and report this issue to " APP_AUTHOR ".");
                    break;
                case GameCardStatus_InsertedAndInfoLoaded:
                    this->changeLayerWrapper(this->list);
                    break;
                default:
                    break;
            }
            
            this->gc_status = gc_status;
        });
    }
    
    GameCardTab::~GameCardTab(void)
    {
        /* Unregister gamecard task listener. */
        this->gc_status_task->UnregisterListener(this->gc_status_task_sub);
        
        /* Clear views vector. */
        if (this->views.size()) this->views.clear();
    }
    
    void GameCardTab::addLayerWrapper(brls::View* view)
    {
        this->views.push_back(view);
        this->addLayer(view);
        if (this->view_index == -1) this->view_index = 0;
    }
    
    void GameCardTab::changeLayerWrapper(brls::View* view)
    {
        int index = -1;
        
        for(size_t i = 0; i < this->views.size(); i++)
        {
            if (this->views[i] == view)
            {
                index = (int)i;
                break;
            }
        }
        
        if (index == -1) return;
        
        /* TODO: check all ListItem elements using a loop. */
        if (brls::Application::getCurrentFocus() == this->placeholder) brls::Application::onGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT, false);
        
        this->changeLayer(index);
        this->invalidate(true);
        this->view_index = index;
    }
}
