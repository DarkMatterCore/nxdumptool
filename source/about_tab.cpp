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
    AboutTabLogo::AboutTabLogo(void) : brls::Image(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"))
    {
        this->setScaleType(brls::ImageScaleType::NO_RESIZE);
    }
    
    void AboutTabLogo::layout(NVGcontext* vg, brls::Style* style, brls::FontStash* stash)
    {
        this->setBoundaries(this->x + this->width / 2 - LOGO_SIZE / 2, this->y, LOGO_SIZE, LOGO_SIZE);
        brls::Image::layout(vg, style, stash);
    }
    
    brls::View* AboutTabLogo::getDefaultFocus(void)
    {
        return this;
    }
    /*
    void AboutTabLogo::onFocusGained(void)
    {
        this->focused = true;
        this->focusEvent.fire(this);
        if (this->hasParent()) this->getParent()->onChildFocusGained(this);
    }
    */
    AboutTabLabel::AboutTabLabel(brls::LabelStyle labelStyle, std::string text, bool center, bool multiline) : brls::Label(labelStyle, text, multiline)
    {
        if (center) this->setHorizontalAlign(NVG_ALIGN_CENTER);
    }
    
    brls::View* AboutTabLabel::getDefaultFocus(void)
    {
        return this;
    }
    /*
    void AboutTabLabel::onFocusGained(void)
    {
        this->focused = true;
        this->focusEvent.fire(this);
        if (this->hasParent()) this->getParent()->onChildFocusGained(this);
    }
    */
    AboutTab::AboutTab(void) : brls::List()
    {
        this->setSpacing(this->getSpacing() / 2);
        this->setMarginBottom(20);
        
        /* Logo. */
        this->addView(new AboutTabLogo());
        
        /* Description. */
        brls::Label* description = new brls::Label(brls::LabelStyle::REGULAR, "about_tab/description"_i18n, true);
        description->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addView(description);
        
        /* Copyright. */
        brls::Label* copyright = new brls::Label(brls::LabelStyle::DESCRIPTION, i18n::getStr("about_tab/copyright"_i18n, APP_AUTHOR), true);
        copyright->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addView(copyright);
        
        /* Links and resources. */
        this->addView(new brls::Header("Links and resources"));
        
        AboutTabLabel* links = new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr("about_tab/links"_i18n, GITHUB_REPOSITORY_URL, APP_TITLE, BOREALIS_URL, LIBUSBHSFS_URL));
        this->addView(links);
        
        
        
        
    }
}
