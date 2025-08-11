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
#include <QtCore/QVariant>
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
    void onLoadPbtxt();
    void onClear();
    // Auto-connected button slots
    void on_saveButton_clicked();
    void on_loadButton_clicked();
    void on_clearButton_clicked();

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

    // New: Support repeated primitive fields (e.g. repeated string)
    struct RepeatedPrimitiveFieldNode {
        struct RowItem {
            QWidget* rowWidget = nullptr;    // container holding editor + remove button
            QWidget* editorWidget = nullptr; // the actual editor for the primitive
        };
        const google::protobuf::FieldDescriptor* field = nullptr;
        QWidget* container = nullptr;
        QVBoxLayout* itemsLayout = nullptr;
        QPushButton* addButton = nullptr;
        std::vector<RowItem> items;
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
        // New: store repeated primitive fields per message
        std::vector<RepeatedPrimitiveFieldNode> repeatedPrimitiveFields;
        std::vector<OneofNode> oneofs;
    };

    // UI construction
    void buildRootForm();
    std::unique_ptr<MessageNode> buildMessageNode(const google::protobuf::Descriptor* descriptor, const QString& title = QString());
    QWidget* buildScalarEditor(const google::protobuf::FieldDescriptor* field);
    QComboBox* buildEnumEditor(const google::protobuf::EnumDescriptor* enumDesc);
    std::unique_ptr<MessageNode> buildSingleMessageField(const google::protobuf::FieldDescriptor* field);
    RepeatedMessageFieldNode buildRepeatedMessageField(const google::protobuf::FieldDescriptor* field);
    // New helpers for repeated primitives
    RepeatedPrimitiveFieldNode buildRepeatedPrimitiveField(const google::protobuf::FieldDescriptor* field);
    QWidget* buildPrimitiveEditor(const google::protobuf::FieldDescriptor* field);
    void addRepeatedPrimitiveRow(RepeatedPrimitiveFieldNode* node, const QVariant& initial = QVariant());

    OneofNode buildOneofNode(const google::protobuf::OneofDescriptor* oneof);

    // Serialization (UI -> proto)
    void writeMessageFromNode(const MessageNode& node, google::protobuf::Message* message);
    void writeScalarField(const ScalarFieldNode& node, google::protobuf::Message* message);
    void writeEnumField(const EnumFieldNode& node, google::protobuf::Message* message);
    void writeMessageField(const MessageFieldNode& node, google::protobuf::Message* message);
    void writeRepeatedMessageField(const RepeatedMessageFieldNode& node, google::protobuf::Message* message);
    // New: write repeated primitive fields
    void writeRepeatedPrimitiveField(const RepeatedPrimitiveFieldNode& node, google::protobuf::Message* message);
    void writeOneofField(const OneofNode& node, google::protobuf::Message* message);

    // Deserialization (proto -> UI)
    void readMessageIntoNode(const google::protobuf::Message& message, MessageNode* node);
    void readScalarFieldIntoEditor(const google::protobuf::Message& message, ScalarFieldNode* node);
    void readEnumFieldIntoEditor(const google::protobuf::Message& message, EnumFieldNode* node);
    void readMessageFieldIntoChild(const google::protobuf::Message& message, MessageFieldNode* node);
    void readRepeatedMessageFieldIntoChildren(const google::protobuf::Message& message, RepeatedMessageFieldNode* node);
    // New: read repeated primitive fields
    void readRepeatedPrimitiveFieldIntoEditors(const google::protobuf::Message& message, RepeatedPrimitiveFieldNode* node);
    void readOneofIntoNode(const google::protobuf::Message& message, OneofNode* node);

    // Clearing helpers (reset UI)
    void clearMessageNode(MessageNode* node);
    void clearScalarField(ScalarFieldNode* node);
    void clearEnumField(EnumFieldNode* node);

    // Helpers
    static QString prettyLabelForField(const google::protobuf::FieldDescriptor* field);

    Ui::ConfigTab* ui;

    // Top action buttons
    QPointer<QPushButton> saveButton_;
    QPointer<QPushButton> loadButton_;
    QPointer<QPushButton> clearButton_;

    // Dynamic content
    QPointer<QScrollArea> scrollArea_;
    QPointer<QWidget> formContainer_;
    QPointer<QVBoxLayout> formLayout_;

    std::unique_ptr<MessageNode> rootNode_;
};

#endif // CONFIG_TAB_H 