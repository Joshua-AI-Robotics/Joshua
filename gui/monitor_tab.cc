#include "monitor_tab.h"
#include "ui_monitor_tab.h"

#include <QtWidgets/QTableWidget>
#include <QtWidgets/QTableWidgetItem>
#include <QtWidgets/QTextEdit>
#include <QtWidgets/QLabel>
#include <QtCore/QDir>
#include <QtCore/QStringListModel>
#include <QtCore/QItemSelectionModel>
#include <QtCore/QTimer>

namespace {
    constexpr auto kConfigPresetPrefix = "config/config_preset/";
    constexpr auto kErrorStyle = "QLabel { background-color: #ff0000; padding: 5px; border-radius: 5px; }";
    constexpr auto kBuildStyle = "QLabel { background-color: #ffa500; padding: 5px; border-radius: 5px; }";
    constexpr auto kRunningStyle = "QLabel { background-color: #4caf50; padding: 5px; border-radius: 5px; }";
    constexpr auto kStoppingStyle = "QLabel { background-color: #ffa500; padding: 5px; border-radius: 5px; }";
    constexpr auto kIdleStyle = "QLabel { background-color: #cccccc; color:rgb(0, 0, 0); padding: 5px; border-radius: 5px; }";
    constexpr auto kDisabledStyle = "QPushButton { background-color:rgb(225, 220, 220);}";
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
    ui->statusLabel->setStyleSheet(kIdleStyle);

    // Initialize config_preset list view.
    QDir config_preset_dir("config/config_preset");
    QStringList config_preset_files = config_preset_dir.entryList(QDir::Files);
    ui->config_preset_listView->setModel(new QStringListModel(config_preset_files));

    // Initialize current config_ from selection if available
    if (ui->config_preset_listView->model()->rowCount() > 0) {
        QModelIndex currentIndex = ui->config_preset_listView->currentIndex();
        if (!currentIndex.isValid()) {
            currentIndex = ui->config_preset_listView->model()->index(0, 0);
            ui->config_preset_listView->setCurrentIndex(currentIndex);
        }
        config_ = std::string(kConfigPresetPrefix) + currentIndex.data().toString().toStdString();
    }

    // Connect selection change to update config_
    connect(ui->config_preset_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &MonitorTab::onConfigPresetSelectionChanged);

    // Connect signals for thread-safe UI updates
    connect(this, &MonitorTab::logMessage, this, &MonitorTab::onLogMessage, Qt::QueuedConnection);
    connect(this, &MonitorTab::updateStatus, this, &MonitorTab::onUpdateStatus, Qt::QueuedConnection);
    connect(this, &MonitorTab::setLaunchButtonEnabled, this, &MonitorTab::onSetLaunchButtonEnabled, Qt::QueuedConnection);

    // Placeholder initial log
    ui->logTextEdit->append("[INFO] Monitor ready. Use Launch to start ROS2 nodes.");
}

MonitorTab::~MonitorTab() { 
    stop_node_generator_build_.store(true);
    if (node_generator_thread_.joinable()) {
        node_generator_thread_.join();
    }
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
    if (enabled) {
        ui->launchButton->setStyleSheet("");
    } else {
        ui->launchButton->setStyleSheet(kDisabledStyle);
    }
}

void MonitorTab::onConfigPresetSelectionChanged(const QModelIndex& current, const QModelIndex& /*previous*/) {
    if (current.isValid()) {
        config_ = std::string(kConfigPresetPrefix) + current.data().toString().toStdString();
        emit logMessage("[INFO] Selected config preset: " + current.data().toString());
    }
}

void MonitorTab::on_launchButton_clicked() {
    emit updateStatus("LAUNCHING", kRunningStyle);
    emit logMessage("[INFO] Launch requested.");
    emit setLaunchButtonEnabled(false);

    // Ensure previous thread is not running
    stop_node_generator_build_.store(false);
    if (node_generator_thread_.joinable()) {
        node_generator_thread_.join();
    }

    node_generator_thread_ = std::thread([this]() { setup_node_generator_thread_func(); });
}

void MonitorTab::on_stopButton_clicked() {
    emit updateStatus("STOPPING", kStoppingStyle);
    emit logMessage("[INFO] Stop requested.");

    // Request cancellation; defer heavy work so UI can repaint first
    stop_node_generator_build_.store(true);

    if (node_generator_thread_.joinable()) {
        node_generator_thread_.join();
    }

    // Shutdown the node generator
    if (node_generator_) {
        node_generator_->Shutdown();
    }
        
    emit updateStatus("IDLE", kIdleStyle);
    emit setLaunchButtonEnabled(true);
}

bool MonitorTab::setup_node_generator() {
    try {
        node_generator_ = std::make_unique<node_generator::NodeGenerator>(config_);  

        emit logMessage("[INFO] Initializing node generator with config: " + QString::fromStdString(config_));
        if(!node_generator_->Initialize()) {
            emit logMessage("[ERROR] Failed to initialize node generator");
            return false;
        }
        emit logMessage("[INFO] Node generator initialized");

        emit logMessage("[INFO] Building required targets");
        emit updateStatus("BUILDING", kBuildStyle);
        if(!node_generator_->BuildRequiredTargets(stop_node_generator_build_)) {
            emit logMessage("[ERROR] Build stopped or failed");
            return false;
        }
        emit logMessage("[INFO] Required targets built");

        emit logMessage("[INFO] Launching nodes");
        if(!node_generator_->LaunchAllNodes()) {
            emit logMessage("[ERROR] Failed to launch nodes");
            emit updateStatus("ERROR", kErrorStyle);
            return false;
        }
        emit logMessage("[INFO] Nodes launched");
        emit updateStatus("RUNNING", kRunningStyle);
    } catch (const std::exception& e) {
        onLogMessage("[ERROR] Exception during node generator setup: " + QString::fromStdString(e.what()));
        return false;
    }
    
    return true;
}

void MonitorTab::setup_node_generator_thread_func() {
    if (!setup_node_generator()) {
        emit updateStatus("ERROR", kErrorStyle);
        emit setLaunchButtonEnabled(true);
        return;
    }

    emit updateStatus("RUNNING", kRunningStyle);
}
