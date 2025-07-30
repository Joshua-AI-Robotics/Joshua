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

// Local includes
#include "config/gui/proto_parser.h"

// Forward declaration
class DynamicFLTKGUI;

struct WidgetBinding {
    Fl_Widget* widget;
    const google::protobuf::FieldDescriptor* field;
    google::protobuf::Message* message;
    std::shared_ptr<config_gui::ProtoParser> parser;
    DynamicFLTKGUI* gui;  // Direct reference to GUI
};

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
    static const int TAB_FONT_SIZE = 18;

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
    
    // Widget bindings for proper memory management
    std::vector<std::unique_ptr<WidgetBinding>> widget_bindings_;
    
    // References to dynamic content areas for real-time updates
    Fl_Scroll* actions_scroll_;
    Fl_Box* actions_count_label_;
    Fl_Scroll* perceptions_scroll_;
    Fl_Box* perceptions_count_label_;
    int next_action_y_;
    int next_perception_y_;
    
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

    // Widget update callback
    static void widgetCallback(Fl_Widget* widget, void* data) {
        auto* binding = static_cast<WidgetBinding*>(data);
        
        if (auto* input = dynamic_cast<Fl_Input*>(widget)) {
            std::string value = input->value();
            binding->parser->setFieldValueFromString(binding->message, binding->field, value);
        } else if (auto* choice = dynamic_cast<Fl_Choice*>(widget)) {
            if (choice->value() >= 0) {
                std::string value = choice->text();
                binding->parser->setFieldValueFromString(binding->message, binding->field, value);
            }
        }
        
        if (binding->gui) {
            binding->gui->updateOutputDisplay();
        }
    }

    // Callback for adding repeated field items
    static void addRepeatedItemCallback(Fl_Widget* widget, void* data) {
        auto* binding = static_cast<WidgetBinding*>(data);
        binding->parser->addRepeatedFieldItem(binding->message, binding->field);
        
        if (binding->gui) {
            // Initialize newly created items with proper default values
            if (binding->field->name() == "single_actions") {
                auto* actions = static_cast<robot::action::Action*>(binding->message);
                int action_count = actions->single_actions_size();
                if (action_count > 0) {
                    auto* new_action = actions->mutable_single_actions(action_count - 1);
                    // Set default values to make nested fields visible
                    new_action->set_action_type(robot::action::ACTUATOR);
                    new_action->set_node_id(action_count);
                    new_action->set_subscribe_topic("/actuator_" + std::to_string(action_count));
                    
                    // Initialize the actuator with defaults
                    auto* actuator = new_action->mutable_actuator();
                    actuator->set_actuator_name("actuator_" + std::to_string(action_count));
                    actuator->set_id(action_count);
                    actuator->set_actuator_type(robot::action::STS3215_SERVO);
                    actuator->set_comm_type(robot::comm_interface::SERIAL);
                    actuator->set_physical_lower_limit(0);
                    actuator->set_physical_upper_limit(4095);
                    actuator->set_operational_lower_limit(500);
                    actuator->set_operational_upper_limit(3500);
                    
                    // Initialize serial config
                    auto* serial_config = actuator->mutable_serial_config();
                    serial_config->set_id(action_count);
                    serial_config->set_port("/dev/ttyUSB" + std::to_string(action_count - 1));
                    serial_config->set_baudrate(1000000);
                    
                    // Initialize STS3215 config
                    auto* sts_config = actuator->mutable_sts3215_config();
                    sts_config->set_servo_id(action_count);
                    sts_config->set_move_time_in_ms(1000);
                    sts_config->set_move_speed(1000);
                    sts_config->set_idle_position(2048);
                }
                binding->gui->addActionEditor();
            } else if (binding->field->name() == "single_perceptions") {
                auto* perceptions = static_cast<robot::perception::Perception*>(binding->message);
                int perception_count = perceptions->single_perceptions_size();
                if (perception_count > 0) {
                    auto* new_perception = perceptions->mutable_single_perceptions(perception_count - 1);
                    
                    // Alternate between camera and encoder for variety (or default to camera)
                    if (perception_count % 2 == 1) {
                        // Set camera as default for odd-numbered perceptions
                        new_perception->set_perception_type(robot::perception::CAMERA);
                        new_perception->set_node_id(perception_count);
                        new_perception->set_publish_topic("/camera_" + std::to_string(perception_count));
                        
                        // Initialize the camera with defaults
                        auto* camera = new_perception->mutable_camera();
                        camera->set_camera_name("camera_" + std::to_string(perception_count));
                        camera->set_id(perception_count);
                        camera->set_camera_type(robot::perception::OPENCV);
                        camera->set_comm_type(robot::comm_interface::SERIAL);
                        
                        // Initialize OpenCV config
                        auto* opencv_config = camera->mutable_opencv_config();
                        opencv_config->set_id(perception_count - 1);
                        opencv_config->set_width(640);
                        opencv_config->set_height(480);
                        opencv_config->set_fps(30);
                        
                        // Initialize serial config for camera
                        auto* serial_config = camera->mutable_serial_config();
                        serial_config->set_id(perception_count);
                        serial_config->set_port("/dev/video" + std::to_string(perception_count - 1));
                        serial_config->set_baudrate(115200);
                    } else {
                        // Set encoder as default for even-numbered perceptions
                        new_perception->set_perception_type(robot::perception::ENCODER);
                        new_perception->set_node_id(perception_count);
                        new_perception->set_publish_topic("/encoder_" + std::to_string(perception_count));
                        
                        // Initialize the encoder with defaults
                        auto* encoder = new_perception->mutable_encoder();
                        encoder->set_encoder_name("encoder_" + std::to_string(perception_count));
                        encoder->set_id(perception_count);
                        encoder->set_encoder_type(robot::perception::STS3215_ENCODER);
                        encoder->set_comm_type(robot::comm_interface::SERIAL);
                        encoder->set_operational_lower_limit(0);
                        encoder->set_operational_upper_limit(4095);
                        encoder->set_publish_unnormalized_data(false);
                        
                        // Initialize serial config for encoder
                        auto* serial_config = encoder->mutable_serial_config();
                        serial_config->set_id(perception_count);
                        serial_config->set_port("/dev/ttyUSB" + std::to_string(perception_count - 1));
                        serial_config->set_baudrate(1000000);
                        
                        // Initialize STS3215 encoder config
                        auto* sts_config = encoder->mutable_sts3215_encoder_config();
                        sts_config->set_servo_id(perception_count);
                    }
                }
                binding->gui->addPerceptionEditor();
            }
            
            binding->gui->updateOutputDisplay();
            std::cout << "Added new item to " << binding->field->name() << std::endl;
        }
    }

    // Callback for removing repeated field items
    static void removeRepeatedItemCallback(Fl_Widget* widget, void* data) {
        auto* binding = static_cast<WidgetBinding*>(data);
        binding->parser->removeLastRepeatedFieldItem(binding->message, binding->field);
        
        if (binding->gui) {
            binding->gui->updateOutputDisplay();
            
            // Update count labels
            if (binding->field->name() == "single_actions") {
                binding->gui->updateActionsCount();
            } else if (binding->field->name() == "single_perceptions") {
                binding->gui->updatePerceptionsCount();
            }
            
            std::cout << "Removed last item from " << binding->field->name() << std::endl;
            std::cout << "Note: Please refresh the view (Ctrl+R) to see the removal in the UI" << std::endl;
        }
    }

public:
    DynamicFLTKGUI() {
        // Initialize parser
        parser_ = std::make_shared<config_gui::ProtoParser>();
        
        // Initialize references
        actions_scroll_ = nullptr;
        actions_count_label_ = nullptr;
        perceptions_scroll_ = nullptr;
        perceptions_count_label_ = nullptr;
        next_action_y_ = 0;
        next_perception_y_ = 0;
        
        // Calculate proportional dimensions
        content_width_ = WINDOW_WIDTH - 2 * MARGIN;
        content_height_ = WINDOW_HEIGHT - MENU_HEIGHT - 2 * MARGIN;
        tab_area_width_ = content_width_;
        tab_area_height_ = content_height_ - 200; // Reserve space for output
        tab_content_width_ = tab_area_width_ - 2 * MARGIN;
        tab_content_height_ = tab_area_height_ - TAB_HEIGHT - 2 * MARGIN;
        
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
        
        // Create main window (resizable)
        window_ = new Fl_Window(WINDOW_WIDTH, WINDOW_HEIGHT, TITLE.c_str());
        window_->resizable(window_);
        window_->size_range(800, 600);
        
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
        output_display_ = new Fl_Text_Display(MARGIN, MENU_HEIGHT + MARGIN + tab_area_height_ + MARGIN, 
                                              WINDOW_WIDTH - 2 * MARGIN, 180, "Configuration Output");
        output_display_->buffer(output_buffer_);
        output_display_->textfont(FL_COURIER);
        output_display_->textsize(TEXT_DISPLAY_FONT_SIZE);
        
        // Update output with current config
        updateOutputDisplay();
        
        window_->end();
    }
    
    void show() {
        window_->show();
    }
    
    void recreateTabs() {
        // Clear existing tabs and widget bindings
        widget_bindings_.clear();
        
        // Remove all child widgets from tabs
        while (tabs_->children() > 0) {
            Fl_Widget* child = tabs_->child(0);
            tabs_->remove(child);
            delete child;
        }
        
        // Recreate tabs
        createDynamicTabs();
        
        // Ensure tabs are properly set up
        tabs_->end();
        
        // Redraw everything
        tabs_->redraw();
        window_->redraw();
        
        std::cout << "Tabs recreated successfully" << std::endl;
    }
    
    void createDynamicTabs() {
        // Parse the config message to get all fields (including empty ones)
        createMainConfigTab();
        createRobotTab();
        createAITab();
        createAllFieldsTab();
    }
    
    void createMainConfigTab() {
        Fl_Group* group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, 
                                     tab_content_width_, tab_content_height_, "Main Config");
        group->labelsize(TAB_FONT_SIZE);
        
        Fl_Scroll* scroll = new Fl_Scroll(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, 
                                         tab_content_width_ - MARGIN * 2, tab_content_height_ - MARGIN * 2);
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 3, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2, 
                                  tab_content_width_ - MARGIN * 3, INPUT_HEIGHT, "Main Configuration");
        title->labelsize(TITLE_FONT_SIZE);
        title->labelfont(FL_BOLD);
        
        // Get all top-level fields from the descriptor (not just set ones)
        const google::protobuf::Descriptor* desc = config_->GetDescriptor();
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2 + INPUT_HEIGHT + SECTION_SPACING;
        
        for (int i = 0; i < desc->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = desc->field(i);
            if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                createFieldWidget(field, current_y, scroll, config_.get());
                current_y += FIELD_SPACING;
            }
        }
        
        scroll->end();
        group->end();
    }
    
    void createRobotTab() {
        Fl_Group* group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, 
                                     tab_content_width_, tab_content_height_, "Robot");
        group->labelsize(TAB_FONT_SIZE);
        
        // Create nested tabs for robot sections
        Fl_Tabs* robot_tabs = new Fl_Tabs(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN,
                                         tab_content_width_ - MARGIN * 2, tab_content_height_ - MARGIN * 2);
        
        // Basic Info Tab
        createRobotBasicTab(robot_tabs);
        
        // Actions Tab
        createActionsTab(robot_tabs);
        
        // Perceptions Tab
        createPerceptionsTab(robot_tabs);
        
        robot_tabs->end();
        group->end();
    }
    
    void createRobotBasicTab(Fl_Tabs* parent_tabs) {
        Fl_Group* group = new Fl_Group(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT,
                                     tab_content_width_ - MARGIN * 2, tab_content_height_ - MARGIN * 2 - TAB_HEIGHT, 
                                     "Basic Info");
        group->labelsize(TAB_FONT_SIZE);
        
        Fl_Scroll* scroll = new Fl_Scroll(MARGIN * 3, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN,
                                         tab_content_width_ - MARGIN * 4, tab_content_height_ - MARGIN * 4 - TAB_HEIGHT);
        
        auto* robot = config_->mutable_robot();
        const google::protobuf::Descriptor* desc = robot->GetDescriptor();
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2;
        
        for (int i = 0; i < desc->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = desc->field(i);
            if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                createFieldWidget(field, current_y, scroll, robot);
                current_y += FIELD_SPACING;
            }
        }
        
        scroll->end();
        group->end();
    }
    
    void createActionsTab(Fl_Tabs* parent_tabs) {
        Fl_Group* group = new Fl_Group(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT,
                                     tab_content_width_ - MARGIN * 2, tab_content_height_ - MARGIN * 2 - TAB_HEIGHT, 
                                     "Actions");
        group->labelsize(TAB_FONT_SIZE);
        
        Fl_Scroll* scroll = new Fl_Scroll(MARGIN * 3, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN,
                                         tab_content_width_ - MARGIN * 4, tab_content_height_ - MARGIN * 4 - TAB_HEIGHT);
        actions_scroll_ = scroll;  // Store reference for dynamic updates
        
        auto* robot = config_->mutable_robot();
        auto* actions = robot->mutable_actions();
        
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2;
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 4, current_y, LABEL_WIDTH * 2, INPUT_HEIGHT, "Actions Configuration");
        title->labelsize(SECTION_FONT_SIZE);
        title->labelfont(FL_BOLD);
        current_y += FIELD_SPACING;
        
        // Show current count
        int action_count = actions->single_actions_size();
        Fl_Box* count_label = new Fl_Box(MARGIN * 4, current_y, LABEL_WIDTH * 2, INPUT_HEIGHT, 
                                        strdup(("Current Actions: " + std::to_string(action_count)).c_str()));
        count_label->labelsize(LABEL_FONT_SIZE);
        actions_count_label_ = count_label;  // Store reference for dynamic updates
        current_y += FIELD_SPACING;
        
        // Add/Remove buttons
        const google::protobuf::FieldDescriptor* single_actions_field = actions->GetDescriptor()->FindFieldByName("single_actions");
        
        Fl_Button* add_btn = new Fl_Button(MARGIN * 4, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Add Action");
        add_btn->labelsize(LABEL_FONT_SIZE);
        
        auto add_binding = std::make_unique<WidgetBinding>();
        add_binding->widget = add_btn;
        add_binding->field = single_actions_field;
        add_binding->message = actions;
        add_binding->parser = parser_;
        add_binding->gui = this;
        
        add_btn->callback(addRepeatedItemCallback, add_binding.get());
        widget_bindings_.push_back(std::move(add_binding));
        
        Fl_Button* remove_btn = new Fl_Button(MARGIN * 4 + BUTTON_WIDTH + MARGIN, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Remove Last");
        remove_btn->labelsize(LABEL_FONT_SIZE);
        
        auto remove_binding = std::make_unique<WidgetBinding>();
        remove_binding->widget = remove_btn;
        remove_binding->field = single_actions_field;
        remove_binding->message = actions;
        remove_binding->parser = parser_;
        remove_binding->gui = this;
        
        remove_btn->callback(removeRepeatedItemCallback, remove_binding.get());
        widget_bindings_.push_back(std::move(remove_binding));
        
        current_y += FIELD_SPACING * 2;
        
        // Show existing actions
        for (int i = 0; i < action_count; ++i) {
            current_y += createActionEditor(i, current_y, scroll, actions);
        }
        
        next_action_y_ = current_y;  // Store position for next action
        
        scroll->end();
        group->end();
    }
    
    void createPerceptionsTab(Fl_Tabs* parent_tabs) {
        Fl_Group* group = new Fl_Group(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT,
                                     tab_content_width_ - MARGIN * 2, tab_content_height_ - MARGIN * 2 - TAB_HEIGHT, 
                                     "Perceptions");
        group->labelsize(TAB_FONT_SIZE);
        
        Fl_Scroll* scroll = new Fl_Scroll(MARGIN * 3, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN,
                                         tab_content_width_ - MARGIN * 4, tab_content_height_ - MARGIN * 4 - TAB_HEIGHT);
        perceptions_scroll_ = scroll;  // Store reference for dynamic updates
        
        auto* robot = config_->mutable_robot();
        auto* perceptions = robot->mutable_perceptions();
        
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2;
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 4, current_y, LABEL_WIDTH * 2, INPUT_HEIGHT, "Perceptions Configuration");
        title->labelsize(SECTION_FONT_SIZE);
        title->labelfont(FL_BOLD);
        current_y += FIELD_SPACING;
        
        // Show current count
        int perception_count = perceptions->single_perceptions_size();
        Fl_Box* count_label = new Fl_Box(MARGIN * 4, current_y, LABEL_WIDTH * 2, INPUT_HEIGHT, 
                                        strdup(("Current Perceptions: " + std::to_string(perception_count)).c_str()));
        count_label->labelsize(LABEL_FONT_SIZE);
        perceptions_count_label_ = count_label;  // Store reference for dynamic updates
        current_y += FIELD_SPACING;
        
        // Add/Remove buttons
        const google::protobuf::FieldDescriptor* single_perceptions_field = perceptions->GetDescriptor()->FindFieldByName("single_perceptions");
        
        Fl_Button* add_btn = new Fl_Button(MARGIN * 4, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Add Perception");
        add_btn->labelsize(LABEL_FONT_SIZE);
        
        auto add_binding = std::make_unique<WidgetBinding>();
        add_binding->widget = add_btn;
        add_binding->field = single_perceptions_field;
        add_binding->message = perceptions;
        add_binding->parser = parser_;
        add_binding->gui = this;
        
        add_btn->callback(addRepeatedItemCallback, add_binding.get());
        widget_bindings_.push_back(std::move(add_binding));
        
        Fl_Button* remove_btn = new Fl_Button(MARGIN * 4 + BUTTON_WIDTH + MARGIN, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Remove Last");
        remove_btn->labelsize(LABEL_FONT_SIZE);
        
        auto remove_binding = std::make_unique<WidgetBinding>();
        remove_binding->widget = remove_btn;
        remove_binding->field = single_perceptions_field;
        remove_binding->message = perceptions;
        remove_binding->parser = parser_;
        remove_binding->gui = this;
        
        remove_btn->callback(removeRepeatedItemCallback, remove_binding.get());
        widget_bindings_.push_back(std::move(remove_binding));
        
        current_y += FIELD_SPACING * 2;
        
        // Show existing perceptions
        for (int i = 0; i < perception_count; ++i) {
            current_y += createPerceptionEditor(i, current_y, scroll, perceptions);
        }
        
        next_perception_y_ = current_y;  // Store position for next perception
        
        scroll->end();
        group->end();
    }
    
    int createActionEditor(int index, int start_y, Fl_Group* parent, google::protobuf::Message* actions_msg) {
        auto* single_action = parser_->getRepeatedFieldItem(actions_msg, 
                                                           actions_msg->GetDescriptor()->FindFieldByName("single_actions"), 
                                                           index);
        if (!single_action) return FIELD_SPACING;
        
        // Section header
        Fl_Box* header = new Fl_Box(MARGIN * 4, start_y, LABEL_WIDTH * 2, INPUT_HEIGHT, 
                                   strdup(("Action " + std::to_string(index + 1)).c_str()));
        header->labelsize(SECTION_FONT_SIZE);
        header->labelfont(FL_BOLD);
        header->box(FL_THIN_DOWN_BOX);
        
        int current_y = start_y + FIELD_SPACING;
        const google::protobuf::Descriptor* desc = single_action->GetDescriptor();
        
        // Create basic fields first
        for (int i = 0; i < desc->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = desc->field(i);
            if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE && 
                !field->containing_oneof()) {
                createFieldWidget(field, current_y, parent, single_action);
                current_y += FIELD_SPACING;
            }
        }
        
        // Add space before oneof sections
        current_y += FIELD_SPACING / 2;
        
        // Handle oneof fields (action_data)
        const google::protobuf::FieldDescriptor* action_type_field = desc->FindFieldByName("action_type");
        if (action_type_field) {
            std::string action_type_value = parser_->getFieldValueAsString(*single_action, action_type_field);
            
            // Show actuator fields if action_type is ACTUATOR
            if (action_type_value == "ACTUATOR") {
                current_y += createActuatorSection(current_y, parent, single_action);
            }
        }
        
        return current_y - start_y + SECTION_SPACING;
    }
    
    int createActuatorSection(int start_y, Fl_Group* parent, google::protobuf::Message* single_action) {
        // Section title
        Fl_Box* actuator_title = new Fl_Box(MARGIN * 4, start_y, LABEL_WIDTH * 2, INPUT_HEIGHT, "Actuator Configuration");
        actuator_title->labelsize(SECTION_FONT_SIZE);
        actuator_title->labelfont(FL_BOLD);
        actuator_title->box(FL_THIN_UP_BOX);
        
        int current_y = start_y + FIELD_SPACING;
        
        // Get or create the actuator message
        const google::protobuf::FieldDescriptor* actuator_field = single_action->GetDescriptor()->FindFieldByName("actuator");
        if (actuator_field) {
            google::protobuf::Message* actuator = single_action->GetReflection()->MutableMessage(single_action, actuator_field);
            const google::protobuf::Descriptor* actuator_desc = actuator->GetDescriptor();
            
            // Create fields for actuator
            for (int i = 0; i < actuator_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = actuator_desc->field(i);
                if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                    createFieldWidget(field, current_y, parent, actuator);
                    current_y += FIELD_SPACING;
                }
            }
            
            // Handle actuator oneof fields (comm_config, action_config)
            current_y += createActuatorOneofSections(current_y, parent, actuator);
        }
        
        return current_y - start_y;
    }
    
    int createActuatorOneofSections(int start_y, Fl_Group* parent, google::protobuf::Message* actuator) {
        int current_y = start_y;
        
        // Serial Config section
        Fl_Box* serial_title = new Fl_Box(MARGIN * 5, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Serial Configuration");
        serial_title->labelsize(LABEL_FONT_SIZE);
        serial_title->labelfont(FL_BOLD);
        current_y += FIELD_SPACING;
        
        const google::protobuf::FieldDescriptor* serial_config_field = actuator->GetDescriptor()->FindFieldByName("serial_config");
        if (serial_config_field) {
            google::protobuf::Message* serial_config = actuator->GetReflection()->MutableMessage(actuator, serial_config_field);
            const google::protobuf::Descriptor* serial_desc = serial_config->GetDescriptor();
            
            for (int i = 0; i < serial_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = serial_desc->field(i);
                createFieldWidget(field, current_y, parent, serial_config);
                current_y += FIELD_SPACING;
            }
        }
        
        current_y += FIELD_SPACING / 2;
        
        // STS3215 Config section
        Fl_Box* sts_title = new Fl_Box(MARGIN * 5, current_y, LABEL_WIDTH, INPUT_HEIGHT, "STS3215 Configuration");
        sts_title->labelsize(LABEL_FONT_SIZE);
        sts_title->labelfont(FL_BOLD);
        current_y += FIELD_SPACING;
        
        const google::protobuf::FieldDescriptor* sts_config_field = actuator->GetDescriptor()->FindFieldByName("sts3215_config");
        if (sts_config_field) {
            google::protobuf::Message* sts_config = actuator->GetReflection()->MutableMessage(actuator, sts_config_field);
            const google::protobuf::Descriptor* sts_desc = sts_config->GetDescriptor();
            
            for (int i = 0; i < sts_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = sts_desc->field(i);
                createFieldWidget(field, current_y, parent, sts_config);
                current_y += FIELD_SPACING;
            }
        }
        
        return current_y - start_y;
    }
    
    int createPerceptionEditor(int index, int start_y, Fl_Group* parent, google::protobuf::Message* perceptions_msg) {
        auto* single_perception = parser_->getRepeatedFieldItem(perceptions_msg,
                                                              perceptions_msg->GetDescriptor()->FindFieldByName("single_perceptions"),
                                                              index);
        if (!single_perception) return FIELD_SPACING;
        
        // Section header with more spacing to avoid overlap
        Fl_Box* header = new Fl_Box(MARGIN * 4, start_y, LABEL_WIDTH * 2, INPUT_HEIGHT, 
                                   strdup(("Perception " + std::to_string(index + 1)).c_str()));
        header->labelsize(SECTION_FONT_SIZE);
        header->labelfont(FL_BOLD);
        header->box(FL_THIN_DOWN_BOX);
        
        int current_y = start_y + FIELD_SPACING;
        const google::protobuf::Descriptor* desc = single_perception->GetDescriptor();
        
        // Create basic fields first
        for (int i = 0; i < desc->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = desc->field(i);
            if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE && 
                !field->containing_oneof()) {
                createFieldWidget(field, current_y, parent, single_perception);
                current_y += FIELD_SPACING;
            }
        }
        
        // Add space before oneof sections
        current_y += FIELD_SPACING / 2;
        
        // Handle oneof fields (perception_data)
        const google::protobuf::FieldDescriptor* perception_type_field = desc->FindFieldByName("perception_type");
        if (perception_type_field) {
            std::string perception_type_value = parser_->getFieldValueAsString(*single_perception, perception_type_field);
            
            // Show camera fields if perception_type is CAMERA
            if (perception_type_value == "CAMERA") {
                current_y += createCameraSection(current_y, parent, single_perception);
            }
            // Show encoder fields if perception_type is ENCODER
            else if (perception_type_value == "ENCODER") {
                current_y += createEncoderSection(current_y, parent, single_perception);
            }
        }
        
        // Add extra spacing between perception editors to prevent overlap
        return current_y - start_y + SECTION_SPACING * 2;
    }
    
    int createCameraSection(int start_y, Fl_Group* parent, google::protobuf::Message* single_perception) {
        // Section title
        Fl_Box* camera_title = new Fl_Box(MARGIN * 4, start_y, LABEL_WIDTH * 2, INPUT_HEIGHT, "Camera Configuration");
        camera_title->labelsize(SECTION_FONT_SIZE);
        camera_title->labelfont(FL_BOLD);
        camera_title->box(FL_THIN_UP_BOX);
        
        int current_y = start_y + FIELD_SPACING;
        
        // Get or create the camera message
        const google::protobuf::FieldDescriptor* camera_field = single_perception->GetDescriptor()->FindFieldByName("camera");
        if (camera_field) {
            google::protobuf::Message* camera = single_perception->GetReflection()->MutableMessage(single_perception, camera_field);
            const google::protobuf::Descriptor* camera_desc = camera->GetDescriptor();
            
            // Create fields for camera
            for (int i = 0; i < camera_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = camera_desc->field(i);
                if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                    createFieldWidget(field, current_y, parent, camera);
                    current_y += FIELD_SPACING;
                }
            }
            
            // Handle camera oneof fields (OpenCV config, etc.)
            current_y += createCameraOneofSections(current_y, parent, camera);
        }
        
        return current_y - start_y;
    }
    
    int createEncoderSection(int start_y, Fl_Group* parent, google::protobuf::Message* single_perception) {
        // Section title
        Fl_Box* encoder_title = new Fl_Box(MARGIN * 4, start_y, LABEL_WIDTH * 2, INPUT_HEIGHT, "Encoder Configuration");
        encoder_title->labelsize(SECTION_FONT_SIZE);
        encoder_title->labelfont(FL_BOLD);
        encoder_title->box(FL_THIN_UP_BOX);
        
        int current_y = start_y + FIELD_SPACING;
        
        // Get or create the encoder message
        const google::protobuf::FieldDescriptor* encoder_field = single_perception->GetDescriptor()->FindFieldByName("encoder");
        if (encoder_field) {
            google::protobuf::Message* encoder = single_perception->GetReflection()->MutableMessage(single_perception, encoder_field);
            const google::protobuf::Descriptor* encoder_desc = encoder->GetDescriptor();
            
            // Create fields for encoder
            for (int i = 0; i < encoder_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = encoder_desc->field(i);
                if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                    createFieldWidget(field, current_y, parent, encoder);
                    current_y += FIELD_SPACING;
                }
            }
            
            // Handle encoder oneof fields
            current_y += createEncoderOneofSections(current_y, parent, encoder);
        }
        
        return current_y - start_y;
    }
    
    int createCameraOneofSections(int start_y, Fl_Group* parent, google::protobuf::Message* camera) {
        int current_y = start_y;
        
        // OpenCV Config section
        Fl_Box* opencv_title = new Fl_Box(MARGIN * 5, current_y, LABEL_WIDTH, INPUT_HEIGHT, "OpenCV Configuration");
        opencv_title->labelsize(LABEL_FONT_SIZE);
        opencv_title->labelfont(FL_BOLD);
        current_y += FIELD_SPACING;
        
        const google::protobuf::FieldDescriptor* opencv_config_field = camera->GetDescriptor()->FindFieldByName("opencv_config");
        if (opencv_config_field) {
            google::protobuf::Message* opencv_config = camera->GetReflection()->MutableMessage(camera, opencv_config_field);
            const google::protobuf::Descriptor* opencv_desc = opencv_config->GetDescriptor();
            
            for (int i = 0; i < opencv_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = opencv_desc->field(i);
                createFieldWidget(field, current_y, parent, opencv_config);
                current_y += FIELD_SPACING;
            }
        }
        
        return current_y - start_y;
    }
    
    int createEncoderOneofSections(int start_y, Fl_Group* parent, google::protobuf::Message* encoder) {
        int current_y = start_y;
        
        // Serial Config section
        Fl_Box* serial_title = new Fl_Box(MARGIN * 5, current_y, LABEL_WIDTH, INPUT_HEIGHT, "Serial Configuration");
        serial_title->labelsize(LABEL_FONT_SIZE);
        serial_title->labelfont(FL_BOLD);
        current_y += FIELD_SPACING;
        
        const google::protobuf::FieldDescriptor* serial_config_field = encoder->GetDescriptor()->FindFieldByName("serial_config");
        if (serial_config_field) {
            google::protobuf::Message* serial_config = encoder->GetReflection()->MutableMessage(encoder, serial_config_field);
            const google::protobuf::Descriptor* serial_desc = serial_config->GetDescriptor();
            
            for (int i = 0; i < serial_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = serial_desc->field(i);
                createFieldWidget(field, current_y, parent, serial_config);
                current_y += FIELD_SPACING;
            }
        }
        
        current_y += FIELD_SPACING / 2;
        
        // STS3215 Encoder Config section
        Fl_Box* sts_title = new Fl_Box(MARGIN * 5, current_y, LABEL_WIDTH, INPUT_HEIGHT, "STS3215 Encoder Configuration");
        sts_title->labelsize(LABEL_FONT_SIZE);
        sts_title->labelfont(FL_BOLD);
        current_y += FIELD_SPACING;
        
        const google::protobuf::FieldDescriptor* sts_config_field = encoder->GetDescriptor()->FindFieldByName("sts3215_encoder_config");
        if (sts_config_field) {
            google::protobuf::Message* sts_config = encoder->GetReflection()->MutableMessage(encoder, sts_config_field);
            const google::protobuf::Descriptor* sts_desc = sts_config->GetDescriptor();
            
            for (int i = 0; i < sts_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* field = sts_desc->field(i);
                createFieldWidget(field, current_y, parent, sts_config);
                current_y += FIELD_SPACING;
            }
        }
        
        return current_y - start_y;
    }
    
    void createAITab() {
        Fl_Group* group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, 
                                     tab_content_width_, tab_content_height_, "AI");
        group->labelsize(TAB_FONT_SIZE);
        
        Fl_Scroll* scroll = new Fl_Scroll(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, 
                                         tab_content_width_ - MARGIN * 2, tab_content_height_ - MARGIN * 2);
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 3, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2, 
                                  tab_content_width_ - MARGIN * 3, INPUT_HEIGHT, "AI Configuration");
        title->labelsize(TITLE_FONT_SIZE);
        title->labelfont(FL_BOLD);
        
        auto* ai = config_->mutable_ai();
        const google::protobuf::Descriptor* desc = ai->GetDescriptor();
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2 + INPUT_HEIGHT + SECTION_SPACING;
        
        for (int i = 0; i < desc->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = desc->field(i);
            createFieldWidget(field, current_y, scroll, ai);
            current_y += FIELD_SPACING;
        }
        
        scroll->end();
        group->end();
    }
    
    void createAllFieldsTab() {
        Fl_Group* group = new Fl_Group(MARGIN, MENU_HEIGHT + MARGIN + TAB_HEIGHT, 
                                     tab_content_width_, tab_content_height_, "All Fields");
        group->labelsize(TAB_FONT_SIZE);
        
        Fl_Scroll* scroll = new Fl_Scroll(MARGIN * 2, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN, 
                                         tab_content_width_ - MARGIN * 2, tab_content_height_ - MARGIN * 2);
        
        // Title
        Fl_Box* title = new Fl_Box(MARGIN * 3, MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2, 
                                  tab_content_width_ - MARGIN * 3, INPUT_HEIGHT, "All Configuration Fields");
        title->labelsize(TITLE_FONT_SIZE);
        title->labelfont(FL_BOLD);
        
        int current_y = MENU_HEIGHT + MARGIN + TAB_HEIGHT + MARGIN * 2 + INPUT_HEIGHT + SECTION_SPACING;
        current_y += createCompleteConfigView(current_y, scroll, config_.get());
        
        scroll->end();
        group->end();
    }
    
    void createFieldWidget(const google::protobuf::FieldDescriptor* field, int y, Fl_Group* parent, 
                          google::protobuf::Message* message) {
        std::string display_name = parser_->getDisplayName(field->name());
        
        // Create label
        Fl_Box* label = new Fl_Box(MARGIN * 3, y, LABEL_WIDTH, INPUT_HEIGHT, 
                                  strdup((display_name + ":").c_str()));
        label->labelsize(LABEL_FONT_SIZE);
        label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        
        // Get current value
        std::string current_value = parser_->getFieldValueAsString(*message, field);
        
        // Create appropriate widget based on field type
        if (field->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
            Fl_Choice* choice = new Fl_Choice(MARGIN * 3 + LABEL_WIDTH + MARGIN, y, 
                                            INPUT_WIDTH, INPUT_HEIGHT);
            choice->textsize(INPUT_FONT_SIZE);
            
            // Add enum values
            std::vector<std::string> enum_values = parser_->getEnumValues(field);
            int selected_idx = 0;
            for (size_t i = 0; i < enum_values.size(); ++i) {
                choice->add(enum_values[i].c_str());
                if (current_value == enum_values[i]) {
                    selected_idx = i;
                }
            }
            choice->value(selected_idx);
            
            // Create and store binding
            auto binding = std::make_unique<WidgetBinding>();
            binding->widget = choice;
            binding->field = field;
            binding->message = message;
            binding->parser = parser_;
            binding->gui = this;
            
            choice->callback(widgetCallback, binding.get());
            widget_bindings_.push_back(std::move(binding));
            
        } else if (field->type() == google::protobuf::FieldDescriptor::TYPE_BOOL) {
            Fl_Choice* choice = new Fl_Choice(MARGIN * 3 + LABEL_WIDTH + MARGIN, y, 
                                            INPUT_WIDTH, INPUT_HEIGHT);
            choice->textsize(INPUT_FONT_SIZE);
            choice->add("false");
            choice->add("true");
            choice->value(current_value == "true" ? 1 : 0);
            
            // Create and store binding
            auto binding = std::make_unique<WidgetBinding>();
            binding->widget = choice;
            binding->field = field;
            binding->message = message;
            binding->parser = parser_;
            binding->gui = this;
            
            choice->callback(widgetCallback, binding.get());
            widget_bindings_.push_back(std::move(binding));
            
        } else {
            // String, numeric types
            Fl_Input* input = new Fl_Input(MARGIN * 3 + LABEL_WIDTH + MARGIN, y, 
                                         INPUT_WIDTH, INPUT_HEIGHT);
            input->textsize(INPUT_FONT_SIZE);
            input->value(current_value.c_str());
            
            // Set input type for numeric fields
            if (field->type() == google::protobuf::FieldDescriptor::TYPE_INT32 ||
                field->type() == google::protobuf::FieldDescriptor::TYPE_INT64 ||
                field->type() == google::protobuf::FieldDescriptor::TYPE_UINT32 ||
                field->type() == google::protobuf::FieldDescriptor::TYPE_UINT64) {
                input->type(FL_INT_INPUT);
            } else if (field->type() == google::protobuf::FieldDescriptor::TYPE_FLOAT ||
                      field->type() == google::protobuf::FieldDescriptor::TYPE_DOUBLE) {
                input->type(FL_FLOAT_INPUT);
            }
            
            // Create and store binding
            auto binding = std::make_unique<WidgetBinding>();
            binding->widget = input;
            binding->field = field;
            binding->message = message;
            binding->parser = parser_;
            binding->gui = this;
            
            input->callback(widgetCallback, binding.get());
            widget_bindings_.push_back(std::move(binding));
        }
    }
    
    int createMessageFieldSection(const google::protobuf::FieldDescriptor* field, int start_y, 
                                 Fl_Group* parent, google::protobuf::Message* message) {
        std::string display_name = parser_->getDisplayName(field->name());
        
        // Section title
        Fl_Box* section_title = new Fl_Box(MARGIN * 3, start_y, LABEL_WIDTH * 2, INPUT_HEIGHT, 
                                          strdup((display_name + " Section").c_str()));
        section_title->labelsize(SECTION_FONT_SIZE);
        section_title->labelfont(FL_BOLD);
        section_title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
        
        int current_y = start_y + FIELD_SPACING;
        
        if (field->is_repeated()) {
            // Handle repeated fields like single_actions
            const google::protobuf::Reflection* reflection = message->GetReflection();
            int count = reflection->FieldSize(*message, field);
            
            Fl_Box* count_label = new Fl_Box(MARGIN * 4, current_y, LABEL_WIDTH, INPUT_HEIGHT, 
                                            strdup(("Current " + display_name + ": " + std::to_string(count)).c_str()));
            count_label->labelsize(LABEL_FONT_SIZE);
            count_label->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
            current_y += FIELD_SPACING;
            
            // Add button for repeated fields
            Fl_Button* add_btn = new Fl_Button(MARGIN * 4, current_y, BUTTON_WIDTH, BUTTON_HEIGHT, "Add New");
            add_btn->labelsize(LABEL_FONT_SIZE);
            current_y += FIELD_SPACING * 2;
            
        } else {
            // Handle singular message fields
            google::protobuf::Message* sub_message = message->GetReflection()->MutableMessage(message, field);
            const google::protobuf::Descriptor* sub_desc = sub_message->GetDescriptor();
            
            for (int i = 0; i < sub_desc->field_count(); ++i) {
                const google::protobuf::FieldDescriptor* sub_field = sub_desc->field(i);
                if (sub_field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                    createFieldWidget(sub_field, current_y, parent, sub_message);
                    current_y += FIELD_SPACING;
                }
            }
        }
        
        return current_y - start_y + SECTION_SPACING;
    }
    
    int createCompleteConfigView(int start_y, Fl_Group* parent, google::protobuf::Message* message) {
        const google::protobuf::Descriptor* desc = message->GetDescriptor();
        int current_y = start_y;
        
        for (int i = 0; i < desc->field_count(); ++i) {
            const google::protobuf::FieldDescriptor* field = desc->field(i);
            if (field->type() != google::protobuf::FieldDescriptor::TYPE_MESSAGE) {
                createFieldWidget(field, current_y, parent, message);
                current_y += FIELD_SPACING;
            } else {
                current_y += createMessageFieldSection(field, current_y, parent, message);
            }
        }
        
        return current_y - start_y;
    }
    
    void updateWidgetValues() {
        // Update all widget values to reflect current config state
        for (auto& binding : widget_bindings_) {
            std::string current_value = parser_->getFieldValueAsString(*binding->message, binding->field);
            
            if (auto* input = dynamic_cast<Fl_Input*>(binding->widget)) {
                input->value(current_value.c_str());
            } else if (auto* choice = dynamic_cast<Fl_Choice*>(binding->widget)) {
                if (binding->field->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
                    // Find the matching enum value
                    for (int i = 0; i < choice->size(); ++i) {
                        if (choice->text(i) && current_value == choice->text(i)) {
                            choice->value(i);
                            break;
                        }
                    }
                } else if (binding->field->type() == google::protobuf::FieldDescriptor::TYPE_BOOL) {
                    choice->value(current_value == "true" ? 1 : 0);
                }
            }
        }
    }
    
    void updateOutputDisplay() {
        std::string config_text;
        google::protobuf::TextFormat::PrintToString(*config_, &config_text);
        output_buffer_->text(config_text.c_str());
    }
    
    void addActionEditor() {
        if (!actions_scroll_ || !actions_count_label_) {
            std::cout << "Actions UI not initialized yet" << std::endl;
            return;
        }
        
        auto* robot = config_->mutable_robot();
        auto* actions = robot->mutable_actions();
        int action_count = actions->single_actions_size();
        
        // Update count label
        updateActionsCount();
        
        // Temporarily end the scroll to add new widgets
        actions_scroll_->end();
        
        // Create editor for the new action (last one added)
        int editor_height = createActionEditor(action_count - 1, next_action_y_, actions_scroll_, actions);
        next_action_y_ += editor_height;
        
        // Restart the scroll
        actions_scroll_->begin();
        actions_scroll_->end();
        
        // Redraw
        actions_scroll_->redraw();
        window_->redraw();
        
        std::cout << "Action editor added to UI" << std::endl;
    }
    
    void addPerceptionEditor() {
        if (!perceptions_scroll_ || !perceptions_count_label_) {
            std::cout << "Perceptions UI not initialized yet" << std::endl;
            return;
        }
        
        auto* robot = config_->mutable_robot();
        auto* perceptions = robot->mutable_perceptions();
        int perception_count = perceptions->single_perceptions_size();
        
        // Update count label
        updatePerceptionsCount();
        
        // Temporarily end the scroll to add new widgets
        perceptions_scroll_->end();
        
        // Create editor for the new perception (last one added)
        int editor_height = createPerceptionEditor(perception_count - 1, next_perception_y_, perceptions_scroll_, perceptions);
        next_perception_y_ += editor_height;
        
        // Restart the scroll
        perceptions_scroll_->begin();
        perceptions_scroll_->end();
        
        // Redraw
        perceptions_scroll_->redraw();
        window_->redraw();
        
        std::cout << "Perception editor added to UI" << std::endl;
    }
    
    void updateActionsCount() {
        if (!actions_count_label_) return;
        
        auto* robot = config_->mutable_robot();
        auto* actions = robot->mutable_actions();
        int action_count = actions->single_actions_size();
        
        std::string new_label = "Current Actions: " + std::to_string(action_count);
        actions_count_label_->copy_label(new_label.c_str());
        actions_count_label_->redraw();
    }
    
    void updatePerceptionsCount() {
        if (!perceptions_count_label_) return;
        
        auto* robot = config_->mutable_robot();
        auto* perceptions = robot->mutable_perceptions();
        int perception_count = perceptions->single_perceptions_size();
        
        std::string new_label = "Current Perceptions: " + std::to_string(perception_count);
        perceptions_count_label_->copy_label(new_label.c_str());
        perceptions_count_label_->redraw();
    }
    
    void saveConfig() {
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
                    recreateTabs();
                    updateOutputDisplay();
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
        // Reset to default configuration
        config_ = std::make_unique<config::Config>();
        config_->set_operation_mode(config::MODE_INFERENCE);
        
        auto* robot = config_->mutable_robot();
        robot->set_name("so100");
        robot->set_id(1);
        robot->set_robot_type(config::MANIPULATOR_ARM);
        
        auto* ai = config_->mutable_ai();
        ai->set_policy_name("example_policy");
        ai->set_pretrained_model_path("/path/to/model");
        ai->set_act_dim(4);
        ai->set_state_dim(12);
        
        current_file_.clear();
        recreateTabs();
        updateOutputDisplay();
    }
    
    void refreshDisplay() {
        recreateTabs();
        updateOutputDisplay();
    }


    
    void handleMenu() {
        // Menu handling is done through callbacks
    }
    
    ~DynamicFLTKGUI() {
        delete output_buffer_;
        // widget_bindings_ will be automatically cleaned up
    }
};

int main() {
    std::cout << "Starting Project Joshua Dynamic FLTK GUI..." << std::endl;
    
    DynamicFLTKGUI gui;
    gui.show();
    
    return Fl::run();
} 