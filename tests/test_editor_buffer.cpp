// SPDX-License-Identifier: MIT

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "../src/editor_buffer.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace {

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path() /
                ("liquify-editor-buffer-" + std::to_string(suffix));
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

auto text(std::span<const std::byte> value) -> std::string {
    const auto* data = reinterpret_cast<const char*>(value.data());
    return std::string(data, value.size());
}

TEST(EditorBuffer, EditsSelectionAndLinesInMemory) {
    auto buffer = liquify::EditorBuffer::from_text("blob-1", "notes.txt", "alpha\nbeta");
    EXPECT_EQ(buffer.line_count(), 2U);
    EXPECT_FALSE(buffer.dirty());

    buffer.set_cursor({0, 5});
    const auto newline = buffer.insert_newline();
    EXPECT_EQ(newline.first_line, 0U);
    EXPECT_EQ(newline.erased_lines, 1U);
    EXPECT_EQ(newline.inserted_lines, 2U);
    EXPECT_EQ(buffer.text(), "alpha\n\nbeta");

    buffer.set_cursor({1, 0});
    const auto inserted = buffer.insert_text("gamma");
    ASSERT_TRUE(inserted);
    buffer.set_cursor({1, 0});
    buffer.move_right(true);
    buffer.move_right(true);
    EXPECT_EQ(buffer.byte_offset(buffer.cursor()), 8U);
    const auto erased = buffer.backspace();
    ASSERT_TRUE(erased);
    EXPECT_EQ(buffer.text(), "alpha\nmma\nbeta");
    EXPECT_TRUE(buffer.dirty());
}

TEST(EditorBuffer, MovesAcrossUtf8Codepoints) {
    const std::string utf8{'a', static_cast<char>(0xc3), static_cast<char>(0xa9), 'z'};
    auto buffer = liquify::EditorBuffer::from_text("blob-2", "unicode.txt", utf8);
    buffer.move_end();
    buffer.move_left();
    EXPECT_EQ(buffer.cursor(), (liquify::TextPosition{0, 3}));
    buffer.move_left();
    EXPECT_EQ(buffer.cursor(), (liquify::TextPosition{0, 1}));
    const auto erased = buffer.delete_forward();
    ASSERT_TRUE(erased);
    EXPECT_EQ(buffer.text(), "az");
}

TEST(EditorBuffer, SaveWritesThroughLiquid) {
    TemporaryDirectory location;
    auto created = liquid::runtime::block_on(liquid::repository::create(location.path()));
    ASSERT_TRUE(created) << created.error().message;

    auto repository = std::move(*created);
    auto written = liquid::runtime::block_on(repository.write("notes.txt", bytes("before")));
    ASSERT_TRUE(written) << written.error().message;
    auto buffer = liquify::EditorBuffer::open(location.path(), "notes.txt", "blob-3");
    ASSERT_TRUE(buffer.error().empty()) << buffer.error();
    buffer.move_end();
    ASSERT_TRUE(buffer.insert_text(" after"));
    ASSERT_TRUE(buffer.save()) << buffer.error();
    EXPECT_FALSE(buffer.dirty());
    EXPECT_GE(buffer.pending_drips(), 1U);

    auto reopened = liquid::runtime::block_on(liquid::repository::open(location.path()));
    ASSERT_TRUE(reopened) << reopened.error().message;
    auto contents = liquid::runtime::block_on(reopened->read("notes.txt"));
    ASSERT_TRUE(contents) << contents.error().message;
    EXPECT_EQ(text(*contents), "before after");
}

TEST(EditorBuffer, AppliesMultiCursorAndColumnEditsAsOneRevision) {
    auto buffer = liquify::EditorBuffer::from_text("blob-multi", "main.cpp", "one\ntwo\nthree");
    buffer.set_cursor({0, 0});
    buffer.add_cursor({1, 0});
    buffer.add_cursor({2, 0});
    ASSERT_TRUE(buffer.insert_text("> "));
    EXPECT_EQ(buffer.text(), "> one\n> two\n> three");
    EXPECT_EQ(buffer.revision(), 1U);
    EXPECT_EQ(buffer.cursors().size(), 3U);

    buffer.select_column({0, 2}, {2, 5});
    ASSERT_EQ(buffer.selections().size(), 3U);
    ASSERT_TRUE(buffer.insert_text("item"));
    EXPECT_EQ(buffer.text(), "> item\n> item\n> itemee");
    EXPECT_EQ(buffer.revision(), 2U);
}

TEST(EditorBuffer, FindsAndReplacesAcrossTheBuffer) {
    auto buffer =
        liquify::EditorBuffer::from_text("blob-find", "main.cpp", "value = 12;\nvalue = 34;\n");
    const auto matches =
        buffer.find("value = [0-9]+", {.regular_expression = true, .case_sensitive = true});
    ASSERT_EQ(matches.size(), 2U);
    ASSERT_TRUE(buffer.replace_all("value", "answer", {.case_sensitive = true}));
    EXPECT_EQ(buffer.text(), "answer = 12;\nanswer = 34;\n");
}

} // namespace

TEST(EditorBuffer, UndoRedoRetainsSelectionsAndSaveBoundary) {
    auto buffer = liquify::EditorBuffer::from_text("undo", "notes.txt", "alpha\nbeta");
    buffer.set_cursor({0, 1});
    buffer.set_cursor({0, 4}, true);
    const auto selection = buffer.selection();
    ASSERT_TRUE(buffer.insert_text("MARKER"));
    const auto edited = buffer.text();
    ASSERT_TRUE(buffer.undo());
    EXPECT_EQ(buffer.text(), "alpha\nbeta");
    EXPECT_EQ(buffer.selection(), selection);
    EXPECT_FALSE(buffer.dirty());
    ASSERT_TRUE(buffer.redo());
    EXPECT_EQ(buffer.text(), edited);
    EXPECT_TRUE(buffer.dirty());
    ASSERT_TRUE(buffer.undo());
    ASSERT_TRUE(buffer.insert_text("different"));
    EXPECT_FALSE(buffer.redo());
}

TEST(EditorBuffer, UndoMultiCursorEditIsOneOperation) {
    auto buffer = liquify::EditorBuffer::from_text("multi-undo", "notes.txt", "alpha\nbeta");
    buffer.set_cursor({0, 2});
    buffer.add_cursor({1, 2});
    const auto cursors = buffer.cursors();
    ASSERT_TRUE(buffer.insert_text("!"));
    EXPECT_EQ(buffer.text(), "al!pha\nbe!ta");
    ASSERT_TRUE(buffer.undo());
    EXPECT_EQ(buffer.text(), "alpha\nbeta");
    EXPECT_EQ(buffer.cursors(), cursors);
    EXPECT_FALSE(buffer.undo());
    ASSERT_TRUE(buffer.redo());
    EXPECT_EQ(buffer.text(), "al!pha\nbe!ta");
}

TEST(EditorBuffer, SavingDoesNotEraseLocalUndo) {
    TemporaryDirectory location;
    auto repository = liquid::runtime::block_on(liquid::repository::create(location.path()));
    ASSERT_TRUE(repository);
    auto buffer = liquify::EditorBuffer::from_repository_text(location.path(), "notes.txt", "old");
    buffer.move_end();
    ASSERT_TRUE(buffer.insert_text(" new"));
    ASSERT_TRUE(buffer.save(*repository));
    ASSERT_TRUE(buffer.undo());
    EXPECT_EQ(buffer.text(), "old");
    EXPECT_TRUE(buffer.dirty());
    ASSERT_TRUE(buffer.redo());
    EXPECT_EQ(buffer.text(), "old new");
    EXPECT_FALSE(buffer.dirty());
}

TEST(EditorBuffer, RefreshSavedTextPreservesDirtyWorkAndUndo) {
    auto buffer = liquify::EditorBuffer::from_text("refresh", "notes.txt", "original");
    ASSERT_TRUE(buffer.refresh_saved_text("persisted operation"));
    EXPECT_FALSE(buffer.dirty());
    ASSERT_TRUE(buffer.undo());
    EXPECT_EQ(buffer.text(), "original");
    EXPECT_TRUE(buffer.dirty());
    EXPECT_FALSE(buffer.refresh_saved_text("external edit"));
    EXPECT_EQ(buffer.text(), "original");
    ASSERT_TRUE(buffer.redo());
    EXPECT_EQ(buffer.text(), "persisted operation");
    EXPECT_FALSE(buffer.dirty());
}
