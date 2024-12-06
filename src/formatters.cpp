#include "formatters.h"

#include "print.h"

std::string processText(std::string file_name, std::span<const char> text, const FormattingParameters& params,
                        const TokenFunc& fn, TextProcFlags flags) {
    Parser parser(std::move(file_name), text, flags);
    Parser::Token token;
    std::string output;

    bool already_matched = false;
    unsigned skip_level = 0;

    output.reserve(text.size() + text.size() / 10);

    do {
        token = parser.parseNext();
        if (!parser.getFileName().empty() && token.line == 1 && token.pos == 1) { token.trimEmptyLines(); }
        fn(parser, token, skip_level, output);
        if (token.type == Parser::TokenType::kPreprocId) {
            auto id = token.getPreprocIdentifier();

            token = parser.parseNext();
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
                                   uxs::contains(params.definitions, token.getTrimmedText());
                    if (id == "ifndef" ? matched : !matched) { ++skip_level; }
                    already_matched = false;
                } else {
                    ++skip_level;
                }
            } else if (id == "elif") {
                if (!skip_level) {
                    ++skip_level, already_matched = true;
                } else if (skip_level == 1 && !already_matched && token.type == Parser::TokenType::kPreprocBody &&
                           uxs::contains(params.definitions, token.getTrimmedText())) {
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

void skipLine(Parser& parser, const Parser::Token& first_tkn, std::string& output) {
    if (first_tkn.isFirst()) {
        auto next = parser.parseNext();
        next.trimEmptyLines();
        next.line = 1, next.pos = 1;
        parser.revert(next);
    } else {
        output.append(first_tkn.getEmptyLines());
    }
}

void fixIdNaming(Parser& parser, const Parser::Token& token, const FormattingParameters& params, std::string& output) {
    if (token.type != Parser::TokenType::kIdentifier) {
        output.append(token.text);
        return;
    }

    auto id = token.getTrimmedText();
    std::string new_id{id};

    auto next = parser.parseNext();
    if (!next.isSymbol('(')) {
        if (id.size() > 1 &&
            (id[0] == '_' ||
             (!uxs::is_upper(id[0]) && !(id[0] == 'k' && uxs::is_upper(id[1])) /* Probably enum member */ &&
              !uxs::all_of(
                  id, [](char ch) { return ch == '_' || uxs::is_digit(ch) || uxs::is_lower(ch); }) /* No upper case */
              && !uxs::all_of(id, [](char ch) {
                     return ch == '_' || uxs::is_digit(ch) || uxs::is_upper(ch);
                 }) /* No lower case */))) {
            new_id.clear();
            bool is_member = false;
            if (id[0] == '_') {
                is_member = true;
                id = id.substr(1);
            }
            new_id.push_back(id[0]);
            for (auto it = id.begin() + 1; it != id.end(); ++it) {
                if ((uxs::is_digit(*it) || uxs::is_upper(*it)) && uxs::is_lower(*(it - 1))) { new_id.push_back('_'); }
                new_id.push_back(uxs::to_lower(*it));
            }
            if (is_member) { new_id.push_back('_'); }
        }
    } else {  // is a function
        if (id[0] == '_') {
            printWarning("{}:{}: underscored function name {}", parser.getFileName(), parser.getLn(), id);
        }
    }

    parser.revert(next);

    output.append(token.text.substr(0, token.ws_count));
    output.append(new_id);
}

bool fixPragmaOnce(Parser& parser, const Parser::Token& first_tkn, std::string& output) {
    std::string_view ext{parser.getFileName()};
    auto dot_pos = ext.rfind('.');
    ext = dot_pos != std::string::npos ? ext.substr(dot_pos + 1) : std::string_view{};
    bool is_header = ext.starts_with('h');

    auto next = parser.parseNext();

    if (is_header && first_tkn.isFirstSignificant() && first_tkn.isPreprocIdentifier("ifndef") &&
        next.type == Parser::TokenType::kPreprocBody) {
        auto defined = next.getFirstIdentifier();

        auto next2 = parser.parseNext();
        auto next3 = parser.parseNext();
        parser.revert(next3);
        parser.revert(next2);

        if (next2.isPreprocIdentifier("define") && next3.isPreprocBodyFirstId(defined)) {
            // C-style header protection
            parser.revert(next);
            return false;
        }
    }

    if (is_header && first_tkn.isFirstSignificant()) {
        if (!first_tkn.isFirst()) { output.append("\n\n"); }
        output.append("#pragma once\n\n");
    }

    if (first_tkn.isPreprocIdentifier("pragma") && next.isPreprocBodyFirstId("once")) {
        skipLine(parser, first_tkn, output);
        return true;
    }

    parser.revert(next);
    return false;
}

bool fixSingleStatement(Parser& parser, const Parser::Token& first_tkn, std::string& output) {
    static constexpr std::array<std::string_view, 4> key_words = {"if", "while", "for", "do"};
    if (!first_tkn.isAnyOfIdentifiers(key_words)) { return false; }

    output.append(first_tkn.text);

    Parser::Token token;
    bool is_else_block = false;

    do {
        token = parser.parseNext();
        if (!is_else_block && !first_tkn.isIdentifier("do")) {
            int level = -1;
            while (!token.isEof()) {
                output.append(token.text);
                if (level >= 0) {
                    level = token.trackLevel(level, '(', ')');
                } else if (token.isSymbol('(')) {
                    level = 1;
                }
                token = parser.parseNext();
                if (level == 0) { break; }
            }
        }

        std::vector<Parser::Token> comments;
        comments.reserve(16);
        while (token.isComment()) {
            comments.emplace_back(token);
            token = parser.parseNext();
        }

        if (!token.isEof()) { output.append(" {"); }
        for (const auto& comment : comments) { output.append(comment.text); }
        comments.clear();
        if (token.isEof()) { return true; }

        if (!token.isSymbol('{')) {
            bool make_nl = token.hasNewLine(), has_comments = false;
            if (!fixSingleStatement(parser, token, output)) {
                for (int level = 0; !token.isEof();) {
                    output.append(token.text);
                    if (level == 0 && token.isSymbol(';')) { break; }
                    level = token.trackLevel(level, '{', '}');
                    token = parser.parseNext();
                }
            }

            token = parser.parseNext();
            while (token.isComment() && !token.hasNewLine()) {
                has_comments = true;
                output.append(token.text);
                token = parser.parseNext();
            }

            output.append(make_nl || has_comments ? first_tkn.makeIndented("}") : " }");
        } else {
            token = parser.parseNext();
            for (int level = 1; !token.isEof();) {
                if (!fixSingleStatement(parser, token, output)) { output.append(token.text); }
                token = parser.parseNext();
                if (level == 0) { break; }
                level = token.trackLevel(level, '{', '}');
            }
        }

        bool has_comments = false;
        while (token.isComment()) {
            has_comments = true;
            output.append(token.text);
            token = parser.parseNext();
        }

        if (first_tkn.isIdentifier("do")) {
            if (token.isIdentifier("while")) {
                output.append(has_comments ? first_tkn.makeIndented("while") : " while");
                token = parser.parseNext();
            }
            for (int level = 0; !token.isEof();) {
                output.append(token.text);
                if (level == 0 && token.isSymbol(';')) { break; }
                level = token.trackLevel(level, '(', ')');
                token = parser.parseNext();
            }
            break;
        } else if (!is_else_block && first_tkn.isIdentifier("if")) {
            if (token.isIdentifier("else")) {
                output.append(has_comments ? first_tkn.makeIndented("else") : " else");
                token = parser.parseNext();
                while (token.isComment()) {
                    comments.emplace_back(token);
                    token = parser.parseNext();
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

    return true;
}
