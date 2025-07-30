#pragma once

#include "config/proto/config.pb.h"
#include "config/config_utils.h"
#include "proto_gui_callbacks.h"

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

    config::Config config_;
    std::vector<config::config_util::ProtoField> proto_fields_;
    
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

    // Drop downs will be generated under tab by using compartive position under group. So default x and y are 0.
    Fl_Choice* create_drop_down(const google::protobuf::EnumDescriptor* enum_descriptor, int x = 0, int y = 0) {
        auto choice = new Fl_Choice(x, y, INPUT_WIDTH, INPUT_HEIGHT);
        choice->textsize(INPUT_FONT_SIZE);
        
        for (int i = 0; i < enum_descriptor->value_count(); i++) {
            const std::string& value_name = enum_descriptor->value(i)->name();
            choice->add(value_name.c_str());
        }
        
        if (enum_descriptor->value_count() > 0) {
            choice->value(0); // Set default to first option
        }
        
        return choice;
    }

    // Labels will be generated under tab by using compartive position under group. So default x and y are 0.
    Fl_Box* create_label(const std::string& label_text, const int& x = 0, const int& y = 0) {
        auto label = new Fl_Box(x, y, LABEL_WIDTH, INPUT_HEIGHT);
        label->copy_label(label_text.c_str());
        label->labelsize(LABEL_FONT_SIZE);
        label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        return label;
    }
    
    void create_tab(const std::string& tab_name) {
        // Create a group for this tab
        auto group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT,
                                    WINDOW_WIDTH - 2 * MARGIN,
                                    WINDOW_HEIGHT - MENU_HEIGHT - 2 * MARGIN - TAB_HEIGHT);

        // Set the label on the group
        group->copy_label(tab_name.c_str());
        group->labelsize(TAB_FONT_SIZE);
                
        // Add the group to the tabs container
        tabs_container_->add(group);
        
        // Store the group reference
        tab_groups_.push_back(group);
    }

    Fl_Input* create_input_field(const int& x = 0, const int& y = 0) {
        auto input = new Fl_Input(x, y, INPUT_WIDTH, INPUT_HEIGHT);
        input->textsize(INPUT_FONT_SIZE);
        return input;
    }

    void fill_tab_content(const int& tab_index, Fl_Box* label, Fl_Input* input) {

        tab_groups_[tab_index]->begin();

        // Count existing Fl_Box labels in the group
        int existing_labels = 0;
        for (int i = 0; i < tab_groups_[tab_index]->children(); ++i) {
            Fl_Widget* child = tab_groups_[tab_index]->child(i);
            if (dynamic_cast<Fl_Box*>(child)) {
                ++existing_labels;
            }
        }

        int content_x = tab_groups_[tab_index]->x() + MARGIN;
        int content_y = tab_groups_[tab_index]->y() + MARGIN + existing_labels * (INPUT_HEIGHT + MARGIN);

        // Add widgets to group first, then position them
        tab_groups_[tab_index]->add(label);
        tab_groups_[tab_index]->add(input);
        
        // Position the label and input at the calculated coordinates
        label->position(content_x, content_y);
        input->resize(content_x + LABEL_WIDTH + MARGIN, content_y, INPUT_WIDTH, INPUT_HEIGHT);

        tab_groups_[tab_index]->end();
    }
    
    void fill_tab_content(const int& tab_index, Fl_Box* label, Fl_Choice* choice) {

        tab_groups_[tab_index]->begin();

        // Count existing Fl_Box labels in the group
        int existing_labels = 0;
        for (int i = 0; i < tab_groups_[tab_index]->children(); ++i) {
            Fl_Widget* child = tab_groups_[tab_index]->child(i);
            if (dynamic_cast<Fl_Box*>(child)) {
                ++existing_labels;
            }
        }

        int content_x = tab_groups_[tab_index]->x() + MARGIN;
        int content_y = tab_groups_[tab_index]->y() + MARGIN + existing_labels * (INPUT_HEIGHT + MARGIN);

        // Add widgets to group first, then position them  
        tab_groups_[tab_index]->add(label);
        tab_groups_[tab_index]->add(choice);
        
        // Position the label and choice at the calculated coordinates
        label->position(content_x, content_y);
        choice->resize(content_x + LABEL_WIDTH + MARGIN, content_y, INPUT_WIDTH, INPUT_HEIGHT);

        tab_groups_[tab_index]->end();
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

        proto_fields_ = config::config_util::GetProtoFields(config_);

        // Create tabs for the highest level of the proto (config.proto).
        for (const auto& field : proto_fields_) {
            create_tab(field.name());
        }

        // Find the field descriptor for operation_mode
        const google::protobuf::Descriptor* config_descriptor = config_.GetDescriptor();
        const google::protobuf::FieldDescriptor* op_mode_field = config_descriptor->FindFieldByName("operation_mode");

        fill_tab_content(0, create_label("Operation Mode:"), create_drop_down(op_mode_field->enum_type()));
        fill_tab_content(0, create_label("Test Mode:"), create_input_field());
        
        window_->end();
    }
    
    void show() {
        window_->show();
    }

};
