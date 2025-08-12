#ifndef CALIBRATION_TAB_H
#define CALIBRATION_TAB_H

#include <QWidget>
#include <memory>

namespace Ui { class CalibrationTab; }

class CalibrationTab : public QWidget {
    Q_OBJECT

public:
    explicit CalibrationTab(QWidget *parent = nullptr);
    ~CalibrationTab();

signals:
    void readingUpdated(float value);

private slots:
    void on_start_subscribe_Button_clicked();
    void on_stop_subscribe_Button_clicked();
    void onReadingUpdated(float value);

private:
    class RosSubscriberRunner; // forward declaration, defined in .cc

    Ui::CalibrationTab *ui;
    std::unique_ptr<RosSubscriberRunner> subscriberRunner_;
};

#endif