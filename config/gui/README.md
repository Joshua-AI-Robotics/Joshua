# Project Joshua Configuration GUI

This directory contains the configuration GUI for Project Joshua, with both static and dynamic approaches.

## Architecture Overview

### Static GUI (advanced_fltk_gui.cc)
- **File**: `advanced_fltk_gui.cc`
- **Approach**: Hardcoded protobuf field handling
- **Features**: 
  - Manual implementation of each field
  - Fixed UI layout
  - Direct protobuf field access
  - Support for actions and perceptions

### Dynamic GUI (dynamic_fltk_gui.cc)
- **File**: `dynamic_fltk_gui.cc`
- **Approach**: Dynamic protobuf parsing and widget generation
- **Features**:
  - Automatic field discovery from protobuf definitions
  - Dynamic UI generation
  - No hardcoded field handling
  - Extensible to any protobuf message

## Proto Parser Library

### Overview
The `proto_parser` library provides dynamic protobuf message parsing and manipulation capabilities.

### Files
- `proto_parser.h` - Header file with interface definitions
- `proto_parser.cc` - Implementation of dynamic parsing logic

### Key Features

#### 1. Dynamic Field Discovery
```cpp
// Parse any protobuf message
config_gui::MessageInfo info = parser->parseMessage(config);
for (const auto& field : info.fields) {
    std::cout << "Field: " << field.display_name << " (" << field.field_type << ")" << std::endl;
}
```

#### 2. Enum Value Extraction
```cpp
// Get all enum values for a field
std::vector<std::string> enum_values = parser->getEnumValues(field_descriptor);
```

#### 3. Type-Safe Value Conversion
```cpp
// Convert field values to/from strings
std::string value = parser->getFieldValueAsString(message, field);
bool success = parser->setFieldValueFromString(message, field, "new_value");
```

#### 4. Repeated Field Management
```cpp
// Handle repeated fields
int count = parser->getRepeatedFieldCount(message, field);
bool added = parser->addRepeatedFieldItem(message, field);
bool removed = parser->removeLastRepeatedFieldItem(message, field);
```

#### 5. Oneof Field Support
```cpp
// Handle oneof fields
std::string current_field = parser->getOneofFieldName(message, field);
bool set = parser->setOneofField(message, "oneof_name", "field_name");
```

### Data Structures

#### FieldInfo
```cpp
struct FieldInfo {
    std::string name;                    // Original field name
    std::string display_name;            // Human-readable name
    std::string value;                   // Current value as string
    std::string field_type;              // Protobuf type name
    bool is_enum;                        // Is enum field
    std::vector<std::string> enum_values; // Available enum values
    bool is_repeated;                    // Is repeated field
    bool is_message;                     // Is message field
    bool is_oneof;                       // Is oneof field
    std::string oneof_name;              // Oneof container name
};
```

#### MessageInfo
```cpp
struct MessageInfo {
    std::string name;                    // Message name
    std::string display_name;            // Human-readable name
    std::vector<FieldInfo> fields;       // All fields in message
    std::vector<MessageInfo> sub_messages; // Nested messages
};
```

## Building and Running

### Build All Targets
```bash
bazel build //config/gui:all
```

### Build Specific Targets
```bash
# Build static GUI
bazel build //config/gui:advanced_fltk_gui

# Build dynamic GUI
bazel build //config/gui:dynamic_fltk_gui

# Build proto parser library
bazel build //config/gui:proto_parser

# Build test program
bazel build //config/gui:test_proto_parser
```

### Run Programs
```bash
# Run static GUI
bazel run //config/gui:advanced_fltk_gui

# Run dynamic GUI
bazel run //config/gui:dynamic_fltk_gui

# Run proto parser test
bazel run //config/gui:test_proto_parser
```

## Usage Examples

### Basic Dynamic Parsing
```cpp
#include "config/gui/proto_parser.h"
#include "config/proto/config.pb.h"

// Create parser
auto parser = std::make_shared<config_gui::ProtoParser>();

// Create config
config::Config config;
config.set_operation_mode(config::MODE_INFERENCE);

// Parse dynamically
config_gui::MessageInfo info = parser->parseConfigMessage(config);

// Access fields
for (const auto& field : info.fields) {
    if (field.is_enum) {
        std::cout << "Enum field: " << field.display_name << std::endl;
        for (const auto& value : field.enum_values) {
            std::cout << "  - " << value << std::endl;
        }
    }
}
```

### Dynamic Widget Creation
```cpp
// Create widgets based on field type
for (const auto& field : info.fields) {
    if (field.is_enum) {
        // Create choice widget with enum values
        Fl_Choice* choice = new Fl_Choice(x, y, w, h);
        for (const auto& value : field.enum_values) {
            choice->add(value.c_str());
        }
    } else if (field.field_type == "string") {
        // Create text input
        Fl_Input* input = new Fl_Input(x, y, w, h);
        input->value(field.value.c_str());
    }
    // ... handle other types
}
```

## Advantages of Dynamic Approach

1. **Maintainability**: No need to update GUI code when protobuf definitions change
2. **Extensibility**: Automatically supports new fields and message types
3. **Consistency**: All fields are handled uniformly
4. **Reduced Code**: Eliminates repetitive hardcoded field handling
5. **Type Safety**: Leverages protobuf's type system for validation

## Future Enhancements

1. **Recursive Message Parsing**: Parse nested messages automatically
2. **Widget Callbacks**: Dynamic callback generation for field updates
3. **Validation**: Automatic validation based on protobuf field rules
4. **Custom Widgets**: Support for custom widget types for complex fields
5. **Layout Management**: Automatic layout generation for optimal UI

## Dependencies

- FLTK (GUI framework)
- Protocol Buffers (message serialization)
- C++17 standard library

## Notes

- The dynamic GUI is currently a simplified implementation
- Physical layout configuration is not yet implemented (marked with TODO)
- The proto parser supports all basic protobuf types and features
- Both GUIs maintain the same file format (.pbtxt) for compatibility 