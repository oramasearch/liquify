// SPDX-License-Identifier: MIT

#pragma once

// The messages between liquify and `liquify-plugin-host` (liquify#82). Both sides link this file
// and nothing else in common, so the host stays free of liquid and of the IDE. Each frame is a
// 4-byte big-endian length followed by one CBOR array; the plugin's own values travel inside it
// as byte strings, unchanged.

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace liquify::plugin_protocol {

inline constexpr std::uint64_t version = 1;
// A frame larger than this is a protocol error, not a value.
inline constexpr std::size_t max_frame_bytes = 64U * 1024U * 1024U;

enum class Kind : std::uint8_t { query = 0, command = 1, panel = 2, event = 3 };

struct ManifestCommand {
  std::string name;
  std::string default_binding;
  std::vector<std::byte> argument_schema;

  auto operator==(const ManifestCommand &) const -> bool = default;
};

// What the host reports about the plugin it mapped. `error` is set instead of the rest when the
// plugin could not be loaded.
struct Manifest {
  std::string name;
  std::vector<std::string> queries;
  std::vector<ManifestCommand> commands;
  std::vector<std::string> panels;
  std::vector<std::string> events;
  std::string error;

  auto operator==(const Manifest &) const -> bool = default;
};

struct Request {
  Kind kind = Kind::query;
  std::string name;
  std::vector<std::byte> input;
};

// `status` is a LIQUID_FRONTEND_STATUS_* value; `message` is set when it is not OK.
struct Response {
  std::uint32_t status = 0;
  std::vector<std::byte> result;
  std::string message;
};

[[nodiscard]] auto encode(const Manifest &value) -> std::vector<std::byte>;
[[nodiscard]] auto encode(const Request &value) -> std::vector<std::byte>;
[[nodiscard]] auto encode(const Response &value) -> std::vector<std::byte>;

[[nodiscard]] auto decode_manifest(std::span<const std::byte> frame)
    -> std::expected<Manifest, std::string>;
[[nodiscard]] auto decode_request(std::span<const std::byte> frame)
    -> std::expected<Request, std::string>;
[[nodiscard]] auto decode_response(std::span<const std::byte> frame)
    -> std::expected<Response, std::string>;

// The 4-byte length that precedes a frame.
[[nodiscard]] auto frame_header(std::size_t size) -> std::array<std::byte, 4>;
[[nodiscard]] auto frame_size(std::span<const std::byte, 4> header) -> std::size_t;

} // namespace liquify::plugin_protocol
