#ifndef MONITOR_TAB_H
#define MONITOR_TAB_H

#include <QtWidgets/QWidget>
#include <memory>
#include <atomic>
#include <thread>
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

private:
    bool setup_node_generator();
    void setup_node_generator_thread_func();

    Ui::MonitorTab* ui;
    std::unique_ptr<node_generator::NodeGenerator> node_generator_;
    std::atomic<bool> stop_node_generator_build_;
};

#endif // MONITOR_TAB_H 