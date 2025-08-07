#include "status_panel.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QStyle>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QProcess>

StatusPanel::StatusPanel(QWidget *parent)
    : QWidget(parent)
    , robotStatus_("Stopped")
    , batteryLevel_(100)
    , temperature_(25.0)
    , currentSpeed_(0.0)
    , startTime_(QDateTime::currentDateTime())
    , statusTimer_(new QTimer(this))
{
    setupUI();
    
    // Start status update timer (1 second intervals)
    connect(statusTimer_, &QTimer::timeout, this, &StatusPanel::updateStatus);
    statusTimer_->start(1000);
}

void StatusPanel::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
    setupSystemStatus();
    setupRobotStatus();
    setupLogDisplay();
    
    // Add all groups to main layout
    mainLayout->addWidget(systemGroup_);
    mainLayout->addWidget(robotGroup_);
    mainLayout->addWidget(logGroup_);
}

void StatusPanel::setupSystemStatus() {
    systemGroup_ = new QGroupBox("System Status");
    QVBoxLayout *layout = new QVBoxLayout(systemGroup_);
    
    // CPU usage
    QHBoxLayout *cpuLayout = new QHBoxLayout();
    cpuLabel_ = new QLabel("CPU: 0%");
    cpuBar_ = new QProgressBar();
    cpuBar_->setRange(0, 100);
    cpuBar_->setValue(0);
    cpuLayout->addWidget(cpuLabel_);
    cpuLayout->addWidget(cpuBar_);
    layout->addLayout(cpuLayout);
    
    // Memory usage
    QHBoxLayout *memoryLayout = new QHBoxLayout();
    memoryLabel_ = new QLabel("Memory: 0%");
    memoryBar_ = new QProgressBar();
    memoryBar_->setRange(0, 100);
    memoryBar_->setValue(0);
    memoryLayout->addWidget(memoryLabel_);
    memoryLayout->addWidget(memoryBar_);
    layout->addLayout(memoryLayout);
    
    // Uptime
    uptimeLabel_ = new QLabel("Uptime: 00:00:00");
    layout->addWidget(uptimeLabel_);
}

void StatusPanel::setupRobotStatus() {
    robotGroup_ = new QGroupBox("Robot Status");
    QVBoxLayout *layout = new QVBoxLayout(robotGroup_);
    
    // Robot status
    robotStatusLabel_ = new QLabel("Status: Stopped");
    robotStatusLabel_->setStyleSheet("font-weight: bold; color: #f57c00;");
    layout->addWidget(robotStatusLabel_);
    
    // Battery level
    QHBoxLayout *batteryLayout = new QHBoxLayout();
    batteryLabel_ = new QLabel("Battery: 100%");
    batteryBar_ = new QProgressBar();
    batteryBar_->setRange(0, 100);
    batteryBar_->setValue(100);
    batteryBar_->setStyleSheet(
        "QProgressBar::chunk { "
        "background-color: #4caf50; "
        "}"
    );
    batteryLayout->addWidget(batteryLabel_);
    batteryLayout->addWidget(batteryBar_);
    layout->addLayout(batteryLayout);
    
    // Temperature
    QHBoxLayout *tempLayout = new QHBoxLayout();
    temperatureLabel_ = new QLabel("Temperature: 25.0°C");
    temperatureBar_ = new QProgressBar();
    temperatureBar_->setRange(0, 100);
    temperatureBar_->setValue(25);
    temperatureBar_->setStyleSheet(
        "QProgressBar::chunk { "
        "background-color: #ff9800; "
        "}"
    );
    tempLayout->addWidget(temperatureLabel_);
    tempLayout->addWidget(temperatureBar_);
    layout->addLayout(tempLayout);
    
    // Current speed
    speedLabel_ = new QLabel("Speed: 0.0 m/s");
    layout->addWidget(speedLabel_);
}

void StatusPanel::setupLogDisplay() {
    logGroup_ = new QGroupBox("System Log");
    QVBoxLayout *layout = new QVBoxLayout(logGroup_);
    
    logTextEdit_ = new QTextEdit();
    logTextEdit_->setMaximumHeight(150);
    logTextEdit_->setReadOnly(true);
    logTextEdit_->setStyleSheet(
        "QTextEdit { "
        "background-color: #2d2d2d; "
        "color: #ffffff; "
        "font-family: 'Courier New', monospace; "
        "font-size: 10px; "
        "}"
    );
    layout->addWidget(logTextEdit_);
    
    // Add initial log entry
    addLogEntry("System initialized");
}

void StatusPanel::onRobotCommand(const QString &command, const QVariantMap &params) {
    QString logMessage = QString("Command: %1").arg(command);
    if (!params.isEmpty()) {
        QStringList paramStrings;
        for (auto it = params.begin(); it != params.end(); ++it) {
            paramStrings << QString("%1=%2").arg(it.key()).arg(it.value().toString());
        }
        logMessage += QString(" (%1)").arg(paramStrings.join(", "));
    }
    
    addLogEntry(logMessage);
    
    // Update robot status based on command
    if (command == "start") {
        robotStatus_ = "Running";
        robotStatusLabel_->setText("Status: Running");
        robotStatusLabel_->setStyleSheet("font-weight: bold; color: #4caf50;");
    } else if (command == "stop" || command == "emergency_stop") {
        robotStatus_ = "Stopped";
        robotStatusLabel_->setText("Status: Stopped");
        robotStatusLabel_->setStyleSheet("font-weight: bold; color: #f57c00;");
    } else if (command == "set_speed") {
        currentSpeed_ = params.value("speed", 0.0).toDouble();
        speedLabel_->setText(QString("Speed: %1 m/s").arg(currentSpeed_));
    }
}

void StatusPanel::updateStatus() {
    updateSystemStatus();
    updateRobotStatus();
}

void StatusPanel::updateSystemStatus() {
    // Simulate CPU and memory usage (in a real app, you'd read from /proc)
    static int cpuCounter = 0;
    static int memoryCounter = 0;
    
    int cpuUsage = 20 + (cpuCounter % 30); // Simulate 20-50% CPU usage
    int memoryUsage = 40 + (memoryCounter % 20); // Simulate 40-60% memory usage
    
    cpuLabel_->setText(QString("CPU: %1%").arg(cpuUsage));
    cpuBar_->setValue(cpuUsage);
    
    memoryLabel_->setText(QString("Memory: %1%").arg(memoryUsage));
    memoryBar_->setValue(memoryUsage);
    
    // Update uptime
    QDateTime now = QDateTime::currentDateTime();
    qint64 uptimeSeconds = startTime_.secsTo(now);
    int hours = uptimeSeconds / 3600;
    int minutes = (uptimeSeconds % 3600) / 60;
    int seconds = uptimeSeconds % 60;
    uptimeLabel_->setText(QString("Uptime: %1:%2:%3")
                         .arg(hours, 2, 10, QChar('0'))
                         .arg(minutes, 2, 10, QChar('0'))
                         .arg(seconds, 2, 10, QChar('0')));
    
    cpuCounter++;
    memoryCounter++;
}

void StatusPanel::updateRobotStatus() {
    // Simulate battery drain and temperature changes
    if (robotStatus_ == "Running") {
        // Simulate battery drain
        if (batteryLevel_ > 0) {
            batteryLevel_ = qMax(0, batteryLevel_ - 1);
        }
        
        // Simulate temperature increase
        temperature_ = qMin(80.0, temperature_ + 0.5);
    } else {
        // Simulate temperature decrease when stopped
        temperature_ = qMax(25.0, temperature_ - 0.2);
    }
    
    // Update battery display
    batteryLabel_->setText(QString("Battery: %1%").arg(batteryLevel_));
    batteryBar_->setValue(batteryLevel_);
    
    // Update battery bar color based on level
    if (batteryLevel_ > 50) {
        batteryBar_->setStyleSheet("QProgressBar::chunk { background-color: #4caf50; }");
    } else if (batteryLevel_ > 20) {
        batteryBar_->setStyleSheet("QProgressBar::chunk { background-color: #ff9800; }");
    } else {
        batteryBar_->setStyleSheet("QProgressBar::chunk { background-color: #f44336; }");
    }
    
    // Update temperature display
    temperatureLabel_->setText(QString("Temperature: %1°C").arg(temperature_, 0, 'f', 1));
    temperatureBar_->setValue(qMin(100, static_cast<int>(temperature_)));
    
    // Update temperature bar color
    if (temperature_ < 40) {
        temperatureBar_->setStyleSheet("QProgressBar::chunk { background-color: #4caf50; }");
    } else if (temperature_ < 60) {
        temperatureBar_->setStyleSheet("QProgressBar::chunk { background-color: #ff9800; }");
    } else {
        temperatureBar_->setStyleSheet("QProgressBar::chunk { background-color: #f44336; }");
    }
}

void StatusPanel::addLogEntry(const QString &message) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString logEntry = QString("[%1] %2").arg(timestamp).arg(message);
    
    logTextEdit_->append(logEntry);
    
    // Keep only last 100 entries
    QStringList lines = logTextEdit_->toPlainText().split('\n');
    if (lines.size() > 100) {
        lines = lines.mid(lines.size() - 100);
        logTextEdit_->setPlainText(lines.join('\n'));
    }
    
    // Auto-scroll to bottom
    QTextCursor cursor = logTextEdit_->textCursor();
    cursor.movePosition(QTextCursor::End);
    logTextEdit_->setTextCursor(cursor);
} 