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
    LayeredErrorFrame::LayeredErrorFrame(std::string msg) : brls::LayerView()
    {
        /* Error frame. */
        this->error_frame = new ErrorFrame(msg);
        this->addLayer(this->error_frame);
        
        /* List. */
        this->list = new brls::List();
        this->addLayer(this->list);
    }
    
    void LayeredErrorFrame::SwitchLayerView(bool use_error_frame)
    {
        int index = this->getLayerIndex();
        int new_index = (index ^ 1);
        brls::View *cur_focus = brls::Application::getCurrentFocus();
        
        /* Don't proceed if we're already at the desired view layer. */
        if (index < 0 || index > 1 || (use_error_frame && index == 0) || (!use_error_frame && index == 1)) return;
        
        /* Focus the sidebar if we're currently focusing an element from our List and we're about to switch to the error frame. */
        if (use_error_frame && cur_focus && cur_focus->hasParent())
        {
            brls::View *cur_focus_parent = cur_focus->getParent();
            if (cur_focus_parent && cur_focus_parent->hasParent() && cur_focus_parent->getParent() == this->list) brls::Application::onGamepadButtonPressed(GLFW_GAMEPAD_BUTTON_DPAD_LEFT, false);
        }
        
        /* Change layer view. */
        this->changeLayer(new_index);
        this->invalidate(true);
    }
}
