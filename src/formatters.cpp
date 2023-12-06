#include "formatters.h"

#include <array>

std::string processText(uxs::span<const char> text, const TokenProcessFunc& fn_token, bool is_at_beg_of_line) {
    std::string output;
    Parser parser(text, is_at_beg_of_line);
    Parser::Token token;

    output.reserve(text.size() + text.size() / 10);
    do {
        parser.parseNext(token);
        if (token.type == Parser::TokenType::kPreprocBody) {
            output.append(processText(token.text, fn_token, false));
        } else {
            fn_token(parser, output, token);
        }
    } while (!token.isEof());
    return output;
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
