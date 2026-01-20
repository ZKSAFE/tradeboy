#include "Process.h"

#include <cstdio>

namespace tradeboy::utils {

bool run_cmd_capture(const std::string& cmd, std::string& out) {
    out.clear();
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) return false;
    char buf[4096];
    while (true) {
        size_t n = fread(buf, 1, sizeof(buf), p);
        if (n > 0) out.append(buf, n);
        if (n < sizeof(buf)) break;
    }
    int rc = pclose(p);
    return rc == 0;
}

} // namespace tradeboy::utils
