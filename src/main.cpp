#include "formatters.h"
#include "print.h"

#include "uxs/cli/parser.h"
#include "uxs/io/filebuf.h"

#define XSTR(s) STR(s)
#define STR(s)  #s

namespace {

std::pair<std::filesystem::path, IncludePathType> findIncludePath(const std::filesystem::path& path,
                                                                  IncludeBrackets brackets,
                                                                  const FormattingParameters& params,
                                                                  const FormattingContext& ctx) {
    if (path.is_absolute()) {
        if (std::filesystem::exists(path)) { return std::make_pair(path.lexically_normal(), IncludePathType::kCustom); }
        return {};
    }
    if (brackets == IncludeBrackets::kDoubleQuotes) {
        for (const auto& dir : uxs::make_reverse_range(ctx.path_stack)) {
            auto path_cat = dir.parent_path() / path;
            if (std::filesystem::exists(path_cat)) {
                return std::make_pair((std::filesystem::current_path() / path_cat).lexically_normal(),
                                      IncludePathType::kCustom);
            }
        }
    }
    for (const auto& [dir, dir_type] : params.include_dirs) {
        auto path_cat = dir / path;
        if (std::filesystem::exists(path_cat)) {
            return std::make_pair((std::filesystem::current_path() / path_cat).lexically_normal(), dir_type);
        }
    }
    return {};
}

bool collectIndirectlyIncludedFiles(std::string_view file_name, const FormattingParameters& params,
                                    FormattingContext& ctx) {
    std::string text;
    if (uxs::filebuf ifile(ctx.path_stack.back().c_str(), "r"); ifile) {
        size_t file_sz = static_cast<size_t>(ifile.seek(0, uxs::seekdir::end));
        text.resize(file_sz);
        ifile.seek(0);
        text.resize(ifile.read(text));
    } else {
        return false;
    }

    auto fn = [&params, &ctx](Parser& parser, const Parser::Token& token, unsigned skip_level, std::string&) {
        if (skip_level || !token.isPreprocIdentifier("include")) { return; }

        auto next = parser.parseNext();
        if (next.type == Parser::TokenType::kPreprocBody) {
            auto [file_name, brackets] = extractIncludePath(next.getTrimmedText());
            auto [file_path, path_type] = findIncludePath(file_name, brackets, params, ctx);
            if (!file_path.empty()) {
                if (path_type == IncludePathType::kCustom) {
                    if (!uxs::find(ctx.path_stack, file_path).second) {
                        ctx.path_stack.emplace_back(file_path);
                        if (!collectIndirectlyIncludedFiles(file_name, params, ctx)) {
                            printWarning("{}:{}: could not open include file `{}`", parser.getFileName(),
                                         parser.getLn(), file_name);
                        }
                        ctx.path_stack.pop_back();
                    } else {
                        printWarning("{}:{}: recursively included file `{}`", parser.getFileName(), parser.getLn(),
                                     file_path);
                    }
                }
                if (ctx.path_stack.size() > 1) { ctx.indirectly_included_files.emplace(std::move(file_path)); }
            } else {
                printWarning("{}:{}: could not find included file `{}`", parser.getFileName(), parser.getLn(),
                             file_name);
            }
        } else {
            parser.revert(next);
        }
    };

    processText(std::string{file_name}, text, params, fn);

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool show_help = false, show_version = false;
    std::string input_file_name, output_file_name;
    FormattingParameters params;
    auto cli =
        uxs::cli::command(argv[0])
        << uxs::cli::overview(
               "This is a tool for enclosing single statements in braces (and other cosmetic fixes) in C and C++ code")
        << uxs::cli::value("file", input_file_name)
        << (uxs::cli::option({"-o"}) & uxs::cli::value("<file>", output_file_name)) % "Output file name."
        << uxs::cli::option({"--fix-file-ending"}).set(params.fix_file_ending) %
               "Change file ending to one new-line symbol."
        << uxs::cli::option({"--fix-single-statement"}).set(params.fix_single_statement) %
               "Enclose single-statement blocks in brackets,\n"
               "format `if`-`else if`-`else`-sequences."
        << uxs::cli::option({"--fix-id-naming"}).set(params.fix_id_naming) % "Fix identifier naming."
        << uxs::cli::option({"--fix-pragma-once"}).set(params.fix_pragma_once) % "Fix pragma once preproc command."
        << uxs::cli::option({"--remove-already-included"}).set(params.remove_already_included) %
               "Remove include directives for already included headers."
        << (uxs::cli::option({"-D"}) & uxs::cli::values("<defs>...", params.definitions)) % "Add definition."
        << (uxs::cli::option({"-I"}) & uxs::cli::basic_value_wrapper<char>("<dirs>...",
                                                                           [&params](std::string_view dir) {
                                                                               params.include_dirs.emplace_back(
                                                                                   dir, IncludePathType::kCustom);
                                                                               return true;
                                                                           })
                                           .multiple()) %
               "Add include directory."
        << (uxs::cli::option({"-IS"}) & uxs::cli::basic_value_wrapper<char>("<dirs>...",
                                                                            [&params](std::string_view dir) {
                                                                                params.include_dirs.emplace_back(
                                                                                    dir, IncludePathType::kSystem);
                                                                                return true;
                                                                            })
                                            .multiple()) %
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

    std::string full_text;
    if (uxs::filebuf ifile(input_file_name.c_str(), "r"); ifile) {
        size_t file_sz = static_cast<size_t>(ifile.seek(0, uxs::seekdir::end));
        full_text.resize(file_sz);
        ifile.seek(0);
        full_text.resize(ifile.read(full_text));
    } else {
        printError("could not open input file `{}`", input_file_name);
        return -1;
    }

    uxs::println("Processing: {}...", input_file_name);

    FormattingContext ctx;
    std::string src_full_text = full_text;

    ctx.path_stack.emplace_back((std::filesystem::current_path() / input_file_name).lexically_normal());

    if (params.remove_already_included) { collectIndirectlyIncludedFiles(input_file_name, params, ctx); }

    if (params.fix_id_naming) {
        full_text = processText(input_file_name, full_text, params,
                                [&params](Parser& parser, const Parser::Token& token, unsigned, std::string& output) {
                                    fixIdNaming(parser, token, params, output);
                                });
    }

    auto fn = [&params, &ctx](Parser& parser, const Parser::Token& token, unsigned skip_level, std::string& output) {
        static constexpr std::array<std::string_view, 9> type_names = {
            "kEof", "kSymbol", "kIdentifier", "kString", "kInteger", "kReal", "kPreprocId", "kPreprocBody", "kComment"};

        if (!skip_level) {
            printDebug(2, "token: {}, ws_count = {}: {:?}", type_names[static_cast<unsigned>(token.type)],
                       token.ws_count, token.getTrimmedText());
        }

        if (token.isEof() && params.fix_file_ending) { return; }

        if (params.fix_pragma_once && fixPragmaOnce(parser, token, output)) { return; }
        if (params.fix_single_statement && fixSingleStatement(parser, token, output)) { return; }

        if (token.isPreprocIdentifier("include")) {
            auto next = parser.parseNext();
            if (next.type == Parser::TokenType::kPreprocBody) {
                if (!skip_level) {
                    auto [file_name, brackets] = extractIncludePath(next.getTrimmedText());
                    auto [file_path, path_type] = findIncludePath(file_name, brackets, params, ctx);
                    if (!file_path.empty()) {
                        if (params.remove_already_included &&
                            (uxs::find_if(ctx.included_files, uxs::is_equal_to(file_path)).second ||
                             uxs::find(ctx.indirectly_included_files, file_path).second)) {
                            skipLine(parser, token, output);
                            return;
                        }
                        ctx.included_files.emplace_back(std::move(file_path), token.line);
                    }
                }
            }
            parser.revert(next);
        }
        output.append(token.text);
    };

    full_text = processText(input_file_name, full_text, params, fn);

    if (params.fix_file_ending) { full_text.push_back('\n'); }

    printDebug(1, "-------------- included files:");
    for (const auto& [file_path, ln] : ctx.included_files) {
        printDebug(1, "include:{}: {}", ln, file_path.generic_string());
    }
    printDebug(1, "-------------- indirectly included files:");
    for (const auto& file_path : ctx.indirectly_included_files) {
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
