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

class TaskContainer {
    public:

    int priority;
    double timestamp;
    TaskStatus status;

    TaskContainer(int priority, double timestamp, TaskStatus status)
        : priority(priority), timestamp(timestamp), status(status) {}

    virtual void run() = 0;

    virtual ~TaskContainer() = default;
};

template <typename Tf>
class TaskContainerLambda : public TaskContainer {
    public:

    Tf lambda;

    TaskContainerLambda(int priority, double timestamp, TaskStatus status, Tf &&lambda)
        : TaskContainer(priority, timestamp, status), lambda(lambda) {}

    void run() override {
        lambda(status);
    }
};

struct TaskContainerNewestFirst {
    bool operator()(const TaskContainer *a, const TaskContainer *b) const {
        if (a->timestamp == b->timestamp) {
            return a->priority < b->priority;
        }
        return a->timestamp < b->timestamp;
    }
};

struct TaskContainerOldestFirst {
    bool operator()(const TaskContainer *a, const TaskContainer *b) const {
        if (a->timestamp == b->timestamp) {
            return a->priority < b->priority;
        }
        return a->timestamp > b->timestamp;
    }
};

class TaskRunner {
    std::multiset<std::unique_ptr<TaskContainer>, TaskContainerNewestFirst> tasks;
    std::vector<std::thread> threads;
    std::mutex lock;
    std::condition_variable cv;
    bool running = false;

    std::unique_ptr <TaskContainer> pop_unsafe() {
        if (tasks.empty()) {
            return NULL;
        }
        return std::move(tasks.extract(tasks.begin()).value());
    }

    void task_loop() {
        std::unique_ptr<TaskContainer> task = NULL;
        while (running || !tasks.empty()) {
            {
                std::unique_lock<std::mutex> guard(lock);
                task = pop_unsafe();
                if (!task) {
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

    template<typename Tf>
    void push(int priority, double timestamp, TaskStatus status, Tf &&lambda) {
        std::unique_lock<std::mutex> guard(lock);
        tasks.emplace(new TaskContainerLambda<Tf>(priority, timestamp, status, lambda));
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