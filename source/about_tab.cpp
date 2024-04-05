/*
 * about_tab.cpp
 *
 * Copyright (c) 2020-2023, DarkMatterCore <pabloacurielz@gmail.com>.
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
    AboutTab::AboutTab(void) : brls::List()
    {
        /* Set custom spacing. */
        this->setSpacing(this->getSpacing() / 2);
        this->setMarginBottom(20);

        /* Logo. */
        this->logo = new brls::Image();
        this->logo->setImage(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
        this->logo->setWidth(LOGO_SIZE);
        this->logo->setHeight(LOGO_SIZE);
        this->logo->setScaleType(brls::ImageScaleType::NO_RESIZE);
        this->logo->setOpacity(0.3F);

        /* Description. */
        this->addView(new AboutTabLabel(brls::LabelStyle::CRASH, "about_tab/description"_i18n, true));

        /* Copyright. */
        brls::Label *copyright = new brls::Label(brls::LabelStyle::DESCRIPTION, i18n::getStr("about_tab/copyright"_i18n, APP_AUTHOR, GITHUB_REPOSITORY_URL), true);
        copyright->setHorizontalAlign(NVG_ALIGN_CENTER);
        this->addView(copyright);

        /* Dependencies. */
        this->addView(new brls::Header("about_tab/dependencies/header"_i18n));
        this->addView(new brls::Label(brls::LabelStyle::SMALL, i18n::getStr("about_tab/dependencies/line_00"_i18n, APP_TITLE, BOREALIS_URL), true));
        this->addView(new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr("about_tab/dependencies/line_01"_i18n, LIBUSBHSFS_URL)));
        this->addView(new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr("about_tab/dependencies/line_02"_i18n, FATFS_URL)));
        this->addView(new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr("about_tab/dependencies/line_03"_i18n, LZ4_URL)));
        this->addView(new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr("about_tab/dependencies/line_04"_i18n, JSON_C_URL)));

        /* Acknowledgments. */
        this->addView(new brls::Header("about_tab/acknowledgments/header"_i18n));
        for(int i = 0; i < 7; i++) this->addView(new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr(fmt::format("about_tab/acknowledgments/line_{:02d}", i))));
        for(int i = 7; i < 12; i++) this->addView(new brls::Label(brls::LabelStyle::SMALL, i18n::getStr(fmt::format("about_tab/acknowledgments/line_{:02d}", i)), true));

        /* Additional links and resources. */
        this->addView(new brls::Header("about_tab/links/header"_i18n));
        this->addView(new AboutTabLabel(brls::LabelStyle::SMALL, i18n::getStr("about_tab/links/line_00"_i18n, DISCORD_SERVER_URL)));
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

        this->logo->invalidate();

        brls::ScrollView::layout(vg, style, stash);
    }
}
