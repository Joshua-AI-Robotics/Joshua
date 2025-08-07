#include "general_tab.h"
#include "ui_general_tab.h"

#include <QtWidgets/QMessageBox>
#include <QtCore/QProcess>

GeneralTab::GeneralTab(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::GeneralTab)
    , startTime_(QDateTime::currentDateTime())
{
    ui->setupUi(this);

    // Start status update timer (1 second)
    statusTimer_ = new QTimer(this);
    connect(statusTimer_, &QTimer::timeout, this, &GeneralTab::updateStatus);
    statusTimer_->start(1000);

    updateSystemInfo();
}

GeneralTab::~GeneralTab() {
    delete ui;
}

void GeneralTab::updateStatus() {
    updateSystemStatus();
    updateRobotStatus();
}

void GeneralTab::updateSystemStatus() {
    // Get memory usage info.
    QProcess process;
    process.start("bash", QStringList() << "-c" << "free -m | awk 'NR==2{printf \"%.2f%%\\n\", $3/$2*100}'");
    process.waitForFinished();
    QString output = process.readAllStandardOutput();
    ui->memory_usage_lineEdit->setText(output);

    QDateTime now = QDateTime::currentDateTime();
    qint64 uptimeSeconds = startTime_.secsTo(now);
    int hours = uptimeSeconds / 3600;
    int minutes = (uptimeSeconds % 3600) / 60;
    int seconds = uptimeSeconds % 60;
    ui->uptimeLabel->setText(QString("Uptime: %1:%2:%3")
                         .arg(hours, 2, 10, QChar('0'))
                         .arg(minutes, 2, 10, QChar('0'))
                         .arg(seconds, 2, 10, QChar('0')));
}

void GeneralTab::updateRobotStatus() {
    // TODO: Implement robot status update.
}

void GeneralTab::updateSystemInfo() {
    ui->os_lineEdit->setText(QString("OS: %1").arg(QSysInfo::prettyProductName()));
    ui->cpu_lineEdit->setText(QString("CPU: %1").arg(QSysInfo::currentCpuArchitecture()));
    // Get GPU info.
    QProcess process;   
    process.start("nvidia-smi");
    process.waitForFinished();
    QString output = process.readAllStandardOutput();
    ui->gpu_textEdit->setText(output);
}
