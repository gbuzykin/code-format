#pragma once

#include "parser.h"

#include <functional>

struct FormattingParameters {
    bool fix_file_endings = false;
    bool fix_single_statement = false;
};

using TokenProcessFunc = std::function<void(Parser&, std::string&, const Parser::Token&)>;

std::string processText(uxs::span<const char> text, const TokenProcessFunc& fn_token, bool is_at_beg_of_line = true);

void fixSingleStatement(Parser& parser, std::string& output, const Parser::Token& first_tkn);
