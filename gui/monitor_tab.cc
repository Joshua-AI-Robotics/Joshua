#include "monitor_tab.h"
#include "ui_monitor_tab.h"

MonitorTab::MonitorTab(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::MonitorTab)
{
    ui->setupUi(this);
}

MonitorTab::~MonitorTab() {
    delete ui;
} 