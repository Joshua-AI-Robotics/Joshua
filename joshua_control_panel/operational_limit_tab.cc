#include "operational_limit_tab.h"
#include "ui_operational_limit_tab.h"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <thread>
#include <atomic>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>

class OperationalLimitTab::RosSubscriberRunner {
public:
    RosSubscriberRunner(QObject* ui_receiver, const QString& topic)
        : ui_receiver_(ui_receiver), topic_(topic), is_running_(false) {}

    ~RosSubscriberRunner() { stop(); }

    bool start() {
        if (is_running_.load()) return true;
        try {
            if (!rclcpp::ok()) {
                int argc = 0; char** argv = nullptr;
                rclcpp::init(argc, argv);
            }

            node_ = std::make_shared<rclcpp::Node>("operational_limit_subscriber");
            subscription_ = node_->create_subscription<std_msgs::msg::Float32MultiArray>(
                topic_.toStdString(), 10,
                [this](const std_msgs::msg::Float32MultiArray::SharedPtr msg) {
                    QMetaObject::invokeMethod(
                        ui_receiver_, "readingUpdated",
                        Qt::QueuedConnection,
                        Q_ARG(float, msg->data[0]),
                        Q_ARG(float, msg->data[1]));
                });

            is_running_.store(true);
            executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
            executor_->add_node(node_);
            spin_thread_ = std::thread([this]() {
                while (is_running_.load() && rclcpp::ok()) {
                    executor_->spin_some();
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                }
            });
            return true;
        } catch (const std::exception& e) {
            std::cerr << "ROS2 subscriber start error: " << e.what() << std::endl;
            subscription_.reset();
            node_.reset();
            executor_.reset();
            is_running_.store(false);
            return false;
        }
    }

    void stop() {
        if (!is_running_.exchange(false)) return;
        if (executor_) {
            try { executor_->cancel(); } catch (...) {}
        }
        if (spin_thread_.joinable()) spin_thread_.join();
        if (executor_) {
            if (node_) executor_->remove_node(node_);
            executor_.reset();
        }
        subscription_.reset();
        node_.reset();
    }

    bool isRunning() const { return is_running_.load(); }
    QString topic() const { return topic_; }

private:
    QObject* ui_receiver_;
    QString topic_;
    std::atomic<bool> is_running_;

    std::shared_ptr<rclcpp::Node> node_;
    rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr subscription_;
    std::unique_ptr<rclcpp::Executor> executor_;
    std::thread spin_thread_;
};

OperationalLimitTab::OperationalLimitTab(QWidget* parent)
    : QWidget(parent), ui(new Ui::OperationalLimitTab) {
    ui->setupUi(this);

    connect(this, &OperationalLimitTab::readingUpdated,
            this, &OperationalLimitTab::onReadingUpdated,
            Qt::QueuedConnection);

    if (ui->start_subscribe_Button) {
        connect(ui->start_subscribe_Button, &QPushButton::clicked,
                this, &OperationalLimitTab::on_start_subscribe_Button_clicked);
    }
    if (ui->stop_subscribe_Button) {
        connect(ui->stop_subscribe_Button, &QPushButton::clicked,
                this, &OperationalLimitTab::on_stop_subscribe_Button_clicked);
    }

    if (ui->test_label) ui->test_label->setText("Idle");
    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(false);
}

OperationalLimitTab::~OperationalLimitTab() {
    subscriberRunner_.reset();
    delete ui;
}

void OperationalLimitTab::on_start_subscribe_Button_clicked() {
    if (!subscriberRunner_) {
        const QString topic = "sts3215_encoder_1_operational_limit";
        subscriberRunner_ = std::make_unique<RosSubscriberRunner>(this, topic);
    }
    const bool ok = subscriberRunner_->start();

    if (ui->test_label) {
        if (ok && subscriberRunner_->isRunning()) {
            ui->test_label->setText(QString("Subscribing: %1").arg(subscriberRunner_->topic()));
        } else {
            ui->test_label->setText("Failed to start subscriber (check ROS 2 env and publisher)");
        }
    }
    if (ui->start_subscribe_Button) ui->start_subscribe_Button->setEnabled(!ok);
    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(ok);
}

void OperationalLimitTab::on_stop_subscribe_Button_clicked() {
    if (subscriberRunner_) {
        subscriberRunner_->stop();
    }
    if (ui->test_label) ui->test_label->setText("Stopped");
    if (ui->start_subscribe_Button) ui->start_subscribe_Button->setEnabled(true);
    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(false);
}

void OperationalLimitTab::onReadingUpdated(float min_value, float max_value) {
    if (ui->test_label) ui->test_label->setText(QString("Latest: %1, %2").arg(min_value).arg(max_value));
} 