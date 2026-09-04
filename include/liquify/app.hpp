#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

struct SDL_Window;
struct SDL_Renderer;

// The application shell: one window, one renderer, one ImGui context, one frame loop.
// SDL3 and Dear ImGui appear only in app.cpp; nothing else in liquify includes them.
namespace liquify {

class RepositoryView;

struct AppOptions {
    std::string title = "liquify";
    int max_frames = 0; // 0 = run until the window is closed; N = render N frames and exit
    std::optional<std::filesystem::path> repository_path;
};

class App {
public:
    explicit App(AppOptions options);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Create the window, renderer and UI context. False (with a message on stderr) on failure,
    // e.g. no display.
    bool init();
    // Run the frame loop; returns the process exit code.
    int run();

private:
    void frame();
    void shutdown();

    AppOptions options_;
    std::unique_ptr<RepositoryView> repository_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool initialized_ = false;
    bool quit_ = false;
};

} // namespace liquify
