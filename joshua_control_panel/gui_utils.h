#pragma once

#include <QtWidgets/QProgressDialog>
#include <QtWidgets/QMessageBox>
#include <QtCore/QStringList>
#include <QtCore/QtGlobal>
#include <vector>

namespace gui {

// A small helper to show a modal, indeterminate progress dialog
// with a list of textual steps that can be marked complete.
class StepProgressDialog {
public:
    StepProgressDialog(QWidget* parent,
                       const QString& windowTitle,
                       const QStringList& stepLabels);

    void show();
    void close();

    // Mark step at index as done (adds a check mark)
    void markStepDone(int index);

    // Optionally append a final line at the bottom (e.g., "Finalizing cleanup...")
    void setFinalMessage(const QString& finalLine);

private:
    void refreshLabel();

    QProgressDialog* dialog_ {nullptr};
    QString window_title_;
    QStringList step_labels_;
    std::vector<bool> step_done_;
    QString final_message_;
};

namespace dialog {

// Shows a Yes/No question dialog and returns the selected button.
QMessageBox::StandardButton askYesNo(
    QWidget* parent,
    const QString& title,
    const QString& question,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton);

// Convenience wrapper: returns true only if user selects Yes.
bool confirm(
    QWidget* parent,
    const QString& title,
    const QString& question,
    QMessageBox::StandardButton defaultButton = QMessageBox::No);

void info(QWidget* parent, const QString& title, const QString& message);
void warning(QWidget* parent, const QString& title, const QString& message);
void error(QWidget* parent, const QString& title, const QString& message);

}  // namespace dialog

namespace progress {

// Opaque handle for thread-safe progress dialogs managed on the GUI thread
struct Handle { quint64 id {0}; };

// Thread-safe: can be called from any thread. Returns a handle to the dialog.
Handle show(QWidget* parent,
            const QString& title,
            const QStringList& steps);

// Thread-safe update helpers
void markStepDone(const Handle& handle, int stepIndex);
void setFinalMessage(const Handle& handle, const QString& finalLine);
void close(const Handle& handle);

}  // namespace progress

}  // namespace gui 