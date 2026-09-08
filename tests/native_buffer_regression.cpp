#include "native_buffer_regression.hpp"

#include "../src/editor_buffer.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/app.hpp"
#include "liquify/editor_graph.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
    std::printf("PASS: %.*s\n", static_cast<int>(message.size()), message.data());
}

struct BufferState {
    liquify::EditorBuffer* identity;
    std::string text;
    std::vector<liquify::TextPosition> cursors;
    std::vector<liquify::TextSelection> selections;
    std::uint64_t revision;
    bool dirty;

    explicit BufferState(liquify::EditorBuffer* buffer)
        : identity(buffer), text(buffer->text()), cursors(buffer->cursors()),
          selections(buffer->selections()), revision(buffer->revision()), dirty(buffer->dirty()) {}

    bool matches(liquify::EditorBuffer* buffer) const {
        return buffer == identity && buffer->text() == text && buffer->cursors() == cursors &&
               buffer->selections() == selections && buffer->revision() == revision &&
               buffer->dirty() == dirty;
    }
};

class FixtureCleanup {
public:
    explicit FixtureCleanup(std::vector<std::filesystem::path>& paths) : paths_(paths) {}

    ~FixtureCleanup() {
        for (const auto& path : paths_) {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    }

private:
    std::vector<std::filesystem::path>& paths_;
};

std::vector<std::filesystem::path> fixture_paths;

void create_fixture(const std::filesystem::path& path) {
    auto repository = liquid::runtime::block_on(liquid::repository::create(path));
    require(repository.has_value(), "fixture created with pinned libliquid");
    for (const auto* name : {"README.md", "src/main.cpp"}) {
        const auto content = std::string{"saved text\n"};
        const auto bytes = std::as_bytes(std::span(content));
        auto written =
            liquid::runtime::block_on(repository->write(name, {bytes.begin(), bytes.end()}));
        require(written.has_value(), "fixture file persisted");
    }
}

std::string persisted(const std::filesystem::path& path, std::string_view file) {
    auto repository = liquid::runtime::block_on(liquid::repository::open(path));
    if (!repository) {
        throw std::runtime_error(repository.error().message);
    }
    auto contents = liquid::runtime::block_on(repository->read(std::string(file)));
    if (!contents) {
        throw std::runtime_error(contents.error().message);
    }
    return {reinterpret_cast<const char*>(contents->data()), contents->size()};
}
} // namespace

void liquify::NativeBufferRegression::pulse(App& app) {
    app.options_.max_frames = 5;
    if (app.run() != 0) {
        throw std::runtime_error("native event loop failed");
    }
    if (app.editor_graph_) {
        app.editor_graph_->wait_until_idle();
    }
}

void liquify::NativeBufferRegression::switches(App& app) {
    app.open_editor("README.md", "fixture-readme");
    pulse(app);
    const auto original = app.editor_->text();
    text("ISSUE_55_UNSAVED_NATIVE_MARKER");
    pulse(app);
    require(app.editor_->dirty(), "native typing made README.md dirty");
    app.editor_->set_cursor({0, 3});
    app.editor_->set_cursor({0, 12}, true);
    const auto state = BufferState(app.editor_);
    app.open_editor("src/main.cpp", "fixture-main");
    pulse(app);
    text("INACTIVE_NATIVE_MARKER");
    pulse(app);
    const auto other = BufferState(app.editor_);
    app.open_editor("README.md", "stale-persisted-blob");
    pulse(app);
    require(app.editor_->text() == state.text, "unsaved marker survives file switch");
    require(state.matches(app.editor_), "same buffer, selection and cursor survive file switch");
    app.open_editor("./README.md", "another-stale-blob");
    require(state.matches(app.editor_), "normalized already-open path retains exact state");
    key('z');
    pulse(app);
    require(app.editor_->text() == original, "native Undo after switch restores original text");
    key('z', true);
    pulse(app);
    require(app.editor_->text() == state.text && app.editor_->selections() == state.selections,
            "native Redo restores marker and selection");
    require(other.matches(app.open_buffers_.at("src/main.cpp").buffer.get()),
            "inactive dirty buffer also retains exact state");
    require(persisted(*app.options_.repository_path, "README.md") == original,
            "switching and undo/redo did not persist unsaved text");
}

void liquify::NativeBufferRegression::close_file(App& app) {
    const auto state = BufferState(app.editor_);
    answer_dialog("Cancel");
    key('w');
    pulse(app);
    require(state.matches(app.editor_),
            "close Cancel preserves exact text, selection and revision");
    key('z');
    pulse(app);
    key('z', true);
    pulse(app);
    require(app.editor_->text() == state.text && app.editor_->selections() == state.selections,
            "close Cancel preserves native undo/redo");
    answer_dialog("Save");
    key('w');
    pulse(app);
    require(!app.open_buffers_.contains("README.md"), "close Save removes only the chosen buffer");
    require(persisted(*app.options_.repository_path, "README.md") == state.text,
            "close Save persisted the unsaved marker through pinned libliquid");
    app.open_editor("README.md", "saved");
    text("EXPLICIT_DISCARD_MARKER");
    pulse(app);
    answer_dialog("Discard");
    key('w');
    pulse(app);
    app.open_editor("README.md", "saved-again");
    require(app.editor_->text() == state.text,
            "close Discard removes only explicitly discarded text");
}

void liquify::NativeBufferRegression::save_failure(App& app) {
    auto invalid = EditorBuffer::from_repository_text(
        *app.options_.repository_path, "invalid.liquid", "invalid ) syntax {");
    app.open_buffers_.emplace("invalid.liquid",
                              App::OpenBuffer{std::make_unique<EditorBuffer>(std::move(invalid))});
    app.open_editor("invalid.liquid", "invalid");
    pulse(app);
    text("SAVE_FAILURE_MARKER");
    pulse(app);
    const auto state = BufferState(app.editor_);
    app.settings_.editor.format_on_save = true;
    answer_dialog("Save|OK");
    key('w');
    pulse(app);
    require(state.matches(app.editor_) && app.open_buffers_.contains("invalid.liquid"),
            "failed Save shows error and keeps the dirty buffer and history");
    answer_dialog("Discard");
    key('w');
    pulse(app);
    app.open_editor("README.md", "retained");
}

void liquify::NativeBufferRegression::switch_repository(App& app) {
    const auto original_path = *app.options_.repository_path;
    const auto suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto other_path = std::filesystem::path(original_path.string() + "-second-" + suffix);
    fixture_paths.push_back(other_path);
    create_fixture(other_path);
    text("REPOSITORY_SWITCH_MARKER");
    pulse(app);
    const auto active = BufferState(app.editor_);
    const auto inactive = BufferState(app.open_buffers_.at("src/main.cpp").buffer.get());
    auto switch_to_other = [&](std::string_view answer) {
        answer_dialog(answer);
        app.begin_open_repository(other_path);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        while (app.repository_operation_ && std::chrono::steady_clock::now() < deadline) {
            pulse(app);
        }
        require(!app.repository_operation_, "repository open operation completed");
    };
    switch_to_other("Cancel");
    require(*app.options_.repository_path == original_path && active.matches(app.editor_) &&
                inactive.matches(app.open_buffers_.at("src/main.cpp").buffer.get()),
            "repository Cancel preserves both dirty buffers, selections and revisions");
    key('z');
    pulse(app);
    key('z', true);
    pulse(app);
    require(app.editor_->text() == active.text && app.editor_->selections() == active.selections,
            "repository Cancel preserves native undo/redo");
    switch_to_other("Discard");
    require(*app.options_.repository_path == other_path && app.open_buffers_.empty(),
            "repository Discard clears buffers only after explicit choice");
    require(persisted(original_path, "README.md").find("REPOSITORY_SWITCH_MARKER") ==
                    std::string::npos &&
                persisted(original_path, "src/main.cpp").find("INACTIVE_NATIVE_MARKER") ==
                    std::string::npos,
            "repository Discard did not persist either unsaved marker");
}

void liquify::NativeBufferRegression::quit(App& app) {
    app.open_editor("README.md", "quit-readme");
    pulse(app);
    text("QUIT_README_MARKER");
    pulse(app);
    app.editor_->set_cursor({0, 2});
    app.editor_->set_cursor({0, 8}, true);
    const auto inactive = BufferState(app.editor_);
    app.open_editor("src/main.cpp", "quit-main");
    pulse(app);
    text("QUIT_MAIN_MARKER");
    pulse(app);
    const auto active = BufferState(app.editor_);
    answer_dialog("Cancel");
    close_window(app);
    pulse(app);
    require(!app.quit_ && active.matches(app.editor_) &&
                inactive.matches(app.open_buffers_.at("README.md").buffer.get()),
            "window close Cancel preserves both dirty buffers and keeps window open");
    answer_dialog("Cancel");
    key('q');
    pulse(app);
    require(!app.quit_ && active.matches(app.editor_),
            "quit shortcut Cancel preserves exact state");
    key('z');
    pulse(app);
    key('z', true);
    pulse(app);
    require(app.editor_->text() == active.text, "window close Cancel preserves native undo/redo");
    answer_dialog("Save");
    close_window(app);
    pulse(app);
    require(app.quit_, "window close Save permits quitting");
    require(persisted(*app.options_.repository_path, "README.md") == inactive.text &&
                persisted(*app.options_.repository_path, "src/main.cpp") == active.text,
            "window close Save persists active and inactive dirty buffers");
}

int liquify::NativeBufferRegression::run(App& app) {
    describe_window(app);
    switches(app);
    close_file(app);
    save_failure(app);
    switch_repository(app);
    quit(app);
    std::printf("Native dirty-buffer regression: PASS\n");
    return 0;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc != 2) {
        std::fprintf(stderr, "usage: liquify-native-buffer-regression NEW_FIXTURE_DIRECTORY\n");
        return 2;
    }
    try {
        const auto fixture = std::filesystem::absolute(argv[1]);
        require(!std::filesystem::exists(fixture), "fixture directory is fresh");
        fixture_paths.push_back(fixture);
        FixtureCleanup cleanup(fixture_paths);
        create_fixture(fixture);
        liquify::AppOptions options;
        options.repository_path = fixture;
        liquify::App app(options);
        if (!app.init()) {
            std::fprintf(stderr,
                         "SKIP: native window initialization is unavailable on this macOS host\n");
            return 77;
        }
        require(true, "native application initialized");
        return liquify::NativeBufferRegression::run(app);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
