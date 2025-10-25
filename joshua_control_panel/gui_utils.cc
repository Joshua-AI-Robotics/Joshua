#include "gui_utils.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QMetaObject>
#include <atomic>
#include <map>

namespace gui {

StepProgressDialog::StepProgressDialog(QWidget* parent,
                                       const QString& windowTitle,
                                       const QStringList& stepLabels)
    : window_title_(windowTitle), step_labels_(stepLabels), step_done_(stepLabels.size(), false) {
  dialog_ = new QProgressDialog("", QString(), 0, 0, parent);
  dialog_->setWindowTitle(window_title_);
  dialog_->setCancelButton(nullptr);
  dialog_->setWindowModality(Qt::ApplicationModal);
  dialog_->setMinimumDuration(0);
  dialog_->setRange(0, 0);
  refreshLabel();
}

void StepProgressDialog::show() {
  if (dialog_) {
    dialog_->show();
    QCoreApplication::processEvents();
  }
}

void StepProgressDialog::close() {
  if (dialog_) {
    dialog_->close();
    QCoreApplication::processEvents();
  }
}

void StepProgressDialog::markStepDone(int index) {
  if (index < 0 || index >= static_cast<int>(step_done_.size())) {
    return;
  }
  step_done_[index] = true;
  refreshLabel();
  QCoreApplication::processEvents();
}

void StepProgressDialog::setFinalMessage(const QString& finalLine) {
  final_message_ = finalLine;
  refreshLabel();
  QCoreApplication::processEvents();
}

void StepProgressDialog::refreshLabel() {
  if (!dialog_) return;

  QStringList lines;
  lines << "Stopping, please wait...";
  for (int i = 0; i < step_labels_.size(); ++i) {
    const bool done = step_done_[i];
    const QString check = done ? " ✔" : "";
    lines << ("- " + step_labels_[i] + check);
  }
  if (!final_message_.isEmpty()) {
    lines << final_message_;
  }
  dialog_->setLabelText(lines.join("\n"));
}

namespace dialog {

QMessageBox::StandardButton askYesNo(QWidget* parent,
                                     const QString& title,
                                     const QString& question,
                                     QMessageBox::StandardButton defaultButton) {
  QMessageBox box(parent);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(title);
  box.setText(question);
  box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
  if (defaultButton != QMessageBox::NoButton) {
    box.setDefaultButton(defaultButton);
  }
  return static_cast<QMessageBox::StandardButton>(box.exec());
}

bool confirm(QWidget* parent,
             const QString& title,
             const QString& question,
             QMessageBox::StandardButton defaultButton) {
  return askYesNo(parent, title, question, defaultButton) == QMessageBox::Yes;
}

void info(QWidget* parent, const QString& title, const QString& message) {
  QMessageBox::information(parent, title, message);
}

void warning(QWidget* parent, const QString& title, const QString& message) {
  QMessageBox::warning(parent, title, message);
}

void error(QWidget* parent, const QString& title, const QString& message) {
  QMessageBox::critical(parent, title, message);
}

}  // namespace dialog

namespace progress {

class ProgressManager : public QObject {
 public:
  explicit ProgressManager(QObject* parent = nullptr) : QObject(parent) {}

  quint64 show(QWidget* parent, const QString& title, const QStringList& steps) {
    const quint64 id = ++next_id_;
    auto dialog = std::make_unique<gui::StepProgressDialog>(parent, title, steps);
    dialog->show();
    dialogs_[id] = std::move(dialog);
    return id;
  }

  void markStepDone(quint64 id, int stepIndex) {
    auto it = dialogs_.find(id);
    if (it == dialogs_.end()) return;
    it->second->markStepDone(stepIndex);
  }

  void setFinalMessage(quint64 id, const QString& finalLine) {
    auto it = dialogs_.find(id);
    if (it == dialogs_.end()) return;
    it->second->setFinalMessage(finalLine);
  }

  void close(quint64 id) {
    auto it = dialogs_.find(id);
    if (it == dialogs_.end()) return;
    it->second->close();
    dialogs_.erase(it);
  }

 private:
  std::atomic<quint64> next_id_{0};
  std::map<quint64, std::unique_ptr<gui::StepProgressDialog>> dialogs_;
};

static ProgressManager* ensureManager() {
  static ProgressManager* manager = nullptr;
  if (!manager) {
    manager = new ProgressManager(qApp);
  }
  return manager;
}

Handle show(QWidget* parent, const QString& title, const QStringList& steps) {
  Handle handle;
  QMetaObject::invokeMethod(
      ensureManager(),
      [parent, title, steps, &handle]() {
        handle.id = ensureManager()->show(parent, title, steps);
      },
      Qt::BlockingQueuedConnection);
  return handle;
}

void markStepDone(const Handle& handle, int stepIndex) {
  if (handle.id == 0) return;
  QMetaObject::invokeMethod(
      ensureManager(),
      [id = handle.id, stepIndex]() { ensureManager()->markStepDone(id, stepIndex); },
      Qt::QueuedConnection);
}

void setFinalMessage(const Handle& handle, const QString& finalLine) {
  if (handle.id == 0) return;
  QMetaObject::invokeMethod(
      ensureManager(),
      [id = handle.id, finalLine]() { ensureManager()->setFinalMessage(id, finalLine); },
      Qt::QueuedConnection);
}

void close(const Handle& handle) {
  if (handle.id == 0) return;
  QMetaObject::invokeMethod(
      ensureManager(), [id = handle.id]() { ensureManager()->close(id); }, Qt::QueuedConnection);
}

}  // namespace progress

}  // namespace gui
