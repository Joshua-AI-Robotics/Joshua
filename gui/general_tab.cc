#include "general_tab.h"
#include "ui_general_tab.h"

#include <QtWidgets/QMessageBox>

GeneralTab::GeneralTab(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::GeneralTab)
    , startTime_(QDateTime::currentDateTime())
{
    ui->setupUi(this);

    // Initialize defaults matching .ui
    ui->startBtn->setEnabled(true);
    ui->stopBtn->setEnabled(false);

    // Start status update timer (1 second)
    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &GeneralTab::updateStatus);
    statusTimer_->start(1000);

    addLogEntry("System initialized");
}

GeneralTab::~GeneralTab() {
    delete ui;
}

// Auto-connected slots
void GeneralTab::on_emergencyStopBtn_clicked() { onEmergencyStop(); }
void GeneralTab::on_startBtn_clicked() { onStartRobot(); }
void GeneralTab::on_stopBtn_clicked() { onStopRobot(); }
void GeneralTab::on_resetBtn_clicked() { onResetRobot(); }
void GeneralTab::on_speedSlider_valueChanged(int value) { onSpeedChanged(value); }
void GeneralTab::on_speedSpinBox_valueChanged(int value) { ui->speedSlider->setValue(value); }
void GeneralTab::on_modeComboBox_currentIndexChanged(int index) { onModeChanged(index); }

// Helper implementations (migrated from previous handlers)
void GeneralTab::onEmergencyStop() {
    robotRunning_ = false;
    ui->startBtn->setEnabled(true);
    ui->stopBtn->setEnabled(false);

    QVariantMap params;
    params["emergency"] = true;
    onRobotCommand("emergency_stop", params);

    QMessageBox::warning(this, "Emergency Stop", "Emergency stop activated!");
}

void GeneralTab::onStartRobot() {
    robotRunning_ = true;
    ui->startBtn->setEnabled(false);
    ui->stopBtn->setEnabled(true);

    QVariantMap params;
    params["speed"] = ui->speedSlider->value();
    onRobotCommand("start", params);
}

void GeneralTab::onStopRobot() {
    robotRunning_ = false;
    ui->startBtn->setEnabled(true);
    ui->stopBtn->setEnabled(false);

    QVariantMap params;
    onRobotCommand("stop", params);
}

void GeneralTab::onResetRobot() {
    QVariantMap params;
    onRobotCommand("reset", params);
}

void GeneralTab::onSpeedChanged(int value) {
    ui->speedLabel->setText(QString("Speed: %1%").arg(value));

    if (robotRunning_) {
        QVariantMap params;
        params["speed"] = value;
        onRobotCommand("set_speed", params);
    }
}

void GeneralTab::onModeChanged(int /*index*/) {
    QVariantMap params;
    params["mode"] = ui->modeComboBox->currentText();
    onRobotCommand("set_mode", params);
}

void GeneralTab::onRobotCommand(const QString &command, const QVariantMap &params) {
    QString logMessage = QString("Command: %1").arg(command);
    if (!params.isEmpty()) {
        QStringList paramStrings;
        for (auto it = params.begin(); it != params.end(); ++it) {
            paramStrings << QString("%1=%2").arg(it.key()).arg(it.value().toString());
        }
        logMessage += QString(" (%1)").arg(paramStrings.join(", "));
    }

    addLogEntry(logMessage);

    if (command == "start") {
        robotStatus_ = "Running";
        ui->robotStatusLabel->setText("Status: Running");
        ui->robotStatusLabel->setStyleSheet("font-weight: bold; color: #4caf50;");
    } else if (command == "stop" || command == "emergency_stop") {
        robotStatus_ = "Stopped";
        ui->robotStatusLabel->setText("Status: Stopped");
        ui->robotStatusLabel->setStyleSheet("font-weight: bold; color: #f57c00;");
    } else if (command == "set_speed") {
        currentSpeed_ = params.value("speed", 0.0).toDouble();
        ui->currentSpeedLabel->setText(QString("Speed: %1 m/s").arg(currentSpeed_));
    }
}

void GeneralTab::updateStatus() {
    updateSystemStatus();
    updateRobotStatus();
}

void GeneralTab::updateSystemStatus() {
    static int cpuCounter = 0;
    static int memoryCounter = 0;

    int cpuUsage = 20 + (cpuCounter % 30);
    int memoryUsage = 40 + (memoryCounter % 20);

    ui->cpuLabel->setText(QString("CPU: %1%").arg(cpuUsage));
    ui->cpuBar->setValue(cpuUsage);

    ui->memoryLabel->setText(QString("Memory: %1%").arg(memoryUsage));
    ui->memoryBar->setValue(memoryUsage);

    QDateTime now = QDateTime::currentDateTime();
    qint64 uptimeSeconds = startTime_.secsTo(now);
    int hours = uptimeSeconds / 3600;
    int minutes = (uptimeSeconds % 3600) / 60;
    int seconds = uptimeSeconds % 60;
    ui->uptimeLabel->setText(QString("Uptime: %1:%2:%3")
                         .arg(hours, 2, 10, QChar('0'))
                         .arg(minutes, 2, 10, QChar('0'))
                         .arg(seconds, 2, 10, QChar('0')));

    cpuCounter++;
    memoryCounter++;
}

void GeneralTab::updateRobotStatus() {
    if (robotStatus_ == "Running") {
        if (batteryLevel_ > 0) {
            batteryLevel_ = qMax(0, batteryLevel_ - 1);
        }
        temperature_ = qMin(80.0, temperature_ + 0.5);
    } else {
        temperature_ = qMax(25.0, temperature_ - 0.2);
    }

    ui->batteryLabel->setText(QString("Battery: %1%").arg(batteryLevel_));
    ui->batteryBar->setValue(batteryLevel_);

    if (batteryLevel_ > 50) {
        ui->batteryBar->setStyleSheet("QProgressBar::chunk { background-color: #4caf50; }");
    } else if (batteryLevel_ > 20) {
        ui->batteryBar->setStyleSheet("QProgressBar::chunk { background-color: #ff9800; }");
    } else {
        ui->batteryBar->setStyleSheet("QProgressBar::chunk { background-color: #f44336; }");
    }

    ui->temperatureLabel->setText(QString("Temperature: %1°C").arg(temperature_, 0, 'f', 1));
    ui->temperatureBar->setValue(qMin(100, static_cast<int>(temperature_)));

    if (temperature_ < 40) {
        ui->temperatureBar->setStyleSheet("QProgressBar::chunk { background-color: #4caf50; }");
    } else if (temperature_ < 60) {
        ui->temperatureBar->setStyleSheet("QProgressBar::chunk { background-color: #ff9800; }");
    } else {
        ui->temperatureBar->setStyleSheet("QProgressBar::chunk { background-color: #f44336; }");
    }
}

void GeneralTab::addLogEntry(const QString &message) {
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString logEntry = QString("[%1] %2").arg(timestamp).arg(message);

    ui->logTextEdit->append(logEntry);

    QStringList lines = ui->logTextEdit->toPlainText().split('\n');
    if (lines.size() > 100) {
        lines = lines.mid(lines.size() - 100);
        ui->logTextEdit->setPlainText(lines.join('\n'));
    }

    QTextCursor cursor = ui->logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    ui->logTextEdit->setTextCursor(cursor);
} 