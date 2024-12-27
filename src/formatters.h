#pragma once

#include "parser.h"

#include <set>

enum class IncludePathType { kCustom = 0, kSystem };
enum class IncludeBrackets { kDoubleQuotes = 0, kAngled };

struct FormattingParameters {
    bool fix_file_endings = false;
    bool fix_single_statement = false;
    bool fix_id_naming = false;
    bool remove_already_included = false;
    std::vector<std::string> definitions;
    std::vector<std::pair<std::filesystem::path, IncludePathType>> include_dirs;
};

struct FormattingContext {
    std::vector<std::filesystem::path> path_stack;
    std::vector<std::pair<std::filesystem::path, int>> included_files;
    std::set<std::filesystem::path> indirectly_included_files;
};

using TokenFunc = std::function<void(Parser&, const Parser::Token&, unsigned, std::string&)>;

std::string processText(std::string file_name, uxs::span<const char> text, const FormattingParameters& params,
                        const TokenFunc& fn_token, TextProcFlags flags = TextProcFlags::kAtBegOfLine);

std::pair<std::string, IncludeBrackets> extractIncludePath(std::string_view text);

void fixIdNaming(Parser& parser, const Parser::Token& token, const FormattingParameters& params, std::string& output);
void fixSingleStatement(Parser& parser, const Parser::Token& first_tkn, std::string& output);
