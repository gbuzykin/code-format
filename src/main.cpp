#include <algorithm>
#include <array>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace lex_detail {
#include "lex_defs.h"
}

namespace lex_detail {
#include "lex_analyzer.inl"
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
        kPreprocDef,
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
            return type == TokenType::kIdentifier &&
                   std::find(std::begin(r), std::end(r), getTrimmedText()) != std::end(r);
        }
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

    Parser(const char* text, size_t length) {
        first_ = text, last_ = text + length;
        revert_stack_.reserve(16);
        lex_state_stack_.reserve(256);
        lex_state_stack_.push_back(lex_detail::sc_initial);
    }
    void parseNext(Token& token);
    void revert(Token token) { revert_stack_.emplace_back(token); }

 private:
    unsigned line_ = 1, pos_ = 1;
    const char* first_ = nullptr;
    const char* last_ = nullptr;
    std::vector<int> lex_state_stack_;
    std::vector<Token> revert_stack_;

    void trackPosition(std::string_view s) {
        for (auto it = s.begin(); it != s.end(); ++it) {
            if (*it == '\n') { ++line_, pos_ = 0; }
            ++pos_;
        }
    }
};

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
        unsigned llen = 0;
        const char* lexeme = first_;
        int pat_no = lex_detail::lex(lexeme, last_, lex_state_stack_, llen, false);
        first_ += llen;
        if (pat_no != lex_detail::err_end_of_input) {
            trackPosition(std::string_view(lexeme, llen));
            switch (pat_no) {
                case lex_detail::pat_comment: token.type = TokenType::kComment; break;
                case lex_detail::pat_string: token.type = TokenType::kString; break;
                case lex_detail::pat_id: token.type = TokenType::kIdentifier; break;
                case lex_detail::pat_int: token.type = TokenType::kInteger; break;
                case lex_detail::pat_real: token.type = TokenType::kReal; break;
                case lex_detail::pat_preproc_body: {
                    token.type = TokenType::kPreprocBody;
                    lex_state_stack_.pop_back();
                } break;
                case lex_detail::pat_preproc: {
                    token.type = TokenType::kPreprocDef;
                    lex_state_stack_.push_back(lex_detail::sc_preproc);
                } break;
                case lex_detail::pat_ws: {
                    token.ws_count += llen;
                    token.line = line_, token.pos = pos_;
                }; break;
                default: break;
            }
        } else {
            token.type = TokenType::kEof;
        }

        if (pat_no != lex_detail::pat_ws) {
            token.text = std::string_view(token_start, first_ - token_start);
            break;
        }
    }
}

void fixSingleStatement(Parser& parser, std::string& output, const Parser::Token& first_tkn) {
    Parser::Token token;
    bool is_else_block = false;

    do {
        parser.parseNext(token);
        if (!is_else_block && !first_tkn.isIdentifier("do")) {
            int level = -1;
            while (!token.isEof()) {
                output.append(token.text);
                if (level >= 0) {
                    level = token.trackLevel(level, '(', ')');
                } else if (token.isSymbol('(')) {
                    level = 1;
                }
                parser.parseNext(token);
                if (level == 0) { break; }
            }
        }

        std::vector<Parser::Token> comments;
        comments.reserve(16);
        while (token.isComment()) {
            comments.emplace_back(token);
            parser.parseNext(token);
        }

        if (!token.isEof()) { output.append(" {"); }
        for (const auto& comment : comments) { output.append(comment.text); }
        comments.clear();
        if (token.isEof()) { return; }

        static std::array<std::string_view, 5> key_words = {"if", "while", "for", "do"};
        if (!token.isSymbol('{')) {
            bool make_nl = token.hasNewLine(), has_comments = false;
            if (token.isAnyOfIdentifiers(key_words)) {
                output.append(token.text);
                fixSingleStatement(parser, output, token);
            } else {
                for (int level = 0; !token.isEof();) {
                    output.append(token.text);
                    if (level == 0 && token.isSymbol(';')) { break; }
                    level = token.trackLevel(level, '{', '}');
                    parser.parseNext(token);
                }
            }

            parser.parseNext(token);
            while (token.isComment() && !token.hasNewLine()) {
                has_comments = true;
                output.append(token.text);
                parser.parseNext(token);
            }

            output.append(make_nl || has_comments ? first_tkn.makeIndented("}") : " }");
        } else {
            parser.parseNext(token);
            for (int level = 1; !token.isEof();) {
                output.append(token.text);
                if (token.isAnyOfIdentifiers(key_words)) { fixSingleStatement(parser, output, token); }
                parser.parseNext(token);
                if (level == 0) { break; }
                level = token.trackLevel(level, '{', '}');
            }
        }

        bool has_comments = false;
        while (token.isComment()) {
            has_comments = true;
            output.append(token.text);
            parser.parseNext(token);
        }

        if (first_tkn.isIdentifier("do")) {
            if (token.isIdentifier("while")) {
                output.append(has_comments ? first_tkn.makeIndented("while") : " while");
                parser.parseNext(token);
            }
            for (int level = 0; !token.isEof();) {
                output.append(token.text);
                if (level == 0 && token.isSymbol(';')) { break; }
                level = token.trackLevel(level, '(', ')');
                parser.parseNext(token);
            }
            break;
        } else if (!is_else_block && first_tkn.isIdentifier("if")) {
            if (token.isIdentifier("else")) {
                output.append(has_comments ? first_tkn.makeIndented("else") : " else");
                parser.parseNext(token);
                while (token.isComment()) {
                    comments.emplace_back(token);
                    parser.parseNext(token);
                }
                if (token.isIdentifier("if")) {
                    for (const auto& comment : comments) { output.append(comment.text); }
                    output.append(!comments.empty() ? first_tkn.makeIndented("if") : " if");
                } else {
                    parser.revert(token);
                    while (!comments.empty()) {
                        parser.revert(comments.back());
                        comments.pop_back();
                    }
                    is_else_block = true;
                }
                comments.clear();
                continue;  // next 'else if'/'else' block
            }
        }
        parser.revert(token);
        break;
    } while (true);
}

using TextProcessFunc = std::function<void(Parser&, std::string&, const Parser::Token&)>;
std::string processText(const char* text, size_t length, const TextProcessFunc& proc_func) {
    std::string output;
    Parser parser(text, length);
    Parser::Token token;

    output.reserve(length + length / 10);
    do {
        parser.parseNext(token);
        if (token.type == Parser::TokenType::kPreprocBody) {
            output.append(processText(token.text.data(), token.text.size(), proc_func));
        } else {
            proc_func(parser, output, token);
        }
    } while (!token.isEof());
    return output;
}

int main(int argc, char** argv) {
    std::string input_file_name, output_file_name;
    bool fix_file_endings = false;
    bool fix_single_statement = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "-o") {
            if (++i < argc) { output_file_name = argv[i]; }
        } else if (arg == "--fix-file-endings") {
            fix_file_endings = true;
        } else if (arg == "--fix-single-statement") {
            fix_single_statement = true;
        } else if (arg == "--help") {
            // clang-format off
            static const char* text[] = {
                "Usage: code-format [options] file",
                "Options:",
                "    -o <file>               Output file name.",
                "    --fix-file-endings      Change file ending to one new-line symbol.",
                "    --fix-single-statement  Enclose single-statement blocks in brackets, ",
                "                            format `if`-`else if`-`else`-squences.",
                "    --help                  Display this information.",
            };
            // clang-format on
            for (const char* l : text) { std::cout << l << std::endl; }
            return 0;
        } else if (arg[0] != '-') {
            input_file_name = arg;
        } else {
            std::cerr << "code-format: fatal error: unknown flag `" << arg << "`" << std::endl;
            return -1;
        }
    }

    if (input_file_name.empty()) {
        std::cerr << "code-format: fatal error: no input file specified" << std::endl;
        return -1;
    }

    std::string src_full_text;
    if (std::ifstream ifile(input_file_name); ifile) {
        size_t file_sz = static_cast<size_t>(ifile.seekg(0, std::ios_base::end).tellg());
        src_full_text.resize(file_sz);
        ifile.seekg(0);
        ifile.read(src_full_text.data(), file_sz);
        src_full_text.resize(ifile.gcount());
    } else {
        std::cerr << "code-format: fatal error: could not open input file `" << input_file_name << "`";
        return -1;
    }

    std::cout << "Processing: " << input_file_name << "..." << std::endl;

#if 0
    auto proc_func = [](Parser& parser, std::string& output, const Parser::Token& token) {
        static const std::string type_names[] = {"kEof",  "kSymbol",     "kIdentifier",  "kString", "kInteger",
                                                 "kReal", "kPreprocDef", "kPreprocBody", "kComment"};
        std::cout << type_names[static_cast<unsigned>(token.type)] << ", ws_count = " << token.ws_count << ": \""
                  << token.getTrimmedText() << "\"" << std::endl;
        output.append(token.text);
    };
#else
    auto proc_func = [fix_single_statement, fix_file_endings](Parser& parser, std::string& output,
                                                              const Parser::Token& token) {
        static std::array<std::string_view, 5> key_words = {"if", "while", "for", "do"};
        if (!token.isEof() || !fix_file_endings) {
            output.append(token.text);
            if (fix_single_statement && token.isAnyOfIdentifiers(key_words)) {
                fixSingleStatement(parser, output, token);
            }
        }
    };
#endif

    std::string full_text = processText(src_full_text.data(), src_full_text.size(), proc_func);
    if (fix_file_endings) { full_text.push_back('\n'); }
    if (!output_file_name.empty() || full_text != src_full_text) {
        if (output_file_name.empty()) { output_file_name = input_file_name; }
        if (std::ofstream ofile(output_file_name); ofile) {
            ofile.write(full_text.data(), full_text.size());
        } else {
            std::cerr << "code-format: error: could not open output file `" << output_file_name << "`";
            return -1;
        }
    }
    return 0;
}
