#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "liquid/error.hpp"
#include "liquify/interactive_analysis.hpp"

namespace liquify {

namespace detail {
class StructuralRepository;
}

enum class StructuralEditOperation : std::uint8_t {
  replace_node = 1,
  insert_before = 2,
  insert_after = 3,
  delete_node = 4,
};

struct StructuralNodeTarget {
  std::string kind;
  SourceRange range;
  std::string fingerprint;

  auto operator==(const StructuralNodeTarget &) const -> bool = default;
};

struct StructuralEditTarget {
  std::filesystem::path path;
  StructuralNodeTarget node;

  auto operator==(const StructuralEditTarget &) const -> bool = default;
};

struct StructuralEdit {
  StructuralEditTarget target;
  StructuralEditOperation operation{StructuralEditOperation::replace_node};
  std::string text;

  auto operator==(const StructuralEdit &) const -> bool = default;
};

struct AppliedStructuralEdit {
  std::filesystem::path path;
  SourceRange range;

  auto operator==(const AppliedStructuralEdit &) const -> bool = default;
};

struct RefusedStructuralEdit {
  StructuralEditTarget target;
  std::string reason;

  auto operator==(const RefusedStructuralEdit &) const -> bool = default;
};

struct StructuralEditResult {
  std::vector<AppliedStructuralEdit> applied;
  std::vector<RefusedStructuralEdit> refused;

  auto operator==(const StructuralEditResult &) const -> bool = default;
};

struct RenamePreview {
  std::string symbol;
  std::vector<StructuralEdit> edits;
  std::vector<SourceLocation> unresolved;
};

struct ExtractFunctionPreview {
  std::vector<StructuralEdit> edits;
  std::vector<std::string> diagnostics;
};

[[nodiscard]] auto
encode_structural_edit_request(std::span<const StructuralEdit> edits)
    -> liquid::result<std::vector<std::byte>>;
[[nodiscard]] auto
decode_structural_edit_request(std::span<const std::byte> input)
    -> liquid::result<std::vector<StructuralEdit>>;
[[nodiscard]] auto
encode_structural_edit_result(const StructuralEditResult &result)
    -> liquid::result<std::vector<std::byte>>;
[[nodiscard]] auto
decode_structural_edit_result(std::span<const std::byte> input)
    -> liquid::result<StructuralEditResult>;

class StructuralEditor {
public:
  StructuralEditor(std::filesystem::path repository_path,
                   InteractiveAnalysis &analysis);

  [[nodiscard]] auto open_buffer(std::filesystem::path path,
                                 StubLanguage language, std::string source)
      -> liquid::result<void>;
  [[nodiscard]] auto ensure_buffer(const std::filesystem::path &path)
      -> liquid::result<void>;
  [[nodiscard]] auto apply_source_edit(const std::filesystem::path &path,
                                       const SourceEdit &edit)
      -> liquid::result<IncrementalParseReport>;
  [[nodiscard]] auto close_buffer(const std::filesystem::path &path)
      -> liquid::result<void>;

  [[nodiscard]] auto selection_chain(const std::filesystem::path &path,
                                     std::uint32_t byte)
      -> liquid::result<std::vector<SyntaxNodeInfo>>;
  [[nodiscard]] auto select_references(const std::filesystem::path &path,
                                       std::uint32_t byte)
      -> liquid::result<std::vector<SourceLocation>>;
  [[nodiscard]] auto
  make_edit(const std::filesystem::path &path, const SyntaxNodeInfo &node,
            StructuralEditOperation operation, std::string text)
      -> liquid::result<StructuralEdit>;
  [[nodiscard]] auto rename(const std::filesystem::path &path,
                            std::uint32_t byte, std::string new_name)
      -> liquid::result<RenamePreview>;
  [[nodiscard]] auto extract_function(const std::filesystem::path &path,
                                      SourceRange selection, std::string name)
      -> liquid::result<ExtractFunctionPreview>;
  [[nodiscard]] auto apply(std::span<const StructuralEdit> edits)
      -> liquid::result<StructuralEditResult>;
  [[nodiscard]] auto pending_drips() const -> std::uint64_t;

private:
  struct Buffer {
    std::filesystem::path path;
    StubLanguage language{};
    std::string source;
  };

  [[nodiscard]] auto source(const std::filesystem::path &path) const
      -> liquid::result<std::string_view>;
  [[nodiscard]] auto exact_node(const StructuralEditTarget &target)
      -> liquid::result<SyntaxNodeInfo>;

  std::filesystem::path repository_path_;
  InteractiveAnalysis *analysis_{};
  std::vector<Buffer> buffers_;
  std::shared_ptr<detail::StructuralRepository> repository_;
};

[[nodiscard]] auto
structural_edit_operation_name(StructuralEditOperation operation)
    -> std::string_view;

} // namespace liquify
