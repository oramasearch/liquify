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
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("liquify-repository-view-" + std::to_string(suffix));
        std::filesystem::remove_all(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;

    [[nodiscard]] auto path() const -> const std::filesystem::path& {
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

TEST(RepositoryView, OpensFixtureCreatedByPinnedLiquid) {
    TemporaryDirectory location;
    auto created = liquid::runtime::block_on(liquid::repository::create(location.path()));
    ASSERT_TRUE(created) << created.error().message;

    auto repository = std::move(*created);
    auto written = liquid::runtime::block_on(repository.write("README.md", bytes("root")));
    ASSERT_TRUE(written) << written.error().message;
    written = liquid::runtime::block_on(repository.write("src/main.cpp", bytes("source")));
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
    auto created = liquid::runtime::block_on(liquid::repository::create(location.path()));
    ASSERT_TRUE(created) << created.error().message;

    auto repository = std::move(*created);
    auto written =
        liquid::runtime::block_on(repository.write("src/nested/main.cpp", bytes("source")));
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
    ASSERT_TRUE(view.directory_error("src").empty()) << view.directory_error("src");
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

    const auto view = liquify::RepositoryView::clone(location.path(),
                                                     "https://lake.invalid/repositories/example");

    EXPECT_FALSE(view.error().empty());
    EXPECT_TRUE(std::filesystem::exists(location.path() / "keep.txt"));
    auto input = std::ifstream{location.path() / "keep.txt"};
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "keep me");
}

TEST(RepositoryView, ReportsRepositorySummaryMetadata) {
    TemporaryDirectory location;
    auto created = liquid::runtime::block_on(liquid::repository::create(location.path()));
    ASSERT_TRUE(created) << created.error().message;

    auto repository = std::move(*created);
    auto written = liquid::runtime::block_on(repository.write("README.md", bytes("root")));
    ASSERT_TRUE(written) << written.error().message;
    const auto change = liquid::runtime::block_on(repository.cut("First named change"));
    ASSERT_TRUE(change) << change.error().message;
    auto set = liquid::runtime::block_on(repository.set_ref("refs/heads/main", *change));
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
