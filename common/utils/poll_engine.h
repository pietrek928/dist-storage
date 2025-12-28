#pragma once

#include <mutex>
#include <unordered_map>
#include <absl/functional/any_invocable.h>

#include "unique_fd.h"


class PollEngine {
    constexpr static int event_batch_size = 16;
    using Tcallback = absl::AnyInvocable<void()>;
    using callback_map_t = std::unordered_multimap<int, std::pair<int, Tcallback>>;

    // variables
    unique_fd epoll_fd_;
    unique_fd wakeup_fd_; // for thread signaling
    std::mutex map_mutex_;
    callback_map_t watchers_;

    void wake();  // KICK: Write to eventfd to wake up Poll() if it's sleeping
    void clear_wake();  // Just a wakeup call, drain the eventfd
    int get_watch_flags(int fd);

public:
    PollEngine();
    void push(int fd, Tcallback callback, bool read, bool write = false);
    void poll(int timeout_ms);
    void pop(int fd);
};