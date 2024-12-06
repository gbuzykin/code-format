#pragma once

#include "parser.h"

#include <set>

struct FormattingParameters {
    bool fix_file_endings = false;
    bool fix_single_statement = false;
    bool fix_id_naming = false;
    bool remove_already_included = false;
    std::vector<std::pair<std::filesystem::path, bool>> include_dirs;
};

struct FormattingContext {
    std::vector<std::filesystem::path> path_stack;
    std::vector<std::pair<std::filesystem::path, int>> included_files;
    std::set<std::filesystem::path> indirectly_included_files;
};

using TokenProcessFunc = std::function<void(Parser&, std::string&, const Parser::Token&)>;

std::string processText(std::string file_name, uxs::span<const char> text, const TokenProcessFunc& fn_token,
                        bool is_at_beg_of_line = true);

std::pair<std::string, bool> extractIncludePath(std::string_view text);

void fixIdNaming(Parser& parser, std::string& output, const Parser::Token& token);
void fixSingleStatement(Parser& parser, std::string& output, const Parser::Token& first_tkn);
