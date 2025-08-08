#ifndef MONITOR_TAB_H
#define MONITOR_TAB_H

#include <QtWidgets/QWidget>
#include <memory>
#include <atomic>
#include <thread>
#include <string>
#include <QtCore/QModelIndex>
#include "node_generator/node_generator.h"


namespace Ui { class MonitorTab; }

class MonitorTab : public QWidget {
    Q_OBJECT
public:
    explicit MonitorTab(QWidget *parent = nullptr);
    ~MonitorTab();

signals:
    void logMessage(const QString& message);
    void updateStatus(const QString& status, const QString& style);
    void enableLaunchButton();
    void setLaunchButtonEnabled(bool enabled);

private slots:
    void on_launchButton_clicked();
    void on_stopButton_clicked();
    void onLogMessage(const QString& message);
    void onUpdateStatus(const QString& status, const QString& style);
    void onSetLaunchButtonEnabled(bool enabled);
    void onConfigPresetSelectionChanged(const QModelIndex& current, const QModelIndex& previous);

private:
    bool setup_node_generator();
    void setup_node_generator_thread_func();

    Ui::MonitorTab* ui;
    std::string config_;
    std::unique_ptr<node_generator::NodeGenerator> node_generator_;
    std::atomic<bool> stop_node_generator_build_;
    std::thread node_generator_thread_;
};

#endif // MONITOR_TAB_H 