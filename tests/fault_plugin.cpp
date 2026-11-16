// SPDX-License-Identifier: MIT

// A plugin that misbehaves on purpose (liquify#82): it crashes, hangs, and tries to write a
// file, open a network socket and read the environment, so the tests can show each is confined
// to its `liquify-plugin-host` process. Its hanging panel and command (`fault.hang`,
// `fault.sleep`) also show the window never waits for them (liquify#149).

#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include "liquid/frontend_abi.h"

#if !defined(_WIN32)
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

void release_bytes(void *, std::uint8_t *data, std::size_t) { std::free(data); }

auto copy_result(const std::uint8_t *value, std::size_t size,
                 liquid_frontend_owned_bytes_v1 *output) -> int {
  auto *data = static_cast<std::uint8_t *>(std::malloc(size));
  if (data == nullptr) {
    return LIQUID_FRONTEND_STATUS_INTERNAL;
  }
  std::memcpy(data, value, size);
  *output = {data, size, release_bytes, nullptr};
  return LIQUID_FRONTEND_STATUS_OK;
}

auto boolean(bool value, liquid_frontend_owned_bytes_v1 *output) -> int {
  const std::uint8_t encoded = value ? 0xf5 : 0xf4;
  return copy_result(&encoded, 1, output);
}

// A CBOR text string argument (the tests send a path).
auto text_argument(liquid_frontend_bytes_v1 input) -> std::string {
  if (input.size == 0) {
    return {};
  }
  const auto initial = input.data[0];
  auto size = std::size_t{};
  auto offset = std::size_t{1};
  if (initial >= 0x60 && initial <= 0x77) {
    size = initial - 0x60U;
  } else if (initial == 0x78 && input.size >= 2) {
    size = input.data[1];
    offset = 2;
  } else if (initial == 0x79 && input.size >= 3) {
    size = (static_cast<std::size_t>(input.data[1]) << 8U) | input.data[2];
    offset = 3;
  } else {
    return {};
  }
  if (input.size < offset + size) {
    return {};
  }
  return {reinterpret_cast<const char *>(input.data + offset), size};
}

auto crash(void *, liquid_frontend_bytes_v1, const liquid_frontend_callbacks_v1 *,
           liquid_frontend_owned_bytes_v1 *, liquid_frontend_error_v1 *) -> int {
  std::abort();
}

auto hang(void *, liquid_frontend_bytes_v1, const liquid_frontend_callbacks_v1 *,
          liquid_frontend_owned_bytes_v1 *, liquid_frontend_error_v1 *) -> int {
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}

auto healthy(void *, liquid_frontend_bytes_v1, const liquid_frontend_callbacks_v1 *,
             liquid_frontend_owned_bytes_v1 *output, liquid_frontend_error_v1 *) -> int {
  static constexpr std::uint8_t model[] = {
      0xa3, 0x64, 'k', 'i', 'n', 'd', 0x64, 't', 'e', 'x', 't', 0x64, 't', 'e', 'x', 't', 0x62,
      'o',  'k',  0x65, 't', 'i', 't', 'l', 'e', 0x67, 'H', 'e', 'a', 'l', 't', 'h', 'y'};
  return copy_result(model, sizeof(model), output);
}

auto write_file(void *, liquid_frontend_bytes_v1 arguments, const liquid_frontend_callbacks_v1 *,
                liquid_frontend_owned_bytes_v1 *output, liquid_frontend_error_v1 *) -> int {
  const auto path = text_argument(arguments);
  auto *file = path.empty() ? nullptr : std::fopen(path.c_str(), "wb");
  if (file != nullptr) {
    std::fclose(file);
  }
  return boolean(file != nullptr, output);
}

auto open_network(void *, liquid_frontend_bytes_v1, const liquid_frontend_callbacks_v1 *,
                  liquid_frontend_owned_bytes_v1 *output, liquid_frontend_error_v1 *) -> int {
#if defined(_WIN32)
  return boolean(false, output);
#else
  const auto descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
  auto opened = descriptor >= 0;
  if (opened) {
    auto address = sockaddr_in{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // Refused (nothing listens) still proves the connection was attempted; a sandbox denies it
    // with EPERM or EACCES before it leaves the process.
    const auto connected =
        ::connect(descriptor, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
    opened = connected == 0 || errno == ECONNREFUSED;
    ::close(descriptor);
  }
  return boolean(opened, output);
#endif
}

auto read_environment(void *, liquid_frontend_bytes_v1, const liquid_frontend_callbacks_v1 *,
                      liquid_frontend_owned_bytes_v1 *output, liquid_frontend_error_v1 *) -> int {
  return boolean(std::getenv("LIQUIFY_PLUGIN_TEST_SECRET") != nullptr, output);
}

constexpr std::uint8_t empty_schema[] = {0xa0};

const liquify_plugin_command_v1 commands[] = {
    {"fault.crash", "", {empty_schema, sizeof(empty_schema)}, crash},
    {"fault.sleep", "", {empty_schema, sizeof(empty_schema)}, hang},
    {"fault.write", "", {empty_schema, sizeof(empty_schema)}, write_file},
    {"fault.network", "", {empty_schema, sizeof(empty_schema)}, open_network},
    {"fault.environment", "", {empty_schema, sizeof(empty_schema)}, read_environment},
};

const liquify_plugin_panel_v1 panels[] = {{"fault.hang", hang}, {"fault.healthy", healthy}};

const liquify_plugin_v1 descriptor = {LIQUIFY_PLUGIN_ABI_VERSION,
                                      sizeof(liquify_plugin_v1),
                                      "fault",
                                      nullptr,
                                      0,
                                      commands,
                                      sizeof(commands) / sizeof(commands[0]),
                                      panels,
                                      sizeof(panels) / sizeof(panels[0]),
                                      nullptr,
                                      nullptr,
                                      nullptr};

} // namespace

extern "C" LIQUIFY_PLUGIN_EXPORT auto liquify_plugin_get_v1() -> const liquify_plugin_v1 * {
  return &descriptor;
}
