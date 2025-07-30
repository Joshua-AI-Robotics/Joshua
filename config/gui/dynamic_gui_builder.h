#pragma once

#include <FL/Fl_Widget.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <vector>
#include <map>
#include <memory>
#include "config/gui/proto_parser.h"

namespace config_gui {

// Structure to hold widget information
struct WidgetInfo {
    Fl_Widget* widget;
    std::string field_name;
    std::string field_type;
    bool is_repeated;
    bool is_oneof;
    std::string oneof_name;
};

class DynamicGUIBuilder {
public:
    DynamicGUIBuilder();
    ~DynamicGUIBuilder() = default;

    // Build GUI for a message
    Fl_Group* buildMessageGUI(const MessageInfo& message_info, 
                             google::protobuf::Message* message,
                             int x, int y, int w, int h);
    
    // Build field widgets
    std::vector<WidgetInfo> buildFieldWidgets(const std::vector<FieldInfo>& fields,
                                             google::protobuf::Message* message,
                                             int x, int y, int w);
    
    // Build individual field widget
    WidgetInfo buildFieldWidget(const FieldInfo& field_info,
                               google::protobuf::Message* message,
                               int x, int y, int w, int h);
    
    // Build repeated field widget
    Fl_Group* buildRepeatedFieldWidget(const FieldInfo& field_info,
                                      google::protobuf::Message* message,
                                      int x, int y, int w, int h);
    
    // Build oneof field widget
    Fl_Group* buildOneofFieldWidget(const std::string& oneof_name,
                                   const std::vector<FieldInfo>& oneof_fields,
                                   google::protobuf::Message* message,
                                   int x, int y, int w, int h);
    
    // Update widget values from message
    void updateWidgetsFromMessage(const std::vector<WidgetInfo>& widgets,
                                 const google::protobuf::Message& message);
    
    // Update message from widget values
    void updateMessageFromWidgets(const std::vector<WidgetInfo>& widgets,
                                 google::protobuf::Message* message);
    
    // Get widget by field name
    WidgetInfo* getWidgetByFieldName(const std::string& field_name);
    
    // Set proto parser
    void setProtoParser(std::shared_ptr<ProtoParser> parser) { parser_ = parser; }

private:
    std::shared_ptr<ProtoParser> parser_;
    std::vector<WidgetInfo> widgets_;
    
    // Callback functions
    static void inputCallback(Fl_Widget*, void* v);
    static void choiceCallback(Fl_Widget*, void* v);
    static void addItemCallback(Fl_Widget*, void* v);
    static void removeItemCallback(Fl_Widget*, void* v);
    
    // Helper methods
    int getInputHeight() const { return 30; }
    int getChoiceHeight() const { return 30; }
    int getButtonHeight() const { return 35; }
    int getSpacing() const { return 10; }
    int getMargin() const { return 20; }
    
    // Widget creation helpers
    Fl_Input* createInputWidget(int x, int y, int w, int h, const std::string& value);
    Fl_Choice* createChoiceWidget(int x, int y, int w, int h, const std::vector<std::string>& choices);
    Fl_Button* createButtonWidget(int x, int y, int w, int h, const std::string& label);
    Fl_Text_Display* createTextDisplayWidget(int x, int y, int w, int h);
};

} // namespace config_gui 