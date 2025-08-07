#ifndef MONITOR_TAB_H
#define MONITOR_TAB_H

#include <QtWidgets/QWidget>

namespace Ui { class MonitorTab; }

class MonitorTab : public QWidget {
    Q_OBJECT
public:
    explicit MonitorTab(QWidget* parent = nullptr);
    ~MonitorTab();

private slots:
    void on_launchButton_clicked();
    void on_stopButton_clicked();
    void on_restartButton_clicked();

private:
    Ui::MonitorTab* ui;
};

#endif // MONITOR_TAB_H 