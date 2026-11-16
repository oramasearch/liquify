// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "liquify/interactive_analysis.hpp"

namespace liquify {

struct ContextReference {
    std::string name;
    std::string signature;
    SourceLocation definition;
};

struct ContextBuildResult {
    std::string action;
    int exit{};
    std::string status;
    std::string why;
};

struct ContextDependencyEdge {
    std::string from;
    std::string to;
};

struct ContextFacts {
    std::filesystem::path path;
    SourceRange focus;
    std::string selected_source;
    std::optional<SyntaxNodeInfo> enclosing_definition;
    std::optional<SymbolRecord> enclosing_symbol;
    std::vector<ContextReference> referenced_definitions;
    std::vector<Diagnostic> file_diagnostics;
    std::vector<Diagnostic> translation_unit_diagnostics;
    std::optional<ContextBuildResult> build_result;
    std::vector<ContextDependencyEdge> dependency_edges;
    std::size_t whole_file_bytes{};
};

struct ContextBlock {
    std::string text;
    std::vector<std::string> omissions;
    std::size_t bytes{};
    std::size_t whole_file_bytes{};
};

class ContextBuilder {
public:
    explicit ContextBuilder(std::size_t byte_budget);

    [[nodiscard]] auto build(const ContextFacts& facts) const -> ContextBlock;
    [[nodiscard]] auto byte_budget() const -> std::size_t;

private:
    std::size_t byte_budget_;
};

} // namespace liquify
