#pragma once

// Standard includes
#include <iostream>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_ask.H>

// Menu callback implementations
void menu_file_open_cb(Fl_Widget*, void* v) {
    Fl_File_Chooser* chooser = new Fl_File_Chooser(".", "*.pbtxt", Fl_File_Chooser::SINGLE, "Open Configuration File");
    chooser->show();
    while (chooser->shown()) {
        Fl::wait();
    }
    if (chooser->value() != nullptr) {
        std::cout << "Opening file: " << chooser->value() << std::endl;
        // TODO: Implement file loading logic
    }
    delete chooser;
}

void menu_file_save_cb(Fl_Widget*, void* v) {
    Fl_File_Chooser* chooser = new Fl_File_Chooser(".", "*.pbtxt", Fl_File_Chooser::CREATE, "Save Configuration File");
    chooser->show();
    while (chooser->shown()) {
        Fl::wait();
    }
    if (chooser->value() != nullptr) {
        std::cout << "Saving file: " << chooser->value() << std::endl;
        // TODO: Implement file saving logic
    }
    delete chooser;
}

void menu_file_exit_cb(Fl_Widget*, void* v) {
    exit(0);
}

void menu_help_about_cb(Fl_Widget*, void* v) {
    fl_message_title("About Project Joshua");
    fl_message("Project Joshua - Proto GUI\n\n"
               "A configuration GUI for Project Joshua robot system.\n"
               "Version 1.0\n\n"
               "Built with FLTK and Protocol Buffers.");
}