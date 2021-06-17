/*
 * about_tab.cpp
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

#include <nxdt_includes.h>
#include <about_tab.hpp>

#define LOGO_SIZE   256

namespace i18n = brls::i18n;    /* For getStr(). */
using namespace i18n::literals; /* For _i18n. */

namespace nxdt::views
{
    AboutTabLabel::AboutTabLabel(brls::LabelStyle labelStyle, std::string text, bool center, bool multiline) : brls::Label(labelStyle, text, multiline)
    {
        if (center) this->setHorizontalAlign(NVG_ALIGN_CENTER);
    }
    
    brls::View* AboutTabLabel::getDefaultFocus(void)
    {
        return this;
    }
    
    void AboutTabLabel::onFocusGained(void)
    {
        this->focused = true;
        this->focusEvent.fire(this);
        if (this->hasParent()) this->getParent()->onChildFocusGained(this);
    }
    
    AboutTab::AboutTab(void) : brls::List()
    {
        this->setSpacing(this->getSpacing() / 2);
        this->setMarginBottom(20);
        
        /* Logo. */
        this->logo = new brls::Image(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
        this->logo->setWidth(LOGO_SIZE);
        this->logo->setHeight(LOGO_SIZE);
        this->logo->setScaleType(brls::ImageScaleType::NO_RESIZE);
        this->logo->setOpacity(0.3F);
        
        /* Description. */
        AboutTabLabel* description = new AboutTabLabel(brls::LabelStyle::CRASH, "about_tab/description"_i18n, true);
        this->addView(description);
        
        /* Copyright. */
        brls::Label* copyright = new brls::Label(brls::LabelStyle::DESCRIPTION, i18n::getStr("about_tab/copyright"_i18n, APP_AUTHOR, GITHUB_REPOSITORY_URL), true);
        copyright->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addView(copyright);
        
        /* Dependencies. */
        this->addView(new brls::Header("about_tab/dependencies/header"_i18n));
        brls::Label* dependencies = new brls::Label(brls::LabelStyle::SMALL, i18n::getStr("about_tab/dependencies/value"_i18n, APP_TITLE, BOREALIS_URL, LIBUSBHSFS_URL, FATFS_URL, LZ4_URL), true);
        this->addView(dependencies);
        
        /* Acknowledgments. */
        this->addView(new brls::Header("about_tab/acknowledgments/header"_i18n));
        AboutTabLabel* acknowledgments = new AboutTabLabel(brls::LabelStyle::SMALL, "about_tab/acknowledgments/value"_i18n);
        this->addView(acknowledgments);
        
        /* Additional links and resources. */
        this->addView(new brls::Header("about_tab/links/header"_i18n));
        AboutTabLabel* links = new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr("about_tab/links/value"_i18n, DISCORD_SERVER_URL));
        this->addView(links);
    }
    
    AboutTab::~AboutTab(void)
    {
        delete this->logo;
    }
    
    void AboutTab::draw(NVGcontext* vg, int x, int y, unsigned width, unsigned height, brls::Style* style, brls::FrameContext* ctx)
    {
        this->logo->frame(ctx);
        brls::ScrollView::draw(vg, x, y, width, height, style, ctx);
    }
    
    void AboutTab::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        this->logo->setBoundaries(
            this->x + (this->width - this->logo->getWidth()) / 2,
            this->y + (this->height - this->logo->getHeight()) / 2,
            this->logo->getWidth(),
            this->logo->getHeight());
        
        this->logo->invalidate(true);
        
        brls::ScrollView::layout(vg, style, stash);
    }
}
