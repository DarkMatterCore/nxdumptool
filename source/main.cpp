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

int main(int argc, char *argv[])
{
    /* Set scope guard to clean up resources at exit. */
    ON_SCOPE_EXIT { utilsCloseResources(); };
    
    /* Initialize application resources. */
    if (!utilsInitializeResources(argc, (const char**)argv)) return EXIT_FAILURE;
    
    /* Set Borealis log level. */
    brls::Logger::setLogLevel(brls::LogLevel::DEBUG);
    
    /* Load Borealis translation files. */
    brls::i18n::loadTranslations();
    
    /* Initialize Borealis. */
    if (!brls::Application::init(APP_TITLE)) return EXIT_FAILURE;
    
    /* Create root view. */
    nxdt::views::RootView *root_view = new nxdt::views::RootView();
    
    /* Add the root view to the stack. */
    brls::Application::pushView(root_view);
    
    /* Run the application. */
    while(brls::Application::mainLoop());
    
    /* Exit. */
    return EXIT_SUCCESS;
}
