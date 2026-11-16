#include "liquify/stub_tree.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include <tree_sitter/api.h>

#include "liquid/runtime.hpp"

extern "C" {
const TSLanguage *tree_sitter_c();
const TSLanguage *tree_sitter_cpp();
}

namespace liquify {
namespace {

constexpr auto stub_query_kind = "liquify.stub-tree/2";
constexpr auto stub_version = std::byte{2};

auto bytes(std::string_view value) -> std::vector<std::byte> {
  auto output = std::vector<std::byte>(value.size());
  for (std::size_t index = 0; index < value.size(); ++index) {
    output[index] = static_cast<std::byte>(value[index]);
  }
  return output;
}

auto text(std::span<const std::byte> value) -> std::string {
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

auto failure(std::string message, std::string context = {}) -> liquid::error {
  return {liquid::errc::invalid_argument, std::move(message),
          std::move(context)};
}

auto append_u32(std::vector<std::byte> &output, std::uint32_t value) -> void {
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    output.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
  }
}

auto append_string(std::vector<std::byte> &output, std::string_view value)
    -> void {
  append_u32(output, static_cast<std::uint32_t>(value.size()));
  const auto encoded = bytes(value);
  output.insert(output.end(), encoded.begin(), encoded.end());
}

auto read_u32(std::span<const std::byte> input, std::size_t &cursor)
    -> liquid::result<std::uint32_t> {
  if (input.size() - cursor < 4) {
    return std::unexpected(failure("truncated stub tree"));
  }
  auto value = std::uint32_t{};
  for (unsigned int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(input[cursor++]) << shift;
  }
  return value;
}

auto read_string(std::span<const std::byte> input, std::size_t &cursor)
    -> liquid::result<std::string> {
  auto length = read_u32(input, cursor);
  if (!length) {
    return std::unexpected(length.error());
  }
  if (*length > input.size() - cursor) {
    return std::unexpected(failure("truncated stub string"));
  }
  const auto value = text(input.subspan(cursor, *length));
  cursor += *length;
  return value;
}

auto encode(StubLanguage language,
            const std::vector<StubDeclaration> &declarations)
    -> std::vector<std::byte> {
  auto output =
      std::vector<std::byte>{stub_version, static_cast<std::byte>(language)};
  append_u32(output, static_cast<std::uint32_t>(declarations.size()));
  for (const auto &declaration : declarations) {
    output.push_back(static_cast<std::byte>(declaration.kind));
    append_string(output, declaration.name);
    append_u32(output,
               static_cast<std::uint32_t>(declaration.modifiers.size()));
    for (const auto &modifier : declaration.modifiers) {
      append_string(output, modifier);
    }
    append_string(output, declaration.signature);
    append_string(output, declaration.documentation);
    append_u32(output, declaration.range.start_byte);
    append_u32(output, declaration.range.end_byte);
    append_u32(output, declaration.range.start_line);
    append_u32(output, declaration.range.start_column);
    append_u32(output, declaration.range.end_line);
    append_u32(output, declaration.range.end_column);
  }
  return output;
}

auto decode(std::span<const std::byte> input, liquid::digest blob)
    -> liquid::result<StubTree> {
  if (input.size() < 6 || input[0] != stub_version) {
    return std::unexpected(failure("unsupported stub tree encoding"));
  }
  const auto language = static_cast<StubLanguage>(input[1]);
  if (language != StubLanguage::c && language != StubLanguage::cpp) {
    return std::unexpected(failure("unsupported stub tree language"));
  }
  auto cursor = std::size_t{2};
  auto count = read_u32(input, cursor);
  if (!count) {
    return std::unexpected(count.error());
  }
  auto declarations = std::vector<StubDeclaration>{};
  // Each declaration takes at least one byte: never reserve more than the input holds (liquify#79).
  declarations.reserve(std::min<std::size_t>(*count, input.size() - cursor));
  for (std::uint32_t index = 0; index < *count; ++index) {
    if (cursor == input.size()) {
      return std::unexpected(failure("truncated stub declaration"));
    }
    StubDeclaration declaration;
    const auto kind = static_cast<std::uint8_t>(input[cursor++]);
    if (!valid_declaration_kind(kind)) {
      return std::unexpected(failure("unknown declaration kind"));
    }
    declaration.kind = static_cast<DeclarationKind>(kind);
    auto name = read_string(input, cursor);
    if (!name) {
      return std::unexpected(name.error());
    }
    declaration.name = std::move(*name);
    auto modifier_count = read_u32(input, cursor);
    if (!modifier_count) {
      return std::unexpected(modifier_count.error());
    }
    declaration.modifiers.reserve(std::min<std::size_t>(*modifier_count, input.size() - cursor));
    for (std::uint32_t modifier_index = 0; modifier_index < *modifier_count;
         ++modifier_index) {
      auto modifier = read_string(input, cursor);
      if (!modifier) {
        return std::unexpected(modifier.error());
      }
      declaration.modifiers.push_back(std::move(*modifier));
    }
    auto signature = read_string(input, cursor);
    if (!signature) {
      return std::unexpected(signature.error());
    }
    declaration.signature = std::move(*signature);
    auto documentation = read_string(input, cursor);
    if (!documentation) {
      return std::unexpected(documentation.error());
    }
    declaration.documentation = std::move(*documentation);
    auto start_byte = read_u32(input, cursor);
    auto end_byte = read_u32(input, cursor);
    auto start_line = read_u32(input, cursor);
    auto start_column = read_u32(input, cursor);
    auto end_line = read_u32(input, cursor);
    auto end_column = read_u32(input, cursor);
    if (!start_byte || !end_byte || !start_line || !start_column || !end_line ||
        !end_column) {
      return std::unexpected(failure("truncated stub source range"));
    }
    declaration.range = {*start_byte,   *end_byte, *start_line,
                         *start_column, *end_line, *end_column};
    declarations.push_back(std::move(declaration));
  }
  if (cursor != input.size()) {
    return std::unexpected(failure("trailing bytes in stub tree"));
  }
  return StubTree{language, blob, std::move(declarations)};
}

auto source_text(TSNode node, std::string_view source) -> std::string_view {
  const auto start = ts_node_start_byte(node);
  const auto end = ts_node_end_byte(node);
  if (start > end || end > source.size()) {
    return {};
  }
  return source.substr(start, end - start);
}

auto normalized(std::string_view value) -> std::string {
  auto output = std::string{};
  bool previous_space = false;
  for (const char character : value) {
    if (std::isspace(static_cast<unsigned char>(character))) {
      previous_space = !output.empty();
      continue;
    }
    if (previous_space) {
      output.push_back(' ');
    }
    output.push_back(character);
    previous_space = false;
  }
  return output;
}

auto contains_word(std::string_view text_value, std::string_view word) -> bool {
  auto position = text_value.find(word);
  while (position != std::string_view::npos) {
    const bool begins_word =
        position == 0 ||
        !std::isalnum(static_cast<unsigned char>(text_value[position - 1]));
    const auto end = position + word.size();
    const bool ends_word =
        end == text_value.size() ||
        !std::isalnum(static_cast<unsigned char>(text_value[end]));
    if (begins_word && ends_word) {
      return true;
    }
    position = text_value.find(word, position + 1);
  }
  return false;
}

auto modifiers(std::string_view signature) -> std::vector<std::string> {
  constexpr auto candidates =
      std::array{"static",  "extern",   "inline",    "const",   "volatile",
                 "virtual", "explicit", "constexpr", "noexcept"};
  auto output = std::vector<std::string>{};
  for (const auto candidate : candidates) {
    if (contains_word(signature, candidate)) {
      output.emplace_back(candidate);
    }
  }
  return output;
}

auto is_name_node(TSNode node) -> bool {
  const auto type = std::string_view(ts_node_type(node));
  return type == "identifier" || type == "field_identifier" ||
         type == "type_identifier" || type == "namespace_identifier";
}

auto declaration_name(TSNode node, std::string_view source) -> std::string {
  if (is_name_node(node)) {
    return std::string(source_text(node, source));
  }
  const auto count = ts_node_named_child_count(node);
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto name =
        declaration_name(ts_node_named_child(node, index), source);
    if (!name.empty()) {
      return name;
    }
  }
  return {};
}

auto range(TSNode node) -> SourceRange {
  const auto start = ts_node_start_point(node);
  const auto end = ts_node_end_point(node);
  return {ts_node_start_byte(node),
          ts_node_end_byte(node),
          start.row,
          start.column,
          end.row,
          end.column};
}

auto signature(TSNode node, std::string_view source, DeclarationKind kind)
    -> std::string {
  auto value = source_text(node, source);
  if (kind == DeclarationKind::function) {
    const auto body = value.find('{');
    if (body != std::string_view::npos) {
      value = value.substr(0, body);
    }
  }
  return normalized(value);
}

auto leading_documentation(std::string_view source, std::uint32_t start)
    -> std::string {
  auto end = static_cast<std::size_t>(start);
  while (end > 0 && std::isspace(static_cast<unsigned char>(source[end - 1]))) {
    --end;
  }
  if (end >= 2 && source.substr(end - 2, 2) == "*/") {
    const auto begin = source.rfind("/*", end - 2);
    if (begin != std::string_view::npos) {
      auto body = source.substr(begin + 2, end - begin - 4);
      if (body.starts_with('*')) {
        body.remove_prefix(1);
      }
      return normalized(body);
    }
  }

  auto lines = std::vector<std::string>{};
  while (end > 0) {
    const auto newline = source.rfind('\n', end - 1);
    const auto begin = newline == std::string_view::npos ? 0 : newline + 1;
    auto line = source.substr(begin, end - begin);
    while (!line.empty() &&
           std::isspace(static_cast<unsigned char>(line.front()))) {
      line.remove_prefix(1);
    }
    if (!line.starts_with("//")) {
      break;
    }
    line.remove_prefix(2);
    lines.insert(lines.begin(), normalized(line));
    if (begin == 0) {
      break;
    }
    end = begin - 1;
    while (end > 0 && source[end - 1] == '\r') {
      --end;
    }
  }
  auto output = std::string{};
  for (const auto &line : lines) {
    if (!output.empty()) {
      output.push_back('\n');
    }
    output += line;
  }
  return output;
}

auto add_declaration(std::vector<StubDeclaration> &output, DeclarationKind kind,
                     TSNode node, TSNode name_node, std::string_view source)
    -> void {
  const auto name = declaration_name(name_node, source);
  if (name.empty()) {
    return;
  }
  const auto declaration_signature = signature(node, source, kind);
  output.push_back(
      {kind, name, modifiers(declaration_signature), declaration_signature,
       leading_documentation(source, ts_node_start_byte(node)), range(node)});
}

auto has_node_type(TSNode node, std::string_view expected) -> bool {
  if (std::string_view(ts_node_type(node)) == expected) {
    return true;
  }
  const auto count = ts_node_named_child_count(node);
  for (std::uint32_t index = 0; index < count; ++index) {
    if (has_node_type(ts_node_named_child(node, index), expected)) {
      return true;
    }
  }
  return false;
}

auto collect_declarators(std::vector<StubDeclaration> &output,
                         DeclarationKind kind, TSNode node,
                         std::string_view source) -> void {
  bool found = false;
  const auto count = ts_node_named_child_count(node);
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto child = ts_node_named_child(node, index);
    const auto type = std::string_view(ts_node_type(child));
    if (type == "init_declarator" || type.ends_with("declarator")) {
      const auto actual_kind = has_node_type(child, "function_declarator")
                                   ? DeclarationKind::function
                                   : kind;
      add_declaration(output, actual_kind, node, child, source);
      found = true;
    }
  }
  if (found) {
    return;
  }
  for (std::uint32_t index = 0; index < count; ++index) {
    const auto child = ts_node_named_child(node, index);
    if (std::string_view(ts_node_type(child)) == "identifier") {
      add_declaration(output, kind, node, child, source);
    }
  }
}

auto collect(TSNode node, std::string_view source,
             std::vector<StubDeclaration> &output) -> void {
  const auto type = std::string_view(ts_node_type(node));
  if (type == "function_definition") {
    add_declaration(output, DeclarationKind::function, node,
                    ts_node_child_by_field_name(node, "declarator", 10),
                    source);
    return;
  }
  if (type == "declaration") {
    collect_declarators(output, DeclarationKind::variable, node, source);
    return;
  }
  if (type == "field_declaration") {
    collect_declarators(output, DeclarationKind::field, node, source);
    return;
  }
  if (type == "type_definition" || type == "alias_declaration" ||
      type == "class_specifier" || type == "struct_specifier" ||
      type == "enum_specifier") {
    add_declaration(output, DeclarationKind::type, node, node, source);
    return;
  }
  if (type == "namespace_definition") {
    add_declaration(output, DeclarationKind::namespace_, node, node, source);
  }
  const auto count = ts_node_named_child_count(node);
  for (std::uint32_t index = 0; index < count; ++index) {
    collect(ts_node_named_child(node, index), source, output);
  }
}

auto parse(StubLanguage language, std::string_view source)
    -> liquid::result<std::vector<StubDeclaration>> {
  TSParser *parser = ts_parser_new();
  if (parser == nullptr) {
    return std::unexpected(failure("could not create tree-sitter parser"));
  }
  const TSLanguage *grammar =
      language == StubLanguage::c ? tree_sitter_c() : tree_sitter_cpp();
  if (!ts_parser_set_language(parser, grammar)) {
    ts_parser_delete(parser);
    return std::unexpected(
        failure("tree-sitter grammar is incompatible with its runtime"));
  }
  TSTree *tree =
      ts_parser_parse_string(parser, nullptr, source.data(), source.size());
  if (tree == nullptr) {
    ts_parser_delete(parser);
    return std::unexpected(failure("could not parse source with tree-sitter"));
  }
  auto declarations = std::vector<StubDeclaration>{};
  collect(ts_tree_root_node(tree), source, declarations);
  ts_tree_delete(tree);
  ts_parser_delete(parser);
  return declarations;
}

auto language_prefix(StubLanguage language) -> std::string_view {
  return language == StubLanguage::c ? "c:" : "cpp:";
}

auto query_argument(StubLanguage language, const liquid::digest &blob)
    -> std::string {
  return std::string(language_prefix(language)) + liquid::to_string(blob);
}

auto language_from_argument(std::string_view argument)
    -> liquid::result<StubLanguage> {
  if (argument.starts_with("c:")) {
    return StubLanguage::c;
  }
  if (argument.starts_with("cpp:")) {
    return StubLanguage::cpp;
  }
  return std::unexpected(
      failure("stub query has an invalid language argument"));
}

auto compute_stub(liquid::query_context &context,
                  std::vector<std::byte> arguments)
    -> liquid::runtime::task<std::vector<std::byte>> {
  const auto argument = text(arguments);
  auto language = language_from_argument(argument);
  if (!language) {
    co_return std::unexpected(language.error());
  }
  auto input = co_await context.read(liquid::graph_input_key{
      liquid::graph_input_kind::working_tree_blob, argument});
  if (!input) {
    co_return std::unexpected(input.error());
  }
  auto declarations = parse(*language, text(input->bytes));
  if (!declarations) {
    co_return std::unexpected(declarations.error());
  }
  co_return encode(*language, *declarations);
}

auto stub_registry() -> liquid::result<liquid::query_registry> {
  auto registry = liquid::query_registry{};
  auto added = registry.add({.name = stub_query_kind, .compute = compute_stub});
  if (!added) {
    return std::unexpected(added.error());
  }
  return registry;
}

auto read_file(const std::filesystem::path &path)
    -> liquid::result<std::string> {
  auto input = std::ifstream(path, std::ios::binary);
  if (!input.is_open()) {
    return std::unexpected(liquid::error{
        liquid::errc::io, "could not open source file", path.string()});
  }
  return std::string{std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>()};
}

} // namespace

StubTrees::StubTrees(liquid::graph graph) : graph_(std::move(graph)) {}

StubTrees::StubTrees(StubTrees &&) noexcept = default;

auto StubTrees::operator=(StubTrees &&) noexcept -> StubTrees & = default;

StubTrees::~StubTrees() = default;

auto StubTrees::open(std::filesystem::path state_directory)
    -> liquid::result<StubTrees> {
  auto registry = stub_registry();
  if (!registry) {
    return std::unexpected(registry.error());
  }
  auto opened = liquid::runtime::block_on(
      liquid::graph::open(state_directory, std::move(*registry)));
  if (!opened) {
    return std::unexpected(opened.error());
  }
  return StubTrees(std::move(*opened));
}

auto StubTrees::for_file(const std::filesystem::path &path)
    -> liquid::result<StubTree> {
  auto language = stub_language_for_path(path);
  if (!language) {
    return std::unexpected(language.error());
  }
  auto source = read_file(path);
  if (!source) {
    return std::unexpected(source.error());
  }
  return for_source(*language, *source);
}

auto StubTrees::for_source(StubLanguage language, std::string_view source)
    -> liquid::result<StubTree> {
  const auto source_bytes = bytes(source);
  auto blob = liquid::hash(liquid::algorithm::blake3, source_bytes);
  if (!blob) {
    return std::unexpected(blob.error());
  }
  const auto argument = query_argument(language, *blob);
  const auto input_key = liquid::graph_input_key{
      liquid::graph_input_kind::working_tree_blob, argument};
  auto stored = liquid::runtime::block_on(
      graph_.set_input(input_key, source_bytes, liquid::durability::low));
  if (!stored) {
    return std::unexpected(stored.error());
  }
  auto demanded = liquid::runtime::block_on(
      graph_.demand({stub_query_kind, bytes(argument)}));
  if (!demanded) {
    return std::unexpected(demanded.error());
  }
  return decode(demanded->bytes, *blob);
}

auto StubTrees::flush() -> liquid::result<void> {
  auto flushed = liquid::runtime::block_on(graph_.flush());
  if (!flushed) {
    return std::unexpected(flushed.error());
  }
  return {};
}

auto StubTrees::statistics() const -> liquid::graph_statistics {
  return graph_.statistics();
}

void StubResolver::add(std::filesystem::path path, StubTree tree) {
  files_.push_back({std::move(path), std::move(tree)});
}

auto StubResolver::resolve(std::string_view name) const
    -> std::vector<SymbolLocation> {
  auto output = std::vector<SymbolLocation>{};
  for (const auto &file : files_) {
    for (const auto &declaration : file.tree.declarations) {
      if (declaration.name == name) {
        output.push_back({file.path, declaration});
      }
    }
  }
  return output;
}

auto stub_language_for_path(const std::filesystem::path &path)
    -> liquid::result<StubLanguage> {
  auto extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(),
                         [](unsigned char character) {
                           return static_cast<char>(std::tolower(character));
                         });
  if (extension == ".c" || extension == ".h") {
    return StubLanguage::c;
  }
  if (extension == ".cc" || extension == ".cp" || extension == ".cxx" ||
      extension == ".cpp" || extension == ".c++" || extension == ".hh" ||
      extension == ".hpp" || extension == ".hxx" || extension == ".h++") {
    return StubLanguage::cpp;
  }
  return std::unexpected(
      failure("unsupported stub-tree language", path.string()));
}

auto declaration_kind_name(DeclarationKind kind) -> std::string_view {
  switch (kind) {
  case DeclarationKind::function:
    return "function";
  case DeclarationKind::type:
    return "type";
  case DeclarationKind::variable:
    return "variable";
  case DeclarationKind::field:
    return "field";
  case DeclarationKind::namespace_:
    return "namespace";
  }
  return "unknown";
}

} // namespace liquify
