#pragma once

#include "parser.h"

#include <uxs/io/iobuf.h>

#include <map>
#include <memory>

enum class DeclType { kFunction = 0, kClass, kStruct, kUnion, kEnum };

struct DeclInfo {
    DeclType type;
    std::string file_name;
    unsigned line;
};

struct Namespace {
    Namespace* parent = nullptr;
    std::string name;
    std::vector<std::unique_ptr<Namespace>> children;
    std::map<std::string, DeclInfo> names;

    Namespace* lookup(std::string_view name);
    Namespace* enter(std::string_view name, unsigned& pop_count);
    void addName(std::string_view name, DeclType decl_type, const Parser& parser);
    std::string getFullName() const;
};

using DeclList = std::vector<std::pair<std::string, DeclInfo>>;

bool scanForDeclarations(Parser& parser, Namespace& root);
DeclList makeDeclarationList(const Namespace& root);
DeclList makeConflictedDeclarationList(const Namespace& root);
void printDeclarationList(uxs::iobuf& out, const DeclList& lst);
