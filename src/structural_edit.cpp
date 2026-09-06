#include "liquify/structural_edit.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <span>
#include <string_view>
#include <utility>

#include "editor_buffer.hpp"
#include "encoding/cbor.hpp"
#include "liquid/edit.hpp"
#include "liquid/hash.hpp"
#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/stub_tree.hpp"

namespace liquify {
namespace detail {

class StructuralRepository {
public:
  explicit StructuralRepository(liquid::repository value)
      : value(std::move(value)) {}

  liquid::repository value;
};

} // namespace detail

namespace {

namespace cbor = liquid::encoding::cbor;

auto failure(std::string message, std::string context = "structural edit")
    -> liquid::error {
  return {liquid::errc::invalid_argument, std::move(message),
          std::move(context)};
}

auto bytes(std::string_view value) -> std::span<const std::byte> {
  return std::as_bytes(std::span(value.data(), value.size()));
}

auto map(std::initializer_list<std::pair<std::string, cbor::value>> fields)
    -> cbor::value {
  auto output = cbor::value::map{};
  output.reserve(fields.size());
  for (const auto &[key, value] : fields) {
    output.emplace_back(cbor::value{key}, value);
  }
  return cbor::value{std::move(output)};
}

auto field(const cbor::value &source, std::string_view name)
    -> const cbor::value * {
  const auto *fields = std::get_if<cbor::value::map>(&source.data);
  if (fields == nullptr) {
    return nullptr;
  }
  for (const auto &[key, value] : *fields) {
    const auto *text = std::get_if<std::string>(&key.data);
    if (text != nullptr && *text == name) {
      return &value;
    }
  }
  return nullptr;
}

auto text_field(const cbor::value &source, std::string_view name)
    -> const std::string * {
  const auto *value = field(source, name);
  return value == nullptr ? nullptr : std::get_if<std::string>(&value->data);
}

auto unsigned_field(const cbor::value &source, std::string_view name)
    -> const std::uint64_t * {
  const auto *value = field(source, name);
  return value == nullptr ? nullptr : std::get_if<std::uint64_t>(&value->data);
}

auto range_value(const SourceRange &range) -> cbor::value {
  return map(
      {{"start_byte",
        cbor::value{static_cast<std::uint64_t>(range.start_byte)}},
       {"end_byte", cbor::value{static_cast<std::uint64_t>(range.end_byte)}},
       {"start_line",
        cbor::value{static_cast<std::uint64_t>(range.start_line)}},
       {"start_column",
        cbor::value{static_cast<std::uint64_t>(range.start_column)}},
       {"end_line", cbor::value{static_cast<std::uint64_t>(range.end_line)}},
       {"end_column",
        cbor::value{static_cast<std::uint64_t>(range.end_column)}}});
}

auto decode_range(const cbor::value &value) -> liquid::result<SourceRange> {
  const auto *start_byte = unsigned_field(value, "start_byte");
  const auto *end_byte = unsigned_field(value, "end_byte");
  const auto *start_line = unsigned_field(value, "start_line");
  const auto *start_column = unsigned_field(value, "start_column");
  const auto *end_line = unsigned_field(value, "end_line");
  const auto *end_column = unsigned_field(value, "end_column");
  constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
  if (start_byte == nullptr || end_byte == nullptr || start_line == nullptr ||
      start_column == nullptr || end_line == nullptr || end_column == nullptr ||
      *start_byte > limit || *end_byte > limit || *start_line > limit ||
      *start_column > limit || *end_line > limit || *end_column > limit ||
      *start_byte > *end_byte) {
    return std::unexpected(
        failure("invalid source range", "structural edit CBOR"));
  }
  return SourceRange{static_cast<std::uint32_t>(*start_byte),
                     static_cast<std::uint32_t>(*end_byte),
                     static_cast<std::uint32_t>(*start_line),
                     static_cast<std::uint32_t>(*start_column),
                     static_cast<std::uint32_t>(*end_line),
                     static_cast<std::uint32_t>(*end_column)};
}

auto target_value(const StructuralEditTarget &target) -> cbor::value {
  return map(
      {{"path", cbor::value{target.path.generic_string()}},
       {"node", map({{"kind", cbor::value{target.node.kind}},
                     {"range", range_value(target.node.range)},
                     {"fingerprint", cbor::value{target.node.fingerprint}}})}});
}

auto normalised_path(std::string_view text)
    -> liquid::result<std::filesystem::path> {
  auto path = std::filesystem::path(text).lexically_normal();
  if (path.empty() || path.is_absolute()) {
    return std::unexpected(failure("structural edit path must be relative",
                                   "structural edit CBOR"));
  }
  for (const auto &component : path) {
    if (component == "..") {
      return std::unexpected(
          failure("structural edit path escapes the repository",
                  "structural edit CBOR"));
    }
  }
  return path;
}

auto decode_target(const cbor::value &value)
    -> liquid::result<StructuralEditTarget> {
  const auto *path_text = text_field(value, "path");
  const auto *node = field(value, "node");
  if (path_text == nullptr || node == nullptr) {
    return std::unexpected(
        failure("invalid structural target", "structural edit CBOR"));
  }
  auto path = normalised_path(*path_text);
  const auto *kind = text_field(*node, "kind");
  const auto *range = field(*node, "range");
  const auto *fingerprint = text_field(*node, "fingerprint");
  if (!path || kind == nullptr || kind->empty() || range == nullptr ||
      fingerprint == nullptr || fingerprint->empty()) {
    return std::unexpected(
        failure("invalid structural node target", "structural edit CBOR"));
  }
  auto decoded_range = decode_range(*range);
  if (!decoded_range) {
    return std::unexpected(decoded_range.error());
  }
  return StructuralEditTarget{std::move(*path),
                              {*kind, *decoded_range, *fingerprint}};
}

auto operation_from_name(std::string_view name)
    -> std::optional<StructuralEditOperation> {
  if (name == "replace_node") {
    return StructuralEditOperation::replace_node;
  }
  if (name == "insert_before") {
    return StructuralEditOperation::insert_before;
  }
  if (name == "insert_after") {
    return StructuralEditOperation::insert_after;
  }
  if (name == "delete_node") {
    return StructuralEditOperation::delete_node;
  }
  return std::nullopt;
}

auto edit_value(const StructuralEdit &edit) -> cbor::value {
  return map(
      {{"target", target_value(edit.target)},
       {"operation", cbor::value{std::string(
                         structural_edit_operation_name(edit.operation))}},
       {"text", cbor::value{edit.text}}});
}

auto decode_edit(const cbor::value &value) -> liquid::result<StructuralEdit> {
  const auto *target = field(value, "target");
  const auto *operation = text_field(value, "operation");
  const auto *text = text_field(value, "text");
  if (target == nullptr || operation == nullptr || text == nullptr) {
    return std::unexpected(
        failure("invalid structural edit", "structural edit CBOR"));
  }
  auto decoded_target = decode_target(*target);
  const auto decoded_operation = operation_from_name(*operation);
  if (!decoded_target || !decoded_operation) {
    return std::unexpected(decoded_target
                               ? failure("unknown structural edit operation",
                                         "structural edit CBOR")
                               : decoded_target.error());
  }
  return StructuralEdit{std::move(*decoded_target), *decoded_operation, *text};
}

auto fingerprint(std::string_view source) -> liquid::result<std::string> {
  auto hashed = liquid::hash(liquid::algorithm::blake3, bytes(source));
  if (!hashed) {
    return std::unexpected(hashed.error());
  }
  return liquid::to_string(*hashed);
}

auto source_text(std::span<const std::byte> input) -> std::string {
  const auto *characters = reinterpret_cast<const char *>(input.data());
  return std::string(characters, input.size());
}

auto range_contains(const SourceRange &outer, const SourceRange &inner)
    -> bool {
  return outer.start_byte <= inner.start_byte &&
         inner.end_byte <= outer.end_byte;
}

auto ranges_overlap(const SourceRange &left, const SourceRange &right) -> bool {
  return left.start_byte < right.end_byte && right.start_byte < left.end_byte;
}

auto is_identifier_kind(std::string_view kind) -> bool {
  return kind == "identifier" || kind == "field_identifier" ||
         kind == "type_identifier" || kind == "namespace_identifier";
}

auto is_non_code_kind(std::string_view kind) -> bool {
  return kind == "comment" || kind.find("string") != std::string_view::npos ||
         kind.find("char_literal") != std::string_view::npos;
}

auto is_identifier_character(char character) -> bool {
  return std::isalnum(static_cast<unsigned char>(character)) ||
         character == '_';
}

auto word_occurrences(std::string_view text, std::string_view word,
                      std::uint32_t offset) -> std::vector<SourceRange> {
  auto output = std::vector<SourceRange>{};
  auto position = text.find(word);
  while (position != std::string_view::npos) {
    const auto end = position + word.size();
    const auto begins_word =
        position == 0 || !is_identifier_character(text[position - 1]);
    const auto ends_word =
        end == text.size() || !is_identifier_character(text[end]);
    if (begins_word && ends_word) {
      output.push_back({offset + static_cast<std::uint32_t>(position),
                        offset + static_cast<std::uint32_t>(end), 0, 0, 0, 0});
    }
    position = text.find(word, position + 1);
  }
  return output;
}

auto valid_identifier(std::string_view name) -> bool {
  if (name.empty() ||
      !(std::isalpha(static_cast<unsigned char>(name.front())) ||
        name.front() == '_')) {
    return false;
  }
  return std::ranges::all_of(name.substr(1), is_identifier_character);
}

auto trim(std::string_view value) -> std::string {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.front()))) {
    value.remove_prefix(1);
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.remove_suffix(1);
  }
  return std::string(value);
}

auto declaration_type(const StubDeclaration &declaration)
    -> std::optional<std::string> {
  const auto position = declaration.signature.find(declaration.name);
  if (position == std::string::npos) {
    return std::nullopt;
  }
  auto type = trim(std::string_view(declaration.signature).substr(0, position));
  if (type.empty() || type.contains('(') || type.contains('=') ||
      type.contains(',')) {
    return std::nullopt;
  }
  constexpr auto storage = std::array<std::string_view, 4>{
      "static ", "extern ", "register ", "mutable "};
  for (const auto prefix : storage) {
    if (type.starts_with(prefix)) {
      type.erase(0, prefix.size());
    }
  }
  return trim(type);
}

auto is_write_use(std::string_view source, const SourceRange &range) -> bool {
  auto before = static_cast<std::size_t>(range.start_byte);
  while (before > 0 &&
         std::isspace(static_cast<unsigned char>(source[before - 1]))) {
    --before;
  }
  if (before >= 2 && (source.substr(before - 2, 2) == "++" ||
                      source.substr(before - 2, 2) == "--")) {
    return true;
  }
  auto after = static_cast<std::size_t>(range.end_byte);
  while (after < source.size() &&
         std::isspace(static_cast<unsigned char>(source[after]))) {
    ++after;
  }
  if (after + 1 < source.size() &&
      (source.substr(after, 2) == "++" || source.substr(after, 2) == "--" ||
       source.substr(after, 2) == "+=" || source.substr(after, 2) == "-=" ||
       source.substr(after, 2) == "*=" || source.substr(after, 2) == "/=")) {
    return true;
  }
  return after < source.size() && source[after] == '=' &&
         (after + 1 == source.size() || source[after + 1] != '=');
}

auto is_statement_kind(std::string_view kind) -> bool {
  return kind == "declaration" || kind.ends_with("_statement");
}

} // namespace

auto encode_structural_edit_request(std::span<const StructuralEdit> edits)
    -> liquid::result<std::vector<std::byte>> {
  auto values = cbor::value::array{};
  values.reserve(edits.size());
  for (const auto &edit : edits) {
    values.push_back(edit_value(edit));
  }
  return cbor::encode(map({{"v", cbor::value{std::uint64_t{1}}},
                           {"edits", cbor::value{std::move(values)}}}));
}

auto decode_structural_edit_request(std::span<const std::byte> input)
    -> liquid::result<std::vector<StructuralEdit>> {
  auto decoded = cbor::decode(input);
  if (!decoded) {
    return std::unexpected(decoded.error());
  }
  const auto *version = unsigned_field(*decoded, "v");
  const auto *edits_value = field(*decoded, "edits");
  const auto *edits = edits_value == nullptr
                          ? nullptr
                          : std::get_if<cbor::value::array>(&edits_value->data);
  if (version == nullptr || *version != 1 || edits == nullptr) {
    return std::unexpected(
        failure("unsupported structural edit request", "structural edit CBOR"));
  }
  auto output = std::vector<StructuralEdit>{};
  output.reserve(edits->size());
  for (const auto &value : *edits) {
    auto edit = decode_edit(value);
    if (!edit) {
      return std::unexpected(edit.error());
    }
    output.push_back(std::move(*edit));
  }
  return output;
}

auto encode_structural_edit_result(const StructuralEditResult &result)
    -> liquid::result<std::vector<std::byte>> {
  auto applied = cbor::value::array{};
  for (const auto &item : result.applied) {
    applied.push_back(map({{"path", cbor::value{item.path.generic_string()}},
                           {"range", range_value(item.range)}}));
  }
  auto refused = cbor::value::array{};
  for (const auto &item : result.refused) {
    refused.push_back(map({{"target", target_value(item.target)},
                           {"reason", cbor::value{item.reason}}}));
  }
  return cbor::encode(map({{"v", cbor::value{std::uint64_t{1}}},
                           {"applied", cbor::value{std::move(applied)}},
                           {"refused", cbor::value{std::move(refused)}}}));
}

auto decode_structural_edit_result(std::span<const std::byte> input)
    -> liquid::result<StructuralEditResult> {
  auto decoded = cbor::decode(input);
  if (!decoded) {
    return std::unexpected(decoded.error());
  }
  const auto *version = unsigned_field(*decoded, "v");
  const auto *applied_value = field(*decoded, "applied");
  const auto *refused_value = field(*decoded, "refused");
  const auto *applied =
      applied_value == nullptr
          ? nullptr
          : std::get_if<cbor::value::array>(&applied_value->data);
  const auto *refused =
      refused_value == nullptr
          ? nullptr
          : std::get_if<cbor::value::array>(&refused_value->data);
  if (version == nullptr || *version != 1 || applied == nullptr ||
      refused == nullptr) {
    return std::unexpected(
        failure("unsupported structural edit result", "structural edit CBOR"));
  }
  auto output = StructuralEditResult{};
  for (const auto &value : *applied) {
    const auto *path_text = text_field(value, "path");
    const auto *range_value_field = field(value, "range");
    if (path_text == nullptr || range_value_field == nullptr) {
      return std::unexpected(
          failure("invalid applied structural edit", "structural edit CBOR"));
    }
    auto path = normalised_path(*path_text);
    auto range = decode_range(*range_value_field);
    if (!path || !range) {
      return std::unexpected(path ? range.error() : path.error());
    }
    output.applied.push_back({std::move(*path), *range});
  }
  for (const auto &value : *refused) {
    const auto *target_value_field = field(value, "target");
    const auto *reason = text_field(value, "reason");
    if (target_value_field == nullptr || reason == nullptr) {
      return std::unexpected(
          failure("invalid refused structural edit", "structural edit CBOR"));
    }
    auto target = decode_target(*target_value_field);
    if (!target) {
      return std::unexpected(target.error());
    }
    output.refused.push_back({std::move(*target), *reason});
  }
  return output;
}

StructuralEditor::StructuralEditor(std::filesystem::path repository_path,
                                   InteractiveAnalysis &analysis)
    : repository_path_(std::move(repository_path)), analysis_(&analysis) {}

auto StructuralEditor::open_buffer(std::filesystem::path path,
                                   StubLanguage language,
                                   std::string source_value)
    -> liquid::result<void> {
  path = path.lexically_normal();
  const auto found = std::ranges::find(buffers_, path, &Buffer::path);
  if (found != buffers_.end()) {
    if (found->language == language && found->source == source_value) {
      return {};
    }
    auto closed = analysis_->close_buffer(path);
    if (!closed) {
      return std::unexpected(closed.error());
    }
    buffers_.erase(found);
  }
  auto opened = analysis_->open_buffer(path, language, source_value);
  if (!opened) {
    return std::unexpected(opened.error());
  }
  buffers_.push_back({std::move(path), language, std::move(source_value)});
  return {};
}

auto StructuralEditor::ensure_buffer(const std::filesystem::path &path)
    -> liquid::result<void> {
  const auto normalized = path.lexically_normal();
  if (std::ranges::find(buffers_, normalized, &Buffer::path) !=
      buffers_.end()) {
    return {};
  }
  auto language = stub_language_for_path(normalized);
  if (!language) {
    return std::unexpected(language.error());
  }
  if (!repository_) {
    auto opened =
        liquid::runtime::block_on(liquid::repository::open(repository_path_));
    if (!opened) {
      return std::unexpected(opened.error());
    }
    repository_ =
        std::make_shared<detail::StructuralRepository>(std::move(*opened));
  }
  auto contents = liquid::runtime::block_on(
      repository_->value.read(normalized.generic_string()));
  if (!contents) {
    return std::unexpected(contents.error());
  }
  return open_buffer(normalized, *language, source_text(*contents));
}

auto StructuralEditor::apply_source_edit(const std::filesystem::path &path,
                                         const SourceEdit &edit)
    -> liquid::result<IncrementalParseReport> {
  const auto normalized = path.lexically_normal();
  const auto found = std::ranges::find(buffers_, normalized, &Buffer::path);
  if (found == buffers_.end()) {
    return std::unexpected(
        failure("structural buffer is not open", normalized.generic_string()));
  }
  if (edit.start_byte > edit.old_end_byte ||
      edit.old_end_byte > found->source.size()) {
    return std::unexpected(
        failure("source edit is outside the structural buffer",
                normalized.generic_string()));
  }
  auto applied = analysis_->apply_edit(normalized, edit);
  if (!applied) {
    return std::unexpected(applied.error());
  }
  found->source.replace(edit.start_byte, edit.old_end_byte - edit.start_byte,
                        edit.text);
  return applied;
}

auto StructuralEditor::close_buffer(const std::filesystem::path &path)
    -> liquid::result<void> {
  const auto normalized = path.lexically_normal();
  const auto found = std::ranges::find(buffers_, normalized, &Buffer::path);
  if (found == buffers_.end()) {
    return std::unexpected(
        failure("structural buffer is not open", normalized.generic_string()));
  }
  auto closed = analysis_->close_buffer(normalized);
  if (!closed) {
    return std::unexpected(closed.error());
  }
  buffers_.erase(found);
  return {};
}

auto StructuralEditor::source(const std::filesystem::path &path) const
    -> liquid::result<std::string_view> {
  const auto normalized = path.lexically_normal();
  const auto found = std::ranges::find(buffers_, normalized, &Buffer::path);
  if (found == buffers_.end()) {
    return std::unexpected(
        failure("structural buffer is not open", normalized.generic_string()));
  }
  return found->source;
}

auto StructuralEditor::selection_chain(const std::filesystem::path &path,
                                       std::uint32_t byte)
    -> liquid::result<std::vector<SyntaxNodeInfo>> {
  auto ensured = ensure_buffer(path);
  if (!ensured) {
    return std::unexpected(ensured.error());
  }
  return analysis_->syntax_ancestors(path, byte);
}

auto StructuralEditor::select_references(const std::filesystem::path &path,
                                         std::uint32_t byte)
    -> liquid::result<std::vector<SourceLocation>> {
  auto ensured = ensure_buffer(path);
  if (!ensured) {
    return std::unexpected(ensured.error());
  }
  auto chain = analysis_->syntax_ancestors(path, byte);
  if (!chain) {
    return std::unexpected(chain.error());
  }
  const auto symbol = std::ranges::find_if(
      *chain, [](const auto &node) { return is_identifier_kind(node.kind); });
  if (symbol == chain->end()) {
    return std::unexpected(
        failure("reference selection cursor is not on an identifier"));
  }
  auto references = analysis_->find_references(path, byte);
  if (!references) {
    return std::unexpected(references.error());
  }
  for (const auto &reference : *references) {
    auto opened = ensure_buffer(reference.location.path);
    if (!opened) {
      return std::unexpected(opened.error());
    }
  }
  references = analysis_->find_references(path, byte);
  if (!references) {
    return std::unexpected(references.error());
  }
  auto output = std::vector<SourceLocation>{};
  auto seen = std::set<std::string>{};
  output.reserve(references->size());
  for (const auto &reference : *references) {
    auto nodes = analysis_->syntax_nodes(reference.location.path);
    if (!nodes) {
      return std::unexpected(nodes.error());
    }
    const auto identifier = std::ranges::find_if(*nodes, [&](const auto &node) {
      return is_identifier_kind(node.kind) &&
             node.spelling == symbol->spelling &&
             range_contains(reference.location.range, node.range);
    });
    if (identifier == nodes->end()) {
      continue;
    }
    const auto token = reference.location.path.generic_string() + ":" +
                       std::to_string(identifier->range.start_byte);
    if (seen.insert(token).second) {
      output.push_back({reference.location.path, identifier->range});
    }
  }
  return output;
}

auto StructuralEditor::make_edit(const std::filesystem::path &path,
                                 const SyntaxNodeInfo &node,
                                 StructuralEditOperation operation,
                                 std::string text)
    -> liquid::result<StructuralEdit> {
  auto ensured = ensure_buffer(path);
  if (!ensured) {
    return std::unexpected(ensured.error());
  }
  auto contents = source(path);
  if (!contents) {
    return std::unexpected(contents.error());
  }
  if (node.range.start_byte > node.range.end_byte ||
      node.range.end_byte > contents->size()) {
    return std::unexpected(failure(
        "syntax node is outside the structural buffer", path.generic_string()));
  }
  const auto node_source = contents->substr(
      node.range.start_byte, node.range.end_byte - node.range.start_byte);
  auto node_fingerprint = fingerprint(node_source);
  if (!node_fingerprint) {
    return std::unexpected(node_fingerprint.error());
  }
  return StructuralEdit{{path.lexically_normal(),
                         {node.kind, node.range, std::move(*node_fingerprint)}},
                        operation,
                        std::move(text)};
}

auto StructuralEditor::exact_node(const StructuralEditTarget &target)
    -> liquid::result<SyntaxNodeInfo> {
  auto ensured = ensure_buffer(target.path);
  if (!ensured) {
    return std::unexpected(ensured.error());
  }
  auto nodes = analysis_->syntax_nodes(target.path);
  if (!nodes) {
    return std::unexpected(nodes.error());
  }
  const auto found = std::ranges::find_if(*nodes, [&target](const auto &node) {
    return node.kind == target.node.kind && node.range == target.node.range;
  });
  if (found == nodes->end()) {
    return std::unexpected(failure("target syntax node no longer exists",
                                   target.path.generic_string()));
  }
  auto contents = source(target.path);
  if (!contents) {
    return std::unexpected(contents.error());
  }
  const auto spelling = contents->substr(
      found->range.start_byte, found->range.end_byte - found->range.start_byte);
  auto actual = fingerprint(spelling);
  if (!actual) {
    return std::unexpected(actual.error());
  }
  if (*actual != target.node.fingerprint) {
    return std::unexpected(failure("target syntax node fingerprint is stale",
                                   target.path.generic_string()));
  }
  return *found;
}

auto StructuralEditor::rename(const std::filesystem::path &path,
                              std::uint32_t byte, std::string new_name)
    -> liquid::result<RenamePreview> {
  if (!valid_identifier(new_name)) {
    return std::unexpected(failure("rename requires a valid identifier"));
  }
  auto chain = selection_chain(path, byte);
  if (!chain) {
    return std::unexpected(chain.error());
  }
  const auto symbol_node = std::ranges::find_if(
      *chain, [](const auto &node) { return is_identifier_kind(node.kind); });
  if (symbol_node == chain->end()) {
    return std::unexpected(failure("rename cursor is not on an identifier"));
  }
  auto references = analysis_->find_references(path, byte);
  if (!references) {
    return std::unexpected(references.error());
  }
  for (const auto &reference : *references) {
    auto opened = ensure_buffer(reference.location.path);
    if (!opened) {
      return std::unexpected(opened.error());
    }
  }
  references = analysis_->find_references(path, byte);
  if (!references) {
    return std::unexpected(references.error());
  }

  auto output = RenamePreview{symbol_node->spelling, {}, {}};
  auto seen = std::set<std::string>{};
  for (const auto &reference : *references) {
    auto nodes = analysis_->syntax_nodes(reference.location.path);
    if (!nodes) {
      return std::unexpected(nodes.error());
    }
    const auto identifier = std::ranges::find_if(*nodes, [&](const auto &node) {
      return is_identifier_kind(node.kind) && node.spelling == output.symbol &&
             range_contains(reference.location.range, node.range);
    });
    if (identifier == nodes->end()) {
      continue;
    }
    const auto token = reference.location.path.generic_string() + ":" +
                       std::to_string(identifier->range.start_byte);
    if (!seen.insert(token).second) {
      continue;
    }
    auto edit = make_edit(reference.location.path, *identifier,
                          StructuralEditOperation::replace_node, new_name);
    if (!edit) {
      return std::unexpected(edit.error());
    }
    output.edits.push_back(std::move(*edit));
  }

  for (const auto &buffer : buffers_) {
    auto nodes = analysis_->syntax_nodes(buffer.path);
    if (!nodes) {
      return std::unexpected(nodes.error());
    }
    for (const auto &node : *nodes) {
      if (!is_non_code_kind(node.kind)) {
        continue;
      }
      for (const auto &range : word_occurrences(node.spelling, output.symbol,
                                                node.range.start_byte)) {
        const auto token = buffer.path.generic_string() + ":" +
                           std::to_string(range.start_byte) + ":" +
                           std::to_string(range.end_byte);
        if (seen.insert(token).second) {
          output.unresolved.push_back({buffer.path, range});
        }
      }
    }
  }
  std::ranges::sort(output.edits, [](const auto &left, const auto &right) {
    if (left.target.path != right.target.path) {
      return left.target.path.generic_string() <
             right.target.path.generic_string();
    }
    return left.target.node.range.start_byte <
           right.target.node.range.start_byte;
  });
  return output;
}

auto StructuralEditor::extract_function(const std::filesystem::path &path,
                                        SourceRange selection, std::string name)
    -> liquid::result<ExtractFunctionPreview> {
  if (!valid_identifier(name)) {
    return std::unexpected(
        failure("extract-function requires a valid identifier"));
  }
  auto ensured = ensure_buffer(path);
  if (!ensured) {
    return std::unexpected(ensured.error());
  }
  auto contents = source(path);
  if (!contents) {
    return std::unexpected(contents.error());
  }
  if (selection.start_byte >= selection.end_byte ||
      selection.end_byte > contents->size()) {
    return std::unexpected(
        failure("extract-function selection is outside the buffer"));
  }
  auto ancestors = analysis_->syntax_ancestors(path, selection.start_byte);
  if (!ancestors) {
    return std::unexpected(ancestors.error());
  }
  const auto definition =
      std::ranges::find_if(*ancestors, [&](const auto &node) {
        return node.kind == "function_definition" &&
               node.range.end_byte >= selection.end_byte;
      });
  if (definition == ancestors->end()) {
    return std::unexpected(failure(
        "extract-function selection must be inside one function definition"));
  }
  auto nodes = analysis_->syntax_nodes(path);
  if (!nodes) {
    return std::unexpected(nodes.error());
  }
  auto statements = std::vector<SyntaxNodeInfo>{};
  for (const auto &node : *nodes) {
    if (!is_statement_kind(node.kind) ||
        !range_contains(selection, node.range)) {
      continue;
    }
    const auto nested = std::ranges::any_of(*nodes, [&](const auto &parent) {
      return is_statement_kind(parent.kind) && parent.range != node.range &&
             range_contains(selection, parent.range) &&
             range_contains(parent.range, node.range);
    });
    if (!nested) {
      statements.push_back(node);
    }
  }
  std::ranges::sort(statements, {},
                    [](const auto &node) { return node.range.start_byte; });
  if (statements.empty()) {
    return std::unexpected(
        failure("extract-function selection contains no complete statements"));
  }
  const auto selected_range = SourceRange{statements.front().range.start_byte,
                                          statements.back().range.end_byte,
                                          statements.front().range.start_line,
                                          statements.front().range.start_column,
                                          statements.back().range.end_line,
                                          statements.back().range.end_column};
  auto output = ExtractFunctionPreview{};
  for (const auto &node : *nodes) {
    if (range_contains(selected_range, node.range) &&
        (node.kind == "return_statement" || node.kind == "break_statement" ||
         node.kind == "continue_statement" || node.kind == "goto_statement")) {
      output.diagnostics.push_back("control flow leaves the selected range");
      break;
    }
  }
  if (!output.diagnostics.empty()) {
    return output;
  }
  auto declarations = analysis_->syntax_declarations(path);
  if (!declarations) {
    return std::unexpected(declarations.error());
  }
  const auto buffer =
      std::ranges::find(buffers_, path.lexically_normal(), &Buffer::path);
  const auto language = buffer->language;
  struct Parameter {
    std::string name;
    std::string type;
    bool written{};
    std::vector<SourceRange> uses;
  };
  auto parameters = std::vector<Parameter>{};
  for (const auto &node : *nodes) {
    if (!is_identifier_kind(node.kind) ||
        !range_contains(selected_range, node.range)) {
      continue;
    }
    const auto declared_inside =
        std::ranges::any_of(*declarations, [&](const auto &candidate) {
          return candidate.declaration.name == node.spelling &&
                 range_contains(selected_range, candidate.declaration.range);
        });
    if (declared_inside) {
      continue;
    }
    const SyntaxDeclarationInfo *declaration = nullptr;
    for (const auto &candidate : *declarations) {
      if (candidate.declaration.kind != DeclarationKind::variable ||
          candidate.declaration.name != node.spelling ||
          candidate.declaration.range.start_byte >= selected_range.start_byte ||
          !range_contains(candidate.scope, selected_range)) {
        continue;
      }
      if (declaration == nullptr ||
          candidate.declaration.range.start_byte >
              declaration->declaration.range.start_byte) {
        declaration = &candidate;
      }
    }
    if (declaration == nullptr) {
      continue;
    }
    auto parameter =
        std::ranges::find(parameters, node.spelling, &Parameter::name);
    if (parameter == parameters.end()) {
      auto type = declaration_type(declaration->declaration);
      if (!type) {
        output.diagnostics.push_back("unresolved type for " + node.spelling);
        continue;
      }
      parameters.push_back({node.spelling, std::move(*type), false, {}});
      parameter = std::prev(parameters.end());
    }
    parameter->written =
        parameter->written || is_write_use(*contents, node.range);
    parameter->uses.push_back(node.range);
  }
  if (!output.diagnostics.empty()) {
    return output;
  }

  auto parameter_text = std::string{};
  auto argument_text = std::string{};
  for (std::size_t index = 0; index < parameters.size(); ++index) {
    const auto &parameter = parameters[index];
    if (index != 0) {
      parameter_text += ", ";
      argument_text += ", ";
    }
    if (language == StubLanguage::cpp && parameter.written) {
      parameter_text += parameter.type + "& " + parameter.name;
      argument_text += parameter.name;
    } else if (language == StubLanguage::c && parameter.written) {
      parameter_text += parameter.type + "* " + parameter.name;
      argument_text += "&" + parameter.name;
    } else {
      parameter_text += parameter.type + " " + parameter.name;
      argument_text += parameter.name;
    }
  }

  auto body = std::string(
      contents->substr(selected_range.start_byte,
                       selected_range.end_byte - selected_range.start_byte));
  if (language == StubLanguage::c) {
    struct Rewrite {
      std::uint32_t offset{};
      std::size_t length{};
      std::string replacement;
    };
    auto rewrites = std::vector<Rewrite>{};
    for (const auto &parameter : parameters) {
      if (!parameter.written) {
        continue;
      }
      for (const auto &use : parameter.uses) {
        rewrites.push_back({use.start_byte - selected_range.start_byte,
                            parameter.name.size(),
                            "(*" + parameter.name + ")"});
      }
    }
    std::ranges::sort(rewrites, std::greater<>{}, &Rewrite::offset);
    for (const auto &rewrite : rewrites) {
      body.replace(rewrite.offset, rewrite.length, rewrite.replacement);
    }
  }
  if (!body.ends_with('\n')) {
    body.push_back('\n');
  }
  auto definition_text =
      "static void " + name + "(" + parameter_text + ") {\n" + body + "}\n\n";
  auto insertion =
      make_edit(path, *definition, StructuralEditOperation::insert_before,
                std::move(definition_text));
  if (!insertion) {
    return std::unexpected(insertion.error());
  }
  output.edits.push_back(std::move(*insertion));

  auto replacement =
      make_edit(path, statements.front(), StructuralEditOperation::replace_node,
                name + "(" + argument_text + ");");
  if (!replacement) {
    return std::unexpected(replacement.error());
  }
  output.edits.push_back(std::move(*replacement));
  for (auto statement = std::next(statements.begin());
       statement != statements.end(); ++statement) {
    auto deletion = make_edit(
        path, *statement, StructuralEditOperation::delete_node, std::string{});
    if (!deletion) {
      return std::unexpected(deletion.error());
    }
    output.edits.push_back(std::move(*deletion));
  }
  return output;
}

auto StructuralEditor::apply(std::span<const StructuralEdit> edits)
    -> liquid::result<StructuralEditResult> {
  struct Pending {
    StructuralEdit edit;
    SourceEdit source_edit;
  };
  auto result = StructuralEditResult{};
  if (!repository_) {
    auto opened =
        liquid::runtime::block_on(liquid::repository::open(repository_path_));
    if (!opened) {
      return std::unexpected(opened.error());
    }
    repository_ =
        std::make_shared<detail::StructuralRepository>(std::move(*opened));
  }
  auto by_path = std::map<std::filesystem::path, std::vector<Pending>>{};
  for (const auto &edit : edits) {
    auto node = exact_node(edit.target);
    if (!node) {
      result.refused.push_back({edit.target, node.error().message});
      continue;
    }
    auto source_edit = SourceEdit{};
    switch (edit.operation) {
    case StructuralEditOperation::replace_node:
      source_edit = {node->range.start_byte, node->range.end_byte, edit.text};
      break;
    case StructuralEditOperation::insert_before:
      source_edit = {node->range.start_byte, node->range.start_byte, edit.text};
      break;
    case StructuralEditOperation::insert_after:
      source_edit = {node->range.end_byte, node->range.end_byte, edit.text};
      break;
    case StructuralEditOperation::delete_node:
      source_edit = {node->range.start_byte, node->range.end_byte, {}};
      break;
    }
    by_path[edit.target.path.lexically_normal()].push_back(
        {edit, std::move(source_edit)});
  }

  auto accepted_by_path = std::map<std::filesystem::path, std::vector<Pending>>{};
  auto liquid_edits = std::vector<liquid::tree_edit>{};
  for (auto &[path, pending] : by_path) {
    std::ranges::sort(pending, [](const auto &left, const auto &right) {
      if (left.source_edit.start_byte != right.source_edit.start_byte) {
        return left.source_edit.start_byte > right.source_edit.start_byte;
      }
      return left.source_edit.old_end_byte > right.source_edit.old_end_byte;
    });
    auto accepted = std::vector<Pending>{};
    for (auto &candidate : pending) {
      const auto overlaps =
          std::ranges::any_of(accepted, [&](const auto &previous) {
            const auto left = SourceRange{candidate.source_edit.start_byte,
                                          candidate.source_edit.old_end_byte};
            const auto right = SourceRange{previous.source_edit.start_byte,
                                           previous.source_edit.old_end_byte};
            return ranges_overlap(left, right);
          });
      if (overlaps) {
        result.refused.push_back(
            {candidate.edit.target, "structural edits overlap"});
        continue;
      }
      accepted.push_back(std::move(candidate));
    }
    for (const auto &item : accepted) {
        auto digest = liquid::parse_digest(item.edit.target.node.fingerprint);
        if (!digest) {
            return std::unexpected(digest.error());
        }
        const auto& range = item.edit.target.node.range;
        auto operation = liquid::tree_edit_operation::replace_node;
        if (item.edit.operation == StructuralEditOperation::insert_before) {
            operation = liquid::tree_edit_operation::insert_before;
        } else if (item.edit.operation == StructuralEditOperation::insert_after) {
            operation = liquid::tree_edit_operation::insert_after;
        } else if (item.edit.operation == StructuralEditOperation::delete_node) {
            operation = liquid::tree_edit_operation::delete_node;
        }
        liquid_edits.push_back({{path.generic_string(),
                                 {item.edit.target.node.kind,
                                  {{range.start_byte, range.start_line + 1, range.start_column + 1},
                                   {range.end_byte, range.end_line + 1, range.end_column + 1}},
                                  *digest}},
                                operation,
                                item.edit.text});
    }
    if (!accepted.empty()) {
        accepted_by_path.emplace(path, std::move(accepted));
    }
  }
  if (liquid_edits.empty()) {
      return result;
  }
  auto preview = liquid::runtime::block_on(
      repository_->value.preview_edit(liquid::tree_edit_request{std::move(liquid_edits)},
                                      {.tool = "liquify", .operation = "structural_edit"}));
  if (!preview) {
      return std::unexpected(preview.error());
  }
  for (const auto& refused : preview->refused) {
      const auto path = std::filesystem::path{refused.target.path}.lexically_normal();
      const auto candidates = accepted_by_path.find(path);
      if (candidates == accepted_by_path.end()) {
          continue;
      }
      const auto found = std::ranges::find_if(candidates->second, [&](const auto& candidate) {
          return candidate.edit.target.node.range.start_byte ==
                     refused.target.node.range.start.byte &&
                 candidate.edit.target.node.range.end_byte == refused.target.node.range.end.byte;
      });
      if (found != candidates->second.end()) {
          result.refused.push_back({found->edit.target, refused.reason});
      }
  }
  if (!preview->changes.empty()) {
      auto applied = liquid::runtime::block_on(repository_->value.apply_edit(*preview));
      if (!applied) {
          return std::unexpected(applied.error());
      }
  }
  for (auto& [path, accepted] : accepted_by_path) {
      for (const auto& item : accepted) {
          const auto refused = std::ranges::any_of(result.refused, [&](const auto& candidate) {
              return candidate.target == item.edit.target;
          });
          if (refused) {
              continue;
          }
          auto updated = apply_source_edit(path, item.source_edit);
          if (!updated) {
              return std::unexpected(updated.error());
          }
          result.applied.push_back({path, item.edit.target.node.range});
      }
  }
  return result;
}

auto StructuralEditor::pending_drips() const -> std::uint64_t {
  if (!repository_) {
    return 0;
  }
  auto status = repository_->value.status();
  return status ? status->pending_drips : 0;
}

auto structural_edit_operation_name(StructuralEditOperation operation)
    -> std::string_view {
  switch (operation) {
  case StructuralEditOperation::replace_node:
    return "replace_node";
  case StructuralEditOperation::insert_before:
    return "insert_before";
  case StructuralEditOperation::insert_after:
    return "insert_after";
  case StructuralEditOperation::delete_node:
    return "delete_node";
  }
  return "unknown";
}

} // namespace liquify
