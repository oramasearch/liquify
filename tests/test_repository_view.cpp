#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
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

TEST(RepositoryView, OpensRepositoryAndListsRootEntries) {
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
}

} // namespace
