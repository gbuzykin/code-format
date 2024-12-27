#pragma once

#include "uxs/format_fs.h"

extern unsigned g_debug_level;

template<typename... Args>
void printError(uxs::format_string<Args...> fmt, const Args&... args) {
    std::string msg("\033[1;37mcode-format: \033[0;31merror: \033[0m");
    msg += fmt.get();
    uxs::vprint(uxs::stdbuf::err, msg, uxs::make_format_args(args...)).endl();
}

template<typename... Args>
void printWarning(uxs::format_string<Args...> fmt, const Args&... args) {
    std::string msg("\033[1;37mcode-format: \033[0;35mwarning: \033[0m");
    msg += fmt.get();
    uxs::vprint(uxs::stdbuf::out, msg, uxs::make_format_args(args...)).endl();
}

template<typename... Args>
void printDebug(unsigned level, uxs::format_string<Args...> fmt, const Args&... args) {
    if (g_debug_level < level) { return; }
    std::string msg("\033[1;37mcode-format: \033[0;33mdebug: \033[0m");
    msg += fmt.get();
    uxs::vprint(uxs::stdbuf::out, msg, uxs::make_format_args(args...)).endl();
}
