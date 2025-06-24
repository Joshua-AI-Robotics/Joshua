#include <iostream>
#include <unordered_map>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <chrono>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <atomic>

class Scheduler{
    public:
    struct TaskInfo{
        std::function<void()> task;
        int task_id;
        std::chrono::milliseconds task_interval;
        std::chrono::steady_clock::time_point next_run_time;
        bool operator>(const TaskInfo& other) const {
            return next_run_time > other.next_run_time;
        }
    };    
    
    Scheduler() : thread_pool_(4){
        stop_.store(false);
        task_id_count_ = 0;
        run_thread_ = std::thread([this](){ this->run();});
    }

    ~Scheduler(){
        std::unique_lock<std::mutex> lock(task_queue_mutex_);
        stop_.store(true);
        task_queue_mutex_.unlock();
        if(run_thread_.joinable()){
            run_thread_.join();
        }
    }

    int Schedule(std::function<void()>&& task, int freq){
        auto task_id = task_id_count_;
        auto now = std::chrono::steady_clock::now();
        TaskInfo this_task = {std::move(task), task_id, std::chrono::milliseconds(1000/freq), now + std::chrono::milliseconds(1000/freq)};
        std::unique_lock<std::mutex> lock(task_queue_mutex_);
        task_queue_.emplace(std::move(this_task));
        task_map_[task_id] = this_task;
        cv_.notify_all();
        task_id_count_++;
        return task_id;
    }

    void Deschedule(const int& task_id){
        std::unique_lock<std::mutex> lock(task_map_mutex_);
        task_to_remove_.insert(task_id);
    }
    private:
    void run(){
        while(!stop_.load()){
            std::unique_lock<std::mutex> lock(task_queue_mutex_);
            if (task_queue_.empty()) {
                cv_.wait(lock, [this]() { return stop_.load() || !task_queue_.empty(); });
            }
            if (stop_.load()) return;
            while(!task_to_remove_.empty() && (task_to_remove_.find(task_queue_.top().task_id) != task_to_remove_.end() )){
                auto id = task_queue_.top().task_id;
                task_queue_.pop();
                {
                    std::unique_lock<std::mutex> lock(task_map_mutex_);
                    task_map_.erase(id);
                }
                task_to_remove_.erase(id);
            }
            auto now = std::chrono::steady_clock::now();
            while(now >= task_queue_.top().next_run_time){
                auto current_task = task_queue_.top();
                task_queue_.pop();
                thread_pool_.push_job(current_task.task);
                current_task.next_run_time = now + current_task.task_interval;
                task_queue_.emplace(current_task);
            }
            cv_.wait_until(lock, task_queue_.top().next_run_time);           
        }
    }
    std::priority_queue<TaskInfo, std::vector<TaskInfo>, std::greater<TaskInfo>> task_queue_;
    std::mutex task_queue_mutex_;
    std::condition_variable cv_;
    std::unordered_map<int, TaskInfo> task_map_;
    std::mutex task_map_mutex_;
    std::unordered_set<int> task_to_remove_;
    int task_id_count_;
    ThreadPool thread_pool_;
    std::thread run_thread_;
    std::atomic<bool> stop_;
};
void Foo(){
    std::cout << "Foo executed\n";
}
void Bar(){
    std::cout << "Bar executed\n";
}
int main(){
    Scheduler sc;
    std::function<void()> job1 = Foo;
    std::function<void()> job2 = Bar;
    auto id1 = sc.Schedule(std::move(job1), 100);
    auto id2 = sc.Schedule(std::move(job2), 4);
    std::this_thread::sleep_for(std::chrono::seconds(1));    
    sc.Deschedule(id1);
    std::cout << "Deschedue Task ID: " << id1 << "\n";
    std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}