/*
 * focusable_item.hpp
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

#ifndef __FOCUSABLE_ITEM_HPP__
#define __FOCUSABLE_ITEM_HPP__

#include <borealis.hpp>

namespace nxdt::views
{
    template<typename ViewType>
    class FocusableItem: public ViewType
    {
        private:
            bool highlight, highlight_bg;

        protected:
            brls::View* getDefaultFocus(void) override
            {
                return this;
            }

            bool isHighlightBackgroundEnabled(void) override
            {
                return this->highlight_bg;
            }

            void onFocusGained(void) override
            {
                if (this->highlight)
                {
                    /* Focus and highlight view. */
                    brls::View::onFocusGained();
                } else {
                    /* Focus view without highlighting it. */
                    this->focused = true;
                    this->focusEvent.fire(this);
                    if (this->hasParent()) this->getParent()->onChildFocusGained(this);
                }
            }

        public:
            template<typename... Types>
            FocusableItem(bool highlight, bool highlight_bg, Types... args) : ViewType(args...), highlight(highlight), highlight_bg(highlight_bg) { }
    };

    /* Define templated classes for the focusable items we're gonna use. */

    class FocusableLabel: public FocusableItem<brls::Label>
    {
        public:
            template<typename... Types>
            FocusableLabel(Types... args) : FocusableItem<brls::Label>(false, false, args...) { }
    };

    class FocusableTable: public FocusableItem<brls::Table>
    {
        public:
            template<typename... Types>
            FocusableTable(Types... args) : FocusableItem<brls::Table>(true, false, args...) { }
    };
}

#endif  /* __FOCUSABLE_ITEM_HPP__ */
