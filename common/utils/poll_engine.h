#pragma once

#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <absl/functional/any_invocable.h>

#include "unique_fd.h"


class PollEngine {
    constexpr static int event_batch_size = 16;
    using Tcallback = absl::AnyInvocable<void()>;
    using callback_map_t = std::unordered_multimap<int, std::pair<int, Tcallback>>;

    unique_fd epoll_fd_;
    unique_fd wakeup_fd_;
    std::mutex map_mutex_;
    std::condition_variable in_flight_cv_;
    callback_map_t watchers_;
    std::unordered_map<int, int> in_flight_;

    void wake();
    void clear_wake();
    int get_watch_flags(int fd);

public:
    PollEngine();
    void push(int fd, Tcallback callback, bool read, bool write = false);
    void poll(int timeout_ms);
    /// Erase watchers for `fd` and wait until no in-flight callback for that fd can still run.
    void pop(int fd);
};
