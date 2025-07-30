// Protobuf includes first
#include "google/protobuf/descriptor.h"
#include "google/protobuf/reflection.h"
#include "google/protobuf/message.h"
#include "google/protobuf/text_format.h"
#include "config/proto/config.pb.h"
#include "robot/action/proto/action.pb.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/comm_interface/proto/comm_interface.pb.h"

// Standard includes
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

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

class AdvancedFLTKGUI {
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
    static const int TAB_FONT_SIZE = 18;

    const std::string TITLE = "Project Joshua - Configuration Generator";
    const std::string MAIN_CONFIG = "Main Config";
    const std::string AI_CONFIG = "AI Config (Not Implemented)";
    const std::string ACTUATORS = "Actuators";
    const std::string OUTPUT = "Output";
    
    // Calculated dimensions
    int content_width_;
    int content_height_;
    int tab_area_width_;
    int tab_area_height_;
    int tab_content_width_;
    int tab_content_height_;
    
    Fl_Window* window_;
    Fl_Menu_Bar* menu_bar_;
    
    // Main configuration tab
    Fl_Input* robot_name_input_;
    Fl_Input* robot_id_input_;
    Fl_Choice* robot_type_choice_;
    Fl_Choice* operation_mode_choice_;
    
    // AI configuration tab
    Fl_Input* policy_name_input_;
    Fl_Input* model_path_input_;
    Fl_Input* act_dim_input_;
    Fl_Input* state_dim_input_;
    
    // Actuator management
    Fl_Text_Display* actuator_display_;
    Fl_Text_Buffer* actuator_buffer_;
    Fl_Input* new_actuator_name_;
    Fl_Input* new_servo_id_;
    Fl_Input* new_comm_port_;
    Fl_Choice* new_actuator_type_;
    
    // Perception management
    Fl_Text_Display* perception_display_;
    Fl_Text_Buffer* perception_buffer_;
    Fl_Input* new_perception_name_;
    Fl_Choice* new_perception_type_;
    Fl_Input* new_perception_comm_port_;
    Fl_Input* new_camera_width_;
    Fl_Input* new_camera_height_;
    Fl_Input* new_camera_fps_;
    Fl_Input* new_encoder_servo_id_;
    
    // File operations
    std::string current_file_;
    
    // Protobuf message
    std::unique_ptr<config::Config> config_;
    
    // Callback functions
    static void saveCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->saveConfig();
    }
    
    static void loadCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->loadConfig();
    }
    
    static void newCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->newConfig();
    }
    
    static void addActuatorCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->addActuator();
    }
    
    static void removeActuatorCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->removeActuator();
    }
    
    static void addPerceptionCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->addPerception();
    }
    
    static void removePerceptionCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->removePerception();
    }
    
    static void menuCallback(Fl_Widget*, void* v) {
        static_cast<AdvancedFLTKGUI*>(v)->handleMenu();
    }
    


public:
    AdvancedFLTKGUI() {
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
            {"&Edit", 0, 0, 0, FL_SUBMENU},
                {"&Add Actuator", 0, addActuatorCallback, this},
                {"&Remove Actuator", 0, removeActuatorCallback, this},
                {"&Add Perception", 0, addPerceptionCallback, this},
                {"&Remove Perception", 0, removePerceptionCallback, this},
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
        Fl_Tabs* tabs = new Fl_Tabs(MARGIN, MENU_HEIGHT + MARGIN, tab_area_width_, tab_area_height_);
        
        // Main Configuration Tab
        Fl_Group* main_group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, tab_content_width_, tab_content_height_, MAIN_CONFIG.c_str());
        main_group->labelsize(TAB_FONT_SIZE);  // Set tab font size
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, tab_content_width_ - MARGIN, INPUT_HEIGHT, TITLE.c_str());
        title->labelsize(TITLE_FONT_SIZE);
        title->labelfont(FL_BOLD);
        
        // Robot configuration section
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + INPUT_HEIGHT + SECTION_SPACING;
        Fl_Box* robot_section = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Robot Configuration:");
        robot_section->labelsize(SECTION_FONT_SIZE);
        robot_section->labelfont(FL_BOLD);
        
        current_y += FIELD_SPACING;
        Fl_Box* name_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Robot Name:");
        name_label->labelsize(LABEL_FONT_SIZE);
        robot_name_input_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        robot_name_input_->value("so100");
        robot_name_input_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* id_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Robot ID:");
        id_label->labelsize(LABEL_FONT_SIZE);
        robot_id_input_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        robot_id_input_->value("1");
        robot_id_input_->type(FL_INT_INPUT);
        robot_id_input_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* type_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Robot Type:");
        type_label->labelsize(LABEL_FONT_SIZE);
        robot_type_choice_ = new Fl_Choice(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        robot_type_choice_->add("MANIPULATOR_ARM");
        robot_type_choice_->add("ROBOT_INVALID");
        robot_type_choice_->value(0);
        robot_type_choice_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* mode_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Operation Mode:");
        mode_label->labelsize(LABEL_FONT_SIZE);
        operation_mode_choice_ = new Fl_Choice(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        operation_mode_choice_->add("MODE_INFERENCE");
        operation_mode_choice_->add("MODE_TELEOPERATE");
        operation_mode_choice_->add("MODE_TRAINING");
        operation_mode_choice_->add("MODE_TEST");
        operation_mode_choice_->value(0);
        operation_mode_choice_->textsize(INPUT_FONT_SIZE);
        
        main_group->end();
        
        // AI Configuration Tab
        Fl_Group* ai_group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, tab_content_width_, tab_content_height_, AI_CONFIG.c_str());
        ai_group->labelsize(TAB_FONT_SIZE);  // Set tab font size
        
        Fl_Box* ai_title = new Fl_Box(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, tab_content_width_ - MARGIN, INPUT_HEIGHT, AI_CONFIG.c_str());
        ai_title->labelsize(TITLE_FONT_SIZE);
        ai_title->labelfont(FL_BOLD);
        
        current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + INPUT_HEIGHT + SECTION_SPACING;
        Fl_Box* ai_section = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "AI Settings:");
        ai_section->labelsize(SECTION_FONT_SIZE);
        ai_section->labelfont(FL_BOLD);
        
        current_y += FIELD_SPACING;
        Fl_Box* policy_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Policy Name:");
        policy_label->labelsize(LABEL_FONT_SIZE);
        policy_name_input_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        policy_name_input_->value("example_policy");
        policy_name_input_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* model_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Model Path:");
        model_label->labelsize(LABEL_FONT_SIZE);
        model_path_input_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        model_path_input_->value("/path/to/model");
        model_path_input_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* act_dim_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Action Dimension:");
        act_dim_label->labelsize(LABEL_FONT_SIZE);
        act_dim_input_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH / 3, INPUT_HEIGHT);
        act_dim_input_->value("4");
        act_dim_input_->type(FL_INT_INPUT);
        act_dim_input_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* state_dim_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "State Dimension:");
        state_dim_label->labelsize(LABEL_FONT_SIZE);
        state_dim_input_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH / 3, INPUT_HEIGHT);
        state_dim_input_->value("12");
        state_dim_input_->type(FL_INT_INPUT);
        state_dim_input_->textsize(INPUT_FONT_SIZE);
        
        ai_group->end();
        
        // Actuator Configuration Tab
        Fl_Group* actuator_group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, tab_content_width_, tab_content_height_, ACTUATORS.c_str());
        actuator_group->labelsize(TAB_FONT_SIZE);  // Set tab font size
        
        Fl_Box* actuator_title = new Fl_Box(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, tab_content_width_ - MARGIN, INPUT_HEIGHT, ACTUATORS.c_str());
        actuator_title->labelsize(TITLE_FONT_SIZE);
        actuator_title->labelfont(FL_BOLD);
        
        // Current actuators display
        current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + INPUT_HEIGHT + SECTION_SPACING;
        Fl_Box* current_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Current Actuators:");
        current_label->labelsize(SECTION_FONT_SIZE);
        current_label->labelfont(FL_BOLD);
        
        current_y += FIELD_SPACING;
        actuator_buffer_ = new Fl_Text_Buffer();
        actuator_display_ = new Fl_Text_Display(MARGIN * 2, current_y, tab_content_width_ - MARGIN * 2, tab_content_height_ / 2);
        actuator_display_->buffer(actuator_buffer_);
        actuator_display_->textfont(FL_COURIER);
        actuator_display_->textsize(TEXT_DISPLAY_FONT_SIZE);
        
        // Add new actuator section
        current_y += tab_content_height_ / 2 + SECTION_SPACING;
        Fl_Box* add_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Add New Actuator:");
        add_label->labelsize(SECTION_FONT_SIZE);
        add_label->labelfont(FL_BOLD);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_name_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Actuator Name:");
        new_name_label->labelsize(LABEL_FONT_SIZE);
        new_actuator_name_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        new_actuator_name_->value("sts_motor_1");
        new_actuator_name_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_type_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Actuator Type:");
        new_type_label->labelsize(LABEL_FONT_SIZE);
        new_actuator_type_ = new Fl_Choice(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        new_actuator_type_->add("STS3215_SERVO");
        new_actuator_type_->add("ACTUATOR_INVALID");
        new_actuator_type_->value(0);
        new_actuator_type_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_servo_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Servo ID:");
        new_servo_label->labelsize(LABEL_FONT_SIZE);
        new_servo_id_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH / 3, INPUT_HEIGHT);
        new_servo_id_->value("1");
        new_servo_id_->type(FL_INT_INPUT);
        new_servo_id_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_port_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Comm Port:");
        new_port_label->labelsize(LABEL_FONT_SIZE);
        new_comm_port_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        new_comm_port_->value("/dev/ttyACM0");
        new_comm_port_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING + MARGIN;
        Fl_Button* add_btn = new Fl_Button(MARGIN * 2, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Add Actuator");
        add_btn->callback(addActuatorCallback, this);
        
        Fl_Button* remove_btn = new Fl_Button(MARGIN * 2 + BUTTON_WIDTH + MARGIN, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Remove Last");
        remove_btn->callback(removeActuatorCallback, this);
        
        actuator_group->end();
        
        // Perception Configuration Tab
        Fl_Group* perception_group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, tab_content_width_, tab_content_height_, "Perceptions");
        
        Fl_Box* perception_title = new Fl_Box(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, tab_content_width_ - MARGIN, INPUT_HEIGHT, "Perceptions");
        perception_title->labelsize(TITLE_FONT_SIZE);
        perception_title->labelfont(FL_BOLD);
        
        // Current perceptions display
        current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + INPUT_HEIGHT + SECTION_SPACING;
        Fl_Box* perception_current_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Current Perceptions:");
        perception_current_label->labelsize(SECTION_FONT_SIZE);
        perception_current_label->labelfont(FL_BOLD);
        
        current_y += FIELD_SPACING;
        perception_buffer_ = new Fl_Text_Buffer();
        perception_display_ = new Fl_Text_Display(MARGIN * 2, current_y, tab_content_width_ - MARGIN * 2, tab_content_height_ / 2);
        perception_display_->buffer(perception_buffer_);
        perception_display_->textfont(FL_COURIER);
        perception_display_->textsize(TEXT_DISPLAY_FONT_SIZE);
        
        // Add new perception section
        current_y += tab_content_height_ / 2 + SECTION_SPACING;
        Fl_Box* add_perception_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Add New Perception:");
        add_perception_label->labelsize(SECTION_FONT_SIZE);
        add_perception_label->labelfont(FL_BOLD);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_perception_name_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Perception Name:");
        new_perception_name_label->labelsize(LABEL_FONT_SIZE);
        new_perception_name_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        new_perception_name_->value("camera_1");
        new_perception_name_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_perception_type_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Perception Type:");
        new_perception_type_label->labelsize(LABEL_FONT_SIZE);
        new_perception_type_ = new Fl_Choice(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        new_perception_type_->add("CAMERA");
        new_perception_type_->add("ENCODER");
        new_perception_type_->add("LIDAR");
        new_perception_type_->add("RADAR");
        new_perception_type_->add("HID");
        new_perception_type_->value(0);
        new_perception_type_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_perception_comm_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Comm Port:");
        new_perception_comm_label->labelsize(LABEL_FONT_SIZE);
        new_perception_comm_port_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH, INPUT_HEIGHT);
        new_perception_comm_port_->value("/dev/video0");
        new_perception_comm_port_->textsize(INPUT_FONT_SIZE);
        
        // Camera-specific fields
        current_y += FIELD_SPACING;
        Fl_Box* new_camera_width_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Camera Width:");
        new_camera_width_label->labelsize(LABEL_FONT_SIZE);
        new_camera_width_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH / 3, INPUT_HEIGHT);
        new_camera_width_->value("640");
        new_camera_width_->type(FL_INT_INPUT);
        new_camera_width_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_camera_height_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Camera Height:");
        new_camera_height_label->labelsize(LABEL_FONT_SIZE);
        new_camera_height_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH / 3, INPUT_HEIGHT);
        new_camera_height_->value("480");
        new_camera_height_->type(FL_INT_INPUT);
        new_camera_height_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING;
        Fl_Box* new_camera_fps_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Camera FPS:");
        new_camera_fps_label->labelsize(LABEL_FONT_SIZE);
        new_camera_fps_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH / 3, INPUT_HEIGHT);
        new_camera_fps_->value("30");
        new_camera_fps_->type(FL_INT_INPUT);
        new_camera_fps_->textsize(INPUT_FONT_SIZE);
        
        // Encoder-specific fields
        current_y += FIELD_SPACING;
        Fl_Box* new_encoder_servo_label = new Fl_Box(MARGIN * 2, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Encoder Servo ID:");
        new_encoder_servo_label->labelsize(LABEL_FONT_SIZE);
        new_encoder_servo_id_ = new Fl_Input(MARGIN * 2 + LABEL_WIDTH + MARGIN, current_y, INPUT_WIDTH / 3, INPUT_HEIGHT);
        new_encoder_servo_id_->value("1");
        new_encoder_servo_id_->type(FL_INT_INPUT);
        new_encoder_servo_id_->textsize(INPUT_FONT_SIZE);
        
        current_y += FIELD_SPACING + MARGIN;
        Fl_Button* add_perception_btn = new Fl_Button(MARGIN * 2, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Add Perception");
        add_perception_btn->callback(addPerceptionCallback, this);
        
        Fl_Button* remove_perception_btn = new Fl_Button(MARGIN * 2 + BUTTON_WIDTH + MARGIN, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Remove Last");
        remove_perception_btn->callback(removePerceptionCallback, this);
        
        perception_group->end();
        
        // Output Tab
        Fl_Group* output_group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, tab_content_width_, tab_content_height_, OUTPUT.c_str());
        output_group->labelsize(TAB_FONT_SIZE);  // Set tab font size
        
        Fl_Box* output_title = new Fl_Box(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, tab_content_width_ - MARGIN, INPUT_HEIGHT, OUTPUT.c_str());
        output_title->labelsize(TITLE_FONT_SIZE);
        output_title->labelfont(FL_BOLD);
        
        Fl_Text_Buffer* output_buffer = new Fl_Text_Buffer();
        Fl_Text_Display* output_display = new Fl_Text_Display(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + INPUT_HEIGHT + MARGIN, tab_content_width_ - MARGIN * 2, tab_content_height_ - INPUT_HEIGHT - MARGIN * 2);
        output_display->buffer(output_buffer);
        output_display->textfont(FL_COURIER);
        output_display->textsize(TEXT_DISPLAY_FONT_SIZE);
        
        // Update output with current config
        std::string config_text;
        google::protobuf::TextFormat::PrintToString(*config_, &config_text);
        output_buffer->text(config_text.c_str());
        
        output_group->end();
        
        tabs->end();
        
        // Update displays
        updateActuatorDisplay();
        updatePerceptionDisplay();
        
        window_->end();
    }
    
    void show() {
        window_->show();
    }
    
    void saveConfig() {
        // Update config from UI
        updateConfigFromUI();
        
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
                    updateUIFromConfig();
                    updateActuatorDisplay();
                    updatePerceptionDisplay();
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
        updateUIFromConfig();
        updateActuatorDisplay();
        updatePerceptionDisplay();
    }
    
    void addActuator() {
        // Get the robot's actions container
        auto* robot = config_->mutable_robot();
        auto* actions = robot->mutable_actions();
        
        // Create a new single action
        auto* single_action = actions->add_single_actions();
        single_action->set_node_id(1);
        single_action->set_action_type(robot::action::ACTUATOR);
        single_action->set_subscribe_topic("/actuator_" + std::to_string(actions->single_actions_size()));
        
        // Create the actuator within the action_data oneof
        auto* actuator = single_action->mutable_actuator();
        actuator->set_actuator_name(new_actuator_name_->value());
        actuator->set_id(actions->single_actions_size());
        
        // Set actuator type
        if (std::string(new_actuator_type_->text()) == "STS3215_SERVO") {
            actuator->set_actuator_type(robot::action::STS3215_SERVO);
        } else {
            actuator->set_actuator_type(robot::action::ACTUATOR_INVALID);
        }
        
        actuator->set_comm_type(robot::comm_interface::SERIAL);
        actuator->set_physical_lower_limit(0);
        actuator->set_physical_upper_limit(4095);
        actuator->set_operational_lower_limit(800);
        actuator->set_operational_upper_limit(3000);
        
        // Set serial communication config
        auto* serial_config = actuator->mutable_serial_config();
        serial_config->set_id(actuator->id());
        serial_config->set_port(new_comm_port_->value());
        serial_config->set_baudrate(1000000);
        
        // Add specific config for STS3215
        if (std::string(new_actuator_type_->text()) == "STS3215_SERVO") {
            auto* sts_config = actuator->mutable_sts3215_config();
            sts_config->set_servo_id(std::stoi(new_servo_id_->value()));
            sts_config->set_move_time_in_ms(40);
            sts_config->set_move_speed(1500);
            sts_config->set_idle_position(2000);
        }
        
        updateActuatorDisplay();
        
        // Update form for next actuator
        new_actuator_name_->value(("sts_motor_" + std::to_string(actions->single_actions_size() + 1)).c_str());
        new_servo_id_->value(std::to_string(actions->single_actions_size() + 1).c_str());
    }
    
    void removeActuator() {
        auto* robot = config_->mutable_robot();
        auto* actions = robot->mutable_actions();
        if (actions->single_actions_size() > 0) {
            actions->mutable_single_actions()->RemoveLast();
            updateActuatorDisplay();
        }
    }
    
    void updateConfigFromUI() {
        config_->mutable_robot()->set_name(robot_name_input_->value());
        config_->mutable_robot()->set_id(std::stoi(robot_id_input_->value()));
        
        // Set robot type
        if (std::string(robot_type_choice_->text()) == "MANIPULATOR_ARM") {
            config_->mutable_robot()->set_robot_type(config::MANIPULATOR_ARM);
        } else if (std::string(robot_type_choice_->text()) == "ROBOT_INVALID") {
            config_->mutable_robot()->set_robot_type(config::ROBOT_INVALID);
        }
        
        // Set operation mode
        if (std::string(operation_mode_choice_->text()) == "MODE_INFERENCE") {
            config_->set_operation_mode(config::MODE_INFERENCE);
        } else if (std::string(operation_mode_choice_->text()) == "MODE_TELEOPERATE") {
            config_->set_operation_mode(config::MODE_TELEOPERATE);
        } else if (std::string(operation_mode_choice_->text()) == "MODE_TRAINING") {
            config_->set_operation_mode(config::MODE_TRAINING);
        } else if (std::string(operation_mode_choice_->text()) == "MODE_TEST") {
            config_->set_operation_mode(config::MODE_TEST);
        }
        
        config_->mutable_ai()->set_policy_name(policy_name_input_->value());
        config_->mutable_ai()->set_pretrained_model_path(model_path_input_->value());
        config_->mutable_ai()->set_act_dim(std::stoi(act_dim_input_->value()));
        config_->mutable_ai()->set_state_dim(std::stoi(state_dim_input_->value()));
    }
    
    void updateUIFromConfig() {
        robot_name_input_->value(config_->robot().name().c_str());
        robot_id_input_->value(std::to_string(config_->robot().id()).c_str());
        
        // Set robot type choice
        switch (config_->robot().robot_type()) {
            case config::MANIPULATOR_ARM:
                robot_type_choice_->value(0);
                break;
            case config::ROBOT_INVALID:
                robot_type_choice_->value(1);
                break;
        }
        
        // Set operation mode choice
        switch (config_->operation_mode()) {
            case config::MODE_INFERENCE:
                operation_mode_choice_->value(0);
                break;
            case config::MODE_TELEOPERATE:
                operation_mode_choice_->value(1);
                break;
            case config::MODE_TRAINING:
                operation_mode_choice_->value(2);
                break;
            case config::MODE_TEST:
                operation_mode_choice_->value(3);
                break;
        }
        
        policy_name_input_->value(config_->ai().policy_name().c_str());
        model_path_input_->value(config_->ai().pretrained_model_path().c_str());
        act_dim_input_->value(std::to_string(config_->ai().act_dim()).c_str());
        state_dim_input_->value(std::to_string(config_->ai().state_dim()).c_str());
    }
    
    void updateActuatorDisplay() {
        std::string display_text = "Current Actuators:\n\n";
        
        const auto& robot = config_->robot();
        const auto& actions = robot.actions();
        for (int i = 0; i < actions.single_actions_size(); ++i) {
            const auto& single_action = actions.single_actions(i);
            if (single_action.has_actuator()) {
                const auto& actuator = single_action.actuator();
                display_text += "Actuator " + std::to_string(i + 1) + ":\n";
                display_text += "  Name: " + actuator.actuator_name() + "\n";
                display_text += "  ID: " + std::to_string(actuator.id()) + "\n";
                display_text += "  Type: " + std::to_string(actuator.actuator_type()) + "\n";
                display_text += "  Subscribe Topic: " + single_action.subscribe_topic() + "\n";
                if (actuator.has_serial_config()) {
                    display_text += "  Comm Port: " + actuator.serial_config().port() + "\n";
                }
                if (actuator.has_sts3215_config()) {
                    display_text += "  Servo ID: " + std::to_string(actuator.sts3215_config().servo_id()) + "\n";
                }
                display_text += "\n";
            }
        }
        
        if (actions.single_actions_size() == 0) {
            display_text += "No actuators configured.\n";
        }
        
        actuator_buffer_->text(display_text.c_str());
    }
    
    void addPerception() {
        // Get the robot's perceptions container
        auto* robot = config_->mutable_robot();
        auto* perceptions = robot->mutable_perceptions();
        
        // Create a new single perception
        auto* single_perception = perceptions->add_single_perceptions();
        single_perception->set_node_id(1);
        single_perception->set_publish_topic("/perception_" + std::to_string(perceptions->single_perceptions_size()));
        
        std::string perception_type = new_perception_type_->text();
        
        if (perception_type == "CAMERA") {
            single_perception->set_perception_type(robot::perception::CAMERA);
            
            // Create camera within the perception_data oneof
            auto* camera = single_perception->mutable_camera();
            camera->set_camera_name(new_perception_name_->value());
            camera->set_id(perceptions->single_perceptions_size());
            camera->set_camera_type(robot::perception::OPENCV);
            camera->set_comm_type(robot::comm_interface::SERIAL);
            
            // Set serial communication config
            auto* serial_config = camera->mutable_serial_config();
            serial_config->set_id(camera->id());
            serial_config->set_port(new_perception_comm_port_->value());
            serial_config->set_baudrate(1000000);
            
            // Set OpenCV config
            auto* opencv_config = camera->mutable_opencv_config();
            opencv_config->set_id(camera->id());
            opencv_config->set_width(std::stoi(new_camera_width_->value()));
            opencv_config->set_height(std::stoi(new_camera_height_->value()));
            opencv_config->set_fps(std::stoi(new_camera_fps_->value()));
            
        } else if (perception_type == "ENCODER") {
            single_perception->set_perception_type(robot::perception::ENCODER);
            
            // Create encoder within the perception_data oneof
            auto* encoder = single_perception->mutable_encoder();
            encoder->set_encoder_name(new_perception_name_->value());
            encoder->set_id(perceptions->single_perceptions_size());
            encoder->set_encoder_type(robot::perception::STS3215_ENCODER);
            encoder->set_comm_type(robot::comm_interface::SERIAL);
            encoder->set_operational_lower_limit(0);
            encoder->set_operational_upper_limit(4095);
            encoder->set_publish_unnormalized_data(true);
            
            // Set serial communication config
            auto* serial_config = encoder->mutable_serial_config();
            serial_config->set_id(encoder->id());
            serial_config->set_port(new_perception_comm_port_->value());
            serial_config->set_baudrate(1000000);
            
            // Set STS3215 encoder config
            auto* sts_encoder_config = encoder->mutable_sts3215_encoder_config();
            sts_encoder_config->set_servo_id(std::stoi(new_encoder_servo_id_->value()));
        }
        
        updatePerceptionDisplay();
        
        // Update form for next perception
        new_perception_name_->value(("perception_" + std::to_string(perceptions->single_perceptions_size() + 1)).c_str());
    }
    
    void removePerception() {
        auto* robot = config_->mutable_robot();
        auto* perceptions = robot->mutable_perceptions();
        if (perceptions->single_perceptions_size() > 0) {
            perceptions->mutable_single_perceptions()->RemoveLast();
            updatePerceptionDisplay();
        }
    }
    
    void updatePerceptionDisplay() {
        std::string display_text = "Current Perceptions:\n\n";
        
        const auto& robot = config_->robot();
        const auto& perceptions = robot.perceptions();
        for (int i = 0; i < perceptions.single_perceptions_size(); ++i) {
            const auto& single_perception = perceptions.single_perceptions(i);
            display_text += "Perception " + std::to_string(i + 1) + ":\n";
            display_text += "  Publish Topic: " + single_perception.publish_topic() + "\n";
            
            if (single_perception.has_camera()) {
                const auto& camera = single_perception.camera();
                display_text += "  Type: Camera\n";
                display_text += "  Name: " + camera.camera_name() + "\n";
                display_text += "  ID: " + std::to_string(camera.id()) + "\n";
                if (camera.has_serial_config()) {
                    display_text += "  Comm Port: " + camera.serial_config().port() + "\n";
                }
                if (camera.has_opencv_config()) {
                    display_text += "  Width: " + std::to_string(camera.opencv_config().width()) + "\n";
                    display_text += "  Height: " + std::to_string(camera.opencv_config().height()) + "\n";
                    display_text += "  FPS: " + std::to_string(camera.opencv_config().fps()) + "\n";
                }
            } else if (single_perception.has_encoder()) {
                const auto& encoder = single_perception.encoder();
                display_text += "  Type: Encoder\n";
                display_text += "  Name: " + encoder.encoder_name() + "\n";
                display_text += "  ID: " + std::to_string(encoder.id()) + "\n";
                if (encoder.has_serial_config()) {
                    display_text += "  Comm Port: " + encoder.serial_config().port() + "\n";
                }
                if (encoder.has_sts3215_encoder_config()) {
                    display_text += "  Servo ID: " + std::to_string(encoder.sts3215_encoder_config().servo_id()) + "\n";
                }
            }
            display_text += "\n";
        }
        
        if (perceptions.single_perceptions_size() == 0) {
            display_text += "No perceptions configured.\n";
        }
        
        perception_buffer_->text(display_text.c_str());
    }
    
    void handleMenu() {
        // Menu handling is done through callbacks
    }
    

    
    ~AdvancedFLTKGUI() {
        delete actuator_buffer_;
        delete perception_buffer_;
    }
};

int main() {
    std::cout << "Starting Project Joshua Advanced FLTK GUI..." << std::endl;
    
    AdvancedFLTKGUI gui;
    gui.show();
    
    return Fl::run();
} 