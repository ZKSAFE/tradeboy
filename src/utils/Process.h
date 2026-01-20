#pragma once

#include <string>

namespace tradeboy::utils {

bool run_cmd_capture(const std::string& cmd, std::string& out);

} // namespace tradeboy::utils
