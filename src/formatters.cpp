#include "formatters.h"

std::string processText(std::string file_name, uxs::span<const char> text, const FormattingParameters& params,
                        const TokenFunc& fn, TextProcFlags flags) {
    Parser parser(std::move(file_name), text, flags);
    Parser::Token token;
    std::string output;

    bool already_matched = false;
    unsigned skip_level = 0;

    output.reserve(text.size() + text.size() / 10);

    do {
        parser.parseNext(token);
        fn(parser, token, skip_level, output);
        if (token.type == Parser::TokenType::kPreprocId) {
            auto id = token.getPreprocIdentifier();

            parser.parseNext(token);
            if (token.type != Parser::TokenType::kPreprocBody || id != "define") { parser.revert(token); }

            if (id == "define") {
                if (token.type == Parser::TokenType::kPreprocBody) {
                    output.append(processText(
                        "", token.text, params,
                        [skip_level, &fn](Parser& parser, const Parser::Token& token, unsigned, std::string& output) {
                            fn(parser, token, skip_level, output);
                        },
                        TextProcFlags::kNone));
                }
            } else if (id == "if" || id == "ifdef" || id == "ifndef") {
                if (!skip_level) {
                    bool matched = token.type == Parser::TokenType::kPreprocBody &&
                                   uxs::contains(params.definitions, uxs::trim_string(token.text));
                    if (id == "ifndef" ? matched : !matched) { ++skip_level; }
                    already_matched = false;
                } else {
                    ++skip_level;
                }
            } else if (id == "elif") {
                if (!skip_level) {
                    ++skip_level, already_matched = true;
                } else if (skip_level == 1 && !already_matched && token.type == Parser::TokenType::kPreprocBody &&
                           uxs::contains(params.definitions, uxs::trim_string(token.text))) {
                    skip_level = 0;
                }
            } else if (id == "else") {
                if (!skip_level) {
                    ++skip_level, already_matched = true;
                } else if (skip_level == 1 && !already_matched) {
                    skip_level = 0;
                }
            } else if (id == "endif") {
                if (skip_level) { --skip_level; }
            }
        }
    } while (!token.isEof());

    return output;
}

std::pair<std::string, IncludeBrackets> extractIncludePath(std::string_view text) {
    text = uxs::trim_string(text);
    if (text.size() < 2) { return {}; }
    IncludeBrackets brackets = IncludeBrackets::kDoubleQuotes;
    if (text.front() == '<' && text.back() == '>') {
        brackets = IncludeBrackets::kAngled;
    } else if (text.front() != '\"' || text.back() != '\"') {
        return {};
    }
    return std::make_pair(uxs::decode_escapes(text.substr(1, text.size() - 2), "\a\b\f\n\r\t\v\\\"", "abfnrtv\\\""),
                          brackets);
}

void fixIdNaming(Parser& parser, const Parser::Token& token, const FormattingParameters& params, std::string& output) {
    if (token.type != Parser::TokenType::kIdentifier) {
        output.append(token.text);
        return;
    }

    auto id = token.getTrimmedText();
    std::string new_id{id};
    output.append(token.text.substr(0, token.ws_count));
    output.append(new_id);
}

void fixSingleStatement(Parser& parser, const Parser::Token& first_tkn, std::string& output) {
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
                fixSingleStatement(parser, token, output);
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
                if (token.isAnyOfIdentifiers(key_words)) { fixSingleStatement(parser, token, output); }
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
