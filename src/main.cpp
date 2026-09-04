#include <charconv>
#include <filesystem>
#include <print>
#include <string>
#include <string_view>
#include <system_error>

#include "liquid/liquid.hpp"
#include "liquify/app.hpp"
#include "liquify/version.hpp"
#include "repository_view.hpp"

namespace {
void usage() {
    std::println("liquify {}", liquify::version_string());
    std::println("  liquify [PATH]              open the IDE, optionally on a liquid repository");
    std::println("  liquify --print-tree PATH   print a repository root tree and exit");
    std::println("  liquify --smoke [N]         render N frames (default 30) and exit");
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
} // namespace

int main(int argc, char** argv) {
    liquify::AppOptions opts;
    bool should_print_tree = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "version" || arg == "--version") {
            if (argc != 2) {
                std::println(stderr, "liquify: version accepts no other arguments");
                return 2;
            }
            std::println(
                "liquify {} (libliquid {})", liquify::version_string(), liquid::version_string());
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--smoke") {
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
    if (should_print_tree) {
        return print_tree(*opts.repository_path);
    }
    liquify::App app(opts);
    if (!app.init())
        return 1;
    return app.run();
}
