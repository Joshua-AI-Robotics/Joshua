#ifndef CALIBRATION_TAB_H
#define CALIBRATION_TAB_H

#include <QWidget>

namespace Ui { class CalibrationTab; }

class CalibrationTab : public QWidget {
    Q_OBJECT

public:
    explicit CalibrationTab(QWidget *parent = nullptr);
    ~CalibrationTab();

private:
    Ui::CalibrationTab *ui;
};

#endif