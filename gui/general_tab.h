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

private slots:
    // Auto-connected slots based on objectName and signal
    void on_emergencyStopBtn_clicked();
    void on_startBtn_clicked();
    void on_stopBtn_clicked();
    void on_resetBtn_clicked();
    void on_speedSlider_valueChanged(int value);
    void on_speedSpinBox_valueChanged(int value);
    void on_modeComboBox_currentIndexChanged(int index);

    void onRobotCommand(const QString &command, const QVariantMap &params);

private:
    // Previous handlers converted to helpers
    void onEmergencyStop();
    void onStartRobot();
    void onStopRobot();
    void onResetRobot();
    void onSpeedChanged(int value);
    void onModeChanged(int index);

    void updateSystemStatus();
    void updateRobotStatus();
    void addLogEntry(const QString &message);

    Ui::GeneralTab* ui; // owns the UI

    // State
    bool robotRunning_ = false;
    QString robotStatus_ = "Stopped";
    int batteryLevel_ = 100;
    double temperature_ = 25.0;
    double currentSpeed_ = 0.0;
    QDateTime startTime_;

    // Internal status update timer (1s)
    QTimer* statusTimer_ = nullptr;
};

#endif // GENERAL_TAB_H 