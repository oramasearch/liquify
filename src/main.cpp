#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "liquid/liquid.hpp"
#include "liquify/app.hpp"
#include "liquify/interactive_analysis.hpp"
#include "liquify/structural_edit.hpp"
#include "liquify/version.hpp"
#include "repository_view.hpp"

namespace {
void usage() {
    std::println("liquify {}", liquify::version_string());
    std::println("  liquify [PATH]              open the IDE, optionally on a "
                 "liquid repository");
    std::println("  liquify --lake URL PATH     open repository reviews from a "
                 "lake forge");
    std::println("  liquify --print-tree PATH   print a repository root tree and exit");
    std::println("  liquify --apply-edits FILE REPOSITORY");
    std::println("                              apply structural-edit CBOR and "
                 "return result CBOR");
    std::println("  liquify --smoke [N]         scroll 50,000 lines for N frames "
                 "and exit");
    std::println("  liquify --screenshot STATE --theme light|dark --out FILE");
    std::println("                              render a deterministic 1440x900 PNG");
    std::println("  liquify version             print the version");
}

auto parse_frame_count(std::string_view argument, int& count) -> bool {
    const auto* begin = argument.data();
    const auto* end = begin + argument.size();
    const auto result = std::from_chars(begin, end, count);
    return result.ec == std::errc{} && result.ptr == end && count > 0;
}

auto print_tree(const std::filesystem::path& path) -> int {
    const auto view = liquify::RepositoryView::open(path);
    if (!view.error().empty()) {
        std::println(stderr, "liquify: {}", view.error());
        return 1;
    }

    std::println("name\tkind\tmode");
    for (const auto& entry : view.entries()) {
        std::println("{}\t{}\t{:06o}", entry.name, liquify::node_kind_name(entry.kind), entry.mode);
    }
    return 0;
}

auto apply_edits(const std::filesystem::path& request_path,
                 const std::filesystem::path& repository_path) -> int {
    auto input = std::ifstream(request_path, std::ios::binary);
    if (!input) {
        std::println(stderr, "liquify: cannot read {}", request_path.string());
        return 1;
    }
    const auto characters = std::vector<char>(std::istreambuf_iterator<char>(input), {});
    const auto request = std::as_bytes(std::span(characters.data(), characters.size()));
    auto edits = liquify::decode_structural_edit_request(request);
    if (!edits) {
        std::println(stderr, "liquify: {}", edits.error().message);
        return 1;
    }
    auto analysis = liquify::InteractiveAnalysis::open(repository_path / ".liquid" /
                                                       "liquify-structural-graph");
    if (!analysis) {
        std::println(stderr, "liquify: {}", analysis.error().message);
        return 1;
    }
    auto editor = liquify::StructuralEditor(repository_path, *analysis);
    auto result = editor.apply(*edits);
    if (!result) {
        std::println(stderr, "liquify: {}", result.error().message);
        return 1;
    }
    auto encoded = liquify::encode_structural_edit_result(*result);
    if (!encoded) {
        std::println(stderr, "liquify: {}", encoded.error().message);
        return 1;
    }
    const auto written = std::fwrite(encoded->data(), 1, encoded->size(), stdout);
    return written == encoded->size() ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--apply-edits") {
        if (argc != 4) {
            std::println(stderr, "liquify: --apply-edits requires a CBOR file and repository");
            return 2;
        }
        return apply_edits(argv[2], argv[3]);
    }
    liquify::AppOptions opts;
    bool should_print_tree = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "version" || arg == "--version") {
            if (argc != 2) {
                std::println(stderr, "liquify: version accepts no other arguments");
                return 2;
            }
            std::println("liquify {} (libliquid {})",
                         liquify::version_string(),
                         liquid::version_string());
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--smoke") {
            opts.smoke = true;
            opts.max_frames = 30;
            if (i + 1 < argc) {
                int frames = 0;
                if (parse_frame_count(argv[i + 1], frames)) {
                    opts.max_frames = frames;
                    ++i;
                }
            }
            continue;
        }
        if (arg == "--screenshot") {
            if (i + 1 == argc) {
                std::println(stderr, "liquify: --screenshot requires a state");
                return 2;
            }
            opts.screenshot_state = std::string{argv[++i]};
            opts.max_frames = 2;
            continue;
        }
        if (arg == "--out") {
            if (i + 1 == argc) {
                std::println(stderr, "liquify: --out requires a path");
                return 2;
            }
            opts.screenshot_output = std::filesystem::path(argv[++i]);
            continue;
        }
        if (arg == "--theme") {
            if (i + 1 == argc) {
                std::println(stderr, "liquify: --theme requires light or dark");
                return 2;
            }
            const auto theme = std::string_view(argv[++i]);
            if (theme == "light") {
                opts.forced_theme = liquify::ThemePreference::light;
            } else if (theme == "dark") {
                opts.forced_theme = liquify::ThemePreference::dark;
            } else {
                std::println(stderr, "liquify: --theme requires light or dark");
                return 2;
            }
            continue;
        }
        if (arg == "--print-tree") {
            if (i + 1 == argc) {
                std::println(stderr, "liquify: --print-tree requires a repository path");
                return 2;
            }
            if (opts.repository_path) {
                std::println(stderr, "liquify: accepts at most one repository path");
                return 2;
            }
            should_print_tree = true;
            opts.repository_path = std::filesystem::path(argv[++i]);
            continue;
        }
        if (arg == "--lake") {
            if (i + 1 == argc) {
                std::println(stderr, "liquify: --lake requires a repository URL");
                return 2;
            }
            if (opts.lake_url) {
                std::println(stderr, "liquify: --lake may be specified once");
                return 2;
            }
            opts.lake_url = std::string{argv[++i]};
            continue;
        }
        if (arg.starts_with("-")) {
            std::println(stderr, "liquify: unknown option {}", arg);
            usage();
            return 2;
        }
        if (opts.repository_path) {
            std::println(stderr, "liquify: accepts at most one repository path");
            return 2;
        }
        opts.repository_path = std::filesystem::path(arg);
    }
    if (opts.max_frames > 0 && opts.repository_path) {
        std::println(stderr, "liquify: --smoke does not open a repository");
        return 2;
    }
    if (opts.lake_url && !opts.repository_path) {
        std::println(stderr, "liquify: --lake requires a repository path");
        return 2;
    }
    if (opts.screenshot_state) {
        constexpr auto states = std::array<std::string_view, 9>{"welcome",
                                                                "repository",
                                                                "editor",
                                                                "diagnostics",
                                                                "build",
                                                                "assistant",
                                                                "review",
                                                                "plugins",
                                                                "structural"};
        if (std::ranges::find(states, *opts.screenshot_state) == states.end()) {
            std::println(stderr, "liquify: unknown screenshot state {}", *opts.screenshot_state);
            return 2;
        }
        if (!opts.screenshot_output || !opts.forced_theme) {
            std::println(stderr, "liquify: --screenshot requires --theme and --out");
            return 2;
        }
        if (opts.repository_path || opts.smoke || opts.lake_url) {
            std::println(stderr, "liquify: --screenshot cannot open a repository or run smoke");
            return 2;
        }
    } else if (opts.screenshot_output || opts.forced_theme) {
        std::println(stderr, "liquify: --theme and --out require --screenshot");
        return 2;
    }
    if (should_print_tree) {
        return print_tree(*opts.repository_path);
    }
    liquify::App app(opts);
    if (!app.init())
        return 1;
    return app.run();
}
