// SPDX-License-Identifier: MIT

// liquify-plugin-host (liquify#82): maps one native plugin in its own process, confined by the
// operating system's sandbox, and answers liquify's requests over one channel. The plugin keeps
// the ABI v1 descriptor it always had; only its canonical-CBOR values cross to the IDE.
//
//   POSIX:   liquify-plugin-host --plugin PATH            (the channel is file descriptor 3)
//   Windows: liquify-plugin-host --plugin PATH --channel READ WRITE   (inherited pipe handles)

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <cwchar>
#include <windows.h>
#include <shellapi.h>
#else
#include <cerrno>
#include <dlfcn.h>
#include <thread>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <sandbox.h>
#endif

#if defined(__linux__)
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#endif

#include "liquid/frontend_abi.h"
#include "plugin_protocol.hpp"

namespace protocol = liquify::plugin_protocol;

namespace {

// ---------------------------------------------------------------------------------------------
// The channel
// ---------------------------------------------------------------------------------------------

#if defined(_WIN32)
using Handle = HANDLE;

auto read_exact(Handle handle, std::span<std::byte> output) -> bool {
  while (!output.empty()) {
    auto read = DWORD{};
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(output.size(), 1U << 20U));
    if (!ReadFile(handle, output.data(), chunk, &read, nullptr) || read == 0) {
      return false;
    }
    output = output.subspan(read);
  }
  return true;
}

auto write_all(Handle handle, std::span<const std::byte> input) -> bool {
  while (!input.empty()) {
    auto written = DWORD{};
    const auto chunk = static_cast<DWORD>(std::min<std::size_t>(input.size(), 1U << 20U));
    if (!WriteFile(handle, input.data(), chunk, &written, nullptr) || written == 0) {
      return false;
    }
    input = input.subspan(written);
  }
  return true;
}
#else
using Handle = int;

auto read_exact(Handle handle, std::span<std::byte> output) -> bool {
  while (!output.empty()) {
    const auto read = ::read(handle, output.data(), output.size());
    if (read < 0 && errno == EINTR) {
      continue;
    }
    if (read <= 0) {
      return false;
    }
    output = output.subspan(static_cast<std::size_t>(read));
  }
  return true;
}

auto write_all(Handle handle, std::span<const std::byte> input) -> bool {
  while (!input.empty()) {
    const auto written = ::write(handle, input.data(), input.size());
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written <= 0) {
      return false;
    }
    input = input.subspan(static_cast<std::size_t>(written));
  }
  return true;
}
#endif

struct Channel {
  Handle in;
  Handle out;

  [[nodiscard]] auto receive() const -> std::optional<std::vector<std::byte>> {
    auto header = std::array<std::byte, 4>{};
    if (!read_exact(in, header)) {
      return std::nullopt;
    }
    const auto size = protocol::frame_size(header);
    if (size > protocol::max_frame_bytes) {
      return std::nullopt;
    }
    auto frame = std::vector<std::byte>(size);
    if (!read_exact(in, frame)) {
      return std::nullopt;
    }
    return frame;
  }

  [[nodiscard]] auto send(std::span<const std::byte> frame) const -> bool {
    const auto header = protocol::frame_header(frame.size());
    return write_all(out, header) && write_all(out, frame);
  }
};

// ---------------------------------------------------------------------------------------------
// The sandbox, applied before the plugin is mapped so its initialisers are confined too.
// ---------------------------------------------------------------------------------------------

#if defined(__APPLE__)
// No network, no file writes, no new programs and no keychain. Reads stay allowed so the
// dynamic loader can map the plugin and the system libraries it links.
constexpr auto sandbox_profile = R"((version 1)
(allow default)
(deny network-outbound (remote ip))
(deny network-inbound (local ip))
(deny network-bind (local ip))
(deny file-write*)
(allow file-write* (literal "/dev/null") (literal "/dev/dtracehelper"))
(deny process-exec*)
(deny process-fork)
(deny mach-lookup (global-name "com.apple.SecurityServer") (global-name "com.apple.secd")
                  (global-name "com.apple.trustd") (global-name "com.apple.trustd.agent"))
)";

auto apply_sandbox() -> std::string {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
  char *error = nullptr;
  if (sandbox_init(sandbox_profile, 0, &error) != 0) {
    auto message = std::string("could not apply the plugin sandbox");
    if (error != nullptr) {
      message += ": ";
      message += error;
      sandbox_free_error(error);
    }
    return message;
  }
#pragma clang diagnostic pop
  return {};
}
#elif defined(__linux__)

#ifndef SYS_landlock_create_ruleset
#define SYS_landlock_create_ruleset 444
#endif
#ifndef SYS_landlock_restrict_self
#define SYS_landlock_restrict_self 446
#endif
#ifndef SECCOMP_RET_KILL_PROCESS
#define SECCOMP_RET_KILL_PROCESS 0x80000000U
#endif

// Landlock's ABI, spelled out so older kernel headers still build.
struct LandlockRuleset {
  std::uint64_t handled_access_fs;
  std::uint64_t handled_access_net;
  std::uint64_t scoped;
};

constexpr std::uint64_t landlock_execute = 1ULL << 0U;
constexpr std::uint64_t landlock_write_file = 1ULL << 1U;
constexpr std::uint64_t landlock_remove_dir = 1ULL << 4U;
constexpr std::uint64_t landlock_remove_file = 1ULL << 5U;
constexpr std::uint64_t landlock_make_char = 1ULL << 6U;
constexpr std::uint64_t landlock_make_dir = 1ULL << 7U;
constexpr std::uint64_t landlock_make_reg = 1ULL << 8U;
constexpr std::uint64_t landlock_make_sock = 1ULL << 9U;
constexpr std::uint64_t landlock_make_fifo = 1ULL << 10U;
constexpr std::uint64_t landlock_make_block = 1ULL << 11U;
constexpr std::uint64_t landlock_make_sym = 1ULL << 12U;
constexpr std::uint64_t landlock_refer = 1ULL << 13U;
constexpr std::uint64_t landlock_truncate = 1ULL << 14U;
constexpr std::uint64_t landlock_bind_tcp = 1ULL << 0U;
constexpr std::uint64_t landlock_connect_tcp = 1ULL << 1U;
constexpr std::uint64_t landlock_scope_abstract_unix = 1ULL << 0U;
constexpr std::uint64_t landlock_scope_signal = 1ULL << 1U;
constexpr std::uint32_t landlock_version_flag = 1U << 0U;

// Every write and exec right is handled and none is granted, so the plugin can create, change
// or remove no file and run no program. Returns false only when the kernel refused a ruleset it
// said it supports; a kernel without Landlock leaves the seccomp filter as the only confinement.
auto apply_landlock() -> bool {
  const auto abi = static_cast<long>(
      syscall(SYS_landlock_create_ruleset, nullptr, 0, landlock_version_flag));
  if (abi < 1) {
    return true;
  }
  auto ruleset = LandlockRuleset{};
  ruleset.handled_access_fs = landlock_execute | landlock_write_file | landlock_remove_dir |
                              landlock_remove_file | landlock_make_char | landlock_make_dir |
                              landlock_make_reg | landlock_make_sock | landlock_make_fifo |
                              landlock_make_block | landlock_make_sym;
  auto size = offsetof(LandlockRuleset, handled_access_net);
  if (abi >= 2) {
    ruleset.handled_access_fs |= landlock_refer;
  }
  if (abi >= 3) {
    ruleset.handled_access_fs |= landlock_truncate;
  }
  if (abi >= 4) {
    ruleset.handled_access_net = landlock_bind_tcp | landlock_connect_tcp;
    size = offsetof(LandlockRuleset, scoped);
  }
  if (abi >= 6) {
    ruleset.scoped = landlock_scope_abstract_unix | landlock_scope_signal;
    size = sizeof(LandlockRuleset);
  }
  const auto descriptor =
      static_cast<int>(syscall(SYS_landlock_create_ruleset, &ruleset, size, 0U));
  if (descriptor < 0) {
    return false;
  }
  const auto restricted = syscall(SYS_landlock_restrict_self, descriptor, 0U);
  ::close(descriptor);
  return restricted == 0;
}

auto apply_seccomp() -> bool {
#if defined(__x86_64__)
  constexpr auto native_arch = AUDIT_ARCH_X86_64;
#elif defined(__aarch64__)
  constexpr auto native_arch = AUDIT_ARCH_AARCH64;
#else
  return true;
#endif
#if defined(__x86_64__) || defined(__aarch64__)
  // BPF_STMT expands to a braced initialiser, so the type is spelled out.
  const sock_filter deny = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EPERM & SECCOMP_RET_DATA));
  const sock_filter allow = BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
  auto filter = std::vector<sock_filter>{};
  filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch)));
  filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, native_arch, 1, 0));
  filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_KILL_PROCESS));
  filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr)));
#if defined(__x86_64__)
  // x32 system calls share the architecture value; refuse them all.
  filter.push_back(BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, 0x40000000U, 0, 1));
  filter.push_back(deny);
#endif
  const auto denied = std::vector<unsigned>{
      __NR_execve, __NR_ptrace, __NR_process_vm_readv, __NR_process_vm_writev,
#ifdef __NR_execveat
      __NR_execveat,
#endif
#ifdef __NR_io_uring_setup
      __NR_io_uring_setup, __NR_io_uring_enter, __NR_io_uring_register,
#endif
  };
  for (const auto number : denied) {
    filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, number, 0, 1));
    filter.push_back(deny);
  }
  // socket(domain, ...) is allowed for AF_UNIX only: no network of any family.
  filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, __NR_socket, 0, 4));
  filter.push_back(BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, args)));
  filter.push_back(BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AF_UNIX, 0, 1));
  filter.push_back(allow);
  filter.push_back(BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | (EACCES & SECCOMP_RET_DATA)));
  filter.push_back(allow);
  auto program = sock_fprog{static_cast<unsigned short>(filter.size()), filter.data()};
  return prctl(PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &program, 0, 0) == 0;
#endif
}

auto apply_sandbox() -> std::string {
  if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
    return "could not apply the plugin sandbox: no_new_privs";
  }
  if (!apply_landlock()) {
    return "could not apply the plugin sandbox: landlock";
  }
  if (!apply_seccomp()) {
    return "could not apply the plugin sandbox: seccomp";
  }
  return {};
}
#else
// Windows: liquify starts this process with a low-integrity token inside a job object that
// allows no child process and is killed when liquify closes it.
auto apply_sandbox() -> std::string { return {}; }
#endif

// ---------------------------------------------------------------------------------------------
// The plugin
// ---------------------------------------------------------------------------------------------

auto load_library(const std::filesystem::path &path, std::string &error) -> void * {
#if defined(_WIN32)
  auto *library = LoadLibraryW(path.wstring().c_str());
  if (library == nullptr) {
    error = "could not load plugin";
  }
  return library;
#else
  dlerror();
  auto *library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library == nullptr) {
    const auto *detail = dlerror();
    error = detail != nullptr ? detail : "could not load plugin";
  }
  return library;
#endif
}

auto entry_point(void *library) -> liquify_plugin_entrypoint_v1 {
#if defined(_WIN32)
  return reinterpret_cast<liquify_plugin_entrypoint_v1>(
      GetProcAddress(static_cast<HMODULE>(library), LIQUIFY_PLUGIN_ENTRYPOINT_V1));
#else
  return reinterpret_cast<liquify_plugin_entrypoint_v1>(
      dlsym(library, LIQUIFY_PLUGIN_ENTRYPOINT_V1));
#endif
}

auto nonempty(const char *value) -> bool { return value != nullptr && value[0] != '\0'; }

struct Plugin {
  const liquify_plugin_v1 *descriptor{};
  liquify_plugin_event_callbacks_v1 events{};
};

// The checks only the process that holds the pointers can make. liquify repeats every check on
// names, duplicates and CBOR against the manifest, so this process is not trusted either.
auto describe(const liquify_plugin_v1 *descriptor, Plugin &plugin) -> protocol::Manifest {
  constexpr auto descriptor_size = offsetof(liquify_plugin_v1, events);
  auto output = protocol::Manifest{};
  if (descriptor == nullptr || descriptor->abi_version != LIQUIFY_PLUGIN_ABI_VERSION ||
      descriptor->struct_size < descriptor_size || !nonempty(descriptor->name) ||
      (descriptor->query_count != 0 && descriptor->queries == nullptr) ||
      (descriptor->command_count != 0 && descriptor->commands == nullptr) ||
      (descriptor->panel_count != 0 && descriptor->panels == nullptr)) {
    output.error = "plugin descriptor does not implement ABI v1";
    return output;
  }
  output.name = descriptor->name;
  for (std::size_t index = 0; index < descriptor->query_count; ++index) {
    const auto &query = descriptor->queries[index];
    if (!nonempty(query.name) || query.invoke == nullptr) {
      output = {};
      output.error = "plugin has an invalid query";
      return output;
    }
    output.queries.emplace_back(query.name);
  }
  for (std::size_t index = 0; index < descriptor->command_count; ++index) {
    const auto &command = descriptor->commands[index];
    if (!nonempty(command.name) || command.default_binding == nullptr ||
        command.invoke == nullptr ||
        (command.argument_schema_cbor.data == nullptr && command.argument_schema_cbor.size != 0)) {
      output = {};
      output.error = "plugin has an invalid command";
      return output;
    }
    const auto *schema = reinterpret_cast<const std::byte *>(command.argument_schema_cbor.data);
    output.commands.push_back(
        {command.name, command.default_binding,
         std::vector<std::byte>(schema, schema + command.argument_schema_cbor.size)});
  }
  for (std::size_t index = 0; index < descriptor->panel_count; ++index) {
    const auto &panel = descriptor->panels[index];
    if (!nonempty(panel.name) || panel.view_model == nullptr) {
      output = {};
      output.error = "plugin has an invalid panel";
      return output;
    }
    output.panels.emplace_back(panel.name);
  }
  const auto *events = descriptor->struct_size >= sizeof(liquify_plugin_v1) ? descriptor->events
                                                                            : nullptr;
  if (events != nullptr && events->abi_version == LIQUIFY_PLUGIN_EVENTS_ABI_VERSION &&
      events->struct_size >= offsetof(liquify_plugin_event_callbacks_v1, presence)) {
    const auto size = events->struct_size;
    if (size >= offsetof(liquify_plugin_event_callbacks_v1, lease) && events->presence) {
      plugin.events.presence = events->presence;
      output.events.emplace_back("presence");
    }
    if (size >= offsetof(liquify_plugin_event_callbacks_v1, attribution) && events->lease) {
      plugin.events.lease = events->lease;
      output.events.emplace_back("lease");
    }
    if (size >= sizeof(liquify_plugin_event_callbacks_v1) && events->attribution) {
      plugin.events.attribution = events->attribution;
      output.events.emplace_back("attribution");
    }
  }
  plugin.descriptor = descriptor;
  return output;
}

auto find_operation(const Plugin &plugin, protocol::Kind kind, std::string_view name)
    -> liquify_plugin_operation_v1 {
  const auto *descriptor = plugin.descriptor;
  switch (kind) {
  case protocol::Kind::query:
    for (std::size_t index = 0; index < descriptor->query_count; ++index) {
      if (name == descriptor->queries[index].name) {
        return descriptor->queries[index].invoke;
      }
    }
    break;
  case protocol::Kind::command:
    for (std::size_t index = 0; index < descriptor->command_count; ++index) {
      if (name == descriptor->commands[index].name) {
        return descriptor->commands[index].invoke;
      }
    }
    break;
  case protocol::Kind::panel:
    for (std::size_t index = 0; index < descriptor->panel_count; ++index) {
      if (name == descriptor->panels[index].name) {
        return descriptor->panels[index].view_model;
      }
    }
    break;
  case protocol::Kind::event:
    break;
  }
  return nullptr;
}

auto invoke(const Plugin &plugin, liquify_plugin_operation_v1 operation,
            std::span<const std::byte> input) -> protocol::Response {
  auto output = liquid_frontend_owned_bytes_v1{};
  auto error = liquid_frontend_error_v1{};
  const auto callbacks = liquid_frontend_callbacks_v1{};
  auto status = int{};
  try {
    status = operation(plugin.descriptor->plugin_context,
                       {reinterpret_cast<const std::uint8_t *>(input.data()), input.size()},
                       &callbacks, &output, &error);
  } catch (const std::exception &exception) {
    return {LIQUID_FRONTEND_STATUS_INTERNAL, {},
            std::string("plugin operation threw an exception: ") + exception.what()};
  } catch (...) {
    return {LIQUID_FRONTEND_STATUS_INTERNAL, {}, "plugin operation threw an exception"};
  }
  auto response = protocol::Response{};
  if (status != LIQUID_FRONTEND_STATUS_OK) {
    response.status = status > 0 ? static_cast<std::uint32_t>(status)
                                 : static_cast<std::uint32_t>(LIQUID_FRONTEND_STATUS_INTERNAL);
    response.message = error.message != nullptr ? error.message : "plugin operation failed";
  } else if (output.data == nullptr || output.release == nullptr) {
    response.status = LIQUID_FRONTEND_STATUS_INTERNAL;
    response.message = "plugin returned invalid owned bytes";
  } else {
    const auto *data = reinterpret_cast<const std::byte *>(output.data);
    response.result.assign(data, data + output.size);
  }
  if (output.data != nullptr && output.release != nullptr) {
    output.release(output.context, output.data, output.size);
  }
  return response;
}

auto deliver(const Plugin &plugin, std::string_view name, std::span<const std::byte> input)
    -> protocol::Response {
  const auto callback = name == "presence"      ? plugin.events.presence
                        : name == "lease"       ? plugin.events.lease
                        : name == "attribution" ? plugin.events.attribution
                                                : nullptr;
  if (callback == nullptr) {
    return {LIQUID_FRONTEND_STATUS_UNSUPPORTED, {}, "plugin does not receive this event"};
  }
  try {
    callback(plugin.descriptor->plugin_context,
             {reinterpret_cast<const std::uint8_t *>(input.data()), input.size()});
  } catch (...) {
    return {LIQUID_FRONTEND_STATUS_INTERNAL, {}, "plugin event threw an exception"};
  }
  return {};
}

auto fail(const Channel &channel, std::string message) -> int {
  auto manifest = protocol::Manifest{};
  manifest.error = std::move(message);
  (void)channel.send(protocol::encode(manifest));
  return 1;
}

#if !defined(_WIN32)
// The host never outlives liquify, even when the plugin is stuck in its own code.
void watch_parent() {
  const auto parent = getppid();
  std::thread([parent] {
    while (getppid() == parent) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    _exit(1);
  }).detach();
}
#endif

} // namespace

auto main(int argc, char **argv) -> int {
  auto plugin_path = std::filesystem::path{};
#if defined(_WIN32)
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
  auto channel = Channel{INVALID_HANDLE_VALUE, INVALID_HANDLE_VALUE};
  auto wide_count = 0;
  auto **wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
  for (int index = 1; wide != nullptr && index < wide_count; ++index) {
    const auto argument = std::wstring_view(wide[index]);
    if (argument == L"--plugin" && index + 1 < wide_count) {
      plugin_path = wide[++index];
    } else if (argument == L"--channel" && index + 2 < wide_count) {
      channel.in = reinterpret_cast<HANDLE>(
          static_cast<std::uintptr_t>(std::wcstoull(wide[index + 1], nullptr, 10)));
      channel.out = reinterpret_cast<HANDLE>(
          static_cast<std::uintptr_t>(std::wcstoull(wide[index + 2], nullptr, 10)));
      index += 2;
    }
  }
  LocalFree(wide);
  (void)argc;
  (void)argv;
  if (channel.in == INVALID_HANDLE_VALUE || channel.out == INVALID_HANDLE_VALUE) {
    return 2;
  }
#else
  const auto channel = Channel{3, 3};
  for (int index = 1; index < argc; ++index) {
    if (std::string_view(argv[index]) == "--plugin" && index + 1 < argc) {
      plugin_path = argv[++index];
    }
  }
  watch_parent();
#endif
  if (plugin_path.empty()) {
    return fail(channel, "plugin host needs --plugin PATH");
  }
  if (auto error = apply_sandbox(); !error.empty()) {
    return fail(channel, std::move(error));
  }
  auto error = std::string{};
  auto *library = load_library(plugin_path, error);
  if (library == nullptr) {
    return fail(channel, error + ": " + plugin_path.string());
  }
  const auto symbol = entry_point(library);
  if (symbol == nullptr) {
    return fail(channel, "plugin entry point is missing: " + plugin_path.string());
  }
  const liquify_plugin_v1 *descriptor = nullptr;
  try {
    descriptor = symbol();
  } catch (...) {
    return fail(channel, "plugin entry point threw an exception: " + plugin_path.string());
  }
  auto plugin = Plugin{};
  auto manifest = describe(descriptor, plugin);
  if (!manifest.error.empty()) {
    return fail(channel, std::move(manifest.error));
  }
  if (!channel.send(protocol::encode(manifest))) {
    return 1;
  }
  while (auto frame = channel.receive()) {
    auto request = protocol::decode_request(*frame);
    if (!request) {
      break;
    }
    auto response = protocol::Response{};
    if (request->kind == protocol::Kind::event) {
      response = deliver(plugin, request->name, request->input);
    } else if (const auto operation = find_operation(plugin, request->kind, request->name)) {
      response = invoke(plugin, operation, request->input);
    } else {
      response = {LIQUID_FRONTEND_STATUS_INVALID_ARGUMENT, {}, "plugin operation is not registered"};
    }
    if (!channel.send(protocol::encode(response))) {
      break;
    }
  }
  if (plugin.descriptor->destroy != nullptr) {
    plugin.descriptor->destroy(plugin.descriptor->plugin_context);
  }
  // The library stays mapped until the process ends: a plugin's own threads may still run.
  return 0;
}
