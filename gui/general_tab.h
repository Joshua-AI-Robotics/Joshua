#ifndef GENERAL_TAB_H
#define GENERAL_TAB_H

#include <QtWidgets/QWidget>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtCore/QVariant>

namespace Ui { class GeneralTab; }

class GeneralTab : public QWidget {
    Q_OBJECT
public:
    explicit GeneralTab(QWidget* parent = nullptr);
    ~GeneralTab();

    void updateStatus();
    void updateSystemStatus();
    void updateRobotStatus();

private slots:
    // Auto-connected slots based on objectName and signal

private:
    void updateSystemInfo();
    void addLogEntry(const QString &message);

    Ui::GeneralTab* ui; // owns the UI

    QDateTime startTime_;
    int batteryLevel_ = 100;
    double temperature_ = 25.0;

    // Internal status update timer (1s)
    QTimer* statusTimer_ = nullptr;
};

#endif // GENERAL_TAB_H 