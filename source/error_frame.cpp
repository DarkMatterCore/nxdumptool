/*
 * error_frame.cpp
 *
 * Copyright (c) 2020-2024, DarkMatterCore <pabloacurielz@gmail.com>.
 *
 * Based on crash_frame.cpp from Borealis.
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
#include <error_frame.hpp>

namespace nxdt::views
{
    ErrorFrame::ErrorFrame(const std::string& msg) : brls::View()
    {
        this->label = new brls::Label(brls::LabelStyle::REGULAR, msg, true);
        this->label->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->label->setParent(this);
    }

    ErrorFrame::~ErrorFrame()
    {
        delete this->label;
    }

    void ErrorFrame::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
    {
        nvgSave(vg);

        /* Background. */
        nvgFillColor(vg, brls::Application::getTheme()->backgroundColorRGB);
        nvgBeginPath(vg);
        nvgRect(vg, x, y, width, height);
        nvgFill(vg);

        /* Scale. */
        float scale = (this->alpha + 2.0f) / 3.0f;
        nvgTranslate(vg, (1.0f - scale) * width * 0.5f, (1.0f - scale) * height * 0.5f);
        nvgScale(vg, scale, scale);

        /* Label. */
        this->label->frame(ctx);

        /* [!] box. */
        unsigned boxSize = style->CrashFrame.boxSize;
        nvgStrokeColor(vg, RGB(255, 255, 255));
        nvgStrokeWidth(vg, style->CrashFrame.boxStrokeWidth);
        nvgBeginPath(vg);
        nvgRect(vg, x + (width - boxSize) / 2, y + style->CrashFrame.boxSpacing, boxSize, boxSize);
        nvgStroke(vg);

        nvgFillColor(vg, RGB(255, 255, 255));

        nvgFontSize(vg, (float)style->CrashFrame.boxSize / 1.25f);
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgBeginPath(vg);
        nvgText(vg, x + width / 2, y + style->CrashFrame.boxSpacing + boxSize / 2, "!", nullptr);
        nvgFill(vg);

        /* End scale. */
        nvgResetTransform(vg);
        nvgRestore(vg);
    }

    void ErrorFrame::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        this->label->setWidth(roundf(static_cast<float>(this->width) * 0.90f));
        this->label->invalidate(true);

        this->label->setBoundaries(
            this->x + (this->width - this->label->getWidth()) / 2,
            this->y + (this->height - style->AppletFrame.footerHeight) / 2,
            this->label->getWidth(),
            this->label->getHeight());
    }

    void ErrorFrame::SetMessage(const std::string& msg)
    {
        this->label->setText(msg);
        this->invalidate();
    }
}
