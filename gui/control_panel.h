#ifndef CONTROL_PANEL_H
#define CONTROL_PANEL_H

#include <QtWidgets/QWidget>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSlider>
#include <QtWidgets/QLabel>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtCore/QTimer>

class ControlPanel : public QWidget {
    Q_OBJECT

public:
    explicit ControlPanel(QWidget *parent = nullptr);
    ~ControlPanel() = default;

signals:
    void robotCommand(const QString &command, const QVariantMap &params);

private slots:
    void onEmergencyStop();
    void onStartRobot();
    void onStopRobot();
    void onResetRobot();
    void onSpeedChanged(int value);
    void onModeChanged(int index);

private:
    void setupUI();
    void setupRobotControls();
    void setupSpeedControls();
    void setupModeControls();
    void connectSignals();

    // Robot control widgets
    QGroupBox* robotGroup_;
    QPushButton* emergencyStopBtn_;
    QPushButton* startBtn_;
    QPushButton* stopBtn_;
    QPushButton* resetBtn_;
    
    // Speed control widgets
    QGroupBox* speedGroup_;
    QSlider* speedSlider_;
    QLabel* speedLabel_;
    QSpinBox* speedSpinBox_;
    
    // Mode control widgets
    QGroupBox* modeGroup_;
    QComboBox* modeComboBox_;
    QCheckBox* autoModeCheckBox_;
    
    // Status
    bool robotRunning_;
};

#endif // CONTROL_PANEL_H 