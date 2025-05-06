#include "parser.h"

namespace lex_detail {
#include "lex_analyzer.inl"
}

unsigned g_debug_level = 0;

namespace {
std::size_t countWs(std::string_view text) {
    std::size_t count = 0;
    for (; count != text.size(); ++count) {
        if (text[count] == '\\' && count + 1 != text.size() && text[count + 1] == '\n') {
            ++count;
        } else if (!uxs::is_space(text[count])) {
            break;
        }
    }
    return count;
}
}  // namespace

Parser::Token Parser::parseNext() {
    if (!revert_stack_.empty()) {
        auto token = revert_stack_.back();
        revert_stack_.pop_back();
        return token;
    }

    Token token{TokenType::kSymbol, false, line_, pos_};

    const char* token_start = first_;

    while (true) {
        int pat = 0;
        std::size_t llen = 0;
        const char *first = first_, *lexeme = first;
        while (true) {
            const char* last = last_;
            if (lex_state_stack_.avail() < static_cast<std::size_t>(last - first)) {
                last = first + lex_state_stack_.avail();
            }
            auto* sptr = lex_state_stack_.endp();
            pat = lex_detail::lex(first, last, &sptr, &llen, last != last_ ? lex_detail::flag_has_more : 0);
            lex_state_stack_.setsize(sptr - lex_state_stack_.data());
            if (pat >= lex_detail::predef_pat_default || last == last_) { break; }
            // enlarge state stack and continue analysis
            lex_state_stack_.reserve(llen);
            first = last;
        }
        first_ += llen;
        if (pat >= lex_detail::predef_pat_default) {
            trackPosition(std::string_view{lexeme, llen});
            switch (pat) {
                case lex_detail::pat_comment: token.type = TokenType::kComment; break;
                case lex_detail::pat_string: token.type = TokenType::kString; break;
                case lex_detail::pat_id: token.type = TokenType::kIdentifier; break;
                case lex_detail::pat_int: token.type = TokenType::kInteger; break;
                case lex_detail::pat_real: token.type = TokenType::kReal; break;
                case lex_detail::pat_preproc_body: {
                    token.type = TokenType::kPreprocBody;
                    lex_state_stack_.back() = lex_detail::sc_initial;
                } break;
                case lex_detail::pat_preproc: {
                    token.type = TokenType::kPreprocId;
                    lex_state_stack_.back() = lex_detail::sc_preproc;
                } break;
                case lex_detail::pat_ws:
                case lex_detail::pat_eol: token.ws_count += llen; break;
                default: break;
            }
        } else {
            token.type = TokenType::kEof;
        }

        if (pat == lex_detail::pat_eol) {
            lex_state_stack_.back() = lex_detail::sc_at_beg_of_line;
        } else if (pat != lex_detail::pat_ws) {
            if (pat != lex_detail::pat_preproc) { lex_state_stack_.back() = lex_detail::sc_initial; }
            token.text = std::string_view{token_start, static_cast<std::size_t>(first_ - token_start)};
            if (token.type == TokenType::kPreprocBody) { token.ws_count = countWs(token.text); }
            if (token.type != TokenType::kComment) {
                token.is_first_significant = is_first_significant_token_;
                is_first_significant_token_ = false;
            }
            break;
        }
    }

    return token;
}

std::string_view Parser::Token::getPreprocIdentifier() const {
    return std::string_view(
        std::find_if(text.begin() + ws_count, text.end(), [](char ch) { return uxs::is_alpha(ch) || ch == '_'; }),
        text.end());
}

std::string_view Parser::Token::getFirstIdentifier() const {
    auto first = text.begin() + ws_count;
    if (first == text.end() || (!uxs::is_alpha(*first) && *first != '_')) { return {}; }
    auto last = std::find_if(first + 1, text.end(), [](char ch) { return !uxs::is_alnum(ch) && ch != '_'; });
    return std::string_view(first, last);
}
