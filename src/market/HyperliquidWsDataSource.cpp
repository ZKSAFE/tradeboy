#include "HyperliquidWsDataSource.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/select.h>

#include "Hyperliquid.h"

extern void log_to_file(const char* fmt, ...);

namespace tradeboy::market {

struct Popen2 {
    FILE* in = nullptr;  // to child stdin
    FILE* out = nullptr; // from child stdout
    pid_t pid = -1;
};

static bool parse_post_id(const std::string& msg, unsigned int& out_id) {
    size_t p = msg.find("\"id\"");
    if (p == std::string::npos) return false;
    p = msg.find(':', p);
    if (p == std::string::npos) return false;
    p++;
    while (p < msg.size() && (msg[p] == ' ' || msg[p] == '\n' || msg[p] == '\t')) p++;
    unsigned int v = 0;
    bool any = false;
    while (p < msg.size() && msg[p] >= '0' && msg[p] <= '9') {
        any = true;
        v = v * 10u + (unsigned int)(msg[p] - '0');
        p++;
    }
    if (!any) return false;
    out_id = v;
    return true;
}

static bool popen2_sh(const char* cmd, Popen2& p) {
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

static void pclose2(Popen2& p) {
    if (p.in) fclose(p.in);
    if (p.out) fclose(p.out);
    p.in = nullptr;
    p.out = nullptr;
    if (p.pid > 0) {
        int st = 0;

        // Avoid shutdown hangs if the child doesn't exit promptly.
        // Try TERM first, then KILL after a short timeout.
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

static std::string base64_encode(const unsigned char* data, size_t len) {
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

static bool read_until(FILE* f, std::string& out, const char* needle, int max_bytes) {
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

static bool read_exact(FILE* f, unsigned char* buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        size_t r = std::fread(buf + got, 1, n - got, f);
        if (r == 0) return false;
        got += r;
    }
    return true;
}

static bool read_exact_timeout(FILE* f, unsigned char* buf, size_t n, int timeout_ms, const std::atomic<bool>* stop_flag) {
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
            continue; // timeout
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

static bool ws_write_text(FILE* f, const std::string& s, unsigned int mask_seed) {
    std::array<unsigned char, 14> hdr;
    size_t hlen = 0;

    hdr[0] = 0x81;

    const size_t plen = s.size();
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

    std::string masked;
    masked.resize(plen);
    for (size_t i = 0; i < plen; i++) {
        masked[i] = (char)((unsigned char)s[i] ^ mask[i % 4]);
    }
    if (plen > 0) {
        if (std::fwrite(masked.data(), 1, plen, f) != plen) return false;
    }

    std::fflush(f);
    return true;
}

static bool ws_write_frame(FILE* f, unsigned char opcode, const unsigned char* payload, size_t plen, unsigned int mask_seed) {
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

static bool ws_read_text(FILE* f, std::string& out, int max_payload) {
    out.clear();

    unsigned char b2[2];
    if (!read_exact(f, b2, 2)) return false;

    const unsigned char fin_opcode = b2[0];
    const unsigned char b1 = b2[1];

    const unsigned char opcode = fin_opcode & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    unsigned long long plen = (unsigned long long)(b1 & 0x7F);

    if (plen == 126) {
        unsigned char ext[2];
        if (!read_exact(f, ext, 2)) return false;
        plen = ((unsigned long long)ext[0] << 8) | (unsigned long long)ext[1];
    } else if (plen == 127) {
        unsigned char ext[8];
        if (!read_exact(f, ext, 8)) return false;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | (unsigned long long)ext[i];
    }

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (!read_exact(f, mask, 4)) return false;
    }

    if ((int)plen > max_payload) {
        std::vector<unsigned char> tmp;
        tmp.resize((size_t)plen);
        if (!read_exact(f, tmp.data(), (size_t)plen)) return false;
        return false;
    }

    std::vector<unsigned char> payload;
    payload.resize((size_t)plen);
    if (plen > 0) {
        if (!read_exact(f, payload.data(), (size_t)plen)) return false;
    }

    if (masked) {
        for (size_t i = 0; i < (size_t)plen; i++) payload[i] ^= mask[i % 4];
    }

    if (opcode == 0x8) {
        return false;
    }
    if (opcode == 0x9) {
        return ws_read_text(f, out, max_payload);
    }
    if (opcode != 0x1) {
        return ws_read_text(f, out, max_payload);
    }

    out.assign((const char*)payload.data(), payload.size());
    return true;
}

static bool ws_read_frame_timeout(FILE* f,
                                 unsigned char& out_opcode,
                                 std::vector<unsigned char>& out_payload,
                                 int max_payload,
                                 int timeout_ms,
                                 const std::atomic<bool>* stop_flag) {
    out_payload.clear();

    unsigned char b2[2];
    if (!read_exact_timeout(f, b2, 2, timeout_ms, stop_flag)) return false;

    const unsigned char fin_opcode = b2[0];
    const unsigned char b1 = b2[1];

    out_opcode = fin_opcode & 0x0F;
    const bool masked = (b1 & 0x80) != 0;
    unsigned long long plen = (unsigned long long)(b1 & 0x7F);

    if (plen == 126) {
        unsigned char ext[2];
        if (!read_exact_timeout(f, ext, 2, timeout_ms, stop_flag)) return false;
        plen = ((unsigned long long)ext[0] << 8) | (unsigned long long)ext[1];
    } else if (plen == 127) {
        unsigned char ext[8];
        if (!read_exact_timeout(f, ext, 8, timeout_ms, stop_flag)) return false;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | (unsigned long long)ext[i];
    }

    unsigned char mask[4] = {0, 0, 0, 0};
    if (masked) {
        if (!read_exact_timeout(f, mask, 4, timeout_ms, stop_flag)) return false;
    }

    if ((int)plen > max_payload) {
        std::vector<unsigned char> tmp;
        tmp.resize((size_t)plen);
        if (!read_exact_timeout(f, tmp.data(), (size_t)plen, timeout_ms, stop_flag)) return false;
        return false;
    }

    out_payload.resize((size_t)plen);
    if (plen > 0) {
        if (!read_exact_timeout(f, out_payload.data(), (size_t)plen, timeout_ms, stop_flag)) return false;
    }

    if (masked) {
        for (size_t i = 0; i < (size_t)plen; i++) out_payload[i] ^= mask[i % 4];
    }
    return true;
}

static std::string extract_data_object_if_wrapped(const std::string& msg) {
    size_t p = msg.find("\"data\"");
    if (p == std::string::npos) return msg;

    p = msg.find('{', p);
    if (p == std::string::npos) return msg;

    int depth = 0;
    for (size_t i = p; i < msg.size(); i++) {
        char c = msg[i];
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                return msg.substr(p, i - p + 1);
            }
        }
    }
    return msg;
}

static std::string extract_object_after_key(const std::string& msg, const char* key) {
    const std::string k = std::string("\"") + key + "\"";
    size_t p = msg.find(k);
    if (p == std::string::npos) return std::string();
    p = msg.find('{', p);
    if (p == std::string::npos) return std::string();

    int depth = 0;
    for (size_t i = p; i < msg.size(); i++) {
        char c = msg[i];
        if (c == '{') depth++;
        else if (c == '}') {
            depth--;
            if (depth == 0) {
                return msg.substr(p, i - p + 1);
            }
        }
    }
    return std::string();
}

static bool ws_connect_and_subscribe(Popen2& p) {
    const char* cmd = "/usr/bin/openssl s_client -quiet -connect api.hyperliquid.xyz:443 -servername api.hyperliquid.xyz";
    if (!popen2_sh(cmd, p)) {
        log_to_file("[WS] popen2 failed\n");
        return false;
    }

    std::srand((unsigned int)(std::time(nullptr) ^ (unsigned int)p.pid));

    unsigned char key_raw[16];
    for (int i = 0; i < 16; i++) key_raw[i] = (unsigned char)(std::rand() & 0xFF);
    std::string key_b64 = base64_encode(key_raw, sizeof(key_raw));

    std::string req;
    req += "GET /ws HTTP/1.1\r\n";
    req += "Host: api.hyperliquid.xyz\r\n";
    req += "Upgrade: websocket\r\n";
    req += "Connection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key_b64 + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += "\r\n";

    if (std::fwrite(req.data(), 1, req.size(), p.in) != req.size()) {
        pclose2(p);
        return false;
    }
    std::fflush(p.in);

    std::string headers;
    if (!read_until(p.out, headers, "\r\n\r\n", 65536)) {
        if (!headers.empty()) {
            std::string prefix = headers;
            if (prefix.size() > 512) prefix.resize(512);
            log_to_file("[WS] handshake read headers failed (read=%d) prefix=<<<%s>>>\n", (int)headers.size(), prefix.c_str());
        } else {
            log_to_file("[WS] handshake read headers failed (read=0)\n");
        }
        pclose2(p);
        return false;
    }

    if (headers.find(" 101 ") == std::string::npos && headers.find(" 101\r\n") == std::string::npos) {
        log_to_file("[WS] handshake failed: %s\n", headers.c_str());
        pclose2(p);
        return false;
    }

    log_to_file("[WS] handshake ok\n");

    const std::string sub = "{\"method\":\"subscribe\",\"subscription\":{\"type\":\"allMids\"}}";
    if (!ws_write_text(p.in, sub, (unsigned int)std::rand())) {
        pclose2(p);
        return false;
    }

    return true;
}

HyperliquidWsDataSource::HyperliquidWsDataSource() {
    th_ = std::thread([this]() { run(); });
}

HyperliquidWsDataSource::~HyperliquidWsDataSource() {
    stop_.store(true);
    if (th_.joinable()) th_.join();
}

void HyperliquidWsDataSource::set_user_address(const std::string& user_address_0x) {
    std::lock_guard<std::mutex> lock(mu_);
    if (user_address_0x_ == user_address_0x) return;
    user_address_0x_ = user_address_0x;
    reconnect_requested_.store(true);
}

bool HyperliquidWsDataSource::fetch_all_mids_raw(std::string& out_json) {
    const long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

    std::lock_guard<std::mutex> lock(mu_);
    if (latest_mids_json_.empty()) return false;
    // If data is too old, let MarketDataService backoff/retry.
    if (latest_mids_ms_ == 0 || (now_ms - latest_mids_ms_) > 15000) return false;
    out_json = latest_mids_json_;
    return true;
}

bool HyperliquidWsDataSource::fetch_user_webdata_raw(std::string& out_json) {
    const long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

    std::lock_guard<std::mutex> lock(mu_);
    if (latest_user_json_.empty()) return false;
    if (latest_user_ms_ == 0 || (now_ms - latest_user_ms_) > 15000) return false;
    out_json = latest_user_json_;
    return true;
}

bool HyperliquidWsDataSource::fetch_spot_clearinghouse_state_raw(std::string& out_json) {
    const long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (!latest_spot_json_.empty() && latest_spot_ms_ != 0 && (now_ms - latest_spot_ms_) <= 15000) {
            out_json = latest_spot_json_;
            return true;
        }
    }

    spot_request_pending_.store(true);
    return false;
}

void HyperliquidWsDataSource::run() {
    int reconnect_backoff_ms = 1000;
    unsigned int log_every = 0;

    while (!stop_.load()) {
        Popen2 p;
        if (!ws_connect_and_subscribe(p)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_backoff_ms));
            reconnect_backoff_ms = std::min(30000, reconnect_backoff_ms * 2);
            continue;
        }

        std::string user_addr;
        {
            std::lock_guard<std::mutex> lock(mu_);
            user_addr = user_address_0x_;
        }
        if (!user_addr.empty()) {
            const std::string sub_user = std::string("{\"method\":\"subscribe\",\"subscription\":{\"type\":\"webData3\",\"user\":\"") +
                                         user_addr + "\"}}";
            if (!ws_write_text(p.in, sub_user, (unsigned int)std::rand())) {
                pclose2(p);
                continue;
            }
        }

        reconnect_backoff_ms = 1000;
        log_every = 0;

        long long last_ping_ms = 0;

        while (!stop_.load()) {
            const long long now_ms = (long long)std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::system_clock::now().time_since_epoch())
                                         .count();

            if (reconnect_requested_.exchange(false)) {
                break;
            }

            if (spot_request_pending_.exchange(false)) {
                std::string addr;
                {
                    std::lock_guard<std::mutex> lock(mu_);
                    addr = user_address_0x_;
                }
                if (!addr.empty()) {
                    unsigned int req_id = spot_request_id_++;
                    std::string req = std::string("{\"method\":\"post\",\"id\":") + std::to_string(req_id) +
                                      ",\"request\":{\"type\":\"info\",\"payload\":{\"type\":\"spotClearinghouseState\",\"user\":\"" +
                                      addr + "\"}}}";
                    if (ws_write_text(p.in, req, (unsigned int)std::rand())) {
                        std::lock_guard<std::mutex> lock(mu_);
                        spot_request_sent_id_ = req_id;
                    }
                }
            }

            // Proactive ping heartbeat (keepalive). The server may also send pings; we respond with pong.
            if (last_ping_ms == 0 || (now_ms - last_ping_ms) > 20000) {
                (void)ws_write_frame(p.in, 0x9, nullptr, 0, (unsigned int)std::rand());
                last_ping_ms = now_ms;
            }

            unsigned char opcode = 0;
            std::vector<unsigned char> payload;
            if (!ws_read_frame_timeout(p.out, opcode, payload, 2 * 1024 * 1024, 1000, &stop_)) {
                break;
            }

            if (opcode == 0x8) {
                break;
            }

            if (opcode == 0x9) {
                // Ping -> Pong
                (void)ws_write_frame(p.in, 0xA, payload.empty() ? nullptr : payload.data(), payload.size(), (unsigned int)std::rand());
                continue;
            }
            if (opcode != 0x1) {
                continue;
            }

            std::string msg((const char*)payload.data(), payload.size());
            if (msg.find("\"mids\"") != std::string::npos) {
                std::string data_obj = extract_data_object_if_wrapped(msg);
                std::string mids_obj = extract_object_after_key(data_obj, "mids");
                if (mids_obj.empty()) mids_obj = extract_object_after_key(msg, "mids");
                if (!mids_obj.empty() && mids_obj.find("\":\"") != std::string::npos) {
                    std::lock_guard<std::mutex> lock(mu_);
                    latest_mids_json_ = std::move(mids_obj);
                    latest_mids_ms_ = now_ms;
                    log_every++;
                    if ((log_every % 20) == 1) {
                        std::string prefix = latest_mids_json_;
                        if (prefix.size() > 120) prefix.resize(120);
                        log_to_file("[WS] allMids mids cached len=%d prefix=<<<%s>>>\n", (int)prefix.size(), prefix.c_str());
                    }
                }
                continue;
            }

            if (msg.find("\"webData3\"") != std::string::npos) {
                std::string data_obj = extract_data_object_if_wrapped(msg);
                if (!data_obj.empty()) {
                    std::lock_guard<std::mutex> lock(mu_);
                    latest_user_json_ = std::move(data_obj);
                    latest_user_ms_ = now_ms;
                }
                continue;
            }

            if (msg.find("\"channel\":\"post\"") != std::string::npos) {
                unsigned int resp_id = 0;
                if (parse_post_id(msg, resp_id)) {
                    unsigned int expected = 0;
                    {
                        std::lock_guard<std::mutex> lock(mu_);
                        expected = spot_request_sent_id_;
                    }
                    if (expected != 0 && resp_id == expected && msg.find("spotClearinghouseState") != std::string::npos) {
                        std::string data_obj = extract_data_object_if_wrapped(msg);
                        if (!data_obj.empty()) {
                            std::lock_guard<std::mutex> lock(mu_);
                            latest_spot_json_ = std::move(data_obj);
                            latest_spot_ms_ = now_ms;
                        }
                    }
                }
                continue;
            }

        }

        pclose2(p);
        std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_backoff_ms));
        reconnect_backoff_ms = std::min(30000, reconnect_backoff_ms * 2);
    }
}

} // namespace tradeboy::market
