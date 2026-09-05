#include "liquify/context_builder.hpp"

#include <algorithm>
#include <format>
#include <map>
#include <string_view>

namespace liquify {
namespace {

struct ContextPart {
    std::string category;
    std::string text;
    bool fact{true};
};

auto range_text(const SourceRange& range) -> std::string {
    return std::format("{}:{}-{}:{} (bytes {}-{})",
                       range.start_line + 1,
                       range.start_column + 1,
                       range.end_line + 1,
                       range.end_column + 1,
                       range.start_byte,
                       range.end_byte);
}

auto location_text(const SourceLocation& location) -> std::string {
    return location.path.generic_string() + ":" + range_text(location.range);
}

auto diagnostic_text(const Diagnostic& diagnostic) -> std::string {
    return std::format("- [{}] {}: {}\n",
                       diagnostic_severity_name(diagnostic.severity),
                       location_text(diagnostic.location),
                       diagnostic.message);
}

auto omission_lines(const std::map<std::string, std::size_t>& counts) -> std::vector<std::string> {
    auto output = std::vector<std::string>{};
    for (const auto& [category, count] : counts) {
        if (count == 0) {
            continue;
        }
        output.push_back(std::format("{} ({} omitted)", category, count));
    }
    return output;
}

auto render_omissions(const std::vector<std::string>& omissions) -> std::string {
    auto output = std::string{"\n[omissions]\n"};
    if (omissions.empty()) {
        output += "- none\n";
        return output;
    }
    for (const auto& omission : omissions) {
        output += "- " + omission + "\n";
    }
    return output;
}

auto referenced_parts(const ContextFacts& facts) -> std::vector<ContextPart> {
    auto output = std::vector<ContextPart>{};
    output.reserve(facts.referenced_definitions.size());
    for (const auto& reference : facts.referenced_definitions) {
        output.push_back({"referenced definitions",
                          std::format("- {} | {} | {}\n",
                                      reference.name,
                                      reference.signature,
                                      location_text(reference.definition))});
    }
    return output;
}

auto diagnostic_parts(const ContextFacts& facts) -> std::vector<ContextPart> {
    auto output = std::vector<ContextPart>{};
    output.reserve(facts.file_diagnostics.size() + facts.translation_unit_diagnostics.size());
    for (const auto& diagnostic : facts.file_diagnostics) {
        output.push_back({"file diagnostics", diagnostic_text(diagnostic)});
    }
    for (const auto& diagnostic : facts.translation_unit_diagnostics) {
        output.push_back({"translation-unit diagnostics", diagnostic_text(diagnostic)});
    }
    return output;
}

auto dependency_parts(const ContextFacts& facts) -> std::vector<ContextPart> {
    auto output = std::vector<ContextPart>{};
    output.reserve(facts.dependency_edges.size());
    for (const auto& edge : facts.dependency_edges) {
        output.push_back(
            {"one-hop dependency edges", std::format("- {} -> {}\n", edge.from, edge.to)});
    }
    return output;
}

auto symbol_reference_parts(const SymbolRecord& symbol) -> std::vector<ContextPart> {
    auto output = std::vector<ContextPart>{};
    output.reserve(symbol.references.size());
    for (const auto& reference : symbol.references) {
        output.push_back({"enclosing symbol references", "- " + location_text(reference) + "\n"});
    }
    return output;
}

} // namespace

ContextBuilder::ContextBuilder(std::size_t byte_budget)
    : byte_budget_(std::max<std::size_t>(byte_budget, 512)) {}

auto ContextBuilder::build(const ContextFacts& facts) const -> ContextBlock {
    auto prefix = std::format("[liquify-context-v1]\n[path]\n{}\n[focus]\n{}\n",
                              facts.path.generic_string(),
                              range_text(facts.focus));
    auto parts = std::vector<ContextPart>{};
    parts.push_back({"selected source", "\n[selected-source]\n" + facts.selected_source + "\n"});
    if (facts.enclosing_definition) {
        parts.push_back({"enclosing definition",
                         "\n[enclosing-definition]\nkind: " + facts.enclosing_definition->kind +
                             "\nrange: " + range_text(facts.enclosing_definition->range) +
                             "\nsource:\n" + facts.enclosing_definition->spelling + "\n"});
    }
    if (facts.enclosing_symbol) {
        const auto& symbol = *facts.enclosing_symbol;
        parts.push_back(
            {"enclosing symbol record",
             std::format("\n[enclosing-symbol]\nname: {}\nsignature: {}\ndefinition: "
                         "{}\nreferences: {}\n",
                         symbol.name,
                         symbol.definition.declaration.signature,
                         location_text(SourceLocation{symbol.definition.path,
                                                      symbol.definition.declaration.range}),
                         symbol.references.size())});
        const auto symbol_references = symbol_reference_parts(symbol);
        if (!symbol_references.empty()) {
            parts.push_back(
                {"enclosing symbol references", "[enclosing-symbol-references]\n", false});
            parts.insert(parts.end(), symbol_references.begin(), symbol_references.end());
        }
    }
    const auto references = referenced_parts(facts);
    if (!references.empty()) {
        parts.push_back({"referenced definitions", "\n[referenced-definitions]\n", false});
        parts.insert(parts.end(), references.begin(), references.end());
    }
    const auto diagnostics = diagnostic_parts(facts);
    if (!diagnostics.empty()) {
        parts.push_back({"diagnostics", "\n[diagnostics]\n", false});
        parts.insert(parts.end(), diagnostics.begin(), diagnostics.end());
    }
    if (facts.build_result) {
        parts.push_back({"latest build result and why",
                         std::format("\n[latest-build-result]\naction: {}\nexit: {}\nstatus: "
                                     "{}\nwhy: {}\n",
                                     facts.build_result->action,
                                     facts.build_result->exit,
                                     facts.build_result->status,
                                     facts.build_result->why)});
    }
    const auto dependencies = dependency_parts(facts);
    if (!dependencies.empty()) {
        parts.push_back({"one-hop dependency edges", "\n[one-hop-dependencies]\n", false});
        parts.insert(parts.end(), dependencies.begin(), dependencies.end());
    }

    auto omitted = std::map<std::string, std::size_t>{};
    for (const auto& part : parts) {
        if (part.fact) {
            ++omitted[part.category];
        }
    }
    auto body = std::string{};
    for (const auto& part : parts) {
        auto candidate_counts = omitted;
        if (part.fact) {
            --candidate_counts[part.category];
        }
        const auto candidate_omissions = omission_lines(candidate_counts);
        const auto candidate = prefix + body + part.text + render_omissions(candidate_omissions);
        if (candidate.size() > byte_budget_) {
            continue;
        }
        body += part.text;
        omitted = std::move(candidate_counts);
    }

    auto omissions = omission_lines(omitted);
    auto text = prefix + body + render_omissions(omissions);
    if (text.size() > byte_budget_) {
        const auto omission_text = render_omissions(omissions);
        const auto available = byte_budget_ > omission_text.size()
                                   ? byte_budget_ - omission_text.size()
                                   : std::size_t{};
        prefix.resize(std::min(prefix.size(), available));
        text = prefix + omission_text;
        if (text.size() > byte_budget_) {
            text.resize(byte_budget_);
        }
    }
    const auto bytes = text.size();
    return ContextBlock{std::move(text), std::move(omissions), bytes, facts.whole_file_bytes};
}

auto ContextBuilder::byte_budget() const -> std::size_t {
    return byte_budget_;
}

} // namespace liquify
