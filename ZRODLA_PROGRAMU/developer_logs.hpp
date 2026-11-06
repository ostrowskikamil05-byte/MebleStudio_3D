#pragma once
#include "core.hpp"
#include <string>

// Developer logging system for debugging and error tracking

struct LogEntry {
    int timestamp = 0;     // Unix timestamp
    int level;             // DEBUG, INFO, WARNING, ERROR
    std::string message;   // Log message
    std::string category;  // Category (CAD, UI, Material, etc.)

    bool isValid() const {
        return level >= 0 && level <= 3;
    }
};

void log_info(const std::string& message);
void log_warning(const std::string& message);
void log_error(const std::string& message);

std::vector<LogEntry> get_recent_logs(int count = 10);

void initialize_logging_system();
