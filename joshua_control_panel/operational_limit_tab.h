#ifndef OPERATIONAL_LIMIT_TAB_H
#define OPERATIONAL_LIMIT_TAB_H

#include <QWidget>
#include <memory>

namespace Ui { class OperationalLimitTab; }

class OperationalLimitTab : public QWidget {
    Q_OBJECT
public:
    explicit OperationalLimitTab(QWidget* parent = nullptr);
    ~OperationalLimitTab();

signals:
    void readingUpdated(float min_value, float max_value);

private slots:
    void on_start_subscribe_Button_clicked();
    void on_stop_subscribe_Button_clicked();
    void onReadingUpdated(float min_value, float max_value);

private:
    class RosSubscriberRunner; // defined in .cc

    Ui::OperationalLimitTab* ui;
    std::unique_ptr<RosSubscriberRunner> subscriberRunner_;
};

#endif // OPERATIONAL_LIMIT_TAB_H 