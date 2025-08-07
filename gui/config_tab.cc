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
#include <QtGui/QPixmap>
#include <QtCore/QFile>
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

    // Use widgets from .ui
    saveButton_ = ui->saveButton;
    loadButton_ = ui->loadButton;
    clearButton_ = ui->clearButton;

    scrollArea_ = ui->scrollArea;
    formContainer_ = ui->formContainer;
    formLayout_ = ui->formLayout;

    // Clean default spacer in formLayout
    for (int i = formLayout_->count() - 1; i >= 0; --i) {
        QLayoutItem* item = formLayout_->itemAt(i);
        if (item && item->spacerItem()) {
            formLayout_->removeItem(item);
            delete item;
        }
    }

    buildRootForm();
}

ConfigTab::~ConfigTab() { delete ui; }

// Auto-connected slots
void ConfigTab::on_saveButton_clicked() { onSavePbtxt(); }
void ConfigTab::on_loadButton_clicked() { onLoadPbtxt(); }
void ConfigTab::on_clearButton_clicked() { onClear(); }

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

void ConfigTab::onLoadPbtxt() {
    if (!rootNode_) return;
    QString filename = QFileDialog::getOpenFileName(this, "Load configuration", QString(), "Prototxt (*.pbtxt)");
    if (filename.isEmpty()) return;

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Error", "Failed to open file for reading.");
        return;
    }
    QByteArray data = file.readAll();

    config::Config cfg;
    if (!google::protobuf::TextFormat::ParseFromString(data.toStdString(), &cfg)) {
        QMessageBox::critical(this, "Error", "Failed to parse text proto.");
        return;
    }

    readMessageIntoNode(cfg, rootNode_.get());
}

void ConfigTab::onClear() {
    if (!rootNode_) return;
    clearMessageNode(rootNode_.get());
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

void ConfigTab::readMessageIntoNode(const Message& message, MessageNode* node) {
    // Scalars
    for (auto& s : node->scalarFields) {
        readScalarFieldIntoEditor(message, &s);
    }
    // Enums
    for (auto& e : node->enumFields) {
        readEnumFieldIntoEditor(message, &e);
    }
    // Non-repeated messages
    for (auto& m : node->messageFields) {
        readMessageFieldIntoChild(message, &m);
    }
    // Repeated messages
    for (auto& r : node->repeatedMessageFields) {
        readRepeatedMessageFieldIntoChildren(message, &r);
    }
    // Oneofs
    for (auto& o : node->oneofs) {
        readOneofIntoNode(message, &o);
    }
}

void ConfigTab::readScalarFieldIntoEditor(const Message& message, ScalarFieldNode* node) {
    const Reflection* refl = message.GetReflection();
    const FieldDescriptor* field = node->field;
    if (!refl->HasField(message, field)) {
        clearScalarField(node);
        return;
    }
    switch (field->type()) {
        case FieldDescriptor::TYPE_BOOL: {
            auto* box = qobject_cast<QCheckBox*>(node->editor);
            if (box) box->setChecked(refl->GetBool(message, field));
            break;
        }
        case FieldDescriptor::TYPE_STRING: {
            auto* edit = qobject_cast<QLineEdit*>(node->editor);
            if (edit) edit->setText(QString::fromStdString(refl->GetString(message, field)));
            break;
        }
        case FieldDescriptor::TYPE_INT32: {
            auto* spin = qobject_cast<QSpinBox*>(node->editor);
            if (spin) spin->setValue(refl->GetInt32(message, field));
            break;
        }
        case FieldDescriptor::TYPE_UINT32: {
            auto* spin = qobject_cast<QSpinBox*>(node->editor);
            if (spin) spin->setValue(static_cast<int>(refl->GetUInt32(message, field)));
            break;
        }
        case FieldDescriptor::TYPE_INT64: {
            auto* edit = qobject_cast<QLineEdit*>(node->editor);
            if (edit) edit->setText(QString::number(refl->GetInt64(message, field)));
            break;
        }
        case FieldDescriptor::TYPE_UINT64: {
            auto* edit = qobject_cast<QLineEdit*>(node->editor);
            if (edit) edit->setText(QString::number(refl->GetUInt64(message, field)));
            break;
        }
        case FieldDescriptor::TYPE_FLOAT: {
            auto* spin = qobject_cast<QDoubleSpinBox*>(node->editor);
            if (spin) spin->setValue(refl->GetFloat(message, field));
            break;
        }
        case FieldDescriptor::TYPE_DOUBLE: {
            auto* spin = qobject_cast<QDoubleSpinBox*>(node->editor);
            if (spin) spin->setValue(refl->GetDouble(message, field));
            break;
        }
        default:
            break;
    }
}

void ConfigTab::readEnumFieldIntoEditor(const Message& message, EnumFieldNode* node) {
    const Reflection* refl = message.GetReflection();
    const FieldDescriptor* field = node->field;
    if (!refl->HasField(message, field)) {
        node->combo->setCurrentIndex(0);
        return;
    }
    const auto* ev = refl->GetEnum(message, field);
    int number = ev ? ev->number() : -1;
    // Find combo index with matching data
    for (int i = 0; i < node->combo->count(); ++i) {
        if (node->combo->itemData(i).toInt() == number) {
            node->combo->setCurrentIndex(i);
            return;
        }
    }
    node->combo->setCurrentIndex(0);
}

void ConfigTab::readMessageFieldIntoChild(const Message& message, MessageFieldNode* node) {
    const Reflection* refl = message.GetReflection();
    const FieldDescriptor* field = node->field;
    if (!refl->HasField(message, field)) {
        clearMessageNode(node->child.get());
        return;
    }
    const Message& sub = refl->GetMessage(message, field);
    readMessageIntoNode(sub, node->child.get());
}

void ConfigTab::readRepeatedMessageFieldIntoChildren(const Message& message, RepeatedMessageFieldNode* node) {
    const Reflection* refl = message.GetReflection();
    const FieldDescriptor* field = node->field;

    // Clear current items
    for (auto& item : node->items) {
        if (item && item->group) {
            node->itemsLayout->removeWidget(item->group);
            item->group->deleteLater();
        }
    }
    node->items.clear();

    int count = refl->FieldSize(message, field);
    for (int i = 0; i < count; ++i) {
        auto child = buildMessageNode(field->message_type(), QString::fromStdString(field->message_type()->name()));
        node->itemsLayout->addWidget(child->group);
        node->items.emplace_back(std::move(child));
        const Message& sub = refl->GetRepeatedMessage(message, field, i);
        readMessageIntoNode(sub, node->items.back().get());
    }
}

void ConfigTab::readOneofIntoNode(const Message& message, OneofNode* node) {
    const Reflection* refl = message.GetReflection();
    const OneofDescriptor* oneof = node->oneof;
    const FieldDescriptor* setField = refl->GetOneofFieldDescriptor(message, oneof);
    if (!setField) {
        node->selector->setCurrentIndex(0);
        return;
    }
    // Find index for the set field
    int idx = 0;
    for (size_t i = 0; i < node->choiceFields.size(); ++i) {
        if (node->choiceFields[i] == setField) {
            idx = static_cast<int>(i) + 1; // +1 because 0 is <none>
            break;
        }
    }
    node->selector->setCurrentIndex(idx);
    if (idx > 0) {
        const Message& sub = message.GetReflection()->GetMessage(message, setField);
        readMessageIntoNode(sub, node->choiceNodes[idx - 1].get());
    }
}

void ConfigTab::clearMessageNode(MessageNode* node) {
    for (auto& s : node->scalarFields) clearScalarField(&s);
    for (auto& e : node->enumFields) clearEnumField(&e);
    for (auto& m : node->messageFields) clearMessageNode(m.child.get());

    // Clear repeated items visually and model-wise
    for (auto& r : node->repeatedMessageFields) {
        for (auto& item : r.items) {
            if (item && item->group) {
                r.itemsLayout->removeWidget(item->group);
                item->group->deleteLater();
            }
        }
        r.items.clear();
    }

    // Reset oneofs
    for (auto& o : node->oneofs) {
        o.selector->setCurrentIndex(0);
        if (o.stacked) o.stacked->setCurrentIndex(0);
    }
}

void ConfigTab::clearScalarField(ScalarFieldNode* node) {
    switch (node->field->type()) {
        case FieldDescriptor::TYPE_BOOL: {
            if (auto* box = qobject_cast<QCheckBox*>(node->editor)) box->setChecked(false);
            break;
        }
        case FieldDescriptor::TYPE_STRING: {
            if (auto* edit = qobject_cast<QLineEdit*>(node->editor)) edit->clear();
            break;
        }
        case FieldDescriptor::TYPE_INT32:
        case FieldDescriptor::TYPE_UINT32: {
            if (auto* spin = qobject_cast<QSpinBox*>(node->editor)) spin->setValue(0);
            break;
        }
        case FieldDescriptor::TYPE_INT64:
        case FieldDescriptor::TYPE_UINT64: {
            if (auto* edit = qobject_cast<QLineEdit*>(node->editor)) edit->clear();
            break;
        }
        case FieldDescriptor::TYPE_FLOAT:
        case FieldDescriptor::TYPE_DOUBLE: {
            if (auto* spin = qobject_cast<QDoubleSpinBox*>(node->editor)) spin->setValue(0.0);
            break;
        }
        default:
            break;
    }
}

void ConfigTab::clearEnumField(EnumFieldNode* node) {
    if (node->combo) node->combo->setCurrentIndex(0);
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