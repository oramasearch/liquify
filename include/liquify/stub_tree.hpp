#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/error.hpp"
#include "liquid/graph.hpp"
#include "liquid/hash.hpp"

namespace liquify {

enum class StubLanguage : std::uint8_t { c = 1, cpp = 2 };

enum class DeclarationKind : std::uint8_t {
  function = 1,
  type = 2,
  variable = 3,
  field = 4,
  namespace_ = 5,
};

struct SourceRange {
  std::uint32_t start_byte{};
  std::uint32_t end_byte{};
  std::uint32_t start_line{};
  std::uint32_t start_column{};
  std::uint32_t end_line{};
  std::uint32_t end_column{};

  auto operator==(const SourceRange &) const -> bool = default;
};

struct StubDeclaration {
  DeclarationKind kind{};
  std::string name;
  std::vector<std::string> modifiers;
  std::string signature;
  std::string documentation;
  SourceRange range;
};

struct StubTree {
  StubLanguage language{};
  liquid::digest blob;
  std::vector<StubDeclaration> declarations;
};

struct SymbolLocation {
  std::filesystem::path path;
  StubDeclaration declaration;
};

class StubResolver {
public:
  void add(std::filesystem::path path, StubTree tree);

  [[nodiscard]] auto resolve(std::string_view name) const
      -> std::vector<SymbolLocation>;

private:
  struct StubFile {
    std::filesystem::path path;
    StubTree tree;
  };

  std::vector<StubFile> files_;
};

class StubTrees {
public:
  StubTrees(StubTrees &&) noexcept;
  auto operator=(StubTrees &&) noexcept -> StubTrees &;
  StubTrees(const StubTrees &) = delete;
  auto operator=(const StubTrees &) -> StubTrees & = delete;
  ~StubTrees();

  [[nodiscard]] static auto open(std::filesystem::path state_directory)
      -> liquid::result<StubTrees>;

  [[nodiscard]] auto for_file(const std::filesystem::path &path)
      -> liquid::result<StubTree>;
  [[nodiscard]] auto for_source(StubLanguage language, std::string_view source)
      -> liquid::result<StubTree>;
  [[nodiscard]] auto flush() -> liquid::result<void>;
  [[nodiscard]] auto statistics() const -> liquid::graph_statistics;

private:
  explicit StubTrees(liquid::graph graph);

  liquid::graph graph_;
};

[[nodiscard]] auto stub_language_for_path(const std::filesystem::path &path)
    -> liquid::result<StubLanguage>;
[[nodiscard]] auto declaration_kind_name(DeclarationKind kind)
    -> std::string_view;

} // namespace liquify
