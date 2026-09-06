// The one translation unit that knows SDL3 and Dear ImGui. The shell uses
// ImGui; the editor submits retained line geometry directly through
// SDL_Renderer with ImGui's GPU glyph atlas.
#include "liquify/app.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <span>
#include <string>
#include <utility>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "misc/cpp/imgui_stdlib.h"
#include <SDL3/SDL.h>

#include "editor_buffer.hpp"
#include "liquid/hash.hpp"
#include "liquid/liquid.hpp"
#include "liquify/ai_config.hpp"
#include "liquify/assistant.hpp"
#include "liquify/editor_graph.hpp"
#include "liquify/stub_tree.hpp"
#include "liquify/version.hpp"
#include "repository_view.hpp"
#include "text_layout.hpp"

namespace liquify {
namespace {

constexpr float editor_padding = 8.0F;

auto smoke_text() -> std::string {
    constexpr std::size_t line_count = 50'000;
    std::string text;
    text.reserve(line_count * 52);
    for (std::size_t line = 0; line < line_count; ++line) {
        text += "line ";
        text += std::to_string(line + 1);
        text += "  retained GPU text layer smoke content";
        if (line + 1 < line_count) {
            text.push_back('\n');
        }
    }
    return text;
}

auto percentile(std::vector<double> values, double fraction) -> double {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto index =
        static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(values.size())) - 1.0);
    return values[std::min(index, values.size() - 1)];
}

auto average(const std::vector<double>& values) -> double {
    if (values.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const auto value : values) {
        total += value;
    }
    return total / static_cast<double>(values.size());
}

auto source_edit_between(std::string_view before, std::string_view after)
    -> std::optional<SourceEdit> {
    if (before == after) {
        return std::nullopt;
    }
    auto first = std::size_t{};
    while (first < before.size() && first < after.size() && before[first] == after[first]) {
        ++first;
    }
    auto before_end = before.size();
    auto after_end = after.size();
    while (before_end > first && after_end > first &&
           before[before_end - 1] == after[after_end - 1]) {
        --before_end;
        --after_end;
    }
    if (first > std::numeric_limits<std::uint32_t>::max() ||
        before_end > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return SourceEdit{static_cast<std::uint32_t>(first),
                      static_cast<std::uint32_t>(before_end),
                      std::string(after.substr(first, after_end - first))};
}

} // namespace

class RetainedTextLayer {
public:
    void reset(std::size_t line_count) {
        lines_.clear();
        lines_.resize(line_count);
    }

    void apply_change(BufferChange change) {
        const auto first = lines_.begin() + static_cast<std::ptrdiff_t>(change.first_line);
        const auto last = first + static_cast<std::ptrdiff_t>(change.erased_lines);
        const auto insertion = lines_.erase(first, last);
        lines_.insert(insertion, change.inserted_lines, std::nullopt);
    }

    auto render(SDL_Renderer* renderer,
                EditorBuffer& buffer,
                TextLayoutCache& layouts,
                std::span<const Diagnostic> diagnostics,
                float x,
                float y,
                float width,
                float height,
                std::size_t first_line,
                float line_height,
                float cell_width,
                float horizontal_scroll,
                std::span<const ReviewDiffLine> review_lines,
                std::span<const std::size_t> comment_lines) -> std::size_t {
        if (cell_width_ != cell_width || font_size_ != ImGui::GetFontSize()) {
            cell_width_ = cell_width;
            font_size_ = ImGui::GetFontSize();
            for (auto& line : lines_) {
                line.reset();
            }
        }

        const auto available_lines = static_cast<std::size_t>(std::ceil(height / line_height)) + 1;
        const auto last_line = std::min(buffer.line_count(), first_line + available_lines);
        draw_review_backgrounds(
            renderer, review_lines, x, y, width, first_line, last_line, line_height);
        draw_selection(renderer,
                       buffer,
                       layouts,
                       x,
                       y,
                       width,
                       first_line,
                       last_line,
                       line_height,
                       cell_width,
                       horizontal_scroll);

        auto* texture = reinterpret_cast<SDL_Texture*>(ImGui::GetIO().Fonts->TexID);
        auto* font = ImGui::GetFont();
        frame_vertices_.clear();
        frame_indices_.clear();
        for (std::size_t line = first_line; line < last_line; ++line) {
            const auto& layout = layouts.line(line, buffer.line(line));
            auto& retained = lines_.at(line);
            if (!retained || retained->generation != layout.generation) {
                retained = build_geometry(layout, *font, cell_width);
            }
            append_line(*retained,
                        x + editor_padding - horizontal_scroll,
                        y + static_cast<float>(line - first_line) * line_height);
        }
        if (!frame_vertices_.empty()) {
            SDL_RenderGeometry(renderer,
                               texture,
                               frame_vertices_.data(),
                               static_cast<int>(frame_vertices_.size()),
                               frame_indices_.data(),
                               static_cast<int>(frame_indices_.size()));
        }

        draw_diagnostics(renderer,
                         buffer,
                         diagnostics,
                         x,
                         y,
                         first_line,
                         last_line,
                         line_height,
                         cell_width,
                         horizontal_scroll);
        draw_review_comments(renderer, comment_lines, x, y, first_line, last_line, line_height);
        draw_cursor(renderer,
                    buffer,
                    layouts,
                    x,
                    y,
                    first_line,
                    last_line,
                    line_height,
                    cell_width,
                    horizontal_scroll);
        return last_line - first_line;
    }

private:
    struct LineGeometry {
        std::uint64_t generation{};
        std::vector<SDL_Vertex> vertices;
        std::vector<int> indices;
    };

    static auto build_geometry(const LineLayout& layout, ImFont& font, float cell_width)
        -> LineGeometry {
        LineGeometry geometry;
        geometry.generation = layout.generation;
        geometry.vertices.reserve(layout.glyphs.size() * 4);
        geometry.indices.reserve(layout.glyphs.size() * 6);
        const SDL_FColor color{0.10F, 0.12F, 0.14F, 1.0F};
        for (const auto& placed : layout.glyphs) {
            if (placed.codepoint == '\t') {
                continue;
            }
            const auto* glyph = font.FindGlyph(placed.codepoint);
            if (glyph == nullptr || !glyph->Visible) {
                continue;
            }
            const auto first = static_cast<int>(geometry.vertices.size());
            const auto origin = placed.column * cell_width;
            geometry.vertices.push_back(
                {{origin + glyph->X0, glyph->Y0}, color, {glyph->U0, glyph->V0}});
            geometry.vertices.push_back(
                {{origin + glyph->X1, glyph->Y0}, color, {glyph->U1, glyph->V0}});
            geometry.vertices.push_back(
                {{origin + glyph->X1, glyph->Y1}, color, {glyph->U1, glyph->V1}});
            geometry.vertices.push_back(
                {{origin + glyph->X0, glyph->Y1}, color, {glyph->U0, glyph->V1}});
            geometry.indices.insert(geometry.indices.end(),
                                    {first, first + 1, first + 2, first, first + 2, first + 3});
        }
        return geometry;
    }

    void append_line(const LineGeometry& geometry, float x, float y) {
        const auto first = static_cast<int>(frame_vertices_.size());
        for (const auto& retained : geometry.vertices) {
            auto vertex = retained;
            vertex.position.x += x;
            vertex.position.y += y;
            frame_vertices_.push_back(vertex);
        }
        for (const auto index : geometry.indices) {
            frame_indices_.push_back(first + index);
        }
    }

    static void draw_selection(SDL_Renderer* renderer,
                               EditorBuffer& buffer,
                               TextLayoutCache& layouts,
                               float x,
                               float y,
                               float width,
                               std::size_t first_line,
                               std::size_t last_line,
                               float line_height,
                               float cell_width,
                               float horizontal_scroll) {
        const auto selection = buffer.selection();
        if (!selection) {
            return;
        }
        SDL_SetRenderDrawColor(renderer, 176, 207, 255, 255);
        const auto begin_line = std::max(first_line, selection->begin.line);
        const auto end_line = std::min(last_line, selection->end.line + 1);
        for (std::size_t line = begin_line; line < end_line; ++line) {
            const auto& layout = layouts.line(line, buffer.line(line));
            const auto first_byte = line == selection->begin.line ? selection->begin.byte : 0;
            const auto last_byte =
                line == selection->end.line ? selection->end.byte : buffer.line(line).size();
            const auto first_column = layout.column_for_byte(first_byte);
            auto last_column = layout.column_for_byte(last_byte);
            if (line < selection->end.line) {
                last_column += 1.0F;
            }
            const SDL_FRect rectangle{x + editor_padding + first_column * cell_width -
                                          horizontal_scroll,
                                      y + static_cast<float>(line - first_line) * line_height,
                                      std::min((last_column - first_column) * cell_width, width),
                                      line_height};
            SDL_RenderFillRect(renderer, &rectangle);
        }
    }

    static void draw_cursor(SDL_Renderer* renderer,
                            EditorBuffer& buffer,
                            TextLayoutCache& layouts,
                            float x,
                            float y,
                            std::size_t first_line,
                            std::size_t last_line,
                            float line_height,
                            float cell_width,
                            float horizontal_scroll) {
        const auto cursor = buffer.cursor();
        if (cursor.line < first_line || cursor.line >= last_line) {
            return;
        }
        const auto& layout = layouts.line(cursor.line, buffer.line(cursor.line));
        const auto column = layout.column_for_byte(cursor.byte);
        const auto width = buffer.mode() == EditingMode::modal_command ? cell_width : 1.5F;
        SDL_SetRenderDrawColor(renderer, 28, 35, 43, 255);
        const SDL_FRect rectangle{x + editor_padding + column * cell_width - horizontal_scroll,
                                  y + static_cast<float>(cursor.line - first_line) * line_height,
                                  width,
                                  line_height};
        SDL_RenderFillRect(renderer, &rectangle);
    }

    static void draw_review_backgrounds(SDL_Renderer* renderer,
                                        std::span<const ReviewDiffLine> review_lines,
                                        float x,
                                        float y,
                                        float width,
                                        std::size_t first_line,
                                        std::size_t last_line,
                                        float line_height) {
        if (review_lines.empty()) {
            return;
        }
        for (auto line = first_line; line < last_line && line < review_lines.size(); ++line) {
            const auto kind = review_lines[line].kind;
            if (kind == ReviewDiffLineKind::added) {
                SDL_SetRenderDrawColor(renderer, 224, 246, 226, 255);
            } else if (kind == ReviewDiffLineKind::removed) {
                SDL_SetRenderDrawColor(renderer, 252, 228, 228, 255);
            } else if (kind == ReviewDiffLineKind::conflict) {
                SDL_SetRenderDrawColor(renderer, 255, 235, 196, 255);
            } else if (kind == ReviewDiffLineKind::file || kind == ReviewDiffLineKind::hunk) {
                SDL_SetRenderDrawColor(renderer, 228, 235, 245, 255);
            } else {
                continue;
            }
            const SDL_FRect rectangle{
                x, y + static_cast<float>(line - first_line) * line_height, width, line_height};
            SDL_RenderFillRect(renderer, &rectangle);
        }
    }

    static void draw_review_comments(SDL_Renderer* renderer,
                                     std::span<const std::size_t> comment_lines,
                                     float x,
                                     float y,
                                     std::size_t first_line,
                                     std::size_t last_line,
                                     float line_height) {
        SDL_SetRenderDrawColor(renderer, 99, 74, 176, 255);
        for (const auto line : comment_lines) {
            if (line < first_line || line >= last_line) {
                continue;
            }
            const SDL_FRect marker{x + 1.0F,
                                   y + static_cast<float>(line - first_line) * line_height + 2.0F,
                                   4.0F,
                                   std::max(2.0F, line_height - 4.0F)};
            SDL_RenderFillRect(renderer, &marker);
        }
    }

    static void draw_diagnostics(SDL_Renderer* renderer,
                                 EditorBuffer& buffer,
                                 std::span<const Diagnostic> diagnostics,
                                 float x,
                                 float y,
                                 std::size_t first_line,
                                 std::size_t last_line,
                                 float line_height,
                                 float cell_width,
                                 float horizontal_scroll) {
        for (const auto& diagnostic : diagnostics) {
            if (diagnostic.location.path.generic_string() != buffer.display_path()) {
                continue;
            }
            const auto& range = diagnostic.location.range;
            const auto begin_line =
                std::max(first_line, static_cast<std::size_t>(range.start_line));
            const auto end_line = std::min(last_line, static_cast<std::size_t>(range.end_line) + 1);
            if (diagnostic.severity == DiagnosticSeverity::error) {
                SDL_SetRenderDrawColor(renderer, 205, 46, 46, 255);
            } else if (diagnostic.severity == DiagnosticSeverity::warning) {
                SDL_SetRenderDrawColor(renderer, 204, 128, 0, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 52, 101, 164, 255);
            }
            for (auto line = begin_line; line < end_line; ++line) {
                const auto first_column = line == range.start_line ? range.start_column : 0U;
                auto last_column = line == range.end_line
                                       ? range.end_column
                                       : static_cast<std::uint32_t>(buffer.line(line).size());
                last_column = std::max(last_column, first_column + 1);
                const auto start_x = x + editor_padding +
                                     static_cast<float>(first_column) * cell_width -
                                     horizontal_scroll;
                const auto finish_x = x + editor_padding +
                                      static_cast<float>(last_column) * cell_width -
                                      horizontal_scroll;
                const auto baseline =
                    y + static_cast<float>(line - first_line + 1) * line_height - 2.0F;
                auto upward = true;
                for (auto position = start_x; position < finish_x; position += 4.0F) {
                    const auto next = std::min(position + 4.0F, finish_x);
                    SDL_RenderLine(renderer,
                                   position,
                                   baseline + (upward ? 1.0F : -1.0F),
                                   next,
                                   baseline + (upward ? -1.0F : 1.0F));
                    upward = !upward;
                }
            }
        }
    }

    std::vector<std::optional<LineGeometry>> lines_;
    std::vector<SDL_Vertex> frame_vertices_;
    std::vector<int> frame_indices_;
    float cell_width_{};
    float font_size_{};
};

App::App(AppOptions options) : options_(std::move(options)) {
    if (options_.repository_path) {
        repository_ =
            std::make_unique<RepositoryView>(RepositoryView::open(*options_.repository_path));
        if (repository_->error().empty()) {
            editor_graph_ = std::make_unique<EditorGraph>(*options_.repository_path);
            auto config = load_ai_config(*options_.repository_path);
            if (!config) {
                editor_status_ = config.error().message;
            } else {
                ai_context_bytes_ = config->context_bytes;
                auto key = ConfiguredSecret{};
                if (config->enabled) {
                    key = anthropic_api_key();
                }
                const auto secret = key.value;
                assistant_ = std::make_unique<Assistant>(*config, key.configured, [secret] {
                    return std::make_unique<AnthropicProvider>(secret);
                });
            }
            if (options_.lake_url) {
                try {
                    const auto* token = std::getenv("LIQUID_TOKEN");
                    review_ = std::make_unique<ReviewController>(
                        *options_.repository_path,
                        *options_.lake_url,
                        token == nullptr ? std::string{} : std::string{token});
                    review_->request_list();
                } catch (const std::exception& error) {
                    editor_status_ = std::string{"review configuration: "} + error.what();
                }
            }
        }
    }
    if (options_.smoke) {
        editor_ = std::make_unique<EditorBuffer>(
            EditorBuffer::from_text("smoke-50000", "smoke-50000.txt", smoke_text()));
        layouts_ = std::make_unique<TextLayoutCache>();
        layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
        text_layer_ = std::make_unique<RetainedTextLayer>();
        text_layer_->reset(editor_->line_count());
        editor_focused_ = true;
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
    int window_width = 1280;
    int window_height = 800;
    SDL_Rect usable{};
    if (SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &usable) && usable.w > 0 &&
        usable.h > 0) {
        window_width = static_cast<int>(usable.w * 0.70F);
        window_height = static_cast<int>(usable.h * 0.70F);
        window_width = std::max(window_width, 900);
        window_height = std::max(window_height, 600);
    }
    const std::string title = options_.title + " " + std::string(version_string());
    window_ = SDL_CreateWindow(title.c_str(),
                               window_width,
                               window_height,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
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
    if (const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay())) {
        display_refresh_hz_ = mode->refresh_rate;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsLight();
    ImGui_ImplSDL3_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer3_Init(renderer_);
    SDL_StartTextInput(window_);
    initialized_ = true;
    return true;
}

void App::open_editor(std::string path, std::string blob_id) {
    if (analysis_buffer_open_ && editor_graph_ && editor_) {
        editor_graph_->close_buffer(std::filesystem::path(editor_->display_path()));
    }
    analysis_buffer_open_ = false;
    analysis_text_.clear();
    review_diff_open_ = false;
    editor_ = std::make_unique<EditorBuffer>(
        EditorBuffer::open(*options_.repository_path, std::move(path), std::move(blob_id)));
    layouts_ = std::make_unique<TextLayoutCache>();
    layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
    text_layer_ = std::make_unique<RetainedTextLayer>();
    text_layer_->reset(editor_->line_count());
    scroll_line_ = 0;
    horizontal_scroll_ = 0.0F;
    editor_focused_ = true;
    editor_status_.clear();
    if (!editor_graph_ || !editor_->error().empty()) {
        return;
    }
    auto language = stub_language_for_path(std::filesystem::path(editor_->display_path()));
    if (!language) {
        return;
    }
    analysis_text_ = editor_->text();
    analysis_buffer_open_ = true;
    editor_graph_->open_buffer(
        std::filesystem::path(editor_->display_path()), *language, analysis_text_);
    editor_graph_->request_diagnostics(std::filesystem::path(editor_->display_path()));
}

void App::show_review_diff(const ReviewSnapshot& review) {
    if (!review.selected || !review.head) {
        return;
    }
    if (analysis_buffer_open_ && editor_graph_ && editor_) {
        editor_graph_->close_buffer(std::filesystem::path(editor_->display_path()));
    }
    analysis_buffer_open_ = false;
    analysis_text_.clear();
    review_diff_lines_ = review.diff;
    auto text = std::string{};
    for (std::size_t index = 0; index < review_diff_lines_.size(); ++index) {
        if (index != 0) {
            text.push_back('\n');
        }
        text += review_diff_lines_[index].text;
    }
    const auto title = "Review " + review.selected->id + " — text diff";
    editor_ = std::make_unique<EditorBuffer>(
        EditorBuffer::from_text("review:" + review.selected->id, title, std::move(text)));
    layouts_ = std::make_unique<TextLayoutCache>();
    layouts_->reset(std::string(editor_->blob_id()), editor_->line_count());
    text_layer_ = std::make_unique<RetainedTextLayer>();
    text_layer_->reset(editor_->line_count());
    review_diff_open_ = true;
    displayed_review_head_ = liquid::to_string(review.head->value);
    pending_review_id_.clear();
    scroll_line_ = 0;
    horizontal_scroll_ = 0.0F;
    editor_focused_ = true;
    editor_status_ = std::format("text diff ready in {:.3f} ms", review.open_to_first_hunk_ms);
}

void App::open_review(std::string id) {
    if (!review_) {
        return;
    }
    const auto snapshot = review_->snapshot();
    if (snapshot.review.selected && snapshot.review.selected->id == id && !snapshot.busy) {
        show_review_diff(snapshot.review);
        return;
    }
    pending_review_id_ = id;
    review_->request_open(std::move(id));
    editor_status_ = "pulling review";
}

void App::navigate_hunk(bool forward) {
    if (!review_) {
        return;
    }
    if (!review_diff_open_) {
        const auto snapshot = review_->snapshot();
        if (!snapshot.review.selected) {
            return;
        }
        show_review_diff(snapshot.review);
    }
    if (!editor_) {
        return;
    }
    const auto current = editor_->cursor().line;
    if (forward) {
        for (auto line = current + 1; line < review_diff_lines_.size(); ++line) {
            if (review_diff_lines_[line].kind == ReviewDiffLineKind::hunk) {
                editor_->set_cursor({line, 0});
                ensure_cursor_visible();
                return;
            }
        }
    } else {
        for (auto line = current; line > 0; --line) {
            if (review_diff_lines_[line - 1].kind == ReviewDiffLineKind::hunk) {
                editor_->set_cursor({line - 1, 0});
                ensure_cursor_visible();
                return;
            }
        }
    }
}

void App::navigate_comment() {
    if (!review_) {
        return;
    }
    const auto snapshot = review_->snapshot();
    if (snapshot.review.comments.empty()) {
        editor_status_ = "review has no comments";
        return;
    }
    next_comment_index_ %= snapshot.review.comments.size();
    const auto& comment = snapshot.review.comments[next_comment_index_++];
    if (comment.state == ReviewAnchorState::deleted) {
        editor_status_ = "deleted anchor parked: " + comment.value.body;
        return;
    }
    if (review_diff_open_ && editor_) {
        const auto found = std::ranges::find_if(review_diff_lines_, [&comment](const auto& line) {
            return line.path == comment.value.anchor.path && line.new_line &&
                   *line.new_line == comment.value.anchor.line;
        });
        if (found != review_diff_lines_.end()) {
            const auto line = static_cast<std::size_t>(found - review_diff_lines_.begin());
            editor_->set_cursor({line, 0});
            ensure_cursor_visible();
            return;
        }
    }
    open_editor(comment.value.anchor.path, "review:" + comment.value.anchor.path);
    if (editor_ && editor_->error().empty()) {
        const auto line = comment.value.anchor.line == 0 ? 0 : comment.value.anchor.line - 1;
        const auto column = comment.value.anchor.column == 0 ? 0 : comment.value.anchor.column - 1;
        editor_->set_cursor({static_cast<std::size_t>(line), static_cast<std::size_t>(column)});
        ensure_cursor_visible();
    }
}

void App::begin_review_comment() {
    if (!review_ || !review_->snapshot().review.selected) {
        editor_status_ = "open a review before commenting";
        return;
    }
    review_comment_body_.clear();
    review_comment_popup_open_ = true;
}

void App::submit_review_comment() {
    if (!review_ || !editor_ || review_comment_body_.empty()) {
        return;
    }
    auto path = std::string{};
    auto line = std::uint64_t{};
    auto column = std::uint64_t{};
    const auto cursor = editor_->cursor();
    if (review_diff_open_) {
        if (cursor.line >= review_diff_lines_.size() || !review_diff_lines_[cursor.line].new_line) {
            editor_status_ = "comments require a current-side diff line";
            return;
        }
        const auto& diff_line = review_diff_lines_[cursor.line];
        path = diff_line.path;
        line = *diff_line.new_line;
        column = cursor.byte > 2 ? cursor.byte - 1 : 1;
    } else {
        path = std::string(editor_->display_path());
        line = cursor.line + 1;
        column = cursor.byte + 1;
    }
    review_->request_comment(review_comment_body_, std::move(path), line, column);
    review_comment_body_.clear();
    editor_status_ = "posting review comment";
}

void App::poll_review() {
    if (!review_) {
        return;
    }
    const auto snapshot = review_->snapshot();
    if (snapshot.generation != review_generation_) {
        review_generation_ = snapshot.generation;
        if (!snapshot.error.empty()) {
            editor_status_ = snapshot.error;
        } else if (snapshot.review.selected && snapshot.review.head) {
            const auto head = liquid::to_string(snapshot.review.head->value);
            if (pending_review_id_ == snapshot.review.selected->id ||
                (!displayed_review_head_.empty() && displayed_review_head_ != head)) {
                show_review_diff(snapshot.review);
            }
        }
    }
    const auto now = std::chrono::steady_clock::now();
    if (snapshot.review.selected && !snapshot.busy && now >= next_review_refresh_) {
        review_->request_refresh();
        next_review_refresh_ = now + std::chrono::seconds{2};
    }
}

void App::apply_change(std::size_t first_line,
                       std::size_t erased_lines,
                       std::size_t inserted_lines) {
    const BufferChange change{first_line, erased_lines, inserted_lines};
    layouts_->apply_change(change);
    text_layer_->apply_change(change);
    editor_status_.clear();
    sync_editor_graph();
}

void App::save_editor() {
    if (!editor_) {
        return;
    }
    if (!editor_->save()) {
        editor_status_ = std::string(editor_->error());
        return;
    }
    editor_status_ = "saved as a drip";
    if (editor_graph_) {
        editor_graph_->note_saved_drip(editor_->pending_drips());
        editor_graph_->refresh_status();
    }
}

void App::sync_editor_graph() {
    if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
        return;
    }
    auto current = editor_->text();
    auto edit = source_edit_between(analysis_text_, current);
    if (!edit) {
        if (analysis_text_ != current) {
            editor_status_ = "analysis overlay exceeds the supported byte range";
        }
        return;
    }
    const auto path = std::filesystem::path(editor_->display_path());
    editor_graph_->apply_edit(path, std::move(*edit));
    analysis_text_ = std::move(current);
    editor_graph_->request_diagnostics(path);
}

void App::request_definition() {
    if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
        return;
    }
    const auto byte = editor_->byte_offset(editor_->cursor());
    if (byte <= std::numeric_limits<std::uint32_t>::max()) {
        editor_graph_->request_definition(std::filesystem::path(editor_->display_path()),
                                          static_cast<std::uint32_t>(byte));
    }
}

void App::request_hover() {
    if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
        return;
    }
    const auto byte = editor_->byte_offset(editor_->cursor());
    if (byte <= std::numeric_limits<std::uint32_t>::max()) {
        editor_graph_->request_hover(std::filesystem::path(editor_->display_path()),
                                     static_cast<std::uint32_t>(byte));
    }
}

void App::request_completion() {
    if (!editor_graph_ || !analysis_buffer_open_ || !editor_) {
        return;
    }
    const auto byte = editor_->byte_offset(editor_->cursor());
    if (byte <= std::numeric_limits<std::uint32_t>::max()) {
        editor_graph_->request_completion(std::filesystem::path(editor_->display_path()),
                                          static_cast<std::uint32_t>(byte));
    }
}

void App::request_assistant_context(std::optional<AssistantAction> action) {
    if (!assistant_ || !editor_graph_ || !analysis_buffer_open_ || !editor_) {
        return;
    }
    if (action && !assistant_->snapshot().enabled) {
        editor_status_ = "AI is off";
        return;
    }
    const auto cursor = editor_->cursor();
    const auto cursor_byte = editor_->byte_offset(cursor);
    if (cursor_byte > std::numeric_limits<std::uint32_t>::max() ||
        editor_->text().size() > std::numeric_limits<std::uint32_t>::max()) {
        editor_status_ = "assistant context exceeds the supported byte range";
        return;
    }
    auto selection_range = std::optional<SourceRange>{};
    auto selected_source = std::string{};
    if (const auto selection = editor_->selection()) {
        const auto start = editor_->byte_offset(selection->begin);
        const auto end = editor_->byte_offset(selection->end);
        selection_range = SourceRange{static_cast<std::uint32_t>(start),
                                      static_cast<std::uint32_t>(end),
                                      static_cast<std::uint32_t>(selection->begin.line),
                                      static_cast<std::uint32_t>(selection->begin.byte),
                                      static_cast<std::uint32_t>(selection->end.line),
                                      static_cast<std::uint32_t>(selection->end.byte)};
        selected_source = editor_->text().substr(start, end - start);
    }
    const auto snapshot = editor_graph_->snapshot();
    ai_context_generation_ = snapshot.ai_context_generation;
    pending_assistant_action_ = action;
    pending_assistant_prompt_ = assistant_prompt_;
    if (action == AssistantAction::explain_diagnostic && pending_assistant_prompt_.empty()) {
        pending_assistant_prompt_ = "Explain the diagnostic at the cursor.";
    } else if (action == AssistantAction::propose_edit && pending_assistant_prompt_.empty()) {
        pending_assistant_prompt_ = "Propose a focused correction.";
    }
    context_preview_open_ = !action.has_value();
    editor_graph_->request_ai_context(std::filesystem::path(editor_->display_path()),
                                      static_cast<std::uint32_t>(cursor_byte),
                                      selection_range,
                                      std::move(selected_source),
                                      editor_->text().size(),
                                      ai_context_bytes_);
    editor_status_ = "building graph context";
}

void App::accept_assistant_edit() {
    if (!assistant_ || !editor_ || !options_.repository_path) {
        return;
    }
    assistant_->accept();
    const auto accepted = assistant_->take_accepted();
    if (!accepted) {
        return;
    }
    const auto change = editor_->apply_edit(*accepted);
    if (!change) {
        editor_status_ = "assistant edit no longer matches this buffer";
        assistant_->reject();
        return;
    }
    apply_change(change->first_line, change->erased_lines, change->inserted_lines);
    save_editor();
    if (editor_->dirty()) {
        return;
    }
    const auto recorded = record_assisted_edit(
        *options_.repository_path, std::filesystem::path(editor_->display_path()), *accepted);
    if (!recorded) {
        editor_status_ = recorded.error().message;
    } else {
        editor_status_ = "accepted assisted edit as one drip";
    }
}

void App::toggle_assistant() {
    if (!assistant_ || !options_.repository_path) {
        return;
    }
    const auto enable = !assistant_->snapshot().enabled;
    const auto saved = set_repository_ai_enabled(*options_.repository_path, enable);
    if (!saved) {
        editor_status_ = saved.error().message;
        return;
    }
    if (!enable) {
        assistant_->set_enabled(false);
        editor_status_ = "AI off";
        return;
    }
    auto config = load_ai_config(*options_.repository_path);
    if (!config) {
        editor_status_ = config.error().message;
        return;
    }
    config->enabled = true;
    ai_context_bytes_ = config->context_bytes;
    auto key = anthropic_api_key();
    const auto secret = key.value;
    assistant_ = std::make_unique<Assistant>(
        *config, key.configured, [secret] { return std::make_unique<AnthropicProvider>(secret); });
    editor_status_ = key.configured ? "AI on" : "AI on; key not configured";
}

void App::poll_editor_graph() {
    if (!editor_graph_) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_status_refresh_) {
        editor_graph_->refresh_status();
        next_status_refresh_ = now + std::chrono::milliseconds{500};
    }
    const auto snapshot = editor_graph_->snapshot();
    if (snapshot.ai_context_generation != ai_context_generation_) {
        ai_context_generation_ = snapshot.ai_context_generation;
        if (snapshot.ai_context) {
            context_preview_ = snapshot.ai_context->text;
            context_omissions_ = snapshot.ai_context->omissions;
            if (pending_assistant_action_) {
                assistant_->submit(*pending_assistant_action_,
                                   std::move(pending_assistant_prompt_),
                                   *snapshot.ai_context,
                                   snapshot.ai_context_focus);
                pending_assistant_action_.reset();
            }
            editor_status_.clear();
        }
    }
    if (snapshot.definition_generation != definition_generation_) {
        definition_generation_ = snapshot.definition_generation;
        if (snapshot.definition) {
            const auto target = snapshot.definition->path.generic_string();
            if (!editor_ || editor_->display_path() != target) {
                open_editor(target, "graph:" + target);
            }
            const auto& range = snapshot.definition->declaration.range;
            editor_->set_cursor({range.start_line, range.start_column});
            ensure_cursor_visible();
        }
    }
}

void App::handle_text(std::string_view text) {
    if (!editor_ || !editor_focused_ || review_diff_open_ ||
        editor_->mode() == EditingMode::modal_command) {
        return;
    }
    const auto change = editor_->insert_text(text);
    if (change) {
        apply_change(change->first_line, change->erased_lines, change->inserted_lines);
    }
}

void App::handle_key(std::uint32_t key, std::uint16_t modifiers) {
    if (!editor_ || !editor_focused_) {
        return;
    }
    const auto primary = (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0;
    const auto shift = (modifiers & SDL_KMOD_SHIFT) != 0;
    const auto alt = (modifiers & SDL_KMOD_ALT) != 0;
    if (primary && shift && key == SDLK_P) {
        command_palette_open_ = true;
        return;
    }
    if (primary && alt && key == SDLK_C) {
        begin_review_comment();
        return;
    }
    if (primary && alt && key == SDLK_A) {
        if (review_) {
            review_->request_approve();
        }
        return;
    }
    if (alt && key == SDLK_DOWN) {
        navigate_hunk(true);
        return;
    }
    if (alt && key == SDLK_UP) {
        navigate_hunk(false);
        return;
    }
    if (alt && key == SDLK_C) {
        navigate_comment();
        return;
    }
    if (primary && shift && key == SDLK_L) {
        toggle_assistant();
        return;
    }
    if (primary && shift && key == SDLK_A) {
        request_assistant_context(AssistantAction::ask);
        return;
    }
    if (primary && shift && key == SDLK_E) {
        request_assistant_context(AssistantAction::explain_diagnostic);
        return;
    }
    if (primary && shift && key == SDLK_I) {
        request_assistant_context(AssistantAction::propose_edit);
        return;
    }
    if (primary && shift && key == SDLK_C) {
        request_assistant_context(std::nullopt);
        return;
    }
    if (primary && (key == SDLK_RETURN || key == SDLK_KP_ENTER)) {
        accept_assistant_edit();
        return;
    }
    if (primary && key == SDLK_BACKSPACE) {
        if (assistant_) {
            assistant_->reject();
        }
        return;
    }
    if (primary && key == SDLK_S) {
        if (review_diff_open_) {
            editor_status_ = "review diffs are read-only";
            return;
        }
        save_editor();
        return;
    }
    if (key == SDLK_F12) {
        request_definition();
        return;
    }
    if (key == SDLK_F1) {
        request_hover();
        return;
    }
    if (primary && key == SDLK_SPACE) {
        request_completion();
        return;
    }
    if (primary && key == SDLK_A) {
        editor_->select_all();
        return;
    }
    if (primary && key == SDLK_M) {
        const auto next = editor_->mode() == EditingMode::standard ? EditingMode::modal_command
                                                                   : EditingMode::standard;
        editor_->set_mode(next);
        return;
    }

    if (review_diff_open_ && key != SDLK_LEFT && key != SDLK_RIGHT && key != SDLK_UP &&
        key != SDLK_DOWN && key != SDLK_HOME && key != SDLK_END && key != SDLK_PAGEUP &&
        key != SDLK_PAGEDOWN) {
        return;
    }

    if (editor_->mode() == EditingMode::modal_command) {
        if (key == SDLK_I) {
            editor_->set_mode(EditingMode::modal_insert);
        } else if (key == SDLK_A) {
            editor_->move_right();
            editor_->set_mode(EditingMode::modal_insert);
        } else if (key == SDLK_H) {
            editor_->move_left(shift);
        } else if (key == SDLK_J) {
            editor_->move_down(shift);
        } else if (key == SDLK_K) {
            editor_->move_up(shift);
        } else if (key == SDLK_L) {
            editor_->move_right(shift);
        } else if (key == SDLK_0) {
            editor_->move_home(shift);
        } else if (key == SDLK_4 && shift) {
            editor_->move_end();
        } else if (key == SDLK_X) {
            const auto change = editor_->delete_forward();
            if (change) {
                apply_change(change->first_line, change->erased_lines, change->inserted_lines);
            }
        }
        ensure_cursor_visible();
        return;
    }

    if (key == SDLK_ESCAPE && editor_->mode() == EditingMode::modal_insert) {
        editor_->set_mode(EditingMode::modal_command);
    } else if (key == SDLK_LEFT) {
        editor_->move_left(shift);
    } else if (key == SDLK_RIGHT) {
        editor_->move_right(shift);
    } else if (key == SDLK_UP) {
        editor_->move_up(shift);
    } else if (key == SDLK_DOWN) {
        editor_->move_down(shift);
    } else if (key == SDLK_HOME) {
        editor_->move_home(shift);
    } else if (key == SDLK_END) {
        editor_->move_end(shift);
    } else if (key == SDLK_PAGEUP) {
        const auto cursor = editor_->cursor();
        const auto amount = std::max<std::size_t>(visible_lines_, 1);
        editor_->set_cursor({cursor.line > amount ? cursor.line - amount : 0, cursor.byte}, shift);
    } else if (key == SDLK_PAGEDOWN) {
        const auto cursor = editor_->cursor();
        const auto amount = std::max<std::size_t>(visible_lines_, 1);
        editor_->set_cursor({cursor.line + amount, cursor.byte}, shift);
    } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        const auto change = editor_->insert_newline();
        apply_change(change.first_line, change.erased_lines, change.inserted_lines);
    } else if (key == SDLK_TAB) {
        const auto change = editor_->insert_text("\t");
        if (change) {
            apply_change(change->first_line, change->erased_lines, change->inserted_lines);
        }
    } else if (key == SDLK_BACKSPACE) {
        const auto change = editor_->backspace();
        if (change) {
            apply_change(change->first_line, change->erased_lines, change->inserted_lines);
        }
    } else if (key == SDLK_DELETE) {
        const auto change = editor_->delete_forward();
        if (change) {
            apply_change(change->first_line, change->erased_lines, change->inserted_lines);
        }
    }
    ensure_cursor_visible();
}

void App::ensure_cursor_visible() {
    if (!editor_ || visible_lines_ == 0) {
        return;
    }
    const auto line = editor_->cursor().line;
    if (line < scroll_line_) {
        scroll_line_ = line;
    } else if (line >= scroll_line_ + visible_lines_) {
        scroll_line_ = line - visible_lines_ + 1;
    }
}

void App::position_cursor(float x, float y, bool extend_selection) {
    if (!editor_ || editor_height_ <= 0.0F) {
        return;
    }
    const auto visible_line =
        static_cast<std::size_t>(std::max(0.0F, std::floor((y - editor_y_) / line_height_)));
    const auto line = std::min(scroll_line_ + visible_line, editor_->line_count() - 1);
    const auto column =
        std::max(0.0F, (x - editor_x_ - editor_padding + horizontal_scroll_) / cell_width_);
    const auto& layout = layouts_->line(line, editor_->line(line));
    const auto byte = layout.byte_for_column(column, editor_->line(line).size());
    editor_->set_cursor({line, byte}, extend_selection);
}

void App::draw_repository_tree() {
    const auto path = options_.repository_path->string();
    ImGui::Text("Repository: %s", path.c_str());
    ImGui::TextDisabled("opened in %.3f ms", repository_->open_time().count());
    ImGui::Separator();
    if (!repository_->error().empty()) {
        ImGui::TextWrapped("Unable to open repository: %s",
                           std::string(repository_->error()).c_str());
        return;
    }
    repository_->poll();
    draw_repository_entries({});
    ImGui::Separator();
    draw_review_panel();
}

void App::draw_review_panel() {
    ImGui::TextUnformatted("Reviews");
    if (!options_.lake_url) {
        ImGui::TextDisabled("Configure with --lake <repository-url>");
        return;
    }
    if (!review_) {
        ImGui::TextWrapped("%s", editor_status_.c_str());
        return;
    }
    const auto snapshot = review_->snapshot();
    if (ImGui::SmallButton("Refresh reviews")) {
        if (snapshot.review.selected) {
            review_->request_refresh();
        } else {
            review_->request_list();
        }
    }
    if (snapshot.busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("loading...");
    }
    if (!snapshot.error.empty()) {
        ImGui::TextWrapped("%s", snapshot.error.c_str());
    }
    for (const auto& item : snapshot.review.reviews) {
        const auto label = item.id + " [" + std::string(review_state_name(item.state)) + "]";
        const auto selected = snapshot.review.selected && snapshot.review.selected->id == item.id;
        if (ImGui::Selectable(label.c_str(), selected)) {
            open_review(item.id);
        }
    }
    if (!snapshot.review.selected) {
        return;
    }
    ImGui::Separator();
    ImGui::TextWrapped("Review %s: %s",
                       snapshot.review.selected->id.c_str(),
                       std::string(review_state_name(snapshot.review.selected->state)).c_str());
    if (ImGui::SmallButton("New comment")) {
        begin_review_comment();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Approve")) {
        review_->request_approve();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Request changes")) {
        review_->request_changes();
    }
    ImGui::TextDisabled("text diff %.3f ms | anchor map %.3f ms",
                        snapshot.review.open_to_first_hunk_ms,
                        snapshot.review.anchor_mapping_ms);
    ImGui::Text("Comments (%zu)", snapshot.review.comments.size());
    for (std::size_t index = 0; index < snapshot.review.comments.size(); ++index) {
        const auto& comment = snapshot.review.comments[index];
        const auto label = comment.value.anchor.path + ":" +
                           std::to_string(comment.value.anchor.line) + " [" +
                           std::string(review_anchor_state_name(comment.state)) + "] " +
                           comment.value.body + "##review-comment-" + std::to_string(index);
        if (ImGui::Selectable(label.c_str())) {
            next_comment_index_ = index;
            navigate_comment();
        }
    }
}

void App::draw_review_comment_popup() {
    if (review_comment_popup_open_) {
        ImGui::OpenPopup("Review comment");
        review_comment_popup_open_ = false;
    }
    if (!ImGui::BeginPopupModal("Review comment", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextUnformatted("Comment on the current cursor");
    ImGui::InputTextMultiline(
        "##review-comment-body", &review_comment_body_, ImVec2(520.0F, 140.0F));
    ImGui::BeginDisabled(review_comment_body_.empty());
    if (ImGui::Button("Post comment")) {
        submit_review_comment();
        if (review_comment_body_.empty()) {
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        review_comment_body_.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::draw_repository_entries(std::string_view directory) {
    const auto entries =
        directory.empty() ? repository_->entries() : repository_->entries(directory);
    for (const auto& entry : entries) {
        const auto path =
            directory.empty() ? entry.name : std::string(directory) + "/" + entry.name;
        ImGui::PushID(path.c_str());
        if (entry.kind == liquid::node_kind::blob) {
            if (ImGui::Selectable(entry.name.c_str(), editor_ && editor_->display_path() == path)) {
                open_editor(path, liquid::to_string(entry.id));
            }
        } else if (entry.kind == liquid::node_kind::tree) {
            const auto open = ImGui::TreeNodeEx(entry.name.c_str(), ImGuiTreeNodeFlags_OpenOnArrow);
            if (open) {
                if (!repository_->directory_loaded(path) && !repository_->directory_loading(path)) {
                    repository_->request_directory(path, entry.ref);
                }
                if (repository_->directory_loading(path)) {
                    ImGui::TextDisabled("loading...");
                } else if (!repository_->directory_error(path).empty()) {
                    ImGui::TextWrapped("Unable to read directory: %s",
                                       std::string(repository_->directory_error(path)).c_str());
                } else {
                    draw_repository_entries(path);
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::Text(
                "%s (%s)", entry.name.c_str(), std::string(node_kind_name(entry.kind)).c_str());
        }
        ImGui::PopID();
    }
}

void App::draw_assistant() {
    if (!assistant_) {
        return;
    }
    const auto snapshot = assistant_->snapshot();
    ImGui::Text("Assistant: %s", snapshot.status.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("What will be sent")) {
        request_assistant_context(std::nullopt);
    }
    ImGui::InputTextWithHint(
        "##assistant-prompt", "Ask about the selection or current definition", &assistant_prompt_);
    ImGui::BeginDisabled(!snapshot.enabled || snapshot.running);
    if (ImGui::Button("Ask")) {
        request_assistant_context(AssistantAction::ask);
    }
    ImGui::SameLine();
    if (ImGui::Button("Explain diagnostic")) {
        request_assistant_context(AssistantAction::explain_diagnostic);
    }
    ImGui::SameLine();
    if (ImGui::Button("Propose edit")) {
        request_assistant_context(AssistantAction::propose_edit);
    }
    ImGui::EndDisabled();
    if (!snapshot.enabled) {
        ImGui::TextUnformatted("AI is off. No provider exists and no connection can be opened.");
    } else if (!snapshot.key_configured) {
        ImGui::TextUnformatted("Anthropic key: not configured");
    } else {
        ImGui::TextUnformatted("Anthropic key: configured");
    }
    if (snapshot.refusal) {
        ImGui::TextColored(
            ImVec4(0.65F, 0.30F, 0.05F, 1.0F), "Refusal: %s", snapshot.response.c_str());
    } else if (!snapshot.response.empty()) {
        ImGui::TextWrapped("%s", snapshot.response.c_str());
    }
    if (snapshot.proposal) {
        ImGui::Separator();
        ImGui::Text("Proposed replacement for bytes %u..%u",
                    snapshot.proposal->start_byte,
                    snapshot.proposal->old_end_byte);
        ImGui::TextWrapped("%s", snapshot.proposal->text.c_str());
        if (ImGui::Button("Accept edit")) {
            accept_assistant_edit();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reject edit")) {
            assistant_->reject();
        }
    }
    if (context_preview_open_) {
        ImGui::OpenPopup("What will be sent");
        context_preview_open_ = false;
    }
    if (ImGui::BeginPopupModal("What will be sent", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Structured graph context: %zu bytes", context_preview_.size());
        if (!context_omissions_.empty()) {
            ImGui::TextWrapped("Omissions: %s", context_omissions_.front().c_str());
        }
        ImGui::InputTextMultiline("##context-preview",
                                  &context_preview_,
                                  ImVec2(720.0F, 420.0F),
                                  ImGuiInputTextFlags_ReadOnly);
        if (ImGui::Button("Close")) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::draw_command_palette() {
    if (command_palette_open_) {
        ImGui::OpenPopup("Command palette");
        command_palette_open_ = false;
    }
    if (!ImGui::BeginPopupModal("Command palette", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    const auto command = [this](const char* label, auto invoke) {
        if (ImGui::Selectable(label)) {
            invoke();
            ImGui::CloseCurrentPopup();
        }
    };
    command("Assistant: Ask (Primary+Shift+A)",
            [this] { request_assistant_context(AssistantAction::ask); });
    command("Assistant: Explain diagnostic (Primary+Shift+E)",
            [this] { request_assistant_context(AssistantAction::explain_diagnostic); });
    command("Assistant: Propose edit (Primary+Shift+I)",
            [this] { request_assistant_context(AssistantAction::propose_edit); });
    command("Assistant: Accept edit (Primary+Enter)", [this] { accept_assistant_edit(); });
    command("Assistant: Reject edit (Primary+Backspace)", [this] {
        if (assistant_) {
            assistant_->reject();
        }
    });
    command("Assistant: Toggle (Primary+Shift+L)", [this] { toggle_assistant(); });
    command("Assistant: What will be sent (Primary+Shift+C)",
            [this] { request_assistant_context(std::nullopt); });
    command("Review: Next hunk (Alt+Down)", [this] { navigate_hunk(true); });
    command("Review: Previous hunk (Alt+Up)", [this] { navigate_hunk(false); });
    command("Review: Next comment (Alt+C)", [this] { navigate_comment(); });
    command("Review: New comment (Primary+Alt+C)", [this] { begin_review_comment(); });
    command("Review: Approve (Primary+Alt+A)", [this] {
        if (review_) {
            review_->request_approve();
        }
    });
    ImGui::EndPopup();
}

void App::draw_editor() {
    const auto path = std::string(editor_->display_path());
    const auto graph_snapshot = editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
    ImGui::Text("%s%s", path.c_str(), editor_->dirty() ? " *" : "");
    ImGui::SameLine();
    if (review_diff_open_) {
        ImGui::TextDisabled("read-only text diff");
    } else if (ImGui::Button(editing_mode_name(editor_->mode()).data())) {
        const auto next = editor_->mode() == EditingMode::standard ? EditingMode::modal_command
                                                                   : EditingMode::standard;
        editor_->set_mode(next);
        editor_focused_ = true;
    }
    if (!review_diff_open_) {
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            save_editor();
            editor_focused_ = true;
        }
    }
    if (assistant_) {
        ImGui::SameLine();
        const auto enabled = assistant_->snapshot().enabled;
        if (ImGui::Button(enabled ? "AI: on" : "AI: off")) {
            toggle_assistant();
        }
    }
    if (analysis_buffer_open_) {
        ImGui::SameLine();
        if (ImGui::Button("Definition (F12)")) {
            request_definition();
        }
        ImGui::SameLine();
        if (ImGui::Button("Hover (F1)")) {
            request_hover();
        }
        ImGui::SameLine();
        if (ImGui::Button("Complete")) {
            request_completion();
        }
    }
    if (!editor_status_.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(editor_status_.c_str());
    } else if (!editor_->error().empty()) {
        ImGui::SameLine();
        const auto error = std::string(editor_->error());
        ImGui::TextUnformatted(error.c_str());
    }
    if (editor_graph_) {
        const auto status = editor_graph_status(graph_snapshot);
        ImGui::TextWrapped("%s", status.c_str());
        if (!graph_snapshot.query_error.empty()) {
            ImGui::TextColored(
                ImVec4(0.75F, 0.12F, 0.12F, 1.0F), "%s", graph_snapshot.query_error.c_str());
        }
        if (graph_snapshot.hover) {
            ImGui::TextWrapped("%s", graph_snapshot.hover->signature.c_str());
            if (!graph_snapshot.hover->documentation.empty()) {
                ImGui::TextWrapped("%s", graph_snapshot.hover->documentation.c_str());
            }
        }
    }

    const auto available = ImGui::GetContentRegionAvail();
    const auto panel_height =
        editor_graph_ ? std::max(0.0F, std::min(320.0F, available.y * 0.45F)) : 0.0F;
    const auto size =
        ImVec2(std::max(available.x, 1.0F), std::max(available.y - panel_height, 1.0F));
    const auto position = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("editor-surface", size, ImGuiButtonFlags_MouseButtonLeft);
    const auto hovered = ImGui::IsItemHovered();
    auto* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        position, ImVec2(position.x + size.x, position.y + size.y), IM_COL32(250, 250, 248, 255));
    draw_list->AddRect(
        position, ImVec2(position.x + size.x, position.y + size.y), IM_COL32(205, 208, 212, 255));

    editor_x_ = position.x;
    editor_y_ = position.y;
    editor_width_ = size.x;
    editor_height_ = size.y;
    line_height_ = ImGui::GetFontSize() + 4.0F;
    const auto* glyph = ImGui::GetFont()->FindGlyph('M');
    cell_width_ = glyph == nullptr ? ImGui::GetFontSize() * 0.6F : glyph->AdvanceX;
    visible_lines_ = std::max<std::size_t>(1, static_cast<std::size_t>(size.y / line_height_));
    editor_visible_ = true;

    if (hovered) {
        const auto wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0F) {
            const auto amount = static_cast<std::size_t>(std::abs(wheel) * 3.0F);
            if (wheel > 0.0F) {
                scroll_line_ = scroll_line_ > amount ? scroll_line_ - amount : 0;
            } else {
                scroll_line_ = std::min(scroll_line_ + amount, editor_->line_count() - 1);
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            editor_focused_ = true;
            position_cursor(
                ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, ImGui::GetIO().KeyShift);
        } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            position_cursor(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y, true);
        }
    }

    if (!editor_graph_) {
        return;
    }
    ImGui::BeginChild("diagnostics-panel",
                      ImVec2(std::max(available.x, 1.0F), panel_height),
                      ImGuiChildFlags_Border);
    draw_assistant();
    ImGui::Separator();
    ImGui::Text("Diagnostics (%zu)", graph_snapshot.diagnostics.size());
    if (graph_snapshot.edit_to_diagnostic_ms > 0.0) {
        ImGui::SameLine();
        ImGui::TextDisabled("edit to first result %.3f ms", graph_snapshot.edit_to_diagnostic_ms);
    }
    for (std::size_t index = 0; index < graph_snapshot.diagnostics.size(); ++index) {
        const auto& diagnostic = graph_snapshot.diagnostics[index];
        const auto label = std::string(diagnostic_severity_name(diagnostic.severity)) + ": " +
                           diagnostic.message + "##diagnostic-" + std::to_string(index);
        if (ImGui::Selectable(label.c_str())) {
            const auto target = diagnostic.location.path.generic_string();
            if (editor_->display_path() != target) {
                open_editor(target, "graph:" + target);
            }
            editor_->set_cursor(
                {diagnostic.location.range.start_line, diagnostic.location.range.start_column});
            ensure_cursor_visible();
        }
    }
    if (!graph_snapshot.completions.empty()) {
        ImGui::Separator();
        ImGui::Text("Completions (%zu)", graph_snapshot.completions.size());
        const auto count = std::min<std::size_t>(graph_snapshot.completions.size(), 20);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& completion = graph_snapshot.completions[index];
            const auto display =
                completion.signature.empty() ? completion.name : completion.signature;
            const auto label = display + "##completion-" + std::to_string(index);
            if (ImGui::Selectable(label.c_str())) {
                const auto change = editor_->insert_text(completion.name);
                if (change) {
                    apply_change(change->first_line, change->erased_lines, change->inserted_lines);
                }
                editor_focused_ = true;
            }
        }
    }
    ImGui::EndChild();
}

void App::draw_contents() {
    if (!repository_ && !editor_) {
        ImGui::TextUnformatted("hello from liquify");
        ImGui::Text("version %s", std::string(version_string()).c_str());
        ImGui::Separator();
        ImGui::Text("libliquid %s linked", std::string(liquid::version_string()).c_str());
        ImGui::TextUnformatted("The IDE for liquid: instant open at any size, diagnostics from the "
                               "build graph, drips.");
        return;
    }

    if (repository_) {
        ImGui::BeginChild("repository-tree", ImVec2(260.0F, 0.0F), ImGuiChildFlags_Border);
        draw_repository_tree();
        ImGui::EndChild();
        ImGui::SameLine();
    }
    ImGui::BeginGroup();
    if (editor_) {
        draw_editor();
    } else {
        ImGui::TextUnformatted("Select a file from the repository tree.");
    }
    ImGui::EndGroup();
    draw_command_palette();
    draw_review_comment_popup();
}

void App::render_editor() {
    if (!editor_visible_ || !editor_ || editor_width_ <= 0.0F || editor_height_ <= 0.0F) {
        return;
    }
    const auto maximum_scroll =
        editor_->line_count() > visible_lines_ ? editor_->line_count() - visible_lines_ : 0;
    scroll_line_ = std::min(scroll_line_, maximum_scroll);

    float old_scale_x = 1.0F;
    float old_scale_y = 1.0F;
    SDL_GetRenderScale(renderer_, &old_scale_x, &old_scale_y);
    const auto framebuffer_scale = ImGui::GetIO().DisplayFramebufferScale;
    SDL_SetRenderScale(renderer_, framebuffer_scale.x, framebuffer_scale.y);
    const SDL_Rect clip{static_cast<int>(editor_x_),
                        static_cast<int>(editor_y_),
                        static_cast<int>(editor_width_),
                        static_cast<int>(editor_height_)};
    SDL_SetRenderClipRect(renderer_, &clip);
    const auto graph_snapshot = editor_graph_ ? editor_graph_->snapshot() : EditorGraphSnapshot{};
    auto comment_lines = std::vector<std::size_t>{};
    if (review_) {
        const auto review_snapshot = review_->snapshot();
        for (const auto& comment : review_snapshot.review.comments) {
            if (comment.state == ReviewAnchorState::deleted) {
                continue;
            }
            if (review_diff_open_) {
                const auto found =
                    std::ranges::find_if(review_diff_lines_, [&comment](const auto& line) {
                        return line.path == comment.value.anchor.path && line.new_line &&
                               *line.new_line == comment.value.anchor.line;
                    });
                if (found != review_diff_lines_.end()) {
                    comment_lines.push_back(
                        static_cast<std::size_t>(found - review_diff_lines_.begin()));
                }
            } else if (comment.value.anchor.path == editor_->display_path() &&
                       comment.value.anchor.line > 0) {
                comment_lines.push_back(static_cast<std::size_t>(comment.value.anchor.line - 1));
            }
        }
    }
    const auto review_lines = review_diff_open_
                                  ? std::span<const ReviewDiffLine>{review_diff_lines_}
                                  : std::span<const ReviewDiffLine>{};
    visible_lines_ = text_layer_->render(renderer_,
                                         *editor_,
                                         *layouts_,
                                         graph_snapshot.diagnostics,
                                         editor_x_,
                                         editor_y_,
                                         editor_width_,
                                         editor_height_,
                                         scroll_line_,
                                         line_height_,
                                         cell_width_,
                                         horizontal_scroll_,
                                         review_lines,
                                         comment_lines);
    SDL_SetRenderClipRect(renderer_, nullptr);
    SDL_SetRenderScale(renderer_, old_scale_x, old_scale_y);
}

void App::frame() {
    poll_editor_graph();
    poll_review();
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    editor_visible_ = false;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("liquify",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings);
    draw_contents();
    ImGui::End();

    ImGui::Render();
    SDL_SetRenderDrawColor(renderer_, 250, 250, 248, 255);
    SDL_RenderClear(renderer_);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    render_editor();
    SDL_RenderPresent(renderer_);
}

void App::prepare_smoke_frame(int frame) {
    if (!options_.smoke || !editor_) {
        return;
    }
    const auto denominator = std::max(options_.max_frames - 1, 1);
    const auto last_line = editor_->line_count() - 1;
    const auto line =
        last_line * static_cast<std::size_t>(frame) / static_cast<std::size_t>(denominator);
    scroll_line_ = line;
    editor_->set_cursor({line, editor_->line(line).size()});
    typing_started_ = std::chrono::steady_clock::now();
    const auto change = editor_->insert_text(".");
    if (change) {
        apply_change(change->first_line, change->erased_lines, change->inserted_lines);
    }
}

void App::print_smoke_metrics() const {
    const auto mean_frame = average(frame_times_ms_);
    const auto mean_typing = average(typing_times_ms_);
    const auto refresh = mean_frame > 0.0 ? 1000.0 / mean_frame : 0.0;
    std::printf(
        "smoke: lines=50000 frames=%zu visible_lines=%zu ", frame_times_ms_.size(), visible_lines_);
    std::printf("avg_frame_ms=%.3f p95_frame_ms=%.3f achieved_hz=%.1f display_hz=%.1f ",
                mean_frame,
                percentile(frame_times_ms_, 0.95),
                refresh,
                display_refresh_hz_);
    std::printf(
        "avg_typing_ms=%.3f p95_typing_ms=%.3f\n", mean_typing, percentile(typing_times_ms_, 0.95));
}

int App::run() {
    if (!initialized_) {
        return 1;
    }
    int frames = 0;
    while (!quit_) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                quit_ = true;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(window_)) {
                quit_ = true;
            }
            if (event.type == SDL_EVENT_TEXT_INPUT) {
                handle_text(event.text.text);
            }
            if (event.type == SDL_EVENT_KEY_DOWN) {
                handle_key(event.key.key, static_cast<std::uint16_t>(event.key.mod));
            }
        }

        prepare_smoke_frame(frames);
        const auto frame_started = std::chrono::steady_clock::now();
        frame();
        const auto presented = std::chrono::steady_clock::now();
        const auto frame_time =
            std::chrono::duration<double, std::milli>(presented - frame_started);
        if (options_.smoke) {
            frame_times_ms_.push_back(frame_time.count());
        }
        if (typing_started_) {
            const auto typing_time =
                std::chrono::duration<double, std::milli>(presented - *typing_started_);
            typing_times_ms_.push_back(typing_time.count());
            typing_started_.reset();
        }

        ++frames;
        if (options_.max_frames > 0 && frames >= options_.max_frames) {
            quit_ = true;
        }
    }
    if (options_.smoke) {
        print_smoke_metrics();
    }
    return 0;
}

void App::shutdown() {
    if (!initialized_) {
        return;
    }
    SDL_StopTextInput(window_);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    SDL_Quit();
    renderer_ = nullptr;
    window_ = nullptr;
    initialized_ = false;
}

} // namespace liquify
