#include "config_tab.h"
#include "ui_config_tab.h"

#include <QtWidgets/QLabel>
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
#include <QtWidgets/QMessageBox>
#include <QtCore/QString>
#include <QtCore/QDebug>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>
#include <google/protobuf/text_format.h>

using google::protobuf::Descriptor;
using google::protobuf::EnumDescriptor;
using google::protobuf::FieldDescriptor;
using google::protobuf::Message;
using google::protobuf::OneofDescriptor;
using google::protobuf::Reflection;

ConfigTab::ConfigTab(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::ConfigTab)
{
    ui->setupUi(this);

    // Reuse and clear the existing vertical layout created by the .ui file
    QVBoxLayout* outerLayout = this->findChild<QVBoxLayout*>("verticalLayout");
    bool createdLayout = false;
    if (!outerLayout) {
        outerLayout = new QVBoxLayout(this);
        createdLayout = true;
    } else {
        // Remove placeholder widgets/spacers
        while (outerLayout->count() > 0) {
            QLayoutItem* item = outerLayout->takeAt(0);
            if (item->widget()) {
                item->widget()->deleteLater();
            }
            delete item;
        }
    }

    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    formContainer_ = new QWidget(scrollArea_);
    formLayout_ = new QVBoxLayout(formContainer_);
    formLayout_->setContentsMargins(8, 8, 8, 8);
    formLayout_->setSpacing(12);

    scrollArea_->setWidget(formContainer_);
    outerLayout->addWidget(scrollArea_);

    auto* actionsRow = new QHBoxLayout();
    actionsRow->addStretch();
    saveButton_ = new QPushButton("Save .pbtxt", this);
    actionsRow->addWidget(saveButton_);
    outerLayout->addLayout(actionsRow);

    if (createdLayout) {
        setLayout(outerLayout);
    }

    connect(saveButton_, &QPushButton::clicked, this, &ConfigTab::onSavePbtxt);

    buildRootForm();
}

ConfigTab::~ConfigTab() { delete ui; }

void ConfigTab::buildRootForm() {
    // Root is config::Config
    const Descriptor* rootDesc = config::Config::descriptor();
    rootNode_ = buildMessageNode(rootDesc, "Config");
    if (rootNode_ && rootNode_->group) {
        formLayout_->addWidget(rootNode_->group);
        formLayout_->addStretch();
    }
}

std::unique_ptr<ConfigTab::MessageNode> ConfigTab::buildMessageNode(const Descriptor* descriptor, const QString& title) {
    auto node = std::make_unique<MessageNode>();
    node->descriptor = descriptor;

    node->group = new QGroupBox(title.isEmpty() ? QString::fromStdString(descriptor->full_name()) : title, this);
    auto* vbox = new QVBoxLayout(node->group);

    node->form = new QFormLayout();
    node->form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    node->form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    node->form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // Scalars, enums, messages, repeated
    for (int i = 0; i < descriptor->field_count(); ++i) {
        const FieldDescriptor* field = descriptor->field(i);
        if (field->containing_oneof()) {
            // Will be added with the oneof section below
            continue;
        }

        if (field->is_repeated() && field->cpp_type() == FieldDescriptor::CPPTYPE_MESSAGE) {
            auto repNode = buildRepeatedMessageField(field);
            QWidget* container = new QWidget(node->group);
            auto* v = new QVBoxLayout(container);

            auto* header = new QHBoxLayout();
            header->addWidget(new QLabel(prettyLabelForField(field)));
            header->addStretch();
            repNode.addButton = new QPushButton("Add", container);
            header->addWidget(repNode.addButton);
            v->addLayout(header);

            repNode.container = container;
            repNode.itemsLayout = new QVBoxLayout();
            v->addLayout(repNode.itemsLayout);

            node->repeatedMessageFields.emplace_back(std::move(repNode));
            // Connect after storing, capture a stable pointer
            RepeatedMessageFieldNode* repPtr = &node->repeatedMessageFields.back();
            QObject::connect(repPtr->addButton, &QPushButton::clicked, container, [this, field, repPtr]() {
                auto itemNode = buildMessageNode(field->message_type(), QString::fromStdString(field->message_type()->name()));
                repPtr->itemsLayout->addWidget(itemNode->group);
                repPtr->items.emplace_back(std::move(itemNode));
            });

            v->addSpacing(6);
            node->form->addRow(container);
            continue;
        }

        switch (field->type()) {
            case FieldDescriptor::TYPE_BOOL:
            case FieldDescriptor::TYPE_STRING:
            case FieldDescriptor::TYPE_INT32:
            case FieldDescriptor::TYPE_INT64:
            case FieldDescriptor::TYPE_UINT32:
            case FieldDescriptor::TYPE_UINT64:
            case FieldDescriptor::TYPE_FLOAT:
            case FieldDescriptor::TYPE_DOUBLE: {
                QWidget* editor = buildScalarEditor(field);
                node->scalarFields.push_back(ScalarFieldNode{field, editor});
                node->form->addRow(prettyLabelForField(field), editor);
                break;
            }
            case FieldDescriptor::TYPE_ENUM: {
                QComboBox* combo = buildEnumEditor(field->enum_type());
                node->enumFields.push_back(EnumFieldNode{field, combo});
                node->form->addRow(prettyLabelForField(field), combo);
                break;
            }
            case FieldDescriptor::TYPE_MESSAGE: {
                auto child = buildSingleMessageField(field);
                node->messageFields.push_back(MessageFieldNode{field, std::move(child)});
                node->form->addRow(prettyLabelForField(field), node->messageFields.back().child->group);
                break;
            }
            default:
                // Other types are rare in this schema; skip for now
                break;
        }
    }

    // Oneofs
    for (int i = 0; i < descriptor->oneof_decl_count(); ++i) {
        const OneofDescriptor* oneof = descriptor->oneof_decl(i);
        OneofNode oneNode = buildOneofNode(oneof);

        QWidget* container = new QWidget(node->group);
        auto* v = new QVBoxLayout(container);

        auto* header = new QHBoxLayout();
        header->addWidget(new QLabel(QString::fromStdString(oneof->name())));
        header->addStretch();
        header->addWidget(oneNode.selector);
        v->addLayout(header);

        oneNode.choicesContainer = new QWidget(container);
        oneNode.stacked = new QStackedLayout(oneNode.choicesContainer);
        // index 0: empty placeholder
        oneNode.stacked->addWidget(new QWidget(oneNode.choicesContainer));
        for (size_t idx = 0; idx < oneNode.choiceNodes.size(); ++idx) {
            oneNode.stacked->addWidget(oneNode.choiceNodes[idx]->group);
        }
        v->addWidget(oneNode.choicesContainer);

        QObject::connect(oneNode.selector, qOverload<int>(&QComboBox::currentIndexChanged), container, [stacked=oneNode.stacked](int index){
            stacked->setCurrentIndex(index); // matches 0..N
        });

        node->oneofs.emplace_back(std::move(oneNode));
        node->form->addRow(container);
    }

    vbox->addLayout(node->form);
    return node;
}

QWidget* ConfigTab::buildScalarEditor(const FieldDescriptor* field) {
    switch (field->type()) {
        case FieldDescriptor::TYPE_BOOL: {
            auto* box = new QCheckBox(this);
            return box;
        }
        case FieldDescriptor::TYPE_STRING: {
            auto* edit = new QLineEdit(this);
            return edit;
        }
        case FieldDescriptor::TYPE_INT32:
        case FieldDescriptor::TYPE_UINT32: {
            auto* spin = new QSpinBox(this);
            spin->setMinimum(std::numeric_limits<int>::min());
            spin->setMaximum(std::numeric_limits<int>::max());
            return spin;
        }
        case FieldDescriptor::TYPE_INT64:
        case FieldDescriptor::TYPE_UINT64: {
            auto* edit = new QLineEdit(this);
            edit->setPlaceholderText("64-bit integer");
            edit->setValidator(nullptr); // could add QIntValidator64 equivalent later
            return edit;
        }
        case FieldDescriptor::TYPE_FLOAT:
        case FieldDescriptor::TYPE_DOUBLE: {
            auto* spin = new QDoubleSpinBox(this);
            spin->setMinimum(-1e12);
            spin->setMaximum(1e12);
            spin->setDecimals(6);
            return spin;
        }
        default:
            return new QWidget(this);
    }
}

QComboBox* ConfigTab::buildEnumEditor(const EnumDescriptor* enumDesc) {
    auto* combo = new QComboBox(this);
    combo->addItem("<unset>", -1);
    for (int i = 0; i < enumDesc->value_count(); ++i) {
        const auto* val = enumDesc->value(i);
        combo->addItem(QString::fromStdString(val->name()), val->number());
    }
    return combo;
}

std::unique_ptr<ConfigTab::MessageNode> ConfigTab::buildSingleMessageField(const FieldDescriptor* field) {
    auto child = buildMessageNode(field->message_type(), QString::fromStdString(field->name()));
    return child;
}

ConfigTab::RepeatedMessageFieldNode ConfigTab::buildRepeatedMessageField(const FieldDescriptor* field) {
    RepeatedMessageFieldNode node;
    node.field = field;
    return node;
}

ConfigTab::OneofNode ConfigTab::buildOneofNode(const OneofDescriptor* oneof) {
    OneofNode node;
    node.oneof = oneof;
    node.selector = new QComboBox(this);
    node.selector->addItem("<none>");
    for (int i = 0; i < oneof->field_count(); ++i) {
        const FieldDescriptor* f = oneof->field(i);
        node.choiceFields.push_back(f);
        auto editor = buildMessageNode(f->message_type(), QString::fromStdString(f->name()));
        node.choiceNodes.emplace_back(std::move(editor));
        node.selector->addItem(QString::fromStdString(f->name()));
    }
    return node;
}

void ConfigTab::onSavePbtxt() {
    if (!rootNode_) return;

    config::Config cfg;
    writeMessageFromNode(*rootNode_, &cfg);

    QString filename = QFileDialog::getSaveFileName(this, "Save configuration", QString(), "Prototxt (*.pbtxt)");
    if (filename.isEmpty()) return;

    std::string out;
    google::protobuf::TextFormat::Printer printer;
    printer.SetUseShortRepeatedPrimitives(true);
    printer.SetSingleLineMode(false);
    printer.SetExpandAny(true);
    printer.SetHideUnknownFields(true);
    if (!printer.PrintToString(cfg, &out)) {
        QMessageBox::critical(this, "Error", "Failed to serialize configuration to text proto.");
        return;
    }

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Failed to open file for writing.");
        return;
    }
    file.write(QByteArray::fromStdString(out));
    file.close();

    QMessageBox::information(this, "Saved", "Configuration saved to " + filename);
}

void ConfigTab::writeMessageFromNode(const MessageNode& node, Message* message) {
    // Scalars
    for (const auto& s : node.scalarFields) {
        writeScalarField(s, message);
    }

    // Enums
    for (const auto& e : node.enumFields) {
        writeEnumField(e, message);
    }

    // Non-repeated messages
    for (const auto& m : node.messageFields) {
        writeMessageField(m, message);
    }

    // Repeated messages
    for (const auto& r : node.repeatedMessageFields) {
        writeRepeatedMessageField(r, message);
    }

    // Oneofs
    for (const auto& o : node.oneofs) {
        writeOneofField(o, message);
    }
}

void ConfigTab::writeScalarField(const ScalarFieldNode& node, Message* message) {
    const Reflection* refl = message->GetReflection();
    const FieldDescriptor* field = node.field;

    switch (field->type()) {
        case FieldDescriptor::TYPE_BOOL: {
            auto* box = qobject_cast<QCheckBox*>(node.editor);
            if (!box) return;
            if (field->is_repeated()) return; // not expected in schema
            refl->SetBool(message, field, box->isChecked());
            break;
        }
        case FieldDescriptor::TYPE_STRING: {
            auto* edit = qobject_cast<QLineEdit*>(node.editor);
            if (!edit) return;
            if (field->is_repeated()) return;
            refl->SetString(message, field, edit->text().toStdString());
            break;
        }
        case FieldDescriptor::TYPE_INT32: {
            auto* spin = qobject_cast<QSpinBox*>(node.editor);
            if (!spin) return;
            if (field->is_repeated()) return;
            refl->SetInt32(message, field, spin->value());
            break;
        }
        case FieldDescriptor::TYPE_UINT32: {
            auto* spin = qobject_cast<QSpinBox*>(node.editor);
            if (!spin) return;
            if (field->is_repeated()) return;
            refl->SetUInt32(message, field, static_cast<uint32_t>(spin->value()));
            break;
        }
        case FieldDescriptor::TYPE_INT64:
        case FieldDescriptor::TYPE_UINT64: {
            auto* edit = qobject_cast<QLineEdit*>(node.editor);
            if (!edit) return;
            bool ok = false;
            quint64 val = edit->text().toULongLong(&ok);
            if (!ok) return;
            if (field->type() == FieldDescriptor::TYPE_INT64) {
                refl->SetInt64(message, field, static_cast<qint64>(val));
            } else {
                refl->SetUInt64(message, field, static_cast<quint64>(val));
            }
            break;
        }
        case FieldDescriptor::TYPE_FLOAT: {
            auto* spin = qobject_cast<QDoubleSpinBox*>(node.editor);
            if (!spin) return;
            refl->SetFloat(message, field, static_cast<float>(spin->value()));
            break;
        }
        case FieldDescriptor::TYPE_DOUBLE: {
            auto* spin = qobject_cast<QDoubleSpinBox*>(node.editor);
            if (!spin) return;
            refl->SetDouble(message, field, spin->value());
            break;
        }
        default:
            break;
    }
}

void ConfigTab::writeEnumField(const EnumFieldNode& node, Message* message) {
    const Reflection* refl = message->GetReflection();
    const FieldDescriptor* field = node.field;
    int number = node.combo->currentData().toInt();
    if (number >= 0) {
        const auto* enumValue = field->enum_type()->FindValueByNumber(number);
        if (enumValue) {
            refl->SetEnum(message, field, enumValue);
        }
    }
}

void ConfigTab::writeMessageField(const MessageFieldNode& node, Message* message) {
    const Reflection* refl = message->GetReflection();
    Message* sub = refl->MutableMessage(message, node.field);
    writeMessageFromNode(*node.child, sub);
}

void ConfigTab::writeRepeatedMessageField(const RepeatedMessageFieldNode& node, Message* message) {
    const Reflection* refl = message->GetReflection();
    for (const auto& item : node.items) {
        Message* sub = refl->AddMessage(message, node.field);
        writeMessageFromNode(*item, sub);
    }
}

void ConfigTab::writeOneofField(const OneofNode& node, Message* message) {
    const Reflection* refl = message->GetReflection();
    int index = node.selector->currentIndex();
    if (index <= 0) return; // none
    int choiceIdx = index - 1;
    if (choiceIdx < 0 || choiceIdx >= static_cast<int>(node.choiceFields.size())) return;

    const FieldDescriptor* field = node.choiceFields[choiceIdx];
    Message* sub = refl->MutableMessage(message, field);
    writeMessageFromNode(*node.choiceNodes[choiceIdx], sub);
}

QString ConfigTab::prettyLabelForField(const FieldDescriptor* field) {
    QString name = QString::fromStdString(field->name());
    // Replace underscores with spaces and capitalize words
    QStringList parts = name.split('_', Qt::SkipEmptyParts);
    for (QString& p : parts) {
        if (!p.isEmpty()) p[0] = p[0].toUpper();
    }
    return parts.join(' ');
} 