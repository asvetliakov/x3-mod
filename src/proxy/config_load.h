#pragma once
// Loading x3m.ini (docs/architecture/config-file.md): called once from initialize_log, under load_backend's INIT_ONCE,
// before any device exists. Never fatal and never writes the file.
#include <windows.h>
#include <string>

namespace x3m::config {
// Resolves X3M_CONFIG (unset: <module dir>\x3m.ini when present; none: no file; bare: no file and no defaults, the
// fixtures' "absent = off"; else that file, a relative path against the module directory), reads and parses the file
// and installs the resolver behind config::get. Logs nothing (the session log is not open yet).
void load(HMODULE module) noexcept;
// proxy_options (proxy_identity.cpp): the settings the environment does not set but config::get resolves, as
// " X3M_NAME=value@file|@default" tokens in table (name) order; after load only (empty before). May throw bad_alloc.
std::string effective_below_environment();
// The always-tier rows of the load: one config_open, at most 32 config_key and one config_more, one config_file
// (when a file was read). Called once, after log_open.
void log_rows() noexcept;
}
