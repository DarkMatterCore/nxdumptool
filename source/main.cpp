#include <nxdt_utils.h>
#include <scope_guard.hpp>

#include <borealis.hpp>

#include "captioned_image.hpp"
#include "components_tab.hpp"
#include "main_activity.hpp"
#include "recycling_list_tab.hpp"

using namespace brls::literals; // for _i18n

int main(int argc, char* argv[])
{
    /* Set scope guard to clean up resources at exit. */
    ON_SCOPE_EXIT { utilsCloseResources(); };

    /* Initialize application resources. */
    if (!utilsInitializeResources(argc, (const char**)argv)) return EXIT_FAILURE;

    // Set log level
    // We recommend to use INFO for real apps
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);

    // Init the app and i18n
    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("demo/title"_i18n);

    // Have the application register an action on every activity that will quit when you press BUTTON_START
    brls::Application::setGlobalQuit(true);

    // Register custom views (including tabs, which are views)
    brls::Application::registerXMLView("CaptionedImage", CaptionedImage::create);
    brls::Application::registerXMLView("RecyclingListTab", RecyclingListTab::create);
    brls::Application::registerXMLView("ComponentsTab", ComponentsTab::create);

    // Add custom values to the theme
    brls::getLightTheme().addColor("captioned_image/caption", nvgRGB(2, 176, 183));
    brls::getDarkTheme().addColor("captioned_image/caption", nvgRGB(51, 186, 227));

    // Add custom values to the style
    brls::getStyle().addMetric("about/padding_top_bottom", 50);
    brls::getStyle().addMetric("about/padding_sides", 75);
    brls::getStyle().addMetric("about/description_margin", 50);

    // Create and push the main activity to the stack
    brls::Application::pushActivity(new MainActivity());

    // Run the app
    while (brls::Application::mainLoop());

    // Exit
    return EXIT_SUCCESS;
}
