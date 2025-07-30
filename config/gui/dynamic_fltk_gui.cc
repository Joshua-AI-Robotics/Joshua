// Protobuf includes first
#include "google/protobuf/descriptor.h"
#include "google/protobuf/reflection.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "config/proto/config.pb.h"

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

// Local includes
#include "config/gui/proto_parser.h"

class DynamicFLTKGUI {
private:
    // Window dimensions
    static const int WINDOW_WIDTH = 1400;
    static const int WINDOW_HEIGHT = 900;
    static const int MENU_HEIGHT = 30;
    static const int MARGIN = 20;
    static const int TAB_HEIGHT = 35;
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

    const std::string TITLE = "Project Joshua - Dynamic Configuration Generator";
    
    // Calculated dimensions
    int content_width_;
    int content_height_;
    int tab_area_width_;
    int tab_area_height_;
    int tab_content_width_;
    int tab_content_height_;
    
    Fl_Window* window_;
    Fl_Menu_Bar* menu_bar_;
    Fl_Tabs* tabs_;
    
    // Proto parser
    std::shared_ptr<config_gui::ProtoParser> parser_;
    
    // Protobuf message
    std::unique_ptr<config::Config> config_;
    
    // Output display
    Fl_Text_Display* output_display_;
    Fl_Text_Buffer* output_buffer_;
    
    // File operations
    std::string current_file_;
    
    // Callback functions
    static void saveCallback(Fl_Widget*, void* v) {
        static_cast<DynamicFLTKGUI*>(v)->saveConfig();
    }
    
    static void loadCallback(Fl_Widget*, void* v) {
        static_cast<DynamicFLTKGUI*>(v)->loadConfig();
    }
    
    static void newCallback(Fl_Widget*, void* v) {
        static_cast<DynamicFLTKGUI*>(v)->newConfig();
    }
    
    static void refreshCallback(Fl_Widget*, void* v) {
        static_cast<DynamicFLTKGUI*>(v)->refreshDisplay();
    }
    
    static void menuCallback(Fl_Widget*, void* v) {
        static_cast<DynamicFLTKGUI*>(v)->handleMenu();
    }

public:
    DynamicFLTKGUI() {
        // Initialize parser
        parser_ = std::make_shared<config_gui::ProtoParser>();
        
        // Calculate proportional dimensions
        content_width_ = WINDOW_WIDTH - 2 * MARGIN;
        content_height_ = WINDOW_HEIGHT - MENU_HEIGHT - 2 * MARGIN;
        tab_area_width_ = content_width_;
        tab_area_height_ = content_height_ - TAB_HEIGHT;
        tab_content_width_ = tab_area_width_ - 2 * MARGIN;
        tab_content_height_ = tab_area_height_ - 2 * MARGIN;
        
        // Initialize config with proper hierarchy
        config_ = std::make_unique<config::Config>();
        config_->set_operation_mode(config::MODE_INFERENCE);
        
        // Initialize robot configuration
        auto* robot = config_->mutable_robot();
        robot->set_name("so100");
        robot->set_id(1);
        robot->set_robot_type(config::MANIPULATOR_ARM);
        
        // Initialize AI configuration
        auto* ai = config_->mutable_ai();
        ai->set_policy_name("example_policy");
        ai->set_pretrained_model_path("/path/to/model");
        ai->set_act_dim(4);
        ai->set_state_dim(12);
        
        // Physical layout is not implemented yet
        // TODO: Implement physical layout configuration
        
        // Create main window (resizable)
        window_ = new Fl_Window(WINDOW_WIDTH, WINDOW_HEIGHT, TITLE.c_str());
        window_->resizable(window_); // Make window resizable
        window_->size_range(800, 600); // Set minimum window size
        
        // Create menu bar
        static Fl_Menu_Item menu_items[] = {
            {"&File", 0, 0, 0, FL_SUBMENU},
                {"&New", FL_CTRL + 'n', newCallback, this},
                {"&Open", FL_CTRL + 'o', loadCallback, this},
                {"&Save", FL_CTRL + 's', saveCallback, this},
                {"Save &As", FL_CTRL + FL_SHIFT + 's', saveCallback, this},
                {0},
            {"&View", 0, 0, 0, FL_SUBMENU},
                {"&Refresh", FL_CTRL + 'r', refreshCallback, this},
                {0},
            {"&Help", 0, 0, 0, FL_SUBMENU},
                {"&About", 0, 0, this},
                {0},
            {0}
        };
        
        menu_bar_ = new Fl_Menu_Bar(0, 0, WINDOW_WIDTH, MENU_HEIGHT);
        menu_bar_->menu(menu_items);
        menu_bar_->callback(menuCallback, this);
        
        // Create tabs
        tabs_ = new Fl_Tabs(MARGIN, MENU_HEIGHT + MARGIN, tab_area_width_, tab_area_height_);
        
        // Create dynamic tabs based on parsed config
        createDynamicTabs();
        
        tabs_->end();
        
        // Create output display
        output_buffer_ = new Fl_Text_Buffer();
        output_display_ = new Fl_Text_Display(MARGIN, WINDOW_HEIGHT - 200, WINDOW_WIDTH - 2 * MARGIN, 180, "Configuration Output");
        output_display_->buffer(output_buffer_);
        output_display_->textfont(FL_COURIER);
        output_display_->textsize(TEXT_DISPLAY_FONT_SIZE);
        
        // Update output with current config
        refreshDisplay();
        
        window_->end();
    }
    
    void show() {
        window_->show();
    }
    
    void createDynamicTabs() {
        // Parse the config message
        config_gui::MessageInfo config_info = parser_->parseConfigMessage(*config_);
        
        // Create tabs for each top-level message
        for (const auto& field : config_info.fields) {
            if (field.is_message) {
                createTabForMessage(field);
            }
        }
        
        // Create a general "All Fields" tab
        createAllFieldsTab(config_info);
    }
    
    void createTabForMessage(const config_gui::FieldInfo& field_info) {
        std::string tab_name = field_info.display_name;
        Fl_Group* group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, 
                                     tab_content_width_, tab_content_height_, tab_name.c_str());
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, 
                                  tab_content_width_ - MARGIN, INPUT_HEIGHT, tab_name.c_str());
        title->labelsize(TITLE_FONT_SIZE);
        title->labelfont(FL_BOLD);
        
        // Create dynamic fields
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + INPUT_HEIGHT + SECTION_SPACING;
        createDynamicFields(field_info, current_y, group);
        
        group->end();
    }
    
    void createAllFieldsTab(const config_gui::MessageInfo& config_info) {
        Fl_Group* group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, 
                                     tab_content_width_, tab_content_height_, "All Fields");
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, 
                                  tab_content_width_ - MARGIN, INPUT_HEIGHT, "All Configuration Fields");
        title->labelsize(TITLE_FONT_SIZE);
        title->labelfont(FL_BOLD);
        
        // Create dynamic fields for all top-level fields
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + INPUT_HEIGHT + SECTION_SPACING;
        for (const auto& field : config_info.fields) {
            createDynamicField(field, current_y, group);
            current_y += FIELD_SPACING;
        }
        
        group->end();
    }
    
    void createDynamicFields(const config_gui::FieldInfo& field_info, int& current_y, Fl_Group* parent) {
        // This is a simplified version - in a full implementation, you would
        // recursively parse the message and create widgets for all fields
        
        Fl_Box* section = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, 
                                    (field_info.display_name + ":").c_str());
        section->labelsize(SECTION_FONT_SIZE);
        section->labelfont(FL_BOLD);
        
        current_y += FIELD_SPACING;
        
        // For now, just show the field info
        std::string info_text = "Type: " + field_info.field_type + "\n";
        if (field_info.is_repeated) {
            info_text += "Repeated field\n";
        }
        if (field_info.is_oneof) {
            info_text += "Oneof field: " + field_info.oneof_name + "\n";
        }
        
        Fl_Box* info = new Fl_Box(MARGIN * 2, current_y, tab_content_width_ - MARGIN * 2, INPUT_HEIGHT * 3, 
                                 info_text.c_str());
        info->labelsize(LABEL_FONT_SIZE);
        info->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);
        
        current_y += INPUT_HEIGHT * 3 + FIELD_SPACING;
    }
    
    void createDynamicField(const config_gui::FieldInfo& field_info, int current_y, Fl_Group* parent) {
        // Create label
        Fl_Box* label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, 
                                  (field_info.display_name + ":").c_str());
        label->labelsize(LABEL_FONT_SIZE);
        
        // Create input widget based on field type
        if (field_info.is_enum) {
            Fl_Choice* choice = new Fl_Choice(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, 
                                            INPUT_WIDTH, INPUT_HEIGHT);
            for (const auto& enum_value : field_info.enum_values) {
                choice->add(enum_value.c_str());
            }
            choice->value(0);
            choice->textsize(INPUT_FONT_SIZE);
        } else if (field_info.field_type == "string") {
            Fl_Input* input = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, 
                                         INPUT_WIDTH, INPUT_HEIGHT);
            input->value(field_info.value.c_str());
            input->textsize(INPUT_FONT_SIZE);
        } else if (field_info.field_type == "int32" || field_info.field_type == "uint32" ||
                   field_info.field_type == "int64" || field_info.field_type == "uint64") {
            Fl_Input* input = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, 
                                         INPUT_WIDTH, INPUT_HEIGHT);
            input->value(field_info.value.c_str());
            input->type(FL_INT_INPUT);
            input->textsize(INPUT_FONT_SIZE);
        } else if (field_info.field_type == "bool") {
            Fl_Choice* choice = new Fl_Choice(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, 
                                            INPUT_WIDTH, INPUT_HEIGHT);
            choice->add("true");
            choice->add("false");
            choice->value(field_info.value == "true" ? 0 : 1);
            choice->textsize(INPUT_FONT_SIZE);
        } else {
            // For message types, show a placeholder
            Fl_Box* placeholder = new Fl_Box(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, 
                                           INPUT_WIDTH, INPUT_HEIGHT, "[Message]");
            placeholder->labelsize(INPUT_FONT_SIZE);
            placeholder->box(FL_DOWN_BOX);
        }
    }
    
    void saveConfig() {
        // Show file chooser
        Fl_File_Chooser* chooser = new Fl_File_Chooser(".", "*.pbtxt", Fl_File_Chooser::CREATE, "Save Configuration");
        chooser->show();
        
        while (chooser->shown()) {
            Fl::wait();
        }
        
        if (chooser->value() != nullptr) {
            std::string filename = chooser->value();
            std::ofstream file(filename);
            
            if (file.is_open()) {
                std::string config_text;
                google::protobuf::TextFormat::PrintToString(*config_, &config_text);
                file << config_text;
                file.close();
                current_file_ = filename;
                std::cout << "Configuration saved to: " << filename << std::endl;
                refreshDisplay();
            } else {
                std::cout << "Error: Could not save file" << std::endl;
            }
        }
        
        delete chooser;
    }
    
    void loadConfig() {
        // Show file chooser
        Fl_File_Chooser* chooser = new Fl_File_Chooser(".", "*.pbtxt", Fl_File_Chooser::SINGLE, "Load Configuration");
        chooser->show();
        
        while (chooser->shown()) {
            Fl::wait();
        }
        
        if (chooser->value() != nullptr) {
            std::string filename = chooser->value();
            std::ifstream file(filename);
            
            if (file.is_open()) {
                std::string content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
                file.close();
                
                if (google::protobuf::TextFormat::ParseFromString(content, config_.get())) {
                    current_file_ = filename;
                    refreshDisplay();
                    std::cout << "Configuration loaded from: " << filename << std::endl;
                } else {
                    std::cout << "Error: Could not parse configuration file" << std::endl;
                }
            } else {
                std::cout << "Error: Could not open file" << std::endl;
            }
        }
        
        delete chooser;
    }
    
    void newConfig() {
        // Reset to default configuration with proper hierarchy
        config_ = std::make_unique<config::Config>();
        config_->set_operation_mode(config::MODE_INFERENCE);
        
        // Initialize robot configuration
        auto* robot = config_->mutable_robot();
        robot->set_name("so100");
        robot->set_id(1);
        robot->set_robot_type(config::MANIPULATOR_ARM);
        
        // Initialize AI configuration
        auto* ai = config_->mutable_ai();
        ai->set_policy_name("example_policy");
        ai->set_pretrained_model_path("/path/to/model");
        ai->set_act_dim(4);
        ai->set_state_dim(12);
        
        // Physical layout is not implemented yet
        // TODO: Implement physical layout configuration
        
        current_file_.clear();
        refreshDisplay();
    }
    
    void refreshDisplay() {
        // Update output with current config
        std::string config_text;
        google::protobuf::TextFormat::PrintToString(*config_, &config_text);
        output_buffer_->text(config_text.c_str());
        
        // Recreate tabs with updated data
        // Note: In a full implementation, you would update existing widgets instead of recreating
        // For now, we'll just update the output display
    }
    
    void handleMenu() {
        // Menu handling is done through callbacks
    }
    
    ~DynamicFLTKGUI() {
        delete output_buffer_;
    }
};

int main() {
    std::cout << "Starting Project Joshua Dynamic FLTK GUI..." << std::endl;
    
    DynamicFLTKGUI gui;
    gui.show();
    
    return Fl::run();
} 