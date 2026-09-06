#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/error.hpp"
#include "liquid/graph.hpp"
#include "liquify/stub_tree.hpp"

namespace liquify {

class PluginHost;

enum class InteractiveQueryKind : std::uint8_t {
  node_at_position,
  enclosing_definition,
  go_to_definition,
  find_references,
  hover,
  completion,
  diagnostics,
};

struct SyntaxNodeInfo {
  std::string kind;
  std::string spelling;
  std::string leading_trivia;
  SourceRange range;
  bool has_error{};
};

struct SyntaxDeclarationInfo {
  StubDeclaration declaration;
  SourceRange scope;
  std::uint32_t scope_depth{};
};

struct SourceLocation {
  std::filesystem::path path;
  SourceRange range;

  auto operator==(const SourceLocation &) const -> bool = default;
};

struct ReferenceLocation {
  SourceLocation location;
  bool is_definition{};
};

struct HoverInfo {
  std::string signature;
  std::string documentation;
  SymbolLocation definition;
};

struct CompletionCandidate {
  std::string name;
  DeclarationKind kind{};
  std::string signature;
  std::filesystem::path path;
  std::uint32_t rank{};
};

enum class DiagnosticSeverity : std::uint8_t {
  note = 1,
  warning = 2,
  error = 3,
};

struct Diagnostic {
  SourceLocation location;
  DiagnosticSeverity severity{};
  std::string message;
};

struct SymbolRecord {
  std::string name;
  SymbolLocation definition;
  std::vector<SourceLocation> references;
};

struct ReferencedDefinition {
  std::string name;
  std::string signature;
  SymbolLocation definition;
};

struct DefinitionContext {
  std::optional<SyntaxNodeInfo> enclosing_definition;
  std::optional<SymbolRecord> enclosing_symbol;
  std::vector<ReferencedDefinition> referenced_definitions;
};

struct SourceEdit {
  std::uint32_t start_byte{};
  std::uint32_t old_end_byte{};
  std::string text;
};

struct IncrementalParseReport {
  SourceRange edited_range;
  std::vector<SourceRange> changed_ranges;
  std::vector<SourceRange> reparsed_leaf_ranges;
  std::uint64_t reused_leaf_nodes{};
  std::uint64_t total_leaf_nodes{};
};

struct InteractiveLatency {
  std::uint32_t samples{};
  double go_to_definition_p95_ms{};
  double completion_p95_ms{};
};

class InteractiveAnalysis {
public:
  InteractiveAnalysis(InteractiveAnalysis &&) noexcept;
  auto operator=(InteractiveAnalysis &&) noexcept -> InteractiveAnalysis &;
  InteractiveAnalysis(const InteractiveAnalysis &) = delete;
  auto operator=(const InteractiveAnalysis &) -> InteractiveAnalysis & = delete;
  ~InteractiveAnalysis();

  [[nodiscard]] static auto open(std::filesystem::path state_directory)
      -> liquid::result<InteractiveAnalysis>;
  [[nodiscard]] static auto open(std::filesystem::path state_directory,
                                 std::shared_ptr<PluginHost> plugins)
      -> liquid::result<InteractiveAnalysis>;

  [[nodiscard]] auto open_buffer(std::filesystem::path path,
                                 StubLanguage language, std::string source)
      -> liquid::result<void>;
  [[nodiscard]] auto apply_edit(const std::filesystem::path &path,
                                SourceEdit edit)
      -> liquid::result<IncrementalParseReport>;
  [[nodiscard]] auto close_buffer(const std::filesystem::path &path)
      -> liquid::result<void>;
  [[nodiscard]] auto add_stub(std::filesystem::path path, StubTree tree)
      -> liquid::result<void>;
  [[nodiscard]] auto set_symbol_records(std::vector<SymbolRecord> records)
      -> liquid::result<void>;
  [[nodiscard]] auto set_build_diagnostics(
      std::filesystem::path buffer_path, std::filesystem::path translation_unit,
      std::vector<Diagnostic> diagnostics) -> liquid::result<void>;

  [[nodiscard]] auto node_at_position(const std::filesystem::path &path,
                                      std::uint32_t byte)
      -> liquid::result<std::optional<SyntaxNodeInfo>>;
  [[nodiscard]] auto syntax_ancestors(const std::filesystem::path &path,
                                      std::uint32_t byte)
      -> liquid::result<std::vector<SyntaxNodeInfo>>;
  [[nodiscard]] auto syntax_nodes(const std::filesystem::path &path)
      -> liquid::result<std::vector<SyntaxNodeInfo>>;
  [[nodiscard]] auto syntax_declarations(const std::filesystem::path &path)
      -> liquid::result<std::vector<SyntaxDeclarationInfo>>;
  [[nodiscard]] auto enclosing_definition(const std::filesystem::path &path,
                                          std::uint32_t byte)
      -> liquid::result<std::optional<SyntaxNodeInfo>>;
  [[nodiscard]] auto go_to_definition(const std::filesystem::path &path,
                                      std::uint32_t byte)
      -> liquid::result<std::optional<SymbolLocation>>;
  [[nodiscard]] auto find_references(const std::filesystem::path &path,
                                     std::uint32_t byte)
      -> liquid::result<std::vector<ReferenceLocation>>;
  [[nodiscard]] auto hover(const std::filesystem::path &path,
                           std::uint32_t byte)
      -> liquid::result<std::optional<HoverInfo>>;
  [[nodiscard]] auto completion(const std::filesystem::path &path,
                                std::uint32_t byte)
      -> liquid::result<std::vector<CompletionCandidate>>;
  [[nodiscard]] auto diagnostics(const std::filesystem::path &path)
      -> liquid::result<std::vector<Diagnostic>>;
  [[nodiscard]] auto definition_context(const std::filesystem::path &path,
                                        std::uint32_t byte)
      -> liquid::result<DefinitionContext>;
  [[nodiscard]] auto plugin_query(std::string_view name,
                                  std::span<const std::byte> arguments_cbor)
      -> liquid::result<std::vector<std::byte>>;
  [[nodiscard]] auto measure_latency(const std::filesystem::path &path,
                                     std::span<const std::uint32_t> positions)
      -> liquid::result<InteractiveLatency>;

  [[nodiscard]] auto flush() -> liquid::result<void>;
  [[nodiscard]] auto statistics() const -> liquid::graph_statistics;

private:
  struct Impl;
  explicit InteractiveAnalysis(std::unique_ptr<Impl> implementation);

  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] auto interactive_query_kind_name(InteractiveQueryKind kind)
    -> std::string_view;
[[nodiscard]] auto diagnostic_severity_name(DiagnosticSeverity severity)
    -> std::string_view;

} // namespace liquify
