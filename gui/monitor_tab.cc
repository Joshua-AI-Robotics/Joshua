#include "monitor_tab.h"
#include "ui_monitor_tab.h"

#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QLabel>
#include <QtCore/QDir>
#include <QtCore/QStringListModel>

namespace {
    constexpr auto kConfigPresetPrefix = "config/config_preset/";
}

MonitorTab::MonitorTab(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::MonitorTab)
    , stop_node_generator_build_(false)
{
    ui->setupUi(this);

    // Initialize node table headers if needed (already set in .ui)
    ui->nodeTable->setColumnCount(6);
    QStringList headers{"Name", "State", "PID", "CPU%", "Mem", "Uptime"};
    ui->nodeTable->setHorizontalHeaderLabels(headers);
    ui->nodeTable->setRowCount(0);

    // Initialize status label
    ui->statusLabel->setText("IDLE");
    ui->statusLabel->setStyleSheet("QLabel { background-color: #cccccc; padding: 5px; border-radius: 5px; }");

    // Initialize config_preset list view.
    QDir config_preset_dir("config/config_preset");
    QStringList config_preset_files = config_preset_dir.entryList(QDir::Files);
    ui->config_preset_listView->setModel(new QStringListModel(config_preset_files));

    // Connect signals for thread-safe UI updates
    connect(this, &MonitorTab::logMessage, this, &MonitorTab::onLogMessage, Qt::QueuedConnection);
    connect(this, &MonitorTab::updateStatus, this, &MonitorTab::onUpdateStatus, Qt::QueuedConnection);
    connect(this, &MonitorTab::setLaunchButtonEnabled, this, &MonitorTab::onSetLaunchButtonEnabled, Qt::QueuedConnection);

    // Placeholder initial log
    ui->logTextEdit->append("[INFO] Monitor ready. Use Launch to start ROS2 nodes.");
}

MonitorTab::~MonitorTab() { 
    // TODO: Stop any running thread
    delete ui; 
}

void MonitorTab::onLogMessage(const QString& message) {
    ui->logTextEdit->append(message);
}

void MonitorTab::onUpdateStatus(const QString& status, const QString& style) {
    ui->statusLabel->setText(status);
    ui->statusLabel->setStyleSheet(style);
}

void MonitorTab::onSetLaunchButtonEnabled(bool enabled) {
    ui->launchButton->setEnabled(enabled);
}

void MonitorTab::on_launchButton_clicked() {
    // Placeholder: simulate nodes launching
    onUpdateStatus("LAUNCHING", "QLabel { background-color: #ffa500; padding: 5px; border-radius: 5px; }");
    onLogMessage("[INFO] Launch requested.");
    onSetLaunchButtonEnabled(false);

    if(!setup_node_generator()) {
        onLogMessage("[ERROR] Failed to setup node generator");
        onUpdateStatus("ERROR", "QLabel { background-color: #ff0000; padding: 5px; border-radius: 5px; }");
        onSetLaunchButtonEnabled(true);
        return;
    }
    
    // Add example row
    int row = ui->nodeTable->rowCount();
    ui->nodeTable->insertRow(row);
    ui->nodeTable->setItem(row, 0, new QTableWidgetItem("example_node"));
    ui->nodeTable->setItem(row, 1, new QTableWidgetItem("running"));
    ui->nodeTable->setItem(row, 2, new QTableWidgetItem("12345"));
    ui->nodeTable->setItem(row, 3, new QTableWidgetItem("1.2"));
    ui->nodeTable->setItem(row, 4, new QTableWidgetItem("48MB"));
    ui->nodeTable->setItem(row, 5, new QTableWidgetItem("00:00:05"));

    onUpdateStatus("RUNNING", "QLabel { background-color: #4caf50; padding: 5px; border-radius: 5px; }");
}

void MonitorTab::on_stopButton_clicked() {
    // Placeholder: simulate nodes stopping
    onUpdateStatus("STOPPING", "QLabel { background-color: #ff0000; padding: 5px; border-radius: 5px; }");
    onLogMessage("[INFO] Stop requested.");

    // Signal thread to stop
    stop_node_generator_build_ = true;
    
    // Clear table
    ui->nodeTable->setRowCount(0);
    onUpdateStatus("IDLE", "QLabel { background-color: #cccccc; padding: 5px; border-radius: 5px; }");
    onSetLaunchButtonEnabled(true);
}

bool MonitorTab::setup_node_generator() {
    try {
        std::string config = kConfigPresetPrefix + ui->config_preset_listView->currentIndex().data().toString().toStdString();
        node_generator_ = std::make_unique<node_generator::NodeGenerator>(config);  

        onLogMessage("[INFO] Initializing node generator with config: " + QString::fromStdString(config));
        if(!node_generator_->Initialize()) {
            onLogMessage("[ERROR] Failed to initialize node generator");
            return false;
        }
        onLogMessage("[INFO] Node generator initialized");

        onLogMessage("[INFO] Building required targets");
        if(!node_generator_->BuildRequiredTargets()) {
            onLogMessage("[ERROR] Failed to build required targets");
            return false;
        }
        onLogMessage("[INFO] Required targets built");

        // onLogMessage("[INFO] Launching nodes");
        // if(!node_generator_->LaunchAllNodes()) {
        //     onLogMessage("[ERROR] Failed to launch nodes");
        //     return false;
        // }
        // onLogMessage("[INFO] Nodes launched");
    } catch (const std::exception& e) {
        onLogMessage("[ERROR] Exception during node generator setup: " + QString::fromStdString(e.what()));
        return false;
    }
    
    return true;
}
