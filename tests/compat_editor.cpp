#include <filesystem>
#include <print>
#include <string>
#include <thread>

#include "../src/editor_buffer.hpp"
#include "../src/repository_view.hpp"

namespace {

auto round_trip(const std::filesystem::path& repository, const std::string& file) -> int {
    auto view = liquify::RepositoryView::open(repository);
    if (!view.error().empty()) {
        std::println(stderr, "open: {}", view.error());
        return 1;
    }
    while (view.navigation_loading()) {
        view.poll();
        std::this_thread::yield();
    }
    if (!view.navigation_error().empty()) {
        std::println(stderr, "index: {}", view.navigation_error());
        return 1;
    }
    auto buffer = liquify::EditorBuffer::open(repository, file, "compatibility-input");
    if (!buffer.error().empty()) {
        std::println(stderr, "read: {}", buffer.error());
        return 1;
    }
    const auto original = buffer.text();
    const auto expected = original + "saved by liquify compatibility check\n";
    buffer.select_all();
    if (!buffer.insert_text(expected) || !buffer.dirty() || !buffer.save()) {
        std::println(stderr, "edit/save: {}", buffer.error());
        return 1;
    }
    if (buffer.dirty() || buffer.pending_drips() == 0) {
        std::println(stderr, "save did not publish a drip and clear the dirty buffer");
        return 1;
    }
    const auto reopened = liquify::EditorBuffer::open(repository, file, "compatibility-readback");
    if (!reopened.error().empty() || reopened.text() != expected) {
        std::println(stderr, "read-back differs: {}", reopened.error());
        return 1;
    }
    std::println("{}: open/edit/save/reopen PASS; pending drips={}", file, buffer.pending_drips());
    std::print("{}", reopened.text());
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::println(stderr, "usage: liquify-compat-editor REPOSITORY FILE");
        return 2;
    }
    return round_trip(argv[1], argv[2]);
}
