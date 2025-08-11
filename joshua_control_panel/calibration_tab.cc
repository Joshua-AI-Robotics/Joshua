#include "calibration_tab.h"
#include "ui_calibration_tab.h"

CalibrationTab::CalibrationTab(QWidget *parent)
    : QWidget(parent), ui(new Ui::CalibrationTab) {
    ui->setupUi(this);
}

CalibrationTab::~CalibrationTab() { delete ui; }