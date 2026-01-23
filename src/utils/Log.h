#pragma once

// NOTE: log_to_file is intentionally banned on RG34XX.
// Using varargs logging has caused ABI/stack corruption crashes during startup.
// Use log_str(const char*) instead.

// log_str is defined in main.cpp for backward compatibility.
// It delegates to tradeboy::core::Logger internally.
void log_str(const char* s);

#define log_to_file(...) static_assert(false, "log_to_file is banned; use log_str")
