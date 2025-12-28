#pragma once

#include <ctime>


void sleep_sec(float sec);
float timespec_diff_sec(const timespec &start, const timespec &end);
timespec timespec_timestamp();
