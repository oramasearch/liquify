#pragma once
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "liquify/app_state.hpp"
#include "liquify/command.hpp"
#include "liquify/editor_model.hpp"
#include "liquify/first_run.hpp"
#include "liquify/input.hpp"
#include "liquify/multiplayer.hpp"
#include "liquify/navigation.hpp"
#include "liquify/plugin.hpp"
#include "liquify/redraw.hpp"
#include "liquify/review.hpp"
#include "liquify/settings.hpp"
#include "liquify/structural_edit.hpp"
#include "liquify/ui_layout.hpp"

struct SDL_Window;
struct SDL_Renderer;

// The application shell: one window, one renderer, one ImGui context, one frame
// loop. SDL3 and Dear ImGui appear only in app.cpp; nothing else in liquify
// includes them.
namespace liquify {

class Assistant;
enum class AssistantAction : std::uint8_t;
class RepositoryView;
class EditorBuffer;
class EditorGraph;
class TextLayoutCache;
class RetainedTextLayer;
struct EditorGraphSnapshot;

struct AppOptions {
  std::string title = "liquify";
  int max_frames =
      0; // 0 = run until the window is closed; N = render N frames and exit
  bool smoke = false;
  std::optional<std::filesystem::path> repository_path;
  std::optional<std::string> lake_url;
  std::optional<std::string> screenshot_state;
  std::optional<std::filesystem::path> screenshot_output;
  std::optional<ThemePreference> forced_theme;
  std::filesystem::path executable_path;
};

class App {
public:
  explicit App(AppOptions options);
  ~App();
  App(const App &) = delete;
  App &operator=(const App &) = delete;

  // Create the window, renderer and UI context. False (with a message on
  // stderr) on failure, e.g. no display.
  bool init();
  // Run the frame loop; returns the process exit code.
  int run();
  [[nodiscard]] auto diagnostics_viewport() const -> InputRectangle;
  [[nodiscard]] auto frame_layout() const -> const UiFrameLayout &;

private:
  friend class NativeBufferRegression;
  friend class NativeWindowTest;
  friend class NativeAcceptance;
  bool init_window(bool high_pixel_density);
  void frame(bool present = true);
  void draw_contents();
  void draw_activity_bar();
  void draw_sidebar();
  void draw_editor_area();
  void draw_bottom_panel();
  void draw_floating_panels();
  void draw_panel(PanelId id);
  void draw_panel_header(PanelId id);
  void draw_panel_menu(PanelId id);
  [[nodiscard]] auto panel_label(PanelId id) const -> std::string;
  void draw_diagnostics_panel(const EditorGraphSnapshot &snapshot);
  void draw_repository_details(const EditorGraphSnapshot &snapshot);
  void draw_multiplayer_details();
  void draw_line_attribution(std::size_t line);
  void draw_status_bar();
  void draw_welcome();
  void draw_user_error(const UserError &failure, std::string_view id);
  void draw_clone_dialog();
  void draw_split_editor();
  void draw_repository_tree();
  void draw_repository_entries(std::string_view directory);
  void draw_navigation_popups();
  void draw_repository_action_popup();
  void draw_navigation_results();
  void draw_review_panel();
  void draw_review_comment_popup();
  void draw_editor();
  void draw_editor_toolbar();
  void draw_editor_modes();
  void draw_build_panel(const EditorGraphSnapshot &snapshot);
  void render_editor(InputRectangle clip);
  void open_editor(std::string path, std::string blob_id,
                   bool record_history = true);
  void open_navigation_location(const NavigationLocation &location,
                                bool record_history = true);
  void navigate_history(bool forward);
  void invoke_navigation_action(NavigationAction action);
  void show_symbol_references();
  void show_call_hierarchy();
  void reveal_repository_path();
  [[nodiscard]] auto current_navigation_location() const
      -> std::optional<NavigationLocation>;
  [[nodiscard]] auto navigation_symbol_at_cursor() const
      -> const NavigationSymbol *;
  void open_review(std::string id);
  void show_review_diff(const ReviewSnapshot &review);
  void navigate_hunk(bool forward);
  void navigate_comment();
  void begin_review_comment();
  void submit_review_comment();
  void poll_review();
  void save_editor();
  bool save_buffer(EditorBuffer &buffer);
  void undo_editor(bool redo);
  void close_editor();
  void request_quit();
  bool confirm_discard(std::string_view action, bool active_only = false);
  void remember_editor_view();
  void refresh_saved_buffer(const std::string &path);
  void sync_editor_graph();
  void request_definition();
  void request_hover();
  void request_completion();
  void request_build();
  void request_tests(std::string filter = {});
  void request_deploy();
  void expand_structural_selection();
  void shrink_structural_selection();
  void select_enclosing_definition();
  void select_symbol_references();
  void open_rename();
  void open_extract_function();
  void apply_structural_preview(bool all_files);
  void draw_structural_editing();
  void request_assistant_context(std::optional<AssistantAction> action);
  void accept_assistant_edit();
  void toggle_assistant();
  void draw_assistant();
  void draw_command_palette();
  void draw_settings();
  void configure_commands();
  bool invoke_command(std::string_view name);
  void draw_plugin_panels();
  void draw_structural_panel();
  void invoke_plugin_command(std::string_view name);
  void execute_panel_command(std::string_view command);
  void poll_plugins();
  void sync_panel_system();
  void reset_panel_layout();
  void poll_multiplayer();
  void refresh_multiplayer();
  void take_editor_lease();
  void release_editor_lease();
  [[nodiscard]] bool editor_mutation_allowed() const;
  void poll_editor_graph();
  void apply_change(std::size_t first_line, std::size_t erased_lines,
                    std::size_t inserted_lines);
  void handle_key(std::uint32_t key, std::uint16_t modifiers);
  void handle_text(std::string_view text);
  void handle_composition(std::string_view text, int selection_start,
                          int selection_length);
  void copy_editor_selection(bool cut);
  void paste_editor_clipboard();
  void handle_file_drop(const std::filesystem::path &source, InputPoint point);
  void draw_composition();
  void update_text_input_area();
  void position_cursor(float x, float y, bool extend_selection);
  void ensure_cursor_visible();
  void prepare_smoke_frame(int frame);
  void print_smoke_metrics() const;
  void persist_shell_state();
  void persist_settings();
  void apply_settings_live(bool rebuild_fonts = false);
  void reset_settings(SettingsGroup group);
  void cycle_theme();
  void toggle_high_contrast();
  void apply_theme();
  void load_font_atlas(float scale);
  bool upload_font_atlas();
  bool fail_rendering(std::string message);
  void update_window_title();
  void begin_open_repository(std::filesystem::path path);
  void begin_clone_repository(std::string remote, std::filesystem::path path);
  void poll_repository_lifecycle();
  bool activate_repository(std::unique_ptr<RepositoryView> repository);
  void show_folder_dialog();
  void open_new_window(std::optional<std::filesystem::path> repository = {});
  void begin_sign_in();
  void poll_sign_in();
  void persist_first_run();
  [[nodiscard]] auto repository_summary() const -> RepositorySummary;
  void capture_screenshot();
  [[nodiscard]] auto dark_theme_active() const -> bool;
  [[nodiscard]] auto high_contrast_active() const -> bool;
  [[nodiscard]] auto screenshot_mode() const -> bool;
  void shutdown();

  // Ask the run loop for a frame. Safe to call from any thread: it wakes a
  // blocked SDL_WaitEventTimeout. See docs/ui/redraw.md.
  void request_repaint();
  // Whether the loop may sleep, and for how long.
  [[nodiscard]] auto redraw_inputs() const -> RedrawInputs;
  // True while the interface is mid-interaction or mid-animation.
  [[nodiscard]] auto interface_is_busy() const -> bool;
  // True while any polled future is outstanding.
  [[nodiscard]] auto async_work_in_flight() const -> bool;
  // Time until the nearest scheduled refresh.
  [[nodiscard]] auto until_next_scheduled_refresh() const
      -> std::chrono::steady_clock::duration;

  AppOptions options_;
  std::atomic<bool> repaint_requested_{true};
  FirstRunState first_run_;
  std::filesystem::path first_run_state_path_;
  std::unique_ptr<RepositoryView> repository_;
  std::optional<std::future<std::unique_ptr<RepositoryView>>>
      repository_operation_;
  std::optional<std::future<IdentitySession>> identity_operation_;
  std::optional<std::string> pending_lake_url_;
  struct OpenBuffer {
    std::unique_ptr<EditorBuffer> buffer;
    std::size_t scroll_line{};
    float horizontal_scroll{};
  };
  std::map<std::string, OpenBuffer> open_buffers_;
  std::unique_ptr<EditorBuffer> transient_editor_;
  EditorBuffer *editor_{};
  std::unique_ptr<EditorGraph> editor_graph_;
  std::unique_ptr<ReviewController> review_;
  std::unique_ptr<Assistant> assistant_;
  std::unique_ptr<CommandRegistry> commands_;
  std::shared_ptr<PluginHost> plugin_host_;
  std::optional<MultiplayerSnapshot> multiplayer_;
  std::future<liquid::result<MultiplayerSnapshot>> multiplayer_refresh_;
  liquid::session_id multiplayer_session_;
  std::unique_ptr<TextLayoutCache> layouts_;
  std::unique_ptr<RetainedTextLayer> text_layer_;
  AppShellState shell_state_;
  InputState input_;
  PanelRegistry panels_;
  Settings settings_;
  SettingsScope settings_scope_{SettingsScope::user};
  std::vector<SettingsDiagnostic> settings_diagnostics_;
  SDL_Window *window_ = nullptr;
  SDL_Renderer *renderer_ = nullptr;
  float editor_x_{};
  float editor_y_{};
  float editor_width_{};
  float editor_height_{};
  UiFrameLayout frame_layout_;
  float line_height_{};
  float cell_width_{};
  InputRectangle repository_drop_rectangle_;
  InputRectangle editor_drop_rectangle_;
  std::size_t scroll_line_{};
  float horizontal_scroll_{};
  std::size_t visible_lines_{};
  bool editor_focused_{};
  bool editor_visible_{};
  bool analysis_buffer_open_{};
  std::string analysis_text_;
  std::string editor_status_;
  std::string assistant_prompt_;
  std::string sidebar_search_;
  std::string find_query_;
  std::string replace_query_;
  std::string settings_query_;
  std::string lake_account_name_;
  std::string lake_account_url_;
  std::string provider_token_input_;
  std::string lake_token_input_;
  FindOptions find_options_;
  std::vector<TextMatch> buffer_matches_;
  std::size_t find_match_index_{};
  std::optional<TextPosition> column_selection_anchor_;
  std::string quick_open_query_;
  std::string symbol_search_query_;
  std::string repository_selection_;
  std::string repository_selection_blob_;
  std::string repository_action_source_;
  std::string repository_action_target_;
  std::string navigation_results_title_;
  std::string window_title_;
  std::string clone_remote_;
  std::string clone_path_;
  std::string identity_error_;
  std::optional<AssistantAction> pending_assistant_action_;
  std::string pending_assistant_prompt_;
  std::string context_preview_;
  std::vector<std::string> context_omissions_;
  std::vector<double> frame_times_ms_;
  std::vector<double> typing_times_ms_;
  std::vector<ReviewDiffLine> review_diff_lines_;
  std::string displayed_review_head_;
  std::string pending_review_id_;
  std::string review_comment_body_;
  std::vector<PluginLoadFailure> plugin_failures_;
  std::vector<PluginPanelSnapshot> plugin_panels_;
  std::vector<std::string> command_diagnostics_;
  std::vector<std::string> native_menu_commands_;
  std::vector<NavigationSymbol> navigation_results_;
  CallHierarchy navigation_hierarchy_;
  NavigationHistory navigation_history_;
  std::optional<NavigationLocation> pending_definition_origin_;
  std::optional<std::chrono::steady_clock::time_point> typing_started_;
  std::chrono::steady_clock::time_point next_status_refresh_{};
  std::chrono::steady_clock::time_point next_review_refresh_{};
  std::chrono::steady_clock::time_point next_plugin_refresh_{};
  std::chrono::steady_clock::time_point next_multiplayer_refresh_{};
  std::uint64_t definition_generation_{};
  std::uint64_t history_generation_{};
  std::uint64_t ai_context_generation_{};
  std::uint64_t review_generation_{};
  std::uint64_t structural_selection_generation_{};
  std::uint64_t structural_references_generation_{};
  std::uint64_t structural_preview_generation_{};
  std::uint64_t structural_result_generation_{};
  std::vector<SourceRange> structural_selection_history_;
  std::vector<SourceLocation> structural_references_;
  std::vector<StructuralEdit> structural_preview_;
  std::vector<std::string> structural_diagnostics_;
  std::vector<std::string> structural_unresolved_;
  std::vector<std::string> structural_accepted_files_;
  std::string structural_name_;
  std::string command_palette_query_;
  std::string command_palette_previous_query_;
  std::size_t command_palette_selection_{};
  std::size_t next_comment_index_{};
  std::size_t ai_context_bytes_{64 * 1024};
  float display_refresh_hz_{};
  float display_scale_ = 1.0F;
  int ui_font_index_ = -1;
  int editor_font_index_ = -1;
  int heading_font_index_ = -1;
  bool applied_dark_theme_{};
  bool applied_high_contrast_{};
  bool reduced_motion_{};
  bool screenshot_written_{};
  bool screenshot_failed_{};
  bool rendering_failed_{};
  bool fonts_rebuild_requested_{};
  int screenshot_frames_rendered_{};
  bool initialized_ = false;
  bool command_palette_open_{};
  bool command_palette_visible_{};
  bool settings_open_{};
  bool settings_popup_visible_{};
  bool find_open_{};
  bool repository_details_open_{};
  std::optional<std::size_t> attribution_line_;
  bool quick_open_open_{};
  bool symbol_search_open_{};
  bool open_recent_open_{};
  bool navigation_results_open_{};
  bool repository_move_open_{};
  bool repository_new_file_open_{};
  bool breadcrumb_focus_requested_{};
  bool repository_focused_{};
  bool context_preview_open_{};
  bool review_diff_open_{};
  bool review_comment_popup_open_{};
  bool rename_open_{};
  bool extract_function_open_{};
  bool structural_preview_open_{};
  bool build_was_running_{};
  bool sync_was_running_{};
  bool hydration_was_running_{};
  bool assistant_was_running_{};
  // Which error components show liquid's raw message, by component id.
  std::map<std::string, bool, std::less<>> error_details_open_;
  bool clone_dialog_open_{};
  bool folder_dialog_open_{};
  bool repository_operation_is_clone_{};
  bool quit_ = false;
};

} // namespace liquify
