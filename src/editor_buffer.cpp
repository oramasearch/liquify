#include "editor_buffer.hpp"

#include <algorithm>
#include <format>
#include <exception>
#include <future>
#include <iterator>
#include <span>
#include <tuple>
#include <utility>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "repository_error.hpp"

namespace liquify {
namespace {

auto error_message(const liquid::error &error) -> std::string {
  return repository_error_message(error);
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

auto previous_codepoint(std::string_view text, std::size_t byte)
    -> std::size_t {
  if (byte == 0) {
    return 0;
  }
  --byte;
  while (byte > 0 &&
         (static_cast<unsigned char>(text[byte]) & 0xc0U) == 0x80U) {
    --byte;
  }
  return byte;
}

auto next_codepoint(std::string_view text, std::size_t byte) -> std::size_t {
  if (byte >= text.size()) {
    return text.size();
  }
  ++byte;
  while (byte < text.size() &&
         (static_cast<unsigned char>(text[byte]) & 0xc0U) == 0x80U) {
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
                                std::string blob_path, std::string blob_id) {
  try {
    auto worker = std::async(
        std::launch::async,
        [](std::filesystem::path path, std::string file) {
          auto opened = liquid::runtime::block_on(
              liquid::repository::open(std::move(path)));
          if (!opened) {
              return std::tuple<std::string, std::string, std::string>{
                  "", error_message(opened.error()), ""};
          }
          auto contents = liquid::runtime::block_on(opened->read(file));
          if (!contents) {
              return std::tuple<std::string, std::string, std::string>{
                  "", error_message(contents.error()), ""};
          }
          // liquify#78: a blob is opened only when it is text of a size an editor can hold.
          constexpr auto max_editor_bytes = std::size_t{16} * 1024 * 1024;
          if (contents->size() > max_editor_bytes) {
              return std::tuple<std::string, std::string, std::string>{
                  "", std::format("{} is too large to open ({} bytes; the limit is {})", file,
                                  contents->size(), max_editor_bytes), ""};
          }
          const auto probe = std::min<std::size_t>(contents->size(), 8192);
          if (std::any_of(contents->begin(), contents->begin() + static_cast<std::ptrdiff_t>(probe),
                          [](auto byte) { return static_cast<unsigned char>(byte) == 0; })) {
              return std::tuple<std::string, std::string, std::string>{
                  "", file + " looks binary and is not opened as text", ""};
          }
          const auto *data = reinterpret_cast<const char *>(contents->data());
          auto editor_config = std::string{};
          auto configured = liquid::runtime::block_on(opened->read(".editorconfig"));
          if (configured) {
              const auto* config_data = reinterpret_cast<const char*>(configured->data());
              editor_config.assign(config_data, configured->size());
          }
          return std::tuple<std::string, std::string, std::string>{
              std::string(data, contents->size()), "", std::move(editor_config)};
        },
        repository_path, blob_path);
    auto [text, error, editor_config] = worker.get();
    auto buffer = from_text(std::move(blob_id), blob_path, std::move(text));
    buffer.repository_path_ = std::move(repository_path);
    buffer.blob_path_ = std::move(blob_path);
    buffer.error_ = std::move(error);
    buffer.preferences_ = parse_editor_preferences(editor_config, buffer.blob_path_);
    return buffer;
  } catch (const std::exception &error) {
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

EditorBuffer EditorBuffer::from_text(std::string blob_id,
                                     std::string display_path,
                                     std::string text) {
  EditorBuffer buffer;
  buffer.blob_id_ = std::move(blob_id);
  buffer.display_path_ = std::move(display_path);
  const auto normalised = normalise_newlines(text);
  buffer.lines_ = split_lines(normalised);
  buffer.saved_text_ = normalised;
  return buffer;
}

EditorBuffer
EditorBuffer::from_repository_text(std::filesystem::path repository_path,
                                   std::string blob_path, std::string text) {
  auto buffer =
      from_text("structural:" + blob_path, blob_path, std::move(text));
  buffer.repository_path_ = std::move(repository_path);
  buffer.blob_path_ = std::move(blob_path);
  return buffer;
}

auto EditorBuffer::blob_id() const -> std::string_view { return blob_id_; }

auto EditorBuffer::display_path() const -> std::string_view {
  return display_path_;
}

auto EditorBuffer::line_count() const -> std::size_t { return lines_.size(); }

auto EditorBuffer::line(std::size_t index) const -> std::string_view {
  return lines_.at(index);
}

auto EditorBuffer::lines() const -> std::span<const std::string> {
  return lines_;
}

auto EditorBuffer::text() const -> std::string {
  std::size_t size = lines_.empty() ? 0 : lines_.size() - 1;
  for (const auto &value : lines_) {
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

auto EditorBuffer::cursor() const -> TextPosition { return cursor_; }

auto EditorBuffer::cursors() const -> std::vector<TextPosition> {
    auto output = std::vector<TextPosition>{cursor_};
    output.reserve(secondary_carets_.size() + 1);
    for (const auto& caret : secondary_carets_) {
        output.push_back(caret.position);
    }
    return output;
}

auto EditorBuffer::byte_offset(TextPosition position) const -> std::size_t {
  position = clamped(position);
  auto offset = std::size_t{};
  for (std::size_t line = 0; line < position.line; ++line) {
    offset += lines_[line].size() + 1;
  }
  return offset + position.byte;
}

auto EditorBuffer::pending_drips() const -> std::uint64_t {
  return pending_drips_;
}

auto EditorBuffer::selection() const -> std::optional<TextSelection> {
  if (!selection_anchor_ || *selection_anchor_ == cursor_) {
    return std::nullopt;
  }
  return TextSelection{std::min(*selection_anchor_, cursor_),
                       std::max(*selection_anchor_, cursor_)};
}

auto EditorBuffer::selections() const -> std::vector<TextSelection> {
    auto output = std::vector<TextSelection>{};
    if (const auto primary = selection()) {
        output.push_back(*primary);
    }
    for (const auto& caret : secondary_carets_) {
        if (!caret.anchor || *caret.anchor == caret.position) {
            continue;
        }
        output.push_back(
            {std::min(*caret.anchor, caret.position), std::max(*caret.anchor, caret.position)});
    }
    return output;
}

auto EditorBuffer::preferences() const -> const EditorPreferences& {
    return preferences_;
}

auto EditorBuffer::mode() const -> EditingMode { return mode_; }

auto EditorBuffer::dirty() const -> bool { return dirty_; }

auto EditorBuffer::error() const -> std::string_view { return error_; }

auto EditorBuffer::revision() const -> std::uint64_t { return revision_; }

void EditorBuffer::set_mode(EditingMode mode) {
  mode_ = mode;
  selection_anchor_.reset();
  secondary_carets_.clear();
}

auto EditorBuffer::clamped(TextPosition position) const -> TextPosition {
  position.line = std::min(position.line, lines_.size() - 1);
  position.byte = std::min(position.byte, lines_[position.line].size());
  while (position.byte > 0 && position.byte < lines_[position.line].size() &&
         (static_cast<unsigned char>(lines_[position.line][position.byte]) &
          0xc0U) == 0x80U) {
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

void EditorBuffer::add_cursor(TextPosition position) {
    position = clamped(position);
    if (position == cursor_ ||
        std::ranges::any_of(secondary_carets_,
                            [position](const auto& caret) { return caret.position == position; })) {
        return;
    }
    secondary_carets_.push_back({position, std::nullopt});
}

void EditorBuffer::clear_secondary_cursors() {
    secondary_carets_.clear();
}

void EditorBuffer::select_all() {
    secondary_carets_.clear();
    selection_anchor_ = TextPosition{};
    cursor_ = TextPosition{lines_.size() - 1, lines_.back().size()};
    preferred_byte_ = cursor_.byte;
}

void EditorBuffer::select(SelectionKind kind) {
    secondary_carets_.clear();
    const auto range = selection_for(lines_, cursor_, kind);
    selection_anchor_ = range.begin;
    cursor_ = range.end;
    preferred_byte_ = cursor_.byte;
}

void EditorBuffer::select_column(TextPosition anchor, TextPosition cursor) {
    const auto ranges = column_selections(lines_, clamped(anchor), clamped(cursor));
    secondary_carets_.clear();
    if (ranges.empty()) {
        selection_anchor_.reset();
        return;
    }
    selection_anchor_ = ranges.front().begin;
    cursor_ = ranges.front().end;
    for (std::size_t index = 1; index < ranges.size(); ++index) {
        secondary_carets_.push_back({ranges[index].end, ranges[index].begin});
    }
    preferred_byte_ = cursor_.byte;
}

void EditorBuffer::set_preferences(EditorPreferences preferences) {
    preferences_ = preferences;
}

void EditorBuffer::prepare_movement(bool extend_selection) {
  if (extend_selection && !selection_anchor_) {
    selection_anchor_ = cursor_;
  }
  if (!extend_selection) {
    selection_anchor_.reset();
    secondary_carets_.clear();
  }
}

void EditorBuffer::finish_movement(bool extend_selection) {
  if (!extend_selection ||
      (selection_anchor_ && *selection_anchor_ == cursor_)) {
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

auto EditorBuffer::replace(TextSelection range, std::string_view text)
    -> BufferChange {
    remember_edit();
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
  const auto cursor_byte = inserted_lines == 1
                               ? prefix.size() + normalised.size()
                               : split_lines(normalised).back().size();

  auto first = lines_.begin() + static_cast<std::ptrdiff_t>(range.begin.line);
  auto last = first + static_cast<std::ptrdiff_t>(erased_lines);
  first = lines_.erase(first, last);
  lines_.insert(first, std::make_move_iterator(inserted.begin()),
                std::make_move_iterator(inserted.end()));
  cursor_ = TextPosition{cursor_line, cursor_byte};
  selection_anchor_.reset();
  secondary_carets_.clear();
  preferred_byte_ = cursor_.byte;
  dirty_ = true;
  error_.clear();
  ++revision_;
  return BufferChange{range.begin.line, erased_lines, inserted_lines};
}

auto EditorBuffer::replace_many(std::vector<TextSelection> ranges, std::string_view replacement)
    -> BufferChange {
    remember_edit();
    struct PendingEdit {
        std::size_t start{};
        std::size_t end{};
        std::size_t order{};
    };
    auto edits = std::vector<PendingEdit>{};
    edits.reserve(ranges.size());
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        auto range = ranges[index];
        range.begin = clamped(range.begin);
        range.end = clamped(range.end);
        if (range.end < range.begin) {
            std::swap(range.begin, range.end);
        }
        edits.push_back({byte_offset(range.begin), byte_offset(range.end), index});
    }
    std::ranges::sort(edits, [](const auto& left, const auto& right) {
        return std::tie(left.start, left.end) < std::tie(right.start, right.end);
    });
    edits.erase(std::unique(edits.begin(),
                            edits.end(),
                            [](const auto& left, const auto& right) {
                                return left.start == right.start && left.end == right.end;
                            }),
                edits.end());
    auto accepted = std::vector<PendingEdit>{};
    for (const auto& edit : edits) {
        if (!accepted.empty() && edit.start < accepted.back().end) {
            continue;
        }
        accepted.push_back(edit);
    }
    const auto old_line_count = lines_.size();
    auto source = text();
    for (auto edit = accepted.rbegin(); edit != accepted.rend(); ++edit) {
        source.replace(edit->start, edit->end - edit->start, replacement);
    }
    auto final_offsets = std::vector<std::size_t>(ranges.size());
    auto adjustment = std::ptrdiff_t{};
    for (const auto& edit : accepted) {
        const auto start = static_cast<std::ptrdiff_t>(edit.start) + adjustment;
        final_offsets[edit.order] = static_cast<std::size_t>(start) + replacement.size();
        adjustment += static_cast<std::ptrdiff_t>(replacement.size()) -
                      static_cast<std::ptrdiff_t>(edit.end - edit.start);
    }
    lines_ = split_lines(source);
    cursor_ = position_for_byte(final_offsets.front()).value_or(TextPosition{});
    secondary_carets_.clear();
    for (std::size_t index = 1; index < final_offsets.size(); ++index) {
        if (const auto position = position_for_byte(final_offsets[index])) {
            secondary_carets_.push_back({*position, std::nullopt});
        }
    }
    selection_anchor_.reset();
    preferred_byte_ = cursor_.byte;
    dirty_ = true;
    error_.clear();
    ++revision_;
    return {0, old_line_count, lines_.size()};
}

auto EditorBuffer::insert_text(std::string_view text)
    -> std::optional<BufferChange> {
  if (text.empty()) {
    return std::nullopt;
  }
  if (!secondary_carets_.empty()) {
      auto ranges = std::vector<TextSelection>{insertion_range()};
      ranges.reserve(secondary_carets_.size() + 1);
      for (const auto& caret : secondary_carets_) {
          if (caret.anchor) {
              ranges.push_back({std::min(*caret.anchor, caret.position),
                                std::max(*caret.anchor, caret.position)});
          } else {
              ranges.push_back({caret.position, caret.position});
          }
      }
      return replace_many(std::move(ranges), text);
  }
  return replace(insertion_range(), text);
}

auto EditorBuffer::insert_newline() -> BufferChange {
    return *insert_text("\n");
}

auto EditorBuffer::backspace() -> std::optional<BufferChange> {
    if (!secondary_carets_.empty()) {
        auto ranges = std::vector<TextSelection>{};
        const auto append = [this, &ranges](TextPosition position,
                                            std::optional<TextPosition> anchor) {
            if (anchor && *anchor != position) {
                ranges.push_back({std::min(*anchor, position), std::max(*anchor, position)});
                return;
            }
            if (position == TextPosition{}) {
                return;
            }
            auto begin = position;
            if (begin.byte > 0) {
                begin.byte = previous_codepoint(lines_[begin.line], begin.byte);
            } else {
                --begin.line;
                begin.byte = lines_[begin.line].size();
            }
            ranges.push_back({begin, position});
        };
        append(cursor_, selection_anchor_);
        for (const auto& caret : secondary_carets_) {
            append(caret.position, caret.anchor);
        }
        if (ranges.empty()) {
            return std::nullopt;
        }
        return replace_many(std::move(ranges), "");
    }
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
    if (!secondary_carets_.empty()) {
        auto ranges = std::vector<TextSelection>{};
        const auto append = [this, &ranges](TextPosition position,
                                            std::optional<TextPosition> anchor) {
            if (anchor && *anchor != position) {
                ranges.push_back({std::min(*anchor, position), std::max(*anchor, position)});
                return;
            }
            auto end = position;
            if (end.byte < lines_[end.line].size()) {
                end.byte = next_codepoint(lines_[end.line], end.byte);
            } else if (end.line + 1 < lines_.size()) {
                ++end.line;
                end.byte = 0;
            } else {
                return;
            }
            ranges.push_back({position, end});
        };
        append(cursor_, selection_anchor_);
        for (const auto& caret : secondary_carets_) {
            append(caret.position, caret.anchor);
        }
        if (ranges.empty()) {
            return std::nullopt;
        }
        return replace_many(std::move(ranges), "");
    }
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

auto EditorBuffer::position_for_byte(std::size_t byte) const
    -> std::optional<TextPosition> {
  auto offset = std::size_t{};
  for (auto line_index = std::size_t{}; line_index < lines_.size();
       ++line_index) {
    const auto line_end = offset + lines_[line_index].size();
    if (byte <= line_end) {
      const auto column = byte - offset;
      if (column < lines_[line_index].size() && column > 0 &&
          (static_cast<unsigned char>(lines_[line_index][column]) & 0xc0U) ==
              0x80U) {
        return std::nullopt;
      }
      return TextPosition{line_index, column};
    }
    offset = line_end + 1;
  }
  return std::nullopt;
}

auto EditorBuffer::apply_edit(const SourceEdit &edit)
    -> std::optional<BufferChange> {
  if (edit.start_byte > edit.old_end_byte) {
    error_ = "edit start is after its end";
    return std::nullopt;
  }
  const auto begin = position_for_byte(edit.start_byte);
  const auto end = position_for_byte(edit.old_end_byte);
  if (!begin || !end) {
    error_ = "edit range is outside the buffer or splits a UTF-8 codepoint";
    return std::nullopt;
  }
  return replace(TextSelection{*begin, *end}, edit.text);
}

auto EditorBuffer::find(std::string_view query, FindOptions options) const
    -> std::vector<TextMatch> {
    return find_text(text(), query, options);
}

auto EditorBuffer::replace_all(std::string_view query,
                               std::string_view replacement,
                               FindOptions options) -> std::optional<BufferChange> {
    const auto source = text();
    const auto replaced = replace_text(source, query, replacement, options);
    if (source == replaced) {
        return std::nullopt;
    }
    return replace({TextPosition{}, {lines_.size() - 1, lines_.back().size()}}, replaced);
}

auto EditorBuffer::edit_state() const -> EditState {
    return {lines_, cursor_, selection_anchor_, secondary_carets_, preferred_byte_};
}

void EditorBuffer::remember_edit() {
    undo_.push_back(edit_state());
    redo_.clear();
}

auto EditorBuffer::restore_edit(std::vector<EditState>& from, std::vector<EditState>& to)
    -> std::optional<BufferChange> {
    if (from.empty()) {
        return std::nullopt;
    }
    const auto previous_lines = lines_.size();
    to.push_back(edit_state());
    auto state = std::move(from.back());
    from.pop_back();
    lines_ = std::move(state.lines);
    cursor_ = state.cursor;
    selection_anchor_ = state.anchor;
    secondary_carets_ = std::move(state.secondary_carets);
    preferred_byte_ = state.preferred_byte;
    dirty_ = text() != saved_text_;
    error_.clear();
    ++revision_;
    return BufferChange{0, previous_lines, lines_.size()};
}

auto EditorBuffer::undo() -> std::optional<BufferChange> {
    return restore_edit(undo_, redo_);
}

auto EditorBuffer::redo() -> std::optional<BufferChange> {
    return restore_edit(redo_, undo_);
}

auto EditorBuffer::refresh_saved_text(std::string_view saved) -> std::optional<BufferChange> {
    if (dirty_) {
        return std::nullopt;
    }
    if (text() == saved) {
        return std::nullopt;
    }
    const auto change = replace({TextPosition{}, {lines_.size() - 1, lines_.back().size()}}, saved);
    saved_text_ = text();
    dirty_ = false;
    return change;
}

auto EditorBuffer::save() -> bool {
  if (!repository_path_) {
    error_ = "buffer is not backed by a liquid repository";
    return false;
  }
  try {
    auto worker = std::async(
        std::launch::async,
        [](std::filesystem::path path, std::string file,
           std::vector<std::byte> contents) {
          auto opened = liquid::runtime::block_on(
              liquid::repository::open(std::move(path)));
          if (!opened) {
            return std::pair{error_message(opened.error()), std::uint64_t{}};
          }
          auto written = liquid::runtime::block_on(
              opened->write(std::move(file), std::move(contents)));
          if (!written) {
            return std::pair{error_message(written.error()), std::uint64_t{}};
          }
          auto status = opened->status();
          if (!status) {
            return std::pair{error_message(status.error()), std::uint64_t{}};
          }
          return std::pair{std::string{}, status->pending_drips};
        },
        *repository_path_, blob_path_, bytes(text()));
    auto [error, pending_drips] = worker.get();
    error_ = std::move(error);
    if (!error_.empty()) {
      return false;
    }
    pending_drips_ = pending_drips;
    dirty_ = false;
    saved_text_ = text();
    return true;
  } catch (const std::exception &error) {
    error_ = error.what();
    return false;
  } catch (...) {
    error_ = "repository worker failed";
    return false;
  }
}

auto EditorBuffer::save(liquid::repository &repository) -> bool {
  auto written =
      liquid::runtime::block_on(repository.write(blob_path_, bytes(text())));
  if (!written) {
    error_ = error_message(written.error());
    return false;
  }
  auto status = repository.status();
  if (!status) {
    error_ = error_message(status.error());
    return false;
  }
  pending_drips_ = status->pending_drips;
  dirty_ = false;
  saved_text_ = text();
  error_.clear();
  return true;
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
