// SPDX-License-Identifier: MIT

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "liquify/stub_tree.hpp"

namespace {

auto source_files(const std::filesystem::path &root)
    -> std::vector<std::filesystem::path> {
  auto output = std::vector<std::filesystem::path>{};
  auto error = std::error_code{};
  auto iterator = std::filesystem::recursive_directory_iterator{
      root, std::filesystem::directory_options::skip_permission_denied, error};
  for (auto end = std::filesystem::recursive_directory_iterator{};
       iterator != end; iterator.increment(error)) {
    if (error) {
      std::cerr << "could not read " << iterator->path() << ": "
                << error.message() << '\n';
      error.clear();
      continue;
    }
    if (iterator->is_directory() &&
        (iterator->path().filename() == ".git" ||
         iterator->path().filename() == "build" ||
         iterator->path().filename() == "modules")) {
      iterator.disable_recursion_pending();
      continue;
    }
    if (iterator->is_regular_file() &&
        liquify::stub_language_for_path(iterator->path())) {
      output.push_back(iterator->path());
    }
  }
  std::ranges::sort(output);
  return output;
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::cerr << "usage: liquify-stub-tree-bench <source-root> "
                 "<graph-state-directory>\n";
    return 2;
  }
  const auto root = std::filesystem::absolute(argv[1]);
  const auto state = std::filesystem::absolute(argv[2]);
  auto trees = liquify::StubTrees::open(state);
  if (!trees) {
    std::cerr << trees.error().message << '\n';
    return 1;
  }
  const auto files = source_files(root);
  auto resolver = liquify::StubResolver{};
  auto names = std::vector<std::string>{};
  const auto extraction_start = std::chrono::steady_clock::now();
  for (const auto &path : files) {
    auto tree = trees->for_file(path);
    if (!tree) {
      std::cerr << path << ": " << tree.error().message << '\n';
      return 1;
    }
    for (const auto &declaration : tree->declarations) {
      names.push_back(declaration.name);
    }
    resolver.add(std::filesystem::relative(path, root), std::move(*tree));
  }
  const auto extraction_elapsed =
      std::chrono::steady_clock::now() - extraction_start;
  if (names.empty()) {
    std::cerr << "no declarations found\n";
    return 1;
  }
  const auto lookup_start = std::chrono::steady_clock::now();
  auto resolved = std::size_t{};
  for (std::size_t index = 0; index < 1000; ++index) {
    resolved += resolver.resolve(names[index % names.size()]).size();
  }
  const auto lookup_elapsed = std::chrono::steady_clock::now() - lookup_start;
  const auto extraction_us =
      std::chrono::duration_cast<std::chrono::microseconds>(extraction_elapsed);
  const auto lookup_us =
      std::chrono::duration_cast<std::chrono::microseconds>(lookup_elapsed);
  const auto per_blob =
      files.empty() ? 0.0
                    : static_cast<double>(extraction_us.count()) / files.size();
  std::cout << "stub extraction: blobs=" << files.size()
            << " elapsed_us=" << extraction_us.count()
            << " per_blob_us=" << per_blob << '\n';
  std::cout << "stub resolution: lookups=1000 elapsed_us=" << lookup_us.count()
            << " per_lookup_us="
            << static_cast<double>(lookup_us.count()) / 1000.0
            << " resolved=" << resolved << '\n';
  return 0;
}
