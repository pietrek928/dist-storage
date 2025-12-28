#include "time.h"


void sleep_sec(float sec) {
    if (sec <= 0) {
        return;
    }
    timespec ts;
    ts.tv_sec = (time_t)sec;
    ts.tv_nsec = (long)((sec - ts.tv_sec) * 1e9);
    // TODO: sleep loop ?
    nanosleep(&ts, NULL);
}

float timespec_diff_sec(const timespec &start, const timespec &end) {
    return (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) * 1e-9;
}

timespec timespec_timestamp() {
    timespec ts;
    // TODO: error handling?
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts;
}
