/*
 * layered_error_frame.cpp
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

#include <layered_error_frame.hpp>

namespace nxdt::views
{
    LayeredErrorFrame::LayeredErrorFrame(void) : brls::LayerView()
    {
        /* Error frame. */
        this->error_frame = new ErrorFrame();
        this->addLayer(this->error_frame);
        
        /* List. */
        this->list = new brls::List();
        this->list->setSpacing(this->list->getSpacing() / 2);
        this->list->setMarginBottom(20);
        this->addLayer(this->list);
    }
    
    LayeredErrorFrame::~LayeredErrorFrame(void)
    {
        /* Clear list views vector. */
        if (this->list_views.size()) this->list_views.clear();
    }
    
    void LayeredErrorFrame::SwitchLayerView(bool use_error_frame)
    {
        if ((use_error_frame && this->layer_view_index == 0) || (!use_error_frame && this->layer_view_index == 1)) return;
        
        int index = (this->layer_view_index ^ 1);
        brls::View *current_focus = brls::Application::getCurrentFocus();
        
        /* Focus the sidebar if we're currently focusing an element from our List. */
        for(brls::View* list_view : this->list_views)
        {
            if (current_focus == list_view)
            {
                brls::Application::onGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT, false);
                break;
            }
        }
        
        /* Change layer view. */
        this->changeLayer(index);
        this->invalidate(true);
        this->layer_view_index = index;
    }
    
    void LayeredErrorFrame::SetErrorFrameMessage(std::string msg)
    {
        this->error_frame->SetMessage(msg);
    }
    
    void LayeredErrorFrame::AddListView(brls::View* view)
    {
        this->list->addView(view);
        this->list_views.push_back(view);
    }
}
