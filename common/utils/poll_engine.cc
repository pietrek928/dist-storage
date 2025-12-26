#include "poll_engine.h"
#include "sys_call_check.h"

#include <vector>
#include <unistd.h>
#include <sys/eventfd.h>
#include <sys/epoll.h>


void PollEngine::wake() {
    uint64_t u = 1;
    write(wakeup_fd_, &u, sizeof(uint64_t));
}

void PollEngine::clear_wake() {
    uint64_t u;
    read(wakeup_fd_, &u, sizeof(uint64_t));
}

int PollEngine::get_watch_flags(int fd) {
    int flags = 0;
    auto its = watchers_.equal_range(fd);
    for (auto it = its.first; it != its.second; ++it) {
        flags |= it->second.first;
    }
    return flags;
}

PollEngine::PollEngine() {
    sys_call("Creating epoll", epoll_fd_ = epoll_create1(0));
    sys_call("Creating wakeup file descriptor for epoll", wakeup_fd_ = eventfd(0, EFD_NONBLOCK));

    epoll_event ev{};
    ev.events = EPOLLIN;
    ev.data.fd = wakeup_fd_;
    sys_call(
        "Adding wakeup fd to epoll",
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &ev)
    );
}

void PollEngine::push(int fd, Tcallback callback, bool read, bool write) {
    {
        std::lock_guard<std::mutex> lock(map_mutex_);
        int request_flags = (read ? EPOLLIN : 0) | (write ? EPOLLOUT : 0);
        int watch_flags = request_flags | get_watch_flags(fd);
        watchers_.emplace(
            fd, std::make_pair(request_flags, std::move(callback))
        );

        struct epoll_event ev{};
        ev.events = watch_flags | EPOLLONESHOT;
        ev.data.fd = fd;

        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == -1) {
            sys_call(
                "Setting up fd watch in epoll",
                epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev)
            );
        }
    }

    wake();
}

void PollEngine::poll(int timeout_ms) {
    epoll_event events[event_batch_size];  // TODO: check err
    int nfds;
    sys_call("Polling epoll", nfds = epoll_wait(epoll_fd_, events, event_batch_size, timeout_ms));

    std::vector<Tcallback> callbacks;

    if (nfds) {
        std::lock_guard<std::mutex> lock(map_mutex_);

        if (nfds < event_batch_size) {
            clear_wake();  // events batch limit reached - check again later for more events
        }
        for (int i = 0; i < nfds; i++) {
            auto its = watchers_.equal_range(events[i].data.fd);
            auto flags = events[i].events;
            for (auto it = its.first; it != its.second;) {
                if (flags & it->second.first) {
                    callbacks.emplace_back(std::move(it->second.second));
                    watchers_.erase(it++);
                } else {
                    ++it;
                }
            }
        }
    }

    for (auto &callback : callbacks) {
        callback();
    }
}

void PollEngine::pop(int fd) {
    std::lock_guard<std::mutex> lock(map_mutex_);
    auto its = watchers_.equal_range(fd);
    watchers_.erase(its.first, its.second);
}
