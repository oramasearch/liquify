#include "editor_buffer.hpp"

#include <algorithm>
#include <exception>
#include <future>
#include <iterator>
#include <span>
#include <utility>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"

namespace liquify {
namespace {

auto error_message(const liquid::error& error) -> std::string {
    if (error.context.empty()) {
        return error.message;
    }
    return error.context + ": " + error.message;
}

auto normalise_newlines(std::string_view text) -> std::string {
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\r') {
            result.push_back(text[index]);
            continue;
        }
        result.push_back('\n');
        if (index + 1 < text.size() && text[index + 1] == '\n') {
            ++index;
        }
    }
    return result;
}

auto split_lines(std::string_view text) -> std::vector<std::string> {
    std::vector<std::string> lines;
    std::size_t begin = 0;
    while (true) {
        const auto end = text.find('\n', begin);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(begin));
            break;
        }
        lines.emplace_back(text.substr(begin, end - begin));
        begin = end + 1;
    }
    return lines;
}

auto previous_codepoint(std::string_view text, std::size_t byte) -> std::size_t {
    if (byte == 0) {
        return 0;
    }
    --byte;
    while (byte > 0 && (static_cast<unsigned char>(text[byte]) & 0xc0U) == 0x80U) {
        --byte;
    }
    return byte;
}

auto next_codepoint(std::string_view text, std::size_t byte) -> std::size_t {
    if (byte >= text.size()) {
        return text.size();
    }
    ++byte;
    while (byte < text.size() && (static_cast<unsigned char>(text[byte]) & 0xc0U) == 0x80U) {
        ++byte;
    }
    return byte;
}

auto bytes(std::string_view value) -> std::vector<std::byte> {
    const auto characters = std::span(value.data(), value.size());
    const auto source = std::as_bytes(characters);
    return {source.begin(), source.end()};
}

} // namespace

EditorBuffer EditorBuffer::open(std::filesystem::path repository_path,
                                std::string blob_path,
                                std::string blob_id) {
    try {
        auto worker = std::async(
            std::launch::async,
            [](std::filesystem::path path, std::string file) {
                auto opened = liquid::runtime::block_on(liquid::repository::open(std::move(path)));
                if (!opened) {
                    return std::pair<std::string, std::string>{"", error_message(opened.error())};
                }
                auto contents = liquid::runtime::block_on(opened->read(std::move(file)));
                if (!contents) {
                    return std::pair<std::string, std::string>{"", error_message(contents.error())};
                }
                const auto* data = reinterpret_cast<const char*>(contents->data());
                return std::pair<std::string, std::string>{std::string(data, contents->size()), ""};
            },
            repository_path,
            blob_path);
        auto [text, error] = worker.get();
        auto buffer = from_text(std::move(blob_id), blob_path, std::move(text));
        buffer.repository_path_ = std::move(repository_path);
        buffer.blob_path_ = std::move(blob_path);
        buffer.error_ = std::move(error);
        return buffer;
    } catch (const std::exception& error) {
        auto buffer = from_text(std::move(blob_id), blob_path, "");
        buffer.repository_path_ = std::move(repository_path);
        buffer.blob_path_ = std::move(blob_path);
        buffer.error_ = error.what();
        return buffer;
    } catch (...) {
        auto buffer = from_text(std::move(blob_id), blob_path, "");
        buffer.repository_path_ = std::move(repository_path);
        buffer.blob_path_ = std::move(blob_path);
        buffer.error_ = "repository worker failed";
        return buffer;
    }
}

EditorBuffer
EditorBuffer::from_text(std::string blob_id, std::string display_path, std::string text) {
    EditorBuffer buffer;
    buffer.blob_id_ = std::move(blob_id);
    buffer.display_path_ = std::move(display_path);
    const auto normalised = normalise_newlines(text);
    buffer.lines_ = split_lines(normalised);
    return buffer;
}

auto EditorBuffer::blob_id() const -> std::string_view {
    return blob_id_;
}

auto EditorBuffer::display_path() const -> std::string_view {
    return display_path_;
}

auto EditorBuffer::line_count() const -> std::size_t {
    return lines_.size();
}

auto EditorBuffer::line(std::size_t index) const -> std::string_view {
    return lines_.at(index);
}

auto EditorBuffer::lines() const -> std::span<const std::string> {
    return lines_;
}

auto EditorBuffer::text() const -> std::string {
    std::size_t size = lines_.empty() ? 0 : lines_.size() - 1;
    for (const auto& value : lines_) {
        size += value.size();
    }
    std::string result;
    result.reserve(size);
    for (std::size_t index = 0; index < lines_.size(); ++index) {
        if (index != 0) {
            result.push_back('\n');
        }
        result += lines_[index];
    }
    return result;
}

auto EditorBuffer::cursor() const -> TextPosition {
    return cursor_;
}

auto EditorBuffer::selection() const -> std::optional<TextSelection> {
    if (!selection_anchor_ || *selection_anchor_ == cursor_) {
        return std::nullopt;
    }
    return TextSelection{std::min(*selection_anchor_, cursor_),
                         std::max(*selection_anchor_, cursor_)};
}

auto EditorBuffer::mode() const -> EditingMode {
    return mode_;
}

auto EditorBuffer::dirty() const -> bool {
    return dirty_;
}

auto EditorBuffer::error() const -> std::string_view {
    return error_;
}

auto EditorBuffer::revision() const -> std::uint64_t {
    return revision_;
}

void EditorBuffer::set_mode(EditingMode mode) {
    mode_ = mode;
    selection_anchor_.reset();
}

auto EditorBuffer::clamped(TextPosition position) const -> TextPosition {
    position.line = std::min(position.line, lines_.size() - 1);
    position.byte = std::min(position.byte, lines_[position.line].size());
    while (position.byte > 0 && position.byte < lines_[position.line].size() &&
           (static_cast<unsigned char>(lines_[position.line][position.byte]) & 0xc0U) == 0x80U) {
        --position.byte;
    }
    return position;
}

void EditorBuffer::set_cursor(TextPosition position, bool extend_selection) {
    prepare_movement(extend_selection);
    cursor_ = clamped(position);
    preferred_byte_ = cursor_.byte;
    finish_movement(extend_selection);
}

void EditorBuffer::select_all() {
    selection_anchor_ = TextPosition{};
    cursor_ = TextPosition{lines_.size() - 1, lines_.back().size()};
    preferred_byte_ = cursor_.byte;
}

void EditorBuffer::prepare_movement(bool extend_selection) {
    if (extend_selection && !selection_anchor_) {
        selection_anchor_ = cursor_;
    }
    if (!extend_selection) {
        selection_anchor_.reset();
    }
}

void EditorBuffer::finish_movement(bool extend_selection) {
    if (!extend_selection || (selection_anchor_ && *selection_anchor_ == cursor_)) {
        selection_anchor_.reset();
    }
}

void EditorBuffer::move_left(bool extend_selection) {
    if (!extend_selection) {
        if (const auto range = selection()) {
            cursor_ = range->begin;
            selection_anchor_.reset();
            preferred_byte_ = cursor_.byte;
            return;
        }
    }
    prepare_movement(extend_selection);
    if (cursor_.byte > 0) {
        cursor_.byte = previous_codepoint(lines_[cursor_.line], cursor_.byte);
    } else if (cursor_.line > 0) {
        --cursor_.line;
        cursor_.byte = lines_[cursor_.line].size();
    }
    preferred_byte_ = cursor_.byte;
    finish_movement(extend_selection);
}

void EditorBuffer::move_right(bool extend_selection) {
    if (!extend_selection) {
        if (const auto range = selection()) {
            cursor_ = range->end;
            selection_anchor_.reset();
            preferred_byte_ = cursor_.byte;
            return;
        }
    }
    prepare_movement(extend_selection);
    if (cursor_.byte < lines_[cursor_.line].size()) {
        cursor_.byte = next_codepoint(lines_[cursor_.line], cursor_.byte);
    } else if (cursor_.line + 1 < lines_.size()) {
        ++cursor_.line;
        cursor_.byte = 0;
    }
    preferred_byte_ = cursor_.byte;
    finish_movement(extend_selection);
}

void EditorBuffer::move_up(bool extend_selection) {
    prepare_movement(extend_selection);
    if (cursor_.line > 0) {
        --cursor_.line;
        cursor_.byte = clamped(TextPosition{cursor_.line, preferred_byte_}).byte;
    }
    finish_movement(extend_selection);
}

void EditorBuffer::move_down(bool extend_selection) {
    prepare_movement(extend_selection);
    if (cursor_.line + 1 < lines_.size()) {
        ++cursor_.line;
        cursor_.byte = clamped(TextPosition{cursor_.line, preferred_byte_}).byte;
    }
    finish_movement(extend_selection);
}

void EditorBuffer::move_home(bool extend_selection) {
    prepare_movement(extend_selection);
    cursor_.byte = 0;
    preferred_byte_ = 0;
    finish_movement(extend_selection);
}

void EditorBuffer::move_end(bool extend_selection) {
    prepare_movement(extend_selection);
    cursor_.byte = lines_[cursor_.line].size();
    preferred_byte_ = cursor_.byte;
    finish_movement(extend_selection);
}

auto EditorBuffer::insertion_range() const -> TextSelection {
    if (const auto range = selection()) {
        return *range;
    }
    return TextSelection{cursor_, cursor_};
}

auto EditorBuffer::replace(TextSelection range, std::string_view text) -> BufferChange {
    range.begin = clamped(range.begin);
    range.end = clamped(range.end);
    if (range.end < range.begin) {
        std::swap(range.begin, range.end);
    }

    const auto normalised = normalise_newlines(text);
    auto inserted = split_lines(normalised);
    const auto prefix = lines_[range.begin.line].substr(0, range.begin.byte);
    const auto suffix = lines_[range.end.line].substr(range.end.byte);
    inserted.front().insert(0, prefix);
    inserted.back() += suffix;

    const auto erased_lines = range.end.line - range.begin.line + 1;
    const auto inserted_lines = inserted.size();
    const auto cursor_line = range.begin.line + inserted_lines - 1;
    const auto cursor_byte = inserted_lines == 1 ? prefix.size() + normalised.size()
                                                 : split_lines(normalised).back().size();

    auto first = lines_.begin() + static_cast<std::ptrdiff_t>(range.begin.line);
    auto last = first + static_cast<std::ptrdiff_t>(erased_lines);
    first = lines_.erase(first, last);
    lines_.insert(
        first, std::make_move_iterator(inserted.begin()), std::make_move_iterator(inserted.end()));
    cursor_ = TextPosition{cursor_line, cursor_byte};
    selection_anchor_.reset();
    preferred_byte_ = cursor_.byte;
    dirty_ = true;
    error_.clear();
    ++revision_;
    return BufferChange{range.begin.line, erased_lines, inserted_lines};
}

auto EditorBuffer::insert_text(std::string_view text) -> std::optional<BufferChange> {
    if (text.empty()) {
        return std::nullopt;
    }
    return replace(insertion_range(), text);
}

auto EditorBuffer::insert_newline() -> BufferChange {
    return replace(insertion_range(), "\n");
}

auto EditorBuffer::backspace() -> std::optional<BufferChange> {
    if (const auto range = selection()) {
        return replace(*range, "");
    }
    if (cursor_ == TextPosition{}) {
        return std::nullopt;
    }
    auto begin = cursor_;
    if (begin.byte > 0) {
        begin.byte = previous_codepoint(lines_[begin.line], begin.byte);
    } else {
        --begin.line;
        begin.byte = lines_[begin.line].size();
    }
    return replace(TextSelection{begin, cursor_}, "");
}

auto EditorBuffer::delete_forward() -> std::optional<BufferChange> {
    if (const auto range = selection()) {
        return replace(*range, "");
    }
    auto end = cursor_;
    if (end.byte < lines_[end.line].size()) {
        end.byte = next_codepoint(lines_[end.line], end.byte);
    } else if (end.line + 1 < lines_.size()) {
        ++end.line;
        end.byte = 0;
    } else {
        return std::nullopt;
    }
    return replace(TextSelection{cursor_, end}, "");
}

auto EditorBuffer::save() -> bool {
    if (!repository_path_) {
        error_ = "buffer is not backed by a liquid repository";
        return false;
    }
    try {
        auto worker = std::async(
            std::launch::async,
            [](std::filesystem::path path, std::string file, std::vector<std::byte> contents) {
                auto opened = liquid::runtime::block_on(liquid::repository::open(std::move(path)));
                if (!opened) {
                    return error_message(opened.error());
                }
                auto written =
                    liquid::runtime::block_on(opened->write(std::move(file), std::move(contents)));
                if (!written) {
                    return error_message(written.error());
                }
                return std::string{};
            },
            *repository_path_,
            blob_path_,
            bytes(text()));
        error_ = worker.get();
        if (!error_.empty()) {
            return false;
        }
        dirty_ = false;
        return true;
    } catch (const std::exception& error) {
        error_ = error.what();
        return false;
    } catch (...) {
        error_ = "repository worker failed";
        return false;
    }
}

auto editing_mode_name(EditingMode mode) -> std::string_view {
    switch (mode) {
    case EditingMode::standard:
        return "Standard";
    case EditingMode::modal_command:
        return "Modal: command";
    case EditingMode::modal_insert:
        return "Modal: insert";
    }
    return "Unknown";
}

} // namespace liquify
