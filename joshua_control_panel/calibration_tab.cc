#include "calibration_tab.h"
#include "ui_calibration_tab.h"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float32.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <thread>
#include <atomic>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <iostream>

// A small helper that owns a ROS2 node and subscription and spins in a background thread.
class CalibrationTab::RosSubscriberRunner {
public:
    RosSubscriberRunner(QObject* ui_receiver, const QString& topic)
        : ui_receiver_(ui_receiver), topic_(topic), is_running_(false) {
    }

    ~RosSubscriberRunner() { stop(); }

    bool start() {
        if (is_running_.load()) return true;
        try {
            // Lazy init rclcpp if needed
            if (!rclcpp::ok()) {
                int argc = 0; char** argv = nullptr;
                rclcpp::init(argc, argv);
            }

            node_ = std::make_shared<rclcpp::Node>("calibration_tab_subscriber");
            subscription_ = node_->create_subscription<std_msgs::msg::Float32MultiArray>(
                topic_.toStdString(), 10,
                [this](const std_msgs::msg::Float32MultiArray::SharedPtr msg) {
                    // Emit Qt signal on the UI receiver via queued connection
                    QMetaObject::invokeMethod(
                        ui_receiver_, "readingUpdated",
                        Qt::QueuedConnection,
                        Q_ARG(float, msg->data[0]),
                        Q_ARG(float, msg->data[1]));
                });

            is_running_.store(true);
            // Spin in a dedicated thread using an executor
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
            // Best-effort cleanup
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
        // Do not shutdown rclcpp globally here; other nodes may be using it
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

CalibrationTab::CalibrationTab(QWidget *parent)
    : QWidget(parent), ui(new Ui::CalibrationTab) {
    ui->setupUi(this);

    // Connect UI update signal to local slot
    connect(this, &CalibrationTab::readingUpdated,
            this, &CalibrationTab::onReadingUpdated,
            Qt::QueuedConnection);

    // Explicitly wire buttons to the renamed slots
    if (ui->start_subscribe_Button) {
        connect(ui->start_subscribe_Button, &QPushButton::clicked,
                this, &CalibrationTab::on_start_subscribe_Button_clicked);
    }
    if (ui->stop_subscribe_Button) {
        connect(ui->stop_subscribe_Button, &QPushButton::clicked,
                this, &CalibrationTab::on_stop_subscribe_Button_clicked);
    }

    // Initial UI state
    if (ui->test_label) ui->test_label->setText("Idle");
    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(false);
}

CalibrationTab::~CalibrationTab() { 
    // Ensure subscriber stops before UI deletion
    subscriberRunner_.reset();
    delete ui; 
}

void CalibrationTab::on_start_subscribe_Button_clicked() {
    if (!subscriberRunner_) {
        // TODO: topic selection via UI; hardcode for now or use a sensible default
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

void CalibrationTab::on_stop_subscribe_Button_clicked() {
    if (subscriberRunner_) {
        subscriberRunner_->stop();
    }
    if (ui->test_label) ui->test_label->setText("Stopped");
    if (ui->start_subscribe_Button) ui->start_subscribe_Button->setEnabled(true);
    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(false);
}

void CalibrationTab::onReadingUpdated(float min_value, float max_value) {
    ui->test_label->setText(QString("Latest: %1 %2").arg(min_value).arg(max_value));
}