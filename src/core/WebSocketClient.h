#pragma once

#include <atomic>
#include <string>
#include <vector>
#include <functional>

#include <pthread.h>

namespace tradeboy::core {

struct WebSocketFrame {
    unsigned char opcode = 0;
    std::vector<unsigned char> payload;
};

struct WebSocketClient {
    WebSocketClient();
    ~WebSocketClient();

    WebSocketClient(const WebSocketClient&) = delete;
    WebSocketClient& operator=(const WebSocketClient&) = delete;

    bool connect(const std::string& host, int port, const std::string& path);
    void disconnect();
    bool is_connected() const;

    bool send_text(const std::string& msg);
    bool send_ping();
    bool send_pong(const std::vector<unsigned char>& payload);

    bool read_frame(WebSocketFrame& out_frame, int timeout_ms, const std::atomic<bool>* stop_flag);

    static const unsigned char OPCODE_TEXT = 0x1;
    static const unsigned char OPCODE_BINARY = 0x2;
    static const unsigned char OPCODE_CLOSE = 0x8;
    static const unsigned char OPCODE_PING = 0x9;
    static const unsigned char OPCODE_PONG = 0xA;

private:
    struct Popen2 {
        FILE* in = nullptr;
        FILE* out = nullptr;
        pid_t pid = -1;
    };

    bool popen2_sh(const char* cmd, Popen2& p);
    void pclose2(Popen2& p);

    bool read_exact(FILE* f, unsigned char* buf, size_t n);
    bool read_exact_timeout(FILE* f, unsigned char* buf, size_t n, int timeout_ms, const std::atomic<bool>* stop_flag);
    bool read_until(FILE* f, std::string& out, const char* needle, int max_bytes);

    bool ws_write_frame(FILE* f, unsigned char opcode, const unsigned char* payload, size_t plen, unsigned int mask_seed);

    std::string base64_encode(const unsigned char* data, size_t len);

    Popen2 proc_;
    mutable pthread_mutex_t mu_;
    unsigned int mask_seed_ = 0;
};

} // namespace tradeboy::core
