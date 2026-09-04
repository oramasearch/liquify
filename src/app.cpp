// The one translation unit that knows SDL3 and Dear ImGui. Init/loop shape follows
// Zetta's headed mode (src/gui/app.cpp in the Zetta bridge): SDL_Renderer backend, vsync,
// window sized to 70% of the usable display.
#include "liquify/app.hpp"

#include <cstdio>
#include <string>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>

#include "liquid/liquid.hpp"
#include "liquify/version.hpp"
#include "repository_view.hpp"

namespace liquify {

App::App(AppOptions options) : options_(std::move(options)) {
    if (options_.repository_path) {
        repository_ =
            std::make_unique<RepositoryView>(RepositoryView::open(*options_.repository_path));
    }
}

App::~App() {
    shutdown();
}

bool App::init() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "liquify: SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }
    int win_w = 1280, win_h = 800;
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 &&
        usable.h > 0) {
        win_w = static_cast<int>(usable.w * 0.70f);
        win_h = static_cast<int>(usable.h * 0.70f);
        if (win_w < 900)
            win_w = 900;
        if (win_h < 600)
            win_h = 600;
    }
    const std::string title = options_.title + " " + std::string(version_string());
    window_ = SDL_CreateWindow(
        title.c_str(), win_w, win_h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window_ == nullptr) {
        std::fprintf(stderr, "liquify: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }
    renderer_ = SDL_CreateRenderer(window_, nullptr);
    if (renderer_ == nullptr) {
        std::fprintf(stderr, "liquify: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        SDL_Quit();
        return false;
    }
    SDL_SetRenderVSync(renderer_, 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // no imgui.ini next to the binary
    ImGui::StyleColorsLight();
    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
    initialized_ = true;
    return true;
}

void App::frame() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("liquify",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    if (!repository_) {
        ImGui::TextUnformatted("hello from liquify");
        ImGui::Text("version %s", std::string(version_string()).c_str());
        ImGui::Separator();
        ImGui::Text("libliquid %s linked", std::string(liquid::version_string()).c_str());
        ImGui::TextUnformatted("The IDE for liquid: instant open at any size, diagnostics from the "
                               "build graph, drips.");
    } else {
        const auto path = options_.repository_path->string();
        ImGui::Text("Repository: %s", path.c_str());
        ImGui::Separator();
        if (!repository_->error().empty()) {
            ImGui::TextWrapped("Unable to open repository: %s",
                               std::string(repository_->error()).c_str());
        } else if (ImGui::BeginTable(
                       "repository-tree", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Kind");
            ImGui::TableSetupColumn("Mode");
            ImGui::TableHeadersRow();
            for (const auto& entry : repository_->entries()) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(node_kind_name(entry.kind).data());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%06o", static_cast<unsigned int>(entry.mode));
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_, 250, 250, 248, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_RenderPresent(renderer_);
}

int App::run() {
    if (!initialized_)
        return 1;
    int frames = 0;
    while (!quit_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                quit_ = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window_))
                quit_ = true;
        }
        frame();
        ++frames;
        if (options_.max_frames > 0 && frames >= options_.max_frames)
            quit_ = true;
    }
    return 0;
}

void App::shutdown() {
    if (!initialized_)
        return;
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (renderer_)
        SDL_DestroyRenderer(renderer_);
    if (window_)
        SDL_DestroyWindow(window_);
    SDL_Quit();
    renderer_ = nullptr;
    window_ = nullptr;
    initialized_ = false;
}

} // namespace liquify
