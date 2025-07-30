#include "config/proto/config.pb.h"
#include "config/config_utils.h"

// Standard includes
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <memory>

// FLTK includes last
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Menu_Item.H>
#include <FL/Fl_Scroll.H>


// Forward declaration
class ProtoGUI;

// Menu callback functions
void menu_file_open_cb(Fl_Widget*, void*);
void menu_file_save_cb(Fl_Widget*, void*);
void menu_file_exit_cb(Fl_Widget*, void*);
void menu_help_about_cb(Fl_Widget*, void*);

class ProtoGUI {
private:
    // Window dimensions
    static const int WINDOW_WIDTH = 2400;
    static const int WINDOW_HEIGHT = 1800;
    static const int MENU_HEIGHT = 40;
    static const int MARGIN = 30;
    static const int TAB_HEIGHT = 45;
    static const int LABEL_WIDTH = 200;
    static const int INPUT_WIDTH = 300;
    static const int INPUT_HEIGHT = 30;
    static const int BUTTON_WIDTH = 120;
    static const int BUTTON_HEIGHT = 35;
    static const int SECTION_SPACING = 40;
    static const int FIELD_SPACING = 35;
    
    // Font sizes
    static const int TITLE_FONT_SIZE = 36;
    static const int SECTION_FONT_SIZE = 24;
    static const int LABEL_FONT_SIZE = 24;
    static const int INPUT_FONT_SIZE = 20;
    static const int TEXT_DISPLAY_FONT_SIZE = 20;
    static const int TAB_FONT_SIZE = 30;
    static const int MENU_FONT_SIZE = 28;

    const std::string TITLE = "Project Joshua - Proto GUI";
    Fl_Window* window_;
    Fl_Menu_Bar* menu_bar_;

    Fl_Tabs* tabs_container_;
    std::vector<Fl_Group*> tab_groups_;
    
    void create_menu_bar() {
        static Fl_Menu_Item menu_items[] = {
            {"&File", 0, 0, 0, FL_SUBMENU},
                {"&Open...", FL_CTRL + 'o', menu_file_open_cb, this},
                {"&Save", FL_CTRL + 's', menu_file_save_cb, this},
                {"Save &As...", FL_CTRL + FL_SHIFT + 's', menu_file_save_cb, this},
                {0},
                {"E&xit", FL_CTRL + 'q', menu_file_exit_cb, this},
                {0},
            {"&Edit", 0, 0, 0, FL_SUBMENU},
                {"&Undo", FL_CTRL + 'z', 0, 0, FL_MENU_DIVIDER},
                {"&Redo", FL_CTRL + 'y', 0, 0},
                {0},
            {"&Help", 0, 0, 0, FL_SUBMENU},
                {"&About", 0, menu_help_about_cb, this},
                {0},
            {0}
        };
        
        menu_bar_ = new Fl_Menu_Bar(0, 0, WINDOW_WIDTH, MENU_HEIGHT);
        menu_bar_->menu(menu_items);
        menu_bar_->labelsize(MENU_FONT_SIZE);
        menu_bar_->textsize(MENU_FONT_SIZE);
    }
    
    void create_tabs_container() {
        // Create a single tabs container
        tabs_container_ = new Fl_Tabs(MARGIN, MENU_HEIGHT + MARGIN, 
                                    WINDOW_WIDTH - 2 * MARGIN, 
                                    WINDOW_HEIGHT - MENU_HEIGHT - 2 * MARGIN);
    }
    
    void create_tab(const std::string& tab_name) {
        // Create a group for this tab
        auto group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT,
                                    WINDOW_WIDTH - 2 * MARGIN,
                                    WINDOW_HEIGHT - MENU_HEIGHT - 2 * MARGIN - TAB_HEIGHT);

        // Set the label on the group
        group->copy_label(tab_name.c_str());
        group->labelsize(TAB_FONT_SIZE);

        group->end();
        
        // Add the group to the tabs container
        tabs_container_->add(group);
        
        // Store the group reference
        tab_groups_.push_back(group);
    }

public:
    ProtoGUI() {
        // Create main window (resizable)
        window_ = new Fl_Window(WINDOW_WIDTH, WINDOW_HEIGHT, TITLE.c_str());
        window_->resizable(window_);
        window_->size_range(WINDOW_WIDTH, WINDOW_HEIGHT);
        
        // Create menu bar
        create_menu_bar();
        
        // Create tabs container.
        create_tabs_container();

        config::Config config;
        auto proto_fields = config::config_util::GetProtoFields(config);

        for (const auto& field : proto_fields) {
            create_tab(field.name());
        }
        
        window_->end();
    }
    
    void show() {
        window_->show();
    }

};

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

int main() {
    std::cout << "Starting Project Joshua Proto GUI..." << std::endl;
    
    ProtoGUI gui;
    gui.show();
    
    return Fl::run();
} 