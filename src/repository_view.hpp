#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/object.hpp"

namespace liquify {

class RepositoryView {
public:
    [[nodiscard]] static RepositoryView open(std::filesystem::path path);

    [[nodiscard]] auto entries() const -> std::span<const liquid::tree_entry>;
    [[nodiscard]] auto error() const -> std::string_view;

private:
    [[nodiscard]] static RepositoryView load(std::filesystem::path path);

    std::vector<liquid::tree_entry> entries_;
    std::string error_;
};

[[nodiscard]] auto node_kind_name(liquid::node_kind kind) -> std::string_view;

} // namespace liquify
