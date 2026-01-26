#include "Logger.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>

namespace tradeboy::core {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger() : fd_(-1) {
}

Logger::~Logger() {
    shutdown();
}

void Logger::init(const char* filename) {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    if (!filename) return;
    fd_ = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd_ >= 0) {
        static const char* hdr = "--- TradeBoy Log Start V7 ---\n";
        (void)write(fd_, hdr, std::strlen(hdr));
    }
}

void Logger::log(const char* s) {
    if (!s) return;
    if (fd_ < 0) return;
    (void)write(fd_, s, std::strlen(s));
}

void Logger::flush() {
    // Intentionally a no-op. On RG34XX, fflush/fsync may block for a long time.
}

void Logger::shutdown() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
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
