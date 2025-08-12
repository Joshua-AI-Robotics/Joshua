#include "operational_limit_tab.h"
#include "ui_operational_limit_tab.h"

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float32_multi_array.hpp>
#include <thread>
#include <atomic>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <unordered_set>

namespace {
    constexpr auto kCalibrationSubscriberNodeName = "operational_limit_subscriber";
}

class OperationalLimitTab::RosSubscriberRunner {
public:
    RosSubscriberRunner(QObject* ui_receiver, const std::vector<QString>& topics)
        : ui_receiver_(ui_receiver), topics_(topics), is_running_(false) {}

    ~RosSubscriberRunner() { stop(); }

    bool start() {
        if (is_running_.load()) return true;
        try {
            if (!rclcpp::ok()) {
                int argc = 0; char** argv = nullptr;
                rclcpp::init(argc, argv);
            }

            node_ = std::make_shared<rclcpp::Node>(kCalibrationSubscriberNodeName);
            for (const auto& topic : topics_) {
                subscriptions_.push_back(node_->create_subscription<std_msgs::msg::Float32MultiArray>(
                    topic.toStdString(), 10,
                    [this, topic](const std_msgs::msg::Float32MultiArray::SharedPtr msg) {
                        // Back-compat signal
                        QMetaObject::invokeMethod(
                            ui_receiver_, "readingUpdated",
                            Qt::QueuedConnection,
                            Q_ARG(float, msg->data[0]),
                            Q_ARG(float, msg->data[1]));
                        // Topic-aware signal
                        QMetaObject::invokeMethod(
                            ui_receiver_, "readingUpdatedForTopic",
                            Qt::QueuedConnection,
                            Q_ARG(QString, topic),
                            Q_ARG(float, msg->data[0]),
                            Q_ARG(float, msg->data[1]));
                    }
                ));
            }

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
            for (auto& subscription : subscriptions_) {
                subscription.reset();
            }
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
        for (auto& subscription : subscriptions_) {
            subscription.reset();
        }
        node_.reset();
    }

    bool isRunning() const { return is_running_.load(); }
    const std::vector<QString>& topics() const { return topics_; }

private:
    QObject* ui_receiver_;
    std::vector<QString> topics_;
    std::atomic<bool> is_running_;

    std::shared_ptr<rclcpp::Node> node_;
    std::vector<rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr> subscriptions_;
    std::unique_ptr<rclcpp::Executor> executor_;
    std::thread spin_thread_;
};

namespace {
// Returns all topics published by any node whose name contains the given substring, filtered to std_msgs/msg/Float32MultiArray
static std::vector<QString> findFloat32MultiArrayTopicsByNode(const std::string& target_node_substring) {
    try {
        if (!rclcpp::ok()) {
            int argc = 0; char** argv = nullptr;
            rclcpp::init(argc, argv);
        }
        auto graph_node = std::make_shared<rclcpp::Node>("operational_limit_topic_discovery");

        const bool no_demangle = false;
        std::vector<QString> topics;
        std::unordered_set<std::string> seen;

        // Enumerate all topics and their types
        auto topics_and_types = graph_node->get_node_graph_interface()->get_topic_names_and_types(no_demangle);
        for (const auto& name_and_types : topics_and_types) {
            const auto& topic_name = name_and_types.first;
            const auto& types = name_and_types.second;
            bool is_float32_multi_array = false;
            for (const auto& type : types) {
                if (type == std::string("std_msgs/msg/Float32MultiArray")) {
                    is_float32_multi_array = true;
                    break;
                }
            }
            if (!is_float32_multi_array) continue;

            // For each topic, check publishers and match node names by substring
            auto publishers_info = graph_node->get_node_graph_interface()->get_publishers_info_by_topic(topic_name, no_demangle);
            bool match = false;
            for (const auto& info : publishers_info) {
                const std::string& node_name = info.node_name();
                const std::string& node_ns = info.node_namespace();
                std::string fq_name = node_ns == "/" ? node_name : (node_ns + "/" + node_name);
                if (node_name.find(target_node_substring) != std::string::npos ||
                    fq_name.find(target_node_substring) != std::string::npos) {
                    match = true;
                    break;
                }
            }
            if (match && seen.insert(topic_name).second) {
                topics.emplace_back(QString::fromStdString(topic_name));
            }
        }
        return topics;
    } catch (const std::exception& e) {
        std::cerr << "Topic discovery error: " << e.what() << std::endl;
        return {};
    }
}
}

OperationalLimitTab::OperationalLimitTab(QWidget* parent)
    : QWidget(parent), ui(new Ui::OperationalLimitTab) {
    ui->setupUi(this);

    connect(this, &OperationalLimitTab::readingUpdated,
            this, &OperationalLimitTab::onReadingUpdated,
            Qt::QueuedConnection);
    connect(this, &OperationalLimitTab::readingUpdatedForTopic,
            this, &OperationalLimitTab::onReadingUpdatedForTopic,
            Qt::QueuedConnection);

    if (ui->start_subscribe_Button) {
        connect(ui->start_subscribe_Button, &QPushButton::clicked,
                this, &OperationalLimitTab::on_start_subscribe_Button_clicked);
    }
    if (ui->stop_subscribe_Button) {
        connect(ui->stop_subscribe_Button, &QPushButton::clicked,
                this, &OperationalLimitTab::on_stop_subscribe_Button_clicked);
    }

    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(false);
}

OperationalLimitTab::~OperationalLimitTab() {
    subscriberRunner_.reset();
    delete ui;
}

void OperationalLimitTab::on_start_subscribe_Button_clicked() {
    if (!subscriberRunner_) {
        // Automatically discover topics published by the ROS 2 node "operational_limit_calibration"
        const auto topics = findFloat32MultiArrayTopicsByNode("operational_limit_calibration");
        
        // Dyamically generate labes based on topics. Each topic has min and max values.
        for (const auto& topic : topics) {
            if (!ui->limit_values_gridLayout) continue;
            const int next_row = ui->limit_values_gridLayout->rowCount();

            auto* topicLabel = new QLabel(topic, this);
            auto* minLabel = new QLabel("-", this);
            auto* maxLabel = new QLabel("-", this);

            ui->limit_values_gridLayout->addWidget(topicLabel, next_row, 0);
            ui->limit_values_gridLayout->addWidget(minLabel, next_row, 1);
            ui->limit_values_gridLayout->addWidget(maxLabel, next_row, 2);

            topicToMinMaxLabels_.insert(topic, qMakePair(minLabel, maxLabel));
        }
        subscriberRunner_ = std::make_unique<RosSubscriberRunner>(this, topics);
    }
    const bool ok = subscriberRunner_->start();

    if (ui->start_subscribe_Button) ui->start_subscribe_Button->setEnabled(!ok);
    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(ok);
}

void OperationalLimitTab::on_stop_subscribe_Button_clicked() {
    if (subscriberRunner_) {
        subscriberRunner_->stop();
    }
    if (ui->start_subscribe_Button) ui->start_subscribe_Button->setEnabled(true);
    if (ui->stop_subscribe_Button) ui->stop_subscribe_Button->setEnabled(false);
}

void OperationalLimitTab::onReadingUpdated(float /*min_value*/, float /*max_value*/) {
    // legacy no-op
}

void OperationalLimitTab::onReadingUpdatedForTopic(QString topic, float min_value, float max_value) {
    auto it = topicToMinMaxLabels_.find(topic);
    if (it == topicToMinMaxLabels_.end()) return;
    auto* minLabel = it.value().first;
    auto* maxLabel = it.value().second;
    if (minLabel) minLabel->setText(QString::number(min_value));
    if (maxLabel) maxLabel->setText(QString::number(max_value));
} 