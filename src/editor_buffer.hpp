// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquify/editor_model.hpp"
#include "liquify/interactive_analysis.hpp"

namespace liquid {
class repository;
}

namespace liquify {

struct BufferChange {
  std::size_t first_line{};
  std::size_t erased_lines{};
  std::size_t inserted_lines{};
};

enum class EditingMode {
  standard,
  modal_command,
  modal_insert,
};

class EditorBuffer {
public:
  [[nodiscard]] static EditorBuffer open(std::filesystem::path repository_path,
                                         std::string blob_path,
                                         std::string blob_id);
  [[nodiscard]] static EditorBuffer
  from_text(std::string blob_id, std::string display_path, std::string text);
  [[nodiscard]] static EditorBuffer
  from_repository_text(std::filesystem::path repository_path,
                       std::string blob_path, std::string text);

  [[nodiscard]] auto blob_id() const -> std::string_view;
  [[nodiscard]] auto display_path() const -> std::string_view;
  [[nodiscard]] auto line_count() const -> std::size_t;
  [[nodiscard]] auto line(std::size_t index) const -> std::string_view;
  [[nodiscard]] auto lines() const -> std::span<const std::string>;
  [[nodiscard]] auto text() const -> std::string;
  [[nodiscard]] auto cursor() const -> TextPosition;
  [[nodiscard]] auto cursors() const -> std::vector<TextPosition>;
  [[nodiscard]] auto selection() const -> std::optional<TextSelection>;
  [[nodiscard]] auto selections() const -> std::vector<TextSelection>;
  [[nodiscard]] auto preferences() const -> const EditorPreferences&;
  [[nodiscard]] auto mode() const -> EditingMode;
  [[nodiscard]] auto dirty() const -> bool;
  [[nodiscard]] auto error() const -> std::string_view;
  [[nodiscard]] auto revision() const -> std::uint64_t;
  [[nodiscard]] auto byte_offset(TextPosition position) const -> std::size_t;
  [[nodiscard]] auto pending_drips() const -> std::uint64_t;

  void set_mode(EditingMode mode);
  void set_cursor(TextPosition position, bool extend_selection = false);
  void add_cursor(TextPosition position);
  void clear_secondary_cursors();
  void select_all();
  void select(SelectionKind kind);
  void select_column(TextPosition anchor, TextPosition cursor);
  void set_preferences(EditorPreferences preferences);
  void move_left(bool extend_selection = false);
  void move_right(bool extend_selection = false);
  void move_up(bool extend_selection = false);
  void move_down(bool extend_selection = false);
  void move_home(bool extend_selection = false);
  void move_end(bool extend_selection = false);

  [[nodiscard]] auto insert_text(std::string_view text)
      -> std::optional<BufferChange>;
  [[nodiscard]] auto insert_newline() -> BufferChange;
  [[nodiscard]] auto backspace() -> std::optional<BufferChange>;
  [[nodiscard]] auto delete_forward() -> std::optional<BufferChange>;
  [[nodiscard]] auto apply_edit(const SourceEdit &edit)
      -> std::optional<BufferChange>;
  [[nodiscard]] auto find(std::string_view query, FindOptions options = {}) const
      -> std::vector<TextMatch>;
  [[nodiscard]] auto replace_all(std::string_view query,
                                 std::string_view replacement,
                                 FindOptions options = {}) -> std::optional<BufferChange>;
  [[nodiscard]] auto save() -> bool;
  [[nodiscard]] auto save(liquid::repository &repository) -> bool;
  [[nodiscard]] auto undo() -> std::optional<BufferChange>;
  [[nodiscard]] auto redo() -> std::optional<BufferChange>;
  [[nodiscard]] auto refresh_saved_text(std::string_view text) -> std::optional<BufferChange>;

  private:
  [[nodiscard]] auto replace(TextSelection range, std::string_view text)
      -> BufferChange;
  [[nodiscard]] auto replace_many(std::vector<TextSelection> ranges, std::string_view text)
      -> BufferChange;
  [[nodiscard]] auto insertion_range() const -> TextSelection;
  [[nodiscard]] auto clamped(TextPosition position) const -> TextPosition;
  [[nodiscard]] auto position_for_byte(std::size_t byte) const
      -> std::optional<TextPosition>;
  void prepare_movement(bool extend_selection);
  void finish_movement(bool extend_selection);

  std::optional<std::filesystem::path> repository_path_;
  std::string blob_path_;
  std::string blob_id_;
  std::string display_path_;
  std::vector<std::string> lines_{1};
  TextPosition cursor_{};
  std::optional<TextPosition> selection_anchor_;
  struct SecondaryCaret {
      TextPosition position;
      std::optional<TextPosition> anchor;
  };
  struct EditState {
      std::vector<std::string> lines;
      TextPosition cursor;
      std::optional<TextPosition> anchor;
      std::vector<SecondaryCaret> secondary_carets;
      std::size_t preferred_byte;
  };
  [[nodiscard]] auto edit_state() const -> EditState;
  void remember_edit();
  [[nodiscard]] auto restore_edit(std::vector<EditState>& from, std::vector<EditState>& to)
      -> std::optional<BufferChange>;
  std::vector<EditState> undo_;
  std::vector<EditState> redo_;
  std::string saved_text_;
  std::vector<SecondaryCaret> secondary_carets_;
  std::size_t preferred_byte_{};
  EditorPreferences preferences_{};
  EditingMode mode_{EditingMode::standard};
  bool dirty_{};
  std::string error_;
  std::uint64_t revision_{};
  std::uint64_t pending_drips_{};
};

[[nodiscard]] auto editing_mode_name(EditingMode mode) -> std::string_view;

} // namespace liquify
