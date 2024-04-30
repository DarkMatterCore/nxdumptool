/*
 * about_tab.hpp
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

#pragma once

#ifndef __ABOUT_TAB_HPP__
#define __ABOUT_TAB_HPP__

#include <borealis.hpp>

#include "focusable_item.hpp"

namespace nxdt::views
{
    /* Extended class to display a focusable and optionally centered (but unhighlightable) label. */
    class AboutTabLabel: public FocusableLabel
    {
        public:
            AboutTabLabel(brls::LabelStyle labelStyle, std::string text, bool center = false) : FocusableLabel(false, false, labelStyle, text, true)
            {
                if (center) this->setHorizontalAlign(NVG_ALIGN_CENTER);
            }
    };

    class AboutTab: public brls::List
    {
        private:
            brls::Image *logo = nullptr;

        protected:
            void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
            void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;

        public:
            AboutTab();
            ~AboutTab();
    };
}

#endif  /* __ABOUT_TAB_HPP__ */
