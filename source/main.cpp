/*
 * main.cpp
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

#include <nxdt_utils.h>
#include <scope_guard.hpp>
#include <root_view.hpp>

int main(int argc, char* argv[])
{
    /* Set scope guard to clean up resources at exit. */
    ON_SCOPE_EXIT { utilsCloseResources(); };
    
    /* Initialize application resources. */
    if (!utilsInitializeResources(argc, (const char**)argv)) return EXIT_FAILURE;
    
    /* Set Borealis log level. */
    /* TODO: set this to INFO before a proper release. */
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    
    /* Initialize Borealis. */
    if (!brls::Application::init()) return EXIT_FAILURE;
    
    /* Create application window. */
    brls::Application::createWindow(APP_TITLE);
    
    /* Set common footer. */
    brls::Application::setCommonFooter("v" APP_VERSION);
    
    /* Register an action on every activity to quit whenever BUTTON_START is pressed. */
    brls::Application::setGlobalQuit(true);
    
    /* Display a FPS counter whenever BUTTON_BACK is pressed. */
    /* TODO: disable this before a proper release. */
    brls::Application::setGlobalFPSToggle(true);
    
    /* Create and push the main activity to the stack. */
    brls::Activity *main_activity = new brls::Activity();
    main_activity->setContentView(new nxdt::views::RootView());
    brls::Application::pushActivity(main_activity);
    
    /* Run the application. */
    while(brls::Application::mainLoop());
    
    /* Exit. */
    return EXIT_SUCCESS;
}
