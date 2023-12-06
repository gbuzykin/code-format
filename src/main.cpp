#include "formatters.h"

#include "uxs/cli/parser.h"
#include "uxs/format.h"
#include "uxs/io/filebuf.h"
#include "uxs/stringalg.h"

#include <array>

#define XSTR(s) STR(s)
#define STR(s)  #s

unsigned g_debug_level = 0;

template<typename... Args>
void printError(uxs::format_string<Args...> fmt, const Args&... args) {
    std::string msg("\033[1;37mcode-format: \033[0;31merror: \033[0m");
    msg += fmt.get();
    uxs::vprint(uxs::stdbuf::err, msg, uxs::make_format_args(args...)).endl();
}

template<typename... Args>
void printWarning(uxs::format_string<Args...> fmt, const Args&... args) {
    std::string msg("\033[1;37mcode-format: \033[0;35mwarning: \033[0m");
    msg += fmt.get();
    uxs::vprint(uxs::stdbuf::out, msg, uxs::make_format_args(args...)).endl();
}

template<typename... Args>
void printDebug(unsigned level, uxs::format_string<Args...> fmt, const Args&... args) {
    if (g_debug_level < level) { return; }
    std::string msg("\033[1;37mcode-format: \033[0;33mdebug: \033[0m");
    msg += fmt.get();
    uxs::vprint(uxs::stdbuf::out, msg, uxs::make_format_args(args...)).endl();
}

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
        << (uxs::cli::option({"-d"}) & uxs::cli::value("<debug level>", g_debug_level)) % "Debug level."
        << uxs::cli::option({"-h", "--help"}).set(show_help) % "Display this information."
        << uxs::cli::option({"-V", "--version"}).set(show_version) % "Display version.";

    auto parse_result = cli->parse(argc, argv);
    if (show_help) {
        for (auto const* node = parse_result.node; node; node = node->get_parent()) {
            if (node->get_type() == uxs::cli::node_type::command) {
                uxs::stdbuf::out.write(static_cast<const uxs::cli::basic_command<char>&>(*node).make_man_page(true));
                break;
            }
        }
        return 0;
    } else if (show_version) {
        uxs::println(uxs::stdbuf::out, "{}", XSTR(VERSION));
        return 0;
    } else if (parse_result.status != uxs::cli::parsing_status::ok) {
        switch (parse_result.status) {
            case uxs::cli::parsing_status::unknown_option: {
                printError("unknown command line option `{}`", argv[parse_result.arg_count]);
            } break;
            case uxs::cli::parsing_status::invalid_value: {
                if (parse_result.arg_count < argc) {
                    printError("invalid command line argument `{}`", argv[parse_result.arg_count]);
                } else {
                    printError("expected command line argument after `{}`", argv[parse_result.arg_count - 1]);
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

    auto fn_token = [&fmt_params](Parser& parser, std::string& output, const Parser::Token& token) {
        static constexpr std::array<std::string_view, 9> type_names = {
            "kEof", "kSymbol", "kIdentifier", "kString", "kInteger", "kReal", "kPreprocId", "kPreprocBody", "kComment"};
        printDebug(2, "token: {}, ws_count = {}: {}", type_names[static_cast<unsigned>(token.type)], token.ws_count,
                   token.type == Parser::TokenType::kString ? uxs::make_quoted_string(token.getTrimmedText()) :
                                                              token.getTrimmedText());
        if (token.type == Parser::TokenType::kPreprocId) {
            auto id = token.getTrailingIdentifier();
            output.append(token.text);
            if (id != "define") {
                Parser::Token next;
                parser.parseNext(next);
                if (next.type == Parser::TokenType::kPreprocBody) {
                    printDebug(2, "preproc body: {}", next.text);
                    output.append(next.text);
                } else {
                    parser.revert(next);
                }
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
    if (fmt_params.fix_file_endings) { full_text.push_back('\n'); }

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
