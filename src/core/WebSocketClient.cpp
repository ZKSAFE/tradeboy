#include "WebSocketClient.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/select.h>

#include "utils/Log.h"

namespace tradeboy::core {

WebSocketClient::WebSocketClient() {
    pthread_mutex_init(&mu_, nullptr);
    mask_seed_ = (unsigned int)(std::time(nullptr) ^ (unsigned int)getpid());
}

WebSocketClient::~WebSocketClient() {
    disconnect();
    pthread_mutex_destroy(&mu_);
}

bool WebSocketClient::popen2_sh(const char* cmd, Popen2& p) {
    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) != 0) return false;
    if (pipe(out_pipe) != 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        return false;
    }

    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(out_pipe[1], STDERR_FILENO);

        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);

        execl("/bin/sh", "sh", "-c", cmd, (char*)nullptr);
        _exit(127);
    }

    close(in_pipe[0]);
    close(out_pipe[1]);

    p.in = fdopen(in_pipe[1], "w");
    p.out = fdopen(out_pipe[0], "r");
    p.pid = pid;
    if (!p.in || !p.out) {
        if (p.in) fclose(p.in);
        if (p.out) fclose(p.out);
        p.in = nullptr;
        p.out = nullptr;
        kill(pid, SIGKILL);
        int st = 0;
        waitpid(pid, &st, 0);
        return false;
    }

    setvbuf(p.in, nullptr, _IONBF, 0);
    return true;
}

void WebSocketClient::pclose2(Popen2& p) {
    if (p.in) fclose(p.in);
    if (p.out) fclose(p.out);
    p.in = nullptr;
    p.out = nullptr;
    if (p.pid > 0) {
        int st = 0;
        kill(p.pid, SIGTERM);
        const long long start_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
        while (true) {
            pid_t rc = waitpid(p.pid, &st, WNOHANG);
            if (rc == p.pid) break;
            if (rc < 0) break;

            const long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::system_clock::now().time_since_epoch())
                                          .count();
            if ((now_ms - start_ms) > 1500) {
                kill(p.pid, SIGKILL);
                waitpid(p.pid, &st, 0);
                break;
            }
            usleep(50 * 1000);
        }
    }
    p.pid = -1;
}

std::string WebSocketClient::base64_encode(const unsigned char* data, size_t len) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        unsigned int v = (unsigned int)data[i] << 16;
        if (i + 1 < len) v |= (unsigned int)data[i + 1] << 8;
        if (i + 2 < len) v |= (unsigned int)data[i + 2];

        out.push_back(tbl[(v >> 18) & 63]);
        out.push_back(tbl[(v >> 12) & 63]);
        if (i + 1 < len) out.push_back(tbl[(v >> 6) & 63]);
        else out.push_back('=');
        if (i + 2 < len) out.push_back(tbl[v & 63]);
        else out.push_back('=');
    }
    return out;
}

bool WebSocketClient::read_until(FILE* f, std::string& out, const char* needle, int max_bytes) {
    out.clear();
    const size_t nlen = std::strlen(needle);
    if (nlen == 0) return true;

    std::vector<char> window;
    window.reserve(nlen);

    int c = 0;
    int read = 0;
    while ((c = std::fgetc(f)) != EOF) {
        out.push_back((char)c);
        read++;
        if ((int)window.size() < (int)nlen) window.push_back((char)c);
        else {
            window.erase(window.begin());
            window.push_back((char)c);
        }
        if (window.size() == nlen && std::memcmp(window.data(), needle, nlen) == 0) {
            return true;
        }
        if (read >= max_bytes) break;
    }
    return false;
}

bool WebSocketClient::read_exact(FILE* f, unsigned char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        size_t r = std::fread(buf + got, 1, n - got, f);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

bool WebSocketClient::read_exact_timeout(FILE* f, unsigned char* buf, size_t n, int timeout_ms, const std::atomic<bool>* stop_flag) {
    size_t got = 0;
    const int fd = fileno(f);
    while (got < n) {
        if (stop_flag && stop_flag->load()) return false;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int rc = select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (rc == 0) {
            continue;
        }
        if (rc < 0) {
            return false;
        }

        size_t r = std::fread(buf + got, 1, n - got, f);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

bool WebSocketClient::ws_write_frame(FILE* f, unsigned char opcode, const unsigned char* payload, size_t plen, unsigned int mask_seed) {
    std::array<unsigned char, 14> hdr;
    size_t hlen = 0;

    hdr[0] = (unsigned char)(0x80 | (opcode & 0x0F));

    if (plen < 126) {
        hdr[1] = (unsigned char)(0x80 | (unsigned char)plen);
        hlen = 2;
    } else if (plen <= 0xFFFF) {
        hdr[1] = 0x80 | 126;
        hdr[2] = (unsigned char)((plen >> 8) & 0xFF);
        hdr[3] = (unsigned char)(plen & 0xFF);
        hlen = 4;
    } else {
        return false;
    }

    unsigned char mask[4];
    unsigned int x = mask_seed;
    x = x * 1664525u + 1013904223u;
    mask[0] = (unsigned char)(x & 0xFF);
    x = x * 1664525u + 1013904223u;
    mask[1] = (unsigned char)(x & 0xFF);
    x = x * 1664525u + 1013904223u;
    mask[2] = (unsigned char)(x & 0xFF);
    x = x * 1664525u + 1013904223u;
    mask[3] = (unsigned char)(x & 0xFF);

    std::memcpy(hdr.data() + hlen, mask, 4);
    hlen += 4;

    if (std::fwrite(hdr.data(), 1, hlen, f) != hlen) return false;
    for (size_t i = 0; i < plen; i++) {
        unsigned char b = payload ? payload[i] : 0;
        b ^= mask[i % 4];
        if (std::fwrite(&b, 1, 1, f) != 1) return false;
    }
    std::fflush(f);
    return true;
}

bool WebSocketClient::connect(const std::string& host, int port, const std::string& path) {
    pthread_mutex_lock(&mu_);

    if (proc_.pid > 0) {
        pclose2(proc_);
    }

    std::string cmd = "/usr/bin/openssl s_client -quiet -connect " + host + ":" + std::to_string(port) + " -servername " + host;
    if (!popen2_sh(cmd.c_str(), proc_)) {
        log_str("[WS] popen2 failed\n");
        pthread_mutex_unlock(&mu_);
        return false;
    }

    std::srand((unsigned int)(std::time(nullptr) ^ (unsigned int)proc_.pid));

    unsigned char key_raw[16];
    for (int i = 0; i < 16; i++) key_raw[i] = (unsigned char)(std::rand() & 0xFF);
    std::string key_b64 = base64_encode(key_raw, sizeof(key_raw));

    std::string req;
    req += "GET " + path + " HTTP/1.1\r\n";
    req += "Host: " + host + "\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key_b64 + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "\r\n";

    if (std::fwrite(req.data(), 1, req.size(), proc_.in) != req.size()) {
        pclose2(proc_);
        pthread_mutex_unlock(&mu_);
        return false;
    }
    std::fflush(proc_.in);

    std::string headers;
    if (!read_until(proc_.out, headers, "\r\n\r\n", 65536)) {
        log_str("[WS] handshake read headers failed\n");
        pclose2(proc_);
        pthread_mutex_unlock(&mu_);
        return false;
    }

    if (headers.find(" 101 ") == std::string::npos && headers.find(" 101\r\n") == std::string::npos) {
        log_str("[WS] handshake failed\n");
        pclose2(proc_);
        pthread_mutex_unlock(&mu_);
        return false;
    }

    log_str("[WS] handshake ok\n");
    pthread_mutex_unlock(&mu_);
    return true;
}

void WebSocketClient::disconnect() {
    pthread_mutex_lock(&mu_);
    if (proc_.pid > 0) {
        pclose2(proc_);
    }
    pthread_mutex_unlock(&mu_);
}

bool WebSocketClient::is_connected() const {
    pthread_mutex_lock(&mu_);
    bool connected = (proc_.pid > 0 && proc_.in != nullptr && proc_.out != nullptr);
    pthread_mutex_unlock(&mu_);
    return connected;
}

bool WebSocketClient::send_text(const std::string& msg) {
    pthread_mutex_lock(&mu_);
    if (!proc_.in) {
        pthread_mutex_unlock(&mu_);
        return false;
    }

    mask_seed_ = mask_seed_ * 1664525u + 1013904223u;
    bool ok = ws_write_frame(proc_.in, OPCODE_TEXT, (const unsigned char*)msg.data(), msg.size(), mask_seed_);
    pthread_mutex_unlock(&mu_);
    return ok;
}

bool WebSocketClient::send_ping() {
    pthread_mutex_lock(&mu_);
    if (!proc_.in) {
        pthread_mutex_unlock(&mu_);
        return false;
    }

    mask_seed_ = mask_seed_ * 1664525u + 1013904223u;
    bool ok = ws_write_frame(proc_.in, OPCODE_PING, nullptr, 0, mask_seed_);
    pthread_mutex_unlock(&mu_);
    return ok;
}

bool WebSocketClient::send_pong(const std::vector<unsigned char>& payload) {
    pthread_mutex_lock(&mu_);
    if (!proc_.in) {
        pthread_mutex_unlock(&mu_);
        return false;
    }

    mask_seed_ = mask_seed_ * 1664525u + 1013904223u;
    bool ok = ws_write_frame(proc_.in, OPCODE_PONG, payload.empty() ? nullptr : payload.data(), payload.size(), mask_seed_);
    pthread_mutex_unlock(&mu_);
    return ok;
}

bool WebSocketClient::read_frame(WebSocketFrame& out_frame, int timeout_ms, const std::atomic<bool>* stop_flag) {
    pthread_mutex_lock(&mu_);
    if (!proc_.out) {
        pthread_mutex_unlock(&mu_);
        return false;
    }

    out_frame.payload.clear();

    unsigned char b2[2];
    if (!read_exact_timeout(proc_.out, b2, 2, timeout_ms, stop_flag)) {
        pthread_mutex_unlock(&mu_);
        return false;
    }

    const unsigned char fin_opcode = b2[0];
    const unsigned char b1 = b2[1];

    out_frame.opcode = fin_opcode & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    unsigned long long plen = (unsigned long long)(b1 & 0x7F);

    if (plen == 126) {
        unsigned char ext[2];
        if (!read_exact_timeout(proc_.out, ext, 2, timeout_ms, stop_flag)) {
            pthread_mutex_unlock(&mu_);
            return false;
        }
        plen = ((unsigned long long)ext[0] << 8) | (unsigned long long)ext[1];
    } else if (plen == 127) {
        unsigned char ext[8];
        if (!read_exact_timeout(proc_.out, ext, 8, timeout_ms, stop_flag)) {
            pthread_mutex_unlock(&mu_);
            return false;
        }
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | (unsigned long long)ext[i];
    }

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (!read_exact_timeout(proc_.out, mask, 4, timeout_ms, stop_flag)) {
            pthread_mutex_unlock(&mu_);
            return false;
        }
    }

    const int max_payload = 2 * 1024 * 1024;
    if ((int)plen > max_payload) {
        std::vector<unsigned char> tmp;
        tmp.resize((size_t)plen);
        if (!read_exact_timeout(proc_.out, tmp.data(), (size_t)plen, timeout_ms, stop_flag)) {
            pthread_mutex_unlock(&mu_);
            return false;
        }
        pthread_mutex_unlock(&mu_);
        return false;
    }

    out_frame.payload.resize((size_t)plen);
    if (plen > 0) {
        if (!read_exact_timeout(proc_.out, out_frame.payload.data(), (size_t)plen, timeout_ms, stop_flag)) {
            pthread_mutex_unlock(&mu_);
            return false;
        }
    }

    if (masked) {
        for (size_t i = 0; i < (size_t)plen; i++) out_frame.payload[i] ^= mask[i % 4];
    }

    pthread_mutex_unlock(&mu_);
    return true;
}

} // namespace tradeboy::core
