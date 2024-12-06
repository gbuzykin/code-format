#include "formatters.h"

#include "uxs/cli/parser.h"
#include "uxs/io/filebuf.h"

#include <set>

#define XSTR(s) STR(s)
#define STR(s)  #s

namespace {

std::pair<std::string, bool> extractIncludePath(std::string_view text) {
    text = uxs::trim_string(text);
    if (text.size() < 2) { return std::make_pair(std::string{}, false); }
    bool is_angled = false;
    if (text.front() == '<' && text.back() == '>') {
        is_angled = true;
    } else if (text.front() != '\"' || text.back() != '\"') {
        return std::make_pair(std::string{}, false);
    }
    return std::make_pair(uxs::decode_escapes(text.substr(1, text.size() - 2), "\a\b\f\n\r\t\v\\\"", "abfnrtv\\\""),
                          is_angled);
}

std::pair<std::filesystem::path, bool> findIncludePath(std::filesystem::path path, bool is_angled,
                                                       const FormattingParameters& fmt_params,
                                                       const std::vector<std::filesystem::path>& path_stack) {
    if (path.is_absolute()) {
        return std::make_pair(std::filesystem::exists(path) ? path.lexically_normal() : std::filesystem::path{}, false);
    }
    if (!is_angled) {
        for (const auto& dir : uxs::make_reverse_range(path_stack)) {
            auto path_cat = dir.parent_path() / path;
            if (std::filesystem::exists(path_cat)) {
                return std::make_pair((std::filesystem::current_path() / path_cat).lexically_normal(), false);
            }
        }
    }
    for (const auto& [dir, is_system] : fmt_params.include_dirs) {
        auto path_cat = dir / path;
        if (std::filesystem::exists(path_cat)) {
            return std::make_pair((std::filesystem::current_path() / path_cat).lexically_normal(), is_system);
        }
    }
    return std::make_pair(std::filesystem::path{}, false);
}

void collectIncludedFiles(std::vector<std::filesystem::path>& path_stack, const FormattingParameters& fmt_params,
                          std::set<std::filesystem::path>& indirectly_included_files) {
    std::string text;
    if (uxs::filebuf ifile(path_stack.back().c_str(), "r"); ifile) {
        size_t file_sz = static_cast<size_t>(ifile.seek(0, uxs::seekdir::end));
        text.resize(file_sz);
        ifile.seek(0);
        text.resize(ifile.read(text));
    } else {
        printWarning("could not open include file `{}`", path_stack.back());
    }

    Parser parser(text);
    Parser::Token token;
    unsigned if_level = 0;

    do {
        parser.parseNext(token);
        if (token.type == Parser::TokenType::kPreprocId) {
            auto id = token.getTrailingIdentifier();
            if (id == "include") {
                if (if_level == 0) {
                    parser.parseNext(token);
                    if (token.type == Parser::TokenType::kPreprocBody) {
                        auto [file_name, is_angled] = extractIncludePath(token.text);
                        auto [file_path, is_system] = findIncludePath(file_name, is_angled, fmt_params, path_stack);
                        if (!file_path.empty()) {
                            if (!is_system) {
                                if (uxs::find(path_stack, file_path).second) {
                                    printWarning("recursively included file `{}`", file_path);
                                    return;
                                }
                                path_stack.emplace_back(file_path);
                                collectIncludedFiles(path_stack, fmt_params, indirectly_included_files);
                                path_stack.pop_back();
                            }
                            if (path_stack.size() > 1) { indirectly_included_files.emplace(std::move(file_path)); }
                        } else {
                            printWarning("could not find included file `{}`", file_name);
                        }
                    } else {
                        parser.revert(token);
                    }
                }
            } else if (id == "if" || id == "ifdef" || id == "ifndef") {
                ++if_level;
            } else if (id == "endif") {
                --if_level;
            }
        }
    } while (!token.isEof());
}

}  // namespace

int main(int argc, char** argv) {
    bool show_help = false, show_version = false;
    std::string input_file_name, output_file_name;
    FormattingParameters fmt_params;
    auto cli =
        uxs::cli::command(argv[0])
        << uxs::cli::overview(
               "This is a tool for enclosing single statements in braces (and other cosmetic fixes) in C and C++ code")
        << uxs::cli::value("file", input_file_name)
        << (uxs::cli::option({"-o"}) & uxs::cli::value("<file>", output_file_name)) % "Output file name."
        << uxs::cli::option({"--fix-file-endings"}).set(fmt_params.fix_file_endings) %
               "Change file ending to one new-line symbol."
        << uxs::cli::option({"--fix-single-statement"}).set(fmt_params.fix_single_statement) %
               "Enclose single-statement blocks in brackets,\n"
               "format `if`-`else if`-`else`-sequences."
        << uxs::cli::option({"--fix-id-naming"}).set(fmt_params.fix_id_naming) % "Fix identifier naming."
        << uxs::cli::option({"--remove-already-included"}).set(fmt_params.remove_already_included) %
               "Remove include directives for already included headers."
        << (uxs::cli::option({"-I"}) & uxs::cli::basic_value_wrapper<char>("<dir>",
                                                                           [&fmt_params](std::string_view dir) {
                                                                               fmt_params.include_dirs.emplace_back(
                                                                                   dir, false);
                                                                               return true;
                                                                           })) %
               "Add include directory."
        << (uxs::cli::option({"-IS"}) & uxs::cli::basic_value_wrapper<char>("<dir>",
                                                                            [&fmt_params](std::string_view dir) {
                                                                                fmt_params.include_dirs.emplace_back(
                                                                                    dir, true);
                                                                                return true;
                                                                            })) %
               "Add system include directory."
        << (uxs::cli::option({"-d"}) & uxs::cli::value("<debug level>", g_debug_level)) % "Debug level."
        << uxs::cli::option({"-h", "--help"}).set(show_help) % "Display this information."
        << uxs::cli::option({"-V", "--version"}).set(show_version) % "Display version.";

    auto parse_result = cli->parse(argc, argv);
    if (show_help) {
        uxs::stdbuf::out.write(parse_result.node->get_command()->make_man_page(uxs::cli::text_coloring::colored));
        return 0;
    } else if (show_version) {
        uxs::println(uxs::stdbuf::out, "{}", XSTR(VERSION));
        return 0;
    } else if (parse_result.status != uxs::cli::parsing_status::ok) {
        switch (parse_result.status) {
            case uxs::cli::parsing_status::unknown_option: {
                printError("unknown command line option `{}`", argv[parse_result.argc_parsed]);
            } break;
            case uxs::cli::parsing_status::invalid_value: {
                if (parse_result.argc_parsed < argc) {
                    printError("invalid command line argument `{}`", argv[parse_result.argc_parsed]);
                } else {
                    printError("expected command line argument after `{}`", argv[parse_result.argc_parsed - 1]);
                }
            } break;
            case uxs::cli::parsing_status::unspecified_value: {
                if (input_file_name.empty()) { printError("no input file specified"); }
            } break;
            default: break;
        }
        return -1;
    }

    std::string src_full_text;
    if (uxs::filebuf ifile(input_file_name.c_str(), "r"); ifile) {
        size_t file_sz = static_cast<size_t>(ifile.seek(0, uxs::seekdir::end));
        src_full_text.resize(file_sz);
        ifile.seek(0);
        src_full_text.resize(ifile.read(src_full_text));
    } else {
        printError("could not open input file `{}`", input_file_name);
        return -1;
    }

    uxs::println("Processing: {}...", input_file_name);

    std::vector<std::filesystem::path> path_stack;
    std::vector<std::pair<std::filesystem::path, int>> included_files;
    std::set<std::filesystem::path> indirectly_included_files;

    path_stack.emplace_back((std::filesystem::current_path() / input_file_name).lexically_normal());

    if (fmt_params.remove_already_included) { collectIncludedFiles(path_stack, fmt_params, indirectly_included_files); }

    unsigned if_level = 0;

    auto fn_token = [&fmt_params, &path_stack, &included_files, &indirectly_included_files, &if_level](
                        Parser& parser, std::string& output, const Parser::Token& token) {
        static constexpr std::array<std::string_view, 9> type_names = {
            "kEof", "kSymbol", "kIdentifier", "kString", "kInteger", "kReal", "kPreprocId", "kPreprocBody", "kComment"};
        printDebug(2, "token: {}, ws_count = {}: {:?}", type_names[static_cast<unsigned>(token.type)], token.ws_count,
                   token.getTrimmedText());
        if (token.type == Parser::TokenType::kPreprocId) {
            auto id = token.getTrailingIdentifier();
            if (id != "define") {
                Parser::Token next;
                parser.parseNext(next);
                if (next.type == Parser::TokenType::kPreprocBody) {
                    printDebug(2, "preproc body: {}", next.text);
                    if (id == "include") {
                        auto [file_name, is_angled] = extractIncludePath(next.text);
                        auto [file_path, is_system] = findIncludePath(file_name, is_angled, fmt_params, path_stack);
                        if (!file_path.empty()) {
                            if (fmt_params.remove_already_included &&
                                (uxs::find_if(included_files, uxs::is_equal_to(file_path)).second ||
                                 uxs::find(indirectly_included_files, file_path).second)) {
                                return;
                            }
                            if (if_level == 0) { included_files.emplace_back(std::move(file_path), token.line); }
                        }
                    } else if (id == "if" || id == "ifdef" || id == "ifndef") {
                        ++if_level;
                    } else if (id == "endif") {
                        --if_level;
                    }
                    output.append(token.text);
                    output.append(next.text);
                } else {
                    output.append(token.text);
                    parser.revert(next);
                }
            } else {
                output.append(token.text);
            }
        } else if (!token.isEof() || !fmt_params.fix_file_endings) {
            static constexpr std::array<std::string_view, 5> key_words = {"if", "while", "for", "do"};
            output.append(token.text);
            if (fmt_params.fix_single_statement && token.isAnyOfIdentifiers(key_words)) {
                fixSingleStatement(parser, output, token);
            }
        }
    };

    std::string full_text = processText(src_full_text, fn_token);

    if (fmt_params.fix_id_naming) {
        auto fn_rename = [](Parser& parser, std::string& output, const Parser::Token& token) {
            if (token.type == Parser::TokenType::kIdentifier) {
                auto id = token.getTrimmedText();
                std::string new_id{id};
                output.append(token.text.substr(0, token.ws_count));
                output.append(new_id);
                return;
            }
            output.append(token.text);
        };
        full_text = processText(full_text, fn_rename);
    }

    if (fmt_params.fix_file_endings) { full_text.push_back('\n'); }

    printDebug(1, "-------------- included files:");
    for (const auto& [file_path, ln] : included_files) {
        printDebug(1, "include:{}: {}", ln, file_path.generic_string());
    }
    printDebug(1, "-------------- indirectly included files:");
    for (const auto& file_path : indirectly_included_files) {
        printDebug(1, "include: {}", file_path.generic_string());
    }

    if (!output_file_name.empty() || full_text != src_full_text) {
        if (output_file_name.empty()) { output_file_name = input_file_name; }
        if (uxs::filebuf ofile(output_file_name.c_str(), "w"); ofile) {
            ofile.write(full_text);
        } else {
            printError("could not open output file `{}`", output_file_name);
            return -1;
        }
    }
    return 0;
}
