/*
 * layered_error_frame.cpp
 *
 * Copyright (c) 2020-2022, DarkMatterCore <pabloacurielz@gmail.com>.
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

    void LayeredErrorFrame::SetParentSidebarItem(brls::SidebarItem *sidebar_item)
    {
        if (sidebar_item) this->sidebar_item = sidebar_item;
    }

    bool LayeredErrorFrame::IsListItemFocused(void)
    {
        brls::View *cur_view = brls::Application::getCurrentFocus();
        size_t cur_list_count = this->list->getViewsCount();

        if (cur_list_count)
        {
            while(cur_view)
            {
                if (cur_view == this->list) return true;
                cur_view = cur_view->getParent();
            }
        }

        return false;
    }

    int LayeredErrorFrame::GetFocusStackViewIndex(void)
    {
        size_t cur_list_count = this->list->getViewsCount();
        std::vector<brls::View*> *focus_stack = brls::Application::getFocusStack();

        if (cur_list_count && focus_stack)
        {
            size_t focus_stack_size = focus_stack->size();

            for(size_t i = 0; i < focus_stack_size; i++)
            {
                for(size_t j = 0; j < cur_list_count; j++)
                {
                    if (this->list->getChild(j) == focus_stack->at(i)) return static_cast<int>(i);
                }
            }
        }

        return -1;
    }

    bool LayeredErrorFrame::UpdateFocusStackViewAtIndex(int index, brls::View *view)
    {
        std::vector<brls::View*> *focus_stack = brls::Application::getFocusStack();
        if (!focus_stack || index < 0) return false;

        size_t focus_stack_size = focus_stack->size();
        if (index >= static_cast<int>(focus_stack_size)) return false;

        focus_stack->at(index) = view;
        brls::Logger::debug("Focus stack updated");

        return true;
    }

    void LayeredErrorFrame::SwitchLayerView(bool use_error_frame, bool update_focused_view, bool update_focus_stack)
    {
        int cur_index = this->getLayerIndex();
        int new_index = (use_error_frame ? 0 : 1);

        size_t cur_list_count = this->list->getViewsCount();
        brls::View *first_child = nullptr;

        int focus_stack_index = this->GetFocusStackViewIndex();
        bool focus_stack_updated = false;

        if (cur_list_count)
        {
            /* Get pointer to the first list item. */
            first_child = this->list->getChild(0);

            /* Update focus stack information, if needed. */
            if (update_focus_stack && focus_stack_index > -1) focus_stack_updated = this->UpdateFocusStackViewAtIndex(focus_stack_index, use_error_frame ? this->sidebar_item : first_child);
        }

        if (!focus_stack_updated)
        {
            /* Check if the user is currently focusing a list item. */
            if (!update_focused_view) update_focused_view = this->IsListItemFocused();

            if (update_focused_view)
            {
                /* Update focused view. */
                if (use_error_frame || !cur_list_count)
                {
                    /* Move focus to the sidebar item. */
                    brls::Application::giveFocus(this->sidebar_item);
                } else {
                    /* Move focus to the first list item. */
                    brls::Application::giveFocus(first_child);

                    /* Make sure to call willAppear() on our list to update the scrolling accordingly. */
                    this->list->willAppear(true);
                }
            }
        }

        /* Change layer view only if the new index is different. */
        if (cur_index != new_index)
        {
            this->changeLayer(new_index);
            this->invalidate(true);
        }
    }
}
