#pragma once

#include <cmath>
#include <mutex>


template<class Ttimestamp, class Tnum>
class ExpAvg {
    mutable std::mutex lock;
    Tnum value = 0;
    Ttimestamp timestamp;
    Tnum a;

    public:
    ExpAvg(Ttimestamp timestamp, Tnum a) : a(a), timestamp(timestamp) {}\

    Tnum get() const {
        return value;
    }

    operator Tnum() const {
        return get();
    }

    void update(Ttimestamp now, Tnum val) {
        std::lock_guard<std::mutex> guard(lock);

        auto k = std::exp(-(now - timestamp) * a);
        value = value * k + val * (1 - k);
        timestamp = now;
    }
};

template<class Ttimestamp, class Tnum>
class ExpAccum {
    mutable std::mutex lock;
    Tnum value = 0;
    Ttimestamp timestamp;
    Tnum a;

    public:
    ExpAccum(Ttimestamp timestamp, Tnum a) : a(a), timestamp(timestamp) {}\

    Tnum get(Ttimestamp now) const {
        return value * std::exp(-(now - timestamp) * a);
    }

    void update(Ttimestamp now, Tnum val) {
        std::lock_guard<std::mutex> guard(lock);

        auto k = std::exp(-(now - timestamp) * a);
        value = value * k + val;
        timestamp = now;
    }
};