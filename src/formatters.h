#pragma once

#include "parser.h"

#include <filesystem>
#include <functional>

struct FormattingParameters {
    bool fix_file_endings = false;
    bool fix_single_statement = false;
    bool fix_id_naming = false;
    bool remove_already_included = false;
    std::vector<std::pair<std::filesystem::path, bool>> include_dirs;
};

using TokenProcessFunc = std::function<void(Parser&, std::string&, const Parser::Token&)>;

std::string processText(uxs::span<const char> text, const TokenProcessFunc& fn_token, bool is_at_beg_of_line = true);

void fixSingleStatement(Parser& parser, std::string& output, const Parser::Token& first_tkn);
