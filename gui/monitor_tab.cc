#include "monitor_tab.h"
#include "ui_monitor_tab.h"

#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QLabel>

MonitorTab::MonitorTab(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::MonitorTab)
{
    ui->setupUi(this);

    // Initialize node table headers if needed (already set in .ui)
    ui->nodeTable->setColumnCount(6);
    QStringList headers{"Name", "State", "PID", "CPU%", "Mem", "Uptime"};
    ui->nodeTable->setHorizontalHeaderLabels(headers);
    ui->nodeTable->setRowCount(0);

    // Placeholder initial log
    ui->logTextEdit->append("[INFO] Monitor ready. Use Launch to start ROS2 nodes.");
}

MonitorTab::~MonitorTab() { delete ui; }

void MonitorTab::on_launchButton_clicked() {
    // Placeholder: simulate nodes launching
    ui->statusLabel->setText("Status: Launching...");
    ui->logTextEdit->append("[INFO] Launch requested.");

    // Add example row
    int row = ui->nodeTable->rowCount();
    ui->nodeTable->insertRow(row);
    ui->nodeTable->setItem(row, 0, new QTableWidgetItem("example_node"));
    ui->nodeTable->setItem(row, 1, new QTableWidgetItem("running"));
    ui->nodeTable->setItem(row, 2, new QTableWidgetItem("12345"));
    ui->nodeTable->setItem(row, 3, new QTableWidgetItem("1.2"));
    ui->nodeTable->setItem(row, 4, new QTableWidgetItem("48MB"));
    ui->nodeTable->setItem(row, 5, new QTableWidgetItem("00:00:05"));

    ui->statusLabel->setText("Status: Running");
}

void MonitorTab::on_stopButton_clicked() {
    // Placeholder: simulate nodes stopping
    ui->statusLabel->setText("Status: Stopping...");
    ui->logTextEdit->append("[INFO] Stop requested.");

    // Clear table
    ui->nodeTable->setRowCount(0);
    ui->statusLabel->setText("Status: Idle");
}

void MonitorTab::on_restartButton_clicked() {
    // Placeholder: simulate restart
    ui->logTextEdit->append("[INFO] Restart requested.");
    on_stopButton_clicked();
    on_launchButton_clicked();
} 