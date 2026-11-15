#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <random>
#include <span>
#include <string>
#include <system_error>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/change_cut.hpp"

namespace {

class Scratch {
public:
    Scratch() {
        auto device = std::random_device{};
        path_ = std::filesystem::temp_directory_path() /
                std::format("liquify-cut-{:08x}{:08x}", device(), device());
    }
    ~Scratch() {
        auto ignored = std::error_code{};
        std::filesystem::remove_all(path_, ignored);
    }
    Scratch(const Scratch&) = delete;
    auto operator=(const Scratch&) -> Scratch& = delete;
    [[nodiscard]] auto path() const -> const std::filesystem::path& { return path_; }

private:
    std::filesystem::path path_;
};

void write(const std::filesystem::path& repository, std::string path, std::string_view text) {
    auto opened = liquid::runtime::block_on(liquid::repository::open(repository));
    ASSERT_TRUE(opened) << opened.error().message;
    const auto bytes = std::as_bytes(std::span(text));
    const auto written = liquid::runtime::block_on(
        opened->write(std::move(path), {bytes.begin(), bytes.end()}));
    ASSERT_TRUE(written) << written.error().message;
}

TEST(ChangeCut, ListsSavedDripsAndCutsThemAsOneChange) {
    const auto scratch = Scratch{};
    ASSERT_TRUE(liquid::runtime::block_on(liquid::repository::create(scratch.path())));
    write(scratch.path(), "src/a.cpp", "int a;\n");
    write(scratch.path(), "src/b.cpp", "int b;\n");
    write(scratch.path(), "src/a.cpp", "int a = 1;\n");

    const auto pending = liquify::load_pending_change(scratch.path());
    ASSERT_TRUE(pending.failure.empty()) << pending.failure.details;
    EXPECT_EQ(pending.drips.size(), 3U);
    EXPECT_EQ(pending.paths, (std::vector<std::string>{"src/a.cpp", "src/b.cpp"}));
    EXPECT_FALSE(pending.drips.front().author.empty());

    const auto cut = liquify::cut_change(scratch.path(), "Add a and b\n\nTwo files.");
    ASSERT_TRUE(cut.failure.empty()) << cut.failure.details;
    ASSERT_FALSE(cut.change.empty());

    auto opened = liquid::runtime::block_on(liquid::repository::open(scratch.path()));
    ASSERT_TRUE(opened);
    const auto log = liquid::runtime::block_on(opened->log());
    ASSERT_TRUE(log);
    ASSERT_FALSE(log->empty());
    EXPECT_EQ(liquid::to_string(log->front().id.value), cut.change);
    EXPECT_EQ(log->front().message, "Add a and b\n\nTwo files.");
    EXPECT_EQ(log->front().author.identifier(), pending.drips.front().author);

    EXPECT_TRUE(liquify::load_pending_change(scratch.path()).drips.empty());

    // The next change gathers only what was saved after this one.
    write(scratch.path(), "src/c.cpp", "int c;\n");
    const auto next = liquify::load_pending_change(scratch.path());
    ASSERT_EQ(next.drips.size(), 1U);
    EXPECT_EQ(next.paths, (std::vector<std::string>{"src/c.cpp"}));
    EXPECT_FALSE(next.base.empty());
}

TEST(ChangeCut, RefusesAnEmptyMessageWithoutTouchingTheRepository) {
    const auto scratch = Scratch{};
    ASSERT_TRUE(liquid::runtime::block_on(liquid::repository::create(scratch.path())));
    write(scratch.path(), "a.txt", "a\n");
    const auto cut = liquify::cut_change(scratch.path(), "  \n body only");
    EXPECT_EQ(cut.failure.summary, "A change needs a message");
    EXPECT_EQ(liquify::load_pending_change(scratch.path()).drips.size(), 1U);
}

TEST(ChangeCut, MessageSummaryIsTheTrimmedFirstLine) {
    EXPECT_EQ(liquify::cut_message_summary("  Fix the parser \nmore"), "Fix the parser");
    EXPECT_EQ(liquify::cut_message_summary("\nbody"), "");
}

} // namespace
