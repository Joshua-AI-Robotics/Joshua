#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
class Subscriber;  // Forward declaration

class Message {
  std::string message_;

 public:
  Message(const std::string& message) : message_(message) {}
  std::string get_message() {
    return message_;
  }
};

class Broker {
  std::mutex mutex_;
  std::condition_variable cv_;
  // <topic, <message>>
  std::unordered_map<std::string, std::queue<std::shared_ptr<Message>>> messages_;
  std::unordered_map<std::string, std::vector<std::shared_ptr<Subscriber>>> subscribers_;

 public:
  void subscribe(const std::string& topic, std::shared_ptr<Subscriber> subscriber);
  void publish(const std::string& topic, std::shared_ptr<Message> message);
  void process_messages();
};

class Publisher {
  std::shared_ptr<Broker> broker_;

 public:
  Publisher(std::shared_ptr<Broker> broker) : broker_(broker) {}
  void publish(const std::string& topic, std::shared_ptr<Message> message) {
    broker_->publish(topic, message);
  }
};

class Subscriber : public std::enable_shared_from_this<Subscriber> {
  std::shared_ptr<Broker> broker_;

 public:
  Subscriber(std::shared_ptr<Broker> broker) : broker_(broker) {}
  void subscribe(const std::string& topic) {
    broker_->subscribe(topic, shared_from_this());
  }
  void receive(std::shared_ptr<Message> message, const std::string& topic) {
    std::cout << "[" << topic << "]: " << message->get_message() << std::endl;
  }
};

void Broker::subscribe(const std::string& topic, std::shared_ptr<Subscriber> subscriber) {
  std::lock_guard<std::mutex> lock(mutex_);
  subscribers_[topic].push_back(subscriber);
}

void Broker::publish(const std::string& topic, std::shared_ptr<Message> message) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_[topic].push(message);
  }
  cv_.notify_all();
}

void Broker::process_messages() {
  const int timeout_in_seconds = 3;
  std::unique_lock<std::mutex> lock(mutex_);
  while (true) {
    bool success = cv_.wait_for(lock, std::chrono::seconds(timeout_in_seconds), [&] {
      for (auto& m : messages_) {
        if (!m.second.empty()) {
          return true;
        }
      }
      return false;
    });
    if (!success) {
      std::cout << "No messages to process for more than " << timeout_in_seconds
                << " seconds...\nExit" << std::endl;
      return;
    }
    for (auto& message : messages_) {
      std::string topic = message.first;
      std::queue<std::shared_ptr<Message>>& q = message.second;
      while (!q.empty()) {
        std::shared_ptr<Message> m = q.front();
        q.pop();
        std::vector<std::shared_ptr<Subscriber>>& subs = subscribers_[topic];
        for (auto& sub : subs) {
          sub->receive(m, topic);
        }
      }
    }
  }
}

void PublishThread(std::unique_ptr<Publisher> publisher, const std::string& topic) {
  const auto message_frequency_in_ms = std::chrono::milliseconds(20);
  for (int i = 0; i < 100; i++) {
    std::string message = "Hello from topic1 " + std::to_string(i);
    publisher->publish(topic, std::make_shared<Message>(message));
    std::this_thread::sleep_for(message_frequency_in_ms);
  }
}

// int main() {
//     auto broker = std::make_shared<Broker>();
//     auto publisher1 = std::make_unique<Publisher>(broker);
//     auto publisher2 = std::make_unique<Publisher>(broker);
//     auto subscriber = std::make_shared<Subscriber>(broker);
//     subscriber->subscribe("topic_1");
//     subscriber->subscribe("topic_2");
//     std::thread t1(PublishThread, std::move(publisher1), "topic_1");
//     std::thread t2(PublishThread, std::move(publisher2), "topic_2");
//     t1.detach();
//     t2.detach();
//     std::thread process_thread(&Broker::process_messages, broker);
//     process_thread.detach();
//     while(true) {
//         char ch;
//         ch = getch();  // Read a character without waiting for Enter
//         if (ch == 'q') {
//             break;
//         }
//         else if(ch == 'p') {
//             broker->publish("topic_1", std::make_shared<Message>("Manually published message"));
//         }
//     }
//     return 0;
// }
