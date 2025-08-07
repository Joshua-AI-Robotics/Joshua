#ifndef STATUS_PANEL_H
#define STATUS_PANEL_H

#include <QtWidgets/QWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTextEdit>
#include <QtCore/QTimer>
#include <QtCore/QDateTime>

class StatusPanel : public QWidget {
    Q_OBJECT

public:
    explicit StatusPanel(QWidget *parent = nullptr);
    ~StatusPanel() = default;

public slots:
    void onRobotCommand(const QString &command, const QVariantMap &params);
    void updateStatus();

private:
    void setupUI();
    void setupSystemStatus();
    void setupRobotStatus();
    void setupLogDisplay();
    void updateSystemStatus();
    void updateRobotStatus();
    void addLogEntry(const QString &message);

    // System status widgets
    QGroupBox* systemGroup_;
    QLabel* cpuLabel_;
    QLabel* memoryLabel_;
    QLabel* uptimeLabel_;
    QProgressBar* cpuBar_;
    QProgressBar* memoryBar_;
    
    // Robot status widgets
    QGroupBox* robotGroup_;
    QLabel* robotStatusLabel_;
    QLabel* batteryLabel_;
    QLabel* temperatureLabel_;
    QLabel* speedLabel_;
    QProgressBar* batteryBar_;
    QProgressBar* temperatureBar_;
    
    // Log display
    QGroupBox* logGroup_;
    QTextEdit* logTextEdit_;
    
    // Status data
    QString robotStatus_;
    int batteryLevel_;
    double temperature_;
    double currentSpeed_;
    QDateTime startTime_;
    
    // Update timer
    QTimer* statusTimer_;
};

#endif // STATUS_PANEL_H 