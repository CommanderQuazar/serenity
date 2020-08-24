/*
 * Copyright (c) 2020, the SerenityOS developers.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Spreadsheet.h"
#include "SpreadsheetWidget.h"
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>
#include <LibGUI/Application.h>
#include <LibGUI/FilePicker.h>
#include <LibGUI/Forward.h>
#include <LibGUI/Menu.h>
#include <LibGUI/MenuBar.h>
#include <LibGUI/Window.h>

int main(int argc, char* argv[])
{
    const char* filename = nullptr;

    Core::ArgsParser args_parser;
    args_parser.add_positional_argument(filename, "File to read from", "file", Core::ArgsParser::Required::No);

    args_parser.parse(argc, argv);

    if (filename) {
        if (!Core::File::exists(filename) || Core::File::is_directory(filename)) {
            fprintf(stderr, "File does not exist or is a directory: %s\n", filename);
            return 1;
        }
    }

    auto app = GUI::Application::construct(argc, argv);

    if (pledge("stdio thread rpath accept cpath wpath shared_buffer unix", nullptr) < 0) {
        perror("pledge");
        return 1;
    }

    if (unveil("/tmp/portal/webcontent", "rw") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil("/etc", "r") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil(Core::StandardPaths::home_directory().characters(), "rwc") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil("/res", "r") < 0) {
        perror("unveil");
        return 1;
    }

    if (unveil(nullptr, nullptr) < 0) {
        perror("unveil");
        return 1;
    }

    auto window = GUI::Window::construct();
    window->set_title("Spreadsheet");
    window->resize(640, 480);

    auto& spreadsheet_widget = window->set_main_widget<Spreadsheet::SpreadsheetWidget>(NonnullRefPtrVector<Spreadsheet::Sheet> {}, filename == nullptr);

    if (filename)
        spreadsheet_widget.load(filename);

    auto menubar = GUI::MenuBar::construct();
    auto& app_menu = menubar->add_menu("Spreadsheet");

    app_menu.add_action(GUI::CommonActions::make_quit_action([&](auto&) {
        app->quit(0);
    }));

    auto& file_menu = menubar->add_menu("File");
    file_menu.add_action(GUI::CommonActions::make_open_action([&](auto&) {
        Optional<String> load_path = GUI::FilePicker::get_open_filepath(window);
        if (!load_path.has_value())
            return;

        spreadsheet_widget.load(load_path.value());
    }));
    file_menu.add_action(GUI::CommonActions::make_save_action([&](auto&) {
        String name = "sheet";
        Optional<String> save_path = GUI::FilePicker::get_save_filepath(window, name, "json");
        if (!save_path.has_value())
            return;

        spreadsheet_widget.save(save_path.value());
    }));

    app->set_menubar(move(menubar));

    window->show();

    return app->exec();
}