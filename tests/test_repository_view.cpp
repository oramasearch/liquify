#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "../src/repository_view.hpp"
#include "liquid/frontend.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace {

class TemporaryDirectory {
public:
  TemporaryDirectory() {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("liquify-repository-view-" + std::to_string(suffix));
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

auto bytes(std::string_view value) -> std::vector<std::byte> {
  const auto characters = std::span(value.data(), value.size());
  const auto source = std::as_bytes(characters);
  return {source.begin(), source.end()};
}

void wait_for_navigation(liquify::RepositoryView &view) {
  while (view.navigation_loading()) {
    view.poll();
    std::this_thread::yield();
  }
}

void wait_for_mutation(liquify::RepositoryView &view) {
  while (view.mutation_running()) {
    view.poll();
    std::this_thread::yield();
  }
}

TEST(RepositoryView, OpensFixtureCreatedByPinnedLiquid) {
  TemporaryDirectory location;
  auto created =
      liquid::runtime::block_on(liquid::repository::create(location.path()));
  ASSERT_TRUE(created) << created.error().message;

  auto repository = std::move(*created);
  auto written =
      liquid::runtime::block_on(repository.write("README.md", bytes("root")));
  ASSERT_TRUE(written) << written.error().message;
  written = liquid::runtime::block_on(
      repository.write("src/main.cpp", bytes("source")));
  ASSERT_TRUE(written) << written.error().message;

  const auto view = liquify::RepositoryView::open(location.path());
  ASSERT_TRUE(view.error().empty()) << view.error();
  ASSERT_EQ(view.entries().size(), 2U);
  EXPECT_EQ(view.entries()[0].name, "README.md");
  EXPECT_EQ(view.entries()[0].kind, liquid::node_kind::blob);
  EXPECT_EQ(view.entries()[0].mode, 0100644U);
  EXPECT_EQ(view.entries()[1].name, "src");
  EXPECT_EQ(view.entries()[1].kind, liquid::node_kind::tree);
  EXPECT_EQ(view.entries()[1].mode, 0040755U);
  EXPECT_GE(view.open_time().count(), 0.0);
}

TEST(RepositoryView, LoadsNestedTreesOnlyWhenRequested) {
  TemporaryDirectory location;
  auto created =
      liquid::runtime::block_on(liquid::repository::create(location.path()));
  ASSERT_TRUE(created) << created.error().message;

  auto repository = std::move(*created);
  auto written = liquid::runtime::block_on(
      repository.write("src/nested/main.cpp", bytes("source")));
  ASSERT_TRUE(written) << written.error().message;

  auto view = liquify::RepositoryView::open(location.path());
  ASSERT_TRUE(view.error().empty()) << view.error();
  ASSERT_EQ(view.entries().size(), 1U);
  EXPECT_FALSE(view.directory_loaded("src"));
  view.request_directory("src", view.entries().front().ref);
  while (view.directory_loading("src")) {
    view.poll();
    std::this_thread::yield();
  }
  ASSERT_TRUE(view.directory_error("src").empty())
      << view.directory_error("src");
  ASSERT_TRUE(view.directory_loaded("src"));
  ASSERT_EQ(view.entries("src").size(), 1U);
  EXPECT_EQ(view.entries("src").front().name, "nested");
}

TEST(RepositoryView, ReportsLiquidErrorForUnreadableRepository) {
  TemporaryDirectory location;
  ASSERT_TRUE(std::filesystem::create_directories(location.path()));

  std::ofstream marker{location.path() / "LIQUID_REPO"};
  ASSERT_TRUE(marker.is_open());
  marker << "not-a-liquid-digest\n";
  marker.close();

  const auto view = liquify::RepositoryView::open(location.path());
  EXPECT_TRUE(view.entries().empty());
  EXPECT_EQ(view.error(), "not-a-liquid-digest: unknown digest algorithm");
}

TEST(RepositoryView, RefusesACloneIntoANonEmptyDirectoryWithoutChangingIt) {
  TemporaryDirectory location;
  ASSERT_TRUE(std::filesystem::create_directories(location.path()));
  auto marker = std::ofstream{location.path() / "keep.txt"};
  marker << "keep me";
  marker.close();

  const auto view = liquify::RepositoryView::clone(
      location.path(), "https://lake.invalid/repositories/example");

  EXPECT_FALSE(view.error().empty());
  EXPECT_TRUE(std::filesystem::exists(location.path() / "keep.txt"));
  auto input = std::ifstream{location.path() / "keep.txt"};
  EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "keep me");
}

TEST(RepositoryView, IndexesPathsSymbolsAndChangeStatesOffTheRenderThread) {
  TemporaryDirectory location;
  auto created =
      liquid::runtime::block_on(liquid::repository::create(location.path()));
  ASSERT_TRUE(created) << created.error().message;

  auto repository = std::move(*created);
  ASSERT_TRUE(liquid::runtime::block_on(
      repository.write("README.md", bytes("baseline"))));
  ASSERT_TRUE(liquid::runtime::block_on(repository.cut("baseline")));
  ASSERT_TRUE(liquid::runtime::block_on(
      repository.write("src/render.cpp", bytes("void render() {}"))));
  auto root = liquid::runtime::block_on(repository.root());
  ASSERT_TRUE(root) << root.error().message;
  const auto src =
      std::ranges::find(root->entries, "src", &liquid::tree_entry::name);
  ASSERT_NE(src, root->entries.end());
  auto source_tree = liquid::runtime::block_on(repository.tree(src->ref));
  ASSERT_TRUE(source_tree) << source_tree.error().message;
  const auto source = std::ranges::find(source_tree->entries, "render.cpp",
                                        &liquid::tree_entry::name);
  ASSERT_NE(source, source_tree->entries.end());
  auto record = liquid::symbol_record{
      .subject = source->id,
      .language = "c++",
      .origin = liquid::symbol_origin::compiler,
      .grammar = "test",
      .occurrences = {{"render-id",
                       "render",
                       liquid::symbol_kind::function,
                       liquid::symbol_role::definition,
                       {{5, 0, 5}, {11, 0, 11}},
                       {},
                       {}}},
  };
  ASSERT_TRUE(
      liquid::runtime::block_on(repository.store_symbols(std::move(record))));
  ASSERT_TRUE(liquid::runtime::block_on(repository.cut("render change")));
  ASSERT_TRUE(liquid::runtime::block_on(
      repository.write("README.md", bytes("modified"))));
  ASSERT_TRUE(repository.status());
  ASSERT_EQ(repository.status()->pending_drips, 1U);
  auto pending_drips = liquid::runtime::block_on(repository.drips());
  ASSERT_TRUE(pending_drips) << pending_drips.error().message;
  ASSERT_FALSE(pending_drips->empty());
  ASSERT_FALSE(pending_drips->back().ops.empty());
  ASSERT_EQ(pending_drips->back().ops.front().path, "README.md");
  repository = liquid::repository{};

  auto view = liquify::RepositoryView::open(location.path());
  ASSERT_TRUE(view.error().empty()) << view.error();
  wait_for_navigation(view);
  ASSERT_TRUE(view.navigation_error().empty()) << view.navigation_error();

  const auto *readme = view.metadata("README.md");
  ASSERT_NE(readme, nullptr);
  EXPECT_EQ(readme->kind, liquify::NavigationFileKind::markdown);
  EXPECT_EQ(readme->change, liquify::NavigationChangeState::modified);
  const auto *render = view.metadata("src/render.cpp");
  ASSERT_NE(render, nullptr);
  EXPECT_EQ(render->kind, liquify::NavigationFileKind::cpp_source);
  EXPECT_EQ(render->change, liquify::NavigationChangeState::in_change);
  ASSERT_EQ(view.navigation_symbols().size(), 1U);
  EXPECT_EQ(view.navigation_symbols()[0].name, "render");
  EXPECT_TRUE(view.navigation_symbols()[0].definition);
}

TEST(RepositoryView, ContextMutationsRefreshTheTreeAndNavigationIndex) {
  TemporaryDirectory location;
  auto created =
      liquid::runtime::block_on(liquid::repository::create(location.path()));
  ASSERT_TRUE(created) << created.error().message;
  ASSERT_TRUE(
      liquid::runtime::block_on(created->write("before.txt", bytes("before"))));

  auto view = liquify::RepositoryView::open(location.path());
  wait_for_navigation(view);
  view.request_move("before.txt", "after.txt");
  wait_for_mutation(view);
  wait_for_navigation(view);
  EXPECT_NE(view.metadata("after.txt"), nullptr);
  EXPECT_EQ(view.metadata("before.txt"), nullptr);

  view.request_new_file("notes/new.txt");
  wait_for_mutation(view);
  wait_for_navigation(view);
  EXPECT_NE(view.metadata("notes/new.txt"), nullptr);
  EXPECT_EQ(view.mutation_status(), "created notes/new.txt");
}

TEST(RepositoryView, ReportsRepositorySummaryMetadata) {
  TemporaryDirectory location;
  auto created =
      liquid::runtime::block_on(liquid::repository::create(location.path()));
  ASSERT_TRUE(created) << created.error().message;

  auto repository = std::move(*created);
  auto written =
      liquid::runtime::block_on(repository.write("README.md", bytes("root")));
  ASSERT_TRUE(written) << written.error().message;
  const auto change =
      liquid::runtime::block_on(repository.cut("First named change"));
  ASSERT_TRUE(change) << change.error().message;
  auto set =
      liquid::runtime::block_on(repository.set_ref("refs/heads/main", *change));
  ASSERT_TRUE(set) << set.error().message;

  const auto view = liquify::RepositoryView::open(location.path());

  ASSERT_TRUE(view.error().empty()) << view.error();
  EXPECT_EQ(view.path(), location.path());
  EXPECT_EQ(view.name(), location.path().filename());
  EXPECT_EQ(view.branch(), "main");
  EXPECT_EQ(view.current_change(), liquid::to_string(change->value));
  EXPECT_EQ(view.last_change(), "First named change");
  EXPECT_FALSE(view.last_changed_at().empty());
  EXPECT_EQ(view.presence_count(), 0U);
}

} // namespace
