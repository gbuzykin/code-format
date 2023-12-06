#include "parser.h"

namespace lex_detail {
#include "lex_analyzer.inl"
}

void Parser::parseNext(Token& token) {
    if (!revert_stack_.empty()) {
        token = revert_stack_.back();
        revert_stack_.pop_back();
        return;
    }

    token.type = TokenType::kSymbol;
    token.ws_count = 0;
    token.line = line_, token.pos = pos_;

    const char* token_start = first_;

    while (true) {
        int pat = 0;
        unsigned llen = 0;
        const char *first = first_, *lexeme = first;
        while (true) {
            bool stack_limitation = false;
            const char* last = last_;
            if (lex_state_stack_.avail() < static_cast<size_t>(last - first)) {
                last = first + lex_state_stack_.avail();
                stack_limitation = true;
            }
            pat = lex_detail::lex(first, last, lex_state_stack_.p_curr(), &llen, stack_limitation);
            if (pat >= lex_detail::predef_pat_default || !stack_limitation) { break; }
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
                case lex_detail::pat_eol: {
                    token.ws_count += llen;
                    token.line = line_, token.pos = pos_;
                } break;
                default: break;
            }
        } else {
            token.type = TokenType::kEof;
        }

        if (pat == lex_detail::pat_eol) {
            lex_state_stack_.back() = lex_detail::sc_at_beg_of_line;
        } else if (pat != lex_detail::pat_ws) {
            if (pat != lex_detail::pat_preproc) { lex_state_stack_.back() = lex_detail::sc_initial; }
            token.text = std::string_view{token_start, static_cast<size_t>(first_ - token_start)};
            break;
        }
    }
}
