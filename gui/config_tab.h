#ifndef CONFIG_TAB_H
#define CONFIG_TAB_H

#include <QtWidgets/QWidget>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QStackedLayout>
#include <QtCore/QPointer>
#include <memory>
#include <vector>
#include <limits>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>

// Proto root for configuration
#include "config/proto/config.pb.h"

namespace Ui { class ConfigTab; }

class ConfigTab : public QWidget {
    Q_OBJECT
public:
    explicit ConfigTab(QWidget* parent = nullptr);
    ~ConfigTab();

private slots:
    void onSavePbtxt();

private:
    // Protobuf-driven UI model
    struct MessageNode;

    struct ScalarFieldNode {
        const google::protobuf::FieldDescriptor* field = nullptr;
        QWidget* editor = nullptr; // QLineEdit/QSpinBox/QDoubleSpinBox/QCheckBox
    };

    struct EnumFieldNode {
        const google::protobuf::FieldDescriptor* field = nullptr;
        QComboBox* combo = nullptr;
    };

    struct MessageFieldNode {
        const google::protobuf::FieldDescriptor* field = nullptr;
        std::unique_ptr<MessageNode> child; // non-repeated message
    };

    struct RepeatedMessageFieldNode {
        const google::protobuf::FieldDescriptor* field = nullptr;
        QWidget* container = nullptr;
        QVBoxLayout* itemsLayout = nullptr;
        QPushButton* addButton = nullptr;
        std::vector<std::unique_ptr<MessageNode>> items;
    };

    struct OneofNode {
        const google::protobuf::OneofDescriptor* oneof = nullptr;
        QComboBox* selector = nullptr; // index 0 == none
        // Parallel vectors of fields and corresponding message nodes
        std::vector<const google::protobuf::FieldDescriptor*> choiceFields;
        std::vector<std::unique_ptr<MessageNode>> choiceNodes; // same indices as choiceFields (1..N) mapped to 1..N
        QWidget* choicesContainer = nullptr;
        QStackedLayout* stacked = nullptr; // index 0 is empty widget
    };

    struct MessageNode {
        const google::protobuf::Descriptor* descriptor = nullptr;
        QGroupBox* group = nullptr;
        QFormLayout* form = nullptr;
        std::vector<ScalarFieldNode> scalarFields;
        std::vector<EnumFieldNode> enumFields;
        std::vector<MessageFieldNode> messageFields;
        std::vector<RepeatedMessageFieldNode> repeatedMessageFields;
        std::vector<OneofNode> oneofs;
    };

    // UI construction
    void buildRootForm();
    std::unique_ptr<MessageNode> buildMessageNode(const google::protobuf::Descriptor* descriptor, const QString& title = QString());
    QWidget* buildScalarEditor(const google::protobuf::FieldDescriptor* field);
    QComboBox* buildEnumEditor(const google::protobuf::EnumDescriptor* enumDesc);
    std::unique_ptr<MessageNode> buildSingleMessageField(const google::protobuf::FieldDescriptor* field);
    RepeatedMessageFieldNode buildRepeatedMessageField(const google::protobuf::FieldDescriptor* field);
    OneofNode buildOneofNode(const google::protobuf::OneofDescriptor* oneof);

    // Serialization
    void writeMessageFromNode(const MessageNode& node, google::protobuf::Message* message);
    void writeScalarField(const ScalarFieldNode& node, google::protobuf::Message* message);
    void writeEnumField(const EnumFieldNode& node, google::protobuf::Message* message);
    void writeMessageField(const MessageFieldNode& node, google::protobuf::Message* message);
    void writeRepeatedMessageField(const RepeatedMessageFieldNode& node, google::protobuf::Message* message);
    void writeOneofField(const OneofNode& node, google::protobuf::Message* message);

    // Helpers
    static QString prettyLabelForField(const google::protobuf::FieldDescriptor* field);

    Ui::ConfigTab* ui;

    // Dynamic content
    QPointer<QScrollArea> scrollArea_;
    QPointer<QWidget> formContainer_;
    QPointer<QVBoxLayout> formLayout_;
    QPointer<QPushButton> saveButton_;

    std::unique_ptr<MessageNode> rootNode_;
};

#endif // CONFIG_TAB_H 