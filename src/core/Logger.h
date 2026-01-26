#pragma once

namespace tradeboy::core {

class Logger {
public:
    static Logger& instance();

    void init(const char* filename);
    void log(const char* s);
    void flush();
    void shutdown();

private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    int fd_ = -1;
};

void logger_init(const char* filename);
void logger_log(const char* s);
void logger_flush();
void logger_shutdown();

} // namespace tradeboy::core
