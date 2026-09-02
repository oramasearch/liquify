#include <cstdlib>
#include <print>
#include <string_view>

#include "liquid/liquid.hpp"
#include "liquify/app.hpp"
#include "liquify/version.hpp"

namespace {
void usage() {
    std::println("liquify {}", liquify::version_string());
    std::println("  liquify              open the IDE");
    std::println("  liquify --smoke [N]  render N frames (default 30) and exit — a display-required self-test");
    std::println("  liquify version      print the version");
}
}  // namespace

int main(int argc, char** argv) {
    liquify::AppOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "version" || arg == "--version") {
            std::println("liquify {} (libliquid {})", liquify::version_string(), liquid::version_string());
            return 0;
        }
        if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        }
        if (arg == "--smoke") {
            opts.max_frames = 30;
            if (i + 1 < argc && argv[i + 1][0] != '-') opts.max_frames = std::atoi(argv[++i]);
            continue;
        }
        std::println(stderr, "liquify: unknown argument {}", arg);
        usage();
        return 2;
    }
    liquify::App app(opts);
    if (!app.init()) return 1;
    return app.run();
}
