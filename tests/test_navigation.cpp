// SPDX-License-Identifier: MIT

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <algorithm>

#include "liquify/command.hpp"
#include "liquify/navigation.hpp"

namespace liquify {
namespace {

TEST(Navigation, FuzzyPathsPreferTheLeafAndCompactMatch) {
    const auto paths = std::array{
        NavigationPath{"docs/ui/app-shell.md", NavigationFileKind::markdown},
        NavigationPath{"src/application_state.cpp", NavigationFileKind::cpp_source},
        NavigationPath{"src/app.cpp", NavigationFileKind::cpp_source},
        NavigationPath{"src", NavigationFileKind::directory},
    };

    const auto matches = rank_paths(paths, "app");
    ASSERT_EQ(matches.size(), 3U);
    EXPECT_EQ(paths[matches[0].index].path, "src/app.cpp");
    EXPECT_EQ(paths[matches[1].index].path, "docs/ui/app-shell.md");
    EXPECT_GT(matches[0].score, matches[1].score);
}

TEST(Navigation, SymbolMatchesPreferDefinitionsAndStableLocations) {
    const auto symbols = std::array{
        NavigationSymbol{"render",
                         "render",
                         NavigationSymbolKind::function,
                         "src/z.cpp",
                         {},
                         {},
                         false},
        NavigationSymbol{"reader",
                         "read_repository",
                         NavigationSymbolKind::function,
                         "src/repository.cpp",
                         {},
                         {},
                         true},
        NavigationSymbol{"render",
                         "render",
                         NavigationSymbolKind::function,
                         "src/app.cpp",
                         {},
                         {},
                         true},
    };

    const auto matches = rank_symbols(symbols, "rend");
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_TRUE(symbols[matches[0].index].definition);
    EXPECT_EQ(symbols[matches[0].index].path, "src/app.cpp");
}

TEST(Navigation, FindsIdentifierAtAndAfterTheCursor) {
    constexpr auto source = std::string_view{"return repository_value;"};
    EXPECT_EQ(identifier_at(source, 10), "repository_value");
    EXPECT_EQ(identifier_at(source, 23), "repository_value");
    EXPECT_TRUE(identifier_at(" + ", 1).empty());
}

TEST(Navigation, OrdersReferencesWithDefinitionFirst) {
    const auto symbols = std::array{
        NavigationSymbol{"render",
                         "render",
                         NavigationSymbolKind::function,
                         "src/z.cpp",
                         {40, 46, 4, 2, 4, 8},
                         {},
                         false},
        NavigationSymbol{"render",
                         "render",
                         NavigationSymbolKind::function,
                         "src/app.cpp",
                         {10, 16, 1, 0, 1, 6},
                         {},
                         true},
        NavigationSymbol{"render",
                         "render",
                         NavigationSymbolKind::function,
                         "src/a.cpp",
                         {20, 26, 2, 0, 2, 6},
                         {},
                         false},
    };

    const auto references = symbol_references(symbols, "render", "render");
    ASSERT_EQ(references.size(), 3U);
    EXPECT_TRUE(references[0].definition);
    EXPECT_EQ(references[1].path, "src/a.cpp");
    EXPECT_EQ(references[2].path, "src/z.cpp");
}

TEST(Navigation, SelectsTheSmallestEnclosingDefinition) {
    const auto symbols = std::array{
        NavigationSymbol{"outer",
                         "outer",
                         NavigationSymbolKind::function,
                         "src/app.cpp",
                         {5, 90, 1, 0, 9, 1},
                         {},
                         true},
        NavigationSymbol{"inner",
                         "inner",
                         NavigationSymbolKind::function,
                         "src/app.cpp",
                         {25, 55, 3, 2, 5, 3},
                         "outer",
                         true},
        NavigationSymbol{"other",
                         "other",
                         NavigationSymbolKind::function,
                         "src/other.cpp",
                         {20, 60, 2, 0, 6, 1},
                         {},
                         true},
    };

    const auto* selected = enclosing_symbol(symbols, "src/app.cpp", 40);
    ASSERT_NE(selected, nullptr);
    EXPECT_EQ(selected->id, "inner");
}

TEST(Navigation, BuildsCallerAndCalleeOrderFromSymbolContainers) {
    const auto symbols = std::array{
        NavigationSymbol{"caller",
                         "caller",
                         NavigationSymbolKind::function,
                         "src/a.cpp",
                         {},
                         {},
                         true},
        NavigationSymbol{"target",
                         "target",
                         NavigationSymbolKind::function,
                         "src/b.cpp",
                         {},
                         {},
                         true},
        NavigationSymbol{"callee",
                         "callee",
                         NavigationSymbolKind::function,
                         "src/c.cpp",
                         {},
                         {},
                         true},
        NavigationSymbol{"target",
                         "target",
                         NavigationSymbolKind::function,
                         "src/a.cpp",
                         {},
                         "caller",
                         false},
        NavigationSymbol{"callee",
                         "callee",
                         NavigationSymbolKind::function,
                         "src/b.cpp",
                         {},
                         "target",
                         false},
    };

    const auto hierarchy = call_hierarchy(symbols, "target", "target");
    ASSERT_EQ(hierarchy.callers.size(), 1U);
    ASSERT_EQ(hierarchy.callees.size(), 1U);
    EXPECT_EQ(hierarchy.callers[0].name, "caller");
    EXPECT_EQ(hierarchy.callees[0].name, "callee");
}

TEST(Navigation, BackForwardAndRecentRetainCursorLocations) {
    auto history = NavigationHistory{};
    history.visit({"src/a.cpp", 4, 2});
    history.visit({"src/b.cpp", 8, 1});
    history.replace_current({"src/b.cpp", 9, 3});

    ASSERT_TRUE(history.can_go_back());
    const auto previous = history.back();
    ASSERT_TRUE(previous);
    EXPECT_EQ(*previous, (NavigationLocation{"src/a.cpp", 4, 2}));
    ASSERT_TRUE(history.can_go_forward());
    const auto next = history.forward();
    ASSERT_TRUE(next);
    EXPECT_EQ(*next, (NavigationLocation{"src/b.cpp", 9, 3}));
    ASSERT_EQ(history.recent().size(), 2U);
    EXPECT_EQ(history.recent()[0].path, "src/b.cpp");
}

TEST(Navigation, BreadcrumbsIncludeFoldersFileAndSymbol) {
    const auto values = breadcrumbs("src/ui/app.cpp", "draw_sidebar");
    ASSERT_EQ(values.size(), 4U);
    EXPECT_EQ(values[0].label, "src");
    EXPECT_EQ(values[2].path, "src/ui/app.cpp");
    EXPECT_TRUE(values[3].symbol);
    EXPECT_EQ(values[3].label, "draw_sidebar");
}

// liquify#34: navigation actions are registry commands, found, bound and searched like any other.
TEST(Navigation, EveryNavigationActionIsARegistryCommandWithAUniqueBinding) {
    auto registry = CommandRegistry(CommandPlatform::macos);
    ASSERT_TRUE(add_builtin_commands(registry).empty());
    const auto commands = navigation_commands();
    EXPECT_EQ(commands.size(), 14U);
    auto chords = std::vector<KeyChord>{};
    for (const auto& command : commands) {
        const auto* definition = registry.find(command.name);
        ASSERT_NE(definition, nullptr) << command.name;
        const auto bindings = registry.bindings(command.name);
        ASSERT_FALSE(bindings.empty()) << command.name;
        EXPECT_EQ(registry.command_for(bindings.front()), definition) << command.name;
        EXPECT_EQ(std::ranges::find(chords, bindings.front()), chords.end()) << command.name;
        chords.push_back(bindings.front());
        const auto found = registry.search(definition->title);
        ASSERT_FALSE(found.empty()) << command.name;
        EXPECT_EQ(found.front().name, definition->name);
    }
    EXPECT_EQ(registry.find("repository.open")->scope, CommandScope::repository);
}

} // namespace
} // namespace liquify
