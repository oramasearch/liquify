#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/structural_edit.hpp"

namespace {

auto fail(const liquid::error &error) -> int {
  std::println(stderr, "structural edit measurement: {}: {}", error.context,
               error.message);
  return 1;
}

auto prepare_repository(const std::filesystem::path &destination)
    -> liquid::result<void> {
  if (std::filesystem::exists(destination)) {
    return std::unexpected(liquid::error{liquid::errc::already_exists,
                                         "measurement repository must be fresh",
                                         destination.string()});
  }
  auto created =
      liquid::runtime::block_on(liquid::repository::create(destination));
  if (!created) {
    return std::unexpected(created.error());
  }
  return {};
}

auto read_file(const std::filesystem::path &path)
    -> liquid::result<std::vector<std::byte>> {
  auto input = std::ifstream(path, std::ios::binary);
  if (!input) {
    return std::unexpected(liquid::error{
        liquid::errc::io, "cannot read measurement source", path.string()});
  }
  const auto characters =
      std::vector<char>(std::istreambuf_iterator<char>(input), {});
  const auto value =
      std::as_bytes(std::span(characters.data(), characters.size()));
  return std::vector<std::byte>(value.begin(), value.end());
}

auto text(std::span<const std::byte> value) -> std::string {
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

auto statement_kind(std::string_view kind) -> bool {
  return kind == "declaration" || kind.ends_with("_statement");
}

struct ExtractCandidate {
  liquify::SourceRange range;
  std::uint32_t lines{};
};

auto find_extract_candidate(liquify::StructuralEditor &editor,
                            liquify::InteractiveAnalysis &analysis,
                            const std::filesystem::path &path)
    -> liquid::result<ExtractCandidate> {
  auto nodes = analysis.syntax_nodes(path);
  if (!nodes) {
    return std::unexpected(nodes.error());
  }
  auto statements = std::vector<liquify::SyntaxNodeInfo>{};
  for (const auto &node : *nodes) {
    if (statement_kind(node.kind)) {
      statements.push_back(node);
    }
  }
  std::ranges::sort(statements, {},
                    [](const auto &node) { return node.range.start_byte; });
  for (const auto &statement : statements) {
    const auto lines =
        statement.range.end_line - statement.range.start_line + 1;
    if (lines < 30 || lines > 120) {
      continue;
    }
    auto preview = editor.extract_function(path, statement.range,
                                           "liquify_measured_extract");
    if (preview && preview->diagnostics.empty() && !preview->edits.empty()) {
      return ExtractCandidate{statement.range, lines};
    }
  }
  for (std::size_t first = 0; first < statements.size(); ++first) {
    for (std::size_t last = first; last < statements.size(); ++last) {
      const auto lines = statements[last].range.end_line -
                         statements[first].range.start_line + 1;
      if (lines < 30) {
        continue;
      }
      if (lines > 120) {
        break;
      }
      const auto range =
          liquify::SourceRange{statements[first].range.start_byte,
                               statements[last].range.end_byte,
                               statements[first].range.start_line,
                               statements[first].range.start_column,
                               statements[last].range.end_line,
                               statements[last].range.end_column};
      auto preview =
          editor.extract_function(path, range, "liquify_measured_extract");
      if (preview && preview->diagnostics.empty() && !preview->edits.empty()) {
        return ExtractCandidate{range, lines};
      }
    }
  }
  return std::unexpected(liquid::error{liquid::errc::not_found,
                                       "no extractable 30-line statement range",
                                       path.string()});
}

} // namespace

int main(int argc, char **argv) {
  if (argc != 3) {
    std::println(stderr,
                 "usage: liquify-structural-edit-bench <liquid-checkout> "
                 "<fresh-repository-state>");
    return 2;
  }
  const auto checkout = std::filesystem::absolute(argv[1]);
  const auto repository_state = std::filesystem::absolute(argv[2]);
  auto prepared = prepare_repository(repository_state);
  if (!prepared) {
    return fail(prepared.error());
  }
  auto repository =
      liquid::runtime::block_on(liquid::repository::open(repository_state));
  if (!repository) {
    return fail(repository.error());
  }
  const auto rename_path =
      std::filesystem::path{"src/repository/repository_impl.cpp"};
  const auto extract_paths = std::vector<std::filesystem::path>{
      "src/graph.cpp",
      "src/daemon.cpp",
      "src/store/store_impl.cpp",
      "src/reapi/server.cpp",
  };
  auto rename_input = read_file(checkout / rename_path);
  if (!rename_input) {
    return fail(rename_input.error());
  }
  auto written = liquid::runtime::block_on(
      repository->write(rename_path.string(), std::move(*rename_input)));
  if (!written) {
    return fail(written.error());
  }
  for (const auto &extract_path : extract_paths) {
    auto extract_input = read_file(checkout / extract_path);
    if (!extract_input) {
      return fail(extract_input.error());
    }
    written = liquid::runtime::block_on(
        repository->write(extract_path.string(), std::move(*extract_input)));
    if (!written) {
      return fail(written.error());
    }
  }
  auto rename_source =
      liquid::runtime::block_on(repository->read(rename_path.string()));
  if (!rename_source) {
    return fail(rename_source.error());
  }
  auto analysis = liquify::InteractiveAnalysis::open(
      repository_state / ".liquid" / "structural-bench-graph");
  if (!analysis) {
    return fail(analysis.error());
  }
  auto editor = liquify::StructuralEditor(repository_state, *analysis);
  auto opened = editor.open_buffer(rename_path, liquify::StubLanguage::cpp,
                                   text(*rename_source));
  if (!opened) {
    return fail(opened.error());
  }
  auto nodes = analysis->syntax_nodes(rename_path);
  if (!nodes) {
    return fail(nodes.error());
  }
  auto counts =
      std::map<std::string, std::pair<std::uint32_t, std::uint32_t>>{};
  for (const auto &node : *nodes) {
    if (node.kind != "identifier") {
      continue;
    }
    auto &[count, first] = counts[node.spelling];
    if (count == 0) {
      first = node.range.start_byte;
    }
    ++count;
  }
  const auto symbol = std::ranges::max_element(
      counts, {}, [](const auto &entry) { return entry.second.first; });
  if (symbol == counts.end() || symbol->second.first < 50) {
    std::println(stderr,
                 "structural edit measurement: no symbol has 50 references");
    return 1;
  }
  const auto rename_started = std::chrono::steady_clock::now();
  auto rename = editor.rename(rename_path, symbol->second.second,
                              "liquify_measured_" + symbol->first);
  if (!rename) {
    return fail(rename.error());
  }
  auto renamed = editor.apply(rename->edits);
  if (!renamed) {
    return fail(renamed.error());
  }
  const auto rename_finished = std::chrono::steady_clock::now();

  auto extract_path = std::filesystem::path{};
  auto extract_candidate = ExtractCandidate{};
  auto found_candidate = false;
  for (const auto &path : extract_paths) {
    auto extract_source =
        liquid::runtime::block_on(repository->read(path.string()));
    if (!extract_source) {
      return fail(extract_source.error());
    }
    opened = editor.open_buffer(path, liquify::StubLanguage::cpp,
                                text(*extract_source));
    if (!opened) {
      return fail(opened.error());
    }
    auto found = find_extract_candidate(editor, *analysis, path);
    if (!found) {
      continue;
    }
    extract_path = path;
    extract_candidate = *found;
    found_candidate = true;
    break;
  }
  if (!found_candidate) {
    std::println(stderr, "structural edit measurement: no extractable 30-line "
                         "statement range in Liquid sources");
    return 1;
  }
  const auto extract_started = std::chrono::steady_clock::now();
  auto extract = editor.extract_function(extract_path, extract_candidate.range,
                                         "liquify_measured_extract");
  if (!extract) {
    return fail(extract.error());
  }
  auto extracted = editor.apply(extract->edits);
  if (!extracted) {
    return fail(extracted.error());
  }
  const auto extract_finished = std::chrono::steady_clock::now();

  const auto rename_ms = std::chrono::duration<double, std::milli>(
                             rename_finished - rename_started)
                             .count();
  const auto extract_ms = std::chrono::duration<double, std::milli>(
                              extract_finished - extract_started)
                              .count();
  std::println("structural edit: rename_symbol={} references={} applied={} "
               "rename_ms={:.3f} extract_lines={} extract_edits={} "
               "extract_ms={:.3f}",
               symbol->first, rename->edits.size(), renamed->applied.size(),
               rename_ms, extract_candidate.lines, extracted->applied.size(),
               extract_ms);
  return rename->edits.size() < 50 || renamed->refused.size() != 0 ||
                 extracted->refused.size() != 0
             ? 1
             : 0;
}
