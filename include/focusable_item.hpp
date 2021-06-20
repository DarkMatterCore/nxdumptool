/*
 * focusable_item.hpp
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

#ifndef __FOCUSABLE_ITEM_HPP__
#define __FOCUSABLE_ITEM_HPP__

#include <borealis.hpp>

namespace nxdt::views
{
    template<typename ViewType>
    class FocusableItem: public ViewType
    {
        private:
            bool highlight_view;
        
        protected:
            brls::View* getDefaultFocus(void) override
            {
                return this;
            }
            
            void onFocusGained(void) override;
        
        public:
            template<typename... Types>
            FocusableItem(bool highlight_view, Types... args) : ViewType(args...)
            {
                this->highlight_view = highlight_view;
            }
    };
    
    template<typename ViewType>
    void FocusableItem<ViewType>::onFocusGained(void)
    {
        if (this->highlight_view)
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
    
    /* Instantiate templates for the focusable item classes we're gonna use. */
    typedef FocusableItem<brls::Label> FocusableLabel;
    typedef FocusableItem<brls::Table> FocusableTable;
}

#endif  /* __FOCUSABLE_ITEM_HPP__ */
