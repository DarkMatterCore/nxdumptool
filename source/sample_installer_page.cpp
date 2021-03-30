/*
    Borealis, a Nintendo Switch UI Library
    Copyright (C) 2019  natinusala
    Copyright (C) 2019  Billy Laws
    Copyright (C) 2019  p-sam

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "sample_installer_page.hpp"

#include <math.h>

using namespace brls::i18n::literals;

SampleInstallerPage::SampleInstallerPage(brls::StagedAppletFrame* frame, std::string label)
{
    // Label
    this->button = (new brls::Button(brls::ButtonStyle::BORDERLESS))->setLabel(label)->setImage(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
    this->button->setParent(this);
    this->button->getClickEvent()->subscribe([frame](View* view) {
        if (frame->isLastStage())
            brls::Application::popView();
        else
            frame->nextStage();
    });
    this->label = new brls::Label(brls::LabelStyle::DIALOG, "installer/stage1/text"_i18n, true);
    this->label->setHorizontalAlign(NVG_ALIGN_CENTER);
    this->label->setParent(this);
}

void SampleInstallerPage::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
{
    this->label->frame(ctx);
    this->button->frame(ctx);
}

brls::View* SampleInstallerPage::getDefaultFocus()
{
    return this->button;
}

void SampleInstallerPage::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
{
    this->label->setWidth(roundf((float)this->width * style->CrashFrame.labelWidth));
    this->label->invalidate(true);

    this->label->setBoundaries(
        this->x + this->width / 2 - this->label->getWidth() / 2,
        this->y + (this->height - style->AppletFrame.footerHeight) / 2,
        this->label->getWidth(),
        this->label->getHeight());

    this->button->setBoundaries(
        this->x + this->width / 2 - style->CrashFrame.buttonWidth / 2,
        this->y + this->height / 2 + style->CrashFrame.buttonHeight,
        style->CrashFrame.buttonWidth,
        style->CrashFrame.buttonHeight);
    this->button->invalidate();
}

SampleInstallerPage::~SampleInstallerPage()
{
    delete this->label;
    delete this->button;
}
