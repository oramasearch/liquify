#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "liquid/repository.hpp"
#include "liquid/runtime.hpp"
#include "liquify/structural_edit.hpp"

namespace {

class TemporaryDirectory {
public:
  explicit TemporaryDirectory(std::string_view name) {
    const auto suffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            (std::string(name) + "-" + std::to_string(suffix));
    std::filesystem::remove_all(path_);
  }

  ~TemporaryDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path_, error);
  }

  TemporaryDirectory(const TemporaryDirectory &) = delete;
  auto operator=(const TemporaryDirectory &) -> TemporaryDirectory & = delete;

  [[nodiscard]] auto path() const -> const std::filesystem::path & {
    return path_;
  }

private:
  std::filesystem::path path_;
};

auto bytes(std::string_view value) -> std::vector<std::byte> {
  const auto source = std::as_bytes(std::span(value.data(), value.size()));
  return {source.begin(), source.end()};
}

auto text(std::span<const std::byte> value) -> std::string {
  const auto *characters = reinterpret_cast<const char *>(value.data());
  return std::string(characters, value.size());
}

auto read_file(const std::filesystem::path &path) -> std::vector<std::byte> {
  auto input = std::ifstream(path, std::ios::binary);
  const auto characters =
      std::vector<char>(std::istreambuf_iterator<char>(input), {});
  const auto input_bytes =
      std::as_bytes(std::span(characters.data(), characters.size()));
  return {input_bytes.begin(), input_bytes.end()};
}

void write_file(const std::filesystem::path &path,
                std::span<const std::byte> contents) {
  std::filesystem::create_directories(path.parent_path());
  auto output = std::ofstream(path, std::ios::binary);
  output.write(reinterpret_cast<const char *>(contents.data()),
               static_cast<std::streamsize>(contents.size()));
}

auto create_repository(
    const std::filesystem::path &path,
    std::span<const std::pair<std::string, std::string>> files)
    -> liquid::repository {
  auto created = liquid::runtime::block_on(liquid::repository::create(path));
  EXPECT_TRUE(created) << created.error().message;
  auto repository = std::move(*created);
  for (const auto &[name, contents] : files) {
    auto written =
        liquid::runtime::block_on(repository.write(name, bytes(contents)));
    EXPECT_TRUE(written) << written.error().message;
  }
  return repository;
}

auto byte_of(std::string_view source, std::string_view needle,
             std::size_t occurrence = 0) -> std::uint32_t {
  auto position = std::size_t{};
  for (std::size_t index = 0; index <= occurrence; ++index) {
    position = source.find(needle, position);
    EXPECT_NE(position, std::string_view::npos);
    if (position == std::string_view::npos) {
      return 0;
    }
    ++position;
  }
  return static_cast<std::uint32_t>(position - 1);
}

struct StructuralFixture {
  TemporaryDirectory location{"liquify-structural"};
  liquid::result<liquify::InteractiveAnalysis> analysis{
      liquify::InteractiveAnalysis::open(location.path() / "graph")};

  [[nodiscard]] auto editor(const std::filesystem::path &repository)
      -> liquify::StructuralEditor {
    EXPECT_TRUE(analysis) << analysis.error().message;
    return liquify::StructuralEditor(repository, *analysis);
  }
};

TEST(StructuralEdit, ExpandsSelectionThroughSyntaxAncestors) {
  TemporaryDirectory repository_path{"liquify-selection-repository"};
  const auto source =
      std::string{"int calculate(int value) { return value + 1; }\n"};
  const auto files =
      std::array{std::pair{std::string{"selection.cpp"}, source}};
  auto repository = create_repository(repository_path.path(), files);
  static_cast<void>(repository);
  StructuralFixture fixture;
  auto editor = fixture.editor(repository_path.path());
  ASSERT_TRUE(
      editor.open_buffer("selection.cpp", liquify::StubLanguage::cpp, source));

  auto chain =
      editor.selection_chain("selection.cpp", byte_of(source, "value", 1));
  ASSERT_TRUE(chain) << chain.error().message;
  ASSERT_GE(chain->size(), 4U);
  EXPECT_EQ(chain->front().kind, "identifier");
  EXPECT_TRUE(std::ranges::any_of(*chain, [](const auto &node) {
    return node.kind == "function_definition";
  }));
  for (std::size_t index = 1; index < chain->size(); ++index) {
    EXPECT_LE((*chain)[index].range.start_byte,
              (*chain)[index - 1].range.start_byte);
    EXPECT_GE((*chain)[index].range.end_byte,
              (*chain)[index - 1].range.end_byte);
  }
}

TEST(StructuralEdit, RenamesAcrossFilesAsOneDripEachAndListsStringOccurrence) {
  TemporaryDirectory repository_path{"liquify-rename-repository"};
  const auto first = std::string{"int target(int value) { return value; "
                                 "}\nconst char* label = \"target\";\n"};
  const auto second =
      std::string{"int target(int value);\nint use() { return target(1); }\n"};
  const auto files = std::array{std::pair{std::string{"first.cpp"}, first},
                                std::pair{std::string{"second.cpp"}, second}};
  auto repository = create_repository(repository_path.path(), files);
  StructuralFixture fixture;
  auto editor = fixture.editor(repository_path.path());
  ASSERT_TRUE(
      editor.open_buffer("first.cpp", liquify::StubLanguage::cpp, first));
  ASSERT_TRUE(
      editor.open_buffer("second.cpp", liquify::StubLanguage::cpp, second));

  auto selections =
      editor.select_references("first.cpp", byte_of(first, "target"));
  ASSERT_TRUE(selections) << selections.error().message;
  ASSERT_EQ(selections->size(), 3U);
  for (const auto &selection : *selections) {
    const auto &contents = selection.path == "first.cpp" ? first : second;
    EXPECT_EQ(
        contents.substr(selection.range.start_byte,
                        selection.range.end_byte - selection.range.start_byte),
        "target");
  }

  auto preview =
      editor.rename("first.cpp", byte_of(first, "target"), "renamed");
  ASSERT_TRUE(preview) << preview.error().message;
  EXPECT_EQ(preview->symbol, "target");
  ASSERT_EQ(preview->edits.size(), 3U);
  ASSERT_EQ(preview->unresolved.size(), 1U);
  EXPECT_EQ(preview->unresolved.front().path, "first.cpp");

  auto applied = editor.apply(preview->edits);
  ASSERT_TRUE(applied) << applied.error().message;
  EXPECT_EQ(applied->applied.size(), 3U);
  EXPECT_TRUE(applied->refused.empty());
  auto reopened = liquid::runtime::block_on(
      liquid::repository::open(repository_path.path()));
  ASSERT_TRUE(reopened) << reopened.error().message;
  auto first_after = liquid::runtime::block_on(reopened->read("first.cpp"));
  auto second_after = liquid::runtime::block_on(reopened->read("second.cpp"));
  ASSERT_TRUE(first_after);
  ASSERT_TRUE(second_after);
  EXPECT_EQ(text(*first_after), "int renamed(int value) { return value; "
                                "}\nconst char* label = \"target\";\n");
  EXPECT_EQ(text(*second_after),
            "int renamed(int value);\nint use() { return renamed(1); }\n");
  EXPECT_EQ(editor.pending_drips(), 2U);
}

TEST(StructuralEdit, RefusesAStaleNodeFingerprint) {
  TemporaryDirectory repository_path{"liquify-stale-repository"};
  const auto source = std::string{"int current = 1;\n"};
  const auto files = std::array{std::pair{std::string{"stale.cpp"}, source}};
  auto repository = create_repository(repository_path.path(), files);
  static_cast<void>(repository);
  StructuralFixture fixture;
  auto editor = fixture.editor(repository_path.path());
  ASSERT_TRUE(
      editor.open_buffer("stale.cpp", liquify::StubLanguage::cpp, source));
  auto chain = editor.selection_chain("stale.cpp", byte_of(source, "current"));
  ASSERT_TRUE(chain);
  auto edit = editor.make_edit("stale.cpp", chain->front(),
                               liquify::StructuralEditOperation::replace_node,
                               "renamed");
  ASSERT_TRUE(edit);
  const auto start = byte_of(source, "current");
  auto changed =
      editor.apply_source_edit("stale.cpp", {start, start + 7, "changed"});
  ASSERT_TRUE(changed);

  const auto edits = std::array{*edit};
  auto applied = editor.apply(edits);
  ASSERT_TRUE(applied) << applied.error().message;
  EXPECT_TRUE(applied->applied.empty());
  ASSERT_EQ(applied->refused.size(), 1U);
  EXPECT_EQ(applied->refused.front().reason,
            "target syntax node fingerprint is stale");
}

TEST(StructuralEdit, ExtractsCompilingCppFunctionWithAnExternalParameter) {
  TemporaryDirectory repository_path{"liquify-extract-repository"};
  const auto source = std::string{"int calculate(int input) {\n"
                                  "    int total = input;\n"
                                  "    total += 1;\n"
                                  "    total += 2;\n"
                                  "    return total;\n"
                                  "}\n"};
  const auto files = std::array{std::pair{std::string{"extract.cpp"}, source}};
  auto repository = create_repository(repository_path.path(), files);
  static_cast<void>(repository);
  StructuralFixture fixture;
  auto editor = fixture.editor(repository_path.path());
  ASSERT_TRUE(
      editor.open_buffer("extract.cpp", liquify::StubLanguage::cpp, source));
  const auto start = byte_of(source, "total += 1");
  const auto end = static_cast<std::uint32_t>(
      byte_of(source, "total += 2") + std::string_view("total += 2;").size());
  auto preview = editor.extract_function(
      "extract.cpp", {start, end, 2, 4, 3, 15}, "increase_total");
  ASSERT_TRUE(preview) << preview.error().message;
  EXPECT_TRUE(preview->diagnostics.empty());
  ASSERT_EQ(preview->edits.size(), 3U);
  auto applied = editor.apply(preview->edits);
  ASSERT_TRUE(applied) << applied.error().message;
  EXPECT_EQ(applied->applied.size(), 3U);

  auto reopened = liquid::runtime::block_on(
      liquid::repository::open(repository_path.path()));
  ASSERT_TRUE(reopened);
  auto extracted = liquid::runtime::block_on(reopened->read("extract.cpp"));
  ASSERT_TRUE(extracted);
  const auto extracted_text = text(*extracted);
  EXPECT_NE(extracted_text.find("static void increase_total(int& total)"),
            std::string::npos);
  EXPECT_NE(extracted_text.find("increase_total(total);"), std::string::npos);
  const auto source_path = repository_path.path() / "extracted.cpp";
  write_file(source_path, *extracted);
  const auto command =
      std::string{"\"" LIQUIFY_CXX_COMPILER "\" -std=c++20 -fsyntax-only \""} +
      source_path.string() + "\"";
  EXPECT_EQ(std::system(command.c_str()), 0);
}

TEST(StructuralEdit, ExtractsCompilingCFunctionWithAWrittenPointerParameter) {
  TemporaryDirectory repository_path{"liquify-extract-c-repository"};
  const auto source = std::string{"int calculate(int input) {\n"
                                  "    int total = input;\n"
                                  "    total += 1;\n"
                                  "    total += 2;\n"
                                  "    return total;\n"
                                  "}\n"};
  const auto files = std::array{std::pair{std::string{"extract.c"}, source}};
  auto repository = create_repository(repository_path.path(), files);
  static_cast<void>(repository);
  StructuralFixture fixture;
  auto editor = fixture.editor(repository_path.path());
  ASSERT_TRUE(
      editor.open_buffer("extract.c", liquify::StubLanguage::c, source));
  const auto start = byte_of(source, "total += 1");
  const auto end = static_cast<std::uint32_t>(
      byte_of(source, "total += 2") + std::string_view("total += 2;").size());
  auto preview = editor.extract_function("extract.c", {start, end, 2, 4, 3, 15},
                                         "increase_total");
  ASSERT_TRUE(preview) << preview.error().message;
  EXPECT_TRUE(preview->diagnostics.empty());
  auto applied = editor.apply(preview->edits);
  ASSERT_TRUE(applied) << applied.error().message;

  auto reopened = liquid::runtime::block_on(
      liquid::repository::open(repository_path.path()));
  ASSERT_TRUE(reopened);
  auto extracted = liquid::runtime::block_on(reopened->read("extract.c"));
  ASSERT_TRUE(extracted);
  const auto extracted_text = text(*extracted);
  EXPECT_NE(extracted_text.find("static void increase_total(int* total)"),
            std::string::npos);
  EXPECT_NE(extracted_text.find("(*total) += 1;"), std::string::npos);
  EXPECT_NE(extracted_text.find("increase_total(&total);"), std::string::npos);
  const auto source_path = repository_path.path() / "extracted.c";
  write_file(source_path, *extracted);
  const auto command =
      std::string{"\"" LIQUIFY_C_COMPILER "\" -std=c17 -fsyntax-only \""} +
      source_path.string() + "\"";
  EXPECT_EQ(std::system(command.c_str()), 0);
}

TEST(StructuralEdit, CanonicalCborRoundTripsThroughHeadlessCommand) {
  TemporaryDirectory repository_path{"liquify-headless-repository"};
  TemporaryDirectory files_path{"liquify-headless-files"};
  const auto source = std::string{"int old_name = 1;\n"};
  const auto files = std::array{std::pair{std::string{"headless.cpp"}, source}};
  auto repository = create_repository(repository_path.path(), files);
  static_cast<void>(repository);
  StructuralFixture fixture;
  auto editor = fixture.editor(repository_path.path());
  ASSERT_TRUE(
      editor.open_buffer("headless.cpp", liquify::StubLanguage::cpp, source));
  auto chain =
      editor.selection_chain("headless.cpp", byte_of(source, "old_name"));
  ASSERT_TRUE(chain);
  auto edit = editor.make_edit("headless.cpp", chain->front(),
                               liquify::StructuralEditOperation::replace_node,
                               "new_name");
  ASSERT_TRUE(edit);
  const auto edits = std::array{*edit};
  auto request = liquify::encode_structural_edit_request(edits);
  ASSERT_TRUE(request) << request.error().message;
  auto decoded = liquify::decode_structural_edit_request(*request);
  ASSERT_TRUE(decoded) << decoded.error().message;
  auto reencoded = liquify::encode_structural_edit_request(*decoded);
  ASSERT_TRUE(reencoded);
  EXPECT_EQ(*request, *reencoded);

  const auto request_path = files_path.path() / "request.cbor";
  const auto result_path = files_path.path() / "result.cbor";
  write_file(request_path, *request);
  const auto command =
      std::string{"\"" LIQUIFY_EXECUTABLE "\" --apply-edits \""} +
      request_path.string() + "\" \"" + repository_path.path().string() +
      "\" > \"" + result_path.string() + "\"";
  ASSERT_EQ(std::system(command.c_str()), 0);
  auto result = liquify::decode_structural_edit_result(read_file(result_path));
  ASSERT_TRUE(result) << result.error().message;
  EXPECT_EQ(result->applied.size(), 1U);
  EXPECT_TRUE(result->refused.empty());
  auto reopened = liquid::runtime::block_on(
      liquid::repository::open(repository_path.path()));
  auto changed = liquid::runtime::block_on(reopened->read("headless.cpp"));
  ASSERT_TRUE(changed);
  EXPECT_EQ(text(*changed), "int new_name = 1;\n");
}

} // namespace
