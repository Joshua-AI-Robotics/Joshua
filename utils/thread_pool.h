#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

class ThreadPool {
 private:
  std::vector<std::thread> workers_threads_;
  std::queue<std::function<void()>> tasks_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> stop_;

 public:
  ThreadPool(const size_t num_of_threads) : stop_(false) {
    stop_.store(false);
    for (size_t i = 0; i < num_of_threads; i++) {
      workers_threads_.emplace_back([this]() {
        while (true) {
          std::unique_lock<std::mutex> lock(mutex_);
          cv_.wait(lock, [this]() { return !tasks_.empty() || stop_.load(); });
          if (stop_.load() && tasks_.empty()) return;
          auto task = std::move(tasks_.front());
          tasks_.pop();
          cv_.notify_all();
          lock.unlock();
          task();
        }
      });
    }
  }
  ~ThreadPool() {
    stop_.store(true);
    cv_.notify_all();
    for (auto& worker : workers_threads_) {
      worker.join();
    }
  }
  void push_job(std::function<void()>& job) {
    std::unique_lock<std::mutex> lock(mutex_);
    tasks_.emplace(job);
    lock.unlock();
    cv_.notify_one();
  }
};
