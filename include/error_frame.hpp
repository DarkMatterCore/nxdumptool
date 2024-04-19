/*
 * error_frame.hpp
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * Based on crash_frame.hpp from Borealis.
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

#ifndef __ERROR_FRAME_HPP__
#define __ERROR_FRAME_HPP__

#include <borealis.hpp>

namespace nxdt::views
{
    class ErrorFrame: public brls::View
    {
        private:
            brls::Label *label = nullptr;

        protected:
            void draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx) override;
            void layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash) override;

        public:
            ErrorFrame(std::string msg = "");
            ~ErrorFrame();

            void SetMessage(std::string msg);
    };
}

#endif  /* __ERROR_FRAME_HPP__ */
