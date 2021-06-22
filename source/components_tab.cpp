/*
    Copyright 2021 natinusala

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "components_tab.hpp"

ComponentsTab::ComponentsTab()
{
    // Inflate the tab from the XML file
    this->inflateFromXMLRes("xml/tabs/components.xml");

    // Bind the button click to a method using the macro (just for the sake of showcasing it, it's overkill in this situation)
    BRLS_REGISTER_CLICK_BY_ID("button_primary", this->onPrimaryButtonClicked);

    // Get a handle to the button and register the action directly
    brls::Button* highlightButton = (brls::Button*)this->getView("button_highlight");
    highlightButton->registerAction(
        "Honk", brls::BUTTON_A, [](brls::View* view) { return true; }, false, brls::SOUND_HONK);
}

bool ComponentsTab::onPrimaryButtonClicked(brls::View* view)
{
    brls::Logger::info("Clicked");
    return true;
}

brls::View* ComponentsTab::create()
{
    // Called by the XML engine to create a new ComponentsTab
    return new ComponentsTab();
}
