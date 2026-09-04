#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "liquify/stub_tree.hpp"

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("liquify-stub-tree-" + std::to_string(suffix));
    std::filesystem::remove_all(path_);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  auto operator=(const TemporaryDirectory &) -> TemporaryDirectory & = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path & {
    return path_;
  }

private:
  std::filesystem::path path_;
};

auto fixture(std::string_view name) -> std::filesystem::path {
  return std::filesystem::path(LIQUIFY_SOURCE_DIR) / "tests/fixtures/stubs" /
         name;
}

auto declaration_names(const liquify::StubTree &tree)
    -> std::vector<std::string> {
  auto names = std::vector<std::string>{};
  for (const auto &declaration : tree.declarations) {
    names.push_back(declaration.name);
  }
  return names;
}

TEST(StubTrees, CFixtureHasExpectedDeclarations) {
  TemporaryDirectory state;
  auto trees = liquify::StubTrees::open(state.path());
  ASSERT_TRUE(trees) << trees.error().message;

  auto tree = trees->for_file(fixture("declarations.c"));
  ASSERT_TRUE(tree) << tree.error().message;
  EXPECT_EQ(declaration_names(*tree),
            (std::vector<std::string>{"Widget", "add", "global_count"}));
  ASSERT_EQ(tree->declarations.size(), 3U);
  EXPECT_EQ(tree->declarations[0].kind, liquify::DeclarationKind::type);
  EXPECT_EQ(tree->declarations[1].kind, liquify::DeclarationKind::function);
  EXPECT_EQ(tree->declarations[1].modifiers,
            (std::vector<std::string>{"static", "inline"}));
  EXPECT_EQ(tree->declarations[2].kind, liquify::DeclarationKind::variable);
  EXPECT_EQ(tree->declarations[2].modifiers,
            (std::vector<std::string>{"extern"}));
}

TEST(StubTrees, CppFixtureHasExpectedDeclarations) {
  TemporaryDirectory state;
  auto trees = liquify::StubTrees::open(state.path());
  ASSERT_TRUE(trees) << trees.error().message;

  auto tree = trees->for_file(fixture("declarations.cpp"));
  ASSERT_TRUE(tree) << tree.error().message;
  EXPECT_EQ(
      declaration_names(*tree),
      (std::vector<std::string>{"sample", "Widget", "version", "make_widget"}));
  ASSERT_EQ(tree->declarations.size(), 4U);
  EXPECT_EQ(tree->declarations[0].kind, liquify::DeclarationKind::namespace_);
  EXPECT_EQ(tree->declarations[1].kind, liquify::DeclarationKind::type);
  EXPECT_EQ(tree->declarations[2].modifiers,
            (std::vector<std::string>{"constexpr"}));
  EXPECT_EQ(tree->declarations[3].kind, liquify::DeclarationKind::function);
  EXPECT_EQ(tree->declarations[3].modifiers,
            (std::vector<std::string>{"inline"}));
}

TEST(StubTrees, IdenticalBlobsShareOneQueryValue) {
  TemporaryDirectory state;
  auto trees = liquify::StubTrees::open(state.path());
  ASSERT_TRUE(trees) << trees.error().message;

  auto first =
      trees->for_source(liquify::StubLanguage::c, "int shared_value;\n");
  ASSERT_TRUE(first) << first.error().message;
  auto second =
      trees->for_source(liquify::StubLanguage::c, "int shared_value;\n");
  ASSERT_TRUE(second) << second.error().message;

  EXPECT_EQ(liquid::to_string(first->blob), liquid::to_string(second->blob));
  EXPECT_EQ(declaration_names(*first), declaration_names(*second));
  EXPECT_EQ(trees->statistics().executions, 1U);
}

TEST(StubTrees, PersistedStubIsReusedAfterGraphReopen) {
  TemporaryDirectory state;
  {
    auto trees = liquify::StubTrees::open(state.path());
    ASSERT_TRUE(trees) << trees.error().message;
    auto tree =
        trees->for_source(liquify::StubLanguage::c, "int persisted_value;\n");
    ASSERT_TRUE(tree) << tree.error().message;
    auto flushed = trees->flush();
    ASSERT_TRUE(flushed) << flushed.error().message;
  }

  auto reopened = liquify::StubTrees::open(state.path());
  ASSERT_TRUE(reopened) << reopened.error().message;
  auto tree =
      reopened->for_source(liquify::StubLanguage::c, "int persisted_value;\n");
  ASSERT_TRUE(tree) << tree.error().message;
  EXPECT_EQ(declaration_names(*tree),
            (std::vector<std::string>{"persisted_value"}));
  EXPECT_EQ(reopened->statistics().executions, 0U);
}

TEST(StubTrees, ResolverUsesStubsFromSeparateFiles) {
  TemporaryDirectory state;
  auto trees = liquify::StubTrees::open(state.path());
  ASSERT_TRUE(trees) << trees.error().message;

  auto provider = trees->for_source(
      liquify::StubLanguage::c, "int provided(int value) { return value; }\n");
  ASSERT_TRUE(provider) << provider.error().message;
  auto consumer = trees->for_source(liquify::StubLanguage::c,
                                    "int caller() { return provided(1); }\n");
  ASSERT_TRUE(consumer) << consumer.error().message;

  liquify::StubResolver resolver;
  resolver.add("provider.c", std::move(*provider));
  resolver.add("consumer.c", std::move(*consumer));
  const auto locations = resolver.resolve("provided");
  ASSERT_EQ(locations.size(), 1U);
  EXPECT_EQ(locations[0].path, "provider.c");
  EXPECT_EQ(locations[0].declaration.kind, liquify::DeclarationKind::function);
}

TEST(StubTrees, EditedBlobRecomputesWhileUnchangedBlobIsReused) {
  TemporaryDirectory state;
  auto trees = liquify::StubTrees::open(state.path());
  ASSERT_TRUE(trees) << trees.error().message;

  auto unchanged =
      trees->for_source(liquify::StubLanguage::c, "int retained;\n");
  ASSERT_TRUE(unchanged) << unchanged.error().message;
  auto before =
      trees->for_source(liquify::StubLanguage::c, "int changing = 1;\n");
  ASSERT_TRUE(before) << before.error().message;
  auto after =
      trees->for_source(liquify::StubLanguage::c, "int changing = 2;\n");
  ASSERT_TRUE(after) << after.error().message;
  auto reused = trees->for_source(liquify::StubLanguage::c, "int retained;\n");
  ASSERT_TRUE(reused) << reused.error().message;

  EXPECT_NE(liquid::to_string(before->blob), liquid::to_string(after->blob));
  EXPECT_EQ(liquid::to_string(unchanged->blob),
            liquid::to_string(reused->blob));
  EXPECT_EQ(trees->statistics().executions, 3U);
}

} // namespace
