// SPDX-License-Identifier: MIT

#include "plugin_protocol.hpp"

#include <optional>
#include <string_view>
#include <utility>

namespace liquify::plugin_protocol {
namespace {

constexpr std::uint8_t major_unsigned = 0;
constexpr std::uint8_t major_bytes = 2;
constexpr std::uint8_t major_text = 3;
constexpr std::uint8_t major_array = 4;

class Writer {
public:
  void head(std::uint8_t major, std::uint64_t value) {
    const auto initial = static_cast<std::uint8_t>(major << 5U);
    if (value < 24U) {
      byte(initial | static_cast<std::uint8_t>(value));
    } else if (value <= 0xffU) {
      byte(initial | 24U);
      big_endian(value, 1);
    } else if (value <= 0xffffU) {
      byte(initial | 25U);
      big_endian(value, 2);
    } else if (value <= 0xffff'ffffU) {
      byte(initial | 26U);
      big_endian(value, 4);
    } else {
      byte(initial | 27U);
      big_endian(value, 8);
    }
  }

  void unsigned_value(std::uint64_t value) { head(major_unsigned, value); }

  void text(std::string_view value) {
    head(major_text, value.size());
    for (const auto character : value) {
      output_.push_back(static_cast<std::byte>(character));
    }
  }

  void bytes(std::span<const std::byte> value) {
    head(major_bytes, value.size());
    output_.insert(output_.end(), value.begin(), value.end());
  }

  void array(std::size_t count) { head(major_array, count); }

  [[nodiscard]] auto take() -> std::vector<std::byte> { return std::move(output_); }

private:
  void byte(std::uint8_t value) { output_.push_back(static_cast<std::byte>(value)); }

  void big_endian(std::uint64_t value, std::size_t width) {
    for (auto index = width; index > 0; --index) {
      byte(static_cast<std::uint8_t>(value >> ((index - 1) * 8U)));
    }
  }

  std::vector<std::byte> output_;
};

class Reader {
public:
  explicit Reader(std::span<const std::byte> input) : input_(input) {}

  [[nodiscard]] auto head(std::uint8_t major) -> std::optional<std::uint64_t> {
    if (cursor_ == input_.size()) {
      return std::nullopt;
    }
    const auto initial = std::to_integer<std::uint8_t>(input_[cursor_++]);
    if ((initial >> 5U) != major) {
      return std::nullopt;
    }
    const auto additional = static_cast<std::uint8_t>(initial & 0x1fU);
    if (additional < 24U) {
      return additional;
    }
    const auto width = additional == 24U   ? 1U
                       : additional == 25U ? 2U
                       : additional == 26U ? 4U
                       : additional == 27U ? 8U
                                           : 0U;
    if (width == 0 || width > input_.size() - cursor_) {
      return std::nullopt;
    }
    auto value = std::uint64_t{};
    for (auto index = 0U; index < width; ++index) {
      value = (value << 8U) | std::to_integer<std::uint8_t>(input_[cursor_++]);
    }
    return value;
  }

  [[nodiscard]] auto unsigned_value() -> std::optional<std::uint64_t> {
    return head(major_unsigned);
  }

  [[nodiscard]] auto text() -> std::optional<std::string> {
    auto span = payload(major_text);
    if (!span) {
      return std::nullopt;
    }
    return std::string(reinterpret_cast<const char *>(span->data()), span->size());
  }

  [[nodiscard]] auto bytes() -> std::optional<std::vector<std::byte>> {
    auto span = payload(major_bytes);
    if (!span) {
      return std::nullopt;
    }
    return std::vector<std::byte>(span->begin(), span->end());
  }

  // An array header whose count is plausible for the bytes left (each item is at least one byte).
  [[nodiscard]] auto array() -> std::optional<std::size_t> {
    auto count = head(major_array);
    if (!count || *count > input_.size() - cursor_) {
      return std::nullopt;
    }
    return static_cast<std::size_t>(*count);
  }

  [[nodiscard]] auto text_array() -> std::optional<std::vector<std::string>> {
    auto count = array();
    if (!count) {
      return std::nullopt;
    }
    auto output = std::vector<std::string>{};
    output.reserve(*count);
    for (std::size_t index = 0; index < *count; ++index) {
      auto value = text();
      if (!value) {
        return std::nullopt;
      }
      output.push_back(std::move(*value));
    }
    return output;
  }

  [[nodiscard]] auto done() const -> bool { return cursor_ == input_.size(); }

private:
  [[nodiscard]] auto payload(std::uint8_t major) -> std::optional<std::span<const std::byte>> {
    auto size = head(major);
    if (!size || *size > input_.size() - cursor_) {
      return std::nullopt;
    }
    const auto output = input_.subspan(cursor_, static_cast<std::size_t>(*size));
    cursor_ += static_cast<std::size_t>(*size);
    return output;
  }

  std::span<const std::byte> input_;
  std::size_t cursor_{};
};

void text_array(Writer &writer, const std::vector<std::string> &values) {
  writer.array(values.size());
  for (const auto &value : values) {
    writer.text(value);
  }
}

auto malformed(std::string_view what) -> std::unexpected<std::string> {
  return std::unexpected(std::string("malformed plugin host ") + std::string(what));
}

} // namespace

auto encode(const Manifest &value) -> std::vector<std::byte> {
  auto writer = Writer{};
  if (!value.error.empty()) {
    writer.array(2);
    writer.unsigned_value(0);
    writer.text(value.error);
    return writer.take();
  }
  writer.array(6);
  writer.unsigned_value(version);
  writer.text(value.name);
  text_array(writer, value.queries);
  writer.array(value.commands.size());
  for (const auto &command : value.commands) {
    writer.array(3);
    writer.text(command.name);
    writer.text(command.default_binding);
    writer.bytes(command.argument_schema);
  }
  text_array(writer, value.panels);
  text_array(writer, value.events);
  return writer.take();
}

auto encode(const Request &value) -> std::vector<std::byte> {
  auto writer = Writer{};
  writer.array(3);
  writer.unsigned_value(static_cast<std::uint64_t>(value.kind));
  writer.text(value.name);
  writer.bytes(value.input);
  return writer.take();
}

auto encode(const Response &value) -> std::vector<std::byte> {
  auto writer = Writer{};
  writer.array(3);
  writer.unsigned_value(value.status);
  writer.bytes(value.result);
  writer.text(value.message);
  return writer.take();
}

auto decode_manifest(std::span<const std::byte> frame) -> std::expected<Manifest, std::string> {
  auto reader = Reader(frame);
  const auto count = reader.array();
  const auto tag = count ? reader.unsigned_value() : std::nullopt;
  if (!tag) {
    return malformed("manifest");
  }
  auto output = Manifest{};
  if (*tag == 0 && *count == 2) {
    auto error = reader.text();
    if (!error || !reader.done()) {
      return malformed("manifest");
    }
    output.error = error->empty() ? "plugin host failed" : std::move(*error);
    return output;
  }
  if (*tag != version || *count != 6) {
    return std::unexpected(std::string("plugin host speaks another protocol version"));
  }
  auto name = reader.text();
  auto queries = name ? reader.text_array() : std::nullopt;
  auto commands = queries ? reader.array() : std::nullopt;
  if (!commands) {
    return malformed("manifest");
  }
  output.name = std::move(*name);
  output.queries = std::move(*queries);
  for (std::size_t index = 0; index < *commands; ++index) {
    auto fields = reader.array();
    auto command_name = fields && *fields == 3 ? reader.text() : std::nullopt;
    auto binding = command_name ? reader.text() : std::nullopt;
    auto schema = binding ? reader.bytes() : std::nullopt;
    if (!schema) {
      return malformed("manifest command");
    }
    output.commands.push_back({std::move(*command_name), std::move(*binding), std::move(*schema)});
  }
  auto panels = reader.text_array();
  auto events = panels ? reader.text_array() : std::nullopt;
  if (!events || !reader.done()) {
    return malformed("manifest");
  }
  output.panels = std::move(*panels);
  output.events = std::move(*events);
  return output;
}

auto decode_request(std::span<const std::byte> frame) -> std::expected<Request, std::string> {
  auto reader = Reader(frame);
  const auto count = reader.array();
  auto kind = count && *count == 3 ? reader.unsigned_value() : std::nullopt;
  auto name = kind ? reader.text() : std::nullopt;
  auto input = name ? reader.bytes() : std::nullopt;
  if (!input || !reader.done() || *kind > static_cast<std::uint64_t>(Kind::event)) {
    return malformed("request");
  }
  return Request{static_cast<Kind>(*kind), std::move(*name), std::move(*input)};
}

auto decode_response(std::span<const std::byte> frame) -> std::expected<Response, std::string> {
  auto reader = Reader(frame);
  const auto count = reader.array();
  auto status = count && *count == 3 ? reader.unsigned_value() : std::nullopt;
  auto result = status ? reader.bytes() : std::nullopt;
  auto message = result ? reader.text() : std::nullopt;
  if (!message || !reader.done() || *status > 0xffff'ffffU) {
    return malformed("response");
  }
  return Response{static_cast<std::uint32_t>(*status), std::move(*result), std::move(*message)};
}

auto frame_header(std::size_t size) -> std::array<std::byte, 4> {
  const auto value = static_cast<std::uint32_t>(size);
  return {static_cast<std::byte>(value >> 24U), static_cast<std::byte>(value >> 16U),
          static_cast<std::byte>(value >> 8U), static_cast<std::byte>(value)};
}

auto frame_size(std::span<const std::byte, 4> header) -> std::size_t {
  auto value = std::size_t{};
  for (const auto byte : header) {
    value = (value << 8U) | std::to_integer<std::uint8_t>(byte);
  }
  return value;
}

} // namespace liquify::plugin_protocol
