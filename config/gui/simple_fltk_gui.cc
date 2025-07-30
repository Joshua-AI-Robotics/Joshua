#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Tabs.H>
#include <iostream>
#include <string>
#include <vector>

class SimpleFLTKGUI {
private:
    Fl_Window* window_;
    Fl_Input* name_input_;
    Fl_Input* id_input_;
    Fl_Choice* mode_choice_;
    Fl_Text_Display* output_display_;
    Fl_Text_Buffer* output_buffer_;
    
    // Callback functions
    static void saveCallback(Fl_Widget*, void* v) {
        static_cast<SimpleFLTKGUI*>(v)->saveConfig();
    }
    
    static void loadCallback(Fl_Widget*, void* v) {
        static_cast<SimpleFLTKGUI*>(v)->loadConfig();
    }
    
    static void clearCallback(Fl_Widget*, void* v) {
        static_cast<SimpleFLTKGUI*>(v)->clearOutput();
    }
    
    static void inputCallback(Fl_Widget*, void* v) {
        static_cast<SimpleFLTKGUI*>(v)->updateOutput();
    }

public:
    SimpleFLTKGUI() {
        // Create main window
        window_ = new Fl_Window(800, 600, "Project Joshua - Simple FLTK GUI");
        
        // Create tabs
        Fl_Tabs* tabs = new Fl_Tabs(10, 10, 780, 540);
        
        // Main Configuration Tab
        Fl_Group* main_group = new Fl_Group(10, 35, 780, 515, "Main Config");
        
        // Title
        Fl_Box* title = new Fl_Box(20, 45, 760, 30, "Project Joshua Configuration");
        title->labelsize(18);
        title->labelfont(FL_BOLD);
        
        // Input fields
        Fl_Box* name_label = new Fl_Box(20, 85, 150, 25, "Robot Name:");
        name_input_ = new Fl_Input(180, 85, 200, 25);
        name_input_->value("so100");
        name_input_->callback(inputCallback, this);
        
        Fl_Box* id_label = new Fl_Box(20, 120, 150, 25, "Robot ID:");
        id_input_ = new Fl_Input(180, 120, 200, 25);
        id_input_->value("1");
        id_input_->type(FL_INT_INPUT);
        id_input_->callback(inputCallback, this);
        
        Fl_Box* mode_label = new Fl_Box(20, 155, 150, 25, "Operation Mode:");
        mode_choice_ = new Fl_Choice(180, 155, 200, 25);
        mode_choice_->add("MODE_INFERENCE");
        mode_choice_->add("MODE_TELEOPERATE");
        mode_choice_->add("MODE_TRAINING");
        mode_choice_->add("MODE_TEST");
        mode_choice_->value(0);
        mode_choice_->callback(inputCallback, this);
        
        // Output display
        Fl_Box* output_label = new Fl_Box(20, 200, 150, 25, "Configuration Output:");
        output_buffer_ = new Fl_Text_Buffer();
        output_display_ = new Fl_Text_Display(20, 230, 760, 200);
        output_display_->buffer(output_buffer_);
        output_display_->textfont(FL_COURIER);
        output_display_->textsize(12);
        
        // Buttons
        Fl_Button* save_btn = new Fl_Button(20, 450, 100, 30, "Save Config");
        save_btn->callback(saveCallback, this);
        
        Fl_Button* load_btn = new Fl_Button(140, 450, 100, 30, "Load Config");
        load_btn->callback(loadCallback, this);
        
        Fl_Button* clear_btn = new Fl_Button(260, 450, 100, 30, "Clear Output");
        clear_btn->callback(clearCallback, this);
        
        main_group->end();
        
        // Robot Configuration Tab
        Fl_Group* robot_group = new Fl_Group(10, 35, 780, 515, "Robot Config");
        
        Fl_Box* robot_title = new Fl_Box(20, 45, 760, 30, "Robot Configuration");
        robot_title->labelsize(16);
        robot_title->labelfont(FL_BOLD);
        
        Fl_Box* robot_info = new Fl_Box(20, 85, 760, 100, 
            "This tab would contain robot-specific configuration options.\n"
            "Including actuator settings, sensor configurations, and\n"
            "physical layout parameters.");
        robot_info->labelsize(12);
        robot_info->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);
        
        robot_group->end();
        
        // AI Configuration Tab
        Fl_Group* ai_group = new Fl_Group(10, 35, 780, 515, "AI Config");
        
        Fl_Box* ai_title = new Fl_Box(20, 45, 760, 30, "AI Configuration");
        ai_title->labelsize(16);
        ai_title->labelfont(FL_BOLD);
        
        Fl_Box* ai_info = new Fl_Box(20, 85, 760, 100, 
            "This tab would contain AI-specific configuration options.\n"
            "Including model paths, policy settings, training parameters,\n"
            "and inference configurations.");
        ai_info->labelsize(12);
        ai_info->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);
        
        ai_group->end();
        
        tabs->end();
        
        // Update output initially
        updateOutput();
        
        window_->end();
    }
    
    void show() {
        window_->show();
    }
    
    void saveConfig() {
        std::string config = "Configuration saved:\n";
        config += "Robot Name: " + std::string(name_input_->value()) + "\n";
        config += "Robot ID: " + std::string(id_input_->value()) + "\n";
        config += "Operation Mode: " + std::string(mode_choice_->text()) + "\n";
        
        output_buffer_->text(config.c_str());
        
        // In a real application, this would save to a file
        std::cout << "Configuration saved to file (simulated)" << std::endl;
    }
    
    void loadConfig() {
        // In a real application, this would load from a file
        std::string config = "Configuration loaded:\n";
        config += "Robot Name: so100\n";
        config += "Robot ID: 1\n";
        config += "Operation Mode: MODE_INFERENCE\n";
        
        output_buffer_->text(config.c_str());
        
        // Update input fields
        name_input_->value("so100");
        id_input_->value("1");
        mode_choice_->value(0);
        
        std::cout << "Configuration loaded from file (simulated)" << std::endl;
    }
    
    void clearOutput() {
        output_buffer_->text("");
    }
    
    void updateOutput() {
        std::string config = "Current Configuration:\n";
        config += "Robot Name: " + std::string(name_input_->value()) + "\n";
        config += "Robot ID: " + std::string(id_input_->value()) + "\n";
        config += "Operation Mode: " + std::string(mode_choice_->text()) + "\n";
        
        output_buffer_->text(config.c_str());
    }
    
    ~SimpleFLTKGUI() {
        delete output_buffer_;
    }
};

int main() {
    std::cout << "Starting Project Joshua Simple FLTK GUI..." << std::endl;
    
    SimpleFLTKGUI gui;
    gui.show();
    
    return Fl::run();
} 