#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace liquify {

enum class NavigationFileKind : std::uint8_t {
    directory,
    c_source,
    cpp_source,
    header,
    markdown,
    cmake,
    json,
    text,
    binary,
    other,
};

enum class NavigationDriveState : std::uint8_t {
    unavailable,
    online_only,
    available_offline,
    not_synced_here,
};

enum class NavigationChangeState : std::uint8_t {
    clean,
    modified,
    in_change,
    conflicted,
};

enum class NavigationSymbolKind : std::uint8_t {
    unknown,
    function,
    type,
    variable,
    field,
    parameter,
    namespace_,
};

struct NavigationRange {
    std::uint64_t start_byte{};
    std::uint64_t end_byte{};
    std::uint64_t start_line{};
    std::uint64_t start_column{};
    std::uint64_t end_line{};
    std::uint64_t end_column{};
};

struct NavigationPath {
    std::filesystem::path path;
    NavigationFileKind kind{NavigationFileKind::other};
    NavigationDriveState drive{NavigationDriveState::unavailable};
    NavigationChangeState change{NavigationChangeState::clean};
    std::string blob_id;
};

struct NavigationSymbol {
    std::string id;
    std::string name;
    NavigationSymbolKind kind{NavigationSymbolKind::unknown};
    std::filesystem::path path;
    NavigationRange range;
    std::optional<std::string> container;
    bool definition{};
};

struct NavigationLocation {
    std::filesystem::path path;
    std::uint64_t line{};
    std::uint64_t column{};

    auto operator==(const NavigationLocation&) const -> bool = default;
};

struct PathMatch {
    std::size_t index{};
    int score{};
};

struct SymbolMatch {
    std::size_t index{};
    int score{};
};

struct Breadcrumb {
    std::string label;
    std::filesystem::path path;
    bool symbol{};
};

struct CallHierarchy {
    std::vector<NavigationSymbol> callers;
    std::vector<NavigationSymbol> callees;
};

enum class NavigationAction : std::uint8_t {
    quick_open,
    symbol_search,
    go_to_definition,
    find_references,
    call_hierarchy,
    focus_breadcrumbs,
    open_recent,
    back,
    forward,
    open,
    rename,
    move,
    new_file,
    reveal,
};

struct NavigationCommand {
    NavigationAction action;
    std::string_view name;
    std::string_view default_binding;
};

[[nodiscard]] auto navigation_file_kind(const std::filesystem::path& path, bool directory)
    -> NavigationFileKind;
[[nodiscard]] auto navigation_file_kind_name(NavigationFileKind kind) -> std::string_view;
[[nodiscard]] auto navigation_drive_state_name(NavigationDriveState state) -> std::string_view;
[[nodiscard]] auto navigation_change_state_name(NavigationChangeState state) -> std::string_view;
[[nodiscard]] auto rank_paths(std::span<const NavigationPath> paths,
                              std::string_view query,
                              std::size_t limit = 20) -> std::vector<PathMatch>;
[[nodiscard]] auto rank_symbols(std::span<const NavigationSymbol> symbols,
                                std::string_view query,
                                std::size_t limit = 20) -> std::vector<SymbolMatch>;
[[nodiscard]] auto identifier_at(std::string_view source, std::size_t byte) -> std::string;
[[nodiscard]] auto enclosing_symbol(std::span<const NavigationSymbol> symbols,
                                    const std::filesystem::path& path,
                                    std::uint64_t byte) -> const NavigationSymbol*;
[[nodiscard]] auto symbol_references(std::span<const NavigationSymbol> symbols,
                                     std::string_view id,
                                     std::string_view name) -> std::vector<NavigationSymbol>;
[[nodiscard]] auto call_hierarchy(std::span<const NavigationSymbol> symbols,
                                  std::string_view id,
                                  std::string_view name) -> CallHierarchy;
[[nodiscard]] auto breadcrumbs(const std::filesystem::path& path,
                               std::optional<std::string_view> symbol = {})
    -> std::vector<Breadcrumb>;
[[nodiscard]] auto navigation_commands() -> std::span<const NavigationCommand>;

class NavigationHistory {
public:
    void visit(NavigationLocation location);
    void replace_current(NavigationLocation location);
    [[nodiscard]] auto back() -> std::optional<NavigationLocation>;
    [[nodiscard]] auto forward() -> std::optional<NavigationLocation>;
    [[nodiscard]] auto recent() const -> std::span<const NavigationLocation>;
    [[nodiscard]] auto can_go_back() const -> bool;
    [[nodiscard]] auto can_go_forward() const -> bool;

private:
    void touch_recent(const NavigationLocation& location);

    std::optional<NavigationLocation> current_;
    std::vector<NavigationLocation> back_;
    std::vector<NavigationLocation> forward_;
    std::vector<NavigationLocation> recent_;
};

} // namespace liquify
