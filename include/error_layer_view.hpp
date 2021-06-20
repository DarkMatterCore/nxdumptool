/*
 * error_layer_view.hpp
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

#ifndef __ERROR_LAYER_VIEW_HPP__
#define __ERROR_LAYER_VIEW_HPP__

#include "error_frame.hpp"

namespace nxdt::views
{
    /* Extended class to switch between ErrorFrame and List views whenever an event is triggered. */
    template<typename TaskType, typename EventType>
    class ErrorLayerView: public brls::LayerView
    {
        private:
            TaskType *task = nullptr;
            std::vector<typename EventType::Subscription> task_subs;
            
            ErrorFrame *error_frame = nullptr;
            
            brls::List *list = nullptr;
            std::vector<brls::View*> list_views;
            
            int layer_view_index = 0;
        
        protected:
            void SwitchLayerView(bool use_error_frame);
            void SetErrorFrameMessage(std::string msg = "");
            void AddListView(brls::View* view);
            void RegisterListener(typename EventType::Callback cb);
        
        public:
            ErrorLayerView(TaskType *task);
            ~ErrorLayerView(void);
    };
    
    template<typename TaskType, typename EventType>
    ErrorLayerView<TaskType, EventType>::ErrorLayerView(TaskType *task) : brls::LayerView(), task(task)
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
    
    template<typename TaskType, typename EventType>
    ErrorLayerView<TaskType, EventType>::~ErrorLayerView(void)
    {
        /* Unregister task listeners. */
        for(typename EventType::Subscription task_sub : this->task_subs) this->task->UnregisterListener(task_sub);
        
        /* Clear task subscriptions vector. */
        if (this->task_subs.size()) this->task_subs.clear();
        
        /* Clear list views vector. */
        if (this->list_views.size()) this->list_views.clear();
    }
    
    template<typename TaskType, typename EventType>
    void ErrorLayerView<TaskType, EventType>::SwitchLayerView(bool use_error_frame)
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
    
    template<typename TaskType, typename EventType>
    void ErrorLayerView<TaskType, EventType>::SetErrorFrameMessage(std::string msg)
    {
        this->error_frame->SetMessage(msg);
    }
    
    template<typename TaskType, typename EventType>
    void ErrorLayerView<TaskType, EventType>::AddListView(brls::View* view)
    {
        this->list->addView(view);
        this->list_views.push_back(view);
    }
    
    template<typename TaskType, typename EventType>
    void ErrorLayerView<TaskType, EventType>::RegisterListener(typename EventType::Callback cb)
    {
        typename EventType::Subscription task_sub = this->task->RegisterListener(cb);
        this->task_subs.push_back(task_sub);
    }
}

#endif  /* __ERROR_LAYER_VIEW_HPP__ */
