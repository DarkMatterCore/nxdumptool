/*
 * data_transfer_progress_display.cpp
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

#include <data_transfer_progress_display.hpp>

namespace nxdt::views
{
    DataTransferProgressDisplay::DataTransferProgressDisplay()
    {
        this->progress_display = new brls::ProgressDisplay();
        this->progress_display->setParent(this);

        this->size_lbl = new brls::Label(brls::LabelStyle::MEDIUM, "", false);
        this->size_lbl->setVerticalAlign(NVG_ALIGN_BOTTOM);
        this->size_lbl->setParent(this);

        this->speed_eta_lbl = new brls::Label(brls::LabelStyle::MEDIUM, "", false);
        this->speed_eta_lbl->setVerticalAlign(NVG_ALIGN_TOP);
        this->speed_eta_lbl->setParent(this);
    }

    DataTransferProgressDisplay::~DataTransferProgressDisplay()
    {
        delete this->progress_display;
        delete this->size_lbl;
        delete this->speed_eta_lbl;
    }

    void DataTransferProgressDisplay::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
    {
        /* Progress display. */
        this->progress_display->frame(ctx);

        /* Size label. */
        this->size_lbl->frame(ctx);

        /* Speed / ETA label. */
        this->speed_eta_lbl->frame(ctx);
    }

    void DataTransferProgressDisplay::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        unsigned elem_width = roundf(static_cast<float>(this->width) * 0.90f);

        /* Progress display. */
        this->progress_display->setBoundaries(
            this->x + (this->width - elem_width) / 2,
            this->y + (this->height - style->CrashFrame.buttonHeight) / 2,
            elem_width,
            style->CrashFrame.buttonHeight);

        this->progress_display->invalidate(true);

        /* Size label. */
        this->size_lbl->setWidth(elem_width);
        this->size_lbl->invalidate(true);

        this->size_lbl->setBoundaries(
            this->x + (this->width - this->size_lbl->getWidth()) / 2,
            this->progress_display->getY() - this->progress_display->getHeight() / 8,
            this->size_lbl->getWidth(),
            this->size_lbl->getHeight());

        /* Speed / ETA label. */
        this->speed_eta_lbl->setWidth(elem_width);
        this->speed_eta_lbl->invalidate(true);

        this->speed_eta_lbl->setBoundaries(
            this->x + (this->width - this->speed_eta_lbl->getWidth()) / 2,
            this->progress_display->getY() + this->progress_display->getHeight() + this->progress_display->getHeight() / 8,
            this->speed_eta_lbl->getWidth(),
            this->speed_eta_lbl->getHeight());
    }

    void DataTransferProgressDisplay::setProgress(const nxdt::tasks::DataTransferProgress& progress)
    {
        /* Update progress percentage. */
        this->progress_display->setProgress(progress.percentage, 100);

        /* Update size string. */
        this->size_lbl->setText(fmt::format("{} / {}", this->GetFormattedSizeString(static_cast<double>(progress.xfer_size)), \
                                                       progress.total_size ? this->GetFormattedSizeString(static_cast<double>(progress.total_size)) : "?"));

        /* Update speed / ETA string. */
        if (!progress.eta.empty())
        {
            this->speed_eta_lbl->setText(fmt::format("{}/s - ETA: {}", this->GetFormattedSizeString(progress.speed), progress.eta));
        } else {
            this->speed_eta_lbl->setText(fmt::format("{}/s", this->GetFormattedSizeString(progress.speed)));
        }

        this->invalidate();
    }

    void DataTransferProgressDisplay::willAppear(bool resetState)
    {
        this->progress_display->willAppear(resetState);
    }

    void DataTransferProgressDisplay::willDisappear(bool resetState)
    {
        this->progress_display->willDisappear(resetState);
    }

    std::string DataTransferProgressDisplay::GetFormattedSizeString(double size)
    {
        char strbuf[0x40] = {0};
        utilsGenerateFormattedSizeString(size, strbuf, sizeof(strbuf));
        return std::string(strbuf);
    }
}
