#include "liquify/navigation.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <tuple>

namespace liquify {
namespace {

auto lower_ascii(std::string_view value) -> std::string {
    auto output = std::string{};
    output.reserve(value.size());
    for (const auto character : value) {
        output.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return output;
}

auto fuzzy_score(std::string_view candidate, std::string_view query) -> std::optional<int> {
    const auto value = lower_ascii(candidate);
    const auto needle = lower_ascii(query);
    if (needle.empty()) {
        return 0;
    }
    auto score = 0;
    auto position = std::size_t{};
    auto previous = std::numeric_limits<std::size_t>::max();
    for (const auto character : needle) {
        const auto found = value.find(character, position);
        if (found == std::string::npos) {
            return std::nullopt;
        }
        score += 8;
        if (found == 0 || value[found - 1] == '/' || value[found - 1] == '_' ||
            value[found - 1] == '-' || value[found - 1] == '.') {
            score += 12;
        }
        if (previous != std::numeric_limits<std::size_t>::max()) {
            score += found == previous + 1 ? 9 : -static_cast<int>(found - previous - 1);
        }
        previous = found;
        position = found + 1;
    }
    if (value == needle) {
        score += 140;
    } else if (value.starts_with(needle)) {
        score += 80;
    }
    score -= static_cast<int>(value.size() / 4);
    return score;
}

auto path_score(const std::filesystem::path& path, std::string_view query) -> std::optional<int> {
    auto score = fuzzy_score(path.generic_string(), query);
    if (!score) {
        return std::nullopt;
    }
    const auto filename = path.filename().generic_string();
    if (const auto leaf = fuzzy_score(filename, query)) {
        *score = std::max(*score, *leaf + 35);
    }
    return score;
}

template <class Value, class Key>
void append_unique(std::vector<Value>& output, Value value, Key key) {
    const auto token = key(value);
    const auto found =
        std::ranges::find_if(output, [&](const auto& item) { return key(item) == token; });
    if (found == output.end()) {
        output.push_back(std::move(value));
    }
}

auto symbol_key(const NavigationSymbol& symbol) {
    return std::tuple{symbol.id,
                      symbol.path.generic_string(),
                      symbol.range.start_line,
                      symbol.range.start_column};
}

auto matches_symbol(const NavigationSymbol& symbol, std::string_view id, std::string_view name)
    -> bool {
    return (!id.empty() && symbol.id == id) || (id.empty() && symbol.name == name);
}

auto definition_for(std::span<const NavigationSymbol> symbols, std::string_view id)
    -> const NavigationSymbol* {
    const auto found = std::ranges::find_if(symbols, [&](const auto& symbol) {
        return symbol.definition && symbol.id == id;
    });
    return found == symbols.end() ? nullptr : &*found;
}

} // namespace

auto navigation_file_kind(const std::filesystem::path& path, bool directory) -> NavigationFileKind {
    if (directory) {
        return NavigationFileKind::directory;
    }
    const auto name = lower_ascii(path.filename().generic_string());
    const auto extension = lower_ascii(path.extension().generic_string());
    if (extension == ".c") {
        return NavigationFileKind::c_source;
    }
    if (extension == ".cc" || extension == ".cpp" || extension == ".cxx") {
        return NavigationFileKind::cpp_source;
    }
    if (extension == ".h" || extension == ".hh" || extension == ".hpp" || extension == ".hxx") {
        return NavigationFileKind::header;
    }
    if (extension == ".md" || extension == ".markdown") {
        return NavigationFileKind::markdown;
    }
    if (name == "cmakelists.txt" || extension == ".cmake") {
        return NavigationFileKind::cmake;
    }
    if (extension == ".json" || extension == ".jsonl") {
        return NavigationFileKind::json;
    }
    if (extension == ".txt" || extension == ".toml" || extension == ".yaml" ||
        extension == ".yml") {
        return NavigationFileKind::text;
    }
    if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".gif" ||
        extension == ".pdf" || extension == ".zip") {
        return NavigationFileKind::binary;
    }
    return NavigationFileKind::other;
}

auto navigation_file_kind_name(NavigationFileKind kind) -> std::string_view {
    switch (kind) {
    case NavigationFileKind::directory:
        return "dir";
    case NavigationFileKind::c_source:
        return "C";
    case NavigationFileKind::cpp_source:
        return "C++";
    case NavigationFileKind::header:
        return "header";
    case NavigationFileKind::markdown:
        return "md";
    case NavigationFileKind::cmake:
        return "cmake";
    case NavigationFileKind::json:
        return "json";
    case NavigationFileKind::text:
        return "text";
    case NavigationFileKind::binary:
        return "binary";
    case NavigationFileKind::other:
        return "file";
    }
    return "file";
}

auto navigation_drive_state_name(NavigationDriveState state) -> std::string_view {
    switch (state) {
    case NavigationDriveState::unavailable:
        return "";
    case NavigationDriveState::online_only:
        return "online only";
    case NavigationDriveState::available_offline:
        return "offline";
    case NavigationDriveState::not_synced_here:
        return "not here";
    }
    return "";
}

auto navigation_change_state_name(NavigationChangeState state) -> std::string_view {
    switch (state) {
    case NavigationChangeState::clean:
        return "";
    case NavigationChangeState::modified:
        return "modified";
    case NavigationChangeState::in_change:
        return "in change";
    case NavigationChangeState::conflicted:
        return "conflicted";
    }
    return "";
}

auto rank_paths(std::span<const NavigationPath> paths, std::string_view query, std::size_t limit)
    -> std::vector<PathMatch> {
    auto output = std::vector<PathMatch>{};
    output.reserve(paths.size());
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (paths[index].kind == NavigationFileKind::directory) {
            continue;
        }
        if (const auto score = path_score(paths[index].path, query)) {
            output.push_back({index, *score});
        }
    }
    std::ranges::sort(output, [&](const auto& left, const auto& right) {
        const auto& left_path = paths[left.index].path;
        const auto& right_path = paths[right.index].path;
        return std::tuple{-left.score,
                          left_path.generic_string().size(),
                          left_path.generic_string()} <
               std::tuple{-right.score,
                          right_path.generic_string().size(),
                          right_path.generic_string()};
    });
    if (output.size() > limit) {
        output.resize(limit);
    }
    return output;
}

auto rank_symbols(std::span<const NavigationSymbol> symbols,
                  std::string_view query,
                  std::size_t limit) -> std::vector<SymbolMatch> {
    auto output = std::vector<SymbolMatch>{};
    output.reserve(symbols.size());
    for (std::size_t index = 0; index < symbols.size(); ++index) {
        if (!symbols[index].definition) {
            continue;
        }
        auto score = fuzzy_score(symbols[index].name, query);
        if (!score) {
            continue;
        }
        *score += 25;
        output.push_back({index, *score});
    }
    std::ranges::sort(output, [&](const auto& left, const auto& right) {
        const auto& left_symbol = symbols[left.index];
        const auto& right_symbol = symbols[right.index];
        return std::tuple{-left.score,
                          left_symbol.name,
                          left_symbol.path.generic_string(),
                          left_symbol.range.start_line} <
               std::tuple{-right.score,
                          right_symbol.name,
                          right_symbol.path.generic_string(),
                          right_symbol.range.start_line};
    });
    if (output.size() > limit) {
        output.resize(limit);
    }
    return output;
}

auto identifier_at(std::string_view source, std::size_t byte) -> std::string {
    if (source.empty()) {
        return {};
    }
    byte = std::min(byte, source.size() - 1);
    const auto identifier = [](char character) {
        const auto value = static_cast<unsigned char>(character);
        return std::isalnum(value) != 0 || character == '_';
    };
    if (!identifier(source[byte]) && byte > 0 && identifier(source[byte - 1])) {
        --byte;
    }
    if (!identifier(source[byte])) {
        return {};
    }
    auto first = byte;
    auto last = byte + 1;
    while (first > 0 && identifier(source[first - 1])) {
        --first;
    }
    while (last < source.size() && identifier(source[last])) {
        ++last;
    }
    return std::string(source.substr(first, last - first));
}

auto enclosing_symbol(std::span<const NavigationSymbol> symbols,
                      const std::filesystem::path& path,
                      std::uint64_t byte) -> const NavigationSymbol* {
    const NavigationSymbol* selected = nullptr;
    auto selected_size = std::numeric_limits<std::uint64_t>::max();
    for (const auto& symbol : symbols) {
        if (!symbol.definition || symbol.path != path || symbol.range.start_byte > byte ||
            byte > symbol.range.end_byte) {
            continue;
        }
        const auto size = symbol.range.end_byte - symbol.range.start_byte;
        if (size < selected_size) {
            selected = &symbol;
            selected_size = size;
        }
    }
    return selected;
}

auto symbol_references(std::span<const NavigationSymbol> symbols,
                       std::string_view id,
                       std::string_view name) -> std::vector<NavigationSymbol> {
    auto output = std::vector<NavigationSymbol>{};
    for (const auto& symbol : symbols) {
        if (matches_symbol(symbol, id, name)) {
            append_unique(output, symbol, symbol_key);
        }
    }
    std::ranges::sort(output, [](const auto& left, const auto& right) {
        return std::tuple{!left.definition,
                          left.path.generic_string(),
                          left.range.start_line,
                          left.range.start_column} < std::tuple{!right.definition,
                                                                right.path.generic_string(),
                                                                right.range.start_line,
                                                                right.range.start_column};
    });
    return output;
}

auto call_hierarchy(std::span<const NavigationSymbol> symbols,
                    std::string_view id,
                    std::string_view name) -> CallHierarchy {
    auto output = CallHierarchy{};
    auto target_ids = std::set<std::string>{};
    for (const auto& symbol : symbols) {
        if (matches_symbol(symbol, id, name)) {
            target_ids.insert(symbol.id);
        }
    }
    for (const auto& occurrence : symbols) {
        if (!occurrence.definition && target_ids.contains(occurrence.id) && occurrence.container) {
            if (const auto* caller = definition_for(symbols, *occurrence.container)) {
                append_unique(output.callers, *caller, symbol_key);
            }
        }
        if (!occurrence.definition && occurrence.container &&
            target_ids.contains(*occurrence.container)) {
            if (const auto* callee = definition_for(symbols, occurrence.id)) {
                append_unique(output.callees, *callee, symbol_key);
            }
        }
    }
    const auto order = [](const auto& left, const auto& right) {
        return std::tuple{left.name, left.path.generic_string(), left.range.start_line} <
               std::tuple{right.name, right.path.generic_string(), right.range.start_line};
    };
    std::ranges::sort(output.callers, order);
    std::ranges::sort(output.callees, order);
    return output;
}

auto breadcrumbs(const std::filesystem::path& path, std::optional<std::string_view> symbol)
    -> std::vector<Breadcrumb> {
    auto output = std::vector<Breadcrumb>{};
    auto accumulated = std::filesystem::path{};
    for (const auto& component : path.lexically_normal()) {
        if (component == ".") {
            continue;
        }
        accumulated /= component;
        output.push_back({component.generic_string(), accumulated, false});
    }
    if (symbol && !symbol->empty()) {
        output.push_back({std::string(*symbol), path, true});
    }
    return output;
}

auto navigation_commands() -> std::span<const NavigationCommand> {
    static constexpr auto commands = std::array{
        NavigationCommand{NavigationAction::quick_open, "Navigation: Quick open", "Primary+P"},
        NavigationCommand{NavigationAction::symbol_search,
                          "Navigation: Symbol search",
                          "Primary+T"},
        NavigationCommand{NavigationAction::go_to_definition,
                          "Navigation: Go to definition",
                          "F12"},
        NavigationCommand{NavigationAction::find_references,
                          "Navigation: Find references",
                          "Shift+F12"},
        NavigationCommand{NavigationAction::call_hierarchy,
                          "Navigation: Call hierarchy",
                          "Alt+F12"},
        NavigationCommand{NavigationAction::focus_breadcrumbs,
                          "Navigation: Focus breadcrumbs",
                          "Primary+Shift+."},
        NavigationCommand{NavigationAction::open_recent, "Navigation: Open recent", "Primary+R"},
        NavigationCommand{NavigationAction::back, "Navigation: Back", "Alt+Left"},
        NavigationCommand{NavigationAction::forward, "Navigation: Forward", "Alt+Right"},
        NavigationCommand{NavigationAction::open, "Repository: Open", "Enter"},
        NavigationCommand{NavigationAction::rename, "Repository: Rename", "F2"},
        NavigationCommand{NavigationAction::move, "Repository: Move", "Primary+Shift+M"},
        NavigationCommand{NavigationAction::new_file, "Repository: New file", "Primary+N"},
        NavigationCommand{NavigationAction::reveal, "Repository: Reveal", "Primary+Alt+R"},
    };
    return commands;
}

void NavigationHistory::visit(NavigationLocation location) {
    if (current_ && *current_ != location) {
        back_.push_back(*current_);
        forward_.clear();
    }
    current_ = std::move(location);
    touch_recent(*current_);
}

void NavigationHistory::replace_current(NavigationLocation location) {
    current_ = std::move(location);
    touch_recent(*current_);
}

auto NavigationHistory::back() -> std::optional<NavigationLocation> {
    if (back_.empty()) {
        return std::nullopt;
    }
    if (current_) {
        forward_.push_back(*current_);
    }
    current_ = back_.back();
    back_.pop_back();
    touch_recent(*current_);
    return current_;
}

auto NavigationHistory::forward() -> std::optional<NavigationLocation> {
    if (forward_.empty()) {
        return std::nullopt;
    }
    if (current_) {
        back_.push_back(*current_);
    }
    current_ = forward_.back();
    forward_.pop_back();
    touch_recent(*current_);
    return current_;
}

auto NavigationHistory::recent() const -> std::span<const NavigationLocation> {
    return recent_;
}

auto NavigationHistory::can_go_back() const -> bool {
    return !back_.empty();
}

auto NavigationHistory::can_go_forward() const -> bool {
    return !forward_.empty();
}

void NavigationHistory::touch_recent(const NavigationLocation& location) {
    const auto found = std::ranges::find(recent_, location.path, &NavigationLocation::path);
    if (found != recent_.end()) {
        recent_.erase(found);
    }
    recent_.insert(recent_.begin(), location);
    constexpr auto recent_limit = std::size_t{20};
    if (recent_.size() > recent_limit) {
        recent_.resize(recent_limit);
    }
}

} // namespace liquify
