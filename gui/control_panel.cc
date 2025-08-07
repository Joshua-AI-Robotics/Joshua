#include "control_panel.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyle>
#include <QtWidgets/QMessageBox>

ControlPanel::ControlPanel(QWidget *parent)
    : QWidget(parent)
    , robotRunning_(false)
{
    setupUI();
    connectSignals();
}

void ControlPanel::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
    setupRobotControls();
    setupSpeedControls();
    setupModeControls();
    
    // Add all groups to main layout
    mainLayout->addWidget(robotGroup_);
    mainLayout->addWidget(speedGroup_);
    mainLayout->addWidget(modeGroup_);
    mainLayout->addStretch();
}

void ControlPanel::setupRobotControls() {
    robotGroup_ = new QGroupBox("Robot Control");
    QVBoxLayout *layout = new QVBoxLayout(robotGroup_);
    
    // Emergency stop button (large and red)
    emergencyStopBtn_ = new QPushButton("EMERGENCY STOP");
    emergencyStopBtn_->setStyleSheet(
        "QPushButton { "
        "background-color: #d32f2f; "
        "color: white; "
        "font-weight: bold; "
        "font-size: 14px; "
        "padding: 15px; "
        "border: 2px solid #b71c1c; "
        "border-radius: 8px; "
        "}"
        "QPushButton:hover { "
        "background-color: #b71c1c; "
        "}"
        "QPushButton:pressed { "
        "background-color: #8e0000; "
        "}"
    );
    layout->addWidget(emergencyStopBtn_);
    
    // Control buttons
    QHBoxLayout *controlLayout = new QHBoxLayout();
    
    startBtn_ = new QPushButton("Start");
    startBtn_->setStyleSheet(
        "QPushButton { "
        "background-color: #388e3c; "
        "color: white; "
        "padding: 8px; "
        "border-radius: 4px; "
        "}"
        "QPushButton:hover { "
        "background-color: #2e7d32; "
        "}"
    );
    
    stopBtn_ = new QPushButton("Stop");
    stopBtn_->setStyleSheet(
        "QPushButton { "
        "background-color: #f57c00; "
        "color: white; "
        "padding: 8px; "
        "border-radius: 4px; "
        "}"
        "QPushButton:hover { "
        "background-color: #ef6c00; "
        "}"
    );
    
    resetBtn_ = new QPushButton("Reset");
    resetBtn_->setStyleSheet(
        "QPushButton { "
        "background-color: #1976d2; "
        "color: white; "
        "padding: 8px; "
        "border-radius: 4px; "
        "}"
        "QPushButton:hover { "
        "background-color: #1565c0; "
        "}"
    );
    
    controlLayout->addWidget(startBtn_);
    controlLayout->addWidget(stopBtn_);
    controlLayout->addWidget(resetBtn_);
    
    layout->addLayout(controlLayout);
}

void ControlPanel::setupSpeedControls() {
    speedGroup_ = new QGroupBox("Speed Control");
    QVBoxLayout *layout = new QVBoxLayout(speedGroup_);
    
    // Speed slider
    speedSlider_ = new QSlider(Qt::Horizontal);
    speedSlider_->setRange(0, 100);
    speedSlider_->setValue(50);
    speedSlider_->setTickPosition(QSlider::TicksBelow);
    speedSlider_->setTickInterval(10);
    layout->addWidget(speedSlider_);
    
    // Speed display
    QHBoxLayout *speedDisplayLayout = new QHBoxLayout();
    speedLabel_ = new QLabel("Speed: 50%");
    speedSpinBox_ = new QSpinBox();
    speedSpinBox_->setRange(0, 100);
    speedSpinBox_->setValue(50);
    speedSpinBox_->setSuffix("%");
    
    speedDisplayLayout->addWidget(speedLabel_);
    speedDisplayLayout->addWidget(speedSpinBox_);
    layout->addLayout(speedDisplayLayout);
}

void ControlPanel::setupModeControls() {
    modeGroup_ = new QGroupBox("Operation Mode");
    QVBoxLayout *layout = new QVBoxLayout(modeGroup_);
    
    // Mode selection
    modeComboBox_ = new QComboBox();
    modeComboBox_->addItem("Manual Control");
    modeComboBox_->addItem("Autonomous");
    modeComboBox_->addItem("Teleoperation");
    modeComboBox_->addItem("Calibration");
    layout->addWidget(modeComboBox_);
    
    // Auto mode checkbox
    autoModeCheckBox_ = new QCheckBox("Enable Auto Mode");
    layout->addWidget(autoModeCheckBox_);
}

void ControlPanel::connectSignals() {
    // Robot control signals
    connect(emergencyStopBtn_, &QPushButton::clicked, this, &ControlPanel::onEmergencyStop);
    connect(startBtn_, &QPushButton::clicked, this, &ControlPanel::onStartRobot);
    connect(stopBtn_, &QPushButton::clicked, this, &ControlPanel::onStopRobot);
    connect(resetBtn_, &QPushButton::clicked, this, &ControlPanel::onResetRobot);
    
    // Speed control signals
    connect(speedSlider_, &QSlider::valueChanged, this, &ControlPanel::onSpeedChanged);
    connect(speedSpinBox_, static_cast<void(QSpinBox::*)(int)>(&QSpinBox::valueChanged), 
            speedSlider_, &QSlider::setValue);
    connect(speedSlider_, &QSlider::valueChanged, 
            speedSpinBox_, &QSpinBox::setValue);
    
    // Mode control signals
    connect(modeComboBox_, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &ControlPanel::onModeChanged);
}

void ControlPanel::onEmergencyStop() {
    robotRunning_ = false;
    startBtn_->setEnabled(true);
    stopBtn_->setEnabled(false);
    
    QVariantMap params;
    params["emergency"] = true;
    emit robotCommand("emergency_stop", params);
    
    QMessageBox::warning(this, "Emergency Stop", "Emergency stop activated!");
}

void ControlPanel::onStartRobot() {
    robotRunning_ = true;
    startBtn_->setEnabled(false);
    stopBtn_->setEnabled(true);
    
    QVariantMap params;
    params["speed"] = speedSlider_->value();
    emit robotCommand("start", params);
}

void ControlPanel::onStopRobot() {
    robotRunning_ = false;
    startBtn_->setEnabled(true);
    stopBtn_->setEnabled(false);
    
    QVariantMap params;
    emit robotCommand("stop", params);
}

void ControlPanel::onResetRobot() {
    QVariantMap params;
    emit robotCommand("reset", params);
}

void ControlPanel::onSpeedChanged(int value) {
    speedLabel_->setText(QString("Speed: %1%").arg(value));
    
    if (robotRunning_) {
        QVariantMap params;
        params["speed"] = value;
        emit robotCommand("set_speed", params);
    }
}

void ControlPanel::onModeChanged(int index) {
    QVariantMap params;
    params["mode"] = modeComboBox_->currentText();
    emit robotCommand("set_mode", params);
} 