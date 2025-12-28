#include "err.h"

#include <iostream> // TODO: better logging
#include <stdexcept>


void show_sys_error(const std::string& call_descr) {
    std::error_code ec(errno, std::generic_category());
    std::cerr << "Warning: Failed to " << call_descr <<": " << ec.message() << std::endl;
}

void throw_sys_error(const std::string& call_descr) {
    std::error_code ec(errno, std::generic_category());
    throw std::runtime_error("Failed to " + call_descr + ": " + ec.message());
}
