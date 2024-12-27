#pragma once

#include "uxs/algorithm.h"
#include "uxs/format_fs.h"

namespace lex_detail {
#include "lex_defs.h"
}

enum class TextProcFlags { kNone = 0, kAtBegOfLine = 1 };
UXS_IMPLEMENT_BITWISE_OPS_FOR_ENUM(TextProcFlags, int);

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

class Parser {
 public:
    enum class TokenType {
        kEof = 0,
        kSymbol,
        kIdentifier,
        kString,
        kInteger,
        kReal,
        kPreprocId,
        kPreprocBody,
        kComment,
    };

    struct Token {
        TokenType type = TokenType::kEof;
        std::string_view text;
        size_t ws_count = 0;
        unsigned line = 0, pos = 0;
        bool isEof() const { return type == TokenType::kEof; }
        bool isSymbol(char ch) const { return type == TokenType::kSymbol && text[ws_count] == ch; }
        bool isComment() const { return type == TokenType::kComment; }
        bool isIdentifier(std::string_view id) const {
            return type == TokenType::kIdentifier && getTrimmedText() == id;
        }
        template<typename Range>
        bool isAnyOfIdentifiers(Range&& r) const {
            return type == TokenType::kIdentifier && uxs::find(r, getTrimmedText()).second;
        }
        bool isPreprocIdentifier(std::string_view id) const {
            return type == TokenType::kPreprocId && getPreprocIdentifier() == id;
        }
        std::string_view getPreprocIdentifier() const;
        int trackLevel(int level, char ch_open, char ch_close) const {
            if (type == TokenType::kSymbol) {
                if (text[ws_count] == ch_open) {
                    ++level;
                } else if (text[ws_count] == ch_close) {
                    --level;
                }
            }
            return level;
        }
        std::string_view getTrimmedText() const { return text.substr(ws_count); }
        bool hasNewLine() const { return text.substr(0, ws_count).find('\n') != std::string::npos; }
        std::string makeIndented(std::string_view text) const {
            std::string result = '\n' + std::string(std::max<size_t>(1, pos) - 1, ' ');
            return result += text;
        }
    };

    Parser(std::string file_name, uxs::span<const char> text, TextProcFlags flags = TextProcFlags::kAtBegOfLine)
        : file_name_(std::move(file_name)) {
        first_ = text.data(), last_ = text.data() + text.size();
        revert_stack_.reserve(16);
        lex_state_stack_.reserve(256);
        lex_state_stack_.push_back(!!(flags & TextProcFlags::kAtBegOfLine) ? lex_detail::sc_at_beg_of_line :
                                                                             lex_detail::sc_initial);
    }
    const std::string& getFileName() const { return file_name_; }
    unsigned getLn() const { return line_; }
    void parseNext(Token& token);
    void revert(Token token) { revert_stack_.emplace_back(token); }

 private:
    std::string file_name_;
    unsigned line_ = 1, pos_ = 1;
    const char* first_ = nullptr;
    const char* last_ = nullptr;
    uxs::inline_basic_dynbuffer<int, 1> lex_state_stack_;
    std::vector<Token> revert_stack_;

    void trackPosition(std::string_view s) {
        uxs::for_each(s, [this](char ch) {
            if (ch == '\n') { ++line_, pos_ = 0; }
            ++pos_;
        });
    }
};
