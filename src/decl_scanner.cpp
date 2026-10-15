#include "decl_scanner.h"

#include <uxs/format.h>
#include <uxs/format_fs.h>  //NOLINT

#include <array>

namespace {
Parser::Token getNextToken(Parser& parser, std::string& id) {
    bool id_started = false;
    while (true) {
        auto tk = parser.parseNext();
        if (tk.type >= Parser::TokenType::kPreprocId) { continue; }
        if (tk.type == Parser::TokenType::kNsSep || tk.type == Parser::TokenType::kIdentifier) {
            if (!id_started) {
                id.clear();
                id_started = true;
            }
            if (tk.type == Parser::TokenType::kIdentifier) {
                id.append(tk.getTrimmedText());
                auto tk2 = parser.parseNext();
                if (tk2.type == Parser::TokenType::kNsSep) {
                    id += '/';
                    continue;
                }
                parser.revert(std::move(tk2));
            } else {
                id += '/';
                continue;
            }
        }
        return tk;
    }
}
}  // namespace

Namespace* Namespace::lookup(std::string_view name) {
    Namespace* found_ns = this;

    auto pos = name.find('/');
    auto subname = name;
    if (pos != std::string::npos) {
        if (pos == 0) { return nullptr; }
        subname = name.substr(0, pos);
        name.remove_prefix(pos + 1);
    }

    while (found_ns) {
        auto it = std::ranges::find_if(children, [subname](auto&& v) { return v->name == subname; });
        if (it != children.end()) { break; }
        found_ns = found_ns->parent;
    }

    if (!found_ns) { return nullptr; }

    while (pos != std::string::npos) {
        pos = name.find('/');
        subname = name;
        if (pos != std::string::npos) {
            subname = name.substr(0, pos);
            name.remove_prefix(pos + 1);
        }

        auto it = std::ranges::find_if(found_ns->children, [subname](auto&& v) { return v->name == subname; });
        if (it == found_ns->children.end()) { return nullptr; }
        found_ns = it->get();
    }

    return found_ns;
}

Namespace* Namespace::enter(std::string_view name, unsigned& pop_count) {
    Namespace* ns = this;
    auto pos = std::string::npos;
    pop_count = 0;

    do {
        pos = name.find('/');
        auto subname = name;
        if (pos != std::string::npos) {
            subname = name.substr(0, pos);
            name.remove_prefix(pos + 1);
        }

        ++pop_count;

        auto it = std::ranges::find_if(ns->children, [name](auto&& v) { return v->name == name; });
        if (it != ns->children.end()) { return it->get(); }
        ns = ns->children.emplace_back(std::make_unique<Namespace>(this, std::string(name))).get();
    } while (pos != std::string::npos);

    return ns;
}

void Namespace::addName(std::string_view name, DeclType decl_type, const Parser& parser) {
    const auto pos = name.rfind('/');
    Namespace* ns = this;
    if (pos != std::string::npos) {
        ns = lookup(name.substr(0, pos));
        name.remove_prefix(pos + 1);
    }
    if (ns) { ns->names.emplace(std::string{name}, DeclInfo{decl_type, parser.getFileName(), parser.getLn()}); }
}

std::string Namespace::getFullName() const {
    std::string full_name = name;
    const Namespace* ns = this;
    while (ns->parent) {
        full_name = ns->parent->name + "::" + full_name;
        ns = ns->parent;
    }
    return full_name;
}

bool scanForDeclarations(Parser& parser, Namespace& root) {
    std::string id;
    Namespace* current_ns = &root;
    std::vector<unsigned> ns_pop_counts;
    int func_decl_state = 0;
    bool reuse_token = false;

    int braces_level = 0;
    int angle_braces_level = 0;
    Parser::Token tk;
    ns_pop_counts.reserve(16);

    static constexpr std::array<std::string_view, 4> class_types = {"class", "struct", "union", "enum"};

    while (true) {
        if (!reuse_token) { tk = getNextToken(parser, id); }
        reuse_token = false;
        if (tk.type == Parser::TokenType::kEof) { break; }

        if (braces_level > 0) {
            braces_level = tk.trackLevel(braces_level, '{', '}');
            if (braces_level == 0) { func_decl_state = 0; }
        } else if (angle_braces_level > 0) {
            angle_braces_level = tk.trackLevel(angle_braces_level, '<', '>');
            if (angle_braces_level == 0) { func_decl_state = 0; }
        } else if (tk.isIdentifier("namespace")) {
            id = "()";
            while (true) {
                tk = getNextToken(parser, id);
                if (tk.isAnyOfSymbols(";{")) {
                    if (tk.isSymbol('{')) { current_ns = current_ns->enter(id, ns_pop_counts.emplace_back(0)); }
                    break;
                }
            }
        } else if (tk.isAnyOfIdentifiers(class_types)) {
            auto decl_type = DeclType::kClass;
            if (tk.isIdentifier("struct")) {
                decl_type = DeclType::kStruct;
            } else if (tk.isIdentifier("union")) {
                decl_type = DeclType::kUnion;
            } else if (tk.isIdentifier("enum")) {
                decl_type = DeclType::kEnum;
            }
            id.clear();
            while (true) {
                tk = getNextToken(parser, id);
                if (tk.isAnyOfSymbols(";{<:")) {
                    if (tk.isSymbol('{')) { ++braces_level; }
                    break;
                }
            }
            if (!id.empty()) { current_ns->addName(id, decl_type, parser); }
        } else if (tk.isIdentifier("template")) {
            tk = getNextToken(parser, id);
            if (tk.isSymbol('<')) {
                ++angle_braces_level;
            } else {
                reuse_token = true;
            }
        } else if (tk.type == Parser::TokenType::kIdentifier) {
            if (func_decl_state == 1) {
                tk = getNextToken(parser, id);
                if (tk.isSymbol('(')) {
                    if (!id.empty()) { current_ns->addName(id, DeclType::kFunction, parser); }
                    func_decl_state = 2;
                } else {
                    reuse_token = true;
                }
            } else if (func_decl_state == 0) {
                func_decl_state = 1;
            }
        } else if (tk.isSymbol('{')) {
            ++braces_level;
        } else if (tk.isSymbol('}')) {
            if (ns_pop_counts.empty()) { return false; }
            for (unsigned n = 0; n < ns_pop_counts.back(); ++n) { current_ns = current_ns->parent; }
            ns_pop_counts.pop_back();
        } else if (tk.isSymbol(';')) {
            func_decl_state = 0;
        } else if (tk.isSymbol('=')) {
            func_decl_state = 2;
        }
    }

    return true;
}

namespace {
void collectDeclarations(DeclList& lst, const Namespace& root) {
    const auto ns_name = root.getFullName();
    for (const auto& [name, info] : root.names) { lst.emplace_back(ns_name + "::" + name, info); }
    for (const auto& child : root.children) { collectDeclarations(lst, *child); }
}
void collectDeclarations(std::multimap<std::string, std::pair<std::string, DeclInfo>>& tbl, const Namespace& root) {
    const auto ns_name = root.getFullName();
    for (const auto& [name, info] : root.names) { tbl.emplace(name, std::make_pair(ns_name + "::" + name, info)); }
    for (const auto& child : root.children) { collectDeclarations(tbl, *child); }
}
}  // namespace

DeclList makeDeclarationList(const Namespace& root) {
    DeclList lst;
    collectDeclarations(lst, root);
    return lst;
}

DeclList makeConflictedDeclarationList(const Namespace& root) {
    std::multimap<std::string, std::pair<std::string, DeclInfo>> tbl;
    collectDeclarations(tbl, root);
    if (tbl.empty()) { return {}; }
    DeclList lst;
    auto it0 = tbl.begin();
    auto it = std::next(it0);
    while (it != tbl.end()) {
        if (it0->first != it->first) {
            if (it != std::next(it0)) {
                while (it0 != it) {
                    lst.emplace_back(it0->second);
                    ++it0;
                }
            } else {
                it0 = it;
            }
        }
        ++it;
    }
    if (it != std::next(it0)) {
        while (it0 != it) {
            lst.emplace_back(it0->second);
            ++it0;
        }
    }
    return lst;
}

void printDeclarationList(uxs::iobuf& out, const DeclList& lst) {
    for (const auto& [full_name, info] : lst) {
        switch (info.type) {
            case DeclType::kClass: uxs::print(out, "{} ", "class"); break;
            case DeclType::kStruct: uxs::print(out, "{} ", "struct"); break;
            case DeclType::kUnion: uxs::print(out, "{} ", "union"); break;
            case DeclType::kEnum: uxs::print(out, "{} ", "enum"); break;
            default: break;
        }
        uxs::println(out, "{}{} file:{} line:{}", full_name, info.type == DeclType::kFunction ? "()" : "",
                     info.file_path, info.line);
    }
}
