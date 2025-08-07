#include "config_tab.h"
#include "ui_config_tab.h"

ConfigTab::ConfigTab(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::ConfigTab)
{
    ui->setupUi(this);
}

ConfigTab::~ConfigTab() {
    delete ui;
} 