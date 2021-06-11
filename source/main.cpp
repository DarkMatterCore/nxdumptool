/*
    Borealis, a Nintendo Switch UI Library
    Copyright (C) 2019-2020  natinusala
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

#include <stdio.h>
#include <stdlib.h>

#include <string>

#include <nxdt_utils.h>
#include <tasks.hpp>
#include <scope_guard.hpp>

#include "custom_layout_tab.hpp"
#include "sample_installer_page.hpp"
#include "sample_loading_page.hpp"

namespace i18n = brls::i18n; // for loadTranslations() and getStr()
using namespace i18n::literals; // for _i18n

std::vector<std::string> NOTIFICATIONS = {
    "You have cool hair",
    "I like your shoes",
    "borealis is powered by nanovg",
    "The Triforce is an inside job",
    "Pozznx will trigger in one day and twelve hours",
    "Aurora Borealis? At this time of day, at this time of year, in this part of the gaming market, located entirely within your Switch?!",
    "May I see it?",
    "Hmm, Steamed Hams!"
};

nxdt::tasks::GameCardTask *gc_task = nullptr;
nxdt::tasks::TitleTask *title_task = nullptr;
nxdt::tasks::UmsTask *ums_task = nullptr;
nxdt::tasks::UsbHostTask *usb_host_task = nullptr;

int main(int argc, char* argv[])
{
    ON_SCOPE_EXIT { utilsCloseResources(); };
    
    /* Initialize application resources. */
    if (!utilsInitializeResources(argc, (const char**)argv)) return EXIT_FAILURE;
    
    /* Set Borealis log level. */
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    
    /* Load Borealis translation files. */
    i18n::loadTranslations();
    
    /* Initialize Borealis. */
    if (!brls::Application::init(APP_TITLE)) return EXIT_FAILURE;
    
    /* Start background tasks. */
    gc_task = new nxdt::tasks::GameCardTask();
    title_task = new nxdt::tasks::TitleTask();
    ums_task = new nxdt::tasks::UmsTask();
    usb_host_task = new nxdt::tasks::UsbHostTask();
    
    /* Create root tab frame. */
    brls::TabFrame *root_frame = new brls::TabFrame();
    root_frame->setTitle(APP_TITLE);
    root_frame->setIcon(BOREALIS_ASSET("icon/" APP_TITLE ".jpg"));
    root_frame->setFooterText("v" APP_VERSION);







    brls::List *testList = new brls::List();

    brls::ListItem* dialogItem = new brls::ListItem("main/pozznx/open"_i18n);
    dialogItem->getClickEvent()->subscribe([](brls::View* view) {
        brls::Dialog* dialog = new brls::Dialog("main/pozznx/warning"_i18n);

        brls::GenericEvent::Callback closeCallback = [dialog](brls::View* view) {
            dialog->close();
            brls::Application::notify("main/pozznx/running"_i18n);
        };

        std::string continueStr = "main/pozznx/continue"_i18n;

        dialog->addButton(continueStr, closeCallback);
        dialog->addButton(continueStr, closeCallback);
        dialog->addButton(continueStr, closeCallback);

        dialog->setCancelable(false);

        dialog->open();
    });

    brls::ListItem* notificationItem = new brls::ListItem("main/notify"_i18n);
    notificationItem->getClickEvent()->subscribe([](brls::View* view) {
        std::string notification = NOTIFICATIONS[std::rand() % NOTIFICATIONS.size()];
        brls::Application::notify(notification);
    });

    brls::ListItem* themeItem = new brls::ListItem("main/tv/resolution"_i18n);
    themeItem->setValue("main/tv/automatic"_i18n);

    brls::ListItem* i18nItem = new brls::ListItem(i18n::getStr("main/i18n/title", i18n::getCurrentLocale(), "main/i18n/lang"_i18n));

    brls::SelectListItem* jankItem = new brls::SelectListItem(
        "main/jank/jank"_i18n,
        {
            "main/jank/native"_i18n,
            "main/jank/minimal"_i18n,
            "main/jank/regular"_i18n,
            "main/jank/maximum"_i18n,
            "main/jank/saxophone"_i18n,
            "main/jank/vista"_i18n,
            "main/jank/ios"_i18n,
        });

    brls::ListItem* crashItem = new brls::ListItem("main/divide/title"_i18n, "main/divide/description"_i18n);
    crashItem->getClickEvent()->subscribe([](brls::View* view) { brls::Application::crash("main/divide/crash"_i18n); });

    brls::ListItem* installerItem = new brls::ListItem("installer/open"_i18n);
    installerItem->getClickEvent()->subscribe([](brls::View* view) {
        brls::StagedAppletFrame* stagedFrame = new brls::StagedAppletFrame();
        stagedFrame->setTitle("installer/title"_i18n);

        stagedFrame->addStage(new SampleInstallerPage(stagedFrame, "installer/stage1/button"_i18n));
        stagedFrame->addStage(new SampleLoadingPage(stagedFrame));
        stagedFrame->addStage(new SampleInstallerPage(stagedFrame, "installer/stage3/button"_i18n));

        brls::Application::pushView(stagedFrame);
    });

    brls::ListItem* popupItem = new brls::ListItem("popup/open"_i18n);
    popupItem->getClickEvent()->subscribe([](brls::View* view) {
        brls::TabFrame* popupTabFrame = new brls::TabFrame();
        popupTabFrame->addTab("popup/red"_i18n, new brls::Rectangle(nvgRGB(255, 0, 0)));
        popupTabFrame->addTab("popup/green"_i18n, new brls::Rectangle(nvgRGB(0, 255, 0)));
        popupTabFrame->addTab("popup/blue"_i18n, new brls::Rectangle(nvgRGB(0, 0, 255)));
        brls::PopupFrame::open("popup/title"_i18n, BOREALIS_ASSET("icon/" APP_TITLE ".jpg"), popupTabFrame, "popup/subtitle/left"_i18n, "popup/subtitle/right"_i18n);
    });

    brls::SelectListItem* layerSelectItem = new brls::SelectListItem("main/layers/title"_i18n, { "main/layers/layer1"_i18n, "main/layers/layer2"_i18n });

    brls::InputListItem* keyboardItem = new brls::InputListItem("main/keyboard/string/title"_i18n, "main/keyboard/string/default"_i18n, "main/keyboard/string/help"_i18n, "", 16);

    brls::IntegerInputListItem* keyboardNumberItem = new brls::IntegerInputListItem("main/keyboard/number/title"_i18n, 1337, "main/keyboard/number/help"_i18n, "", 10);

    testList->addView(dialogItem);
    testList->addView(notificationItem);
    testList->addView(themeItem);
    testList->addView(i18nItem);
    testList->addView(jankItem);
    testList->addView(crashItem);
    testList->addView(installerItem);
    testList->addView(popupItem);
    testList->addView(keyboardItem);
    testList->addView(keyboardNumberItem);

    brls::Label* testLabel = new brls::Label(brls::LabelStyle::REGULAR, "main/more"_i18n, true);
    testList->addView(testLabel);

    brls::ListItem* actionTestItem = new brls::ListItem("main/actions/title"_i18n);
    actionTestItem->registerAction("main/actions/notify"_i18n, brls::Key::L, [] {
        brls::Application::notify("main/actions/triggered"_i18n);
        return true;
    });
    testList->addView(actionTestItem);

    brls::LayerView* testLayers = new brls::LayerView();
    brls::List* layerList1      = new brls::List();
    brls::List* layerList2      = new brls::List();

    layerList1->addView(new brls::Header("main/layers/layer1"_i18n, false));
    layerList1->addView(new brls::ListItem("main/layers/item1"_i18n));
    layerList1->addView(new brls::ListItem("main/layers/item2"_i18n));
    layerList1->addView(new brls::ListItem("main/layers/item3"_i18n));

    layerList2->addView(new brls::Header("main/layers/layer2"_i18n, false));
    layerList2->addView(new brls::ListItem("main/layers/item1"_i18n));
    layerList2->addView(new brls::ListItem("main/layers/item2"_i18n));
    layerList2->addView(new brls::ListItem("main/layers/item3"_i18n));

    testLayers->addLayer(layerList1);
    testLayers->addLayer(layerList2);

    layerSelectItem->getValueSelectedEvent()->subscribe([=](size_t selection) {
        testLayers->changeLayer(selection);
    });

    testList->addView(layerSelectItem);

    root_frame->addTab("main/tabs/first"_i18n, testList);
    root_frame->addTab("main/tabs/second"_i18n, testLayers);
    root_frame->addSeparator();
    root_frame->addTab("main/tabs/third"_i18n, new brls::Rectangle(nvgRGB(255, 0, 0)));
    root_frame->addTab("main/tabs/fourth"_i18n, new brls::Rectangle(nvgRGB(0, 255, 0)));
    root_frame->addSeparator();
    root_frame->addTab("main/tabs/custom_navigation_tab"_i18n, new CustomLayoutTab());

    // Add the root view to the stack
    brls::Application::pushView(root_frame);

    // Run the app
    while (brls::Application::mainLoop());
    
    // Exit
    return EXIT_SUCCESS;
}
