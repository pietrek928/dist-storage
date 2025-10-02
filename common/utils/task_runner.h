#pragma once

#include <mutex>
#include <condition_variable>
#include <thread>
#include <set>
#include <vector>


enum TaskStatus {
    PENDING,
    RUNNING,
    COMPLETED,
    FAILED,
    CANCELLED,
    TIMEOUT,
    OVERLOAD
};

typedef struct {
    int priority;
    double timestamp;
    TaskStatus status;
} TaskInfo;

class TaskContainerBase {
    TaskInfo info;
    public:

    auto get_priority() const {
        return info.priority;
    }

    auto get_timestamp() const {
        return info.priority;
    }

    auto get_status() const {
        return info.status;
    }

    TaskContainerBase(TaskInfo info): info(info) {}

    virtual void run() = 0;

    virtual ~TaskContainerBase() = default;
};

template <typename Tf>
class TaskContainerLambda: TaskContainerBase {
    public:

    std::unique_ptr<Tf> lambda;

    TaskContainerLambda(TaskInfo info, Tf &&lambda)
    : TaskContainerBase(info), lambda(std::make_unique<Tf>(std::move(lambda))) {}

    void run() {
        lambda(get_status());
    }
};

struct TaskContainerNewestFirst {
    bool operator()(const TaskContainerBase *a, const TaskContainerBase *b) const {
        if (a->get_timestamp() == b->get_timestamp()) {
            return a->get_priority() < b->get_priority();
        }
        return a->get_timestamp() < b->get_timestamp();
    }
};

struct TaskContainerOldestFirst {
    bool operator()(const TaskContainerBase *a, const TaskContainerBase *b) const {
        if (a->get_timestamp() == b->get_timestamp()) {
            return a->get_priority() < b->get_priority();
        }
        return a->get_timestamp() > b->get_timestamp();
    }
};

template<class TaskContainer>
class TaskRunner {
    std::multiset<TaskContainer> tasks;
    std::vector<std::thread> threads;
    std::mutex lock;
    std::condition_variable cv;
    bool running = false;

    TaskContainer &&pop_unsafe() {
        return std::move(tasks.extract(tasks.begin()).value());
    }

    void task_loop() {
        TaskContainer task;
        while (running || !tasks.empty()) {
            {
                std::unique_lock<std::mutex> guard(lock);
                if (!tasks.empty()) {
                    task = pop_unsafe();
                } else {
                    cv.wait(guard);
                    task = pop_unsafe();
                    if (!task) {
                        continue;
                    }
                }
            }

            task->run();
        }
    }

    void start_threads(int num_threads) {
        running = true;
        for (int i = 0; i < num_threads; ++i) {
            threads.emplace_back(&TaskRunner::task_loop, this);
        }
    }

    public:

    TaskRunner(int num_threads) {
        start_threads(num_threads);
    }

    template<typename ...Ttask_args>
    void push(int priority, double timestamp, TaskStatus status, Ttask_args... task_args) {
        std::unique_lock<std::mutex> guard(lock);
        tasks.emplace(new TaskContainer(priority, timestamp, status, task_args...));
        cv.notify_one();
    }

    void pop_tasks(int max_per_priority, double older_than) {
        std::vector<std::unique_ptr<TaskContainer>> popped_tasks;
        std::unique_lock<std::mutex> guard(lock);
        int cnt = 0;
        int last_priority = -1;

        auto it = tasks.begin();
        while (it != tasks.end()) {
            TaskContainer *task = it->get();
            if (task->priority == -1) {
                it ++;
                continue;
            }

            if (task->priority == last_priority) {
                cnt ++;
            } else {
                last_priority = task->priority;
                cnt = 1;
            }
            if (cnt > max_per_priority) {
                task->status = OVERLOAD;
                task->priority = -1;
                popped_tasks.emplace_back(std::move(*it));
                it = tasks.erase(it);
            } else if (task->timestamp < older_than) {
                task->status = TIMEOUT;
                task->priority = -1;
                popped_tasks.emplace_back(std::move(*it));
                it = tasks.erase(it);
            } else {
                it++;
            }
        }

        for (auto &task : popped_tasks) {
            tasks.emplace(std::move(task));
        }
    }

    void cancel_all() {
        std::unique_lock<std::mutex> guard(lock);
        for (auto &task : tasks) {
            task->status = CANCELLED;
            task->priority = -1;
        }
    }

    void join() {
        running = false;
        cv.notify_all();
        for (auto &thread : threads) {
            thread.join();
        }
        threads.clear();
    }

    ~TaskRunner() {
        cancel_all();
        join();
    }
};