#include "calibration_tab.h"

#include <QtWidgets/QTabWidget>

#include "operational_limit_tab.h"
#include "ui_calibration_tab.h"

CalibrationTab::CalibrationTab(QWidget* parent) : QWidget(parent), ui(new Ui::CalibrationTab) {
  ui->setupUi(this);

  // Create and add subtabs
  auto* operationalLimitTab = new OperationalLimitTab(this);
  ui->modeTabWidget->addTab(operationalLimitTab, "Operational Limit");
}

CalibrationTab::~CalibrationTab() {
  delete ui;
}
