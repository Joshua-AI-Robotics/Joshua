#include <string>
#include <vector>
#include <map>
#include <memory>
#include "google/protobuf/descriptor.h"
#include "google/protobuf/reflection.h"
#include "google/protobuf/message.h"
#include "config/proto/config.pb.h"
#include <glog/logging.h>

struct ProtoField {
    const google::protobuf::FieldDescriptor* descriptor;
    std::vector<ProtoField> nested_fields; 
    
    ProtoField(const google::protobuf::FieldDescriptor* d)
        : descriptor(d) {}
    
    bool is_repeated() const {
        return descriptor->is_repeated();
    }

    bool is_message() const {
        return descriptor->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE;
    }

    std::string name() const {
        return descriptor->name();
    }

    std::string type() const {
        return descriptor->type_name();
    }
};

void populate_nested_fields(ProtoField& proto_field) {
    if (!proto_field.is_message()) {
        return;
    }
    
    const google::protobuf::Descriptor* message_descriptor = proto_field.descriptor->message_type();
    if (!message_descriptor) {
        return;
    }
    
    for (int i = 0; i < message_descriptor->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* nested_field = message_descriptor->field(i);
        ProtoField nested_proto_field(nested_field);
        
        // Recursively populate nested fields
        populate_nested_fields(nested_proto_field);
        
        proto_field.nested_fields.push_back(nested_proto_field);
    }
}

void print_fields(const std::vector<ProtoField>& fields, int indent = 0) {
    std::string indent_str(indent * 2, ' ');
    
    for (const auto& field : fields) {
        LOG(INFO) << indent_str << field.name() << " (" << field.type() << ")" << (field.is_repeated() ? " [repeated]" : "");
        
        if (!field.nested_fields.empty()) {
            print_fields(field.nested_fields, indent + 1);
        }
    }
}

int main(int argc, char* argv[]) {
    config::Config config;
        
    std::vector<ProtoField> existing_attributes;

    const google::protobuf::Descriptor* descriptor = config.GetDescriptor();
    
    for (int i = 0; i < descriptor->field_count(); ++i) {
        const google::protobuf::FieldDescriptor* field = descriptor->field(i);
        ProtoField proto_field(field);
        
        // Populate nested fields recursively
        populate_nested_fields(proto_field);
        
        existing_attributes.push_back(proto_field);
    }

    // Print all fields with proper indentation for nested fields
    print_fields(existing_attributes);

    return 0;
}