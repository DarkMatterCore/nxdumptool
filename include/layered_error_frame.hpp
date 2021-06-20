/*
 * layered_error_frame.hpp
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

#pragma once

#ifndef __LAYERED_ERROR_FRAME_HPP__
#define __LAYERED_ERROR_FRAME_HPP__

#include "error_frame.hpp"

namespace nxdt::views
{
    /* Extended class to switch between ErrorFrame and List views on demand. */
    class LayeredErrorFrame: public brls::LayerView
    {
        private:
            int layer_view_index = 0;
            
            ErrorFrame *error_frame = nullptr;
            
            brls::List *list = nullptr;
            std::vector<brls::View*> list_views;
        
        protected:
            void SwitchLayerView(bool use_error_frame);
            void SetErrorFrameMessage(std::string msg = "");
            void AddListView(brls::View* view);
        
        public:
            LayeredErrorFrame(void);
            ~LayeredErrorFrame(void);
    };
}

#endif  /* __LAYERED_ERROR_FRAME_HPP__ */
