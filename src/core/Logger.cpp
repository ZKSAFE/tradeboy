#include "Logger.h"

#include <cstdio>

namespace tradeboy::core {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger() : f_(nullptr) {
}

Logger::~Logger() {
    shutdown();
}

void Logger::init(const char* filename) {
    if (f_) {
        fclose((FILE*)f_);
        f_ = nullptr;
    }
    if (filename) {
        f_ = fopen(filename, "w");
        if (f_) {
            fputs("--- TradeBoy Log Start V7 ---\n", (FILE*)f_);
            fflush((FILE*)f_);
        }
    }
}

void Logger::log(const char* s) {
    if (!s) return;
    if (f_) {
        fputs(s, (FILE*)f_);
        fflush((FILE*)f_);  // Flush immediately for crash safety
    }
}

void Logger::flush() {
    if (f_) {
        fflush((FILE*)f_);
    }
}

void Logger::shutdown() {
    if (f_) {
        fclose((FILE*)f_);
        f_ = nullptr;
    }
}

void logger_init(const char* filename) {
    Logger::instance().init(filename);
}

void logger_log(const char* s) {
    Logger::instance().log(s);
}

void logger_flush() {
    Logger::instance().flush();
}

void logger_shutdown() {
    Logger::instance().shutdown();
}

} // namespace tradeboy::core
