#include "decl_scanner.h"
#include "formatters.h"
#include "print.h"

#include <uxs/cli/parser.h>
#include <uxs/io/filebuf.h>

#define XSTR(s) STR(s)
#define STR(s)  #s

namespace {

std::filesystem::path findIncludePath(const std::filesystem::path& path, IncludeBrackets brackets,
                                      const FormattingParameters& params, const FormattingContext& ctx) {
    if (path.empty()) { return {}; }
    if (path.is_absolute()) {
        if (std::filesystem::exists(path)) { return path.lexically_normal(); }
        return {};
    }
    if (brackets == IncludeBrackets::kDoubleQuotes) {
        for (const auto& dir : est::make_reverse_range(ctx.path_stack)) {
            auto path_cat = dir.parent_path() / path;
            if (std::filesystem::exists(path_cat)) {
                return (std::filesystem::current_path() / path_cat).lexically_normal();
            }
        }
    }
    for (const auto& dir : params.include_dirs) {
        auto path_cat = dir / path;
        if (std::filesystem::exists(path_cat)) {
            return (std::filesystem::current_path() / path_cat).lexically_normal();
        }
    }
    return {};
}

bool loadTextFile(const std::filesystem::path& file_path, std::string& text) {
    if (uxs::filebuf ifile(file_path.c_str(), "r"); ifile) {
        std::size_t file_sz = static_cast<std::size_t>(ifile.seek(0, uxs::seekdir::end));
        text.resize(file_sz);
        ifile.seek(0);
        text.resize(ifile.read(text));
        return true;
    }
    return false;
}

bool collectIndirectlyIncludedFiles(const std::filesystem::path& file_path, const FormattingParameters& params,
                                    FormattingContext& ctx) {
    std::string text;
    if (!loadTextFile(ctx.path_stack.back(), text)) { return false; }

    auto fn = [&params, &ctx](Parser& parser, const Parser::Token& token, unsigned skip_level, std::string&) {
        if (skip_level) { return false; }

        if (token.isPreprocIdentifier("pragma")) {
            auto next = parser.parseNext();
            if (next.type == Parser::TokenType::kPreprocBody) {
                if (next.getTrimmedText() == "once" && !ctx.once_included_files.emplace(ctx.path_stack.back()).second) {
                    return true;
                }
            } else {
                parser.revert(next);
            }
        } else if (!token.isPreprocIdentifier("include")) {
            return false;
        }

        auto next = parser.parseNext();
        if (next.type == Parser::TokenType::kPreprocBody) {
            auto [file_path, brackets] = extractIncludePath(next.getTrimmedText());
            auto found_file_path = findIncludePath(file_path, brackets, params, ctx);
            if (!found_file_path.empty()) {
                if (brackets == IncludeBrackets::kDoubleQuotes) {
                    ctx.path_stack.emplace_back(found_file_path);
                    if (!collectIndirectlyIncludedFiles(file_path, params, ctx)) {
                        printWarning("{}:{}: could not open include file `{}`", parser.getFileName(), parser.getLn(),
                                     file_path);
                    }
                    ctx.path_stack.pop_back();
                }
                if (ctx.path_stack.size() > 1) { ctx.indirectly_included_files.emplace(std::move(found_file_path)); }
            } else {
                printWarning("{}:{}: could not find included file `{}`", parser.getFileName(), parser.getLn(),
                             file_path);
            }
        } else {
            parser.revert(next);
        }

        return false;
    };

    processText(file_path, text, ctx, fn);

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool show_help = false, show_version = false;
    std::vector<std::filesystem::path> input_file_paths;
    std::filesystem::path output_file_path;

    FormattingParameters params;

    const auto string_to_path = [](const auto& sval, auto& v) {
        v = uxs::utf_native_path_adapter{}(sval);
        return true;
    };

    auto cli = uxs::cli::command(argv[0])
               << uxs::cli::overview("This is a tool to automate cosmetic fixes in C and C++ code")
               << uxs::cli::values("<files>", input_file_paths, string_to_path)
               << (uxs::cli::option({"-o"}) & uxs::cli::value("<file>", output_file_path, string_to_path)) %
                      "Output file name."
               << uxs::cli::option({"--fix-file-ending"}).set(params.fix_file_ending) %
                      "Change file ending to one new-line symbol."
               << uxs::cli::option({"--fix-single-statement"}).set(params.fix_single_statement) %
                      "Enclose single-statement blocks in brackets,\n"
                      "format `if`-`else if`-`else`-sequences."
               << uxs::cli::option({"--fix-id-naming"}).set(params.fix_id_naming) % "Fix identifier naming."
               << uxs::cli::option({"--fix-pragma-once"}).set(params.fix_pragma_once) %
                      "Fix pragma once preproc command."
               << uxs::cli::option({"--remove-already-included"}).set(params.remove_already_included) %
                      "Remove include directives for already included headers."
               << uxs::cli::option({"--list-all-declarations"}).set(params.list_all_declarations) %
                      "List all file declarations."
               << (uxs::cli::option({"-D"}) & uxs::cli::values("<defs>...", params.definitions)) % "Add definition."
               << (uxs::cli::option({"-I"}) & uxs::cli::values("<dirs>...", params.include_dirs, string_to_path)) %
                      "Add include directory."
               << (uxs::cli::option({"-d"}) & uxs::cli::value("<debug level>", g_debug_level)) % "Debug level."
               << uxs::cli::option({"-h", "--help"}).set(show_help) % "Display this information."
               << uxs::cli::option({"-V", "--version"}).set(show_version) % "Display version.";

    auto parse_result = cli->parse(argc, argv);
    if (show_help) {
        uxs::stdbuf::out().write(parse_result.node->get_command()->make_man_page(uxs::cli::text_coloring::colored));
        return 0;
    } else if (show_version) {
        uxs::println(uxs::stdbuf::out(), "{}", XSTR(VERSION));
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
            default: break;
        }
        return -1;
    }

    if (input_file_paths.empty()) {
        printError("no input file specified");
        return -1;
    }

    if (params.list_all_declarations) {
        Namespace root;

        for (const auto& input_file_path : input_file_paths) {
            std::string full_text;
            if (!loadTextFile(input_file_path, full_text)) {
                printError("could not open input file `{}`", input_file_path);
                return -1;
            }

            uxs::println("Processing: {}...", input_file_path);

            Parser parser(input_file_path, full_text);
            if (!scanForDeclarations(parser, root)) {
                printError("error scanning file `{}`", input_file_path);
                return -1;
            }
        }

        const auto decl_list = makeConflictedDeclarationList(root);

        if (uxs::filebuf ofile(output_file_path.c_str(), "w"); ofile) {
            printDeclarationList(ofile, decl_list);
        } else {
            printError("could not open output file `{}`", output_file_path);
            return -1;
        }
        return 0;
    }

    const auto input_file_path = input_file_paths[0];

    std::string full_text;
    if (!loadTextFile(input_file_path, full_text)) {
        printError("could not open input file `{}`", input_file_path);
        return -1;
    }

    uxs::println("Processing: {}...", input_file_path);

    FormattingContext ctx;
    std::string src_full_text = full_text;

    ctx.path_stack.emplace_back((std::filesystem::current_path() / input_file_path).lexically_normal());

    if (params.remove_already_included) {
        ctx.definitions = params.definitions;
        collectIndirectlyIncludedFiles(input_file_path, params, ctx);
    }

    if (params.fix_id_naming) {
        ctx.definitions = params.definitions;
        full_text = processText(input_file_path, full_text, ctx,
                                [&params](Parser& parser, const Parser::Token& token, unsigned, std::string& output) {
                                    fixIdNaming(parser, token, params, output);
                                    return false;
                                });
    }

    auto fn = [&params, &ctx](Parser& parser, const Parser::Token& token, unsigned skip_level, std::string& output) {
        static constexpr std::array<std::string_view, 9> type_names = {
            "kEof", "kSymbol", "kIdentifier", "kString", "kInteger", "kReal", "kPreprocId", "kPreprocBody", "kComment"};

        if (!skip_level) {
            printDebug(2, "token: {}, ws_count = {}: {:?}", type_names[static_cast<unsigned>(token.type)],
                       token.ws_count, token.getTrimmedText());
        }

        if (token.isEof() && params.fix_file_ending) { return false; }

        if (params.fix_pragma_once && fixPragmaOnce(parser, token, output)) { return false; }
        if (params.fix_single_statement && fixSingleStatement(parser, token, output)) { return false; }

        if (token.isPreprocIdentifier("include")) {
            auto next = parser.parseNext();
            if (next.type == Parser::TokenType::kPreprocBody) {
                if (!skip_level) {
                    auto [file_path, brackets] = extractIncludePath(next.getTrimmedText());
                    auto found_file_path = findIncludePath(file_path, brackets, params, ctx);
                    if (!found_file_path.empty()) {
                        if (params.remove_already_included &&
                            (std::ranges::any_of(ctx.included_files,
                                                 [&found_file_path](auto&& v) { return v.first == found_file_path; }) ||
                             std::ranges::any_of(ctx.indirectly_included_files,
                                                 [&found_file_path](auto&& v) { return v == found_file_path; }))) {
                            skipLine(parser, token, output);
                            return false;
                        }
                        ctx.included_files.emplace_back(std::move(found_file_path), token.line);
                    }
                }
            }
            parser.revert(next);
        }

        output.append(token.text);
        return false;
    };

    ctx.definitions = params.definitions;
    full_text = processText(input_file_path, full_text, ctx, fn);

    if (params.fix_file_ending) { full_text.push_back('\n'); }

    printDebug(1, "-------------- included files:");
    for (const auto& [file_path, ln] : ctx.included_files) {
        printDebug(1, "include:{}: {}", ln, file_path.generic_string());
    }
    printDebug(1, "-------------- indirectly included files:");
    for (const auto& file_path : ctx.indirectly_included_files) {
        printDebug(1, "include: {}", file_path.generic_string());
    }

    if (!output_file_path.empty() || full_text != src_full_text) {
        if (output_file_path.empty()) { output_file_path = input_file_path; }
        if (uxs::filebuf ofile(output_file_path.c_str(), "w"); ofile) {
            ofile.write(full_text);
        } else {
            printError("could not open output file `{}`", output_file_path);
            return -1;
        }
    }
    return 0;
}
