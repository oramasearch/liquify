#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;

// The application shell: one window, one renderer, one ImGui context, one frame
// loop. SDL3 and Dear ImGui appear only in app.cpp; nothing else in liquify
// includes them.
namespace liquify {

class RepositoryView;
class EditorBuffer;
class EditorGraph;
class TextLayoutCache;
class RetainedTextLayer;

struct AppOptions {
    std::string title = "liquify";
    int max_frames = 0; // 0 = run until the window is closed; N = render N frames and exit
    bool smoke = false;
    std::optional<std::filesystem::path> repository_path;
};

class App {
public:
    explicit App(AppOptions options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Create the window, renderer and UI context. False (with a message on
    // stderr) on failure, e.g. no display.
    bool init();
    // Run the frame loop; returns the process exit code.
    int run();

private:
    void frame();
    void draw_contents();
    void draw_repository_tree();
    void draw_repository_entries(std::string_view directory);
    void draw_editor();
    void render_editor();
    void open_editor(std::string path, std::string blob_id);
    void save_editor();
    void sync_editor_graph();
    void request_definition();
    void request_hover();
    void request_completion();
    void poll_editor_graph();
    void apply_change(std::size_t first_line, std::size_t erased_lines, std::size_t inserted_lines);
    void handle_key(std::uint32_t key, std::uint16_t modifiers);
    void handle_text(std::string_view text);
    void position_cursor(float x, float y, bool extend_selection);
    void ensure_cursor_visible();
    void prepare_smoke_frame(int frame);
    void print_smoke_metrics() const;
    void shutdown();

    AppOptions options_;
    std::unique_ptr<RepositoryView> repository_;
    std::unique_ptr<EditorBuffer> editor_;
    std::unique_ptr<EditorGraph> editor_graph_;
    std::unique_ptr<TextLayoutCache> layouts_;
    std::unique_ptr<RetainedTextLayer> text_layer_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    float editor_x_{};
    float editor_y_{};
    float editor_width_{};
    float editor_height_{};
    float line_height_{};
    float cell_width_{};
    std::size_t scroll_line_{};
    float horizontal_scroll_{};
    std::size_t visible_lines_{};
    bool editor_focused_{};
    bool editor_visible_{};
    bool analysis_buffer_open_{};
    std::string analysis_text_;
    std::string editor_status_;
    std::vector<double> frame_times_ms_;
    std::vector<double> typing_times_ms_;
    std::optional<std::chrono::steady_clock::time_point> typing_started_;
    std::chrono::steady_clock::time_point next_status_refresh_{};
    std::uint64_t definition_generation_{};
    float display_refresh_hz_{};
    bool initialized_ = false;
    bool quit_ = false;
};

} // namespace liquify
